#include "game/Session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "epsilon/AstroLayers.h"
#include "game/Armament.h"
#include "game/Units.h"

namespace game {

using namespace eps;
using namespace eps::astro;
using Kind = TargetRef::Kind;

namespace {

constexpr int kContactEvery = 15;   // pasos entre lecturas de sensores
constexpr std::size_t kMaxContacts = 24;
constexpr std::size_t kMaxLog = 5;
constexpr double kWreckSeconds = 7.0;  // lo que dura la destruccion antes de la pantalla final
constexpr double kShutdownSeconds = 14.0;  // lo que dura la cinematica de la nave apagada
constexpr double kAlignedAngle = 0.06; // radianes: por debajo, el piloto ya puede acelerar
constexpr double kGravity = 6.674e-20; // km3 / kg / s2
constexpr double kSoftTouch = 1.0;     // km sobre la superficie: la nave mide 1,2 km
constexpr double kStationClearing = 15.0;  // km sin rocas alrededor de una mina en un cumulo
constexpr double kWarpRouteMin = 5.0e4;  // km: por debajo, la ruta sigue a impulso
constexpr double kCurveRouteMin = 1.0e10;  // km: por encima, sale de un sistema y merece una burbuja

std::uint64_t ScanKey(const TargetRef& t) {
    return static_cast<std::uint64_t>(t.kind) << 48 | static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.system)) << 24 | static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.index) & 0xFFFFFFu);
}

std::string Fixed(double value, int decimals = 1) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

const Body* FindPlanet(const World& w, int system, int data) {
    for (const int b : w.state[static_cast<std::size_t>(system)].bodies) {
        const Body& body = w.bodies[static_cast<std::size_t>(b)];
        if (body.data == data) return &body;
    }
    return nullptr;
}

const AsteroidBelt* FindBelt(const World& w, int system, int beltData, int* slot = nullptr) {
    const SystemState& state = w.state[static_cast<std::size_t>(system)];
    if (beltData < 0 || static_cast<std::size_t>(beltData) >= state.beltBodies.size()) return nullptr;
    const int body = state.beltBodies[static_cast<std::size_t>(beltData)];
    if (body < 0) return nullptr;
    const int index = w.bodies[static_cast<std::size_t>(body)].belt;
    if (slot) *slot = index;
    return &w.belts[static_cast<std::size_t>(index)];
}

// Velocidad en el mundo de lo que sigue una orbita.
Vec3d BodyVelocity(const World& w, const Body& body, double time) {
    if (body.orbit < 0) return {};
    const KeplerOrbit& orbit = w.orbits[static_cast<std::size_t>(body.orbit)];
    if (orbit.node == kNoNode) return {};
    return w.scene.World(w.scene.Parent(orbit.node)).TransformDirection(OrbitVelocity(orbit, time));
}

// Rejilla de espesor: la niebla cambia de una zona a otra, sin llegar a despejarse.
double NebulaDensity(const Vec3d& absolute) {
    constexpr double kCell = 5.0e10;
    const Vec3d p = absolute * (1.0 / kCell);
    const double a = std::sin(p.x * 1.3 + 0.7) * std::cos(p.z * 0.9 - 1.1);
    const double b = std::sin(p.y * 1.7 + p.z * 0.6) * 0.5;
    return std::clamp(1.0 + 0.45 * a + 0.35 * b, 0.45, 1.8);
}

}  // namespace

GameSession::GameSession(Engine& engine, const Balance& rules, std::uint32_t seed, GameModeId mode)
    : engine_(engine), rules_(rules), world_(rules, seed, mode), bubble_(seed) {
    fleet_.emplace_back();
    crews_.emplace_back();
    Use(0);
    vessel_->id = nextVessel_++;
    vessel_->ship = MakeShip(rules);
    World& w = engine_.GetWorld();
    world_.anomalies = GenerateAnomalies(w.extents, rules_.anomalies, seed);
    ResizeSpecies(world_.species, w.extents.size());
    w.scene.UpdateWorld(engine_.Jobs());
    pilot_->known.assign(w.state.size(), 0);
    pilot_->known[0] = 1;
    pilot_->visited = pilot_->known;
    PlaceAtStart();
    pilot_->dialogue.Start(MakeTutorial());
}

void GameSession::PlaceAtStart() { PlaceInSystem(0); }

// A medio camino de la estrella de ese sistema, dentro de el.
void GameSession::PlaceInSystem(int system) {
    const World& w = engine_.GetWorld();
    const std::size_t i = static_cast<std::size_t>(system);
    const Vec3d center = w.scene.WorldPosition(w.sectors[i]);
    vessel_->ship.position = center + Vec3d{w.extents[i] * 0.25, 0.0, -w.extents[i] * 0.15};
    const Vec3d toStar = Normalize(center - vessel_->ship.position);
    FaceTo(vessel_->ship, toStar);
    vessel_->ship.moveDirection = toStar;
    vessel_->ship.warpHeading = toStar;
}

void GameSession::Say(const std::string& text, Audience) {
    pilot_->log.push_back(text);
    while (pilot_->log.size() > kMaxLog) pilot_->log.pop_front();
}

double GameSession::Visibility() const {
    const World& w = engine_.GetWorld();
    return rules_.world.fogDistance / NebulaDensity(vessel_->ship.position + w.origin);
}

// Grandes zonas de un color a otro. Cambia despacio: hay que viajar para notarlo.
float GameSession::NebulaTone() const {
    const Vec3d p = (vessel_->ship.position + engine_.GetWorld().origin) * (1.0 / 1.0e11);
    return static_cast<float>(std::clamp(0.5 + 0.36 * std::sin(p.x * 1.1 + 0.4) * std::cos(p.z * 0.8 - 0.6) + 0.14 * std::sin(p.y * 1.4 + p.z * 0.5), 0.0, 1.0));
}

bool GameSession::KnownSystem(int system) const { return system >= 0 && static_cast<std::size_t>(system) < pilot_->known.size() && pilot_->known[static_cast<std::size_t>(system)]; }

int GameSession::KnownSystems() const {
    int count = 0;
    for (const char k : pilot_->known) count += k ? 1 : 0;
    return count;
}

bool GameSession::Visited(int system) const { return system >= 0 && static_cast<std::size_t>(system) < pilot_->visited.size() && pilot_->visited[static_cast<std::size_t>(system)]; }

int GameSession::VisitedSystems() const {
    int count = 0;
    for (const char v : pilot_->visited) count += v ? 1 : 0;
    return count;
}

std::string GameSession::SystemName(int system) const { return "SYS-" + std::to_string(system); }

std::vector<AtlasRow> GameSession::AtlasRows(int system) const {
    std::vector<AtlasRow> rows;
    const World& w = engine_.GetWorld();
    if (system < 0 || static_cast<std::size_t>(system) >= w.systems.size()) return rows;
    const StarSystemData& data = *w.systems[static_cast<std::size_t>(system)];
    const std::string base = SystemName(system);

    for (int m = 0; m < data.StarCount(); ++m) {
        AtlasRow row;
        row.target = {Kind::Star, system, m};
        row.known = Scanned(row.target);
        row.name = data.StarCount() > 1 ? base + " " + static_cast<char>('A' + m) : base;
        row.detail = row.known ? Info(data.StarAt(m).kind).name : "Sin analizar";
        rows.push_back(row);
    }
    const auto addBody = [&](int index, int depth, int order) {
        const BodyData& body = data.bodies[static_cast<std::size_t>(index)];
        AtlasRow row;
        row.target = {Kind::Planet, system, index};
        row.depth = depth;
        row.known = Scanned(row.target);
        const Body* loaded = FindPlanet(w, system, index);
        row.name = loaded && !loaded->name.empty() ? loaded->name : base + (depth == 0 ? " " + std::string(1, static_cast<char>('b' + order % 24)) : " luna " + std::to_string(order + 1));
        row.detail = row.known ? Name(body.kind) : "Sin analizar";
        rows.push_back(row);
    };
    int planet = 0;
    for (int b = 0; b < static_cast<int>(data.bodies.size()); ++b) {
        const BodyData& body = data.bodies[static_cast<std::size_t>(b)];
        if (body.parent >= 0 || body.destroyed || body.kind == PlanetClass::Comet || body.kind == PlanetClass::Asteroid) continue;
        addBody(b, 0, planet++);
        int moon = 0;
        for (int c = b + 1; c < static_cast<int>(data.bodies.size()); ++c) {
            const BodyData& candidate = data.bodies[static_cast<std::size_t>(c)];
            if (candidate.parent == b && !candidate.destroyed) addBody(c, 1, moon++);
        }
    }
    for (int b = 0; b < static_cast<int>(data.belts.size()); ++b) {
        AtlasRow row;
        row.target = {Kind::Belt, system, b};
        row.known = Scanned(row.target);
        row.name = "Campo " + std::to_string(b + 1);
        row.detail = row.known ? ProfileInfo(data.belts[static_cast<std::size_t>(b)].profile).name : "Sin analizar";
        rows.push_back(row);
    }
    for (const Anomaly& a : world_.anomalies) {
        if (a.system != system || !(a.discovered || a.revealed)) continue;
        AtlasRow row;
        row.target = {Kind::Anomaly, system, a.id};
        row.known = true;
        row.name = a.name;
        row.detail = a.kind == AnomalyKind::Derelict ? (a.emitter ? "Con emisor" : "Vacía") : Name(a.stance);
        if (a.kind == AnomalyKind::AlienStation && a.stationLevel >= 1) row.detail += ", nivel " + std::to_string(a.stationLevel);
        rows.push_back(row);
    }
    for (const Station& s : world_.stations.All()) {
        if (s.site.system != system) continue;
        AtlasRow row;
        row.target = {Kind::Station, system, s.id};
        row.known = true;
        row.name = s.name;
        row.detail = s.kind == StationKind::Power ? "Central" : "Origami";
        rows.push_back(row);
    }
    return rows;
}

bool GameSession::SetDrive(DriveMode mode) {
    if (!DriveAvailable(vessel_->ship, mode, rules_.drive)) {
        Say(mode == DriveMode::Warp ? "Sin energía negativa: el emisor está en una estructura abandonada."
                                    : "El hiper pide otro emisor de energía negativa, de otra estructura.");
        return false;
    }
    if (Curves(mode) && !CanCurve(vessel_->ship, mode, rules_.drive)) {
        Say("Antimateria insuficiente: formar la burbuja pide " + MassText(FormationCost(vessel_->ship, mode, rules_.drive)) + ".");
        return false;
    }
    vessel_->ship.mode = mode;
    return true;
}

void GameSession::SetThrottle(double value) { vessel_->ship.throttle = std::clamp(value, -1.0, 1.0); }

bool GameSession::SetAutopilot(const TargetRef& target) {
    if (!Resolve(target)) {
        Say("Sin datos de ese objetivo: acérquese más.");
        return false;
    }
    if (vessel_->orbiting && *vessel_->orbiting == target) return true;  // ya esta alli
    vessel_->autopilot = target;
    vessel_->orbiting.reset();
    vessel_->dockPhase = DockPhase::None;
    vessel_->exploring = -1;
    vessel_->fuelWarned = false;
    CancelScan();
    return true;
}

void GameSession::CancelAutopilot() { vessel_->autopilot.reset(); }

std::optional<TargetInfo> GameSession::Resolve(const TargetRef& t) const {
    const World& w = engine_.GetWorld();
    if (t.kind == Kind::None) return std::nullopt;
    if (t.kind != Kind::Station && t.kind != Kind::Anomaly && (t.system < 0 || static_cast<std::size_t>(t.system) >= w.state.size())) return std::nullopt;

    TargetInfo info;
    switch (t.kind) {
        case Kind::Star: {
            const StarSystemData& system = *w.systems[static_cast<std::size_t>(t.system)];
            const SystemState& state = w.state[static_cast<std::size_t>(t.system)];
            if (t.index < 0 || static_cast<std::size_t>(t.index) >= state.starNodes.size()) return std::nullopt;
            const StarData& star = system.StarAt(t.index);
            const StarYield yield = YieldOf(star);
            info.position = w.scene.WorldPosition(state.starNodes[static_cast<std::size_t>(t.index)]);
            if (static_cast<std::size_t>(t.index) < state.starBodies.size()) info.velocity = BodyVelocity(w, w.bodies[static_cast<std::size_t>(state.starBodies[static_cast<std::size_t>(t.index)])], bubble_.simTime);  // una estrella doble se mueve
            info.radius = star.radius;
            info.arrival = yield.orbitRadius;
            info.name = w.bodies[static_cast<std::size_t>(state.starBodies[static_cast<std::size_t>(std::min<std::size_t>(t.index, state.starBodies.size() - 1))])].name;
            info.detail = std::string(Info(star.kind).name) + "  " + PowerText(yield.energyPerHour) + "  blindaje " + std::to_string(yield.requiredTier);
            return info;
        }
        case Kind::Planet: {
            const Body* body = FindPlanet(w, t.system, t.index);
            if (!body) return std::nullopt;
            const BodyData* data = BodyInfo(w, *body);
            info.position = w.scene.WorldPosition(body->node);
            info.velocity = BodyVelocity(w, *body, bubble_.simTime);
            info.radius = body->radius;
            info.arrival = body->radius * 1.6 + 500.0;
            info.name = body->name;
            info.detail = data ? Name(data->kind) : "";
            return info;
        }
        case Kind::Belt: {
            int slot = -1;
            const AsteroidBelt* belt = FindBelt(w, t.system, t.index, &slot);
            if (!belt) return std::nullopt;
            // El cumulo mas cercano: es donde se puede trabajar.
            Vec3d near;
            if (!AsteroidLayer::NearestFamily(w, vessel_->ship.position, bubble_.simTime, near, slot)) return std::nullopt;
            info.position = near;
            info.radius = 220.0;
            info.arrival = 2000.0;
            info.name = w.beltInfo[static_cast<std::size_t>(slot)].name;
            info.detail = std::string("Campo ") + ProfileInfo(belt->profile).name;
            return info;
        }
        case Kind::Station: {
            const Station* s = world_.stations.Find(t.index);
            if (!s) return std::nullopt;
            if (!StationVisible(*s)) return std::nullopt;
            info.position = StationPosition(*s);
            // Va con lo que orbita: sin esto, el piloto automatico nunca la alcanzaria.
            if (s->site.star >= 0) {
                info.velocity = Vec3d{-std::sin(s->angle), 0.0, std::cos(s->angle)} * (rules_.stations.orbitRate / 3600.0 * s->yield.orbitRadius);
            } else if (s->site.body >= 0) {
                if (const Body* body = FindPlanet(w, s->site.system, s->site.body)) info.velocity = BodyVelocity(w, *body, bubble_.simTime);
            } else if (s->site.belt >= 0) {
                if (const AsteroidBelt* belt = FindBelt(w, s->site.system, s->site.belt)) {
                    const std::size_t family = static_cast<std::size_t>(s->site.family) % belt->families.size();
                    info.velocity = w.scene.World(belt->parent).TransformDirection(OrbitVelocity(belt->families[family].orbit, bubble_.simTime));
                }
            }
            info.radius = StationRadius(s->kind);
            info.arrival = 8.0;
            info.name = s->name;
            info.detail = s->kind == StationKind::Power ? "Antimateria" : "Astillero: " + MassText(s->stored.Total()) + " de material";
            return info;
        }
        case Kind::Anomaly: {
            for (const Anomaly& a : world_.anomalies) {
                if (a.id != t.index) continue;
                info.position = AnomalyPosition(a);
                info.velocity = a.velocity;
                info.radius = a.radius;
                info.arrival = a.radius * 3.0;
                info.name = a.name;
                info.detail = a.kind == AnomalyKind::Derelict ? (a.emitter ? "Emisor de energía negativa" : "Vacía") : Name(a.stance);
                if (a.kind == AnomalyKind::AlienStation && a.stationLevel >= 1 && a.stationLevel <= kStationLevels)
                    info.detail += ", nivel " + std::to_string(a.stationLevel) + " (" + kStationLevelNames[a.stationLevel - 1] + ")";
                if (a.maxHull > 0.0 && a.hull < a.maxHull) info.detail += ", casco " + Fixed(100.0 * a.hull / a.maxHull, 0) + " %";
                return info;
            }
            return std::nullopt;
        }
        case Kind::Ship: {
            // Otra nave de la flota, mientras vuele. La propia no es un objetivo.
            for (std::size_t i = 0; i < fleet_.size(); ++i) {
                const Vessel& v = fleet_[i];
                if (v.id != t.index || &v == vessel_ || v.phase != GamePhase::Flying) continue;
                info.position = v.ship.position;
                info.velocity = VelocityOf(v);
                info.radius = rules_.ship.radius;
                info.arrival = rules_.ship.radius * 3.0 + 2.0;
                info.name = "Nave " + std::to_string(v.id) + " (" + crews_[i].name + ")";
                info.detail = "Aliada, casco " + Fixed(100.0 * v.ship.hull / rules_.ship.hull, 0) + " %";
                return info;
            }
            return std::nullopt;
        }
        default: return std::nullopt;
    }
}

bool GameSession::InRange(const TargetRef& target, double range) const {
    const std::optional<TargetInfo> info = Resolve(target);
    return info && Length(info->position - vessel_->ship.position) <= range + info->arrival;
}

Vec3d GameSession::AnomalyPosition(const Anomaly& a) const {
    const World& w = engine_.GetWorld();
    return w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(a.system)]) + a.offset;
}

bool GameSession::StationVisible(const Station& s) const {
    const World& w = engine_.GetWorld();
    if (s.site.star >= 0) return true;
    return w.state[static_cast<std::size_t>(s.site.system)].loaded;
}

Vec3d GameSession::StationPosition(const Station& s) const {
    const World& w = engine_.GetWorld();
    const SystemState& state = w.state[static_cast<std::size_t>(s.site.system)];
    if (s.kind == StationKind::Shipyard) {
        // Quieta respecto a su planeta o al sistema. Mientras se despliega va de donde se abrio
        // (launch) a su sitio (offset), apartandose de la nave hacia popa.
        Vec3d base = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(s.site.system)]);
        if (s.site.body >= 0)
            if (const Body* body = FindPlanet(w, s.site.system, s.site.body)) base = w.scene.WorldPosition(body->node);
        const double slide = ShipyardDeploy(s.deployAge).slide;
        return base + s.launch + (s.site.offset - s.launch) * slide;
    }
    const Vec3d around{std::cos(s.angle), 0.05, std::sin(s.angle)};
    if (s.site.star >= 0) return w.scene.WorldPosition(state.starNodes[static_cast<std::size_t>(s.site.star)]) + around * s.yield.orbitRadius;
    if (s.site.body >= 0) {
        const Body* body = FindPlanet(w, s.site.system, s.site.body);
        if (body) return w.scene.WorldPosition(body->node) + around * (body->radius * 1.35 + 200.0);
    } else if (s.site.belt >= 0) {
        if (const AsteroidBelt* belt = FindBelt(w, s.site.system, s.site.belt)) {
            const std::size_t family = static_cast<std::size_t>(s.site.family) % belt->families.size();
            return FamilyPosition(w.scene, *belt, belt->families[family], bubble_.simTime) + around * 40.0;
        }
    }
    return w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(s.site.system)]);
}

Vec3d GameSession::ShipyardLaunchPosition(const Station& s) const {
    const World& w = engine_.GetWorld();
    Vec3d base = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(s.site.system)]);
    if (s.site.body >= 0)
        if (const Body* body = FindPlanet(w, s.site.system, s.site.body)) base = w.scene.WorldPosition(body->node);
    return base + s.launch;
}

Quatd GameSession::StationRotation(const Station& s) const {
    if (s.kind == StationKind::Shipyard) return s.attitude;
    if (s.kind != StationKind::Power || s.site.star < 0) return {};
    const World& w = engine_.GetWorld();
    const SystemState& state = w.state[static_cast<std::size_t>(s.site.system)];
    if (static_cast<std::size_t>(s.site.star) >= state.starNodes.size()) return {};
    return TurnedToward({}, w.scene.WorldPosition(state.starNodes[static_cast<std::size_t>(s.site.star)]) - StationPosition(s));
}

// Lo que miden los modelos (GameModels.cpp): la central, su corola.
// La origami: el anillo desplegado, de 1026 m de punta a punta (estacion.gltf).
double GameSession::StationRadius(StationKind kind) { return kind == StationKind::Power ? 0.45 : 0.515; }

// Las escotillas, en metros del modelo (arriba es -Y). La central la lleva encima del deposito, lejos
// de los colectores; la origami, en la esclusa del nudo 1 (atraque_1), en la cara exterior del anillo.
GameSession::DockPort GameSession::Port(const Station& s) const {
    const Mat4d turn = StationRotation(s).ToMatrix();
    Vec3d at, out;
    if (s.kind == StationKind::Shipyard) {
        out = {1.0, 0.0, 0.0};
        at = Vec3d{512.6, 0.0, 0.0} * 0.001;
    } else {
        out = {0.0, -1.0, 0.0};
        at = Vec3d{0.0, -137.0, -240.0} * 0.001;
    }
    return {StationPosition(s) + turn.TransformDirection(at), turn.TransformDirection(out)};
}

namespace {

// Cuaternion del giro cuyas columnas son x, y, z (las imagenes de los ejes).
Quatd FromAxes(const Vec3d& x, const Vec3d& y, const Vec3d& z) {
    const double trace = x.x + y.y + z.z;
    Quatd q;
    if (trace > 0.0) {
        const double s = 0.5 / std::sqrt(trace + 1.0);
        q = {(y.z - z.y) * s, (z.x - x.z) * s, (x.y - y.x) * s, 0.25 / s};
    } else if (x.x > y.y && x.x > z.z) {
        const double s = 2.0 * std::sqrt(1.0 + x.x - y.y - z.z);
        q = {0.25 * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        const double s = 2.0 * std::sqrt(1.0 + y.y - x.x - z.z);
        q = {(y.x + x.y) / s, 0.25 * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        const double s = 2.0 * std::sqrt(1.0 + z.z - x.x - y.y);
        q = {(z.x + x.z) / s, (z.y + y.z) / s, 0.25 * s, (x.y - y.x) / s};
    }
    return q.Normalized();
}

constexpr double kDockTube = 0.12;        // km: largo del ducto desplegado, del costado a la escotilla
constexpr double kDockTubeSeconds = 4.0;  // s reales que tarda en desplegarse

}  // namespace

// Atracada, la nave se pone frente a la escotilla: su compuerta (ShipDesign::dockPort) mira a la
// estacion, la proa queda de lado, y el ducto origami cubre lo que las separa. La compuerta puede
// estar en un costado (la maqueta, +X) o en la panza (la nave glTF, bajo el puente): la nave gira
// hasta que su dockNormal apunta a la escotilla, con la proa en el plano de la estacion.
Quatd GameSession::DockAttitude(const Station& s) const {
    const DockPort port = Port(s);
    const Vec3d toHatch = port.normal * -1.0;  // en el mundo: de la compuerta a la escotilla
    Vec3d forward = Cross(port.normal, Vec3d{0, -1, 0});
    // En la origami, la nave atraca paralela al eje del anillo: tumbada a lo largo de el, sus anillos
    // de curvatura chocarian con el de la estacion.
    if (s.kind == StationKind::Shipyard) forward = StationRotation(s).ToMatrix().TransformDirection(Vec3d{0, -1, 0});
    if (Length(forward) < 1e-6) forward = Cross(port.normal, Vec3d{1, 0, 0});
    forward = Normalize(forward);
    const Vec3d third = Cross(toHatch, forward);
    // Lo mismo en la nave: la compuerta, la proa sin la parte que va hacia la compuerta, y el tercero.
    const Vec3d n = Normalize(CurrentShipDesign().dockNormal);
    Vec3d f = Vec3d{0, 0, 1} - n * Dot(Vec3d{0, 0, 1}, n);
    if (Length(f) < 1e-6) f = Vec3d{0, 1, 0} - n * Dot(Vec3d{0, 1, 0}, n);
    f = Normalize(f);
    const Vec3d t = Cross(n, f);
    // Cada eje de la nave, llevado al mundo: el de la compuerta va a la escotilla, la proa a forward.
    const auto image = [&](const Vec3d& e) { return toHatch * Dot(n, e) + forward * Dot(f, e) + third * Dot(t, e); };
    return FromAxes(image({1, 0, 0}), image({0, 1, 0}), image({0, 0, 1}));
}

Vec3d GameSession::DockGoal(const Station& s) const {
    const DockPort port = Port(s);
    const Vec3d hatchSide = DockAttitude(s).ToMatrix().TransformDirection(CurrentShipDesign().dockPort * 0.001);
    return port.position + port.normal * kDockTube - hatchSide - StationPosition(s);
}

GameSession::DockTube GameSession::Tube() const {
    DockTube t;
    const Station* s = DockedStation();
    if (!s || vessel_->dockPhase != DockPhase::Docked || !StationVisible(*s)) return t;
    const DockPort port = Port(*s);
    t.active = true;
    t.stationEnd = port.position;
    t.shipEnd = vessel_->ship.position + vessel_->ship.orientation.ToMatrix().TransformDirection(CurrentShipDesign().dockPort * 0.001);
    t.extension = std::clamp(vessel_->dockedAge / kDockTubeSeconds, 0.0, 1.0);
    return t;
}

bool GameSession::DeployPower(const TargetRef& target) {
    World& w = engine_.GetWorld();
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info || target.kind != Kind::Star) {
        Say("La central se despliega en estrellas y agujeros negros.");
        return false;
    }
    if (!Scanned(target)) {
        Say("Sin datos de este astro: analícelo con el escáner.");
        return false;
    }
    const StarData& star = w.systems[static_cast<std::size_t>(target.system)]->StarAt(target.index);
    const StarYield yield = YieldOf(star);
    if (vessel_->ship.heatShieldTier < yield.requiredTier) {
        Say("Blindaje insuficiente: esta órbita pide nivel " + std::to_string(yield.requiredTier) + ".");
        return false;
    }
    if (!InRange(target, rules_.stations.deployRange)) {
        Say("Demasiado lejos de la órbita.");
        return false;
    }

    StationSite site;
    site.system = target.system;
    site.star = target.index;
    const bool free = world_.stations.NextIsFree(StationKind::Power);
    std::string error;
    const int id = world_.stations.Deploy(StationKind::Power, site, "Central " + std::to_string(world_.stations.All().size() + 1) + " en " + info->name, yield, vessel_->ship.cargo, &error);
    if (id < 0) {
        Say(error);
        return false;
    }
    const Vec3d fromStar = vessel_->ship.position - info->position;
    world_.stations.Find(id)->angle = std::atan2(fromStar.z, fromStar.x);
    Say(std::string(free ? "Central desplegada sin coste: " : "Central desplegada: ") + PowerText(yield.energyPerHour) + " para " + MassRateText(world_.stations.AntimatterFrom(yield.energyPerHour)) + " de antimateria.");
    SaveCheckpoint("Partida guardada");
    return true;
}

// --- Estacion origami --------------------------------------------------------------------------

namespace {
// Como va montada en la nave (estacion.gltf): girada -90 grados sobre X, con su eje en el de la nave,
// y su centro 135,5 m a popa del centro, entre las bodegas y entre los dos anillos de curvatura.
const Quatd kShipyardMount = Quatd::FromAxisAngle({1, 0, 0}, -1.5707963267948966);
constexpr double kShipyardMountAft = 0.1355;  // km
double SmoothStep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
}  // namespace

GameSession::ShipyardPhase GameSession::ShipyardDeploy(double age) {
    ShipyardPhase p;
    p.ring = std::clamp(age / kShipyardOpenSeconds, 0.0, 1.0);
    p.slide = SmoothStep((age - kShipyardOpenSeconds) / kShipyardSlideSeconds);
    p.arms = std::clamp((age - kShipyardOpenSeconds - kShipyardSlideSeconds) / kShipyardArmsSeconds, 0.0, 1.0);
    return p;
}

bool GameSession::CanDeployShipyard(std::string* why) const {
    const auto fail = [&](const char* reason) {
        if (why) *why = reason;
        return false;
    };
    const World& w = engine_.GetWorld();
    if (!vessel_->ship.stationAboard) return fail("La estación origami ya está desplegada.");
    if (vessel_->phase != GamePhase::Flying || pilot_->onFoot) return fail("Ahora no se puede soltar la estación.");
    if (vessel_->dockPhase != DockPhase::None) return fail("Suelte antes la nave de la estación.");
    // La estacion no orbita: se queda quieta junto al planeta, como la nave cuando frena con Espacio.
    if (vessel_->orbiting) return fail("Salga de la órbita y quede quieta (Espacio) para soltar la estación.");
    if (vessel_->ship.mode != DriveMode::Impulse) return fail("La estación solo se suelta con el impulso.");
    if (vessel_->ship.speed > rules_.stations.shipyardDeploySpeed) return fail("Demasiado rápido: frene antes de soltar la estación.");
    if (vessel_->nearestSystem < 0 || static_cast<std::size_t>(vessel_->nearestSystem) >= w.state.size() || !w.state[static_cast<std::size_t>(vessel_->nearestSystem)].loaded)
        return fail("La estación se despliega dentro de un sistema solar.");
    return true;
}

bool GameSession::DeployShipyard() {
    std::string why;
    if (!CanDeployShipyard(&why)) {
        Say(why);
        return false;
    }
    const World& w = engine_.GetWorld();
    const Mat4d turn = vessel_->ship.orientation.ToMatrix();
    const Vec3d stowed = vessel_->ship.position + turn.TransformDirection(Vec3d{0, 0, -kShipyardMountAft});
    const Vec3d aft = turn.TransformDirection(Vec3d{0, 0, -1});
    StationSite site;
    site.system = vessel_->nearestSystem;
    // Junto a un planeta se queda con el; si no, quieta en el sistema.
    Vec3d base = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(vessel_->nearestSystem)]);
    if (vessel_->frame.body >= 0) {
        const Body& body = w.bodies[static_cast<std::size_t>(vessel_->frame.body)];
        if (!body.isStar && body.extra < 0 && body.data >= 0 && body.system == vessel_->nearestSystem) {
            site.body = body.data;
            base = w.scene.WorldPosition(body.node);
        }
    }
    site.offset = stowed + aft * rules_.stations.shipyardSlide - base;
    std::string error;
    const int id = world_.stations.Deploy(StationKind::Shipyard, site, "Estación Origami en " + SystemName(vessel_->nearestSystem), {}, vessel_->ship.cargo, &error);
    if (id < 0) {
        Say(error);
        return false;
    }
    Station* s = world_.stations.Find(id);
    s->launch = stowed - base;
    s->attitude = (vessel_->ship.orientation * kShipyardMount).Normalized();
    vessel_->ship.stationAboard = false;
    vessel_->undockGrace = id;  // se abre alrededor de la nave: no choca hasta que se aparte
    Say("Estación origami soltada: se abre alrededor de las bodegas y se aparta a popa.");
    SaveCheckpoint("Partida guardada");
    return true;
}

// --- Drones mineros ----------------------------------------------------------------------------

int GameSession::DronesOut() const {
    int out = 0;
    for (const MiningDrone& d : vessel_->drones) out += d.state != DroneState::Docked ? 1 : 0;
    return out;
}

Vec3d GameSession::DroneBayPosition(int slot) const {
    const ShipDesign& design = CurrentShipDesign();
    if (slot < 0 || slot >= static_cast<int>(design.slots.size())) return vessel_->ship.position;
    const ModuleSlot& s = design.slots[static_cast<std::size_t>(slot)];
    return vessel_->ship.position + vessel_->ship.orientation.ToMatrix().TransformDirection((s.position + s.normal * 26.0) * 0.001);
}

Vec3d GameSession::DroneBayNormal(int slot) const {
    const ShipDesign& design = CurrentShipDesign();
    if (slot < 0 || slot >= static_cast<int>(design.slots.size())) return Heading(vessel_->ship);
    return vessel_->ship.orientation.ToMatrix().TransformDirection(design.slots[static_cast<std::size_t>(slot)].normal);
}

std::optional<Vec3d> GameSession::DroneWorkPoint() const {
    if (!vessel_->droneSite) return std::nullopt;
    const std::optional<TargetInfo> info = Resolve(*vessel_->droneSite);
    if (!info) return std::nullopt;
    if (vessel_->droneSite->kind != Kind::Planet) return info->position;
    // En la superficie, del lado por el que llega la nave.
    const Vec3d toShip = vessel_->ship.position - info->position;
    const Vec3d up = Length(toShip) > 1e-9 ? Normalize(toShip) : Vec3d{0, -1, 0};
    return info->position + up * info->radius;
}

void GameSession::SyncDrones() {
    const ShipDesign& design = CurrentShipDesign();
    std::vector<MiningDrone> kept;
    for (std::size_t i = 0; i < vessel_->ship.loadout.modules.size() && i < design.slots.size(); ++i) {
        if (vessel_->ship.loadout.modules[i] != ShipModuleKind::DroneBay || !SlotWorking(vessel_->ship, static_cast<int>(i))) continue;
        for (int k = 0; k < rules_.drones.perBay; ++k) {
            MiningDrone drone;
            drone.slot = static_cast<int>(i);
            drone.index = k;
            for (const MiningDrone& d : vessel_->drones)
                if (d.slot == drone.slot && d.index == k) drone = d;
            kept.push_back(drone);
        }
    }
    int lost = 0;
    for (const MiningDrone& d : vessel_->drones) {
        bool found = false;
        for (const MiningDrone& k : kept) found = found || (k.slot == d.slot && k.index == d.index);
        if (!found && d.state != DroneState::Docked) ++lost;
    }
    if (lost > 0) Say(lost == 1 ? "Un dron minero se ha quedado sin bahía: perdido." : std::to_string(lost) + " drones mineros se han quedado sin bahía: perdidos.");
    vessel_->drones = std::move(kept);
}

bool GameSession::DeployDrones(const TargetRef& target) {
    World& w = engine_.GetWorld();
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info || (target.kind != Kind::Planet && target.kind != Kind::Belt)) {
        Say("Los drones minan planetas y campos de asteroides.");
        return false;
    }
    if (!Scanned(target)) {
        Say("Sin datos de este astro: analícelo con el escáner.");
        return false;
    }
    SyncDrones();
    if (vessel_->drones.empty()) {
        Say("No hay bahías de drones que funcionen.");
        return false;
    }
    if (vessel_->ship.mode != DriveMode::Impulse) {
        Say("Los drones solo salen con el impulso.");
        return false;
    }
    if (!InRange(target, rules_.drones.range)) {
        Say("Demasiado lejos para los drones: está a " + Fixed(Length(info->position - vessel_->ship.position), 0) + " km y hace falta menos de " + Fixed(rules_.drones.range + info->arrival, 0) + " km.");
        return false;
    }
    Deposit deposit;
    if (target.kind == Kind::Planet) {
        const Body* body = FindPlanet(w, target.system, target.index);
        const BodyData* data = body ? BodyInfo(w, *body) : nullptr;
        if (!data || !DepositOf(*data, deposit)) {
            Say("Aquí no hay nada que extraer.");
            return false;
        }
    } else {
        const AsteroidBelt* belt = FindBelt(w, target.system, target.index);
        if (!belt) return false;
        deposit = DepositOf(belt->profile);
    }
    vessel_->droneSite = target;
    vessel_->droneDeposit = deposit;
    vessel_->dronesFullWarned = false;
    vessel_->droneYield = 0.0;
    if (vessel_->extractZ > 0) {
        const std::optional<astro::PeriodicShares> shares = ElementSharesOf(target);
        const double fraction = shares ? (*shares)[static_cast<std::size_t>(vessel_->extractZ - 1)] : 0.0;
        if (fraction > 0.0) {
            vessel_->droneYield = fraction * rules_.drones.recovery;
            Say("Extracción selectiva: " + std::string(astro::Periodic(vessel_->extractZ).name) + ".");
        } else {
            Say(std::string(astro::Periodic(vessel_->extractZ).name) + " no hay aquí: los drones mineran a granel.");
        }
    }
    Say("Drones mineros hacia " + info->name + ": minan, vuelven y descargan hasta que se les recoja.");
    return true;
}

void GameSession::SetExtractElement(int z) {
    vessel_->extractZ = ElementStock::Valid(z) ? z : 0;
    // Con drones ya trabajando, el cambio vale desde su siguiente carga.
    vessel_->droneYield = 0.0;
    if (vessel_->extractZ > 0 && vessel_->droneSite) {
        const std::optional<astro::PeriodicShares> shares = ElementSharesOf(*vessel_->droneSite);
        if (shares) vessel_->droneYield = (*shares)[static_cast<std::size_t>(vessel_->extractZ - 1)] * rules_.drones.recovery;
    }
}

std::optional<astro::PeriodicShares> GameSession::ElementSharesOf(const TargetRef& t) const {
    const World& w = engine_.GetWorld();
    if (t.system < 0 || static_cast<std::size_t>(t.system) >= w.systems.size() || t.index < 0 || !Scanned(t)) return std::nullopt;
    const astro::StarSystemData& system = *w.systems[static_cast<std::size_t>(t.system)];
    // La misma semilla en todas partes: la tabla periodica del mapa y esto dan lo mismo.
    const std::uint32_t base = static_cast<std::uint32_t>(t.system) * 1000u;
    const std::uint32_t index = static_cast<std::uint32_t>(t.index);
    if (t.kind == Kind::Planet && index < system.bodies.size()) return astro::AllElementsOf(astro::ElementsOf(system.bodies[index].materials), base + index + 1u);
    if (t.kind == Kind::Star && t.index < system.StarCount()) {
        const astro::StarData& star = system.StarAt(t.index);
        if (star.kind == astro::StarClass::BlackHole) return std::nullopt;
        return astro::AllElementsOf(astro::ElementsOf(star), base + 900u + index);
    }
    if (t.kind == Kind::Belt && index < system.belts.size()) return astro::AllElementsOf(BeltElements(system.belts[index].profile), base + 800u + index);
    return std::nullopt;
}

double GameSession::ElementYieldRate(const TargetRef& t, int z) const {
    if (!ElementStock::Valid(z) || (t.kind != Kind::Planet && t.kind != Kind::Belt)) return 0.0;
    const std::optional<astro::PeriodicShares> shares = ElementSharesOf(t);
    if (!shares) return 0.0;
    const World& w = engine_.GetWorld();
    Deposit deposit;
    if (t.kind == Kind::Planet) {
        const Body* body = FindPlanet(w, t.system, t.index);
        const BodyData* data = body ? BodyInfo(w, *body) : nullptr;
        if (!data || !DepositOf(*data, deposit)) return 0.0;
    } else {
        const AsteroidBelt* belt = FindBelt(w, t.system, t.index);
        if (!belt) return 0.0;
        deposit = DepositOf(belt->profile);
    }
    return ElementYieldPerHour(rules_.drones, deposit.richness, (*shares)[static_cast<std::size_t>(z - 1)]);
}

void GameSession::RecallDrones() {
    const bool out = DronesOut() > 0;
    vessel_->droneSite.reset();
    for (MiningDrone& d : vessel_->drones)
        if (d.state != DroneState::Docked) d.state = DroneState::Returning;
    if (out) Say("Drones mineros: vuelven a sus bahías.");
}

void GameSession::UpdateDrones(double seconds, double hours) {
    SyncDrones();
    if (vessel_->drones.empty()) {
        vessel_->droneSite.reset();
        return;
    }
    const DroneRules& r = rules_.drones;
    const auto unload = [&](MiningDrone& d) {
        if (d.element > 0) {  // extraccion selectiva: va al inventario de elementos (kg) y no ocupa bodega
            vessel_->ship.elements.Add(d.element, d.load * d.yield * 1.0e6);
            d.load = 0.0;
            return;
        }
        const double room = std::max(CargoCapacity(vessel_->ship, rules_.ship) - vessel_->ship.cargo.Total(), 0.0);
        const double amount = std::min(d.load, room);
        for (std::size_t k = 0; k < vessel_->droneDeposit.share.size(); ++k) vessel_->ship.cargo.Add(static_cast<Resource>(k), amount * vessel_->droneDeposit.share[k]);
        d.load -= amount;
        if (d.load > 1e-9 && !vessel_->dronesFullWarned) {
            Say("Bodega llena: los drones esperan en sus bahías con el mineral.");
            vessel_->dronesFullWarned = true;
        } else if (d.load <= 1e-9) {
            d.load = 0.0;
        }
    };
    // Con otro motor que el impulso se repliegan de golpe: no pueden seguir a la nave.
    if (vessel_->ship.mode != DriveMode::Impulse && DronesOut() > 0) {
        for (MiningDrone& d : vessel_->drones) {
            d.state = DroneState::Docked;
            unload(d);
        }
        vessel_->droneSite.reset();
        Say("Los drones se repliegan a sus bahías antes de acelerar.");
    }
    std::optional<Vec3d> work = DroneWorkPoint();
    if (vessel_->droneSite && (!work || Length(*work - vessel_->ship.position) > r.leash)) {
        vessel_->droneSite.reset();
        work.reset();
        for (MiningDrone& d : vessel_->drones)
            if (d.state != DroneState::Docked) d.state = DroneState::Returning;
        Say("Drones mineros: el sitio queda fuera de alcance; vuelven a la nave.");
    }
    const double room = CargoCapacity(vessel_->ship, rules_.ship) - vessel_->ship.cargo.Total();
    const auto fly = [&](MiningDrone& d, const Vec3d& goal, double speed) {
        const Vec3d gap = goal - d.position;
        const double distance = Length(gap);
        const double step = speed * seconds;
        if (distance <= std::max(step, 0.02)) {
            d.position = goal;
            return true;
        }
        d.heading = gap * (1.0 / distance);
        d.position += d.heading * step;
        return false;
    };
    for (MiningDrone& d : vessel_->drones) {
        // Cada uno trabaja en su sitio, a unos km de los demas.
        const double a = static_cast<double>(d.slot * 2 + d.index) * 2.399963;
        const Vec3d spot = Vec3d{std::cos(a), 0.25 * std::sin(a * 1.7), std::sin(a)} * 2.5;
        switch (d.state) {
            case DroneState::Docked:
                if (d.load > 0.0) unload(d);
                if (vessel_->droneSite && work && d.load <= 0.0 && (room > 0.0 || vessel_->droneYield > 0.0)) {
                    d.state = DroneState::Outbound;
                    d.position = DroneBayPosition(d.slot);
                    d.heading = DroneBayNormal(d.slot);
                }
                break;
            case DroneState::Outbound:
                if (!work) d.state = DroneState::Returning;
                else if (fly(d, *work + spot, r.speed)) d.state = DroneState::Mining;
                break;
            case DroneState::Mining:
                if (!work) {
                    d.state = DroneState::Returning;
                    break;
                }
                d.position = *work + spot;
                if (d.load <= 0.0) {  // cada carga es de un solo tipo: lo elegido al empezarla
                    d.element = vessel_->droneYield > 0.0 ? vessel_->extractZ : 0;
                    d.yield = vessel_->droneYield;
                }
                d.load +=r.rate * vessel_->droneDeposit.richness * hours;
                if (d.load >= r.hold) {
                    d.load = r.hold;
                    d.state = DroneState::Returning;
                }
                break;
            case DroneState::Returning:
                // Si la nave se mueve, la alcanzan: van algo mas rapido que ella.
                if (fly(d, DroneBayPosition(d.slot), std::max(r.speed, vessel_->ship.speed * 1.5))) {
                    d.state = DroneState::Docked;
                    unload(d);
                }
                break;
        }
    }
}

bool GameSession::Interact(const TargetRef& target) {
    if (target.kind == Kind::Station) {
        Station* s = world_.stations.Find(target.index);
        // Sin enlace hay que ir en persona.
        if (s && s->kind == StationKind::Shipyard) {
            // A la origami se le entrega material: todo el metal y el silicato que pase de la reserva
            // del escudo de arena, que es su municion.
            if (!InRange(target, rules_.ship.interactRange)) {
                Say("Hay que estar junto a la estación para entregarle material.");
                return false;
            }
            const double metal = vessel_->ship.cargo.Get(Resource::Metal);
            const double silicate = std::max(vessel_->ship.cargo.Get(Resource::Silicate) - rules_.ship.startSilicate, 0.0);
            if (metal + silicate <= 0.0) {
                Say("No hay metal ni silicato en la bodega para entregar.");
                return false;
            }
            vessel_->ship.cargo.Add(Resource::Metal, -metal);
            vessel_->ship.cargo.Add(Resource::Silicate, -silicate);
            s->stored.Add(Resource::Metal, metal);
            s->stored.Add(Resource::Silicate, silicate);
            Say("Entregado a " + s->name + ": " + MassText(metal) + " de metal y " + MassText(silicate) + " de silicato. Tiene " + MassText(s->stored.Total()) + " para construir.");
            SaveCheckpoint("Partida guardada");
            return true;
        }
        if (!s || (vessel_->ship.relayTier <= 0 && !InRange(target, rules_.ship.interactRange))) {
            Say("Hay que estar junto a la estación para recoger.");
            return false;
        }
        const double antimatterBefore = vessel_->ship.antimatter, cargoBefore = vessel_->ship.cargo.Total();
        world_.stations.Collect(target.index, vessel_->ship.cargo, CargoCapacity(vessel_->ship, rules_.ship), vessel_->ship.antimatter, AntimatterCapacity(vessel_->ship, rules_.ship));
        ++pilot_->collected;
        Say("Recogido: " + MassText(vessel_->ship.cargo.Total() - cargoBefore) + " de carga y " + MassText(vessel_->ship.antimatter - antimatterBefore) + " de antimateria.");
        SaveCheckpoint("Partida guardada");
        return true;
    }
    if (target.kind == Kind::Anomaly) {
        for (Anomaly& a : world_.anomalies) {
            if (a.id != target.index) continue;
            if (a.kind != AnomalyKind::Derelict) {
                Say("No responde.");
                return false;
            }
            if (!InRange(target, rules_.ship.interactRange)) {
                Say("Demasiado lejos de la estructura.");
                return false;
            }
            if (!a.emitter) {
                Say("Ya no queda nada dentro.");
                return false;
            }
            vessel_->exploring = a.id;
            Say("Explorando " + a.name + ".");
            return true;
        }
    }
    Say("Nada con lo que interactuar.");
    return false;
}

bool GameSession::UpgradeStation(int id, ModuleKind module) {
    std::string error;
    if (!world_.stations.AddOrUpgradeModule(id, module, vessel_->ship.cargo, &error)) {
        Say(error);
        return false;
    }
    Say(std::string(Name(module)) + " instalado.");
    return true;
}

double GameSession::ScanRange() const {
    // Cada antena de sensores montada suma alcance.
    return rules_.scanner.range * (1.0 + rules_.scanner.rangePerTier * (vessel_->ship.scannerTier - 1)) * (1.0 + kSensorBoost * WorkingModules(vessel_->ship, ShipModuleKind::SensorArray));
}

bool GameSession::StartScan() {
    if (Scanning()) return false;
    if (!CanScan()) {
        Say("El barrido exige la nave quieta: pulse Espacio o entre en órbita.");
        return false;
    }
    if (vessel_->ship.energy < rules_.scanner.energy) {
        Say("Energía insuficiente para el barrido.");
        return false;
    }
    vessel_->ship.energy -= rules_.scanner.energy;
    vessel_->scanProgress = 0.0;
    Say("Barrido de microondas y ultravioleta en curso.");
    return true;
}

void GameSession::CancelScan() { vessel_->scanProgress = -1.0; }

// En una orbita asistida la nave no enciende nada: para el escaner, es tan estable como estar quieta.
bool GameSession::CanScan() const { return !vessel_->autopilot && vessel_->ship.throttle == 0.0 && (vessel_->orbiting || vessel_->ship.speed <= 1.0); }

bool GameSession::Scanned(const TargetRef& t) const {
    if (t.kind != Kind::Star && t.kind != Kind::Planet && t.kind != Kind::Belt) return true;
    return pilot_->scanned.count(ScanKey(t)) != 0;
}

void GameSession::MarkScanned(int system) {
    const StarSystemData& data = *engine_.GetWorld().systems[static_cast<std::size_t>(system)];
    for (int m = 0; m < data.StarCount(); ++m) pilot_->scanned.insert(ScanKey({Kind::Star, system, m}));
    for (std::size_t b = 0; b < data.bodies.size(); ++b) pilot_->scanned.insert(ScanKey({Kind::Planet, system, static_cast<int>(b)}));
    for (std::size_t b = 0; b < data.belts.size(); ++b) pilot_->scanned.insert(ScanKey({Kind::Belt, system, static_cast<int>(b)}));
}

void GameSession::MarkKnown(int system) {
    if (system < 0 || static_cast<std::size_t>(system) >= pilot_->known.size()) return;
    pilot_->known[static_cast<std::size_t>(system)] = 1;
    MarkScanned(system);
}

void GameSession::SetCreative(bool on) {
    vessel_->creative = on;
    if (!on) {
        Say("Modo creativo desactivado.");
        return;
    }
    vessel_->ship.emitters = std::max(vessel_->ship.emitters, 8);
    vessel_->ship.scannerTier = std::max(vessel_->ship.scannerTier, 5);
    vessel_->ship.heatShieldTier = std::max(vessel_->ship.heatShieldTier, 5);
    vessel_->ship.handlingTier = std::max(vessel_->ship.handlingTier, 3);
    vessel_->ship.relayTier = std::max(vessel_->ship.relayTier, 1);
    vessel_->ship.computerTier = std::max(vessel_->ship.computerTier, rules_.combat.maxTier);
    for (WeaponMount& m : vessel_->ship.weapons) m.tier = std::max(m.tier, rules_.combat.maxTier);
    DefenseState& d = vessel_->ship.defense;
    for (int& t : d.shieldTier) t = std::max(t, rules_.combat.maxTier);  // el Amor incluido, para probarlo
    for (int& t : d.coolantTier) t = std::max(t, rules_.combat.maxTier);
    for (int k = 0; k < static_cast<int>(ShieldKind::Count); ++k) d.charge[static_cast<std::size_t>(k)] = ShieldCapacity(rules_.defense, static_cast<ShieldKind>(k), d.shieldTier[static_cast<std::size_t>(k)]);
    for (std::size_t s = 0; s < pilot_->known.size(); ++s) MarkKnown(static_cast<int>(s));
    Say("Modo creativo: nave invulnerable, todo sin fin y todos los sistemas localizados.");
}

double GameSession::SystemAlert(int system) const {
    return system >= 0 && static_cast<std::size_t>(system) < world_.species.alert.size() ? world_.species.alert[static_cast<std::size_t>(system)] : 0.0;
}

int GameSession::SpawnAnomaly(AnomalyKind kind, Stance stance, double km, int level, int shipClass) {
    const World& w = engine_.GetWorld();
    const int system = vessel_->nearestSystem;
    if (system < 0 || static_cast<std::size_t>(system) >= w.sectors.size()) return -1;
    Anomaly a;
    for (const Anomaly& o : world_.anomalies) a.id = std::max(a.id, o.id + 1);
    a.kind = kind;
    a.stance = kind == AnomalyKind::Derelict ? Stance::None : stance;
    a.system = system;
    a.offset = vessel_->ship.position + Heading(vessel_->ship) * std::max(km, 1.0) - w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(system)]);
    a.home = a.offset;
    a.radius = kind == AnomalyKind::Derelict ? 12.0 : kind == AnomalyKind::AlienStation ? 2.5 : (stance == Stance::Hostile ? 0.5 : 0.7);
    a.emitter = kind == AnomalyKind::Derelict;
    a.cold = kind == AnomalyKind::Derelict;
    a.discovered = a.revealed = true;
    a.name = std::string(kind == AnomalyKind::Derelict ? "Estructura abandonada" : kind == AnomalyKind::AlienStation ? (stance == Stance::Hostile ? "Bastion hostil" : "Estacion neutral")
                                                                                                                  : (stance == Stance::Hostile ? "Nave hostil" : "Nave neutral")) + " " + std::to_string(a.id);
    if (kind == AnomalyKind::AlienStation) a.stationLevel = level;
    if (kind == AnomalyKind::AlienStation && level == kArmorPlanetLevel) {
        // Con su radio de Jupiter: se pone a km de su superficie, no de su centro.
        a.offset += Heading(vessel_->ship) * rules_.anomalies.armorPlanetRadius;
        a.home = a.offset;
        a.discovered = false;  // como el de verdad: avisa al entrar en su percepcion
        MakeArmorPlanet(a, rules_.anomalies);
    } else {
        InitCombat(a, rules_.anomalies);
    }
    if (kind == AnomalyKind::AlienShip && shipClass >= 0 && shipClass < kShipClassCount) {
        a.shipClass = static_cast<ShipClassId>(shipClass);
        a.variant = ShipVariant::Normal;
        a.maxHull = a.hull = ClassOf(a).hull * rules_.anomalies.referenceHull;
        InitShield(a, rules_.anomalies);
    }
    ApplyBoost(a, RevengeBoost(world_.species, rules_.alert));  // tras una revancha, las nuevas tambien vienen reforzadas
    world_.anomalies.push_back(a);
    UpdateContacts();
    return a.id;
}

// Lo que alcanza el barrido en este sistema: estrellas, planetas y campos.
void GameSession::ScanBodies(double range, int& found) {
    const World& w = engine_.GetWorld();
    const int system = vessel_->nearestSystem;
    const StarSystemData& data = *w.systems[static_cast<std::size_t>(system)];
    const auto consider = [&](const TargetRef& t) {
        if (Scanned(t)) return;
        const std::optional<TargetInfo> info = Resolve(t);
        if (!info || Length(info->position - vessel_->ship.position) > range) return;
        pilot_->scanned.insert(ScanKey(t));
        ++found;
    };
    for (int m = 0; m < data.StarCount(); ++m) consider({Kind::Star, system, m});
    for (std::size_t b = 0; b < data.bodies.size(); ++b) {
        const BodyData& body = data.bodies[b];
        if (!body.destroyed && body.kind != PlanetClass::Comet && body.kind != PlanetClass::Asteroid) consider({Kind::Planet, system, static_cast<int>(b)});
    }
    for (std::size_t b = 0; b < data.belts.size(); ++b) consider({Kind::Belt, system, static_cast<int>(b)});
}

void GameSession::UpdateScan(double realSeconds) {
    if (!Scanning()) return;
    if (!CanScan()) {
        Say("Barrido perdido: la nave se ha movido.");
        CancelScan();
        return;
    }
    vessel_->scanProgress += realSeconds / rules_.scanner.seconds;
    if (vessel_->scanProgress < 1.0) return;
    CancelScan();

    const double range = ScanRange();
    World& w = engine_.GetWorld();
    const double systemRange = rules_.scanner.systemRange * (1.0 + rules_.scanner.rangePerTier * (vessel_->ship.scannerTier - 1));
    int systems = 0;
    for (std::size_t s = 0; s < pilot_->known.size(); ++s) {
        if (pilot_->known[s] || Length(w.scene.WorldPosition(w.sectors[s]) - vessel_->ship.position) > systemRange) continue;
        pilot_->known[s] = 1;
        ++systems;
    }
    int bodies = 0;
    ScanBodies(rules_.scanner.bodyRange * (1.0 + rules_.scanner.rangePerTier * (vessel_->ship.scannerTier - 1)), bodies);
    int found = 0, weak = 0, strongest = 0;
    for (Anomaly& a : world_.anomalies) {
        if (Length(AnomalyPosition(a) - vessel_->ship.position) > range) continue;
        if (a.signature > vessel_->ship.scannerTier) {
            ++weak;
            strongest = std::max(strongest, a.signature);
        } else if (!a.revealed) {
            a.revealed = true;
            ++found;
        }
    }
    ++vessel_->scansDone;
    ListenForEcho(range);
    vessel_->lastScan = found > 0 ? "Barrido completo: " + std::to_string(found) + " anomalías nuevas." : "Barrido completo: nada nuevo en este radio.";
    if (weak > 0) vessel_->lastScan += " Señal débil: pide escáner de nivel " + std::to_string(strongest) + ".";
    if (vessel_->echo.active) {
        const double au = astro::kAstronomicalUnit;
        const std::string course = ", rumbo " + Fixed(vessel_->echo.bearing, 0) + "°, elevación " + (vessel_->echo.elevation >= 0.0 ? "+" : "") + Fixed(vessel_->echo.elevation, 0) + "°.";
        vessel_->lastScan += vessel_->echo.exact ? " Eco frío exacto a " + Fixed(vessel_->echo.distance / au, 1) + " UA" + course
                                 : " Eco frío a ~" + Fixed(vessel_->echo.distance / au, 1) + " UA (±" + Fixed(vessel_->echo.error / au, 1) + ")" + course;
    }
    if (vessel_->echo.weak > 0) vessel_->lastScan += " Señal débil más allá del alcance: nivel " + std::to_string(vessel_->echo.needTier) + ".";
    if (bodies > 0) vessel_->lastScan += " Astros analizados: " + std::to_string(bodies) + ".";
    if (systems > 0) vessel_->lastScan += " Sistemas localizados: " + std::to_string(systems) + ".";
    Say(vessel_->lastScan);
    if (found + systems + bodies > 0) saveWanted_ = true;
    bubble_.steps = 0;  // que aparezca en sensores ya
}

double GameSession::EchoRange() const { return ScanRange() * rules_.scanner.echoFactor; }

bool GameSession::EchoGuaranteed() const { return pilot_->dialogue.Tag() == kStructureStep; }

std::optional<Vec3d> GameSession::EchoPosition() const {
    const World& w = engine_.GetWorld();
    if (!vessel_->echo.active || vessel_->echo.system < 0 || static_cast<std::size_t>(vessel_->echo.system) >= w.sectors.size()) return std::nullopt;
    return w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(vessel_->echo.system)]) + vessel_->echo.offset;
}

// Tras un barrido: lo frio y sin revelar que queda fuera del alcance pero dentro del eco. Si su firma
// no pasa del nivel, da el rumbo y la distancia de lo mas cercano, con un error que baja con el nivel;
// si la pasa, solo avisa de una senal debil. En el paso de la estructura del tutorial, la pista es
// exacta y llega aunque el eco no alcance.
void GameSession::ListenForEcho(double range) {
    vessel_->echo = {};
    vessel_->echoTarget = -1;
    const World& w = engine_.GetWorld();
    const int system = vessel_->nearestSystem;
    if (system < 0 || static_cast<std::size_t>(system) >= w.sectors.size()) return;
    const bool guaranteed = EchoGuaranteed();
    const double reach = EchoRange();
    const Anomaly* nearest = nullptr;
    double nearestDistance = 0.0;
    for (const Anomaly& a : world_.anomalies) {
        if (a.system != system || !a.cold || a.revealed) continue;
        const double d = Length(AnomalyPosition(a) - vessel_->ship.position);
        if (d <= range) continue;  // eso ya lo ha visto, o es senal debil, el barrido normal
        const bool heard = d <= reach;
        if (a.signature > vessel_->ship.scannerTier) {
            if (!heard) continue;
            ++vessel_->echo.weak;  // la regla de la firma: sin rumbo, solo el aviso
            vessel_->echo.needTier = std::max(vessel_->echo.needTier, a.signature);
            continue;
        }
        if (!heard && !guaranteed) continue;
        if (!nearest || d < nearestDistance) {
            nearest = &a;
            nearestDistance = d;
        }
    }
    if (!nearest) return;

    const Vec3d truth = AnomalyPosition(*nearest) - vessel_->ship.position;
    Vec3d direction = truth * (1.0 / nearestDistance);
    double distance = nearestDistance;
    if (!guaranteed) {
        // Un error fijo para cada barrido: sale de la semilla, la anomalia y la cuenta de barridos.
        const auto unit = [&](std::uint32_t salt) {
            std::uint32_t x = world_.seed * 2654435761u ^ static_cast<std::uint32_t>(nearest->id) * 40503u ^ static_cast<std::uint32_t>(vessel_->scansDone) * 97u ^ salt;
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return static_cast<double>(x) / 4294967296.0;
        };
        const double scale = std::pow(rules_.scanner.echoErrorPerTier, std::max(vessel_->ship.scannerTier - 1, 0));
        const double bearingError = rules_.scanner.echoBearingError * scale * kPi / 180.0;
        const double distanceError = rules_.scanner.echoDistanceError * scale;
        // Se gira el rumbo un angulo de hasta bearingError alrededor de un eje perpendicular cualquiera.
        const Vec3d helper = std::abs(direction.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0};
        const Vec3d side = Normalize(Cross(direction, helper)), up = Cross(side, direction);
        const double spin = unit(0x1u) * 2.0 * kPi, tilt = unit(0x2u) * bearingError;
        const Vec3d axis = side * std::cos(spin) + up * std::sin(spin);
        direction = Normalize(direction * std::cos(tilt) + Cross(axis, direction) * std::sin(tilt));
        distance *= 1.0 + distanceError * (2.0 * unit(0x3u) - 1.0);
        vessel_->echo.error = distance * std::hypot(distanceError, bearingError);
    }
    const Vec3d center = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(system)]);
    vessel_->echo.active = true;
    vessel_->echo.exact = guaranteed;
    vessel_->echo.system = system;
    vessel_->echo.offset = vessel_->ship.position + direction * distance - center;
    vessel_->echo.distance = distance;
    vessel_->echo.bearing = std::fmod(std::atan2(direction.z, direction.x) * 180.0 / kPi + 360.0, 360.0);
    vessel_->echo.elevation = std::asin(std::clamp(direction.y, -1.0, 1.0)) * 180.0 / kPi;
    vessel_->echoTarget = nearest->id;
}

int GameSession::RevealedStructures() const {
    int count = 0;
    for (const Anomaly& a : world_.anomalies) count += a.kind == AnomalyKind::Derelict && a.revealed ? 1 : 0;
    return count;
}

// Un momento que merece guardarse: lo dice en el registro y pide al juego que escriba la partida.
// Ya no guarda nada a lo que volver: al perder la nave el mundo sigue como esta (ver Respawn).
void GameSession::SaveCheckpoint(const std::string& reason) {
    if (vessel_->phase != GamePhase::Flying || reason.empty()) return;
    Say(reason + ".");
    saveWanted_ = true;
}

bool GameSession::TakeSaveRequest() {
    const bool wanted = saveWanted_;
    saveWanted_ = false;
    return wanted;
}

bool GameSession::TakeSaveDeletion() {
    const bool lost = saveLost_;
    saveLost_ = false;
    return lost;
}

SaveData GameSession::Snapshot() const {
    SaveData d;
    WorldSave& w = d.world;
    w.seed = world_.seed;
    w.mode = world_.mode;
    w.simTime = bubble_.simTime;
    w.stations = world_.stations.All();
    w.nextStation = world_.stations.NextId();
    w.anomalies = world_.anomalies;
    w.species = world_.species;

    // Todas las naves van en el mundo.
    for (const Vessel& from : fleet_) {
        ShipSave v;
        v.id = from.id;
        v.ship = from.ship;
        v.absolutePosition = from.ship.position + engine_.GetWorld().origin;
        v.homeStation = from.homeStation;
        // Con la nave recien perdida se guarda todo como esta y esta marca: al cargar, reaparece.
        v.lost = from.phase != GamePhase::Flying;
        if (from.orbiting) {
            v.orbitKind = static_cast<int>(from.orbiting->kind);
            v.orbitSystem = from.orbiting->system;
            v.orbitIndex = from.orbiting->index;
            v.orbitOffset = from.orbitOffset;
            v.orbitNormal = from.orbitNormal;
            v.orbitRate = from.orbitRate;
            v.docked = from.dockPhase == DockPhase::Docked;  // a pie tambien: al cargar se empieza a bordo
        }
        w.ships.push_back(std::move(v));
    }

    // El jugador es el de la nave del foco. Los de las demas naves se piden con SnapshotPlayer.
    d.player = SnapshotPlayer(focus_);
    return d;
}

PlayerSave GameSession::SnapshotPlayer(int index) const {
    const PlayerState& from = crews_[static_cast<std::size_t>(index)];
    PlayerSave p;
    p.id = from.id;
    p.name = from.name;
    p.ship = fleet_[static_cast<std::size_t>(index)].id;
    p.tutorialStep = from.dialogue.Active() ? from.dialogue.Step() : 1000000;  // terminado o apagado
    p.cursorUsed = from.cursorUsed;
    p.analysisUsed = from.analysisUsed;
    p.collected = from.collected;
    p.kills = from.kills;
    p.knownSystems = from.known;
    p.visitedSystems = from.visited;
    p.scanned.assign(from.scanned.begin(), from.scanned.end());
    return p;
}

bool GameSession::Restore(const SaveData& d, std::string* error) {
    if (d.world.seed != world_.seed) {
        if (error) *error = "la partida es de otra galaxia";
        return false;
    }
    const ShipSave* mine = d.Ship();
    if (!mine) {
        if (error) *error = "la partida no tiene ninguna nave";
        return false;
    }
    const World& w = engine_.GetWorld();

    // El mundo.
    world_.mode = d.world.mode;
    world_.stations.Restore(d.world.stations, d.world.nextStation);
    world_.anomalies = d.world.anomalies;
    for (Anomaly& a : world_.anomalies) {
        if (a.kind != AnomalyKind::Derelict && a.maxHull <= 0.0) InitCombat(a, rules_.anomalies);  // partida anterior al combate
        else if (a.kind != AnomalyKind::Derelict && a.shieldMax <= 0.0) InitShield(a, rules_.anomalies);  // anterior a los escudos
    }
    // Una partida anterior a la fase F no tiene el planeta coraza de su galaxia: se le anade. Uno ya
    // destruido sigue en la lista (con casco 0), asi que no vuelve.
    if (std::none_of(world_.anomalies.begin(), world_.anomalies.end(), [](const Anomaly& a) { return IsArmorPlanet(a); })) {
        int id = 1;
        for (const Anomaly& a : world_.anomalies) id = std::max(id, a.id + 1);
        Anomaly planet;
        if (PlaceArmorPlanet(w.extents, rules_.anomalies, world_.seed, id, planet)) world_.anomalies.push_back(planet);
    }
    world_.species = d.world.species;
    ResizeSpecies(world_.species, w.extents.size());
    bubble_.simTime = d.world.simTime;
    bubble_.lostPieces.clear();
    bubble_.projectiles.clear();
    bubble_.steps = 0;

    // Las naves: una por cada una del mundo, con un jugador en blanco. El foco, en la de quien carga.
    const bool cinematic = pilot_->cinematic, firstPerson = pilot_->firstPerson;
    fleet_.clear();
    crews_.clear();
    focus_ = 0;
    nextVessel_ = 1;
    for (std::size_t i = 0; i < d.world.ships.size(); ++i) {
        fleet_.emplace_back();
        crews_.emplace_back();
        if (&d.world.ships[i] == mine) focus_ = static_cast<int>(i);
        nextVessel_ = std::max(nextVessel_, d.world.ships[i].id + 1);
    }
    for (std::size_t i = 0; i < d.world.ships.size(); ++i) {
        Use(static_cast<int>(i));
        RestoreVessel(d.world.ships[i]);
    }

    // El jugador que carga; los de las demas naves saben lo mismo que el, hasta que carguen lo suyo.
    Use(focus_);
    pilot_->id = d.player.id;
    pilot_->name = d.player.name;
    pilot_->analysisUsed = d.player.analysisUsed;
    pilot_->kills = d.player.kills;
    pilot_->collected = d.player.collected;
    pilot_->known = d.player.knownSystems;
    pilot_->known.resize(w.state.size(), 0);
    pilot_->known[0] = 1;
    pilot_->visited = d.player.visitedSystems;
    pilot_->visited.resize(w.state.size(), 0);
    pilot_->visited[0] = 1;
    pilot_->scanned = std::set<std::uint64_t>(d.player.scanned.begin(), d.player.scanned.end());
    pilot_->cursorUsed = d.player.cursorUsed;
    pilot_->cinematic = cinematic;
    pilot_->firstPerson = firstPerson;
    pilot_->dialogue.Start(MakeTutorial());
    pilot_->dialogue.Resume(d.player.tutorialStep);
    for (std::size_t i = 0; i < crews_.size(); ++i) {
        if (static_cast<int>(i) == focus_) continue;
        PlayerState& other = crews_[i];
        other.id = "local-" + std::to_string(fleet_[i].id);
        other.name = "Piloto " + std::to_string(fleet_[i].id);
        other.known = pilot_->known;
        other.visited = pilot_->visited;
        other.scanned = pilot_->scanned;
        other.cursorUsed = other.analysisUsed = true;
    }
    fleetSystem_ = fleet_.size() > 1 ? eps::NearestSystem(w, vessel_->ship.position) : -1;

    // Las que se guardaron perdidas reaparecen ahora.
    for (std::size_t i = 0; i < d.world.ships.size(); ++i) {
        if (!d.world.ships[i].lost) continue;
        Use(static_cast<int>(i));
        if (ModeInfo(world_.mode).permadeath && static_cast<int>(i) == focus_) ResetCampaign();
        else Respawn();
    }
    Use(focus_);
    return true;
}

// Deja la nave en curso como dice el guardado.
void GameSession::RestoreVessel(const ShipSave& v) {
    const World& w = engine_.GetWorld();
    vessel_->id = v.id;
    vessel_->ship = v.ship;
    vessel_->ship.position = v.absolutePosition - w.origin;
    vessel_->ship.aim = vessel_->ship.orientation;
    vessel_->ship.angularRate = {};
    if (vessel_->ship.weapons.empty()) vessel_->ship.weapons = DefaultWeapons(rules_.ship);
    vessel_->ship.loadout.starter = false;  // lo montado es lo que dice la partida
    RefitToDesign();  // modulos y piezas a la nave cargada; sus torretas salen de los modulos
    // Lo demas empieza de cero: los drones en sus bahias, sin pista del escaner, sin ruta.
    vessel_->phase = GamePhase::Flying;
    if (v.orbitKind != 0) {
        TargetRef orbit;
        orbit.kind = static_cast<Kind>(v.orbitKind);
        orbit.system = v.orbitSystem;
        orbit.index = v.orbitIndex;
        vessel_->orbiting = orbit;
        vessel_->orbitOffset = v.orbitOffset;
        vessel_->orbitNormal = v.orbitNormal;
        vessel_->orbitRate = v.orbitRate;
    }
    vessel_->dockPhase = v.docked && vessel_->orbiting && vessel_->orbiting->kind == Kind::Station ? DockPhase::Docked : DockPhase::None;
    vessel_->homeStation = v.homeStation;
    vessel_->nearestSystem = eps::NearestSystem(w, vessel_->ship.position);
}

void GameSession::ContinueAfterDefeat() {
    if (vessel_->phase != GamePhase::GameOver) return;
    if (ModeInfo(world_.mode).permadeath) {
        vessel_->phase = GamePhase::Flying;
        vessel_->wreckAge = 0;
        ResetCampaign();
        return;
    }
    Respawn();
}

// Tras perder la nave, fuera del modo realista. El mundo no vuelve atras: estaciones, anomalias, reloj
// y lo descubierto siguen como estan, porque puede haber mas naves en el (docs/multijugador.md). La
// nave reaparece reparada y atracada en la ultima estacion propia donde atraco, o en el punto de
// partida si no atraco en ninguna, y pierde la parte de la bodega que diga el modo. Lo que llevaba
// montado y se perdio en combate, se queda perdido.
void GameSession::Respawn() {
    const GameMode& mode = ModeInfo(world_.mode);
    ShipState& s = vessel_->ship;
    s.speed = 0;
    s.velocity = {};
    s.warpSpeed = 0;
    s.holding = true;
    s.throttle = 0;
    s.warpCharge = 0;
    s.angularRate = {};
    s.aim = s.orientation;
    s.hull = rules_.ship.hull;
    s.zones = IntactHull();
    std::fill(s.pieces.begin(), s.pieces.end(), 1.0);
    SyncModuleWeapons();  // las torretas de las piezas repuestas vuelven a contar
    s.energy = rules_.ship.energy;
    s.antimatter = std::max(s.antimatter, rules_.ship.startAntimatter);  // que pueda volver a salir
    s.defense.heat = 0.0;
    // Los emisores ya estan en el motor; la bodega es lo que se pierde.
    if (mode.cargoLoss > 0.0) {
        for (const Resource r : {Resource::Metal, Resource::Silicate, Resource::Ice, Resource::Gas, Resource::StellarMatter}) s.cargo.Add(r, -s.cargo.Get(r) * mode.cargoLoss);
    }
    vessel_->phase = GamePhase::Flying;
    vessel_->wreckAge = 0;
    vessel_->failedZone = -1;
    vessel_->zoneHeat = {};
    vessel_->hullHits.clear();
    vessel_->frame = {};
    vessel_->autopilot.reset();
    vessel_->orbiting.reset();
    vessel_->tracking.reset();
    vessel_->dockPhase = DockPhase::None;
    vessel_->dockWanted = -1;
    vessel_->exploring = -1;
    vessel_->drones.clear();
    vessel_->droneSite.reset();
    vessel_->droneYield = 0.0;
    CancelScan();
    pilot_->onFoot = false;
    bubble_.lostPieces.clear();
    bubble_.projectiles.clear();

    const Station* home = world_.stations.Find(vessel_->homeStation);
    PlaceInSystem(home ? home->site.system : 0);
    vessel_->respawnPending = home != nullptr;
    if (!home) Say("De vuelta en el punto de partida: no hay estación propia en la que hayas atracado.");
    else if (!FinishRespawn()) Say("De vuelta en el sistema de " + home->name + ".");
}

// Atraca la nave que reaparece en su estacion. Si el sistema de la estacion aun no esta cargado, no
// puede: Step lo vuelve a intentar hasta que lo este.
bool GameSession::FinishRespawn() {
    const Station* home = world_.stations.Find(vessel_->homeStation);
    if (!home || vessel_->phase != GamePhase::Flying) {
        vessel_->respawnPending = false;
        return false;
    }
    const TargetRef ref{Kind::Station, home->site.system, home->id};
    const std::optional<TargetInfo> info = Resolve(ref);
    if (!info) return false;
    vessel_->respawnPending = false;
    JumpTo(ref, info->arrival);
    Dock(ref, true);
    Say("De vuelta en " + home->name + ".");
    return true;
}

// Modo realista: no queda nada. Nave nueva, sin estaciones, y las anomalias vuelven a su sitio.
void GameSession::ResetCampaign() {
    const World& w = engine_.GetWorld();
    vessel_->ship = MakeShip(rules_);
    bubble_.lostPieces.clear();
    vessel_->hullHits.clear();
    vessel_->failedZone = -1;
    vessel_->zoneHeat = {};
    world_.stations = StationNetwork(rules_.stations);
    world_.anomalies = GenerateAnomalies(w.extents, rules_.anomalies, world_.seed);
    vessel_->echo = {};
    vessel_->echoTarget = -1;
    world_.species = SpeciesState{};
    ResizeSpecies(world_.species, w.extents.size());
    vessel_->drones.clear();
    vessel_->droneSite.reset();
    vessel_->droneYield = 0.0;
    bubble_.projectiles.clear();
    pilot_->kills = 0;
    pilot_->collected = 0;
    pilot_->known.assign(pilot_->known.size(), 0);
    pilot_->known[0] = 1;
    pilot_->visited = pilot_->known;
    pilot_->scanned.clear();
    PlaceAtStart();
    vessel_->autopilot.reset();
    vessel_->orbiting.reset();
    vessel_->dockPhase = DockPhase::None;
    pilot_->onFoot = false;
    vessel_->exploring = -1;
    vessel_->homeStation = -1;
    vessel_->respawnPending = false;
    Say("Otra tripulación, otro intento.");
}

void GameSession::LoseShip() {
    vessel_->phase = GamePhase::Exploding;
    vessel_->wreckAge = 0;
    vessel_->wreckPosition = vessel_->ship.position;
    vessel_->autopilot.reset();
    vessel_->tracking.reset();
    vessel_->orbiting.reset();
    vessel_->dockPhase = DockPhase::None;
    vessel_->dockWanted = -1;
    pilot_->onFoot = false;
    vessel_->exploring = -1;
    CancelScan();
    vessel_->ship.speed = 0;
    vessel_->ship.velocity = {};
    vessel_->ship.warpSpeed = 0;
    vessel_->ship.throttle = 0;
    Say(vessel_->lossReason.empty() ? "Casco destruido." : vessel_->lossReason);
    if (ModeInfo(world_.mode).permadeath) saveLost_ = true;
}

bool GameSession::JumpTo(const TargetRef& target, double distance) {
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info) return false;
    const Vec3d away = Length(vessel_->ship.position - info->position) > 1.0 ? Normalize(vessel_->ship.position - info->position) : Vec3d{0, 0, -1};
    vessel_->ship.position = info->position + away * distance;
    vessel_->autopilot.reset();
    vessel_->dockPhase = DockPhase::None;
    const Vec3d look = away * -1.0;
    FaceTo(vessel_->ship, look);
    vessel_->ship.moveDirection = look;
    vessel_->ship.warpHeading = look;
    EnterOrbit(target, *info, distance);
    return true;
}

void GameSession::Steer(const ShipControls& c, double realSeconds) {
    if (!vessel_->autopilot && (c.turnYaw != 0.0f || c.turnPitch != 0.0f || c.turnRoll != 0.0f)) {
        // Sobre los ejes de la camara: lo que se ve a la derecha queda a la derecha, sin importar
        // desde donde se mire la nave ni como este girada.
        const Quatd turn = Quatd::FromAxisAngle({0, 1, 0}, static_cast<double>(c.turnYaw)) * Quatd::FromAxisAngle({1, 0, 0}, -static_cast<double>(c.turnPitch)) * Quatd::FromAxisAngle({0, 0, 1}, -static_cast<double>(c.turnRoll));
        vessel_->ship.aim = (vessel_->ship.aim * c.view * turn * c.view.Conjugate()).Normalized();
        LimitAim(vessel_->ship, rules_.ship.aimLead);
    }
    vessel_->ship.throttle = std::clamp(vessel_->ship.throttle + c.throttleChange * realSeconds, -1.0, 1.0);
    if (c.manualBrake) HoldPosition();
}

void GameSession::HoldPosition() {
    vessel_->ship.throttle = 0.0;
    vessel_->ship.holding = true;
    if (vessel_->autopilot) CancelAutopilot();
}

double GameSession::ObjectMass(const Body& b) const {
    const World& w = engine_.GetWorld();
    if (b.isStar) return w.systems[static_cast<std::size_t>(b.system)]->StarAt(b.member).mass;
    const BodyData* data = BodyInfo(w, b);
    return data ? data->mass : 0.0;
}

// Una luna va con su planeta, y este con su estrella.
Vec3d GameSession::WorldVelocity(const Body& b) const {
    const World& w = engine_.GetWorld();
    Vec3d velocity = BodyVelocity(w, b, bubble_.simTime);
    if (b.parent >= 0) velocity += BodyVelocity(w, w.bodies[static_cast<std::size_t>(b.parent)], bubble_.simTime);
    return velocity;
}

void GameSession::RefreshFrame(LocalFrame& f) const {
    const World& w = engine_.GetWorld();
    if (f.body >= 0) {
        const Body& b = w.bodies[static_cast<std::size_t>(f.body)];
        if (!Alive(b)) {
            f = {};
            return;
        }
        f.position = w.scene.WorldPosition(b.node);
        f.velocity = WorldVelocity(b);
    } else if (f.belt >= 0) {
        const AsteroidBelt& belt = w.belts[static_cast<std::size_t>(f.belt)];
        const AsteroidFamily& family = belt.families[static_cast<std::size_t>(f.family)];
        f.position = FamilyPosition(w.scene, belt, family, bubble_.simTime);
        f.velocity = family.fixed ? Vec3d{} : w.scene.World(belt.parent).TransformDirection(OrbitVelocity(family.orbit, bubble_.simTime));
    }
}

// Conica por tramos: dentro de la zona de influencia de un cuerpo solo cuenta su gravedad, y la
// nave se mueve respecto a el. Asi una nave quieta junto a un planeta viaja con el planeta, sin
// tener que perseguirlo, y al cruzar de una zona a otra su velocidad real no cambia.
void GameSession::UpdateFrame() {
    const World& w = engine_.GetWorld();
    const std::size_t system = static_cast<std::size_t>(vessel_->nearestSystem);
    const SystemState& state = w.state[system];
    LocalFrame best;
    best.reach = 1.0e300;

    if (state.loaded) {
        for (const int index : state.bodies) {
            const Body& b = w.bodies[static_cast<std::size_t>(index)];
            if (!Alive(b) || b.kind == PlanetClass::Belt) continue;
            const double mass = ObjectMass(b);
            if (mass <= 0.0) continue;
            const Vec3d center = w.scene.WorldPosition(b.node);
            const Body* host = b.parent >= 0 ? &w.bodies[static_cast<std::size_t>(b.parent)] : (state.starBody >= 0 ? &w.bodies[static_cast<std::size_t>(state.starBody)] : nullptr);
            if (!host || !Alive(*host)) continue;
            const double hostMass = ObjectMass(*host);
            const double reach = std::max(Length(center - w.scene.WorldPosition(host->node)) * std::pow(mass / std::max(hostMass, 1.0), 0.4), b.radius * 5.0);
            if (b.kind == PlanetClass::Asteroid || b.kind == PlanetClass::Comet) continue;  // apenas atraen
            if (Length(vessel_->ship.position - center) >= reach || reach >= best.reach) continue;
            best = {};
            best.body = index;
            best.mu = kGravity * mass;
            best.radius = b.radius;
            best.reach = reach;
        }
    }
    if (best.body < 0) {
        // Un cumulo de asteroides no atrae, pero la nave debe poder viajar con el.
        for (std::size_t bi = 0; bi < w.belts.size() && best.belt < 0; ++bi) {
            const AsteroidBelt& belt = w.belts[bi];
            if (!belt.used || belt.parent == kNoNode) continue;
            const double fromCenter = Length(vessel_->ship.position - w.scene.WorldPosition(belt.parent));
            if (fromCenter > belt.outer * 1.4 + 1000.0) continue;
            for (std::size_t f = 0; f < belt.families.size(); ++f) {
                const AsteroidFamily& family = belt.families[f];
                if (Length(vessel_->ship.position - FamilyPosition(w.scene, belt, family, bubble_.simTime)) < family.radius * 1.3) {
                    best = {};
                    best.belt = static_cast<int>(bi);
                    best.family = static_cast<int>(f);
                    best.radius = family.radius;
                    best.reach = family.radius * 1.3;
                    break;
                }
            }
        }
    }
    if (best.body < 0 && best.belt < 0) {
        best = {};
        double nearest = w.extents[system] * 3.0;
        for (const int index : state.starBodies) {
            const Body& b = w.bodies[static_cast<std::size_t>(index)];
            const double distance = Length(vessel_->ship.position - w.scene.WorldPosition(b.node));
            if (distance >= nearest) continue;
            nearest = distance;
            best.body = index;
            best.mu = kGravity * ObjectMass(b);
            best.radius = b.radius;
            best.reach = w.extents[system] * 3.0;
        }
    }
    RefreshFrame(best);

    // La velocidad real se conserva: solo cambia respecto a quien se mide.
    if (best.body != vessel_->frame.body || best.belt != vessel_->frame.belt || best.family != vessel_->frame.family) vessel_->ship.velocity += vessel_->frame.velocity - best.velocity;
    vessel_->frame = best;
}

std::string GameSession::DominantName() const {
    const World& w = engine_.GetWorld();
    if (vessel_->frame.body >= 0) return w.bodies[static_cast<std::size_t>(vessel_->frame.body)].name;
    if (vessel_->frame.belt >= 0) return w.beltInfo[static_cast<std::size_t>(vessel_->frame.belt)].name;
    return "";
}

// Radio de Schwarzschild entre distancia: mide cuanto se frena el tiempo en un pozo gravitatorio.
double GameSession::GravityWell() const {
    if (vessel_->frame.body < 0 || vessel_->frame.mu <= 0.0) return 0.0;
    const World& w = engine_.GetWorld();
    const Body& b = w.bodies[static_cast<std::size_t>(vessel_->frame.body)];
    const double radius = std::max(Length(vessel_->ship.position - vessel_->frame.position), b.radius);
    const double rs = 2.0 * vessel_->frame.mu / (kLightSpeed * kLightSpeed);
    const bool hole = b.isStar && w.systems[static_cast<std::size_t>(b.system)]->StarAt(b.member).kind == StarClass::BlackHole;
    return rs * (hole ? 1.0 : rules_.world.gravityBoost) / radius;
}

double GameSession::OrbitMu(const TargetRef& target) const {
    const World& w = engine_.GetWorld();
    if (target.kind == Kind::Planet) {
        if (const Body* body = FindPlanet(w, target.system, target.index)) return kGravity * ObjectMass(*body);
    } else if (target.kind == Kind::Star) {
        return kGravity * w.systems[static_cast<std::size_t>(target.system)]->StarAt(target.index).mass;
    }
    return 0.0;
}

// Orbita circular asistida: la nave da vueltas sola, sin gastar nada y sin decaer.
void GameSession::EnterOrbit(const TargetRef& target, const TargetInfo& info, double radius) {
    const Vec3d offset = vessel_->ship.position - info.position;
    const double distance = Length(offset);
    const Vec3d side = distance > 1.0 ? offset * (1.0 / distance) : Vec3d{0, 0, -1};
    Vec3d normal = Cross(side, vessel_->ship.velocity + vessel_->ship.warpHeading * 0.001);
    if (Length(normal) < 1e-6) normal = Cross(side, Heading(vessel_->ship));
    if (Length(normal) < 1e-6) normal = Cross(side, std::abs(side.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0});
    vessel_->orbitNormal = Normalize(normal);
    vessel_->orbitOffset = side * radius;
    const double mu = OrbitMu(target);
    vessel_->orbitRate = mu > 0.0 ? std::sqrt(mu / (radius * radius * radius)) : 0.0;
    vessel_->orbiting = target;
    vessel_->autopilot.reset();
    vessel_->routePhase = RoutePhase::None;
    vessel_->ship.velocity = {};
    vessel_->ship.warpSpeed = 0;
    vessel_->ship.warpCharge = 0;
    vessel_->ship.throttle = 0;
    vessel_->ship.holding = false;
    vessel_->ship.mode = DriveMode::Impulse;
    vessel_->ship.speed = vessel_->orbitRate * radius;
}

// Al encender el motor sale con la velocidad que llevaba en la orbita.
void GameSession::LeaveOrbit() {
    if (const std::optional<TargetInfo> info = Resolve(*vessel_->orbiting)) {
        const Vec3d tangent = Cross(vessel_->orbitNormal, vessel_->orbitOffset) * vessel_->orbitRate;
        vessel_->ship.velocity = tangent + info->velocity - vessel_->frame.velocity;
    }
    // Recien soltada, la proa aun toca la escotilla: esa estacion no choca hasta que se aparte.
    if (vessel_->dockPhase != DockPhase::None && vessel_->orbiting->kind == Kind::Station) vessel_->undockGrace = vessel_->orbiting->index;
    vessel_->orbiting.reset();
    vessel_->ship.holding = false;
    if (vessel_->dockPhase != DockPhase::None) Say("Amarras sueltas.");
    vessel_->dockPhase = DockPhase::None;
}

// ---- Atraque ---------------------------------------------------------------------------------

const Station* GameSession::DockedStation() const {
    return vessel_->dockPhase != DockPhase::None && vessel_->orbiting && vessel_->orbiting->kind == Kind::Station ? world_.stations.Find(vessel_->orbiting->index) : nullptr;
}

bool GameSession::Dock(const TargetRef& target, bool instant) {
    if (vessel_->phase != GamePhase::Flying || pilot_->onFoot) return false;
    if (target.kind != Kind::Station || !world_.stations.Find(target.index) || !Resolve(target)) {
        Say("Solo se puede atracar en una estación propia.");
        return false;
    }
    if (vessel_->orbiting && *vessel_->orbiting == target && vessel_->dockPhase != DockPhase::None) return true;
    if (InRange(target, rules_.ship.interactRange) && vessel_->ship.warpSpeed <= rules_.drive.impulseSpeed) {
        BeginDock(target);
        if (instant) {
            const Station* s = world_.stations.Find(target.index);
            vessel_->orbitOffset = DockGoal(*s);
            vessel_->ship.orientation = vessel_->ship.aim = DockAttitude(*s);
            vessel_->ship.angularRate = {};
            vessel_->dockPhase = DockPhase::Docked;
        }
        return true;
    }
    vessel_->dockWanted = target.index;
    if (SetAutopilot(target)) return true;
    vessel_->dockWanted = -1;
    return false;
}

void GameSession::BeginDock(const TargetRef& target) {
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info) return;
    vessel_->autopilot.reset();
    vessel_->exploring = -1;
    CancelScan();
    EnterOrbit(target, *info, std::max(Length(vessel_->ship.position - info->position), info->radius + rules_.ship.bow));
    vessel_->orbitRate = 0.0;
    vessel_->dockedAge = 0.0;
    vessel_->dockPhase = DockPhase::Approaching;
    vessel_->dockWanted = -1;
    Say("Haz de atraque enganchado.");
}

void GameSession::Undock() {
    if (vessel_->dockPhase == DockPhase::None || !vessel_->orbiting) return;
    LeaveOrbit();
}

// El haz acerca la nave sin que importe lo que lleve: no se puede rebotar ni pasar de largo. Primero
// la rodea, sin atravesarla, hasta ponerla frente a la escotilla; despues la mete en linea recta,
// con la proa por delante. Atracada, la sigue aunque la estacion gire.
void GameSession::StepDock(double realSeconds) {
    if (vessel_->dockPhase == DockPhase::None) return;
    const Station* s = DockedStation();
    if (!s || !StationVisible(*s)) return;
    const DockPort port = Port(*s);
    const Vec3d goal = DockGoal(*s);
    // De costado: el haz la gira hasta que su compuerta de estribor mira a la escotilla.
    vessel_->ship.aim = DockAttitude(*s);
    if (vessel_->dockPhase == DockPhase::Docked) {
        vessel_->orbitOffset = goal;
        vessel_->ship.orientation = vessel_->ship.aim;
        vessel_->ship.angularRate = {};
        vessel_->dockedAge += realSeconds;
        return;
    }
    const double keep = std::exp(-realSeconds / rules_.ship.dockTime);
    const Vec3d corridor = goal + port.normal * rules_.ship.dockCorridor;
    const Vec3d fromGoal = vessel_->orbitOffset - goal;
    const double along = Dot(fromGoal, port.normal);
    const double aside = Length(fromGoal - port.normal * along);
    if (along > -0.01 && aside < 0.03) {
        // En el pasillo: recta hacia la escotilla.
        vessel_->orbitOffset = goal + fromGoal * keep;
    } else {
        // Alrededor: el angulo y la altura bajan a la vez, sin pasar nunca mas cerca que el pasillo.
        const double radius = Length(vessel_->orbitOffset), target = Length(corridor);
        const Vec3d from = vessel_->orbitOffset * (1.0 / std::max(radius, 1e-9)), to = Normalize(corridor);
        const double angle = std::acos(std::clamp(Dot(from, to), -1.0, 1.0));
        Vec3d axis = Cross(from, to);
        if (Length(axis) < 1e-9) axis = Cross(from, std::abs(from.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0});
        const double turn = angle < 0.002 ? angle : angle * (1.0 - keep);
        const Vec3d direction = Quatd::FromAxisAngle(Normalize(axis), turn).ToMatrix().TransformDirection(from);
        const double reach = target + (radius - target) * keep;
        vessel_->orbitOffset = angle < 0.002 && std::abs(reach - target) < 0.01 ? corridor : direction * std::max(reach, std::min(radius, target));
    }
    if (Length(vessel_->orbitOffset - goal) > 0.01) return;
    vessel_->orbitOffset = goal;
    vessel_->dockPhase = DockPhase::Docked;
    Say("Atracada en " + s->name + ".");
    saveWanted_ = true;
}

// Junto a una estacion la nave no corre: cuanto mas cerca, mas se le recorta la velocidad respecto a
// ella. Sin mando, los estabilizadores frenan la deriva: se queda a su lado en vez de alejarse.
void GameSession::LimitNearStations(double realSeconds) {
    const ShipRules& r = rules_.ship;
    for (const Station& s : world_.stations.All()) {
        if (s.site.system != vessel_->nearestSystem || !StationVisible(s)) continue;
        const Vec3d at = StationPosition(s);
        const double distance = Length(at - vessel_->ship.position);
        if (distance > rules_.world.stationNear) continue;
        const std::optional<TargetInfo> info = Resolve({Kind::Station, s.site.system, s.id});
        if (!info) continue;
        const Vec3d matched = info->velocity - vessel_->frame.velocity;
        Vec3d relative = vessel_->ship.velocity - matched;
        // En la zona de atraque, casi parada; mas lejos, acercarse va despacio y alejarse, menos.
        const bool closing = Dot(relative, at - vessel_->ship.position) > 0.0;
        const bool zone = distance < r.dockZone;
        const double top = StationTop(distance, closing);
        if (!vessel_->approach || distance < vessel_->approach->distance) vessel_->approach = ApproachInfo{s.name, distance, top, zone};
        if (vessel_->ship.throttle == 0.0) relative = relative * std::exp(-realSeconds / rules_.ship.driftDamping);
        const double speed = Length(relative);
        vessel_->ship.velocity = matched + (speed > top ? relative * (top / speed) : relative);
        vessel_->ship.warpSpeed = std::min(vessel_->ship.warpSpeed, top);
    }
}

// Los topes se piensan en segundos reales, que es lo que ve el jugador: el tiempo se frena al
// acercarse (UpdateFocus), y sin esto la nave se arrastraria. Al lado va a tiempo real y mandan los
// propulsores finos. Devuelve km/s simulados.
double GameSession::StationTop(double distance, bool closing) const {
    const ShipRules& r = rules_.ship;
    const double rate = std::max(world_.timeScale * bubble_.timeFocus, 1e-6);
    double real = 0.0;  // km por segundo real
    if (distance < r.maneuverRange) real = closing ? std::max(distance / r.maneuverTime, r.maneuverMinimum) : std::max(distance / r.maneuverLeave, r.maneuverLeaveMinimum);
    else real = distance / (closing ? r.approachTime : r.recedeTime);
    return real / rate;
}

int GameSession::StationFrame() const {
    int best = -1;
    double nearest = rules_.world.stationNear;
    for (const Station& s : world_.stations.All()) {
        if (s.site.system != vessel_->nearestSystem || !StationVisible(s)) continue;
        // Una origami que aun se aparta de la nave no la lleva consigo: se iria con ella.
        if (s.kind == StationKind::Shipyard && ShipyardDeploy(s.deployAge).slide < 1.0) continue;
        const double distance = Length(StationPosition(s) - vessel_->ship.position);
        if (distance < nearest) {
            nearest = distance;
            best = s.id;
        }
    }
    return best;
}

// ---- A pie -----------------------------------------------------------------------------------

bool GameSession::Disembark() {
    const Station* s = DockedStation();
    if (!s || !Docked() || pilot_->onFoot) {
        Say("Hay que estar atracada para bajar.");
        return false;
    }
    bubble_.deckKind = s->kind;
    bubble_.deck = MakeDeck(bubble_.deckKind);
    pilot_->avatar = {};
    pilot_->avatar.position = bubble_.deck.spawn;
    pilot_->avatar.yaw = 1.5707963;  // de espaldas a la esclusa
    bubble_.crew = MakeCrew(bubble_.deck, body_, rules_.foot.crew, bubble_.fx);
    pilot_->onFoot = true;
    Say("A pie en " + s->name + ".");
    return true;
}

bool GameSession::CanBoard() const {
    if (!pilot_->onFoot) return false;
    Vec3d to = bubble_.deck.airlock - pilot_->avatar.position;
    to.y = 0.0;
    return Length(to) <= rules_.foot.boardRange;
}

bool GameSession::Board(bool anywhere) {
    if (!pilot_->onFoot) return false;
    if (!anywhere && !CanBoard()) {
        Say("La esclusa está lejos.");
        return false;
    }
    pilot_->onFoot = false;
    bubble_.crew.clear();
    Say("De vuelta a bordo.");
    return true;
}

void GameSession::StepFoot(const FootControls& c, double realSeconds) {
    pilot_->avatar.yaw += c.yaw;
    pilot_->avatar.pitch = std::clamp(pilot_->avatar.pitch + c.pitch, -1.45, 1.45);
    if (realSeconds <= 0.0) return;
    StepHumanoid(pilot_->avatar, c.move, bubble_.deck.area, body_, rules_.foot.gravity, realSeconds);
    for (Person& p : bubble_.crew) StepPerson(p, bubble_.deck, body_, rules_.foot.gravity, realSeconds, bubble_.fx);
}

bool GameSession::EnterOrbitHere() {
    const World& w = engine_.GetWorld();
    if (vessel_->orbiting) return true;
    if (vessel_->frame.body < 0 || vessel_->ship.warpSpeed > rules_.drive.impulseSpeed) {
        Say("Aquí no hay nada que orbitar.");
        return false;
    }
    const Body& b = w.bodies[static_cast<std::size_t>(vessel_->frame.body)];
    TargetRef target;
    target.system = b.system;
    target.kind = b.isStar ? Kind::Star : Kind::Planet;
    target.index = b.isStar ? b.member : b.data;
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info) return false;
    const double radius = Length(vessel_->ship.position - info->position);
    if (radius < b.radius * 1.05) {
        Say("Demasiado bajo para orbitar.");
        return false;
    }
    // Pasar a la velocidad circular cuesta energia.
    const double circular = std::sqrt(vessel_->frame.mu / radius);
    vessel_->ship.energy = std::max(vessel_->ship.energy - std::abs(circular - Length(vessel_->ship.velocity)) * 0.05, 0.0);
    EnterOrbit(target, *info, radius);
    Say("Órbita estable alrededor de " + info->name + ".");
    return true;
}

// La ruta: orientarse, formar la burbuja, viajar, frenar y entrar en orbita, rodeando lo que se
// cruce. Devuelve lo que debe hacer el motor en este paso.
DriveInput GameSession::FlyRoute(double seconds, Vec3d& wanted) {
    DriveInput order;
    const std::optional<TargetInfo> info = Resolve(*vessel_->autopilot);
    if (!info) {
        Say("Objetivo perdido.");
        vessel_->autopilot.reset();
        vessel_->routePhase = RoutePhase::None;
        return order;
    }
    const Vec3d toTarget = info->position - vessel_->ship.position;
    const double distance = Length(toTarget);
    const double remaining = distance - info->arrival;
    vessel_->routeDistance = remaining;
    if (remaining <= std::max(vessel_->ship.speed * seconds * 1.5, 3.0)) {
        const TargetRef arrived = *vessel_->autopilot;
        if (arrived.kind == Kind::Station && vessel_->dockWanted == arrived.index) {
            BeginDock(arrived);
            return order;
        }
        Say("En órbita de " + info->name + ".");
        EnterOrbit(arrived, *info, std::max(std::min(distance, info->arrival), info->radius * 1.05));
        return order;
    }

    Vec3d direction = toTarget * (1.0 / std::max(distance, 1e-6));
    const Vec3d matched = info->velocity - vessel_->frame.velocity;  // viaja con el objetivo: asi lo alcanza
    Vec3d carried = matched;
    // La burbuja reacciona con retraso, y fuera pasa mas tiempo que a bordo. Con este margen frena
    // sin pasarse nunca: cada factor e de distancia lleva unos segundos, pero llega entera.
    const double response = rules_.drive.warpBrake * 4.0 * vessel_->exteriorFactor;
    double warpLimit = remaining / std::max(response, seconds * 1.5);
    const double handling = 1.0 + rules_.ship.handlingBonus * vessel_->ship.handlingTier;
    const double accel = rules_.drive.impulseAccel * handling;
    double cruise = std::min(rules_.drive.impulseSpeed, std::sqrt(2.0 * accel * std::max(remaining, 0.0)) * 0.8);

    // Lo primero que se cruce en el camino se rodea, por la tangente a una altura segura.
    const World& w = engine_.GetWorld();
    const Body* obstacle = nullptr;
    double nearest = remaining, shell = 0, open = 1.0e300;
    for (const Body& b : w.bodies) {
        if (!Alive(b) || b.kind == PlanetClass::Belt || b.radius < 50.0) continue;
        const Vec3d center = w.scene.WorldPosition(b.node);
        const Vec3d offset = vessel_->ship.position - center;
        const double safe = (b.isStar ? b.radius * rules_.ship.heatReach : b.radius * rules_.ship.clearance) + 100.0;
        open = std::min(open, Length(offset) - safe);  // cerca de una masa no se corre, aunque no estorbe
        if (Length(center - info->position) < 1.0) continue;
        const double along = -Dot(offset, direction);
        if (along <= 0.0 || along >= nearest) continue;
        if (Length(offset + direction * along) >= safe) continue;
        obstacle = &b;
        nearest = along;
        shell = safe;
    }
    bool rounding = false;
    warpLimit = std::min(warpLimit, std::max(open, 0.0) / std::max(response, seconds * 1.5) + rules_.drive.impulseSpeed);
    if (obstacle) {
        const Vec3d offset = vessel_->ship.position - w.scene.WorldPosition(obstacle->node);
        const double height = Length(offset);
        const Vec3d normal = offset * (1.0 / std::max(height, 1e-6));
        Vec3d side = direction - normal * Dot(direction, normal);
        if (Length(side) < 1e-3) side = Heading(vessel_->ship) - normal * Dot(Heading(vessel_->ship), normal);
        if (Length(side) < 1e-3) side = Cross(normal, std::abs(normal.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0});
        side = Normalize(side);
        if (height > shell) {
            const double sine = shell / height;
            direction = normal * -std::sqrt(1.0 - sine * sine) + side * sine;
        } else {
            direction = Normalize(side + normal * ((shell - height) / shell * 2.0));
        }
        // Tan despacio como haga falta para que el casco pueda trazar la curva.
        const double turn = rules_.ship.turnRate * handling;
        const double curve = turn * shell / std::max(world_.timeScale * vessel_->exteriorFactor, 1.0);
        const double approach = std::max(height - shell, 0.0) / std::max(response, seconds * 1.5);
        warpLimit = std::min(warpLimit, approach + curve);
        cruise = std::min(cruise, approach + curve);
        carried = WorldVelocity(*obstacle) - vessel_->frame.velocity;
        rounding = height < shell * 3.0;
    }
    AimAt(vessel_->ship, direction);
    const bool aligned = AimError(vessel_->ship) <= kAlignedAngle * 3.0;
    const double facing = std::max(Dot(Heading(vessel_->ship), direction), 0.0);  // ya avanza mientras gira

    // Lejos, crucero. Entre estrellas, burbuja. Cerca, el motor normal.
    const bool bubble = remaining > kWarpRouteMin && !rounding;
    if (bubble) {
        const DriveMode curve = HyperUnlocked(vessel_->ship, rules_.drive) ? DriveMode::Hyper : DriveMode::Warp;
        const bool far = remaining > kCurveRouteMin;
        const bool curving = far && CanCurve(vessel_->ship, curve, rules_.drive);
        if (far && !curving && !vessel_->fuelWarned) {
            vessel_->fuelWarned = true;
            Say(HybridUnlocked(vessel_->ship) ?"Sin antimateria para la burbuja: la ruta sigue en crucero." : "Sin motor híbrido: la ruta sigue en crucero.");
        }
        vessel_->ship.mode = curving ? curve : DriveMode::Cruise;
        vessel_->ship.throttle = 1.0;
        // El crucero arranca al momento: hasta estar alineada, nada de correr hacia donde no toca.
        order.warpLimit = aligned ? std::max(warpLimit, 1.0) : 1.0;
        wanted = carried + direction * (vessel_->ship.warpSpeed > rules_.drive.impulseSpeed ? 0.0 : cruise * facing);
        vessel_->routePhase = !aligned ? RoutePhase::Aligning : vessel_->ship.warpSpeed > warpLimit * 1.02 ? RoutePhase::Braking : RoutePhase::Cruising;
    } else {
        vessel_->ship.mode = DriveMode::Impulse;
        vessel_->ship.throttle = 0.0;
        wanted = carried + direction * (cruise * facing);
        vessel_->routePhase = rounding ? RoutePhase::Rounding : !aligned ? RoutePhase::Aligning : RoutePhase::Braking;
    }
    order.wanted = &wanted;
    return order;
}

RouteReport GameSession::Route() const {
    RouteReport r;
    if (!vessel_->autopilot) return r;
    r.phase = vessel_->routePhase;
    r.distance = vessel_->routeDistance;
    if (const std::optional<TargetInfo> info = Resolve(*vessel_->autopilot)) r.target = info->name;
    const double rate = vessel_->ship.speed * world_.timeScale * vessel_->exteriorFactor;
    r.seconds = rate > 1.0 ? vessel_->routeDistance / rate : 0.0;
    return r;
}

// La orbita que lleva ahora: la que seguiria si apagase todo.
OrbitReport GameSession::OrbitNow() const {
    OrbitReport o;
    if (vessel_->orbiting) {
        const std::optional<TargetInfo> info = Resolve(*vessel_->orbiting);
        if (!info) return o;
        const double height = Length(vessel_->orbitOffset) - info->radius;
        o.valid = true;
        o.locked = true;
        o.body = info->name;
        o.periapsis = o.apoapsis = height;
        o.period = vessel_->orbitRate > 0.0 ? 2.0 * kPi / vessel_->orbitRate : 0.0;
        return o;
    }
    if (vessel_->frame.mu <= 0.0 || vessel_->ship.holding) return o;
    const Vec3d r = vessel_->ship.position - vessel_->frame.position;
    const double radius = Length(r), speed2 = Dot(vessel_->ship.velocity, vessel_->ship.velocity);
    const double energy = speed2 * 0.5 - vessel_->frame.mu / radius;
    const Vec3d h = Cross(r, vessel_->ship.velocity);
    const double p = Dot(h, h) / vessel_->frame.mu;
    o.valid = true;
    o.body = DominantName();
    if (energy >= 0.0) {
        o.escaping = true;
        return o;
    }
    const double a = -vessel_->frame.mu / (2.0 * energy);
    const double e = std::sqrt(std::max(1.0 - p / a, 0.0));
    o.periapsis = a * (1.0 - e) - vessel_->frame.radius;
    o.apoapsis = a * (1.0 + e) - vessel_->frame.radius;
    o.period = 2.0 * kPi * std::sqrt(a * a * a / vessel_->frame.mu);
    return o;
}

// Una explosion, un combate o un choque inminente no pueden pasar a 60 veces la velocidad real.
void GameSession::UpdateFocus(double realSeconds) {
    const World& w = engine_.GetWorld();
    double wanted = 1.0;
    if (pilot_->onFoot) {
        wanted = 1.0 / std::max(world_.timeScale, 1.0);  // una persona vive a tiempo real
    } else if (vessel_->phase != GamePhase::Flying) {
        wanted = rules_.world.wreckTime;
    } else {
        if (vessel_->warning && vessel_->warning->seconds < 8.0) wanted = rules_.world.dangerTime;
        if (Overheated()) wanted = rules_.world.dangerTime;  // da tiempo a salir de donde quema
        if (ModeInfo(world_.mode).hostilesAttack) {
            const Vec3d sector = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(vessel_->nearestSystem)]);
            for (const Anomaly& a : world_.anomalies) {
                if (a.system != vessel_->nearestSystem || a.stance != Stance::Hostile || a.hull <= 0.0) continue;
                if (Length(sector + a.offset - vessel_->ship.position) < std::max(rules_.anomalies.hostileWeaponRange * 2.0, Scaled(rules_.combat, a.weapon, a.weaponTier).range * 1.3)) wanted = rules_.world.dangerTime;
            }
        }
        // Un proyectil en vuelo cerca de la nave tambien pide tiempo para reaccionar, sea quien sea el que dispara.
        for (const Projectile& p : bubble_.projectiles) {
            if (!p.dead && Length(p.position - vessel_->ship.position) < rules_.combat.defenseRange * 4.0) wanted = rules_.world.dangerTime;
        }
        // Junto a una estacion, mas despacio; pegada a ella, a tiempo real, para arrimarse a la escotilla.
        const double real = 1.0 / std::max(world_.timeScale, 1.0);
        for (const Station& s : world_.stations.All()) {
            if (s.site.system != vessel_->nearestSystem || !StationVisible(s)) continue;
            const double d = Length(StationPosition(s) - vessel_->ship.position), fine = rules_.ship.maneuverRange;
            if (d >= rules_.world.stationNear) continue;
            double near = rules_.world.stationTime;
            if (d < fine && vessel_->dockPhase != DockPhase::Docked) near = real;  // atracada no hay que maniobrar
            else if (d < fine * 10.0) near = real * std::pow(rules_.world.stationTime / real, std::log10(d / fine));
            wanted = std::min(wanted, std::max(near, real));
        }
    }
    vessel_->timeFocus += (wanted - vessel_->timeFocus) * (1.0 - std::exp(-realSeconds / 0.35));
}

std::optional<CollisionWarning> GameSession::FindWarning() const {
    if (vessel_->phase != GamePhase::Flying || vessel_->orbiting || vessel_->autopilot) return std::nullopt;  // la ruta ya se ocupa
    const World& w = engine_.GetWorld();
    const double rate = std::max(world_.timeScale * bubble_.timeFocus * vessel_->exteriorFactor, 1.0);
    std::optional<CollisionWarning> out;
    double soonest = 30.0;  // segundos reales
    const Vec3d velocity = vessel_->ship.velocity + vessel_->ship.warpHeading * vessel_->ship.warpSpeed;
    for (const Body& b : w.bodies) {
        if (!Alive(b) || b.kind == PlanetClass::Belt) continue;
        const double limit = b.radius * (b.isStar ? rules_.ship.heatReach * 0.5 : 1.0);
        const Vec3d offset = vessel_->ship.position - w.scene.WorldPosition(b.node);
        const Vec3d relative = velocity + vessel_->frame.velocity - WorldVelocity(b);
        const double speed = Length(relative);
        if (speed < 0.05) continue;
        const Vec3d direction = relative * (1.0 / speed);
        const double along = -Dot(offset, direction);
        if (along <= 0.0) continue;
        const double miss = Length(offset + direction * along);
        if (miss >= limit) continue;
        const double seconds = (along - std::sqrt(limit * limit - miss * miss)) / (speed * rate);
        if (seconds >= soonest) continue;
        soonest = std::max(seconds, 0.0);
        out = CollisionWarning{b.name, soonest, b.isStar};
    }
    return out;
}

std::vector<Vec3d> GameSession::PredictedPath() const {
    std::vector<Vec3d> path;
    constexpr int kPoints = 96;
    if (vessel_->orbiting) {
        const std::optional<TargetInfo> info = Resolve(*vessel_->orbiting);
        if (!info || vessel_->orbitRate <= 0.0) return path;
        const Vec3d side = Cross(vessel_->orbitNormal, vessel_->orbitOffset);
        // La nave esta aqui y la camara con ella: los puntos se aprietan junto a la nave.
        constexpr int kHalf = 180;
        for (int i = -kHalf; i <= kHalf; ++i) {
            const double t = static_cast<double>(i) / kHalf;
            const double a = kPi * t * t * t;
            path.push_back(info->position + vessel_->orbitOffset * std::cos(a) + side * std::sin(a));
        }
        return path;
    }
    if (vessel_->autopilot) {
        const std::optional<TargetInfo> info = Resolve(*vessel_->autopilot);
        if (!info) return path;
        path.push_back(vessel_->ship.position);
        path.push_back(info->position + Normalize(vessel_->ship.position - info->position) * info->arrival);
        // La orbita de llegada.
        const Vec3d in = Normalize(vessel_->ship.position - info->position);
        const Vec3d other = Normalize(Cross(in, std::abs(in.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0}));
        for (int i = 0; i <= kPoints; ++i) {
            const double a = 2.0 * kPi * i / kPoints;
            path.push_back(info->position + (in * std::cos(a) + other * std::sin(a)) * info->arrival);
        }
        return path;
    }
    if (vessel_->ship.holding || vessel_->ship.speed < 0.5 || vessel_->ship.warpSpeed > rules_.drive.impulseSpeed) return path;

    // Inercia y gravedad, un buen trozo por delante.
    Vec3d r = vessel_->ship.position - vessel_->frame.position, v = vessel_->ship.velocity;
    double period = 4.0 * 3600.0;
    if (vessel_->frame.mu > 0.0) {
        const double radius = Length(r);
        period = std::min(period * 6.0, 2.0 * kPi * std::sqrt(radius * radius * radius / vessel_->frame.mu));
    }
    // El paso empieza en un par de km, porque la camara esta a un par de km, y crece con lo recorrido:
    // lejos, un tramo largo sigue pareciendo curvo. Salto de rana: la orbita se cierra sola.
    const auto pull = [&](const Vec3d& at) {
        const double d = Length(at);
        return vessel_->frame.mu > 0.0 ? at * (-vessel_->frame.mu / (d * d * d)) : Vec3d{};
    };
    double elapsed = 0;
    for (int i = 0; i < kPoints * 10 && elapsed < period; ++i) {
        const double radius = Length(r), speed = std::max(Length(v), 1e-3);
        if (radius < vessel_->frame.radius) break;
        path.push_back(vessel_->frame.position + r);
        const double dt = std::min(std::max(elapsed * 0.03, 2.0 / speed), std::min(radius * 0.02 / speed, period / 120.0));
        const Vec3d before = pull(r);
        r += v * dt + before * (0.5 * dt * dt);
        v += (before + pull(r)) * (0.5 * dt);
        elapsed += dt;
    }
    return path;
}

// Una copia de la nave vuela sola los proximos segundos: con el mando puesto, girando hacia donde
// apunta y con la gravedad de aqui. Junto a una estacion, en su marco y con sus topes.
std::vector<Vec3d> GameSession::ManeuverPath(double realSeconds) const {
    std::vector<Vec3d> path;
    if (vessel_->phase != GamePhase::Flying || vessel_->orbiting || pilot_->onFoot || realSeconds <= 0.0) return path;
    constexpr int kSteps = 160;
    const double dt = realSeconds / kSteps;
    const double rate = world_.timeScale * bubble_.timeFocus;
    ShipState s = vessel_->ship;
    Vec3d origin = vessel_->frame.position, matched;
    const int anchor = vessel_->autopilot ? -1 : StationFrame();
    if (anchor >= 0) {
        const std::optional<TargetInfo> info = Resolve({Kind::Station, vessel_->nearestSystem, anchor});
        origin = info->position;
        matched = info->velocity - vessel_->frame.velocity;
    }
    s.position = vessel_->ship.position - origin;
    s.velocity = vessel_->ship.velocity - matched;
    path.push_back(vessel_->ship.position);
    for (int i = 0; i < kSteps; ++i) {
        StepHeading(s, rules_.ship, dt);
        DriveInput order;
        const double d = std::max(Length(s.position), 1.0);
        if (anchor < 0 && vessel_->frame.mu > 0.0) order.gravity = s.position * (-vessel_->frame.mu / (d * d * d));
        order.dragScale = anchor >= 0 || (vessel_->frame.body >= 0 && !engine_.GetWorld().bodies[static_cast<std::size_t>(vessel_->frame.body)].isStar) ? 0.0 : 1.0;
        StepDrive(s, rules_, dt * rate, dt * rate * vessel_->exteriorFactor, dt, order);
        if (anchor >= 0) {
            const double distance = Length(s.position);
            const double top = StationTop(distance, Dot(s.velocity, s.position) < 0.0);
            if (s.throttle == 0.0) s.velocity = s.velocity * std::exp(-dt / rules_.ship.driftDamping);
            const double speed = Length(s.velocity);
            if (speed > top) s.velocity = s.velocity * (top / speed);
        } else if (vessel_->frame.body >= 0 && Length(s.position) < vessel_->frame.radius) {
            break;
        }
        path.push_back(origin + s.position);
    }
    return path;
}

void GameSession::LoseShipTo(const std::string& reason) {
    if (vessel_->phase != GamePhase::Flying) return;
    vessel_->lossReason = reason;
    vessel_->warning.reset();
    LoseShip();
}

// Un cuerpo no se atraviesa. Por debajo de cierta velocidad, el golpe abolla el casco; por encima,
// la nave se deshace. Lo que se lleva por dentro se pierde: nunca se impide alejarse.
void GameSession::Impact(const std::string& name, const Vec3d& normal, const Vec3d& bodyVelocity, bool star) {
    const Vec3d world = vessel_->ship.velocity + vessel_->ship.warpHeading * vessel_->ship.warpSpeed + vessel_->frame.velocity;
    const double closing = -Dot(world - bodyVelocity, normal);
    if (closing <= 0.0) return;

    const GameMode& mode = ModeInfo(world_.mode);
    if (mode.damage > 0.0 && !vessel_->creative) {  // en creativo choca igual, pero sin dano
        if (star) return LoseShipTo("Nave vaporizada en " + name + ".");
        if (closing >= rules_.ship.shatterSpeed) return LoseShipTo("Nave pulverizada contra " + name + ".");
        const double share = closing / rules_.ship.shatterSpeed;
        const double taken = rules_.ship.hull * share * share * mode.damage;
        vessel_->ship.hull = std::max(vessel_->ship.hull - taken, 0.0);
        Say("Impacto contra " + name + ".");
        WoundHull(taken, normal * -1.0, true);  // el golpe llega por el lado que da al cuerpo
    }
    // Se queda en la superficie, con lo que no iba hacia dentro.
    vessel_->ship.warpSpeed = 0.0;
    vessel_->ship.warpCharge = 0.0;
    const double left = -Dot(vessel_->ship.velocity + vessel_->frame.velocity - bodyVelocity, normal);
    if (left > 0.0) vessel_->ship.velocity += normal * left;
}

// from es donde estaba antes de este paso: a gran velocidad un paso puede saltarse un planeta
// entero, asi que se comprueba todo el tramo recorrido y no solo el final.
void GameSession::KeepOutOfBodies(const Vec3d& from) {
    const World& w = engine_.GetWorld();
    const Vec3d travel = vessel_->ship.position - from;
    const double length = Length(travel);
    double first = 2.0;
    Vec3d hit, hitCenter;
    const Body* struck = nullptr;
    for (const Body& b : w.bodies) {
        if (!Alive(b) || b.kind == PlanetClass::Belt) continue;
        const Vec3d center = w.scene.WorldPosition(b.node);
        const double limit = b.radius + kSoftTouch;
        const Vec3d offset = vessel_->ship.position - center;
        const double distance = Length(offset);
        if (distance < limit && distance > 0.0) {
            vessel_->ship.position = center + offset * (limit / distance);
            Impact(b.name, offset * (1.0 / distance), WorldVelocity(b), b.isStar);
            continue;
        }
        if (length < limit * 0.1) continue;
        const Vec3d start = from - center;
        const double half = Dot(start, travel), squared = Dot(start, start) - limit * limit;
        const double inside = half * half - length * length * squared;
        if (squared <= 0.0 || half >= 0.0 || inside <= 0.0) continue;
        const double t = (-half - std::sqrt(inside)) / (length * length);
        if (t < first && t <= 1.0) {
            first = t;
            hit = from + travel * t;
            hitCenter = center;
            struck = &b;
        }
    }
    if (struck) {
        vessel_->ship.position = hit;
        Impact(struck->name, Normalize(hit - hitCenter), WorldVelocity(*struck), struck->isStar);
    }
}

// Naves, bastiones, estructuras y estaciones propias tambien son solidos. Se barre el tramo
// recorrido, como con los astros. Embestir una nave o un bastion le hace el mismo dano que recibe
// el casco propio; atracando, la estacion deja pasar a la nave.
void GameSession::KeepOutOfStructures(const Vec3d& from) {
    const Vec3d travel = vessel_->ship.position - from;
    const double squared = std::max(Dot(travel, travel), 1e-12);
    struct Solid { Vec3d center, velocity; double radius; std::string name; int anomaly; int vessel; };
    std::vector<Solid> solids;
    for (const Anomaly& a : world_.anomalies) {
        if (a.system != vessel_->nearestSystem || (a.maxHull > 0.0 && a.hull <= 0.0)) continue;
        solids.push_back({AnomalyPosition(a), a.velocity, a.radius, a.name, a.id, -1});
    }
    // Las demas naves de la flota tambien son solidas: chocar con una dana a las dos.
    for (std::size_t i = 0; i < fleet_.size(); ++i) {
        const Vessel& v = fleet_[i];
        if (&v == vessel_ || v.phase != GamePhase::Flying) continue;
        solids.push_back({v.ship.position, VelocityOf(v), rules_.ship.radius, "la nave " + std::to_string(v.id), -1, static_cast<int>(i)});
    }
    // Las estaciones propias son pequenas al lado de la nave (1,2 km): cuenta su eslora entera, de
    // proa a popa, y no una esfera que no dejaria arrimarse a la escotilla.
    if (vessel_->dockPhase == DockPhase::None) {
        const Vec3d heading = Heading(vessel_->ship);
        const double half = rules_.ship.bow;
        for (const Station& s : world_.stations.All()) {
            if (s.site.system != vessel_->nearestSystem || !StationVisible(s)) continue;
            const std::optional<TargetInfo> info = Resolve({Kind::Station, s.site.system, s.id});
            if (!info) continue;
            double limit = info->radius + rules_.ship.beam;
            const double along = std::clamp(Dot(info->position - vessel_->ship.position, heading), -half, half);
            Vec3d nearest = vessel_->ship.position + heading * along;
            Vec3d away = nearest - info->position;
            double gap = Length(away);
            if (s.kind == StationKind::Shipyard) {
                // Un anillo hueco: cuenta la distancia de la eslora al tubo, no a una esfera. Se puede
                // pasar por dentro.
                constexpr double kRing = 0.47, kTube = 0.045;  // km: radio de los nudos y su grueso
                const Vec3d axis = StationRotation(s).ToMatrix().TransformDirection(Vec3d{0, -1, 0});
                limit = kTube + rules_.ship.beam * 0.5;
                gap = 1e300;
                for (int k = 0; k <= 16; ++k) {
                    const Vec3d p = vessel_->ship.position + heading * (half * (static_cast<double>(k) / 8.0 - 1.0));
                    const Vec3d v = p - info->position;
                    const Vec3d radial = v - axis * Dot(v, axis);
                    const double r = Length(radial);
                    if (r < 1e-9) continue;
                    const Vec3d onRing = info->position + radial * (kRing / r);
                    const double d = Length(p - onRing);
                    if (d < gap) {
                        gap = d;
                        nearest = p;
                        away = p - onRing;
                    }
                }
            }
            if (s.id == vessel_->undockGrace) {
                if (gap > limit + 0.05) vessel_->undockGrace = -1;  // ya se ha apartado
                continue;
            }
            if (gap >= limit) continue;
            const Vec3d normal = gap > 1e-9 ? away * (1.0 / gap) : heading * -1.0;
            vessel_->ship.position += normal * (limit - gap);
            Impact(s.name, normal, info->velocity, false);
            return;
        }
    }
    for (const Solid& s : solids) {
        const double limit = s.radius + rules_.ship.radius;
        const double along = std::clamp(Dot(s.center - from, travel) / squared, 0.0, 1.0);
        const Vec3d closest = from + travel * along;
        if (Length(closest - s.center) >= limit) continue;
        // El primer punto del tramo que toca la esfera.
        const Vec3d start = from - s.center;
        const double b = Dot(start, travel), c = Dot(start, start) - limit * limit;
        const double inside = b * b - squared * c;
        const double t = c <= 0.0 ? 0.0 : std::clamp((-b - std::sqrt(std::max(inside, 0.0))) / squared, 0.0, 1.0);
        Vec3d away = from + travel * t - s.center;
        if (Length(away) < 1e-9) away = Heading(vessel_->ship) * -1.0;
        const Vec3d normal = Normalize(away);
        vessel_->ship.position = s.center + normal * limit;
        const Vec3d world = vessel_->ship.velocity + vessel_->ship.warpHeading * vessel_->ship.warpSpeed + vessel_->frame.velocity;
        const double closing = -Dot(world - s.velocity, normal);
        if (s.anomaly >= 0 && closing > 0.0) {
            if (Anomaly* a = FindAnomaly(s.anomaly); a && a->maxHull > 0.0) {
                const double share = closing / rules_.ship.shatterSpeed;
                DamageAnomaly(*a, DamageType::Kinetic, rules_.ship.hull * share * share, 0);
            }
        }
        if (s.vessel >= 0 && closing > 0.0) {
            // La otra recibe el mismo golpe, por el lado contrario.
            const Vec3d mine = world;
            const std::string name = "la nave " + std::to_string(vessel_->id);
            const Using other(*this, s.vessel);
            Impact(name, normal * -1.0, mine, false);
        }
        Impact(s.name, normal, s.velocity, false);
        world_.anomalies.erase(std::remove_if(world_.anomalies.begin(), world_.anomalies.end(), [](const Anomaly& a) { return a.maxHull > 0.0 && a.hull <= 0.0; }), world_.anomalies.end());
        return;
    }
}

// Las rocas sueltas del anillo (BeltField): las mismas que se dibujan, en orbitas circulares.
void GameSession::CheckFieldRocks(const Vec3d& from) {
    const World& w = engine_.GetWorld();
    const Vec3d travel = vessel_->ship.position - from;
    const double reach = Length(travel) + 40.0;
    const bool plasma = vessel_->ship.defense.field == ShieldKind::Plasma && FieldActive();
    std::vector<BeltFieldRock> rocks;
    for (const AsteroidBelt& belt : w.belts) {
        if (!belt.used || belt.parent == kNoNode || belt.mu <= 0.0 || belt.families.empty() || belt.families.front().fixed) continue;
        rocks.clear();
        BeltField(belt, w.scene.World(belt.parent), w.scene.WorldPosition(belt.parent), vessel_->ship.position, reach, bubble_.simTime, BeltRockTier(belt), rocks);
        const double squared = std::max(Dot(travel, travel), 1e-9);
        for (const BeltFieldRock& rock : rocks) {
            if (plasma && static_cast<double>(rock.radius) < rules_.defense.plasmaRock) continue;  // se vaporiza sin notarlo
            const double along = std::clamp(Dot(rock.position - from, travel) / squared, 0.0, 1.0);
            const Vec3d away = from + travel * along - rock.position;
            const double limit = static_cast<double>(rock.radius) + kSoftTouch * 0.6;
            if (Dot(away, away) >= limit * limit) continue;
            const Vec3d normal = Length(away) > 1e-6 ? Normalize(away) : Normalize(from - rock.position);
            vessel_->ship.position = rock.position + normal * limit;
            Impact("un asteroide", normal, rock.velocity, false);
            return;
        }
    }
}

// Las rocas del cumulo en el que esta la nave. Se generan solo aqui, y salen de su semilla.
void GameSession::CheckRocks(const Vec3d& from) {
    if (vessel_->frame.belt < 0) return;
    const World& w = engine_.GetWorld();
    const AsteroidFamily& family = w.belts[static_cast<std::size_t>(vessel_->frame.belt)].families[static_cast<std::size_t>(vessel_->frame.family)];
    if (bubble_.rocks.size() > 32 && bubble_.rocks.find(family.seed) == bubble_.rocks.end()) bubble_.rocks.clear();
    std::vector<AsteroidRock>& rocks = bubble_.rocks[family.seed];
    if (rocks.empty()) rocks = GenerateRocks(family);
    // Una mina ya ha despejado lo que tenia alrededor: ahi se maniobra sin rocas.
    for (const Station& s : world_.stations.All()) {
        if (s.site.system != vessel_->nearestSystem || !StationVisible(s)) continue;
        const Vec3d at = StationPosition(s) - vessel_->frame.position;
        if (Length(at) > static_cast<double>(family.radius) + kStationClearing) continue;
        for (std::size_t i = 0; i < rocks.size(); ++i)
            if (Length(Vec3d(rocks[i].offset) - at) < kStationClearing + static_cast<double>(rocks[i].radius)) engine_.GetWorld().deadRocks[family.seed].insert(static_cast<int>(i));
    }
    const auto dead = w.deadRocks.find(family.seed);
    const Vec3d travel = vessel_->ship.position - from;
    const double squared = std::max(Dot(travel, travel), 1e-9);
    for (std::size_t i = 0; i < rocks.size(); ++i) {
        const AsteroidRock& rock = rocks[i];
        if (dead != w.deadRocks.end() && dead->second.count(static_cast<int>(i)) != 0) continue;
        const Vec3d center = vessel_->frame.position + Vec3d(rock.offset);
        const double along = std::clamp(Dot(center - from, travel) / squared, 0.0, 1.0);
        const Vec3d closest = from + travel * along;
        const double limit = static_cast<double>(rock.radius) + kSoftTouch * 0.6;
        const Vec3d away = closest - center;
        if (Dot(away, away) >= limit * limit) continue;
        // El plasma vaporiza las rocas pequenas al instante: la nave sigue sin notarlas.
        if (static_cast<double>(rock.radius) < rules_.defense.plasmaRock && vessel_->ship.defense.field == ShieldKind::Plasma && FieldActive()) {
            engine_.GetWorld().deadRocks[family.seed].insert(static_cast<int>(i));
            DefenseState& d = vessel_->ship.defense;
            const std::size_t k = static_cast<std::size_t>(ShieldKind::Plasma);
            d.charge[k] = std::max(d.charge[k] - static_cast<double>(rock.radius) * 100.0, 0.0);
            d.heat = std::min(d.heat + static_cast<double>(rock.radius) * 10.0, rules_.thermal.maximum);
            Say("Asteroide vaporizado por el plasma.");
            continue;
        }
        const Vec3d normal = Length(away) > 1e-6 ? Normalize(away) : Normalize(from - center);
        vessel_->ship.position = center + normal * limit;
        Impact("un asteroide", normal, vessel_->frame.velocity, false);
        return;
    }
}

void GameSession::UpdateAnomalies(double seconds) {
    const World& w = engine_.GetWorld();
    const GameMode& mode = ModeInfo(world_.mode);
    // La alerta baja con el tiempo; al acabar una rendicion, la flota vuelve reforzada.
    DecayAlertUntil(world_.species, bubble_.simTime, bubble_.simTime - seconds, rules_.alert);
    if (EndSurrender(world_.species, world_.anomalies, bubble_.simTime, rules_.alert)) Say("La flota alienígena vuelve al combate, un 50 % más fuerte.", Audience::Everyone);
    const bool surrender = Surrendered(world_.species, bubble_.simTime);
    for (Anomaly& a : world_.anomalies) {
        const Vec3d sector = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(a.system)]);
        // Cada anomalia atiende a la nave en vuelo que tiene mas cerca: a ella persigue, a ella dispara
        // y a su tripulacion van los avisos.
        int nearest = -1;
        double distance = 1.0e300;
        for (std::size_t i = 0; i < fleet_.size(); ++i) {
            if (fleet_[i].phase != GamePhase::Flying) continue;
            const double d = Length(fleet_[i].ship.position - sector - a.offset);
            if (d < distance) {
                distance = d;
                nearest = static_cast<int>(i);
            }
        }
        if (nearest < 0 || distance > rules_.anomalies.activeRange) continue;
        const Using target(*this, nearest);
        const Vec3d ship = vessel_->ship.position - sector;
        const double close = Visibility() * 1.5;
        // El plasma hace brillar la nave: los hostiles la ven desde mucho mas lejos.
        const double sight = vessel_->ship.defense.field == ShieldKind::Plasma && FieldActive() ? rules_.defense.plasmaSight : 1.0;
        // El planeta coraza mide como Jupiter: lo que cuenta es la distancia a su superficie.
        const bool planet = IsArmorPlanet(a);
        const double gap = planet ? std::max(distance - a.radius, 0.0) : distance;
        // Apagada (sin espina o sin corazon), sigue a la deriva con la velocidad que llevaba.
        if (a.disabled) a.offset += a.velocity * seconds;
        else if (surrender && a.kind == AnomalyKind::AlienShip) {
            // Rendicion temporal: la flota se detiene.
            a.pursuing = false;
            a.velocity = a.velocity * std::exp(-seconds / 4.0);
            a.offset += a.velocity * seconds;
        } else StepAnomaly(a, ship, rules_.anomalies, seconds, bubble_.rng, mode.hostilesAttack, sight);
        a.cooldown = std::max(a.cooldown - seconds, 0.0);
        if (mode.hostilesAttack && !surrender && a.armed && !a.disabled && a.stance == Stance::Hostile && a.hull > 0.0) {
            const WeaponRules spec = Scaled(rules_.combat, a.weapon, a.weaponTier);
            // El planeta coraza dispara a todo lo que entra en su rango de percepcion.
            const double reach = planet ? rules_.anomalies.armorPlanetSight : spec.range;
            if (gap < reach && a.cooldown <= 0.0) {
                if (!a.discovered) Say("Bajo ataque: " + a.name + ".");
                FireAlien(a, sector + a.offset);
            }
        }
        if (gap < rules_.ship.identifyRange || (planet && gap < rules_.anomalies.armorPlanetSight)) {
            if (planet && !a.discovered && a.hull > 0.0) Say("¡Un planeta coraza! Estás dentro de su rango de percepción: huye cuanto antes.");
            a.discovered = true;
        }
        if (gap < close) a.revealed = true;  // a esta distancia se ve a simple vista
    }
}

Anomaly* GameSession::FindAnomaly(int id) {
    for (Anomaly& a : world_.anomalies)
        if (a.id == id) return &a;
    return nullptr;
}

const Anomaly* GameSession::FindAnomaly(int id) const {
    for (const Anomaly& a : world_.anomalies)
        if (a.id == id) return &a;
    return nullptr;
}

// ---- Combate -------------------------------------------------------------------------------

Vec3d GameSession::ShipVelocity() const { return VelocityOf(*vessel_); }

// La de cualquier nave de la flota. Si sigue a otra nave no se pregunta por ella: dos que se siguieran
// la una a la otra no acabarian nunca.
Vec3d GameSession::VelocityOf(const Vessel& v) const {
    if (v.orbiting && v.orbiting->kind != Kind::Ship) {
        if (const std::optional<TargetInfo> info = Resolve(*v.orbiting)) return info->velocity + v.ship.moveDirection * v.ship.speed;
    }
    return v.ship.velocity + v.frame.velocity + v.ship.warpHeading * v.ship.warpSpeed;
}

int GameSession::IndexOf(const Vessel& v) const {
    for (std::size_t i = 0; i < fleet_.size(); ++i)
        if (&fleet_[i] == &v) return static_cast<int>(i);
    return 0;
}

int GameSession::IndexOfId(int id) const {
    for (std::size_t i = 0; i < fleet_.size(); ++i)
        if (fleet_[i].id == id) return static_cast<int>(i);
    return -1;
}

bool GameSession::SetFocus(int index) {
    if (index < 0 || index >= static_cast<int>(fleet_.size())) return false;
    focus_ = index;
    Use(index);
    return true;
}

// Otra nave en la flota, a km de la del foco, a su costado y parada, con un jugador nuevo que sabe lo
// mismo que el del foco. Devuelve su id.
int GameSession::AddVessel(double km) {
    const Vessel& origin = fleet_[static_cast<std::size_t>(focus_)];
    const PlayerState& guide = crews_[static_cast<std::size_t>(focus_)];
    const Vec3d side = origin.ship.orientation.ToMatrix().TransformDirection(Vec3d{1, 0, 0});
    fleet_.emplace_back();
    crews_.emplace_back();
    Vessel& v = fleet_.back();
    v.id = nextVessel_++;
    v.ship = MakeShip(rules_);
    v.ship.position = origin.ship.position + side * std::max(km, rules_.ship.radius * 4.0);
    v.ship.orientation = v.ship.aim = origin.ship.orientation;
    v.ship.moveDirection = v.ship.warpHeading = Heading(origin.ship);
    v.nearestSystem = origin.nearestSystem;
    v.timeFocus = origin.timeFocus;
    PlayerState& p = crews_.back();
    p.id = "local-" + std::to_string(v.id);
    p.name = "Piloto " + std::to_string(v.id);
    p.known = guide.known;
    p.visited = guide.visited;
    p.scanned = guide.scanned;
    p.cursorUsed = p.analysisUsed = true;
    // Fase 3a: mientras haya varias naves, ninguna sale de este sistema (ver LeashToFleet).
    fleetSystem_ = origin.nearestSystem;
    Use(focus_);  // emplace_back no mueve las demas, pero la nave en curso se vuelve a fijar por si acaso
    return v.id;
}

// Limite provisional de la fase 3a: el motor solo carga un sistema, el de la nave del foco, asi que
// con varias naves ninguna sale del sistema donde esta la flota. Se quita con las burbujas (fase 4).
void GameSession::LeashToFleet() {
    if (fleet_.size() < 2 || fleetSystem_ < 0) return;
    const World& w = engine_.GetWorld();
    const std::size_t s = static_cast<std::size_t>(fleetSystem_);
    const Vec3d center = w.scene.WorldPosition(w.sectors[s]);
    const double limit = w.extents[s] * 1.5;
    const Vec3d out = vessel_->ship.position - center;
    const double distance = Length(out);
    if (distance <= limit) {
        if (distance < limit * 0.95) vessel_->leashWarned = false;
        return;
    }
    const Vec3d normal = out * (1.0 / distance);
    vessel_->ship.position = center + normal * limit;
    vessel_->ship.warpSpeed = 0.0;
    vessel_->ship.warpCharge = 0.0;
    vessel_->ship.throttle = 0.0;
    const double leaving = Dot(vessel_->ship.velocity, normal);
    if (leaving > 0.0) vessel_->ship.velocity += normal * -leaving;
    if (vessel_->autopilot) CancelAutopilot();
    if (!vessel_->leashWarned) Say("Con varias naves en la partida, ninguna puede salir todavía de este sistema.");
    vessel_->leashWarned = true;
}

// Los astros del sistema que tiran de un proyectil. Los asteroides y cometas apenas atraen.
std::vector<GravitySource> GameSession::GravitySources() const {
    std::vector<GravitySource> out;
    const World& w = engine_.GetWorld();
    const SystemState& state = w.state[static_cast<std::size_t>(vessel_->nearestSystem)];
    if (!state.loaded) return out;
    std::vector<int> seen;
    const auto add = [&](int index) {
        if (index < 0 || std::find(seen.begin(), seen.end(), index) != seen.end()) return;
        seen.push_back(index);
        const Body& b = w.bodies[static_cast<std::size_t>(index)];
        if (!Alive(b) || b.kind == PlanetClass::Belt || b.kind == PlanetClass::Asteroid || b.kind == PlanetClass::Comet) return;
        const double mass = ObjectMass(b);
        if (mass > 0.0) out.push_back({w.scene.WorldPosition(b.node), kGravity * mass, b.radius});
    };
    for (const int index : state.bodies) add(index);
    add(state.starBody);
    for (const int index : state.starBodies) add(index);
    return out;
}

int GameSession::WeaponTier(WeaponKind kind) const {
    int tier = 1;
    for (const WeaponMount& m : vessel_->ship.weapons)
        if (m.kind == kind) tier = std::max(tier, m.tier);
    return tier;
}

double GameSession::WeaponRange(WeaponKind kind) const { return Scaled(rules_.combat, kind, WeaponTier(kind)).range; }

double GameSession::WeaponCooldown(WeaponKind kind) const {
    double soonest = 1e300;
    for (const WeaponMount& m : vessel_->ship.weapons)
        if (m.kind == kind) soonest = std::min(soonest, m.cooldown);
    return soonest;
}

void GameSession::TrackTarget(const TargetRef& target) {
    if (!Solution(target, WeaponKind::Spinal)) {
        Say("Sin solución de tiro para alinear.");
        return;
    }
    if (vessel_->autopilot) CancelAutopilot();
    vessel_->tracking = target;
}

bool GameSession::WeaponReady(WeaponKind kind) const {
    for (const WeaponMount& m : vessel_->ship.weapons)
        if (m.kind == kind && m.cooldown <= 0.0) return true;
    return false;
}

std::optional<FiringSolution> GameSession::Solution(const TargetRef& target, WeaponKind kind) const {
    if (target.kind != Kind::Anomaly) return std::nullopt;
    const Anomaly* a = FindAnomaly(target.index);
    if (!a || a->maxHull <= 0.0 || a->hull <= 0.0) return std::nullopt;
    const WeaponRules spec = Scaled(rules_.combat, kind, WeaponTier(kind));
    const FiringSolution s = Solve(vessel_->ship.position, ShipVelocity(), AnomalyPosition(*a), a->velocity, spec.speed, kind == WeaponKind::Spinal, GravitySources(), vessel_->ship.computerTier, rules_.combat);
    if (!s.valid) return std::nullopt;
    return s;
}

bool GameSession::AimAtSolution(const TargetRef& target, WeaponKind kind) {
    const std::optional<FiringSolution> s = Solution(target, kind);
    if (!s) return false;
    AimAt(vessel_->ship, s->direction);
    LimitAim(vessel_->ship, rules_.ship.aimLead);
    return true;
}

bool GameSession::Fire(WeaponKind kind, const TargetRef& target) {
    if (vessel_->phase != GamePhase::Flying || pilot_->onFoot) return false;
    const std::string name = Name(kind);
    const Anomaly* a = target.kind == Kind::Anomaly ? FindAnomaly(target.index) : nullptr;
    if (!a || a->maxHull <= 0.0 || a->hull <= 0.0) {
        Say("Sin blanco: fija una nave o un bastión.");
        return false;
    }
    if (vessel_->ship.warpSpeed > rules_.drive.impulseSpeed) {
        Say("No se puede disparar con la burbuja formada.");
        return false;
    }
    if (Overheated()) {
        Say(name + ": sobrecalentada, espera a que baje el calor.");
        return false;
    }
    WeaponMount* mount = nullptr;
    bool mounted = false;
    for (WeaponMount& m : vessel_->ship.weapons) {
        if (m.kind != kind) continue;
        mounted = true;
        if (m.cooldown <= 0.0) {
            mount = &m;
            break;
        }
    }
    if (!mounted) {
        Say(name + ": no está montada.");
        return false;
    }
    if (!mount) {
        Say(name + ": recargando.");
        return false;
    }
    const WeaponRules spec = Scaled(rules_.combat, kind, mount->tier);
    if (Length(AnomalyPosition(*a) - vessel_->ship.position) > spec.range) {
        Say(name + ": fuera de alcance (" + Fixed(spec.range / 1000.0, 0) + " mil km).");
        return false;
    }
    if (vessel_->ship.energy < spec.energy || vessel_->ship.antimatter < spec.antimatter || vessel_->ship.cargo.Get(Resource::Metal) < spec.metal) {
        Say(name + ": sin " + (vessel_->ship.energy < spec.energy ? "energía" : vessel_->ship.antimatter < spec.antimatter ? "antimateria" : "metal") + " para disparar.");
        return false;
    }
    const std::optional<FiringSolution> s = Solution(target, kind);
    if (!s) {
        Say(name + ": sin solución de tiro.");
        return false;
    }

    Vec3d direction = s->direction;
    if (kind == WeaponKind::Spinal) direction = Heading(vessel_->ship);  // fijo al casco
    else if (kind == WeaponKind::Turret) direction = Scatter(direction, AimError(rules_.combat, vessel_->ship.computerTier) * AimSpread(vessel_->ship.zones), bubble_.rng);  // el puente apunta

    vessel_->ship.energy -= spec.energy;
    vessel_->ship.antimatter -= spec.antimatter;
    vessel_->ship.cargo.Add(Resource::Metal, -spec.metal);
    mount->cooldown = spec.cooldown;
    vessel_->ship.defense.heat = std::min(vessel_->ship.defense.heat + rules_.thermal.weaponHeat[static_cast<int>(kind)], rules_.thermal.maximum);
    const Vec3d shooter = ShipVelocity();
    for (int i = 0; i < spec.count; ++i) {
        Projectile p;
        p.kind = kind;
        p.type = spec.type;
        p.team = 0;
        p.owner = 0;
        p.ship = vessel_->id;
        p.target = a->id;
        p.damage = spec.damage;
        p.proximity = spec.proximity;
        p.life = spec.life;
        p.guided = spec.turn > 0.0;
        p.beam = kind == WeaponKind::Spinal;
        p.cruise = spec.speed;
        p.turn = spec.turn;
        const Vec3d dir = spec.count > 1 ? Scatter(direction, 0.03, bubble_.rng) : direction;
        // Una torreta de modulo dispara desde su ranura; las de serie, desde el borde del casco.
        const ShipDesign& design = CurrentShipDesign();
        if (mount->slot >= 0 && mount->slot < static_cast<int>(design.slots.size()))
            p.position = vessel_->ship.position + vessel_->ship.orientation.ToMatrix().TransformDirection(design.slots[static_cast<std::size_t>(mount->slot)].position * 0.001) + dir * 0.03;
        else p.position = vessel_->ship.position + dir * (rules_.ship.radius + 0.1);
        p.origin = p.position;
        p.velocity = p.beam ? dir * spec.speed : shooter + dir * (p.guided ? spec.speed * 0.3 : spec.speed);
        bubble_.projectiles.push_back(p);
    }
    return true;
}

int GameSession::FireAll(const TargetRef& target) {
    int fired = 0;
    for (int k = 0; k < static_cast<int>(WeaponKind::Count); ++k) {
        const WeaponKind kind = static_cast<WeaponKind>(k);
        if (!WeaponReady(kind)) continue;
        const std::optional<FiringSolution> s = Solution(target, kind);
        if (!s || s->flightTime * Scaled(rules_.combat, kind, 1).speed > WeaponRange(kind) * 1.2) continue;
        const Anomaly* a = FindAnomaly(target.index);
        if (!a || Length(AnomalyPosition(*a) - vessel_->ship.position) > WeaponRange(kind)) continue;
        if (kind == WeaponKind::Spinal && std::acos(std::clamp(Dot(Heading(vessel_->ship), s->direction), -1.0, 1.0)) * Length(AnomalyPosition(*a) - vessel_->ship.position) > Scaled(rules_.combat, kind, 1).proximity) continue;
        if (Fire(kind, target)) ++fired;
    }
    // Las armas humanas de los modulos: las que esten listas y tengan blanco a tiro.
    for (std::size_t i = 0; i < vessel_->ship.loadout.modules.size(); ++i)
        if (IsHumanWeapon(vessel_->ship.loadout.modules[i]) && FireModule(static_cast<int>(i), target, nullptr, true)) ++fired;
    return fired;
}

void GameSession::FireAlien(Anomaly& a, const Vec3d& position) {
    const WeaponRules spec = Scaled(rules_.combat, a.weapon, a.weaponTier);
    const FiringSolution s = Solve(position, a.velocity, vessel_->ship.position, ShipVelocity(), spec.speed, a.weapon == WeaponKind::Spinal, GravitySources(), a.computerTier, rules_.combat);
    a.cooldown = spec.cooldown * (1.0 + rules_.combat.jitter * Uniform(bubble_.rng, -1.0, 1.0));
    if (!s.valid) return;
    const bool guided = spec.turn > 0.0;
    Projectile p;
    p.kind = a.weapon;
    p.type = spec.type;
    p.team = TeamOf(a);
    p.owner = a.id;
    p.target = 0;  // una nave de jugador
    p.ship = vessel_->id;  // esta
    p.damage = spec.damage * a.boost;  // la revancha tras una rendicion
    p.proximity = spec.proximity;
    p.life = spec.life;
    p.guided = guided;
    p.beam = a.weapon == WeaponKind::Spinal;
    p.cruise = spec.speed;
    p.turn = spec.turn;
    // El escudo electromagnetico confunde sus sensores: fallan mas, incluso el haz.
    const bool jammed = vessel_->ship.defense.field == ShieldKind::EM && FieldActive();
    const double error = AimError(rules_.combat, a.computerTier) * (jammed ? rules_.defense.emJamming : 1.0);
    const Vec3d dir = guided || (p.beam && !jammed) ? s.direction : Scatter(s.direction, error, bubble_.rng);
    p.position = position + dir * (a.radius + 0.5);
    p.origin = p.position;
    p.velocity = p.beam ? dir * spec.speed : a.velocity + dir * (guided ? spec.speed * 0.3 : spec.speed);
    bubble_.projectiles.push_back(p);
}

// Todo dano de armas a la nave pasa por aqui: primero el campo, luego los conos y al final el casco.
void GameSession::Hurt(DamageType type, double amount, const std::string& source, const Vec3d& toward, int attacker, const ZoneImpact* impact) {
    const GameMode& mode = ModeInfo(world_.mode);
    if (mode.damage <= 0.0 || vessel_->phase != GamePhase::Flying) return;
    DefenseState& d = vessel_->ship.defense;
    const bool frontal = Dot(Heading(vessel_->ship), toward) > 0.0;
    const HitResult hit = AbsorbHit(d, rules_.defense, type, amount, frontal, FieldActive(), vessel_->ship.cargo.Get(Resource::Silicate));
    vessel_->ship.cargo.Add(Resource::Silicate, -hit.silicate);
    if (d.field != ShieldKind::Dust && d.field != ShieldKind::Love) d.heat = std::min(d.heat + hit.field * rules_.thermal.absorbHeat, rules_.thermal.maximum);  // la arena se traga ese calor
    if (d.field == ShieldKind::Love && hit.field > 0.0) {
        vessel_->ship.hull = std::min(vessel_->ship.hull + hit.field * rules_.defense.loveHeal, rules_.ship.hull);
        vessel_->ship.energy = std::min(vessel_->ship.energy + hit.field * (1.0 - rules_.defense.loveHeal), rules_.ship.energy);
        if (hit.reflected > 0.0) {
            if (Anomaly* a = FindAnomaly(attacker)) DamageAnomaly(*a, type, hit.reflected, 0);
        }
    }
    const double taken = hit.hull * mode.damage;
    vessel_->ship.hull = std::max(vessel_->ship.hull - taken, 0.0);
    if (impact) WoundAt(taken, *impact);
    else WoundHull(taken, toward);
    // Una rafaga ocupa una sola linea del registro.
    const std::string lead = std::string("Impactos de ") + source + ": ";
    if (pilot_->log.empty() || pilot_->log.back().rfind(lead, 0) != 0) {
        vessel_->hitStreak = 0;
        vessel_->hitDamage = 0.0;
        vessel_->hitShield = 0.0;
    } else {
        pilot_->log.pop_back();
    }
    ++vessel_->hitStreak;
    vessel_->hitDamage += taken;
    vessel_->hitShield += hit.field + hit.plates;
    std::string line = lead + std::to_string(vessel_->hitStreak) + " (" + Name(type) + "), -" + Fixed(vessel_->hitDamage, 0) + " de casco";
    if (vessel_->hitShield >= 0.5) line += ", " + Fixed(vessel_->hitShield, 0) + " parados por los escudos";
    Say(line + ".");
    if (vessel_->ship.hull <= 0.0) vessel_->lossReason = "Nave destruida por " + source + ".";
}

// El golpe cae en la zona de ese lado. No cambia el casco total: eso ya lo ha hecho quien llama.
// Un golpe directo en una zona roja hace mas dano: es donde mas duele.
void GameSession::WoundHull(double amount, const Vec3d& worldFrom, bool blunt) {
    if (amount <= 0.0 || rules_.ship.hull <= 0.0) return;
    vessel_->directedLoss += amount;
    const Vec3d local = Normalize(vessel_->ship.orientation.Conjugate().ToMatrix().TransformDirection(worldFrom) + Vec3d{0, 0, 1e-9});
    const int zone = ZoneHitFrom(local, std::uniform_real_distribution<double>(0.0, 1.0)(bubble_.rng));
    RecordHit(zone, local, amount / rules_.ship.hull, blunt);
    const double critical = ZoneInfo(zone).red != RedZone::None ? kCriticalHit : 1.0;
    WoundZone(zone, amount * critical / (rules_.ship.hull * kZoneToughness));
}

// El golpe cae en la zona que encuentra su trayectoria, y su crater en ese punto.
void GameSession::WoundAt(double amount, const ZoneImpact& impact) {
    if (amount <= 0.0 || rules_.ship.hull <= 0.0) return;
    vessel_->directedLoss += amount;
    RecordHit(impact.zone, impact.from, amount / rules_.ship.hull, false, &impact.point);
    const double critical = ZoneInfo(impact.zone).red != RedZone::None ? kCriticalHit : 1.0;
    const double fraction = amount * critical / (rules_.ship.hull * kZoneToughness);
    WoundPiece(impact.zone, impact.point, fraction / ZoneHardness(vessel_->ship, impact.zone));
    WoundZone(impact.zone, fraction);
}

int GameSession::ShootHull(const Vec3d& start, const Vec3d& end, double hullAmount) {
    const ZoneImpact impact = ProjectileImpact(start, end);
    if (hullAmount > 0.0) {
        vessel_->ship.hull = std::max(vessel_->ship.hull - hullAmount, 0.0);
        WoundAt(hullAmount, impact);
    }
    return impact.zone;
}

void GameSession::RecordHit(int zone, const Vec3d& localFrom, double intensity, bool blunt, const Vec3d* point) {
    if (intensity <= 0.0) return;
    HullHit h;
    if (point) {
        h.located = true;
        h.point = *point;
    }
    h.id = vessel_->nextHit++;
    h.zone = zone;
    h.from = localFrom;
    h.intensity = intensity;
    h.blunt = blunt;
    h.time = bubble_.simTime;
    h.absolute = vessel_->ship.position + engine_.GetWorld().origin;
    h.velocity = vessel_->ship.velocity + vessel_->ship.warpHeading * vessel_->ship.warpSpeed + vessel_->frame.velocity;
    h.orientation = vessel_->ship.orientation;
    vessel_->hullHits.push_back(h);
    // Se guardan los ultimos: un casco con mas agujeros que estos ya no se distingue.
    constexpr std::size_t kMaxHits = 48;
    if (vessel_->hullHits.size() > kMaxHits) vessel_->hullHits.erase(vessel_->hullHits.begin());
}

void GameSession::SpreadWound(double amount) {
    if (amount <= 0.0 || rules_.ship.hull <= 0.0) return;
    for (int z = 0; z < kHullZoneCount; ++z) WoundZone(z, amount / rules_.ship.hull);
}

// Una zona periferica que llega a 0 se suelta: sale despedida hacia fuera, girando, y se pierde.
// Lo que sobra del golpe, o lo que entra por el hueco de una zona ya perdida, sigue hacia dentro.
// Una zona roja a 0 apaga la nave o la hace estallar.
void GameSession::WoundZone(int zone, double fraction) {
    if (zone < 0 || zone >= kHullZoneCount || fraction <= 0.0) return;
    fraction /= ZoneHardness(vessel_->ship, zone);  // placas y refuerzos montados en ella
    const HullZoneInfo& info = ZoneInfo(zone);
    double& integrity = vessel_->ship.zones[static_cast<std::size_t>(zone)];
    if (ZoneLost(vessel_->ship.zones, zone)) {
        WoundZone(info.inner, fraction * kInnerShare);
        return;
    }
    const double excess = fraction - integrity;
    const bool wasStanding = integrity > 0.0;
    integrity = std::max(integrity - fraction, 0.0);
    vessel_->zoneHeat[static_cast<std::size_t>(zone)] = std::min(vessel_->zoneHeat[static_cast<std::size_t>(zone)] + 0.3 + fraction * 4.0, 1.0);
    if (integrity > 0.0 || vessel_->ship.hull <= 0.0) return;

    if (info.red != RedZone::None) {
        if (wasStanding) FailRedZone(zone);
        return;
    }
    if (!info.losable || !wasStanding) return;

    const auto toWorld = vessel_->ship.orientation.ToMatrix();
    const Vec3d out = Normalize(toWorld.TransformDirection(info.center + Vec3d{0, 0, 1e-3}));
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    const Vec3d pivot = CustomZoneLayout() ? CustomZoneLayout()->centers[static_cast<std::size_t>(zone)] : info.center;
    LostPiece piece;
    piece.zone = zone;
    piece.pivot = pivot;
    piece.absolute = vessel_->ship.position + toWorld.TransformDirection(pivot * 0.001) + engine_.GetWorld().origin;
    // Se lleva la velocidad de la nave y un empujon de unas decenas de m/s hacia fuera.
    piece.velocity = vessel_->ship.velocity + vessel_->ship.warpHeading * vessel_->ship.warpSpeed + vessel_->frame.velocity + (out + Vec3d{unit(bubble_.fx), unit(bubble_.fx), unit(bubble_.fx)} * 0.3) * 0.03;
    piece.orientation = vessel_->ship.orientation;
    piece.spinAxis = Normalize(Vec3d{unit(bubble_.fx), unit(bubble_.fx), unit(bubble_.fx)} + Vec3d{0, 0, 1e-3});
    piece.spin = 0.15 + 0.35 * std::fabs(unit(bubble_.fx));
    bubble_.lostPieces.push_back(piece);
    Say(std::string("Se ha desprendido ") + (info.masculine ? "el " : "la ") + info.name + ": solo se repone atracando en una estación propia.");

    // Con una bodega se va lo que llevaba: su parte de cada material.
    if (zone >= static_cast<int>(HullZone::Hold1) && zone <= static_cast<int>(HullZone::Hold6)) {
        const double share = 1.0 / static_cast<double>(HoldsLeft(vessel_->ship.zones) + 1);
        for (const Resource r : {Resource::Metal, Resource::Silicate, Resource::Ice, Resource::Gas, Resource::StellarMatter}) vessel_->ship.cargo.Add(r, -vessel_->ship.cargo.Get(r) * share);
    }
    if (zone == static_cast<int>(HullZone::WarpRing)) Say("Sin anillo no hay curvatura: solo queda el impulso.");
    LoseModules(zone, -1);
    if (excess > 0.0) WoundZone(info.inner, excess * kInnerShare);
}

void GameSession::FailRedZone(int zone) {
    if (vessel_->phase != GamePhase::Flying || vessel_->creative || ModeInfo(world_.mode).damage <= 0.0) return;
    vessel_->failedZone = zone;
    switch (static_cast<HullZone>(zone)) {
        case HullZone::Core:
            vessel_->lossReason = "El núcleo perdió la contención: la nave estalló desde dentro.";
            vessel_->warning.reset();
            LoseShip();
            break;
        case HullZone::Bridge: ShutDownShip("Puente destruido: la nave se quedó sin mando y se apagó."); break;
        default: ShutDownShip("Espina partida: la energía ya no llega a ningún sistema y la nave se apagó."); break;
    }
}

// Se apaga: sin motores ni luces, a la deriva y girando despacio. La pantalla final llega tras la
// cinematica (kShutdownSeconds).
void GameSession::ShutDownShip(const std::string& reason) {
    vessel_->lossReason = reason;
    vessel_->warning.reset();
    vessel_->phase = GamePhase::Adrift;
    vessel_->wreckAge = 0;
    vessel_->wreckPosition = vessel_->ship.position;
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    vessel_->driftVelocity = Heading(vessel_->ship) * 0.04 + Vec3d{unit(bubble_.rng), unit(bubble_.rng), unit(bubble_.rng)} * 0.01;
    vessel_->ship.angularRate = Normalize(Vec3d{unit(bubble_.rng), unit(bubble_.rng), unit(bubble_.rng)} + Vec3d{0, 1e-3, 0}) * 0.035;
    vessel_->autopilot.reset();
    vessel_->tracking.reset();
    vessel_->orbiting.reset();
    vessel_->dockPhase = DockPhase::None;
    vessel_->dockWanted = -1;
    pilot_->onFoot = false;
    vessel_->exploring = -1;
    CancelScan();
    vessel_->ship.speed = 0;
    vessel_->ship.velocity = {};
    vessel_->ship.warpSpeed = 0;
    vessel_->ship.warpCharge = 0;
    vessel_->ship.throttle = 0;
    vessel_->ship.energy = 0;
    Say(reason);
    if (ModeInfo(world_.mode).permadeath) saveLost_ = true;
}

// La pieza de esa zona mas cercana al impacto se lleva su parte. Una pieza pequena (una antena, un
// mastil) se rompe antes que la zona entera; a 0 se suelta sola.
void GameSession::WoundPiece(int zone, const Vec3d& point, double fraction) {
    const ShipDesign& design = CurrentShipDesign();
    if (design.pieces.empty() || vessel_->ship.pieces.size() != design.pieces.size() || fraction <= 0.0) return;
    int best = -1;
    double nearest = 1e300;
    for (std::size_t i = 0; i < design.pieces.size(); ++i) {
        const DesignPiece& p = design.pieces[i];
        if (p.zone != zone || PieceLost(vessel_->ship, static_cast<int>(i))) continue;
        const double gap = PieceDistance(p, point);
        if (gap < nearest) {
            nearest = gap;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return;
    // Lo que aguanta va con su tamano frente al de la zona.
    double zoneSize = 1.0;
    for (const DesignPiece& p : design.pieces)
        if (p.zone == zone) zoneSize = std::max(zoneSize, p.radius);
    const double share = std::clamp(design.pieces[static_cast<std::size_t>(best)].radius / zoneSize, 0.12, 1.0);
    double& integrity = vessel_->ship.pieces[static_cast<std::size_t>(best)];
    integrity = std::max(integrity - fraction * 0.6 / share, 0.0);
    if (integrity <= 0.0 && !ZoneLost(vessel_->ship.zones, zone) && vessel_->ship.hull > 0.0) DetachPiece(best);
}

void GameSession::DetachPiece(int index) {
    const ShipDesign& design = CurrentShipDesign();
    if (index < 0 || index >= static_cast<int>(design.pieces.size())) return;
    const DesignPiece& p = design.pieces[static_cast<std::size_t>(index)];
    if (index < static_cast<int>(vessel_->ship.pieces.size())) vessel_->ship.pieces[static_cast<std::size_t>(index)] = 0.0;
    const auto toWorld = vessel_->ship.orientation.ToMatrix();
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    LostPiece piece;
    piece.zone = p.zone;
    piece.piece = index;
    piece.pivot = p.center;
    piece.absolute = vessel_->ship.position + toWorld.TransformDirection(p.center * 0.001) + engine_.GetWorld().origin;
    const Vec3d out = Normalize(toWorld.TransformDirection(p.center) + Vec3d{0, 0, 1e-6});
    piece.velocity = vessel_->ship.velocity + vessel_->ship.warpHeading * vessel_->ship.warpSpeed + vessel_->frame.velocity + (out + Vec3d{unit(bubble_.fx), unit(bubble_.fx), unit(bubble_.fx)} * 0.4) * 0.025;
    piece.orientation = vessel_->ship.orientation;
    piece.spinAxis = Normalize(Vec3d{unit(bubble_.fx), unit(bubble_.fx), unit(bubble_.fx)} + Vec3d{0, 0, 1e-3});
    piece.spin = 0.2 + 0.6 * std::fabs(unit(bubble_.fx));
    bubble_.lostPieces.push_back(piece);
    Say("Arrancada: " + PrettyName(p.name) + ".");
    LoseModules(-1, index);
}

// Un modulo cuya zona se pierde, o cuya pieza de montaje se suelta, se va con ella.
void GameSession::LoseModules(int zone, int piece) {
    const ShipDesign& design = CurrentShipDesign();
    bool changed = false;
    vessel_->ship.loadout.starter = false;  // lo perdido no vuelve solo
    for (std::size_t i = 0; i < vessel_->ship.loadout.modules.size() && i < design.slots.size(); ++i) {
        ShipModuleKind& m = vessel_->ship.loadout.modules[i];
        if (m == ShipModuleKind::None) continue;
        const ModuleSlot& s = design.slots[i];
        if ((zone >= 0 && s.zone == zone) || (piece >= 0 && s.piece == piece)) {
            Say(std::string("Perdido: ") + ModuleSpec(m).name + " (" + PrettyName(s.name) + ").");
            m = ShipModuleKind::None;
            if (i < vessel_->ship.loadout.ammo.size()) vessel_->ship.loadout.ammo[i] = 0;  // los misiles se van con la bahia
            changed = true;
        }
    }
    if (changed) SyncModuleWeapons();
}

// Cada torreta montada como modulo es un arma mas, con su recarga, al nivel de la torreta de serie.
void GameSession::SyncModuleWeapons() {
    int tier = rules_.ship.weaponTier;
    for (const WeaponMount& m : vessel_->ship.weapons)
        if (m.slot < 0 && m.kind == WeaponKind::Turret) tier = m.tier;
    std::vector<WeaponMount> kept;
    for (const WeaponMount& m : vessel_->ship.weapons)
        if (m.slot < 0) kept.push_back(m);
    for (std::size_t i = 0; i < vessel_->ship.loadout.modules.size(); ++i) {
        if (vessel_->ship.loadout.modules[i] != ShipModuleKind::Turret || !SlotWorking(vessel_->ship, static_cast<int>(i))) continue;
        double cooldown = 0.0;
        for (const WeaponMount& m : vessel_->ship.weapons)
            if (m.slot == static_cast<int>(i)) cooldown = m.cooldown;
        kept.push_back({WeaponKind::Turret, tier, cooldown, static_cast<int>(i)});
    }
    vessel_->ship.weapons = std::move(kept);
}

void GameSession::RefitToDesign() {
    FitToDesign(vessel_->ship);
    SyncModuleWeapons();
}

bool GameSession::InstallModule(int slot, ShipModuleKind kind, std::string* why) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!CanRefit()) return fail("Los módulos se cambian atracado en una estación propia.");
    vessel_->ship.loadout.starter = false;  // desde aqui manda el jugador
    FitToDesign(vessel_->ship);
    const ShipDesign& design = CurrentShipDesign();
    if (slot < 0 || slot >= static_cast<int>(design.slots.size())) return fail("Esa ranura no existe.");
    const ModuleSlot& s = design.slots[static_cast<std::size_t>(slot)];
    if (kind != ShipModuleKind::None && ModuleSpec(kind).slot != s.kind) return fail(std::string(ModuleSpec(kind).name) + (s.kind == SlotKind::External ? " va por dentro, no en una ranura exterior." : " va por fuera, no en una bahía interior."));
    if (ZoneLost(vessel_->ship.zones, s.zone) || PieceLost(vessel_->ship, s.piece)) return fail("Esa parte del casco no está: hay que repararla antes.");
    ShipModuleKind& current = vessel_->ship.loadout.modules[static_cast<std::size_t>(slot)];
    if (current == kind) return fail("Ya está montado.");
    const ArmamentRules& arm = rules_.armament;
    const ShipModuleSpec& spec = ModuleSpec(kind);
    const bool free = vessel_->creative;  // en creativo no cuesta nada

    // Las bahias de drones de serie se cambian de hueco (MoveModule) pero no se sacan de la nave; si se ha
    // perdido alguna, volver a montarla es gratis hasta tener las de serie.
    int bays = 0;
    for (const ShipModuleKind m : vessel_->ship.loadout.modules) bays += m == ShipModuleKind::DroneBay ? 1 : 0;
    if (current == ShipModuleKind::DroneBay && kind != ShipModuleKind::DroneBay && bays <= vessel_->ship.loadout.starterBays)
        return fail("Las bahías de drones de serie no se sacan de la nave: cámbialas de hueco con «mover».");
    const bool starterBay = kind == ShipModuleKind::DroneBay && bays < vessel_->ship.loadout.starterBays;

    // Armas humanas: la clase de la nave las admite, el plano esta fabricado y hay elementos.
    const HumanWeaponSpec* weapon = HumanWeaponOf(arm, kind);
    if (weapon) {
        int spinal = 0;
        for (std::size_t j = 0; j < vessel_->ship.loadout.modules.size(); ++j) {
            const HumanWeaponSpec* other = j == static_cast<std::size_t>(slot) ? nullptr : HumanWeaponOf(arm, vessel_->ship.loadout.modules[j]);
            spinal += other && other->type == ArmType::RailSpinal ? 1 : 0;
        }
        std::string reason;
        if (!MountAllowed(arm, *weapon, rules_.ship.classId, rules_.ship.variant, spinal, &reason)) return fail(reason);
        if (!free && !HasBlueprint(vessel_->ship.loadout, BlueprintId(*weapon))) return fail(BlueprintName(BlueprintId(*weapon)) + ": se fabrica en la estación origami.");
        if (!free) {
            const std::string missing = MissingElements(vessel_->ship.elements, weapon->elements, 4);
            if (!missing.empty()) return fail("Faltan elementos: " + missing + ".");
        }
    }

    const double needMetal = starterBay ? 0.0 : spec.metal, needSilicate = starterBay ? 0.0 : spec.silicate;
    if (!free && kind != ShipModuleKind::None && (vessel_->ship.cargo.Get(Resource::Metal) < needMetal || vessel_->ship.cargo.Get(Resource::Silicate) < needSilicate))
        return fail(std::string("Faltan materiales: ") + spec.name + " pide " + MassText(needMetal) + " de metal y " + MassText(needSilicate) + " de silicato.");
    // Lo que se quita devuelve la mitad (tambien los elementos de un arma).
    if (!free && current != ShipModuleKind::None) {
        const ShipModuleSpec& old = ModuleSpec(current);
        vessel_->ship.cargo.Add(Resource::Metal, old.metal * arm.refund);
        vessel_->ship.cargo.Add(Resource::Silicate, old.silicate * arm.refund);
        if (const HumanWeaponSpec* oldWeapon = HumanWeaponOf(arm, current)) RefundElements(vessel_->ship.elements, oldWeapon->elements, 4, arm.refund);
    }
    if (!free) {
        vessel_->ship.cargo.Add(Resource::Metal, -needMetal);
        vessel_->ship.cargo.Add(Resource::Silicate, -needSilicate);
        if (weapon) PayElements(vessel_->ship.elements, weapon->elements, 4);
    }
    current = kind;
    vessel_->ship.loadout.ammo[static_cast<std::size_t>(slot)] = 0;  // los misiles de lo que estaba no pasan a lo nuevo
    vessel_->ship.loadout.cooldown[static_cast<std::size_t>(slot)] = 0.0;
    SyncModuleWeapons();
    Say(kind == ShipModuleKind::None ? "Desmontado: " + PrettyName(s.name) + "." : std::string("Montado: ") + spec.name + " en " + PrettyName(s.name) + ".");
    return true;
}

bool GameSession::MoveModule(int from, int to, std::string* why) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!CanRefit()) return fail("Los módulos se cambian atracado en una estación propia.");
    FitToDesign(vessel_->ship);
    const ShipDesign& design = CurrentShipDesign();
    const int count = static_cast<int>(design.slots.size());
    if (from < 0 || from >= count || to < 0 || to >= count) return fail("Esa ranura no existe.");
    if (from == to) return fail("Ya está ahí.");
    ShipModuleKind& source = vessel_->ship.loadout.modules[static_cast<std::size_t>(from)];
    ShipModuleKind& target = vessel_->ship.loadout.modules[static_cast<std::size_t>(to)];
    if (source != ShipModuleKind::DroneBay) return fail("Solo se mueven gratis las bahías de drones de serie; lo demás se desmonta y se vuelve a montar.");
    if (design.slots[static_cast<std::size_t>(to)].kind != SlotKind::External) return fail("Una bahía de drones va por fuera.");
    if (target != ShipModuleKind::None) return fail("Ese hueco está ocupado.");
    if (ZoneLost(vessel_->ship.zones, design.slots[static_cast<std::size_t>(to)].zone) || PieceLost(vessel_->ship, design.slots[static_cast<std::size_t>(to)].piece)) return fail("Esa parte del casco no está: hay que repararla antes.");
    if (DronesOut() > 0) return fail("Hay drones fuera: espera a que vuelvan.");
    vessel_->ship.loadout.starter = false;
    source = ShipModuleKind::None;
    target = ShipModuleKind::DroneBay;
    SyncDrones();  // los drones atracados pasan a la bahia nueva
    Say("Bahía de drones movida: " + PrettyName(design.slots[static_cast<std::size_t>(from)].name) + " a " + PrettyName(design.slots[static_cast<std::size_t>(to)].name) + ".");
    return true;
}

bool GameSession::FabricateBlueprint(int id, std::string* why) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (id < 0 || id >= kBlueprintCount) return fail("Ese plano no existe.");
    if (HasBlueprint(vessel_->ship.loadout, id)) return fail("Ya está fabricado.");
    const Station* yard = DockedStation();
    if (!vessel_->creative && !(yard && yard->kind == StationKind::Shipyard)) return fail("Los planos se fabrican atracada en la estación origami.");
    const double energy = BlueprintEnergy(rules_.armament, id), metal = BlueprintMetal(rules_.armament, id);
    if (!vessel_->creative && (vessel_->ship.energy < energy || vessel_->ship.cargo.Get(Resource::Metal) < metal))
        return fail(BlueprintName(id) + " pide " + Fixed(energy, 0) + " EJ y " + MassText(metal) + " de metal.");
    if (!vessel_->creative) {
        vessel_->ship.energy -= energy;
        vessel_->ship.cargo.Add(Resource::Metal, -metal);
    }
    GrantBlueprint(vessel_->ship.loadout, id);
    Say(BlueprintName(id) + " fabricado.");
    return true;
}

int GameSession::ManufactureMissiles(int slot, int count, std::string* why) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return 0;
    };
    if (!CanRefit()) return fail("Los misiles se fabrican atracada en una estación propia.");
    FitToDesign(vessel_->ship);
    if (slot < 0 || slot >= static_cast<int>(vessel_->ship.loadout.modules.size())) return fail("Esa ranura no existe.");
    const HumanWeaponSpec* w = HumanWeaponOf(rules_.armament, vessel_->ship.loadout.modules[static_cast<std::size_t>(slot)]);
    if (!w || w->capacity <= 0) return fail("Esa ranura no lleva una bahía de misiles.");
    if (!SlotWorking(vessel_->ship, slot)) return fail("Esa bahía está dañada.");
    int& ammo = vessel_->ship.loadout.ammo[static_cast<std::size_t>(slot)];
    const int room = MissileRoom(*w, ammo);
    if (room <= 0) return fail("La bahía ya está llena.");
    int n = count <= 0 ? room : std::min(count, room);
    // Se fabrican los que alcancen: el limite es lo que haya de energia, de metal o del elemento mas escaso.
    if (!vessel_->creative) {
        const MissileCost one = MissileBatch(*w, 1);
        while (n > 0) {
            const MissileCost c = MissileBatch(*w, n);
            if (vessel_->ship.energy >= c.energy && vessel_->ship.cargo.Get(Resource::Metal) >= c.metal && MissingElements(vessel_->ship.elements, w->missile, 3, n).empty()) break;
            --n;
        }
        if (n <= 0) {
            const std::string missing = MissingElements(vessel_->ship.elements, w->missile, 3, 1);
            if (!missing.empty()) return fail("Faltan elementos para un misil: " + missing + ".");
            return fail(std::string("Un misil pide ") + Fixed(one.energy, 1) + " EJ y " + MassText(one.metal) + " de metal.");
        }
        const MissileCost c = MissileBatch(*w, n);
        vessel_->ship.energy -= c.energy;
        vessel_->ship.cargo.Add(Resource::Metal, -c.metal);
        PayElements(vessel_->ship.elements, w->missile, 3, n);
    }
    ammo += n;
    Say(std::to_string(n) + (n == 1 ? " misil fabricado" : " misiles fabricados") + " para " + PrettyName(CurrentShipDesign().slots[static_cast<std::size_t>(slot)].name) + ".");
    return n;
}

bool GameSession::SetPointDefense(int points, std::string* why) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!CanRefit()) return fail("La defensa puntual se monta atracada en una estación propia.");
    const int capacity = PointDefenseCapacity();
    if (points < 0 || points > capacity) return fail("Esta nave tiene " + std::to_string(capacity) + " puntos de defensa.");
    const int delta = points - vessel_->ship.loadout.pointDefense;
    if (delta == 0) return fail("Ya están montados.");
    const double metal = delta * rules_.armament.pointMass;
    if (!vessel_->creative && delta > 0 && vessel_->ship.cargo.Get(Resource::Metal) < metal) return fail("Faltan materiales: " + MassText(metal) + " de metal.");
    if (!vessel_->creative) vessel_->ship.cargo.Add(Resource::Metal, delta > 0 ? -metal : -metal * rules_.armament.refund);  // quitar devuelve la mitad
    vessel_->ship.loadout.pointDefense = points;
    Say("Defensa puntual: " + std::to_string(points) + " de " + std::to_string(capacity) + " puntos.");
    return true;
}

int GameSession::PointDefenseCapacity() const { return DefensePoints(rules_.armament, rules_.ship.classId, rules_.ship.variant); }

double GameSession::PointDefenseRate() const {
    int platforms = 0;
    for (std::size_t i = 0; i < vessel_->ship.loadout.modules.size(); ++i)
        platforms += vessel_->ship.loadout.modules[i] == ShipModuleKind::PointDefensePlatform && SlotWorking(vessel_->ship, static_cast<int>(i)) ? 1 : 0;
    return game::PointDefenseRate(rules_.armament, vessel_->ship.loadout.pointDefense, platforms);
}

bool GameSession::FireModule(int slot, const TargetRef& target, std::string* why, bool quiet) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        if (!quiet) Say(reason);
        return false;
    };
    if (vessel_->phase != GamePhase::Flying || pilot_->onFoot) return false;
    if (slot < 0 || slot >= static_cast<int>(vessel_->ship.loadout.modules.size())) return fail("Esa ranura no existe.");
    const HumanWeaponSpec* w = HumanWeaponOf(rules_.armament, vessel_->ship.loadout.modules[static_cast<std::size_t>(slot)]);
    if (!w) return fail("Esa ranura no lleva un arma humana.");
    const std::string name = w->name;
    if (!SlotWorking(vessel_->ship, slot)) return fail(name + ": dañada.");
    const Anomaly* a = target.kind == Kind::Anomaly ? FindAnomaly(target.index) : nullptr;
    if (!a || a->maxHull <= 0.0 || a->hull <= 0.0) return fail("Sin blanco: fija una nave o un bastión.");
    if (vessel_->ship.warpSpeed > rules_.drive.impulseSpeed) return fail("No se puede disparar con la burbuja formada.");
    if (Overheated()) return fail(name + ": sobrecalentada, espera a que baje el calor.");
    double& cooldown = vessel_->ship.loadout.cooldown[static_cast<std::size_t>(slot)];
    int& ammo = vessel_->ship.loadout.ammo[static_cast<std::size_t>(slot)];
    if (cooldown > 0.0) return fail(name + ": recargando.");
    if (w->capacity > 0 && ammo <= 0) return fail(name + ": sin misiles; se fabrican atracada.");
    if (Length(AnomalyPosition(*a) - vessel_->ship.position) > w->range) return fail(name + ": fuera de alcance (" + Fixed(w->range / 1000.0, 0) + " mil km).");
    if (vessel_->ship.energy < w->energy) return fail(name + ": sin energía para disparar.");
    const FiringSolution s = Solve(vessel_->ship.position, ShipVelocity(), AnomalyPosition(*a), a->velocity, w->speed, false, GravitySources(), vessel_->ship.computerTier, rules_.combat);
    if (!s.valid) return fail(name + ": sin solución de tiro.");

    Vec3d direction = s.direction;
    if (w->type == ArmType::RailSpinal) {
        // Fija al eje: dispara por donde mira el casco, y solo si el blanco esta en la mira.
        const double miss = std::acos(std::clamp(Dot(Heading(vessel_->ship), s.direction), -1.0, 1.0)) * Length(AnomalyPosition(*a) - vessel_->ship.position);
        if (miss > w->proximity) return fail(name + ": el blanco no está en el eje; gira la nave.");
        direction = Heading(vessel_->ship);
    }
    if (w->turn <= 0.0) direction = Scatter(direction, AimError(rules_.combat, vessel_->ship.computerTier) * w->aimError * (w->type == ArmType::RailSpinal ? 1.0 : AimSpread(vessel_->ship.zones)), bubble_.rng);

    vessel_->ship.energy -= w->energy;
    cooldown = w->cooldown;
    if (w->capacity > 0) --ammo;
    vessel_->ship.defense.heat = std::min(vessel_->ship.defense.heat + w->heat, rules_.thermal.maximum);
    Projectile p;
    p.kind = w->turn > 0.0 ? WeaponKind::Missile : WeaponKind::Turret;
    p.type = w->damageType;
    p.team = 0;
    p.owner = 0;
    p.ship = vessel_->id;
    p.target = a->id;
    p.damage = w->damage;
    p.proximity = w->proximity;
    p.life = w->life;
    p.guided = w->turn > 0.0;
    p.cruise = w->speed;
    p.turn = w->turn;
    const ShipDesign& design = CurrentShipDesign();
    p.position = vessel_->ship.position + vessel_->ship.orientation.ToMatrix().TransformDirection(design.slots[static_cast<std::size_t>(slot)].position * 0.001) + direction * 0.03;
    p.origin = p.position;
    p.velocity = ShipVelocity() + direction * (p.guided ? w->speed * 0.3 : w->speed);
    bubble_.projectiles.push_back(p);
    return true;
}

bool GameSession::SetImpulseEngine(int tier, std::string* why) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!CanRefit()) return fail("El motor se cambia atracado en una estación propia.");
    if (tier < 1 || tier > kImpulseEngineCount) return fail("Ese motor no existe.");
    if (tier == vessel_->ship.loadout.engine) return fail("Ya lleva ese motor.");
    const ImpulseEngineSpec& next = ImpulseEngine(tier);
    const ImpulseEngineSpec& old = ImpulseEngine(vessel_->ship.loadout.engine);
    const double metal = vessel_->creative ? 0.0 : next.metal - old.metal * 0.5, silicate = vessel_->creative ? 0.0 : next.silicate - old.silicate * 0.5;
    if (vessel_->ship.cargo.Get(Resource::Metal) < metal || vessel_->ship.cargo.Get(Resource::Silicate) < silicate)
        return fail(std::string("Faltan materiales: ") + next.name + " pide " + Fixed(std::max(metal, 0.0), 0) + " kt de metal y " + Fixed(std::max(silicate, 0.0), 0) + " de silicato.");
    vessel_->ship.cargo.Add(Resource::Metal, -metal);
    vessel_->ship.cargo.Add(Resource::Silicate, -silicate);
    vessel_->ship.loadout.engine = tier;
    Say(std::string("Motor de impulso: ") + next.name + ".");
    return true;
}

void GameSession::DamageZone(int zone, double hullAmount, const Vec3d* from) {
    if (zone < 0 || zone >= kHullZoneCount || hullAmount <= 0.0) return;
    vessel_->ship.hull = std::max(vessel_->ship.hull - hullAmount, 0.0);
    vessel_->directedLoss += hullAmount;
    if (from) RecordHit(zone, Normalize(*from + Vec3d{0, 0, 1e-9}), hullAmount / rules_.ship.hull, false);
    WoundZone(zone, hullAmount / (rules_.ship.hull * kZoneToughness));
}

void GameSession::UpdateLostPieces(double seconds, double realSeconds) {
    for (Vessel& v : fleet_) {
        for (int z = 0; z < kHullZoneCount; ++z) v.zoneHeat[static_cast<std::size_t>(z)] *= std::exp(-realSeconds / 6.0);
        for (HullHit& h : v.hullHits) h.age += realSeconds;
    }
    for (Anomaly& a : world_.anomalies)
        for (double& h : a.zoneHeat) h *= std::exp(-realSeconds / 6.0);
    for (LostPiece& p : bubble_.lostPieces) {
        p.absolute += p.velocity * seconds;
        p.orientation = Quatd::FromAxisAngle(p.spinAxis, p.spin * seconds) * p.orientation;
        p.age += realSeconds;
    }
    // Pasado un rato, o lejos de la nave que se mira, ya no se ve: se olvida.
    const Vec3d ship = fleet_[static_cast<std::size_t>(focus_)].ship.position + engine_.GetWorld().origin;
    bubble_.lostPieces.erase(std::remove_if(bubble_.lostPieces.begin(), bubble_.lostPieces.end(), [&](const LostPiece& p) { return p.age > 120.0 || Length(p.absolute - ship) > 2.0e4; }), bubble_.lostPieces.end());
}

// Un impacto en una nave o un bastion. Si estaba en calma, ahora deja de estarlo.
void GameSession::Strike(Anomaly& a, const Projectile& p, const ZoneImpact* impact) { DamageAnomaly(a, p.type, p.damage, p.team, impact); }

void GameSession::DamageAnomaly(Anomaly& a, DamageType type, double amount, int team, const ZoneImpact* impact) {
    if (a.hull <= 0.0) return;
    // Primero cambia de bando: una neutral atacada pasa a su modelo hostil, y en el caen las zonas.
    if (team == 0 && a.stance != Stance::Hostile) {
        a.stance = Stance::Hostile;
        Say(a.name + " responde al fuego.");
    }
    const double taken = ShieldAnomaly(a, rules_.defense, type, amount);
    a.hull -= taken;
    // Atacar sube la alerta de la region, en proporcion al dano (en cascos de fragata ligera).
    if (team == 0) RaiseAlert(world_.species, a.system, rules_.alert.attackRaise * std::max(taken, 0.0) / rules_.anomalies.referenceHull, bubble_.simTime, rules_.alert);
    const ZoneLayout* layout = ZonesOf(a);
    if (impact && layout && taken > 0.0 && a.maxHull > 0.0) {
        if (team == 0) {
            ++pilot_->lastStrike.serial;
            pilot_->lastStrike.anomaly = a.id;
            pilot_->lastStrike.zone = impact->zone;
            pilot_->lastStrike.point = impact->point;
            pilot_->strikeAge = 0.0;
            TrackStrike();
        }
        if (a.hull > 0.0) {
            // Las mismas cuentas que en la nave: una zona aguanta kZoneToughness de su casco entero.
            const double critical = ZoneInfo(impact->zone).red != RedZone::None ? kCriticalHit : 1.0;
            WoundAnomaly(a, impact->zone, taken * critical / (a.maxHull * kZoneToughness), team);
        }
    }
    if (a.hull > 0.0) return;
    a.hull = 0.0;
    if (team != 0) return;
    ++pilot_->kills;
    const double loot = a.kind == AnomalyKind::AlienStation ? rules_.anomalies.stationLoot : rules_.anomalies.shipLoot;
    vessel_->ship.cargo.Add(Resource::Metal, loot);
    Say(a.name + " destruida. Restos: +" + MassText(loot) + " de metal.");
    OnAlienDestroyed(a);
}

// Lo que pasa en la especie al perder una nave o una estacion a manos del jugador.
void GameSession::OnAlienDestroyed(const Anomaly& a) {
    const int level = a.kind == AnomalyKind::AlienStation ? a.stationLevel : 0;
    if (level == kHangarLevel) {
        // Un hangar militar: la alerta llega a los sistemas de alrededor, no solo a su region.
        const World& w = engine_.GetWorld();
        std::vector<Vec3d> positions;
        positions.reserve(w.sectors.size());
        for (const auto& sector : w.sectors) positions.push_back(w.scene.WorldPosition(sector));
        RaiseAlert(world_.species, a.system, DestroyRaise(rules_.alert, level), bubble_.simTime, rules_.alert, &positions, rules_.alert.hangarSpread);
        Say("La destrucción del hangar militar extiende la alerta a " + std::to_string(rules_.alert.hangarSpread) + " sistemas.");
    } else {
        RaiseAlert(world_.species, a.system, DestroyRaise(rules_.alert, level), bubble_.simTime, rules_.alert);
    }
    if (TriggersSurrender(a)) {
        BeginSurrender(world_.species, bubble_.simTime, rules_.alert);
        Say("La flota alienígena se detiene: rendición temporal. Cuando vuelva, lo hará más fuerte.", Audience::Everyone);
    }
}

// Como WoundZone en la nave: una zona periferica a 0 se suelta y sale girando; lo que sobra, y lo que
// entra por el hueco de una zona ya perdida o que su modelo no tiene, sigue hacia la zona de dentro.
void GameSession::WoundAnomaly(Anomaly& a, int zone, double fraction, int team) {
    const ZoneLayout* layout = ZonesOf(a);
    if (!layout || zone < 0 || zone >= kHullZoneCount || fraction <= 0.0 || a.hull <= 0.0) return;
    const HullZoneInfo& info = ZoneInfo(zone);
    const std::size_t z = static_cast<std::size_t>(zone);
    if (!ZonePresent(*layout, zone)) {
        WoundAnomaly(a, info.inner, fraction, team);
        return;
    }
    double& integrity = a.zones[z];
    if (ZoneLost(a.zones, zone)) {
        WoundAnomaly(a, info.inner, fraction * kInnerShare, team);
        return;
    }
    const double excess = fraction - integrity;
    const bool wasStanding = integrity > 0.0;
    integrity = std::max(integrity - fraction, 0.0);
    a.zoneHeat[z] = std::min(a.zoneHeat[z] + 0.3 + fraction * 4.0, 1.0);
    if (integrity > 0.0) return;

    if (info.red != RedZone::None) {
        if (wasStanding) FailAnomalyZone(a, zone, team);
        return;
    }
    if (!info.losable || !wasStanding) return;

    const Quatd turn = Quatd::FromAxisAngle({0, 1, 0}, static_cast<double>(a.yaw));
    const Vec3d pivot = layout->centers[z];
    const Vec3d out = Normalize(turn.ToMatrix().TransformDirection(pivot + Vec3d{0, 0, 1e-3}));
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    LostPiece piece;
    piece.zone = zone;
    piece.model = AlienModelOf(a);
    piece.pivot = pivot;
    piece.absolute = AnomalyPosition(a) + turn.ToMatrix().TransformDirection(pivot * 0.001) + engine_.GetWorld().origin;
    piece.velocity = a.velocity + (out + Vec3d{unit(bubble_.fx), unit(bubble_.fx), unit(bubble_.fx)} * 0.3) * 0.03;
    piece.orientation = turn;
    piece.spinAxis = Normalize(Vec3d{unit(bubble_.fx), unit(bubble_.fx), unit(bubble_.fx)} + Vec3d{0, 0, 1e-3});
    piece.spin = 0.15 + 0.35 * std::fabs(unit(bubble_.fx));
    bubble_.lostPieces.push_back(piece);
    if (team == 0) Say(a.name + " pierde " + ZoneLabel(layout, zone, true) + ".");
    if (excess > 0.0) WoundAnomaly(a, info.inner, excess * kInnerShare, team);
}

// El nucleo a 0 la hace estallar; la espina o el puente (en un bastion, su corazon) la dejan apagada.
void GameSession::FailAnomalyZone(Anomaly& a, int zone, int team) {
    const ZoneLayout* layout = ZonesOf(a);
    if (ZoneInfo(zone).red == RedZone::Explode) {
        if (team == 0) Say(a.name + ": " + ZoneLabel(layout, zone, true) + " revienta.");
        a.hull = 0.0;  // DamageAnomaly, que es quien llega aqui, hace las cuentas de la destruccion
        return;
    }
    if (a.disabled) return;
    a.disabled = true;
    a.pursuing = false;
    if (team == 0) Say(a.name + " queda apagada: ha perdido " + ZoneLabel(layout, zone, true) + ".");
}

int GameSession::ShootAnomaly(int id, const Vec3d& start, const Vec3d& end, double hullAmount) {
    Anomaly* a = FindAnomaly(id);
    if (!a || a->hull <= 0.0) return -1;
    if (a->stance != Stance::Hostile) {
        a->stance = Stance::Hostile;
        Say(a->name + " responde al fuego.");
    }
    const ZoneLayout* layout = ZonesOf(*a);
    if (!layout) return -1;
    const ZoneImpact impact = ProjectileImpact(*layout, start, end);
    HitAnomalyUnshielded(*a, impact, hullAmount);
    return impact.zone;
}

bool GameSession::StrikeAnomaly(int id, DamageType type, double amount) {
    Anomaly* a = FindAnomaly(id);
    if (!a || a->hull <= 0.0) return false;
    DamageAnomaly(*a, type, amount, 0);
    return true;
}

bool GameSession::DamageAnomalyZone(int id, int zone, double hullAmount) {
    Anomaly* a = FindAnomaly(id);
    if (!a || a->hull <= 0.0 || zone < 0 || zone >= kHullZoneCount || hullAmount <= 0.0) return false;
    if (a->stance != Stance::Hostile) {
        a->stance = Stance::Hostile;
        Say(a->name + " responde al fuego.");
    }
    const ZoneLayout* layout = ZonesOf(*a);
    if (!layout) return false;
    ZoneImpact impact;
    impact.zone = zone;
    impact.point = layout->centers[static_cast<std::size_t>(zone)];
    impact.from = Normalize(impact.point + Vec3d{0, 0, 1e-6});
    HitAnomalyUnshielded(*a, impact, hullAmount);
    return true;
}

// Sin escudos: lo que se pide es lo que llega al casco.
void GameSession::HitAnomalyUnshielded(Anomaly& a, const ZoneImpact& impact, double hullAmount) {
    if (hullAmount <= 0.0) return;
    const double shield = a.shieldCharge;
    a.shieldCharge = 0.0;
    DamageAnomaly(a, DamageType::Kinetic, hullAmount, 0, &impact);
    a.shieldCharge = shield;
}

void GameSession::SetCinematic(bool on) {
    if (on == pilot_->cinematic) return;
    pilot_->cinematic = on;
    Say(on ? "Modo cinemática: la cámara seguirá cada impacto en el blanco." : "Modo cinemática desactivado.");
}

bool GameSession::CinematicView() const {
    return pilot_->cinematic && pilot_->lastStrike.serial > 0 && pilot_->strikeAge < rules_.cinematic.seconds && vessel_->phase == GamePhase::Flying && !pilot_->onFoot;
}

void GameSession::TrackStrike() {
    const Anomaly* a = pilot_->lastStrike.anomaly >= 0 ? FindAnomaly(pilot_->lastStrike.anomaly) : nullptr;
    if (!a) return;
    pilot_->lastStrike.target = AnomalyPosition(*a) + engine_.GetWorld().origin;
    pilot_->lastStrike.yaw = static_cast<double>(a->yaw);
    if (const ZoneLayout* layout = ZonesOf(*a)) pilot_->lastStrike.size = Length(layout->extent) * 0.001;
}

// Gira sobre Y (rumbo) y luego sobre X (cabeceo): +Z acaba en forward y no hay alabeo.
Quatd CameraLooking(const Vec3d& forward) {
    const double length = Length(forward);
    const Vec3d d = length > 1e-12 ? forward * (1.0 / length) : Vec3d{0, 0, 1};
    const double heading = std::atan2(d.x, d.z);
    const double pitch = std::atan2(-d.y, std::sqrt(d.x * d.x + d.z * d.z));
    return (Quatd::FromAxisAngle({0, 1, 0}, heading) * Quatd::FromAxisAngle({1, 0, 0}, pitch)).Normalized();
}

// La camara, a un lado del golpe y algo por encima, da vueltas despacio alrededor del blanco.
bool GameSession::CinematicShot(Vec3d& eye, Quatd& look) const {
    if (!CinematicView()) return false;
    const CinematicRules& r = rules_.cinematic;
    const Vec3d target = pilot_->lastStrike.target - engine_.GetWorld().origin;
    const Quatd turn = Quatd::FromAxisAngle({0, 1, 0}, pilot_->lastStrike.yaw);
    const Vec3d hit = target + turn.ToMatrix().TransformDirection(pilot_->lastStrike.point * 0.001);
    // Empieza del lado del golpe; si dio en el centro, del lado de la nave.
    Vec3d side = hit - target;
    side.y = 0.0;
    if (Length(side) < 1e-6) {
        side = vessel_->ship.position - target;
        side.y = 0.0;
    }
    if (Length(side) < 1e-9) side = Vec3d{1, 0, 0};
    const Vec3d around = Quatd::FromAxisAngle({0, 1, 0}, pilot_->strikeAge * r.orbitRate).ToMatrix().TransformDirection(Normalize(side));
    const double distance = std::max(pilot_->lastStrike.size * r.distance, r.minimum);
    eye = hit + around * distance + Vec3d{0, -distance * 0.35, 0};  // arriba es -Y
    look = CameraLooking(hit - eye);
    return true;
}

void GameSession::UpdateCombat(double seconds) {
    for (Vessel& v : fleet_) {
        if (v.phase != GamePhase::Flying) continue;
        for (WeaponMount& m : v.ship.weapons) m.cooldown = std::max(m.cooldown - seconds, 0.0);
        for (double& c : v.ship.loadout.cooldown) c = std::max(c - seconds, 0.0);
    }
    if (bubble_.projectiles.empty()) return;
    const GameMode& mode = ModeInfo(world_.mode);

    // Las torretas derriban misiles y drones que se acerquen: las de los jugadores y las de los hostiles.
    // Y la defensa puntual de cada nave (puntos y plataformas, Armament.h), que se suma a sus torretas.
    struct Defender { Vec3d position; int team; double rate; int vessel; };  // rate: probabilidad por segundo
    std::vector<Defender> defenders;
    for (std::size_t i = 0; i < fleet_.size(); ++i) {
        if (fleet_[i].phase != GamePhase::Flying) continue;
        const Using mine(*this, static_cast<int>(i));
        int tier = 0;
        for (const WeaponMount& m : vessel_->ship.weapons)
            if (m.kind == WeaponKind::Turret) tier = std::max(tier, m.tier);
        const double rate = rules_.combat.defenseRate * tier + PointDefenseRate();
        if (rate > 0.0) defenders.push_back({vessel_->ship.position, 0, rate, static_cast<int>(i)});
    }
    for (const Anomaly& a : world_.anomalies) {
        if (!mode.hostilesAttack || !a.armed || a.disabled || a.hull <= 0.0 || a.weapon != WeaponKind::Turret || a.stance != Stance::Hostile) continue;
        defenders.push_back({AnomalyPosition(a), TeamOf(a), rules_.combat.defenseRate * a.weaponTier, -1});
    }
    for (Projectile& p : bubble_.projectiles) {
        if (!p.guided || p.dead) continue;
        for (const Defender& d : defenders) {
            if (d.team == p.team || Length(p.position - d.position) > rules_.combat.defenseRange) continue;
            if (Uniform(bubble_.rng, 0.0, 1.0) < 1.0 - std::exp(-d.rate * seconds)) {
                p.dead = true;
                if (d.vessel >= 0) {
                    const Using mine(*this, d.vessel);
                    Say(std::string(Name(p.kind)) + " enemigo derribado.");
                }
                break;
            }
        }
    }
    StepProjectiles(seconds);
}

// Cada proyectil avanza en tramos cortos y se comprueba todo el recorrido: uno rapido no atraviesa nada.
void GameSession::StepProjectiles(double seconds) {
    if (bubble_.projectiles.empty() || seconds <= 0.0) return;
    const std::vector<GravitySource> sources = GravitySources();
    const int parts = std::clamp(static_cast<int>(std::ceil(seconds / 0.25)), 1, 120);
    const double dt = seconds / parts;
    // Las naves en vuelo: pueden recibir un proyectil enemigo. Entre naves de jugadores no hay fuego.
    struct Hull { int index; int id; Vec3d velocity; };
    std::vector<Hull> hulls;
    for (std::size_t i = 0; i < fleet_.size(); ++i)
        if (fleet_[i].phase == GamePhase::Flying) hulls.push_back({static_cast<int>(i), fleet_[i].id, VelocityOf(fleet_[i])});

    // Las naves y las anomalias ya se movieron este paso: aqui se retrocede hasta el principio de el.
    std::vector<Anomaly*> targets;
    std::vector<Vec3d> ends;
    for (Anomaly& a : world_.anomalies) {
        if (a.maxHull <= 0.0 || a.hull <= 0.0) continue;
        targets.push_back(&a);
        ends.push_back(AnomalyPosition(a));
    }
    for (int part = 1; part <= parts; ++part) {
        const double back = seconds - part * dt;  // lo que falta para el final del paso
        for (Projectile& p : bubble_.projectiles) {
            if (p.dead) continue;
            Vec3d tp, tv;
            bool have = false;
            if (p.guided && p.target == 0) {
                for (const Hull& h : hulls) {
                    if (h.id != p.ship) continue;
                    tp = fleet_[static_cast<std::size_t>(h.index)].ship.position - h.velocity * back;
                    tv = h.velocity;
                    have = true;
                    break;
                }
            } else if (p.guided && p.target > 0) {
                for (std::size_t i = 0; i < targets.size(); ++i) {
                    if (targets[i]->id != p.target || targets[i]->hull <= 0.0) continue;
                    tp = ends[i] - targets[i]->velocity * back;
                    tv = targets[i]->velocity;
                    have = true;
                    break;
                }
            }
            const Vec3d from = p.position;
            StepProjectile(p, sources, rules_.combat, dt, have ? &tp : nullptr, have ? &tv : nullptr);
            p.age += dt;
            const auto finish = [&](bool hit) {
                p.dead = true;
                bubble_.impacts.push_back({p.position, p.origin, p.type, p.beam, hit, 0.0});
            };
            if (p.life <= 0.0) {
                if (p.beam) finish(false);  // el haz se ve aunque no de en nada
                else p.dead = true;
                continue;
            }
            for (const GravitySource& s : sources) {
                if (!p.dead && SegmentNear(from, p.position, s.position, s.radius)) finish(true);  // se estrella
            }
            if (p.dead) continue;
            if (p.team != 0) {
                for (const Hull& h : hulls) {
                    const Vessel& v = fleet_[static_cast<std::size_t>(h.index)];
                    if (v.phase != GamePhase::Flying || !SegmentNear(from, p.position, v.ship.position - h.velocity * back, rules_.ship.radius + p.proximity)) continue;
                    const Using struck(*this, h.index);
                    const Anomaly* owner = FindAnomaly(p.owner);
                    const Vec3d relative = p.velocity - h.velocity;
                    const Vec3d toward = Length(relative) > 1e-9 ? relative * (-1.0 / Length(relative)) : Normalize(p.origin - vessel_->ship.position);
                    // Donde da de verdad: el tramo recorrido en este paso, visto desde la nave (que tambien
                    // se ha movido), en metros del modelo. La trayectoria decide la zona, no el atacante.
                    const Vec3d center = vessel_->ship.position - h.velocity * back;
                    const auto toModel = vessel_->ship.orientation.Conjugate().ToMatrix();
                    const Vec3d start = toModel.TransformDirection(from - (center - h.velocity * dt)) * 1000.0;
                    const Vec3d end = toModel.TransformDirection(p.position - center) * 1000.0;
                    const ZoneImpact impact = ProjectileImpact(start, end);
                    Hurt(p.type, p.damage, owner ? owner->name : std::string("fuego enemigo"), toward, p.owner, &impact);
                    finish(true);
                    break;
                }
                if (p.dead) continue;
            }
            for (std::size_t i = 0; i < targets.size(); ++i) {
                Anomaly& a = *targets[i];
                if (a.hull <= 0.0 || TeamOf(a) == p.team || a.id == p.owner) continue;
                if (SegmentNear(from, p.position, ends[i] - a.velocity * back, a.radius + p.proximity)) {
                    // El golpe es de la nave que disparo: suyas son la baja, el botin y los avisos.
                    const int shooter = p.team == 0 ? IndexOfId(p.ship) : -1;
                    const Using mine(*this, shooter >= 0 ? shooter : IndexOf(*vessel_));
                    // Donde da de verdad, como en la nave: el tramo de este paso visto desde la anomalia,
                    // en metros de su modelo.
                    if (const ZoneLayout* layout = ZonesOf(a)) {
                        const Vec3d center = ends[i] - a.velocity * back;
                        const auto toModel = Quatd::FromAxisAngle({0, 1, 0}, static_cast<double>(a.yaw)).Conjugate().ToMatrix();
                        const Vec3d start = toModel.TransformDirection(from - (center - a.velocity * dt)) * 1000.0;
                        const Vec3d end = toModel.TransformDirection(p.position - center) * 1000.0;
                        const ZoneImpact impact = ProjectileImpact(*layout, start, end);
                        Strike(a, p, &impact);
                    } else {
                        Strike(a, p);
                    }
                    finish(true);
                    break;
                }
            }
        }
    }
    bubble_.projectiles.erase(std::remove_if(bubble_.projectiles.begin(), bubble_.projectiles.end(), [](const Projectile& p) { return p.dead; }), bubble_.projectiles.end());
    world_.anomalies.erase(std::remove_if(world_.anomalies.begin(), world_.anomalies.end(), [](const Anomaly& a) { return a.maxHull > 0.0 && a.hull <= 0.0; }), world_.anomalies.end());
}

// Entrar en la orbita de una estrella demasiado caliente para el blindaje quema el casco.
void GameSession::UpdateHeat(double seconds) {
    const World& w = engine_.GetWorld();
    vessel_->inHeatZone = false;
    const std::size_t s = static_cast<std::size_t>(vessel_->nearestSystem);
    const SystemState& state = w.state[s];
    const StarSystemData& system = *w.systems[s];
    const double damage = ModeInfo(world_.mode).damage;
    for (std::size_t m = 0; m < state.starNodes.size(); ++m) {
        const StarData& star = system.StarAt(static_cast<int>(m));
        const StarYield yield = YieldOf(star);
        const double distance = Length(w.scene.WorldPosition(state.starNodes[m]) - vessel_->ship.position);
        if (vessel_->ship.heatShieldTier < yield.requiredTier && distance < yield.orbitRadius * 1.5) {
            vessel_->inHeatZone = true;
            vessel_->ship.hull = std::max(vessel_->ship.hull - damage * 300.0 * (yield.requiredTier - vessel_->ship.heatShieldTier) * seconds / 3600.0, 0.0);
        }
        if (star.kind == StarClass::BlackHole) continue;

        // El calor sube con la cuarta potencia al acercarse: lejos no se nota y cerca evapora.
        const double reach = star.radius * std::max(rules_.ship.heatReach - 0.5 * (vessel_->ship.heatShieldTier - yield.requiredTier), 1.4);
        if (distance < reach) {
            vessel_->inHeatZone = true;
            const double ratio = reach / std::max(distance, 1.0);
            vessel_->ship.hull = std::max(vessel_->ship.hull - damage * rules_.ship.heatBurn * (ratio * ratio * ratio * ratio - 1.0) * seconds, 0.0);
            if (vessel_->ship.hull <= 0.0) vessel_->lossReason = "Nave evaporada por el calor de " + w.bodies[static_cast<std::size_t>(state.starBodies[std::min(m, state.starBodies.size() - 1)])].name + ".";
        }
    }
}

// ---- Escudos y calor ---------------------------------------------------------------------

bool GameSession::SetShield(ShieldKind kind) {
    DefenseState& d = vessel_->ship.defense;
    if (!IsField(kind) || d.shieldTier[static_cast<std::size_t>(kind)] <= 0) {
        Say(std::string("Escudo ") + Name(kind) + ": no lo tienes.");
        return false;
    }
    const bool changed = d.field != kind || !d.fieldOn;
    d.field = kind;
    d.fieldOn = true;
    if (changed) Say(std::string("Escudo: ") + Name(kind) + ".");
    return true;
}

void GameSession::SetShieldOn(bool on) {
    if (vessel_->ship.defense.fieldOn == on) return;
    vessel_->ship.defense.fieldOn = on;
    Say(on ? std::string("Escudo: ") + Name(vessel_->ship.defense.field) + "." : std::string("Escudos apagados."));
}

ShieldKind GameSession::CycleShield() {
    DefenseState& d = vessel_->ship.defense;
    const int count = static_cast<int>(ShieldKind::Count);
    if (!d.fieldOn) {
        for (int k = 0; k < count; ++k)
            if (IsField(static_cast<ShieldKind>(k)) && d.shieldTier[static_cast<std::size_t>(k)] > 0) {
                SetShield(static_cast<ShieldKind>(k));
                return d.field;
            }
        return d.field;
    }
    for (int k = static_cast<int>(d.field) + 1; k < count; ++k)
        if (IsField(static_cast<ShieldKind>(k)) && d.shieldTier[static_cast<std::size_t>(k)] > 0) {
            SetShield(static_cast<ShieldKind>(k));
            return d.field;
        }
    SetShieldOn(false);
    return d.field;
}

bool GameSession::FieldActive() const {
    const DefenseState& d = vessel_->ship.defense;
    const std::size_t f = static_cast<std::size_t>(d.field);
    if (!d.fieldOn || !IsField(d.field) || d.shieldTier[f] <= 0) return false;
    if (d.field == ShieldKind::Love) return true;  // ni calor ni energia: es otra fisica
    if (Overheated()) return false;
    if (d.field == ShieldKind::Dust && vessel_->ship.cargo.Get(Resource::Silicate) <= 0.0) return false;
    return rules_.defense.shields[f].energy <= 0.0 || vessel_->ship.energy > 0.0;
}

bool GameSession::Overheated() const { return game::Overheated(vessel_->ship.defense, rules_.thermal); }

void GameSession::SetCoolant(CoolantKind kind, bool on) { vessel_->ship.defense.coolantOn[static_cast<std::size_t>(kind)] = on; }

// proper son segundos a bordo: los sistemas de la nave van con su reloj, no con el de fuera.
void GameSession::UpdateThermal(double proper, double realSeconds) {
    DefenseState& d = vessel_->ship.defense;
    const ThermalRules& t = rules_.thermal;
    const DefenseRules& r = rules_.defense;
    const GameMode& mode = ModeInfo(world_.mode);
    const World& w = engine_.GetWorld();
    const double density = NebulaDensity(vessel_->ship.position + w.origin);
    const double hours = proper / 3600.0;
    double heatIn = 0.0;

    // El campo encendido gasta, calienta y se recarga.
    const std::size_t f = static_cast<std::size_t>(d.field);
    if (FieldActive() && d.field != ShieldKind::Love) {
        const ShieldRules& s = r.shields[f];
        vessel_->ship.energy = std::max(vessel_->ship.energy - s.energy * hours, 0.0);
        if (d.field == ShieldKind::Dust) vessel_->ship.cargo.Add(Resource::Silicate, -std::min(r.dustUpkeep * hours, vessel_->ship.cargo.Get(Resource::Silicate)));
        heatIn += s.heat * proper;
        d.charge[f] = std::min(d.charge[f] + ShieldRegen(r, d.field, d.shieldTier[f]) * proper, ShieldCapacity(r, d.field, d.shieldTier[f]));
    }

    // Curvatura: calienta al formarse la burbuja y, mientras dura, segun la velocidad.
    const bool curving = Curves(vessel_->ship.mode);
    if (curving) heatIn += t.formHeat * std::max(vessel_->ship.warpCharge - vessel_->lastWarpCharge, 0.0);
    vessel_->lastWarpCharge = curving ? vessel_->ship.warpCharge : 0.0;
    const bool stable = curving && vessel_->ship.warpCharge >= 1.0 && vessel_->ship.warpSpeed > 0.0;
    const double tens = vessel_->ship.warpSpeed / (10.0 * kLightSpeed);
    if (stable) heatIn += t.warpHeat * std::sqrt(tens) * proper;

    // Presion de ariete: por encima de c, el gas golpea la proa. Los conos la aguantan; sin ellos, el casco.
    const std::size_t a = static_cast<std::size_t>(ShieldKind::Ablative);
    vessel_->ramming = stable && vessel_->ship.warpSpeed > kLightSpeed;
    if (vessel_->ramming) {
        double ram = r.ramPressure * density * std::sqrt(tens) * realSeconds;
        const double plates = std::min(ram, d.charge[a]);
        d.charge[a] -= plates;
        ram -= plates;
        if (ram > 0.0 && mode.damage > 0.0) {
            vessel_->ship.hull = std::max(vessel_->ship.hull - ram * mode.damage, 0.0);
            if (!vessel_->ramWarned) Say("Presión de ariete: sin conos ablativos, la curvatura castiga el casco.");
            vessel_->ramWarned = true;
            if (vessel_->ship.hull <= 0.0) vessel_->lossReason = "Nave aplastada por la presión de ariete.";
        }
    } else {
        vessel_->ramWarned = false;
        // El gas de la nebulosa lija las placas, tanto mas cuanto mas rapido.
        if (!vessel_->orbiting) d.charge[a] = std::max(d.charge[a] - r.ablativeWear * density * std::min(vessel_->ship.speed / kLightSpeed, 1.0) * proper, 0.0);
    }
    // Las placas de los conos van montadas en la proa: con ella danada caben menos, y sin ella ninguna.
    const double plateCap = ShieldCapacity(r, ShieldKind::Ablative, d.shieldTier[a]) * std::max(Zone(vessel_->ship.zones, HullZone::Bow), 0.0);
    d.charge[a] = std::min(d.charge[a], plateCap);
    vessel_->ship.massFactor = 1.0 + (plateCap > 0.0 ? r.ablativeMass * d.charge[a] / plateCap : 0.0);

    // Junto a una estrella los radiadores apenas sueltan calor, y en su zona de calor entra mas.
    vessel_->radiators = 1.0;
    const SystemState& state = w.state[static_cast<std::size_t>(vessel_->nearestSystem)];
    const StarSystemData& system = *w.systems[static_cast<std::size_t>(vessel_->nearestSystem)];
    for (std::size_t m = 0; m < state.starNodes.size(); ++m) {
        const StarData& star = system.StarAt(static_cast<int>(m));
        if (star.kind == StarClass::BlackHole) continue;
        const double reach = star.radius * t.starReach;
        const double distance = Length(w.scene.WorldPosition(state.starNodes[m]) - vessel_->ship.position);
        if (distance < reach) vessel_->radiators = std::min(vessel_->radiators, (distance / reach) * (distance / reach));
    }
    vessel_->radiators *= RadiatorOutput(vessel_->ship.zones) * (1.0 + kRadiatorBoost * WorkingModules(vessel_->ship, ShipModuleKind::Radiator));  // rotos sueltan menos; las aletas, mas
    if (vessel_->inHeatZone) heatIn += t.starHeat * proper;

    // En una estacion propia: agua del hielo de la bodega y placas nuevas con metal.
    const bool atStation = vessel_->orbiting && vessel_->orbiting->kind == Kind::Station && world_.stations.Find(vessel_->orbiting->index) != nullptr;
    if (atStation) {
        const double water = std::min({t.stationRefill * hours, t.waterTank - d.water, vessel_->ship.cargo.Get(Resource::Ice)});
        d.water += water;
        vessel_->ship.cargo.Add(Resource::Ice, -water);
        const double plates = std::min({r.plateRepair * hours, plateCap - d.charge[a], vessel_->ship.cargo.Get(Resource::Metal) / r.plateMetal});
        if (plates > 0.0) {
            d.charge[a] += plates;
            vessel_->ship.cargo.Add(Resource::Metal, -plates * r.plateMetal);
        }
    }

    // Con la nave fria y energia de sobra, se licua nitrogeno del gas de la bodega.
    if (d.coolantTier[2] > 0 && d.nitrogen < t.nitrogenTank && d.heat < t.limit * t.coolStart && vessel_->ship.energy > rules_.ship.energy * 0.3) {
        const double make = std::min({t.nitrogenMake * hours, t.nitrogenTank - d.nitrogen, vessel_->ship.cargo.Get(Resource::Gas), vessel_->ship.energy / t.nitrogenEnergy});
        if (make > 0.0) {
            d.nitrogen += make;
            vessel_->ship.cargo.Add(Resource::Gas, -make);
            vessel_->ship.energy -= make * t.nitrogenEnergy;
        }
    }

    const CoolingResult cool = StepCooling(d, t, proper, heatIn, vessel_->radiators, vessel_->ship.cargo.Get(Resource::Ice));
    vessel_->ship.cargo.Add(Resource::Ice, -cool.iceUsed);

    if (Overheated()) {
        if (mode.damage > 0.0) {
            vessel_->ship.hull = std::max(vessel_->ship.hull - t.overheatBurn * (d.heat - t.limit) * proper * mode.damage, 0.0);
            if (vessel_->ship.hull <= 0.0) vessel_->lossReason = "Nave fundida por su propio calor.";
        }
        if (!vessel_->overheatWarned) Say("Sobrecalentamiento: escudos y armas fuera de servicio.");
        vessel_->overheatWarned = true;
    } else if (vessel_->overheatWarned && d.heat < t.limit * 0.9) {
        vessel_->overheatWarned = false;
        Say("El calor baja del límite: escudos y armas de vuelta.");
    }
}

void GameSession::UpdateRepair(double seconds) {
    if (!vessel_->orbiting || vessel_->orbiting->kind != Kind::Station) return;
    const Station* s = world_.stations.Find(vessel_->orbiting->index);
    // Atracada, se repara en cualquier estacion propia.
    if (!s || (s->kind != StationKind::Power && !Docked())) return;
    vessel_->ship.hull = std::min(vessel_->ship.hull + rules_.ship.repairPerHour * seconds / 3600.0, rules_.ship.hull);
    // Las zonas se reparan al mismo ritmo. Una pieza perdida solo se rehace atracada: hace falta el muelle.
    const double rate = rules_.ship.repairPerHour * seconds / 3600.0 / (rules_.ship.hull * kZoneToughness);
    for (int z = 0; z < kHullZoneCount; ++z) {
        double& integrity = vessel_->ship.zones[static_cast<std::size_t>(z)];
        if (integrity >= 1.0 || (ZoneLost(vessel_->ship.zones, z) && !Docked())) continue;
        const bool rebuilt = ZoneLost(vessel_->ship.zones, z);
        integrity = std::min(integrity + rate, 1.0);
        if (rebuilt) Say(std::string("Reconstruida en el muelle: ") + ZoneInfo(z).name + ".");
        // Una zona como nueva ya no tiene crateres.
        if (integrity >= 1.0) vessel_->hullHits.erase(std::remove_if(vessel_->hullHits.begin(), vessel_->hullHits.end(), [z](const HullHit& h) { return h.zone == z; }), vessel_->hullHits.end());
    }
    // Las piezas igual: una arrancada solo se rehace en el muelle.
    bool rebuilt = false;
    for (std::size_t i = 0; i < vessel_->ship.pieces.size(); ++i) {
        double& integrity = vessel_->ship.pieces[i];
        if (integrity >= 1.0 || (integrity <= 0.0 && !Docked())) continue;
        rebuilt = rebuilt || integrity <= 0.0;
        integrity = std::min(integrity + rate * 2.0, 1.0);
    }
    if (rebuilt) {
        Say("Piezas arrancadas, rehechas en el muelle.");
        SyncModuleWeapons();
    }
}

void GameSession::UpdateExploration(double hours) {
    if (vessel_->exploring < 0) return;
    for (Anomaly& a : world_.anomalies) {
        if (a.id != vessel_->exploring) continue;
        if (!Explore(a, rules_.anomalies, hours)) {
            if (!a.emitter) vessel_->exploring = -1;
            continue;
        }
        ++vessel_->ship.emitters;
        if (vessel_->ship.emitters == 1) Say("Emisor de energía negativa recuperado: curvatura Híbrida disponible.");
        else if (vessel_->ship.emitters == rules_.drive.hyperEmitters) Say("Segundo emisor de energía negativa: motor Hiper disponible.");
        else Say("Otro emisor de energía negativa: el hiper llega ahora a " + Fixed(HyperFactor(vessel_->ship, rules_.drive), 0) + " c.");
        vessel_->exploring = -1;
        SaveCheckpoint("Partida guardada");
    }
}

// La lista se rehace de vez en cuando, pero lo que se pinta debe estar donde esta ahora: un
// cumulo de asteroides recorre cientos de km entre dos lecturas, y el origen del mundo se mueve.
void GameSession::RefreshContacts() {
    for (Contact& c : vessel_->contacts) {
        if (const std::optional<TargetInfo> info = Resolve(c.target)) {
            c.position = info->position;
            c.distance = Length(info->position - vessel_->ship.position);
        }
    }
}

void GameSession::UpdateContacts() {
    World& w = engine_.GetWorld();
    vessel_->contacts.clear();
    // La pista sobra en cuanto se revela lo que señalaba o se sale de su sistema.
    if (vessel_->echo.active) {
        bool stale = vessel_->echo.system != vessel_->nearestSystem;
        for (const Anomaly& a : world_.anomalies) stale = stale || (a.id == vessel_->echoTarget && a.revealed);
        if (stale) {
            vessel_->echo.active = false;
            vessel_->echoTarget = -1;
        }
    }
    // Lo que se visita ya se conoce, aunque nadie lo haya barrido.
    for (std::size_t s = 0; s < pilot_->known.size(); ++s) {
        if (Length(w.scene.WorldPosition(w.sectors[s]) - vessel_->ship.position) >= w.extents[s] * 1.5) continue;
        pilot_->known[s] = 1;
        pilot_->visited[s] = 1;
    }
    const double sensors = rules_.ship.sensorRange * (Visibility() / rules_.world.fogDistance);
    const double identify = rules_.ship.identifyRange * (Visibility() / rules_.world.fogDistance);

    const auto add = [&](const TargetRef& target, Stance stance, double range) {
        const std::optional<TargetInfo> info = Resolve(target);
        if (!info) return;
        const double distance = Length(info->position - vessel_->ship.position);
        if (distance > range) return;
        Contact c;
        c.target = target;
        c.position = info->position;
        c.distance = distance;
        const bool astro = target.kind == Kind::Planet || target.kind == Kind::Belt;
        c.identified = astro ? Scanned(target) : distance < identify || target.kind == Kind::Station || target.kind == Kind::Star || target.kind == Kind::Ship;
        c.stance = c.identified ? stance : Stance::None;
        c.name = c.identified ? info->name : "Señal sin identificar";
        c.detail = c.identified ? info->detail : "";
        vessel_->contacts.push_back(std::move(c));
    };

    // Una estrella se nota mucho antes que lo demas, tanto mas cuanto mas brilla.
    for (std::size_t s = 0; s < w.state.size(); ++s) {
        if (!pilot_->known[s]) continue;  // otro sistema no aparece hasta que un barrido lo localiza
        for (int m = 0; m < static_cast<int>(w.state[s].starNodes.size()); ++m) {
            add({Kind::Star, static_cast<int>(s), m}, Stance::None, 1.0e300);
        }
        if (!w.state[s].loaded) continue;
        const StarSystemData& system = *w.systems[s];
        for (std::size_t b = 0; b < system.bodies.size(); ++b) {
            const BodyData& body = system.bodies[b];
            if (body.destroyed || body.parent >= 0 || body.kind == PlanetClass::Comet || body.kind == PlanetClass::Asteroid) continue;
            const TargetRef planet{Kind::Planet, static_cast<int>(s), static_cast<int>(b)};
            add(planet, Stance::None, Scanned(planet) ? 1.0e300 : sensors);
        }
        for (std::size_t b = 0; b < system.belts.size(); ++b) {
            const TargetRef belt{Kind::Belt, static_cast<int>(s), static_cast<int>(b)};
            add(belt, Stance::None, Scanned(belt) ? 1.0e300 : sensors);
        }
    }
    for (const Station& s : world_.stations.All()) add({Kind::Station, s.site.system, s.id}, Stance::None, 1.0e300);
    for (const Vessel& v : fleet_)
        if (&v != vessel_) add({Kind::Ship, v.nearestSystem, v.id}, Stance::None, 1.0e300);  // las naves de la flota se saben siempre
    for (const Anomaly& a : world_.anomalies) {
        // Lo frio solo existe para los sensores despues de un barrido.
        if (a.cold && !a.revealed) continue;
        add({Kind::Anomaly, a.system, a.id}, a.stance, a.revealed ? 1.0e300 : sensors);
    }

    std::sort(vessel_->contacts.begin(), vessel_->contacts.end(), [](const Contact& a, const Contact& b) { return a.distance < b.distance; });
    if (vessel_->contacts.size() > kMaxContacts) vessel_->contacts.resize(kMaxContacts);
}

// ---- El paso ----
// Step reparte el trabajo en tramos, segun de quien es cada cosa (docs/multijugador.md, fase 1.4). Hoy
// hay una nave y una burbuja; con varias, los tramos de nave se repetiran por cada una.
//   BeginBubble    el reloj: cuanto tiempo real, propio y exterior dura este paso
//   StepVessel     la nave obedece el mando y se mueve
//   AdvanceBubble  el mundo avanza: origen, orbitas, estaciones
//   SettleVessel   la nave se asienta en el mundo ya movido: marco, choques, avisos
//   (el trabajo de la nave y lo que se mueve en la burbuja: drones, barrido, anomalias, combate)
//   FinishVessel   calor, reparacion, sensores y, si no queda casco, la perdida
void GameSession::Step(double realSeconds, const ShipControls& controls, float focalPixels) {
    // El mando es el de la nave del foco; las demas siguen con lo que tengan ordenado.
    std::vector<ShipControls> all(fleet_.size());
    all[static_cast<std::size_t>(focus_)] = controls;
    StepAll(realSeconds, all, focalPixels);
}

void GameSession::StepAll(double realSeconds, const std::vector<ShipControls>& controls, float focalPixels) {
    const std::size_t count = fleet_.size();
    // Pase lo que pase, al salir la nave en curso vuelve a ser la del foco.
    struct Back {
        GameSession& s;
        ~Back() { s.Use(s.focus_); }
    } back{*this};
    std::vector<VesselStep> t(count);
    std::vector<StepClock> k(count);
    const auto use = [&](std::size_t i) { Use(static_cast<int>(i)); };

    // Modo cinematica: durante la toma de un impacto todo va mas despacio, pero solo si la estan
    // viendo todos los que comparten la burbuja. La toma dura en tiempo real.
    bool everyoneWatching = true;
    for (std::size_t i = 0; i < count; ++i) {
        use(i);
        pilot_->strikeAge += realSeconds;
        everyoneWatching = everyoneWatching && CinematicView();
    }
    if (everyoneWatching) realSeconds *= rules_.cinematic.timeFactor;

    // El reloj de la burbuja (docs/multijugador.md, 2.3): manda la nave que mas despacio quiere ir, en
    // el foco y en la dilatacion. Con una sola nave es el reloj de siempre.
    double focus = 1.0e300, exterior = 1.0e300;
    for (std::size_t i = 0; i < count; ++i) {
        use(i);
        if (vessel_->respawnPending) FinishRespawn();
        PrepareVessel(realSeconds, t[i]);
        focus = std::min(focus, vessel_->timeFocus);
        exterior = std::min(exterior, vessel_->exteriorFactor);
    }
    bubble_.timeFocus = focus;
    const double base = realSeconds * world_.timeScale * focus;
    const double seconds = base * exterior;
    bubble_.simTime += seconds;
    ++bubble_.steps;
    for (std::size_t i = 0; i < count; ++i) {
        k[i].real = realSeconds;
        // A bordo de la que dilata mas que la burbuja pasa menos tiempo: no acelera el mundo de las demas.
        k[i].proper = fleet_[i].exteriorFactor == exterior ? base : seconds / fleet_[i].exteriorFactor;
        k[i].seconds = seconds;
        k[i].hours = seconds / 3600.0;
    }
    UpdateLostPieces(seconds, realSeconds);

    for (std::size_t i = 0; i < count; ++i) {
        use(i);
        pilot_->dialogue.Update(*this, realSeconds);
        vessel_->approach.reset();
        // El casco que se pierda en este paso sin pasar por WoundHull se reparte al final entre todas las zonas.
        t[i].hullStart = vessel_->ship.hull;
        vessel_->directedLoss = 0.0;
        StepVessel(i < controls.size() ? controls[i] : ShipControls{}, k[i], t[i]);
    }

    AdvanceBubble(realSeconds, seconds, t, focalPixels);

    std::vector<char> alive(count, 0);
    bool anyone = false;
    for (std::size_t i = 0; i < count; ++i) {
        use(i);
        alive[i] = SettleVessel(k[i], t[i]) ? 1 : 0;  // perdida o recien perdida: para ella nada mas se mueve
        anyone = anyone || alive[i];
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!alive[i]) continue;
        use(i);
        UpdateExploration(k[i].hours);
        UpdateDrones(k[i].seconds, k[i].hours);
        UpdateScan(k[i].real);  // el barrido dura segundos reales: no se acelera con el tiempo del juego
    }
    // Lo que se mueve en la burbuja. Sin ninguna nave en vuelo, se queda quieto, como siempre.
    if (anyone) {
        Use(focus_);
        UpdateAnomalies(seconds);
        UpdateCombat(seconds);
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!alive[i]) continue;
        use(i);
        TrackStrike();
        FinishVessel(k[i], t[i]);
    }
}

// Lo que una nave pide al reloj antes de que avance: cuanto dilata, cuanto quiere frenar el tiempo, y
// donde esta la estacion junto a la que vuela.
void GameSession::PrepareVessel(double realSeconds, VesselStep& t) {
    // A bordo pasa el tiempo del jugador; fuera, mas deprisa cuanto mas rapido va y mas hondo esta
    // en un pozo gravitatorio.
    vessel_->exteriorFactor = ExteriorRate(vessel_->orbiting ? 0.0 : vessel_->ship.speed, GravityWell(), rules_.world.maxTimeFactor);
    UpdateFocus(realSeconds);
    // La estacion junto a la que vuela, donde estaba antes de que avance el reloj: una mina de un
    // cinturon se mueve con bubble_.simTime, y medirla despues dejaria a la nave atras en cada paso.
    t.anchorCandidate = vessel_->phase == GamePhase::Flying ? StationFrame() : -1;
    t.anchorStart = t.anchorCandidate >= 0 ? StationPosition(*world_.stations.Find(t.anchorCandidate)) : Vec3d{};
}

void GameSession::StepVessel(const ShipControls& controls, const StepClock& k, VesselStep& t) {
    const World& w = engine_.GetWorld();
    if (vessel_->phase == GamePhase::Exploding) {
        vessel_->wreckAge += k.real;
        if (vessel_->wreckAge >= kWreckSeconds) vessel_->phase = GamePhase::GameOver;
    }
    // Apagada, va a la deriva y gira despacio, tambien detras de la pantalla final.
    if (vessel_->phase == GamePhase::Adrift || (vessel_->phase == GamePhase::GameOver && ShutDown())) {
        vessel_->wreckAge += k.real;
        vessel_->wreckPosition += vessel_->driftVelocity * k.real;
        vessel_->ship.position = vessel_->wreckPosition;
        const double spin = Length(vessel_->ship.angularRate);
        if (spin > 0.0) vessel_->ship.orientation = (Quatd::FromAxisAngle(vessel_->ship.angularRate, spin * k.real) * vessel_->ship.orientation).Normalized();
        vessel_->ship.aim = vessel_->ship.orientation;
        if (vessel_->phase == GamePhase::Adrift && vessel_->wreckAge >= kShutdownSeconds) vessel_->phase = GamePhase::GameOver;
    }
    t.flying = vessel_->phase == GamePhase::Flying;
    t.before = vessel_->ship.position;
    t.frameBefore = vessel_->frame.position;
    t.hadFrame = vessel_->frame.body >= 0 || vessel_->frame.belt >= 0;
    // Junto a una estacion propia la nave vuela en el marco de la estacion (ver StationFrame).

    if (t.flying) {
        const bool steering = controls.turnYaw != 0.0f || controls.turnPitch != 0.0f || controls.turnRoll != 0.0f || controls.throttleChange != 0.0;
        if (vessel_->autopilot && steering) CancelAutopilot();
        if (pilot_->onFoot) StepFoot(controls.foot, k.real);
        else if (vessel_->orbiting && (vessel_->ship.throttle != 0.0 || controls.throttleChange != 0.0)) LeaveOrbit();

        Steer(controls, k.real);
        // Sin gas, los propulsores de maniobra callan y solo gira el giroscopio.
        const bool noGas = vessel_->ship.cargo.Get(Resource::Gas) <= 0.0;
        if (noGas && !vessel_->gasWarned) Say("Sin gas para los propulsores de maniobra: solo gira el giroscopio, mucho más despacio.");
        vessel_->gasWarned = noGas;
        StepDock(k.real);
        if (vessel_->tracking && (controls.turnYaw != 0.0f || controls.turnPitch != 0.0f || controls.turnRoll != 0.0f || vessel_->autopilot || !AimAtSolution(*vessel_->tracking, WeaponKind::Spinal))) vessel_->tracking.reset();
        StepHeading(vessel_->ship, rules_.ship, k.real);
        if (vessel_->orbiting) {
            if (!Resolve(*vessel_->orbiting)) {
                vessel_->orbiting.reset();
                vessel_->dockPhase = DockPhase::None;
                if (pilot_->onFoot) Board(true);
            } else {
                // Una orbita asistida es una circunferencia exacta: no decae ni gasta.
                const double angle = vessel_->orbitRate * k.seconds;
                const Vec3d side = Cross(vessel_->orbitNormal, vessel_->orbitOffset);
                vessel_->orbitOffset = vessel_->orbitOffset * std::cos(angle) + side * std::sin(angle);
                vessel_->ship.speed = vessel_->orbitRate * Length(vessel_->orbitOffset);
                vessel_->ship.moveDirection = Length(side) > 0.0 ? Normalize(side) : Heading(vessel_->ship);
                vessel_->ship.shipHours += k.proper / 3600.0;
                StepReactor(vessel_->ship, rules_, k.proper);
            }
        } else {
            Vec3d wanted;
            DriveInput order;
            if (vessel_->autopilot) order = FlyRoute(k.seconds, wanted);
            if (!vessel_->orbiting) {
                if (vessel_->exploring >= 0 && vessel_->ship.speed > 1.0) vessel_->exploring = -1;

                // Un paso largo cerca de un cuerpo se saldria de la orbita: se divide.
                int parts = 1;
                const double radius = std::max(Length(vessel_->ship.position - vessel_->frame.position), 1.0);
                if (vessel_->frame.mu > 0.0 && vessel_->ship.warpSpeed <= rules_.drive.impulseSpeed) {
                    const double period = 2.0 * kPi * std::sqrt(radius * radius * radius / vessel_->frame.mu);
                    parts = std::clamp(static_cast<int>(std::ceil(k.seconds / (period * 0.01))), 1, 64);
                }
                // Cerca de un planeta, la niebla no debe sacarla de orbita.
                order.dragScale = vessel_->frame.body >= 0 && !w.bodies[static_cast<std::size_t>(vessel_->frame.body)].isStar ? 0.0 : 1.0;
                const bool stable = Curves(vessel_->ship.mode) && vessel_->ship.warpCharge >= 1.0;
                // En el marco de una estacion la nave se mueve respecto a ella y sin la gravedad del
                // astro, que la estacion compensa: al soltarse no se queda atras ni cae.
                Vec3d matched;
                if (!vessel_->autopilot) t.anchor = t.anchorCandidate;
                if (t.anchor >= 0) {
                    const std::optional<TargetInfo> info = Resolve({Kind::Station, vessel_->nearestSystem, t.anchor});
                    t.anchorBefore = t.anchorStart;
                    matched = info->velocity - vessel_->frame.velocity;
                    vessel_->ship.velocity = vessel_->ship.velocity - matched;
                    order.dragScale = 0.0;
                    parts = 1;
                }
                for (int i = 0; i < parts; ++i) {
                    const Vec3d at = vessel_->ship.position - vessel_->frame.position;
                    const double d = std::max(Length(at), 1.0);
                    order.gravity = vessel_->frame.mu > 0.0 && t.anchor < 0 ? at * (-vessel_->frame.mu / (d * d * d)) : Vec3d{};
                    StepDrive(vessel_->ship, rules_, k.proper / parts, k.seconds / parts, k.real / parts, order);
                }
                vessel_->ship.velocity += matched;
                if (t.anchor >= 0) t.anchorOffset = vessel_->ship.position - t.anchorBefore;
                if (stable && vessel_->ship.antimatter <= 0.0) Say("Sin antimateria: la burbuja se colapsa.");
            }
        }
    }
}

void GameSession::AdvanceBubble(double realSeconds, double seconds, std::vector<VesselStep>& t, float focalPixels) {
    World& w = engine_.GetWorld();
    // Los planetas que alguna nave orbita no se descargan.
    bubble_.pins.clear();
    for (const Vessel& v : fleet_) {
        if (!v.orbiting || v.orbiting->kind != Kind::Planet) continue;
        if (const Body* body = FindPlanet(w, v.orbiting->system, v.orbiting->index)) bubble_.pins.push_back(static_cast<int>(body - w.bodies.data()));
    }
    // El origen del mundo y lo que el motor carga siguen a la nave del foco.
    Vessel& eye = fleet_[static_cast<std::size_t>(focus_)];
    Vec3d position = eye.ship.position;
    engine_.Recenter(position);
    const Vec3d shift = position - eye.ship.position;
    for (std::size_t i = 0; i < fleet_.size(); ++i) {
        Vessel& v = fleet_[i];
        t[i].shift = shift;
        v.ship.position += shift;
        v.wreckPosition += shift;
        for (MiningDrone& d : v.drones) d.position += shift;
    }
    eye.ship.position = position;  // exacta: es la que el motor ha dejado junto al origen
    for (Projectile& p : bubble_.projectiles) {
        p.position += shift;
        p.origin += shift;
    }
    for (ImpactFlash& f : bubble_.impacts) {
        f.position += shift;
        f.origin += shift;
        f.age += realSeconds;
    }
    bubble_.impacts.erase(std::remove_if(bubble_.impacts.begin(), bubble_.impacts.end(), [](const ImpactFlash& f) { return f.age > 0.6; }), bubble_.impacts.end());

    engine_.Advance(eye.ship.position, focalPixels, bubble_.simTime, bubble_.pins, realSeconds);

    for (Vessel& v : fleet_) v.nearestSystem = eps::NearestSystem(w, v.ship.position);
    // Por sello de tiempo: cada estacion produce hasta el reloj de esta burbuja.
    world_.stations.ProduceUntil(bubble_.simTime, bubble_.simTime - seconds);
    world_.stations.Age(realSeconds);
}

bool GameSession::SettleVessel(const StepClock& k, VesselStep& t) {
    // La nave viaja con el cuerpo que domina aqui: en su marco, este no se mueve.
    RefreshFrame(vessel_->frame);
    Vec3d carried = t.hadFrame && (vessel_->frame.body >= 0 || vessel_->frame.belt >= 0) ? vessel_->frame.position - (t.frameBefore + t.shift) : Vec3d{};
    if (t.anchor >= 0) {
        const Station* s = world_.stations.Find(t.anchor);
        if (s && StationVisible(*s)) {
            // La estacion lleva a la nave consigo, se mueva como se mueva.
            const Vec3d now = StationPosition(*s);
            carried = now - (t.anchorBefore + t.shift);
            vessel_->ship.position = now + t.anchorOffset - carried;  // se suma abajo
        }
    }
    // Los drones fuera vuelan en el mismo marco que la nave: van con el planeta o el cumulo.
    for (MiningDrone& d : vessel_->drones)
        if (d.state != DroneState::Docked) d.position += carried;
    if (!t.flying) {
        vessel_->wreckPosition += carried;  // los restos siguen al planeta, no se quedan atras
        if (bubble_.steps % kContactEvery == 1) UpdateContacts();
        else RefreshContacts();
        return false;
    }
    if (!vessel_->orbiting) vessel_->ship.position += carried;
    UpdateFrame();
    // Con la nave ya llevada por la estacion: antes, la distancia saldria contra donde va a estar.
    if (!vessel_->orbiting && !vessel_->autopilot) LimitNearStations(k.real);

    if (!vessel_->orbiting) {
        KeepOutOfBodies(t.before + t.shift + carried);
        CheckRocks(t.before + t.shift + carried);
        if (vessel_->phase == GamePhase::Flying && vessel_->frame.belt < 0 && vessel_->ship.warpSpeed <= rules_.drive.impulseSpeed) CheckFieldRocks(t.before + t.shift + carried);
        if (vessel_->phase == GamePhase::Flying) KeepOutOfStructures(t.before + t.shift + carried);
    }
    if (vessel_->phase != GamePhase::Flying) return false;
    LeashToFleet();
    vessel_->warning = FindWarning();

    vessel_->ship.energy = std::min(vessel_->ship.energy, rules_.ship.energy);
    return true;
}

void GameSession::FinishVessel(const StepClock& k, VesselStep& t) {
    // Donde ha atracado es donde reaparecera si pierde la nave.
    if (vessel_->dockPhase == DockPhase::Docked && vessel_->orbiting) vessel_->homeStation = vessel_->orbiting->index;
    // Orbitas y anomalias acaban de moverse: la nave va con su objetivo en este mismo paso.
    if (vessel_->orbiting) {
        if (const std::optional<TargetInfo> info = Resolve(*vessel_->orbiting)) vessel_->ship.position = info->position + vessel_->orbitOffset;
    }

    UpdateHeat(k.proper);
    UpdateThermal(k.proper, k.real);
    UpdateRepair(k.seconds);
    if (bubble_.steps % kContactEvery == 1) UpdateContacts();
    else RefreshContacts();
    SpreadWound(t.hullStart - vessel_->ship.hull - vessel_->directedLoss);  // calor, presion de ariete: sin direccion

    if (vessel_->creative) {
        vessel_->ship.zones = IntactHull();
        vessel_->hullHits.clear();
        vessel_->ship.hull = rules_.ship.hull;
        vessel_->ship.energy = rules_.ship.energy;
        vessel_->ship.antimatter = AntimatterCapacity(vessel_->ship, rules_.ship);
        for (const Resource r : {Resource::Metal, Resource::Silicate, Resource::Ice, Resource::Gas, Resource::StellarMatter})
            if (vessel_->ship.cargo.Get(r) < 2000.0) vessel_->ship.cargo.Add(r, 2000.0 - vessel_->ship.cargo.Get(r));
        // Depositos y placas llenos. El calor sigue contando, para poder probarlo.
        DefenseState& d = vessel_->ship.defense;
        d.water = rules_.thermal.waterTank;
        d.nitrogen = rules_.thermal.nitrogenTank;
        const std::size_t a = static_cast<std::size_t>(ShieldKind::Ablative);
        d.charge[a] = ShieldCapacity(rules_.defense, ShieldKind::Ablative, d.shieldTier[a]);
    }
    if (vessel_->ship.hull <= 0.0) LoseShip();
}

int GameSession::StepFixed(double realSeconds, const ShipControls& c, float focalPixels) {
    // Los giros son incrementos: se suman hasta que un paso los gaste. Lo demas es el ultimo valor.
    ShipControls& k = fixedControls_;
    k.turnYaw += c.turnYaw;
    k.turnPitch += c.turnPitch;
    k.turnRoll += c.turnRoll;
    k.foot.yaw += c.foot.yaw;
    k.foot.pitch += c.foot.pitch;
    k.foot.move = c.foot.move;
    k.view = c.view;
    k.throttleChange = c.throttleChange;
    k.manualBrake = k.manualBrake || c.manualBrake;

    // Con un tiron largo no se intenta recuperar todo: como mucho, ocho pasos de golpe.
    fixedPending_ = std::min(fixedPending_ + std::max(realSeconds, 0.0), kFixedStep * 8.0);
    int steps = 0;
    while (fixedPending_ >= kFixedStep) {
        fixedPending_ -= kFixedStep;
        Step(kFixedStep, k, focalPixels);
        ++steps;
        k.turnYaw = k.turnPitch = k.turnRoll = 0.0f;
        k.foot.yaw = k.foot.pitch = 0.0;
        k.manualBrake = false;
    }
    return steps;
}

CommandResult GameSession::ApplyTo(int vesselId, const ShipCommand& command) {
    const int index = IndexOfId(vesselId);
    if (index < 0) return {};
    const Using other(*this, index);
    return Apply(command);
}

CommandResult GameSession::Apply(const ShipCommand& c) {
    using T = CommandType;
    CommandResult r;
    // Una orden puede venir de fuera: lo que no es un valor de su enumeracion se rechaza.
    const auto within = [&](int count) { return c.value >= 0 && c.value < count; };
    switch (c.type) {
        case T::SetDrive: r.ok = within(4) && SetDrive(static_cast<DriveMode>(c.value)); break;
        case T::SetThrottle:
            SetThrottle(c.amount);
            r.ok = true;
            break;
        case T::HoldPosition:
            HoldPosition();
            r.ok = true;
            break;
        case T::Autopilot: r.ok = SetAutopilot(c.target); break;
        case T::CancelAutopilot:
            CancelAutopilot();
            r.ok = true;
            break;
        case T::Orbit: r.ok = EnterOrbitHere(); break;
        case T::DeployPower: r.ok = DeployPower(c.target); break;
        case T::DeployShipyard: r.ok = DeployShipyard(); break;
        case T::Interact: r.ok = Interact(c.target); break;
        case T::UpgradeStation: r.ok = c.index >= static_cast<int>(ModuleKind::Collector) && UpgradeStation(c.value, static_cast<ModuleKind>(c.index)); break;
        case T::DeployDrones: r.ok = DeployDrones(c.target); break;
        case T::RecallDrones:
            RecallDrones();
            r.ok = true;
            break;
        case T::ExtractElement:
            SetExtractElement(c.value);
            r.count = vessel_->extractZ;
            r.ok = true;
            break;
        case T::StartScan: r.ok = StartScan(); break;
        case T::CancelScan:
            CancelScan();
            r.ok = true;
            break;
        case T::Fire: r.ok = within(static_cast<int>(WeaponKind::Count)) && Fire(static_cast<WeaponKind>(c.value), c.target); break;
        case T::FireAll:
            r.count = FireAll(c.target);
            r.ok = r.count > 0;
            break;
        case T::FireModule: r.ok = FireModule(c.value, c.target, &r.why); break;
        case T::Aim: r.ok = within(static_cast<int>(WeaponKind::Count)) && AimAtSolution(c.target, static_cast<WeaponKind>(c.value)); break;
        case T::Track:
            TrackTarget(c.target);
            r.ok = vessel_->tracking.has_value();
            break;
        case T::StopTracking:
            StopTracking();
            r.ok = true;
            break;
        case T::SetShield: r.ok = within(static_cast<int>(ShieldKind::Count)) && SetShield(static_cast<ShieldKind>(c.value)); break;
        case T::ShieldOn:
            SetShieldOn(c.value != 0);
            r.ok = true;
            break;
        case T::CycleShield:
            r.count = static_cast<int>(CycleShield());
            r.ok = true;
            break;
        case T::Coolant:
            if (!within(static_cast<int>(CoolantKind::Count))) break;
            SetCoolant(static_cast<CoolantKind>(c.value), c.index != 0);
            r.ok = true;
            break;
        case T::Dock: r.ok = Dock(c.target, c.value != 0); break;
        case T::Undock:
            Undock();
            r.ok = true;
            break;
        case T::Disembark: r.ok = Disembark(); break;
        case T::Board: r.ok = Board(c.value != 0); break;
        case T::InstallModule:
            if (within(static_cast<int>(ShipModuleKind::Count))) r.ok = InstallModule(c.index, static_cast<ShipModuleKind>(c.value), &r.why);
            else r.why = "Tipo desconocido";
            break;
        case T::MoveModule: r.ok = MoveModule(c.value, c.index, &r.why); break;
        case T::ImpulseEngine: r.ok = SetImpulseEngine(c.value, &r.why); break;
        case T::Blueprint: r.ok = FabricateBlueprint(c.value, &r.why); break;
        case T::Missiles:
            r.count = ManufactureMissiles(c.value, c.index, &r.why);
            r.ok = r.count > 0;
            break;
        case T::PointDefense: r.ok = SetPointDefense(c.value, &r.why); break;
        case T::Continue:
            r.ok = vessel_->phase == GamePhase::GameOver;
            ContinueAfterDefeat();
            break;
        case T::NextDialogue:
            pilot_->dialogue.Next();
            r.ok = true;
            break;
        case T::ReplayTip:
            pilot_->dialogue.Replay();
            r.ok = true;
            break;
        case T::AnalysisUsed:
            MarkAnalysisUsed();
            r.ok = true;
            break;
        case T::CursorUsed:
            MarkCursorUsed();
            r.ok = true;
            break;
        case T::Cinematic:
            SetCinematic(c.value != 0);
            r.ok = true;
            break;
        case T::Count: break;
    }
    return r;
}

}  // namespace game
