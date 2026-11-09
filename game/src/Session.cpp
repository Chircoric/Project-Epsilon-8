#include "game/Session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "epsilon/AstroLayers.h"
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
    : engine_(engine), rules_(rules), seed_(seed), mode_(mode), ship_(MakeShip(rules)), stations_(rules.stations), rng_(seed), timeScale_(rules.world.timeScale) {
    World& w = engine_.GetWorld();
    anomalies_ = GenerateAnomalies(w.extents, rules_.anomalies, seed);
    w.scene.UpdateWorld(engine_.Jobs());
    known_.assign(w.state.size(), 0);
    known_[0] = 1;
    visited_ = known_;
    PlaceAtStart();
    dialogue_.Start(MakeTutorial());
    SaveCheckpoint("");
}

// A medio camino de la primera estrella, dentro de su sistema.
void GameSession::PlaceAtStart() {
    const World& w = engine_.GetWorld();
    const Vec3d center = w.scene.WorldPosition(w.sectors[0]);
    ship_.position = center + Vec3d{w.extents[0] * 0.25, 0.0, -w.extents[0] * 0.15};
    const Vec3d toStar = Normalize(center - ship_.position);
    FaceTo(ship_, toStar);
    ship_.moveDirection = toStar;
    ship_.warpHeading = toStar;
}

void GameSession::Say(const std::string& text) {
    log_.push_back(text);
    while (log_.size() > kMaxLog) log_.pop_front();
}

double GameSession::Visibility() const {
    const World& w = engine_.GetWorld();
    return rules_.world.fogDistance / NebulaDensity(ship_.position + w.origin);
}

// Grandes zonas de un color a otro. Cambia despacio: hay que viajar para notarlo.
float GameSession::NebulaTone() const {
    const Vec3d p = (ship_.position + engine_.GetWorld().origin) * (1.0 / 1.0e11);
    return static_cast<float>(std::clamp(0.5 + 0.36 * std::sin(p.x * 1.1 + 0.4) * std::cos(p.z * 0.8 - 0.6) + 0.14 * std::sin(p.y * 1.4 + p.z * 0.5), 0.0, 1.0));
}

bool GameSession::KnownSystem(int system) const { return system >= 0 && static_cast<std::size_t>(system) < known_.size() && known_[static_cast<std::size_t>(system)]; }

int GameSession::KnownSystems() const {
    int count = 0;
    for (const char k : known_) count += k ? 1 : 0;
    return count;
}

bool GameSession::Visited(int system) const { return system >= 0 && static_cast<std::size_t>(system) < visited_.size() && visited_[static_cast<std::size_t>(system)]; }

int GameSession::VisitedSystems() const {
    int count = 0;
    for (const char v : visited_) count += v ? 1 : 0;
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
    for (const Anomaly& a : anomalies_) {
        if (a.system != system || !(a.discovered || a.revealed)) continue;
        AtlasRow row;
        row.target = {Kind::Anomaly, system, a.id};
        row.known = true;
        row.name = a.name;
        row.detail = a.kind == AnomalyKind::Derelict ? (a.emitter ? "Con emisor" : "Vacía") : Name(a.stance);
        rows.push_back(row);
    }
    for (const Station& s : stations_.All()) {
        if (s.site.system != system) continue;
        AtlasRow row;
        row.target = {Kind::Station, system, s.id};
        row.known = true;
        row.name = s.name;
        row.detail = s.kind == StationKind::Mining ? "Mina" : "Central";
        rows.push_back(row);
    }
    return rows;
}

bool GameSession::SetDrive(DriveMode mode) {
    if (!DriveAvailable(ship_, mode, rules_.drive)) {
        Say(mode == DriveMode::Warp ? "Sin energía negativa: el emisor está en una estructura abandonada."
                                    : "El hiper pide otro emisor de energía negativa, de otra estructura.");
        return false;
    }
    if (Curves(mode) && !CanCurve(ship_, mode, rules_.drive)) {
        Say("Antimateria insuficiente: formar la burbuja pide " + MassText(FormationCost(ship_, mode, rules_.drive)) + ".");
        return false;
    }
    ship_.mode = mode;
    return true;
}

void GameSession::SetThrottle(double value) { ship_.throttle = std::clamp(value, -1.0, 1.0); }

bool GameSession::SetAutopilot(const TargetRef& target) {
    if (!Resolve(target)) {
        Say("Sin datos de ese objetivo: acérquese más.");
        return false;
    }
    if (orbiting_ && *orbiting_ == target) return true;  // ya esta alli
    autopilot_ = target;
    orbiting_.reset();
    dockPhase_ = DockPhase::None;
    exploring_ = -1;
    fuelWarned_ = false;
    CancelScan();
    return true;
}

void GameSession::CancelAutopilot() { autopilot_.reset(); }

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
            if (static_cast<std::size_t>(t.index) < state.starBodies.size()) info.velocity = BodyVelocity(w, w.bodies[static_cast<std::size_t>(state.starBodies[static_cast<std::size_t>(t.index)])], simTime_);  // una estrella doble se mueve
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
            info.velocity = BodyVelocity(w, *body, simTime_);
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
            if (!AsteroidLayer::NearestFamily(w, ship_.position, simTime_, near, slot)) return std::nullopt;
            info.position = near;
            info.radius = 220.0;
            info.arrival = 2000.0;
            info.name = w.beltInfo[static_cast<std::size_t>(slot)].name;
            info.detail = std::string("Campo ") + ProfileInfo(belt->profile).name;
            return info;
        }
        case Kind::Station: {
            const Station* s = stations_.Find(t.index);
            if (!s) return std::nullopt;
            if (!StationVisible(*s)) return std::nullopt;
            info.position = StationPosition(*s);
            // Va con lo que orbita: sin esto, el piloto automatico nunca la alcanzaria.
            if (s->site.star >= 0) {
                info.velocity = Vec3d{-std::sin(s->angle), 0.0, std::cos(s->angle)} * (rules_.stations.orbitRate / 3600.0 * s->yield.orbitRadius);
            } else if (s->site.body >= 0) {
                if (const Body* body = FindPlanet(w, s->site.system, s->site.body)) info.velocity = BodyVelocity(w, *body, simTime_);
            } else if (s->site.belt >= 0) {
                if (const AsteroidBelt* belt = FindBelt(w, s->site.system, s->site.belt)) {
                    const std::size_t family = static_cast<std::size_t>(s->site.family) % belt->families.size();
                    info.velocity = w.scene.World(belt->parent).TransformDirection(OrbitVelocity(belt->families[family].orbit, simTime_));
                }
            }
            info.radius = StationRadius(s->kind);
            info.arrival = 8.0;
            info.name = s->name;
            info.detail = s->kind == StationKind::Mining ? "Minería" : "Antimateria";
            return info;
        }
        case Kind::Anomaly: {
            for (const Anomaly& a : anomalies_) {
                if (a.id != t.index) continue;
                info.position = AnomalyPosition(a);
                info.velocity = a.velocity;
                info.radius = a.radius;
                info.arrival = a.radius * 3.0;
                info.name = a.name;
                info.detail = a.kind == AnomalyKind::Derelict ? (a.emitter ? "Emisor de energía negativa" : "Vacía") : Name(a.stance);
                if (a.maxHull > 0.0 && a.hull < a.maxHull) info.detail += ", casco " + Fixed(100.0 * a.hull / a.maxHull, 0) + " %";
                return info;
            }
            return std::nullopt;
        }
        default: return std::nullopt;
    }
}

bool GameSession::InRange(const TargetRef& target, double range) const {
    const std::optional<TargetInfo> info = Resolve(target);
    return info && Length(info->position - ship_.position) <= range + info->arrival;
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
    const Vec3d around{std::cos(s.angle), 0.05, std::sin(s.angle)};
    if (s.site.star >= 0) return w.scene.WorldPosition(state.starNodes[static_cast<std::size_t>(s.site.star)]) + around * s.yield.orbitRadius;
    if (s.site.body >= 0) {
        const Body* body = FindPlanet(w, s.site.system, s.site.body);
        if (body) return w.scene.WorldPosition(body->node) + around * (body->radius * 1.35 + 200.0);
    } else if (s.site.belt >= 0) {
        if (const AsteroidBelt* belt = FindBelt(w, s.site.system, s.site.belt)) {
            const std::size_t family = static_cast<std::size_t>(s.site.family) % belt->families.size();
            return FamilyPosition(w.scene, *belt, belt->families[family], simTime_) + around * 40.0;
        }
    }
    return w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(s.site.system)]);
}

Quatd GameSession::StationRotation(const Station& s) const {
    if (s.kind != StationKind::Power || s.site.star < 0) return {};
    const World& w = engine_.GetWorld();
    const SystemState& state = w.state[static_cast<std::size_t>(s.site.system)];
    if (static_cast<std::size_t>(s.site.star) >= state.starNodes.size()) return {};
    return TurnedToward({}, w.scene.WorldPosition(state.starNodes[static_cast<std::size_t>(s.site.star)]) - StationPosition(s));
}

// Lo que miden los modelos (GameModels.cpp): la mina, patas y taladros; la central, su corola.
double GameSession::StationRadius(StationKind kind) { return kind == StationKind::Mining ? 0.32 : 0.45; }

// Las escotillas, en metros del modelo (arriba es -Y). La mina la lleva en el costado del nucleo,
// entre dos brazos; la central, encima del deposito, lejos de los colectores.
GameSession::DockPort GameSession::Port(const Station& s) const {
    const Mat4d turn = StationRotation(s).ToMatrix();
    Vec3d at, out;
    if (s.kind == StationKind::Mining) {
        out = {0.5, 0.0, 0.8660254};
        at = Vec3d{53.5, -20.0, 92.66} * 0.001;
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

// Atracada, la nave se pone de costado frente a la escotilla: su compuerta de estribor (ShipDesign::
// dockPort) mira a la estacion, la proa queda de lado, y el ducto origami cubre lo que las separa.
Quatd GameSession::DockAttitude(const Station& s) const {
    const DockPort port = Port(s);
    const Vec3d side = port.normal * -1.0;  // el +X de la nave apunta a la escotilla
    Vec3d forward = Cross(port.normal, Vec3d{0, -1, 0});
    if (Length(forward) < 1e-6) forward = Cross(port.normal, Vec3d{1, 0, 0});
    forward = Normalize(forward);
    return FromAxes(side, Cross(forward, side), forward);
}

Vec3d GameSession::DockGoal(const Station& s) const {
    const DockPort port = Port(s);
    const Vec3d hatchSide = DockAttitude(s).ToMatrix().TransformDirection(CurrentShipDesign().dockPort * 0.001);
    return port.position + port.normal * kDockTube - hatchSide - StationPosition(s);
}

GameSession::DockTube GameSession::Tube() const {
    DockTube t;
    const Station* s = DockedStation();
    if (!s || dockPhase_ != DockPhase::Docked || !StationVisible(*s)) return t;
    const DockPort port = Port(*s);
    t.active = true;
    t.stationEnd = port.position;
    t.shipEnd = ship_.position + ship_.orientation.ToMatrix().TransformDirection(CurrentShipDesign().dockPort * 0.001);
    t.extension = std::clamp(dockedAge_ / kDockTubeSeconds, 0.0, 1.0);
    return t;
}

bool GameSession::DeployMining(const TargetRef& target) {
    World& w = engine_.GetWorld();
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info || (target.kind != Kind::Planet && target.kind != Kind::Belt)) {
        Say("La minería se despliega en planetas y campos de asteroides.");
        return false;
    }
    if (!Scanned(target)) {
        Say("Sin datos de este astro: analícelo con el escáner.");
        return false;
    }
    if (!InRange(target, rules_.stations.deployRange)) {
        Say("Demasiado lejos para desplegar.");
        return false;
    }

    StationSite site;
    site.system = target.system;
    Deposit deposit;
    if (target.kind == Kind::Planet) {
        const Body* body = FindPlanet(w, target.system, target.index);
        const BodyData* data = body ? BodyInfo(w, *body) : nullptr;
        if (!data || !DepositOf(*data, deposit)) {
            Say("Aquí no hay nada que extraer.");
            return false;
        }
        site.body = target.index;
    } else {
        int slot = -1;
        const AsteroidBelt* belt = FindBelt(w, target.system, target.index, &slot);
        if (!belt) return false;
        deposit = DepositOf(belt->profile);
        site.belt = target.index;
        double nearest = 1.0e300;
        for (std::size_t f = 0; f < belt->families.size(); ++f) {
            const double d = Length(FamilyPosition(w.scene, *belt, belt->families[f], simTime_) - ship_.position);
            if (d < nearest) {
                nearest = d;
                site.family = static_cast<int>(f);
            }
        }
    }

    const bool free = stations_.NextIsFree(StationKind::Mining);
    std::string error;
    const int id = stations_.Deploy(StationKind::Mining, site, "Mina " + std::to_string(stations_.All().size() + 1) + " en " + info->name, deposit, {}, ship_.cargo, &error);
    if (id < 0) {
        Say(error);
        return false;
    }
    // Sale de la nave: empieza en el lado de la orbita donde esta ella.
    const Vec3d fromTarget = ship_.position - info->position;
    stations_.Find(id)->angle = std::atan2(fromTarget.z, fromTarget.x);
    Say(std::string(free ? "Mina desplegada sin coste: " : "Mina desplegada: ") + info->name);
    SaveCheckpoint("Punto de control");
    return true;
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
    if (ship_.heatShieldTier < yield.requiredTier) {
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
    const bool free = stations_.NextIsFree(StationKind::Power);
    std::string error;
    const int id = stations_.Deploy(StationKind::Power, site, "Central " + std::to_string(stations_.All().size() + 1) + " en " + info->name, {}, yield, ship_.cargo, &error);
    if (id < 0) {
        Say(error);
        return false;
    }
    const Vec3d fromStar = ship_.position - info->position;
    stations_.Find(id)->angle = std::atan2(fromStar.z, fromStar.x);
    Say(std::string(free ? "Central desplegada sin coste: " : "Central desplegada: ") + PowerText(yield.energyPerHour) + " para " + MassRateText(stations_.AntimatterFrom(yield.energyPerHour)) + " de antimateria.");
    SaveCheckpoint("Punto de control");
    return true;
}

bool GameSession::Interact(const TargetRef& target) {
    if (target.kind == Kind::Station) {
        Station* s = stations_.Find(target.index);
        // Sin enlace hay que ir en persona.
        if (!s || (ship_.relayTier <= 0 && !InRange(target, rules_.ship.interactRange))) {
            Say("Hay que estar junto a la estación para recoger.");
            return false;
        }
        const double antimatterBefore = ship_.antimatter, cargoBefore = ship_.cargo.Total();
        stations_.Collect(target.index, ship_.cargo, CargoCapacity(ship_, rules_.ship), ship_.antimatter, AntimatterCapacity(ship_, rules_.ship));
        ++collected_;
        Say("Recogido: " + MassText(ship_.cargo.Total() - cargoBefore) + " de carga y " + MassText(ship_.antimatter - antimatterBefore) + " de antimateria.");
        SaveCheckpoint("Punto de control");
        return true;
    }
    if (target.kind == Kind::Anomaly) {
        for (Anomaly& a : anomalies_) {
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
            exploring_ = a.id;
            Say("Explorando " + a.name + ".");
            return true;
        }
    }
    Say("Nada con lo que interactuar.");
    return false;
}

bool GameSession::UpgradeStation(int id, ModuleKind module) {
    std::string error;
    if (!stations_.AddOrUpgradeModule(id, module, ship_.cargo, &error)) {
        Say(error);
        return false;
    }
    Say(std::string(Name(module)) + " instalado.");
    return true;
}

double GameSession::ScanRange() const {
    // Cada antena de sensores montada suma alcance.
    return rules_.scanner.range * (1.0 + rules_.scanner.rangePerTier * (ship_.scannerTier - 1)) * (1.0 + kSensorBoost * WorkingModules(ship_, ShipModuleKind::SensorArray));
}

bool GameSession::StartScan() {
    if (Scanning()) return false;
    if (!CanScan()) {
        Say("El barrido exige la nave quieta: pulse Espacio o entre en órbita.");
        return false;
    }
    if (ship_.energy < rules_.scanner.energy) {
        Say("Energía insuficiente para el barrido.");
        return false;
    }
    ship_.energy -= rules_.scanner.energy;
    scanProgress_ = 0.0;
    Say("Barrido de microondas y ultravioleta en curso.");
    return true;
}

void GameSession::CancelScan() { scanProgress_ = -1.0; }

// En una orbita asistida la nave no enciende nada: para el escaner, es tan estable como estar quieta.
bool GameSession::CanScan() const { return !autopilot_ && ship_.throttle == 0.0 && (orbiting_ || ship_.speed <= 1.0); }

bool GameSession::Scanned(const TargetRef& t) const {
    if (t.kind != Kind::Star && t.kind != Kind::Planet && t.kind != Kind::Belt) return true;
    return scanned_.count(ScanKey(t)) != 0;
}

void GameSession::MarkScanned(int system) {
    const StarSystemData& data = *engine_.GetWorld().systems[static_cast<std::size_t>(system)];
    for (int m = 0; m < data.StarCount(); ++m) scanned_.insert(ScanKey({Kind::Star, system, m}));
    for (std::size_t b = 0; b < data.bodies.size(); ++b) scanned_.insert(ScanKey({Kind::Planet, system, static_cast<int>(b)}));
    for (std::size_t b = 0; b < data.belts.size(); ++b) scanned_.insert(ScanKey({Kind::Belt, system, static_cast<int>(b)}));
}

void GameSession::MarkKnown(int system) {
    if (system < 0 || static_cast<std::size_t>(system) >= known_.size()) return;
    known_[static_cast<std::size_t>(system)] = 1;
    MarkScanned(system);
}

void GameSession::SetCreative(bool on) {
    creative_ = on;
    if (!on) {
        Say("Modo creativo desactivado.");
        return;
    }
    ship_.emitters = std::max(ship_.emitters, 8);
    ship_.scannerTier = std::max(ship_.scannerTier, 5);
    ship_.heatShieldTier = std::max(ship_.heatShieldTier, 5);
    ship_.handlingTier = std::max(ship_.handlingTier, 3);
    ship_.relayTier = std::max(ship_.relayTier, 1);
    ship_.computerTier = std::max(ship_.computerTier, rules_.combat.maxTier);
    for (WeaponMount& m : ship_.weapons) m.tier = std::max(m.tier, rules_.combat.maxTier);
    DefenseState& d = ship_.defense;
    for (int& t : d.shieldTier) t = std::max(t, rules_.combat.maxTier);  // el Amor incluido, para probarlo
    for (int& t : d.coolantTier) t = std::max(t, rules_.combat.maxTier);
    for (int k = 0; k < static_cast<int>(ShieldKind::Count); ++k) d.charge[static_cast<std::size_t>(k)] = ShieldCapacity(rules_.defense, static_cast<ShieldKind>(k), d.shieldTier[static_cast<std::size_t>(k)]);
    for (std::size_t s = 0; s < known_.size(); ++s) MarkKnown(static_cast<int>(s));
    Say("Modo creativo: nave invulnerable, todo sin fin y todos los sistemas localizados.");
}

int GameSession::SpawnAnomaly(AnomalyKind kind, Stance stance, double km) {
    const World& w = engine_.GetWorld();
    const int system = nearestSystem_;
    if (system < 0 || static_cast<std::size_t>(system) >= w.sectors.size()) return -1;
    Anomaly a;
    for (const Anomaly& o : anomalies_) a.id = std::max(a.id, o.id + 1);
    a.kind = kind;
    a.stance = kind == AnomalyKind::Derelict ? Stance::None : stance;
    a.system = system;
    a.offset = ship_.position + Heading(ship_) * std::max(km, 1.0) - w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(system)]);
    a.home = a.offset;
    a.radius = kind == AnomalyKind::Derelict ? 12.0 : kind == AnomalyKind::AlienStation ? 2.5 : (stance == Stance::Hostile ? 0.5 : 0.7);
    a.emitter = kind == AnomalyKind::Derelict;
    a.cold = kind == AnomalyKind::Derelict;
    a.discovered = a.revealed = true;
    a.name = std::string(kind == AnomalyKind::Derelict ? "Estructura abandonada" : kind == AnomalyKind::AlienStation ? (stance == Stance::Hostile ? "Bastion hostil" : "Estacion neutral")
                                                                                                                  : (stance == Stance::Hostile ? "Nave hostil" : "Nave neutral")) + " " + std::to_string(a.id);
    InitCombat(a, rules_.anomalies);
    anomalies_.push_back(a);
    UpdateContacts();
    return a.id;
}

// Lo que alcanza el barrido en este sistema: estrellas, planetas y campos.
void GameSession::ScanBodies(double range, int& found) {
    const World& w = engine_.GetWorld();
    const int system = nearestSystem_;
    const StarSystemData& data = *w.systems[static_cast<std::size_t>(system)];
    const auto consider = [&](const TargetRef& t) {
        if (Scanned(t)) return;
        const std::optional<TargetInfo> info = Resolve(t);
        if (!info || Length(info->position - ship_.position) > range) return;
        scanned_.insert(ScanKey(t));
        ++found;
    };
    for (int m = 0; m < data.StarCount(); ++m) consider({Kind::Star, system, m});
    for (std::size_t b = 0; b < data.bodies.size(); ++b) {
        const BodyData& body = data.bodies[b];
        if (!body.destroyed && body.kind != PlanetClass::Comet && body.kind != PlanetClass::Asteroid) consider({Kind::Planet, system, static_cast<int>(b)});
    }
    for (std::size_t b = 0; b < data.belts.size(); ++b) consider({Kind::Belt, system, static_cast<int>(b)});
}

void GameSession::UpdateScan(double hours) {
    if (!Scanning()) return;
    if (!CanScan()) {
        Say("Barrido perdido: la nave se ha movido.");
        CancelScan();
        return;
    }
    scanProgress_ += hours / rules_.scanner.hours;
    if (scanProgress_ < 1.0) return;
    CancelScan();

    const double range = ScanRange();
    World& w = engine_.GetWorld();
    const double systemRange = rules_.scanner.systemRange * (1.0 + rules_.scanner.rangePerTier * (ship_.scannerTier - 1));
    int systems = 0;
    for (std::size_t s = 0; s < known_.size(); ++s) {
        if (known_[s] || Length(w.scene.WorldPosition(w.sectors[s]) - ship_.position) > systemRange) continue;
        known_[s] = 1;
        ++systems;
    }
    int bodies = 0;
    ScanBodies(rules_.scanner.bodyRange * (1.0 + rules_.scanner.rangePerTier * (ship_.scannerTier - 1)), bodies);
    int found = 0, weak = 0, strongest = 0;
    for (Anomaly& a : anomalies_) {
        if (Length(AnomalyPosition(a) - ship_.position) > range) continue;
        if (a.signature > ship_.scannerTier) {
            ++weak;
            strongest = std::max(strongest, a.signature);
        } else if (!a.revealed) {
            a.revealed = true;
            ++found;
        }
    }
    lastScan_ = found > 0 ? "Barrido completo: " + std::to_string(found) + " anomalías nuevas." : "Barrido completo: nada nuevo en este radio.";
    if (weak > 0) lastScan_ += " Señal débil: pide escáner de nivel " + std::to_string(strongest) + ".";
    if (bodies > 0) lastScan_ += " Astros analizados: " + std::to_string(bodies) + ".";
    if (systems > 0) lastScan_ += " Sistemas localizados: " + std::to_string(systems) + ".";
    Say(lastScan_);
    if (found + systems + bodies > 0) saveWanted_ = true;
    steps_ = 0;  // que aparezca en sensores ya
}

int GameSession::RevealedStructures() const {
    int count = 0;
    for (const Anomaly& a : anomalies_) count += a.kind == AnomalyKind::Derelict && a.revealed ? 1 : 0;
    return count;
}

void GameSession::SaveCheckpoint(const std::string& reason) {
    if (phase_ != GamePhase::Flying) return;
    Checkpoint c{ship_, ship_.position + engine_.GetWorld().origin, stations_, anomalies_, simTime_, collected_, 0, known_, scanned_};
    checkpoint_ = std::move(c);
    if (reason.empty()) return;
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
    d.seed = seed_;
    d.mode = mode_;
    d.visitedSystems = visited_;  // lo visitado no se olvida al volver a un punto de control
    d.tutorialStep = dialogue_.Active() ? dialogue_.Step() : 1000000;  // terminado o apagado
    d.cursorUsed = cursorUsed_;
    if (phase_ == GamePhase::Flying || !checkpoint_) {
        d.ship = ship_;
        d.absolutePosition = ship_.position + engine_.GetWorld().origin;
        d.stations = stations_.All();
        d.nextStation = stations_.NextId();
        d.anomalies = anomalies_;
        d.knownSystems = known_;
        d.scanned.assign(scanned_.begin(), scanned_.end());
        d.simTime = simTime_;
        d.collected = collected_;
        if (orbiting_) {
            d.orbitKind = static_cast<int>(orbiting_->kind);
            d.orbitSystem = orbiting_->system;
            d.orbitIndex = orbiting_->index;
            d.orbitOffset = orbitOffset_;
            d.orbitNormal = orbitNormal_;
            d.orbitRate = orbitRate_;
            d.docked = dockPhase_ == DockPhase::Docked;  // a pie tambien: al cargar se empieza a bordo
        }
    } else {
        d.ship = checkpoint_->ship;
        d.absolutePosition = checkpoint_->absolutePosition;
        d.stations = checkpoint_->stations.All();
        d.nextStation = checkpoint_->stations.NextId();
        d.anomalies = checkpoint_->anomalies;
        d.knownSystems = checkpoint_->known;
        d.scanned.assign(checkpoint_->scanned.begin(), checkpoint_->scanned.end());
        d.simTime = checkpoint_->simTime;
        d.collected = checkpoint_->collected;
    }
    return d;
}

bool GameSession::Restore(const SaveData& d, std::string* error) {
    if (d.seed != seed_) {
        if (error) *error = "la partida es de otra galaxia";
        return false;
    }
    const World& w = engine_.GetWorld();
    mode_ = d.mode;
    ship_ = d.ship;
    lostPieces_.clear();
    hullHits_.clear();
    failedZone_ = -1;
    zoneHeat_ = {};
    ship_.position = d.absolutePosition - w.origin;
    ship_.aim = ship_.orientation;
    ship_.angularRate = {};
    if (ship_.weapons.empty()) ship_.weapons = DefaultWeapons(rules_.ship);
    RefitToDesign();  // modulos y piezas a la nave cargada; sus torretas salen de los modulos
    projectiles_.clear();
    tracking_.reset();
    stations_.Restore(d.stations, d.nextStation);
    anomalies_ = d.anomalies;
    for (Anomaly& a : anomalies_) {
        if (a.kind != AnomalyKind::Derelict && a.maxHull <= 0.0) InitCombat(a, rules_.anomalies);  // partida anterior al combate
        else if (a.kind != AnomalyKind::Derelict && a.shieldMax <= 0.0) InitShield(a, rules_.anomalies);  // anterior a los escudos
    }
    simTime_ = d.simTime;
    collected_ = d.collected;
    known_ = d.knownSystems;
    known_.resize(w.state.size(), 0);
    known_[0] = 1;
    visited_ = d.visitedSystems;
    visited_.resize(w.state.size(), 0);
    visited_[0] = 1;
    scanned_ = std::set<std::uint64_t>(d.scanned.begin(), d.scanned.end());
    cursorUsed_ = d.cursorUsed;
    dialogue_.Resume(d.tutorialStep);

    phase_ = GamePhase::Flying;
    wreckAge_ = 0;
    autopilot_.reset();
    orbiting_.reset();
    exploring_ = -1;
    frame_ = {};
    CancelScan();
    if (d.orbitKind != 0) {
        TargetRef orbit;
        orbit.kind = static_cast<Kind>(d.orbitKind);
        orbit.system = d.orbitSystem;
        orbit.index = d.orbitIndex;
        orbiting_ = orbit;
        orbitOffset_ = d.orbitOffset;
        orbitNormal_ = d.orbitNormal;
        orbitRate_ = d.orbitRate;
    }
    onFoot_ = false;
    dockWanted_ = -1;
    dockPhase_ = d.docked && orbiting_ && orbiting_->kind == Kind::Station ? DockPhase::Docked : DockPhase::None;
    log_.clear();
    steps_ = 0;
    SaveCheckpoint("");
    return true;
}

void GameSession::ContinueAfterDefeat() {
    if (phase_ != GamePhase::GameOver) return;
    phase_ = GamePhase::Flying;
    wreckAge_ = 0;
    const GameMode& mode = ModeInfo(mode_);
    if (mode.permadeath || !checkpoint_) {
        ResetCampaign();
        return;
    }

    const Checkpoint& c = *checkpoint_;
    ship_ = c.ship;
    ship_.position = c.absolutePosition - engine_.GetWorld().origin;
    ship_.speed = 0;
    ship_.velocity = {};
    ship_.warpSpeed = 0;
    ship_.holding = true;
    ship_.throttle = 0;
    ship_.warpCharge = 0;
    ship_.angularRate = {};
    frame_ = {};
    ship_.aim = ship_.orientation;
    ship_.hull = rules_.ship.hull;
    ship_.zones = IntactHull();
    lostPieces_.clear();
    hullHits_.clear();
    failedZone_ = -1;
    stations_ = c.stations;
    anomalies_ = c.anomalies;
    projectiles_.clear();
    collected_ = c.collected;
    simTime_ = c.simTime;
    known_ = c.known;
    scanned_ = c.scanned;

    // Los emisores ya estan en el motor; la bodega es lo que se pierde.
    if (mode.cargoLoss > 0.0) {
        for (const Resource r : {Resource::Metal, Resource::Silicate, Resource::Ice, Resource::Gas, Resource::StellarMatter}) ship_.cargo.Add(r, -ship_.cargo.Get(r) * mode.cargoLoss);
    }
    autopilot_.reset();
    orbiting_.reset();
    dockPhase_ = DockPhase::None;
    onFoot_ = false;
    exploring_ = -1;
    Say("De vuelta en el último punto de control.");
}

// Modo realista: no queda nada. Nave nueva, sin estaciones, y las anomalias vuelven a su sitio.
void GameSession::ResetCampaign() {
    const World& w = engine_.GetWorld();
    ship_ = MakeShip(rules_);
    lostPieces_.clear();
    hullHits_.clear();
    failedZone_ = -1;
    zoneHeat_ = {};
    stations_ = StationNetwork(rules_.stations);
    anomalies_ = GenerateAnomalies(w.extents, rules_.anomalies, seed_);
    projectiles_.clear();
    kills_ = 0;
    collected_ = 0;
    known_.assign(known_.size(), 0);
    known_[0] = 1;
    visited_ = known_;
    scanned_.clear();
    PlaceAtStart();
    autopilot_.reset();
    orbiting_.reset();
    dockPhase_ = DockPhase::None;
    onFoot_ = false;
    exploring_ = -1;
    checkpoint_.reset();
    SaveCheckpoint("");
    Say("Otra tripulación, otro intento.");
}

void GameSession::LoseShip() {
    phase_ = GamePhase::Exploding;
    wreckAge_ = 0;
    wreckPosition_ = ship_.position;
    autopilot_.reset();
    tracking_.reset();
    orbiting_.reset();
    dockPhase_ = DockPhase::None;
    dockWanted_ = -1;
    onFoot_ = false;
    exploring_ = -1;
    CancelScan();
    ship_.speed = 0;
    ship_.velocity = {};
    ship_.warpSpeed = 0;
    ship_.throttle = 0;
    Say(lossReason_.empty() ? "Casco destruido." : lossReason_);
    if (ModeInfo(mode_).permadeath) saveLost_ = true;
}

bool GameSession::JumpTo(const TargetRef& target, double distance) {
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info) return false;
    const Vec3d away = Length(ship_.position - info->position) > 1.0 ? Normalize(ship_.position - info->position) : Vec3d{0, 0, -1};
    ship_.position = info->position + away * distance;
    autopilot_.reset();
    dockPhase_ = DockPhase::None;
    const Vec3d look = away * -1.0;
    FaceTo(ship_, look);
    ship_.moveDirection = look;
    ship_.warpHeading = look;
    EnterOrbit(target, *info, distance);
    return true;
}

void GameSession::Steer(const ShipControls& c, double realSeconds) {
    if (!autopilot_ && (c.turnYaw != 0.0f || c.turnPitch != 0.0f || c.turnRoll != 0.0f)) {
        // Sobre los ejes de la camara: lo que se ve a la derecha queda a la derecha, sin importar
        // desde donde se mire la nave ni como este girada.
        const Quatd turn = Quatd::FromAxisAngle({0, 1, 0}, static_cast<double>(c.turnYaw)) * Quatd::FromAxisAngle({1, 0, 0}, -static_cast<double>(c.turnPitch)) * Quatd::FromAxisAngle({0, 0, 1}, -static_cast<double>(c.turnRoll));
        ship_.aim = (ship_.aim * c.view * turn * c.view.Conjugate()).Normalized();
        LimitAim(ship_, rules_.ship.aimLead);
    }
    ship_.throttle = std::clamp(ship_.throttle + c.throttleChange * realSeconds, -1.0, 1.0);
    if (c.manualBrake) HoldPosition();
}

void GameSession::HoldPosition() {
    ship_.throttle = 0.0;
    ship_.holding = true;
    if (autopilot_) CancelAutopilot();
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
    Vec3d velocity = BodyVelocity(w, b, simTime_);
    if (b.parent >= 0) velocity += BodyVelocity(w, w.bodies[static_cast<std::size_t>(b.parent)], simTime_);
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
        f.position = FamilyPosition(w.scene, belt, family, simTime_);
        f.velocity = family.fixed ? Vec3d{} : w.scene.World(belt.parent).TransformDirection(OrbitVelocity(family.orbit, simTime_));
    }
}

// Conica por tramos: dentro de la zona de influencia de un cuerpo solo cuenta su gravedad, y la
// nave se mueve respecto a el. Asi una nave quieta junto a un planeta viaja con el planeta, sin
// tener que perseguirlo, y al cruzar de una zona a otra su velocidad real no cambia.
void GameSession::UpdateFrame() {
    const World& w = engine_.GetWorld();
    const std::size_t system = static_cast<std::size_t>(nearestSystem_);
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
            if (Length(ship_.position - center) >= reach || reach >= best.reach) continue;
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
            const double fromCenter = Length(ship_.position - w.scene.WorldPosition(belt.parent));
            if (fromCenter > belt.outer * 1.4 + 1000.0) continue;
            for (std::size_t f = 0; f < belt.families.size(); ++f) {
                const AsteroidFamily& family = belt.families[f];
                if (Length(ship_.position - FamilyPosition(w.scene, belt, family, simTime_)) < family.radius * 1.3) {
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
            const double distance = Length(ship_.position - w.scene.WorldPosition(b.node));
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
    if (best.body != frame_.body || best.belt != frame_.belt || best.family != frame_.family) ship_.velocity += frame_.velocity - best.velocity;
    frame_ = best;
}

std::string GameSession::DominantName() const {
    const World& w = engine_.GetWorld();
    if (frame_.body >= 0) return w.bodies[static_cast<std::size_t>(frame_.body)].name;
    if (frame_.belt >= 0) return w.beltInfo[static_cast<std::size_t>(frame_.belt)].name;
    return "";
}

// Radio de Schwarzschild entre distancia: mide cuanto se frena el tiempo en un pozo gravitatorio.
double GameSession::GravityWell() const {
    if (frame_.body < 0 || frame_.mu <= 0.0) return 0.0;
    const World& w = engine_.GetWorld();
    const Body& b = w.bodies[static_cast<std::size_t>(frame_.body)];
    const double radius = std::max(Length(ship_.position - frame_.position), b.radius);
    const double rs = 2.0 * frame_.mu / (kLightSpeed * kLightSpeed);
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
    const Vec3d offset = ship_.position - info.position;
    const double distance = Length(offset);
    const Vec3d side = distance > 1.0 ? offset * (1.0 / distance) : Vec3d{0, 0, -1};
    Vec3d normal = Cross(side, ship_.velocity + ship_.warpHeading * 0.001);
    if (Length(normal) < 1e-6) normal = Cross(side, Heading(ship_));
    if (Length(normal) < 1e-6) normal = Cross(side, std::abs(side.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0});
    orbitNormal_ = Normalize(normal);
    orbitOffset_ = side * radius;
    const double mu = OrbitMu(target);
    orbitRate_ = mu > 0.0 ? std::sqrt(mu / (radius * radius * radius)) : 0.0;
    orbiting_ = target;
    autopilot_.reset();
    routePhase_ = RoutePhase::None;
    ship_.velocity = {};
    ship_.warpSpeed = 0;
    ship_.warpCharge = 0;
    ship_.throttle = 0;
    ship_.holding = false;
    ship_.mode = DriveMode::Impulse;
    ship_.speed = orbitRate_ * radius;
}

// Al encender el motor sale con la velocidad que llevaba en la orbita.
void GameSession::LeaveOrbit() {
    if (const std::optional<TargetInfo> info = Resolve(*orbiting_)) {
        const Vec3d tangent = Cross(orbitNormal_, orbitOffset_) * orbitRate_;
        ship_.velocity = tangent + info->velocity - frame_.velocity;
    }
    // Recien soltada, la proa aun toca la escotilla: esa estacion no choca hasta que se aparte.
    if (dockPhase_ != DockPhase::None && orbiting_->kind == Kind::Station) undockGrace_ = orbiting_->index;
    orbiting_.reset();
    ship_.holding = false;
    if (dockPhase_ != DockPhase::None) Say("Amarras sueltas.");
    dockPhase_ = DockPhase::None;
}

// ---- Atraque ---------------------------------------------------------------------------------

const Station* GameSession::DockedStation() const {
    return dockPhase_ != DockPhase::None && orbiting_ && orbiting_->kind == Kind::Station ? stations_.Find(orbiting_->index) : nullptr;
}

bool GameSession::Dock(const TargetRef& target, bool instant) {
    if (phase_ != GamePhase::Flying || onFoot_) return false;
    if (target.kind != Kind::Station || !stations_.Find(target.index) || !Resolve(target)) {
        Say("Solo se puede atracar en una estación propia.");
        return false;
    }
    if (orbiting_ && *orbiting_ == target && dockPhase_ != DockPhase::None) return true;
    if (InRange(target, rules_.ship.interactRange) && ship_.warpSpeed <= rules_.drive.impulseSpeed) {
        BeginDock(target);
        if (instant) {
            const Station* s = stations_.Find(target.index);
            orbitOffset_ = DockGoal(*s);
            ship_.orientation = ship_.aim = DockAttitude(*s);
            ship_.angularRate = {};
            dockPhase_ = DockPhase::Docked;
        }
        return true;
    }
    dockWanted_ = target.index;
    if (SetAutopilot(target)) return true;
    dockWanted_ = -1;
    return false;
}

void GameSession::BeginDock(const TargetRef& target) {
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info) return;
    autopilot_.reset();
    exploring_ = -1;
    CancelScan();
    EnterOrbit(target, *info, std::max(Length(ship_.position - info->position), info->radius + rules_.ship.bow));
    orbitRate_ = 0.0;
    dockedAge_ = 0.0;
    dockPhase_ = DockPhase::Approaching;
    dockWanted_ = -1;
    Say("Haz de atraque enganchado.");
}

void GameSession::Undock() {
    if (dockPhase_ == DockPhase::None || !orbiting_) return;
    LeaveOrbit();
}

// El haz acerca la nave sin que importe lo que lleve: no se puede rebotar ni pasar de largo. Primero
// la rodea, sin atravesarla, hasta ponerla frente a la escotilla; despues la mete en linea recta,
// con la proa por delante. Atracada, la sigue aunque la estacion gire.
void GameSession::StepDock(double realSeconds) {
    if (dockPhase_ == DockPhase::None) return;
    const Station* s = DockedStation();
    if (!s || !StationVisible(*s)) return;
    const DockPort port = Port(*s);
    const Vec3d goal = DockGoal(*s);
    // De costado: el haz la gira hasta que su compuerta de estribor mira a la escotilla.
    ship_.aim = DockAttitude(*s);
    if (dockPhase_ == DockPhase::Docked) {
        orbitOffset_ = goal;
        ship_.orientation = ship_.aim;
        ship_.angularRate = {};
        dockedAge_ += realSeconds;
        return;
    }
    const double keep = std::exp(-realSeconds / rules_.ship.dockTime);
    const Vec3d corridor = goal + port.normal * rules_.ship.dockCorridor;
    const Vec3d fromGoal = orbitOffset_ - goal;
    const double along = Dot(fromGoal, port.normal);
    const double aside = Length(fromGoal - port.normal * along);
    if (along > -0.01 && aside < 0.03) {
        // En el pasillo: recta hacia la escotilla.
        orbitOffset_ = goal + fromGoal * keep;
    } else {
        // Alrededor: el angulo y la altura bajan a la vez, sin pasar nunca mas cerca que el pasillo.
        const double radius = Length(orbitOffset_), target = Length(corridor);
        const Vec3d from = orbitOffset_ * (1.0 / std::max(radius, 1e-9)), to = Normalize(corridor);
        const double angle = std::acos(std::clamp(Dot(from, to), -1.0, 1.0));
        Vec3d axis = Cross(from, to);
        if (Length(axis) < 1e-9) axis = Cross(from, std::abs(from.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0});
        const double turn = angle < 0.002 ? angle : angle * (1.0 - keep);
        const Vec3d direction = Quatd::FromAxisAngle(Normalize(axis), turn).ToMatrix().TransformDirection(from);
        const double reach = target + (radius - target) * keep;
        orbitOffset_ = angle < 0.002 && std::abs(reach - target) < 0.01 ? corridor : direction * std::max(reach, std::min(radius, target));
    }
    if (Length(orbitOffset_ - goal) > 0.01) return;
    orbitOffset_ = goal;
    dockPhase_ = DockPhase::Docked;
    Say("Atracada en " + s->name + ".");
    SaveCheckpoint("");
    saveWanted_ = true;
}

// Junto a una estacion la nave no corre: cuanto mas cerca, mas se le recorta la velocidad respecto a
// ella. Sin mando, los estabilizadores frenan la deriva: se queda a su lado en vez de alejarse.
void GameSession::LimitNearStations(double realSeconds) {
    const ShipRules& r = rules_.ship;
    for (const Station& s : stations_.All()) {
        if (s.site.system != nearestSystem_ || !StationVisible(s)) continue;
        const Vec3d at = StationPosition(s);
        const double distance = Length(at - ship_.position);
        if (distance > rules_.world.stationNear) continue;
        const std::optional<TargetInfo> info = Resolve({Kind::Station, s.site.system, s.id});
        if (!info) continue;
        const Vec3d matched = info->velocity - frame_.velocity;
        Vec3d relative = ship_.velocity - matched;
        // En la zona de atraque, casi parada; mas lejos, acercarse va despacio y alejarse, menos.
        const bool closing = Dot(relative, at - ship_.position) > 0.0;
        const bool zone = distance < r.dockZone;
        const double top = StationTop(distance, closing);
        if (!approach_ || distance < approach_->distance) approach_ = ApproachInfo{s.name, distance, top, zone};
        if (ship_.throttle == 0.0) relative = relative * std::exp(-realSeconds / rules_.ship.driftDamping);
        const double speed = Length(relative);
        ship_.velocity = matched + (speed > top ? relative * (top / speed) : relative);
        ship_.warpSpeed = std::min(ship_.warpSpeed, top);
    }
}

// Los topes se piensan en segundos reales, que es lo que ve el jugador: el tiempo se frena al
// acercarse (UpdateFocus), y sin esto la nave se arrastraria. Al lado va a tiempo real y mandan los
// propulsores finos. Devuelve km/s simulados.
double GameSession::StationTop(double distance, bool closing) const {
    const ShipRules& r = rules_.ship;
    const double rate = std::max(timeScale_ * timeFocus_, 1e-6);
    double real = 0.0;  // km por segundo real
    if (distance < r.maneuverRange) real = closing ? std::max(distance / r.maneuverTime, r.maneuverMinimum) : std::max(distance / r.maneuverLeave, r.maneuverLeaveMinimum);
    else real = distance / (closing ? r.approachTime : r.recedeTime);
    return real / rate;
}

int GameSession::StationFrame() const {
    int best = -1;
    double nearest = rules_.world.stationNear;
    for (const Station& s : stations_.All()) {
        if (s.site.system != nearestSystem_ || !StationVisible(s)) continue;
        const double distance = Length(StationPosition(s) - ship_.position);
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
    if (!s || !Docked() || onFoot_) {
        Say("Hay que estar atracada para bajar.");
        return false;
    }
    deckKind_ = s->kind;
    deck_ = MakeDeck(deckKind_);
    player_ = {};
    player_.position = deck_.spawn;
    player_.yaw = 1.5707963;  // de espaldas a la esclusa
    crew_ = MakeCrew(deck_, body_, rules_.foot.crew, rng_);
    onFoot_ = true;
    Say("A pie en " + s->name + ".");
    return true;
}

bool GameSession::CanBoard() const {
    if (!onFoot_) return false;
    Vec3d to = deck_.airlock - player_.position;
    to.y = 0.0;
    return Length(to) <= rules_.foot.boardRange;
}

bool GameSession::Board(bool anywhere) {
    if (!onFoot_) return false;
    if (!anywhere && !CanBoard()) {
        Say("La esclusa está lejos.");
        return false;
    }
    onFoot_ = false;
    crew_.clear();
    Say("De vuelta a bordo.");
    return true;
}

void GameSession::StepFoot(const FootControls& c, double realSeconds) {
    player_.yaw += c.yaw;
    player_.pitch = std::clamp(player_.pitch + c.pitch, -1.45, 1.45);
    if (realSeconds <= 0.0) return;
    StepHumanoid(player_, c.move, deck_.area, body_, rules_.foot.gravity, realSeconds);
    for (Person& p : crew_) StepPerson(p, deck_, body_, rules_.foot.gravity, realSeconds, rng_);
}

bool GameSession::EnterOrbitHere() {
    const World& w = engine_.GetWorld();
    if (orbiting_) return true;
    if (frame_.body < 0 || ship_.warpSpeed > rules_.drive.impulseSpeed) {
        Say("Aquí no hay nada que orbitar.");
        return false;
    }
    const Body& b = w.bodies[static_cast<std::size_t>(frame_.body)];
    TargetRef target;
    target.system = b.system;
    target.kind = b.isStar ? Kind::Star : Kind::Planet;
    target.index = b.isStar ? b.member : b.data;
    const std::optional<TargetInfo> info = Resolve(target);
    if (!info) return false;
    const double radius = Length(ship_.position - info->position);
    if (radius < b.radius * 1.05) {
        Say("Demasiado bajo para orbitar.");
        return false;
    }
    // Pasar a la velocidad circular cuesta energia.
    const double circular = std::sqrt(frame_.mu / radius);
    ship_.energy = std::max(ship_.energy - std::abs(circular - Length(ship_.velocity)) * 0.05, 0.0);
    EnterOrbit(target, *info, radius);
    Say("Órbita estable alrededor de " + info->name + ".");
    return true;
}

// La ruta: orientarse, formar la burbuja, viajar, frenar y entrar en orbita, rodeando lo que se
// cruce. Devuelve lo que debe hacer el motor en este paso.
DriveInput GameSession::FlyRoute(double seconds, Vec3d& wanted) {
    DriveInput order;
    const std::optional<TargetInfo> info = Resolve(*autopilot_);
    if (!info) {
        Say("Objetivo perdido.");
        autopilot_.reset();
        routePhase_ = RoutePhase::None;
        return order;
    }
    const Vec3d toTarget = info->position - ship_.position;
    const double distance = Length(toTarget);
    const double remaining = distance - info->arrival;
    routeDistance_ = remaining;
    if (remaining <= std::max(ship_.speed * seconds * 1.5, 3.0)) {
        const TargetRef arrived = *autopilot_;
        if (arrived.kind == Kind::Station && dockWanted_ == arrived.index) {
            BeginDock(arrived);
            return order;
        }
        Say("En órbita de " + info->name + ".");
        EnterOrbit(arrived, *info, std::max(std::min(distance, info->arrival), info->radius * 1.05));
        return order;
    }

    Vec3d direction = toTarget * (1.0 / std::max(distance, 1e-6));
    const Vec3d matched = info->velocity - frame_.velocity;  // viaja con el objetivo: asi lo alcanza
    Vec3d carried = matched;
    // La burbuja reacciona con retraso, y fuera pasa mas tiempo que a bordo. Con este margen frena
    // sin pasarse nunca: cada factor e de distancia lleva unos segundos, pero llega entera.
    const double response = rules_.drive.warpBrake * 4.0 * exteriorFactor_;
    double warpLimit = remaining / std::max(response, seconds * 1.5);
    const double handling = 1.0 + rules_.ship.handlingBonus * ship_.handlingTier;
    const double accel = rules_.drive.impulseAccel * handling;
    double cruise = std::min(rules_.drive.impulseSpeed, std::sqrt(2.0 * accel * std::max(remaining, 0.0)) * 0.8);

    // Lo primero que se cruce en el camino se rodea, por la tangente a una altura segura.
    const World& w = engine_.GetWorld();
    const Body* obstacle = nullptr;
    double nearest = remaining, shell = 0, open = 1.0e300;
    for (const Body& b : w.bodies) {
        if (!Alive(b) || b.kind == PlanetClass::Belt || b.radius < 50.0) continue;
        const Vec3d center = w.scene.WorldPosition(b.node);
        const Vec3d offset = ship_.position - center;
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
        const Vec3d offset = ship_.position - w.scene.WorldPosition(obstacle->node);
        const double height = Length(offset);
        const Vec3d normal = offset * (1.0 / std::max(height, 1e-6));
        Vec3d side = direction - normal * Dot(direction, normal);
        if (Length(side) < 1e-3) side = Heading(ship_) - normal * Dot(Heading(ship_), normal);
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
        const double curve = turn * shell / std::max(timeScale_ * exteriorFactor_, 1.0);
        const double approach = std::max(height - shell, 0.0) / std::max(response, seconds * 1.5);
        warpLimit = std::min(warpLimit, approach + curve);
        cruise = std::min(cruise, approach + curve);
        carried = WorldVelocity(*obstacle) - frame_.velocity;
        rounding = height < shell * 3.0;
    }
    AimAt(ship_, direction);
    const bool aligned = AimError(ship_) <= kAlignedAngle * 3.0;
    const double facing = std::max(Dot(Heading(ship_), direction), 0.0);  // ya avanza mientras gira

    // Lejos, crucero. Entre estrellas, burbuja. Cerca, el motor normal.
    const bool bubble = remaining > kWarpRouteMin && !rounding;
    if (bubble) {
        const DriveMode curve = HyperUnlocked(ship_, rules_.drive) ? DriveMode::Hyper : DriveMode::Warp;
        const bool far = remaining > kCurveRouteMin;
        const bool curving = far && CanCurve(ship_, curve, rules_.drive);
        if (far && !curving && !fuelWarned_) {
            fuelWarned_ = true;
            Say(HybridUnlocked(ship_) ?"Sin antimateria para la burbuja: la ruta sigue en crucero." : "Sin motor híbrido: la ruta sigue en crucero.");
        }
        ship_.mode = curving ? curve : DriveMode::Cruise;
        ship_.throttle = 1.0;
        // El crucero arranca al momento: hasta estar alineada, nada de correr hacia donde no toca.
        order.warpLimit = aligned ? std::max(warpLimit, 1.0) : 1.0;
        wanted = carried + direction * (ship_.warpSpeed > rules_.drive.impulseSpeed ? 0.0 : cruise * facing);
        routePhase_ = !aligned ? RoutePhase::Aligning : ship_.warpSpeed > warpLimit * 1.02 ? RoutePhase::Braking : RoutePhase::Cruising;
    } else {
        ship_.mode = DriveMode::Impulse;
        ship_.throttle = 0.0;
        wanted = carried + direction * (cruise * facing);
        routePhase_ = rounding ? RoutePhase::Rounding : !aligned ? RoutePhase::Aligning : RoutePhase::Braking;
    }
    order.wanted = &wanted;
    return order;
}

RouteReport GameSession::Route() const {
    RouteReport r;
    if (!autopilot_) return r;
    r.phase = routePhase_;
    r.distance = routeDistance_;
    if (const std::optional<TargetInfo> info = Resolve(*autopilot_)) r.target = info->name;
    const double rate = ship_.speed * timeScale_ * exteriorFactor_;
    r.seconds = rate > 1.0 ? routeDistance_ / rate : 0.0;
    return r;
}

// La orbita que lleva ahora: la que seguiria si apagase todo.
OrbitReport GameSession::OrbitNow() const {
    OrbitReport o;
    if (orbiting_) {
        const std::optional<TargetInfo> info = Resolve(*orbiting_);
        if (!info) return o;
        const double height = Length(orbitOffset_) - info->radius;
        o.valid = true;
        o.locked = true;
        o.body = info->name;
        o.periapsis = o.apoapsis = height;
        o.period = orbitRate_ > 0.0 ? 2.0 * kPi / orbitRate_ : 0.0;
        return o;
    }
    if (frame_.mu <= 0.0 || ship_.holding) return o;
    const Vec3d r = ship_.position - frame_.position;
    const double radius = Length(r), speed2 = Dot(ship_.velocity, ship_.velocity);
    const double energy = speed2 * 0.5 - frame_.mu / radius;
    const Vec3d h = Cross(r, ship_.velocity);
    const double p = Dot(h, h) / frame_.mu;
    o.valid = true;
    o.body = DominantName();
    if (energy >= 0.0) {
        o.escaping = true;
        return o;
    }
    const double a = -frame_.mu / (2.0 * energy);
    const double e = std::sqrt(std::max(1.0 - p / a, 0.0));
    o.periapsis = a * (1.0 - e) - frame_.radius;
    o.apoapsis = a * (1.0 + e) - frame_.radius;
    o.period = 2.0 * kPi * std::sqrt(a * a * a / frame_.mu);
    return o;
}

// Una explosion, un combate o un choque inminente no pueden pasar a 60 veces la velocidad real.
void GameSession::UpdateFocus(double realSeconds) {
    const World& w = engine_.GetWorld();
    double wanted = 1.0;
    if (onFoot_) {
        wanted = 1.0 / std::max(timeScale_, 1.0);  // una persona vive a tiempo real
    } else if (phase_ != GamePhase::Flying) {
        wanted = rules_.world.wreckTime;
    } else {
        if (warning_ && warning_->seconds < 8.0) wanted = rules_.world.dangerTime;
        if (Overheated()) wanted = rules_.world.dangerTime;  // da tiempo a salir de donde quema
        if (ModeInfo(mode_).hostilesAttack) {
            const Vec3d sector = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(nearestSystem_)]);
            for (const Anomaly& a : anomalies_) {
                if (a.system != nearestSystem_ || a.stance != Stance::Hostile || a.hull <= 0.0) continue;
                if (Length(sector + a.offset - ship_.position) < std::max(rules_.anomalies.hostileWeaponRange * 2.0, Scaled(rules_.combat, a.weapon, a.weaponTier).range * 1.3)) wanted = rules_.world.dangerTime;
            }
        }
        // Un proyectil en vuelo cerca de la nave tambien pide tiempo para reaccionar, sea quien sea el que dispara.
        for (const Projectile& p : projectiles_) {
            if (!p.dead && Length(p.position - ship_.position) < rules_.combat.defenseRange * 4.0) wanted = rules_.world.dangerTime;
        }
        // Junto a una estacion, mas despacio; pegada a ella, a tiempo real, para arrimarse a la escotilla.
        const double real = 1.0 / std::max(timeScale_, 1.0);
        for (const Station& s : stations_.All()) {
            if (s.site.system != nearestSystem_ || !StationVisible(s)) continue;
            const double d = Length(StationPosition(s) - ship_.position), fine = rules_.ship.maneuverRange;
            if (d >= rules_.world.stationNear) continue;
            double near = rules_.world.stationTime;
            if (d < fine && dockPhase_ != DockPhase::Docked) near = real;  // atracada no hay que maniobrar
            else if (d < fine * 10.0) near = real * std::pow(rules_.world.stationTime / real, std::log10(d / fine));
            wanted = std::min(wanted, std::max(near, real));
        }
    }
    timeFocus_ += (wanted - timeFocus_) * (1.0 - std::exp(-realSeconds / 0.35));
}

std::optional<CollisionWarning> GameSession::FindWarning() const {
    if (phase_ != GamePhase::Flying || orbiting_ || autopilot_) return std::nullopt;  // la ruta ya se ocupa
    const World& w = engine_.GetWorld();
    const double rate = std::max(timeScale_ * timeFocus_ * exteriorFactor_, 1.0);
    std::optional<CollisionWarning> out;
    double soonest = 30.0;  // segundos reales
    const Vec3d velocity = ship_.velocity + ship_.warpHeading * ship_.warpSpeed;
    for (const Body& b : w.bodies) {
        if (!Alive(b) || b.kind == PlanetClass::Belt) continue;
        const double limit = b.radius * (b.isStar ? rules_.ship.heatReach * 0.5 : 1.0);
        const Vec3d offset = ship_.position - w.scene.WorldPosition(b.node);
        const Vec3d relative = velocity + frame_.velocity - WorldVelocity(b);
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
    if (orbiting_) {
        const std::optional<TargetInfo> info = Resolve(*orbiting_);
        if (!info || orbitRate_ <= 0.0) return path;
        const Vec3d side = Cross(orbitNormal_, orbitOffset_);
        // La nave esta aqui y la camara con ella: los puntos se aprietan junto a la nave.
        constexpr int kHalf = 180;
        for (int i = -kHalf; i <= kHalf; ++i) {
            const double t = static_cast<double>(i) / kHalf;
            const double a = kPi * t * t * t;
            path.push_back(info->position + orbitOffset_ * std::cos(a) + side * std::sin(a));
        }
        return path;
    }
    if (autopilot_) {
        const std::optional<TargetInfo> info = Resolve(*autopilot_);
        if (!info) return path;
        path.push_back(ship_.position);
        path.push_back(info->position + Normalize(ship_.position - info->position) * info->arrival);
        // La orbita de llegada.
        const Vec3d in = Normalize(ship_.position - info->position);
        const Vec3d other = Normalize(Cross(in, std::abs(in.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0}));
        for (int i = 0; i <= kPoints; ++i) {
            const double a = 2.0 * kPi * i / kPoints;
            path.push_back(info->position + (in * std::cos(a) + other * std::sin(a)) * info->arrival);
        }
        return path;
    }
    if (ship_.holding || ship_.speed < 0.5 || ship_.warpSpeed > rules_.drive.impulseSpeed) return path;

    // Inercia y gravedad, un buen trozo por delante.
    Vec3d r = ship_.position - frame_.position, v = ship_.velocity;
    double period = 4.0 * 3600.0;
    if (frame_.mu > 0.0) {
        const double radius = Length(r);
        period = std::min(period * 6.0, 2.0 * kPi * std::sqrt(radius * radius * radius / frame_.mu));
    }
    // El paso empieza en un par de km, porque la camara esta a un par de km, y crece con lo recorrido:
    // lejos, un tramo largo sigue pareciendo curvo. Salto de rana: la orbita se cierra sola.
    const auto pull = [&](const Vec3d& at) {
        const double d = Length(at);
        return frame_.mu > 0.0 ? at * (-frame_.mu / (d * d * d)) : Vec3d{};
    };
    double elapsed = 0;
    for (int i = 0; i < kPoints * 10 && elapsed < period; ++i) {
        const double radius = Length(r), speed = std::max(Length(v), 1e-3);
        if (radius < frame_.radius) break;
        path.push_back(frame_.position + r);
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
    if (phase_ != GamePhase::Flying || orbiting_ || onFoot_ || realSeconds <= 0.0) return path;
    constexpr int kSteps = 160;
    const double dt = realSeconds / kSteps;
    const double rate = timeScale_ * timeFocus_;
    ShipState s = ship_;
    Vec3d origin = frame_.position, matched;
    const int anchor = autopilot_ ? -1 : StationFrame();
    if (anchor >= 0) {
        const std::optional<TargetInfo> info = Resolve({Kind::Station, nearestSystem_, anchor});
        origin = info->position;
        matched = info->velocity - frame_.velocity;
    }
    s.position = ship_.position - origin;
    s.velocity = ship_.velocity - matched;
    path.push_back(ship_.position);
    for (int i = 0; i < kSteps; ++i) {
        StepHeading(s, rules_.ship, dt);
        DriveInput order;
        const double d = std::max(Length(s.position), 1.0);
        if (anchor < 0 && frame_.mu > 0.0) order.gravity = s.position * (-frame_.mu / (d * d * d));
        order.dragScale = anchor >= 0 || (frame_.body >= 0 && !engine_.GetWorld().bodies[static_cast<std::size_t>(frame_.body)].isStar) ? 0.0 : 1.0;
        StepDrive(s, rules_, dt * rate, dt * rate * exteriorFactor_, dt, order);
        if (anchor >= 0) {
            const double distance = Length(s.position);
            const double top = StationTop(distance, Dot(s.velocity, s.position) < 0.0);
            if (s.throttle == 0.0) s.velocity = s.velocity * std::exp(-dt / rules_.ship.driftDamping);
            const double speed = Length(s.velocity);
            if (speed > top) s.velocity = s.velocity * (top / speed);
        } else if (frame_.body >= 0 && Length(s.position) < frame_.radius) {
            break;
        }
        path.push_back(origin + s.position);
    }
    return path;
}

void GameSession::LoseShipTo(const std::string& reason) {
    if (phase_ != GamePhase::Flying) return;
    lossReason_ = reason;
    warning_.reset();
    LoseShip();
}

// Un cuerpo no se atraviesa. Por debajo de cierta velocidad, el golpe abolla el casco; por encima,
// la nave se deshace. Lo que se lleva por dentro se pierde: nunca se impide alejarse.
void GameSession::Impact(const std::string& name, const Vec3d& normal, const Vec3d& bodyVelocity, bool star) {
    const Vec3d world = ship_.velocity + ship_.warpHeading * ship_.warpSpeed + frame_.velocity;
    const double closing = -Dot(world - bodyVelocity, normal);
    if (closing <= 0.0) return;

    const GameMode& mode = ModeInfo(mode_);
    if (mode.damage > 0.0 && !creative_) {  // en creativo choca igual, pero sin dano
        if (star) return LoseShipTo("Nave vaporizada en " + name + ".");
        if (closing >= rules_.ship.shatterSpeed) return LoseShipTo("Nave pulverizada contra " + name + ".");
        const double share = closing / rules_.ship.shatterSpeed;
        const double taken = rules_.ship.hull * share * share * mode.damage;
        ship_.hull = std::max(ship_.hull - taken, 0.0);
        Say("Impacto contra " + name + ".");
        WoundHull(taken, normal * -1.0, true);  // el golpe llega por el lado que da al cuerpo
    }
    // Se queda en la superficie, con lo que no iba hacia dentro.
    ship_.warpSpeed = 0.0;
    ship_.warpCharge = 0.0;
    const double left = -Dot(ship_.velocity + frame_.velocity - bodyVelocity, normal);
    if (left > 0.0) ship_.velocity += normal * left;
}

// from es donde estaba antes de este paso: a gran velocidad un paso puede saltarse un planeta
// entero, asi que se comprueba todo el tramo recorrido y no solo el final.
void GameSession::KeepOutOfBodies(const Vec3d& from) {
    const World& w = engine_.GetWorld();
    const Vec3d travel = ship_.position - from;
    const double length = Length(travel);
    double first = 2.0;
    Vec3d hit, hitCenter;
    const Body* struck = nullptr;
    for (const Body& b : w.bodies) {
        if (!Alive(b) || b.kind == PlanetClass::Belt) continue;
        const Vec3d center = w.scene.WorldPosition(b.node);
        const double limit = b.radius + kSoftTouch;
        const Vec3d offset = ship_.position - center;
        const double distance = Length(offset);
        if (distance < limit && distance > 0.0) {
            ship_.position = center + offset * (limit / distance);
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
        ship_.position = hit;
        Impact(struck->name, Normalize(hit - hitCenter), WorldVelocity(*struck), struck->isStar);
    }
}

// Naves, bastiones, estructuras y estaciones propias tambien son solidos. Se barre el tramo
// recorrido, como con los astros. Embestir una nave o un bastion le hace el mismo dano que recibe
// el casco propio; atracando, la estacion deja pasar a la nave.
void GameSession::KeepOutOfStructures(const Vec3d& from) {
    const Vec3d travel = ship_.position - from;
    const double squared = std::max(Dot(travel, travel), 1e-12);
    struct Solid { Vec3d center, velocity; double radius; std::string name; int anomaly; };
    std::vector<Solid> solids;
    for (const Anomaly& a : anomalies_) {
        if (a.system != nearestSystem_ || (a.maxHull > 0.0 && a.hull <= 0.0)) continue;
        solids.push_back({AnomalyPosition(a), a.velocity, a.radius, a.name, a.id});
    }
    // Las estaciones propias son pequenas al lado de la nave (1,2 km): cuenta su eslora entera, de
    // proa a popa, y no una esfera que no dejaria arrimarse a la escotilla.
    if (dockPhase_ == DockPhase::None) {
        const Vec3d heading = Heading(ship_);
        const double half = rules_.ship.bow;
        for (const Station& s : stations_.All()) {
            if (s.site.system != nearestSystem_ || !StationVisible(s)) continue;
            const std::optional<TargetInfo> info = Resolve({Kind::Station, s.site.system, s.id});
            if (!info) continue;
            const double limit = info->radius + rules_.ship.beam;
            const double along = std::clamp(Dot(info->position - ship_.position, heading), -half, half);
            const Vec3d nearest = ship_.position + heading * along;
            const Vec3d away = nearest - info->position;
            const double gap = Length(away);
            if (s.id == undockGrace_) {
                if (gap > limit + 0.05) undockGrace_ = -1;  // ya se ha apartado
                continue;
            }
            if (gap >= limit) continue;
            const Vec3d normal = gap > 1e-9 ? away * (1.0 / gap) : heading * -1.0;
            ship_.position += normal * (limit - gap);
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
        if (Length(away) < 1e-9) away = Heading(ship_) * -1.0;
        const Vec3d normal = Normalize(away);
        ship_.position = s.center + normal * limit;
        const Vec3d world = ship_.velocity + ship_.warpHeading * ship_.warpSpeed + frame_.velocity;
        const double closing = -Dot(world - s.velocity, normal);
        if (s.anomaly >= 0 && closing > 0.0) {
            if (Anomaly* a = FindAnomaly(s.anomaly); a && a->maxHull > 0.0) {
                const double share = closing / rules_.ship.shatterSpeed;
                DamageAnomaly(*a, DamageType::Kinetic, rules_.ship.hull * share * share, 0);
            }
        }
        Impact(s.name, normal, s.velocity, false);
        anomalies_.erase(std::remove_if(anomalies_.begin(), anomalies_.end(), [](const Anomaly& a) { return a.maxHull > 0.0 && a.hull <= 0.0; }), anomalies_.end());
        return;
    }
}

// Las rocas sueltas del anillo (BeltField): las mismas que se dibujan, en orbitas circulares.
void GameSession::CheckFieldRocks(const Vec3d& from) {
    const World& w = engine_.GetWorld();
    const Vec3d travel = ship_.position - from;
    const double reach = Length(travel) + 40.0;
    const bool plasma = ship_.defense.field == ShieldKind::Plasma && FieldActive();
    std::vector<BeltFieldRock> rocks;
    for (const AsteroidBelt& belt : w.belts) {
        if (!belt.used || belt.parent == kNoNode || belt.mu <= 0.0 || belt.families.empty() || belt.families.front().fixed) continue;
        rocks.clear();
        BeltField(belt, w.scene.World(belt.parent), w.scene.WorldPosition(belt.parent), ship_.position, reach, simTime_, BeltRockTier(belt), rocks);
        const double squared = std::max(Dot(travel, travel), 1e-9);
        for (const BeltFieldRock& rock : rocks) {
            if (plasma && static_cast<double>(rock.radius) < rules_.defense.plasmaRock) continue;  // se vaporiza sin notarlo
            const double along = std::clamp(Dot(rock.position - from, travel) / squared, 0.0, 1.0);
            const Vec3d away = from + travel * along - rock.position;
            const double limit = static_cast<double>(rock.radius) + kSoftTouch * 0.6;
            if (Dot(away, away) >= limit * limit) continue;
            const Vec3d normal = Length(away) > 1e-6 ? Normalize(away) : Normalize(from - rock.position);
            ship_.position = rock.position + normal * limit;
            Impact("un asteroide", normal, rock.velocity, false);
            return;
        }
    }
}

// Las rocas del cumulo en el que esta la nave. Se generan solo aqui, y salen de su semilla.
void GameSession::CheckRocks(const Vec3d& from) {
    if (frame_.belt < 0) return;
    const World& w = engine_.GetWorld();
    const AsteroidFamily& family = w.belts[static_cast<std::size_t>(frame_.belt)].families[static_cast<std::size_t>(frame_.family)];
    if (rocks_.size() > 32 && rocks_.find(family.seed) == rocks_.end()) rocks_.clear();
    std::vector<AsteroidRock>& rocks = rocks_[family.seed];
    if (rocks.empty()) rocks = GenerateRocks(family);
    // Una mina ya ha despejado lo que tenia alrededor: ahi se maniobra sin rocas.
    for (const Station& s : stations_.All()) {
        if (s.site.system != nearestSystem_ || !StationVisible(s)) continue;
        const Vec3d at = StationPosition(s) - frame_.position;
        if (Length(at) > static_cast<double>(family.radius) + kStationClearing) continue;
        for (std::size_t i = 0; i < rocks.size(); ++i)
            if (Length(Vec3d(rocks[i].offset) - at) < kStationClearing + static_cast<double>(rocks[i].radius)) engine_.GetWorld().deadRocks[family.seed].insert(static_cast<int>(i));
    }
    const auto dead = w.deadRocks.find(family.seed);
    const Vec3d travel = ship_.position - from;
    const double squared = std::max(Dot(travel, travel), 1e-9);
    for (std::size_t i = 0; i < rocks.size(); ++i) {
        const AsteroidRock& rock = rocks[i];
        if (dead != w.deadRocks.end() && dead->second.count(static_cast<int>(i)) != 0) continue;
        const Vec3d center = frame_.position + Vec3d(rock.offset);
        const double along = std::clamp(Dot(center - from, travel) / squared, 0.0, 1.0);
        const Vec3d closest = from + travel * along;
        const double limit = static_cast<double>(rock.radius) + kSoftTouch * 0.6;
        const Vec3d away = closest - center;
        if (Dot(away, away) >= limit * limit) continue;
        // El plasma vaporiza las rocas pequenas al instante: la nave sigue sin notarlas.
        if (static_cast<double>(rock.radius) < rules_.defense.plasmaRock && ship_.defense.field == ShieldKind::Plasma && FieldActive()) {
            engine_.GetWorld().deadRocks[family.seed].insert(static_cast<int>(i));
            DefenseState& d = ship_.defense;
            const std::size_t k = static_cast<std::size_t>(ShieldKind::Plasma);
            d.charge[k] = std::max(d.charge[k] - static_cast<double>(rock.radius) * 100.0, 0.0);
            d.heat = std::min(d.heat + static_cast<double>(rock.radius) * 10.0, rules_.thermal.maximum);
            Say("Asteroide vaporizado por el plasma.");
            continue;
        }
        const Vec3d normal = Length(away) > 1e-6 ? Normalize(away) : Normalize(from - center);
        ship_.position = center + normal * limit;
        Impact("un asteroide", normal, frame_.velocity, false);
        return;
    }
}

void GameSession::UpdateAnomalies(double seconds) {
    const World& w = engine_.GetWorld();
    const GameMode& mode = ModeInfo(mode_);
    const double close = Visibility() * 1.5;
    // El plasma hace brillar la nave: los hostiles la ven desde mucho mas lejos.
    const double sight = ship_.defense.field == ShieldKind::Plasma && FieldActive() ? rules_.defense.plasmaSight : 1.0;
    for (Anomaly& a : anomalies_) {
        const Vec3d sector = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(a.system)]);
        const Vec3d ship = ship_.position - sector;
        const double distance = Length(ship - a.offset);
        if (distance > rules_.anomalies.activeRange) continue;
        StepAnomaly(a, ship, rules_.anomalies, seconds, rng_, mode.hostilesAttack, sight);
        a.cooldown = std::max(a.cooldown - seconds, 0.0);
        if (mode.hostilesAttack && a.armed && a.stance == Stance::Hostile && a.hull > 0.0) {
            const WeaponRules spec = Scaled(rules_.combat, a.weapon, a.weaponTier);
            if (distance < spec.range && a.cooldown <= 0.0) {
                if (!a.discovered) Say("Bajo ataque: " + a.name + ".");
                FireAlien(a, sector + a.offset);
            }
        }
        if (distance < rules_.ship.identifyRange) a.discovered = true;
        if (distance < close) a.revealed = true;  // a esta distancia se ve a simple vista
    }
}

Anomaly* GameSession::FindAnomaly(int id) {
    for (Anomaly& a : anomalies_)
        if (a.id == id) return &a;
    return nullptr;
}

const Anomaly* GameSession::FindAnomaly(int id) const {
    for (const Anomaly& a : anomalies_)
        if (a.id == id) return &a;
    return nullptr;
}

// ---- Combate -------------------------------------------------------------------------------

Vec3d GameSession::ShipVelocity() const {
    if (orbiting_) {
        if (const std::optional<TargetInfo> info = Resolve(*orbiting_)) return info->velocity + ship_.moveDirection * ship_.speed;
    }
    return ship_.velocity + frame_.velocity + ship_.warpHeading * ship_.warpSpeed;
}

// Los astros del sistema que tiran de un proyectil. Los asteroides y cometas apenas atraen.
std::vector<GravitySource> GameSession::GravitySources() const {
    std::vector<GravitySource> out;
    const World& w = engine_.GetWorld();
    const SystemState& state = w.state[static_cast<std::size_t>(nearestSystem_)];
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
    for (const WeaponMount& m : ship_.weapons)
        if (m.kind == kind) tier = std::max(tier, m.tier);
    return tier;
}

double GameSession::WeaponRange(WeaponKind kind) const { return Scaled(rules_.combat, kind, WeaponTier(kind)).range; }

double GameSession::WeaponCooldown(WeaponKind kind) const {
    double soonest = 1e300;
    for (const WeaponMount& m : ship_.weapons)
        if (m.kind == kind) soonest = std::min(soonest, m.cooldown);
    return soonest;
}

void GameSession::TrackTarget(const TargetRef& target) {
    if (!Solution(target, WeaponKind::Spinal)) {
        Say("Sin solución de tiro para alinear.");
        return;
    }
    if (autopilot_) CancelAutopilot();
    tracking_ = target;
}

bool GameSession::WeaponReady(WeaponKind kind) const {
    for (const WeaponMount& m : ship_.weapons)
        if (m.kind == kind && m.cooldown <= 0.0) return true;
    return false;
}

std::optional<FiringSolution> GameSession::Solution(const TargetRef& target, WeaponKind kind) const {
    if (target.kind != Kind::Anomaly) return std::nullopt;
    const Anomaly* a = FindAnomaly(target.index);
    if (!a || a->maxHull <= 0.0 || a->hull <= 0.0) return std::nullopt;
    const WeaponRules spec = Scaled(rules_.combat, kind, WeaponTier(kind));
    const FiringSolution s = Solve(ship_.position, ShipVelocity(), AnomalyPosition(*a), a->velocity, spec.speed, kind == WeaponKind::Spinal, GravitySources(), ship_.computerTier, rules_.combat);
    if (!s.valid) return std::nullopt;
    return s;
}

bool GameSession::AimAtSolution(const TargetRef& target, WeaponKind kind) {
    const std::optional<FiringSolution> s = Solution(target, kind);
    if (!s) return false;
    AimAt(ship_, s->direction);
    LimitAim(ship_, rules_.ship.aimLead);
    return true;
}

bool GameSession::Fire(WeaponKind kind, const TargetRef& target) {
    if (phase_ != GamePhase::Flying || onFoot_) return false;
    const std::string name = Name(kind);
    const Anomaly* a = target.kind == Kind::Anomaly ? FindAnomaly(target.index) : nullptr;
    if (!a || a->maxHull <= 0.0 || a->hull <= 0.0) {
        Say("Sin blanco: fija una nave o un bastión.");
        return false;
    }
    if (ship_.warpSpeed > rules_.drive.impulseSpeed) {
        Say("No se puede disparar con la burbuja formada.");
        return false;
    }
    if (Overheated()) {
        Say(name + ": sobrecalentada, espera a que baje el calor.");
        return false;
    }
    WeaponMount* mount = nullptr;
    bool mounted = false;
    for (WeaponMount& m : ship_.weapons) {
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
    if (Length(AnomalyPosition(*a) - ship_.position) > spec.range) {
        Say(name + ": fuera de alcance (" + Fixed(spec.range / 1000.0, 0) + " mil km).");
        return false;
    }
    if (ship_.energy < spec.energy || ship_.antimatter < spec.antimatter || ship_.cargo.Get(Resource::Metal) < spec.metal) {
        Say(name + ": sin " + (ship_.energy < spec.energy ? "energía" : ship_.antimatter < spec.antimatter ? "antimateria" : "metal") + " para disparar.");
        return false;
    }
    const std::optional<FiringSolution> s = Solution(target, kind);
    if (!s) {
        Say(name + ": sin solución de tiro.");
        return false;
    }

    Vec3d direction = s->direction;
    if (kind == WeaponKind::Spinal) direction = Heading(ship_);  // fijo al casco
    else if (kind == WeaponKind::Turret) direction = Scatter(direction, AimError(rules_.combat, ship_.computerTier) * AimSpread(ship_.zones), rng_);  // el puente apunta

    ship_.energy -= spec.energy;
    ship_.antimatter -= spec.antimatter;
    ship_.cargo.Add(Resource::Metal, -spec.metal);
    mount->cooldown = spec.cooldown;
    ship_.defense.heat = std::min(ship_.defense.heat + rules_.thermal.weaponHeat[static_cast<int>(kind)], rules_.thermal.maximum);
    const Vec3d shooter = ShipVelocity();
    for (int i = 0; i < spec.count; ++i) {
        Projectile p;
        p.kind = kind;
        p.type = spec.type;
        p.team = 0;
        p.owner = 0;
        p.target = a->id;
        p.damage = spec.damage;
        p.proximity = spec.proximity;
        p.life = spec.life;
        p.guided = spec.turn > 0.0;
        p.beam = kind == WeaponKind::Spinal;
        p.cruise = spec.speed;
        p.turn = spec.turn;
        const Vec3d dir = spec.count > 1 ? Scatter(direction, 0.03, rng_) : direction;
        // Una torreta de modulo dispara desde su ranura; las de serie, desde el borde del casco.
        const ShipDesign& design = CurrentShipDesign();
        if (mount->slot >= 0 && mount->slot < static_cast<int>(design.slots.size()))
            p.position = ship_.position + ship_.orientation.ToMatrix().TransformDirection(design.slots[static_cast<std::size_t>(mount->slot)].position * 0.001) + dir * 0.03;
        else p.position = ship_.position + dir * (rules_.ship.radius + 0.1);
        p.origin = p.position;
        p.velocity = p.beam ? dir * spec.speed : shooter + dir * (p.guided ? spec.speed * 0.3 : spec.speed);
        projectiles_.push_back(p);
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
        if (!a || Length(AnomalyPosition(*a) - ship_.position) > WeaponRange(kind)) continue;
        if (kind == WeaponKind::Spinal && std::acos(std::clamp(Dot(Heading(ship_), s->direction), -1.0, 1.0)) * Length(AnomalyPosition(*a) - ship_.position) > Scaled(rules_.combat, kind, 1).proximity) continue;
        if (Fire(kind, target)) ++fired;
    }
    return fired;
}

void GameSession::FireAlien(Anomaly& a, const Vec3d& position) {
    const WeaponRules spec = Scaled(rules_.combat, a.weapon, a.weaponTier);
    const FiringSolution s = Solve(position, a.velocity, ship_.position, ShipVelocity(), spec.speed, a.weapon == WeaponKind::Spinal, GravitySources(), a.computerTier, rules_.combat);
    a.cooldown = spec.cooldown * (1.0 + rules_.combat.jitter * Uniform(rng_, -1.0, 1.0));
    if (!s.valid) return;
    const bool guided = spec.turn > 0.0;
    Projectile p;
    p.kind = a.weapon;
    p.type = spec.type;
    p.team = TeamOf(a);
    p.owner = a.id;
    p.target = 0;  // el jugador
    p.damage = spec.damage;
    p.proximity = spec.proximity;
    p.life = spec.life;
    p.guided = guided;
    p.beam = a.weapon == WeaponKind::Spinal;
    p.cruise = spec.speed;
    p.turn = spec.turn;
    // El escudo electromagnetico confunde sus sensores: fallan mas, incluso el haz.
    const bool jammed = ship_.defense.field == ShieldKind::EM && FieldActive();
    const double error = AimError(rules_.combat, a.computerTier) * (jammed ? rules_.defense.emJamming : 1.0);
    const Vec3d dir = guided || (p.beam && !jammed) ? s.direction : Scatter(s.direction, error, rng_);
    p.position = position + dir * (a.radius + 0.5);
    p.origin = p.position;
    p.velocity = p.beam ? dir * spec.speed : a.velocity + dir * (guided ? spec.speed * 0.3 : spec.speed);
    projectiles_.push_back(p);
}

// Todo dano de armas a la nave pasa por aqui: primero el campo, luego los conos y al final el casco.
void GameSession::Hurt(DamageType type, double amount, const std::string& source, const Vec3d& toward, int attacker, const ZoneImpact* impact) {
    const GameMode& mode = ModeInfo(mode_);
    if (mode.damage <= 0.0 || phase_ != GamePhase::Flying) return;
    DefenseState& d = ship_.defense;
    const bool frontal = Dot(Heading(ship_), toward) > 0.0;
    const HitResult hit = AbsorbHit(d, rules_.defense, type, amount, frontal, FieldActive(), ship_.cargo.Get(Resource::Silicate));
    ship_.cargo.Add(Resource::Silicate, -hit.silicate);
    if (d.field != ShieldKind::Dust && d.field != ShieldKind::Love) d.heat = std::min(d.heat + hit.field * rules_.thermal.absorbHeat, rules_.thermal.maximum);  // la arena se traga ese calor
    if (d.field == ShieldKind::Love && hit.field > 0.0) {
        ship_.hull = std::min(ship_.hull + hit.field * rules_.defense.loveHeal, rules_.ship.hull);
        ship_.energy = std::min(ship_.energy + hit.field * (1.0 - rules_.defense.loveHeal), rules_.ship.energy);
        if (hit.reflected > 0.0) {
            if (Anomaly* a = FindAnomaly(attacker)) DamageAnomaly(*a, type, hit.reflected, 0);
        }
    }
    const double taken = hit.hull * mode.damage;
    ship_.hull = std::max(ship_.hull - taken, 0.0);
    if (impact) WoundAt(taken, *impact);
    else WoundHull(taken, toward);
    // Una rafaga ocupa una sola linea del registro.
    const std::string lead = std::string("Impactos de ") + source + ": ";
    if (log_.empty() || log_.back().rfind(lead, 0) != 0) {
        hitStreak_ = 0;
        hitDamage_ = 0.0;
        hitShield_ = 0.0;
    } else {
        log_.pop_back();
    }
    ++hitStreak_;
    hitDamage_ += taken;
    hitShield_ += hit.field + hit.plates;
    std::string line = lead + std::to_string(hitStreak_) + " (" + Name(type) + "), -" + Fixed(hitDamage_, 0) + " de casco";
    if (hitShield_ >= 0.5) line += ", " + Fixed(hitShield_, 0) + " parados por los escudos";
    Say(line + ".");
    if (ship_.hull <= 0.0) lossReason_ = "Nave destruida por " + source + ".";
}

// El golpe cae en la zona de ese lado. No cambia el casco total: eso ya lo ha hecho quien llama.
// Un golpe directo en una zona roja hace mas dano: es donde mas duele.
void GameSession::WoundHull(double amount, const Vec3d& worldFrom, bool blunt) {
    if (amount <= 0.0 || rules_.ship.hull <= 0.0) return;
    directedLoss_ += amount;
    const Vec3d local = Normalize(ship_.orientation.Conjugate().ToMatrix().TransformDirection(worldFrom) + Vec3d{0, 0, 1e-9});
    const int zone = ZoneHitFrom(local, std::uniform_real_distribution<double>(0.0, 1.0)(rng_));
    RecordHit(zone, local, amount / rules_.ship.hull, blunt);
    const double critical = ZoneInfo(zone).red != RedZone::None ? kCriticalHit : 1.0;
    WoundZone(zone, amount * critical / (rules_.ship.hull * kZoneToughness));
}

// El golpe cae en la zona que encuentra su trayectoria, y su crater en ese punto.
void GameSession::WoundAt(double amount, const ZoneImpact& impact) {
    if (amount <= 0.0 || rules_.ship.hull <= 0.0) return;
    directedLoss_ += amount;
    RecordHit(impact.zone, impact.from, amount / rules_.ship.hull, false, &impact.point);
    const double critical = ZoneInfo(impact.zone).red != RedZone::None ? kCriticalHit : 1.0;
    const double fraction = amount * critical / (rules_.ship.hull * kZoneToughness);
    WoundPiece(impact.zone, impact.point, fraction / ZoneHardness(ship_, impact.zone));
    WoundZone(impact.zone, fraction);
}

int GameSession::ShootHull(const Vec3d& start, const Vec3d& end, double hullAmount) {
    const ZoneImpact impact = ProjectileImpact(start, end);
    if (hullAmount > 0.0) {
        ship_.hull = std::max(ship_.hull - hullAmount, 0.0);
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
    h.id = nextHit_++;
    h.zone = zone;
    h.from = localFrom;
    h.intensity = intensity;
    h.blunt = blunt;
    h.time = simTime_;
    h.absolute = ship_.position + engine_.GetWorld().origin;
    h.velocity = ship_.velocity + ship_.warpHeading * ship_.warpSpeed + frame_.velocity;
    h.orientation = ship_.orientation;
    hullHits_.push_back(h);
    // Se guardan los ultimos: un casco con mas agujeros que estos ya no se distingue.
    constexpr std::size_t kMaxHits = 48;
    if (hullHits_.size() > kMaxHits) hullHits_.erase(hullHits_.begin());
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
    fraction /= ZoneHardness(ship_, zone);  // placas y refuerzos montados en ella
    const HullZoneInfo& info = ZoneInfo(zone);
    double& integrity = ship_.zones[static_cast<std::size_t>(zone)];
    if (ZoneLost(ship_.zones, zone)) {
        WoundZone(info.inner, fraction * kInnerShare);
        return;
    }
    const double excess = fraction - integrity;
    const bool wasStanding = integrity > 0.0;
    integrity = std::max(integrity - fraction, 0.0);
    zoneHeat_[static_cast<std::size_t>(zone)] = std::min(zoneHeat_[static_cast<std::size_t>(zone)] + 0.3 + fraction * 4.0, 1.0);
    if (integrity > 0.0 || ship_.hull <= 0.0) return;

    if (info.red != RedZone::None) {
        if (wasStanding) FailRedZone(zone);
        return;
    }
    if (!info.losable || !wasStanding) return;

    const auto toWorld = ship_.orientation.ToMatrix();
    const Vec3d out = Normalize(toWorld.TransformDirection(info.center + Vec3d{0, 0, 1e-3}));
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    const Vec3d pivot = CustomZoneLayout() ? CustomZoneLayout()->centers[static_cast<std::size_t>(zone)] : info.center;
    LostPiece piece;
    piece.zone = zone;
    piece.pivot = pivot;
    piece.absolute = ship_.position + toWorld.TransformDirection(pivot * 0.001) + engine_.GetWorld().origin;
    // Se lleva la velocidad de la nave y un empujon de unas decenas de m/s hacia fuera.
    piece.velocity = ship_.velocity + ship_.warpHeading * ship_.warpSpeed + frame_.velocity + (out + Vec3d{unit(rng_), unit(rng_), unit(rng_)} * 0.3) * 0.03;
    piece.orientation = ship_.orientation;
    piece.spinAxis = Normalize(Vec3d{unit(rng_), unit(rng_), unit(rng_)} + Vec3d{0, 0, 1e-3});
    piece.spin = 0.15 + 0.35 * std::fabs(unit(rng_));
    lostPieces_.push_back(piece);
    Say(std::string("Se ha desprendido ") + (info.masculine ? "el " : "la ") + info.name + ": solo se repone atracando en una estación propia.");

    // Con una bodega se va lo que llevaba: su parte de cada material.
    if (zone >= static_cast<int>(HullZone::Hold1) && zone <= static_cast<int>(HullZone::Hold6)) {
        const double share = 1.0 / static_cast<double>(HoldsLeft(ship_.zones) + 1);
        for (const Resource r : {Resource::Metal, Resource::Silicate, Resource::Ice, Resource::Gas, Resource::StellarMatter}) ship_.cargo.Add(r, -ship_.cargo.Get(r) * share);
    }
    if (zone == static_cast<int>(HullZone::WarpRing)) Say("Sin anillo no hay curvatura: solo queda el impulso.");
    LoseModules(zone, -1);
    if (excess > 0.0) WoundZone(info.inner, excess * kInnerShare);
}

void GameSession::FailRedZone(int zone) {
    if (phase_ != GamePhase::Flying || creative_ || ModeInfo(mode_).damage <= 0.0) return;
    failedZone_ = zone;
    switch (static_cast<HullZone>(zone)) {
        case HullZone::Core:
            lossReason_ = "El núcleo perdió la contención: la nave estalló desde dentro.";
            warning_.reset();
            LoseShip();
            break;
        case HullZone::Bridge: ShutDownShip("Puente destruido: la nave se quedó sin mando y se apagó."); break;
        default: ShutDownShip("Espina partida: la energía ya no llega a ningún sistema y la nave se apagó."); break;
    }
}

// Se apaga: sin motores ni luces, a la deriva y girando despacio. La pantalla final llega tras la
// cinematica (kShutdownSeconds).
void GameSession::ShutDownShip(const std::string& reason) {
    lossReason_ = reason;
    warning_.reset();
    phase_ = GamePhase::Adrift;
    wreckAge_ = 0;
    wreckPosition_ = ship_.position;
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    driftVelocity_ = Heading(ship_) * 0.04 + Vec3d{unit(rng_), unit(rng_), unit(rng_)} * 0.01;
    ship_.angularRate = Normalize(Vec3d{unit(rng_), unit(rng_), unit(rng_)} + Vec3d{0, 1e-3, 0}) * 0.035;
    autopilot_.reset();
    tracking_.reset();
    orbiting_.reset();
    dockPhase_ = DockPhase::None;
    dockWanted_ = -1;
    onFoot_ = false;
    exploring_ = -1;
    CancelScan();
    ship_.speed = 0;
    ship_.velocity = {};
    ship_.warpSpeed = 0;
    ship_.warpCharge = 0;
    ship_.throttle = 0;
    ship_.energy = 0;
    Say(reason);
    if (ModeInfo(mode_).permadeath) saveLost_ = true;
}

// La pieza de esa zona mas cercana al impacto se lleva su parte. Una pieza pequena (una antena, un
// mastil) se rompe antes que la zona entera; a 0 se suelta sola.
void GameSession::WoundPiece(int zone, const Vec3d& point, double fraction) {
    const ShipDesign& design = CurrentShipDesign();
    if (design.pieces.empty() || ship_.pieces.size() != design.pieces.size() || fraction <= 0.0) return;
    int best = -1;
    double nearest = 1e300;
    for (std::size_t i = 0; i < design.pieces.size(); ++i) {
        const DesignPiece& p = design.pieces[i];
        if (p.zone != zone || PieceLost(ship_, static_cast<int>(i))) continue;
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
    double& integrity = ship_.pieces[static_cast<std::size_t>(best)];
    integrity = std::max(integrity - fraction * 0.6 / share, 0.0);
    if (integrity <= 0.0 && !ZoneLost(ship_.zones, zone) && ship_.hull > 0.0) DetachPiece(best);
}

void GameSession::DetachPiece(int index) {
    const ShipDesign& design = CurrentShipDesign();
    if (index < 0 || index >= static_cast<int>(design.pieces.size())) return;
    const DesignPiece& p = design.pieces[static_cast<std::size_t>(index)];
    if (index < static_cast<int>(ship_.pieces.size())) ship_.pieces[static_cast<std::size_t>(index)] = 0.0;
    const auto toWorld = ship_.orientation.ToMatrix();
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    LostPiece piece;
    piece.zone = p.zone;
    piece.piece = index;
    piece.pivot = p.center;
    piece.absolute = ship_.position + toWorld.TransformDirection(p.center * 0.001) + engine_.GetWorld().origin;
    const Vec3d out = Normalize(toWorld.TransformDirection(p.center) + Vec3d{0, 0, 1e-6});
    piece.velocity = ship_.velocity + ship_.warpHeading * ship_.warpSpeed + frame_.velocity + (out + Vec3d{unit(rng_), unit(rng_), unit(rng_)} * 0.4) * 0.025;
    piece.orientation = ship_.orientation;
    piece.spinAxis = Normalize(Vec3d{unit(rng_), unit(rng_), unit(rng_)} + Vec3d{0, 0, 1e-3});
    piece.spin = 0.2 + 0.6 * std::fabs(unit(rng_));
    lostPieces_.push_back(piece);
    Say("Arrancada: " + PrettyName(p.name) + ".");
    LoseModules(-1, index);
}

// Un modulo cuya zona se pierde, o cuya pieza de montaje se suelta, se va con ella.
void GameSession::LoseModules(int zone, int piece) {
    const ShipDesign& design = CurrentShipDesign();
    bool changed = false;
    for (std::size_t i = 0; i < ship_.loadout.modules.size() && i < design.slots.size(); ++i) {
        ShipModuleKind& m = ship_.loadout.modules[i];
        if (m == ShipModuleKind::None) continue;
        const ModuleSlot& s = design.slots[i];
        if ((zone >= 0 && s.zone == zone) || (piece >= 0 && s.piece == piece)) {
            Say(std::string("Perdido: ") + ModuleSpec(m).name + " (" + PrettyName(s.name) + ").");
            m = ShipModuleKind::None;
            changed = true;
        }
    }
    if (changed) SyncModuleWeapons();
}

// Cada torreta montada como modulo es un arma mas, con su recarga, al nivel de la torreta de serie.
void GameSession::SyncModuleWeapons() {
    int tier = rules_.ship.weaponTier;
    for (const WeaponMount& m : ship_.weapons)
        if (m.slot < 0 && m.kind == WeaponKind::Turret) tier = m.tier;
    std::vector<WeaponMount> kept;
    for (const WeaponMount& m : ship_.weapons)
        if (m.slot < 0) kept.push_back(m);
    for (std::size_t i = 0; i < ship_.loadout.modules.size(); ++i) {
        if (ship_.loadout.modules[i] != ShipModuleKind::Turret || !SlotWorking(ship_, static_cast<int>(i))) continue;
        double cooldown = 0.0;
        for (const WeaponMount& m : ship_.weapons)
            if (m.slot == static_cast<int>(i)) cooldown = m.cooldown;
        kept.push_back({WeaponKind::Turret, tier, cooldown, static_cast<int>(i)});
    }
    ship_.weapons = std::move(kept);
}

void GameSession::RefitToDesign() {
    FitToDesign(ship_);
    SyncModuleWeapons();
}

bool GameSession::InstallModule(int slot, ShipModuleKind kind, std::string* why) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!CanRefit()) return fail("Los módulos se cambian atracado en una estación propia.");
    FitToDesign(ship_);
    const ShipDesign& design = CurrentShipDesign();
    if (slot < 0 || slot >= static_cast<int>(design.slots.size())) return fail("Esa ranura no existe.");
    const ModuleSlot& s = design.slots[static_cast<std::size_t>(slot)];
    if (kind != ShipModuleKind::None && ModuleSpec(kind).slot != s.kind) return fail(std::string(ModuleSpec(kind).name) + (s.kind == SlotKind::External ? " va por dentro, no en una ranura exterior." : " va por fuera, no en una bahía interior."));
    if (ZoneLost(ship_.zones, s.zone) || PieceLost(ship_, s.piece)) return fail("Esa parte del casco no está: hay que repararla antes.");
    ShipModuleKind& current = ship_.loadout.modules[static_cast<std::size_t>(slot)];
    if (current == kind) return fail("Ya está montado.");
    const ShipModuleSpec& spec = ModuleSpec(kind);
    const bool free = creative_;  // en creativo no cuesta nada
    if (!free && kind != ShipModuleKind::None && (ship_.cargo.Get(Resource::Metal) < spec.metal || ship_.cargo.Get(Resource::Silicate) < spec.silicate))
        return fail(std::string("Faltan materiales: ") + spec.name + " pide " + Fixed(spec.metal, 0) + " kt de metal y " + Fixed(spec.silicate, 0) + " de silicato.");
    // Lo que se quita devuelve la mitad.
    if (!free && current != ShipModuleKind::None) {
        const ShipModuleSpec& old = ModuleSpec(current);
        ship_.cargo.Add(Resource::Metal, old.metal * 0.5);
        ship_.cargo.Add(Resource::Silicate, old.silicate * 0.5);
    }
    if (!free) {
        ship_.cargo.Add(Resource::Metal, -spec.metal);
        ship_.cargo.Add(Resource::Silicate, -spec.silicate);
    }
    current = kind;
    SyncModuleWeapons();
    Say(kind == ShipModuleKind::None ? "Desmontado: " + PrettyName(s.name) + "." : std::string("Montado: ") + spec.name + " en " + PrettyName(s.name) + ".");
    return true;
}

bool GameSession::SetImpulseEngine(int tier, std::string* why) {
    const auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!CanRefit()) return fail("El motor se cambia atracado en una estación propia.");
    if (tier < 1 || tier > kImpulseEngineCount) return fail("Ese motor no existe.");
    if (tier == ship_.loadout.engine) return fail("Ya lleva ese motor.");
    const ImpulseEngineSpec& next = ImpulseEngine(tier);
    const ImpulseEngineSpec& old = ImpulseEngine(ship_.loadout.engine);
    const double metal = creative_ ? 0.0 : next.metal - old.metal * 0.5, silicate = creative_ ? 0.0 : next.silicate - old.silicate * 0.5;
    if (ship_.cargo.Get(Resource::Metal) < metal || ship_.cargo.Get(Resource::Silicate) < silicate)
        return fail(std::string("Faltan materiales: ") + next.name + " pide " + Fixed(std::max(metal, 0.0), 0) + " kt de metal y " + Fixed(std::max(silicate, 0.0), 0) + " de silicato.");
    ship_.cargo.Add(Resource::Metal, -metal);
    ship_.cargo.Add(Resource::Silicate, -silicate);
    ship_.loadout.engine = tier;
    Say(std::string("Motor de impulso: ") + next.name + ".");
    return true;
}

void GameSession::DamageZone(int zone, double hullAmount, const Vec3d* from) {
    if (zone < 0 || zone >= kHullZoneCount || hullAmount <= 0.0) return;
    ship_.hull = std::max(ship_.hull - hullAmount, 0.0);
    directedLoss_ += hullAmount;
    if (from) RecordHit(zone, Normalize(*from + Vec3d{0, 0, 1e-9}), hullAmount / rules_.ship.hull, false);
    WoundZone(zone, hullAmount / (rules_.ship.hull * kZoneToughness));
}

void GameSession::UpdateLostPieces(double seconds, double realSeconds) {
    for (int z = 0; z < kHullZoneCount; ++z) zoneHeat_[static_cast<std::size_t>(z)] *= std::exp(-realSeconds / 6.0);
    for (LostPiece& p : lostPieces_) {
        p.absolute += p.velocity * seconds;
        p.orientation = Quatd::FromAxisAngle(p.spinAxis, p.spin * seconds) * p.orientation;
        p.age += realSeconds;
    }
    for (HullHit& h : hullHits_) h.age += realSeconds;
    // Pasado un rato, o lejos, ya no se ve: se olvida.
    const Vec3d ship = ship_.position + engine_.GetWorld().origin;
    lostPieces_.erase(std::remove_if(lostPieces_.begin(), lostPieces_.end(), [&](const LostPiece& p) { return p.age > 120.0 || Length(p.absolute - ship) > 2.0e4; }), lostPieces_.end());
}

// Un impacto en una nave o un bastion. Si estaba en calma, ahora deja de estarlo.
void GameSession::Strike(Anomaly& a, const Projectile& p) { DamageAnomaly(a, p.type, p.damage, p.team); }

void GameSession::DamageAnomaly(Anomaly& a, DamageType type, double amount, int team) {
    if (a.hull <= 0.0) return;
    a.hull -= ShieldAnomaly(a, rules_.defense, type, amount);
    if (team == 0 && a.stance != Stance::Hostile) {
        a.stance = Stance::Hostile;
        Say(a.name + " responde al fuego.");
    }
    if (a.hull > 0.0) return;
    a.hull = 0.0;
    if (team != 0) return;
    ++kills_;
    const double loot = a.kind == AnomalyKind::AlienStation ? rules_.anomalies.stationLoot : rules_.anomalies.shipLoot;
    ship_.cargo.Add(Resource::Metal, loot);
    Say(a.name + " destruida. Restos: +" + MassText(loot) + " de metal.");
}

void GameSession::UpdateCombat(double seconds) {
    for (WeaponMount& m : ship_.weapons) m.cooldown = std::max(m.cooldown - seconds, 0.0);
    if (projectiles_.empty()) return;
    const GameMode& mode = ModeInfo(mode_);
    const World& w = engine_.GetWorld();

    // Las torretas derriban misiles y drones que se acerquen: las del jugador y las de los hostiles.
    struct Defender { Vec3d position; int team; int tier; };
    std::vector<Defender> defenders;
    int playerTier = 0;
    for (const WeaponMount& m : ship_.weapons)
        if (m.kind == WeaponKind::Turret) playerTier = std::max(playerTier, m.tier);
    if (playerTier > 0 && phase_ == GamePhase::Flying) defenders.push_back({ship_.position, 0, playerTier});
    for (const Anomaly& a : anomalies_) {
        if (!mode.hostilesAttack || !a.armed || a.hull <= 0.0 || a.weapon != WeaponKind::Turret || a.stance != Stance::Hostile) continue;
        defenders.push_back({AnomalyPosition(a), TeamOf(a), a.weaponTier});
    }
    for (Projectile& p : projectiles_) {
        if (!p.guided || p.dead) continue;
        for (const Defender& d : defenders) {
            if (d.team == p.team || Length(p.position - d.position) > rules_.combat.defenseRange) continue;
            if (Uniform(rng_, 0.0, 1.0) < 1.0 - std::exp(-rules_.combat.defenseRate * d.tier * seconds)) {
                p.dead = true;
                if (d.team == 0) Say(std::string(Name(p.kind)) + " enemigo derribado.");
                break;
            }
        }
    }
    (void)w;
    StepProjectiles(seconds);
}

// Cada proyectil avanza en tramos cortos y se comprueba todo el recorrido: uno rapido no atraviesa nada.
void GameSession::StepProjectiles(double seconds) {
    if (projectiles_.empty() || seconds <= 0.0) return;
    const std::vector<GravitySource> sources = GravitySources();
    const int parts = std::clamp(static_cast<int>(std::ceil(seconds / 0.25)), 1, 120);
    const double dt = seconds / parts;
    const Vec3d shipVelocity = ShipVelocity();
    const bool flying = phase_ == GamePhase::Flying;

    // La nave y las anomalias ya se movieron este paso: aqui se retrocede hasta el principio de el.
    std::vector<Anomaly*> targets;
    std::vector<Vec3d> ends;
    for (Anomaly& a : anomalies_) {
        if (a.maxHull <= 0.0 || a.hull <= 0.0) continue;
        targets.push_back(&a);
        ends.push_back(AnomalyPosition(a));
    }
    for (int part = 1; part <= parts; ++part) {
        const double back = seconds - part * dt;  // lo que falta para el final del paso
        for (Projectile& p : projectiles_) {
            if (p.dead) continue;
            Vec3d tp, tv;
            bool have = false;
            if (p.guided && p.target == 0 && flying) {
                tp = ship_.position - shipVelocity * back;
                tv = shipVelocity;
                have = true;
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
                impacts_.push_back({p.position, p.origin, p.type, p.beam, hit, 0.0});
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
            if (p.team != 0 && flying && SegmentNear(from, p.position, ship_.position - shipVelocity * back, rules_.ship.radius + p.proximity)) {
                const Anomaly* owner = FindAnomaly(p.owner);
                const Vec3d relative = p.velocity - shipVelocity;
                const Vec3d toward = Length(relative) > 1e-9 ? relative * (-1.0 / Length(relative)) : Normalize(p.origin - ship_.position);
                // Donde da de verdad: el tramo recorrido en este paso, visto desde la nave (que tambien
                // se ha movido), en metros del modelo. La trayectoria decide la zona, no el atacante.
                const Vec3d center = ship_.position - shipVelocity * back;
                const auto toModel = ship_.orientation.Conjugate().ToMatrix();
                const Vec3d start = toModel.TransformDirection(from - (center - shipVelocity * dt)) * 1000.0;
                const Vec3d end = toModel.TransformDirection(p.position - center) * 1000.0;
                const ZoneImpact impact = ProjectileImpact(start, end);
                Hurt(p.type, p.damage, owner ? owner->name : std::string("fuego enemigo"), toward, p.owner, &impact);
                finish(true);
                continue;
            }
            for (std::size_t i = 0; i < targets.size(); ++i) {
                Anomaly& a = *targets[i];
                if (a.hull <= 0.0 || TeamOf(a) == p.team || a.id == p.owner) continue;
                if (SegmentNear(from, p.position, ends[i] - a.velocity * back, a.radius + p.proximity)) {
                    Strike(a, p);
                    finish(true);
                    break;
                }
            }
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& p) { return p.dead; }), projectiles_.end());
    anomalies_.erase(std::remove_if(anomalies_.begin(), anomalies_.end(), [](const Anomaly& a) { return a.maxHull > 0.0 && a.hull <= 0.0; }), anomalies_.end());
}

// Entrar en la orbita de una estrella demasiado caliente para el blindaje quema el casco.
void GameSession::UpdateHeat(double seconds) {
    const World& w = engine_.GetWorld();
    inHeatZone_ = false;
    const std::size_t s = static_cast<std::size_t>(nearestSystem_);
    const SystemState& state = w.state[s];
    const StarSystemData& system = *w.systems[s];
    const double damage = ModeInfo(mode_).damage;
    for (std::size_t m = 0; m < state.starNodes.size(); ++m) {
        const StarData& star = system.StarAt(static_cast<int>(m));
        const StarYield yield = YieldOf(star);
        const double distance = Length(w.scene.WorldPosition(state.starNodes[m]) - ship_.position);
        if (ship_.heatShieldTier < yield.requiredTier && distance < yield.orbitRadius * 1.5) {
            inHeatZone_ = true;
            ship_.hull = std::max(ship_.hull - damage * 300.0 * (yield.requiredTier - ship_.heatShieldTier) * seconds / 3600.0, 0.0);
        }
        if (star.kind == StarClass::BlackHole) continue;

        // El calor sube con la cuarta potencia al acercarse: lejos no se nota y cerca evapora.
        const double reach = star.radius * std::max(rules_.ship.heatReach - 0.5 * (ship_.heatShieldTier - yield.requiredTier), 1.4);
        if (distance < reach) {
            inHeatZone_ = true;
            const double ratio = reach / std::max(distance, 1.0);
            ship_.hull = std::max(ship_.hull - damage * rules_.ship.heatBurn * (ratio * ratio * ratio * ratio - 1.0) * seconds, 0.0);
            if (ship_.hull <= 0.0) lossReason_ = "Nave evaporada por el calor de " + w.bodies[static_cast<std::size_t>(state.starBodies[std::min(m, state.starBodies.size() - 1)])].name + ".";
        }
    }
}

// ---- Escudos y calor ---------------------------------------------------------------------

bool GameSession::SetShield(ShieldKind kind) {
    DefenseState& d = ship_.defense;
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
    if (ship_.defense.fieldOn == on) return;
    ship_.defense.fieldOn = on;
    Say(on ? std::string("Escudo: ") + Name(ship_.defense.field) + "." : std::string("Escudos apagados."));
}

ShieldKind GameSession::CycleShield() {
    DefenseState& d = ship_.defense;
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
    const DefenseState& d = ship_.defense;
    const std::size_t f = static_cast<std::size_t>(d.field);
    if (!d.fieldOn || !IsField(d.field) || d.shieldTier[f] <= 0) return false;
    if (d.field == ShieldKind::Love) return true;  // ni calor ni energia: es otra fisica
    if (Overheated()) return false;
    if (d.field == ShieldKind::Dust && ship_.cargo.Get(Resource::Silicate) <= 0.0) return false;
    return rules_.defense.shields[f].energy <= 0.0 || ship_.energy > 0.0;
}

bool GameSession::Overheated() const { return game::Overheated(ship_.defense, rules_.thermal); }

void GameSession::SetCoolant(CoolantKind kind, bool on) { ship_.defense.coolantOn[static_cast<std::size_t>(kind)] = on; }

// proper son segundos a bordo: los sistemas de la nave van con su reloj, no con el de fuera.
void GameSession::UpdateThermal(double proper, double realSeconds) {
    DefenseState& d = ship_.defense;
    const ThermalRules& t = rules_.thermal;
    const DefenseRules& r = rules_.defense;
    const GameMode& mode = ModeInfo(mode_);
    const World& w = engine_.GetWorld();
    const double density = NebulaDensity(ship_.position + w.origin);
    const double hours = proper / 3600.0;
    double heatIn = 0.0;

    // El campo encendido gasta, calienta y se recarga.
    const std::size_t f = static_cast<std::size_t>(d.field);
    if (FieldActive() && d.field != ShieldKind::Love) {
        const ShieldRules& s = r.shields[f];
        ship_.energy = std::max(ship_.energy - s.energy * hours, 0.0);
        if (d.field == ShieldKind::Dust) ship_.cargo.Add(Resource::Silicate, -std::min(r.dustUpkeep * hours, ship_.cargo.Get(Resource::Silicate)));
        heatIn += s.heat * proper;
        d.charge[f] = std::min(d.charge[f] + ShieldRegen(r, d.field, d.shieldTier[f]) * proper, ShieldCapacity(r, d.field, d.shieldTier[f]));
    }

    // Curvatura: calienta al formarse la burbuja y, mientras dura, segun la velocidad.
    const bool curving = Curves(ship_.mode);
    if (curving) heatIn += t.formHeat * std::max(ship_.warpCharge - lastWarpCharge_, 0.0);
    lastWarpCharge_ = curving ? ship_.warpCharge : 0.0;
    const bool stable = curving && ship_.warpCharge >= 1.0 && ship_.warpSpeed > 0.0;
    const double tens = ship_.warpSpeed / (10.0 * kLightSpeed);
    if (stable) heatIn += t.warpHeat * std::sqrt(tens) * proper;

    // Presion de ariete: por encima de c, el gas golpea la proa. Los conos la aguantan; sin ellos, el casco.
    const std::size_t a = static_cast<std::size_t>(ShieldKind::Ablative);
    ramming_ = stable && ship_.warpSpeed > kLightSpeed;
    if (ramming_) {
        double ram = r.ramPressure * density * std::sqrt(tens) * realSeconds;
        const double plates = std::min(ram, d.charge[a]);
        d.charge[a] -= plates;
        ram -= plates;
        if (ram > 0.0 && mode.damage > 0.0) {
            ship_.hull = std::max(ship_.hull - ram * mode.damage, 0.0);
            if (!ramWarned_) Say("Presión de ariete: sin conos ablativos, la curvatura castiga el casco.");
            ramWarned_ = true;
            if (ship_.hull <= 0.0) lossReason_ = "Nave aplastada por la presión de ariete.";
        }
    } else {
        ramWarned_ = false;
        // El gas de la nebulosa lija las placas, tanto mas cuanto mas rapido.
        if (!orbiting_) d.charge[a] = std::max(d.charge[a] - r.ablativeWear * density * std::min(ship_.speed / kLightSpeed, 1.0) * proper, 0.0);
    }
    // Las placas de los conos van montadas en la proa: con ella danada caben menos, y sin ella ninguna.
    const double plateCap = ShieldCapacity(r, ShieldKind::Ablative, d.shieldTier[a]) * std::max(Zone(ship_.zones, HullZone::Bow), 0.0);
    d.charge[a] = std::min(d.charge[a], plateCap);
    ship_.massFactor = 1.0 + (plateCap > 0.0 ? r.ablativeMass * d.charge[a] / plateCap : 0.0);

    // Junto a una estrella los radiadores apenas sueltan calor, y en su zona de calor entra mas.
    radiators_ = 1.0;
    const SystemState& state = w.state[static_cast<std::size_t>(nearestSystem_)];
    const StarSystemData& system = *w.systems[static_cast<std::size_t>(nearestSystem_)];
    for (std::size_t m = 0; m < state.starNodes.size(); ++m) {
        const StarData& star = system.StarAt(static_cast<int>(m));
        if (star.kind == StarClass::BlackHole) continue;
        const double reach = star.radius * t.starReach;
        const double distance = Length(w.scene.WorldPosition(state.starNodes[m]) - ship_.position);
        if (distance < reach) radiators_ = std::min(radiators_, (distance / reach) * (distance / reach));
    }
    radiators_ *= RadiatorOutput(ship_.zones) * (1.0 + kRadiatorBoost * WorkingModules(ship_, ShipModuleKind::Radiator));  // rotos sueltan menos; las aletas, mas
    if (inHeatZone_) heatIn += t.starHeat * proper;

    // En una estacion propia: agua del hielo de la bodega y placas nuevas con metal.
    const bool atStation = orbiting_ && orbiting_->kind == Kind::Station && stations_.Find(orbiting_->index) != nullptr;
    if (atStation) {
        const double water = std::min({t.stationRefill * hours, t.waterTank - d.water, ship_.cargo.Get(Resource::Ice)});
        d.water += water;
        ship_.cargo.Add(Resource::Ice, -water);
        const double plates = std::min({r.plateRepair * hours, plateCap - d.charge[a], ship_.cargo.Get(Resource::Metal) / r.plateMetal});
        if (plates > 0.0) {
            d.charge[a] += plates;
            ship_.cargo.Add(Resource::Metal, -plates * r.plateMetal);
        }
    }

    // Con la nave fria y energia de sobra, se licua nitrogeno del gas de la bodega.
    if (d.coolantTier[2] > 0 && d.nitrogen < t.nitrogenTank && d.heat < t.limit * t.coolStart && ship_.energy > rules_.ship.energy * 0.3) {
        const double make = std::min({t.nitrogenMake * hours, t.nitrogenTank - d.nitrogen, ship_.cargo.Get(Resource::Gas), ship_.energy / t.nitrogenEnergy});
        if (make > 0.0) {
            d.nitrogen += make;
            ship_.cargo.Add(Resource::Gas, -make);
            ship_.energy -= make * t.nitrogenEnergy;
        }
    }

    const CoolingResult cool = StepCooling(d, t, proper, heatIn, radiators_, ship_.cargo.Get(Resource::Ice));
    ship_.cargo.Add(Resource::Ice, -cool.iceUsed);

    if (Overheated()) {
        if (mode.damage > 0.0) {
            ship_.hull = std::max(ship_.hull - t.overheatBurn * (d.heat - t.limit) * proper * mode.damage, 0.0);
            if (ship_.hull <= 0.0) lossReason_ = "Nave fundida por su propio calor.";
        }
        if (!overheatWarned_) Say("Sobrecalentamiento: escudos y armas fuera de servicio.");
        overheatWarned_ = true;
    } else if (overheatWarned_ && d.heat < t.limit * 0.9) {
        overheatWarned_ = false;
        Say("El calor baja del límite: escudos y armas de vuelta.");
    }
}

void GameSession::UpdateRepair(double seconds) {
    if (!orbiting_ || orbiting_->kind != Kind::Station) return;
    const Station* s = stations_.Find(orbiting_->index);
    // Atracada, se repara en cualquier estacion propia.
    if (!s || (s->kind != StationKind::Power && !Docked())) return;
    ship_.hull = std::min(ship_.hull + rules_.ship.repairPerHour * seconds / 3600.0, rules_.ship.hull);
    // Las zonas se reparan al mismo ritmo. Una pieza perdida solo se rehace atracada: hace falta el muelle.
    const double rate = rules_.ship.repairPerHour * seconds / 3600.0 / (rules_.ship.hull * kZoneToughness);
    for (int z = 0; z < kHullZoneCount; ++z) {
        double& integrity = ship_.zones[static_cast<std::size_t>(z)];
        if (integrity >= 1.0 || (ZoneLost(ship_.zones, z) && !Docked())) continue;
        const bool rebuilt = ZoneLost(ship_.zones, z);
        integrity = std::min(integrity + rate, 1.0);
        if (rebuilt) Say(std::string("Reconstruida en el muelle: ") + ZoneInfo(z).name + ".");
        // Una zona como nueva ya no tiene crateres.
        if (integrity >= 1.0) hullHits_.erase(std::remove_if(hullHits_.begin(), hullHits_.end(), [z](const HullHit& h) { return h.zone == z; }), hullHits_.end());
    }
    // Las piezas igual: una arrancada solo se rehace en el muelle.
    bool rebuilt = false;
    for (std::size_t i = 0; i < ship_.pieces.size(); ++i) {
        double& integrity = ship_.pieces[i];
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
    if (exploring_ < 0) return;
    for (Anomaly& a : anomalies_) {
        if (a.id != exploring_) continue;
        if (!Explore(a, rules_.anomalies, hours)) {
            if (!a.emitter) exploring_ = -1;
            continue;
        }
        ++ship_.emitters;
        if (ship_.emitters == 1) Say("Emisor de energía negativa recuperado: curvatura Híbrida disponible.");
        else if (ship_.emitters == rules_.drive.hyperEmitters) Say("Segundo emisor de energía negativa: motor Hiper disponible.");
        else Say("Otro emisor de energía negativa: el hiper llega ahora a " + Fixed(HyperFactor(ship_, rules_.drive), 0) + " c.");
        exploring_ = -1;
        SaveCheckpoint("Punto de control");
    }
}

// La lista se rehace de vez en cuando, pero lo que se pinta debe estar donde esta ahora: un
// cumulo de asteroides recorre cientos de km entre dos lecturas, y el origen del mundo se mueve.
void GameSession::RefreshContacts() {
    for (Contact& c : contacts_) {
        if (const std::optional<TargetInfo> info = Resolve(c.target)) {
            c.position = info->position;
            c.distance = Length(info->position - ship_.position);
        }
    }
}

void GameSession::UpdateContacts() {
    World& w = engine_.GetWorld();
    contacts_.clear();
    // Lo que se visita ya se conoce, aunque nadie lo haya barrido.
    for (std::size_t s = 0; s < known_.size(); ++s) {
        if (Length(w.scene.WorldPosition(w.sectors[s]) - ship_.position) >= w.extents[s] * 1.5) continue;
        known_[s] = 1;
        visited_[s] = 1;
    }
    const double sensors = rules_.ship.sensorRange * (Visibility() / rules_.world.fogDistance);
    const double identify = rules_.ship.identifyRange * (Visibility() / rules_.world.fogDistance);

    const auto add = [&](const TargetRef& target, Stance stance, double range) {
        const std::optional<TargetInfo> info = Resolve(target);
        if (!info) return;
        const double distance = Length(info->position - ship_.position);
        if (distance > range) return;
        Contact c;
        c.target = target;
        c.position = info->position;
        c.distance = distance;
        const bool astro = target.kind == Kind::Planet || target.kind == Kind::Belt;
        c.identified = astro ? Scanned(target) : distance < identify || target.kind == Kind::Station || target.kind == Kind::Star;
        c.stance = c.identified ? stance : Stance::None;
        c.name = c.identified ? info->name : "Señal sin identificar";
        c.detail = c.identified ? info->detail : "";
        contacts_.push_back(std::move(c));
    };

    // Una estrella se nota mucho antes que lo demas, tanto mas cuanto mas brilla.
    for (std::size_t s = 0; s < w.state.size(); ++s) {
        if (!known_[s]) continue;  // otro sistema no aparece hasta que un barrido lo localiza
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
    for (const Station& s : stations_.All()) add({Kind::Station, s.site.system, s.id}, Stance::None, 1.0e300);
    for (const Anomaly& a : anomalies_) {
        // Lo frio solo existe para los sensores despues de un barrido.
        if (a.cold && !a.revealed) continue;
        add({Kind::Anomaly, a.system, a.id}, a.stance, a.revealed ? 1.0e300 : sensors);
    }

    std::sort(contacts_.begin(), contacts_.end(), [](const Contact& a, const Contact& b) { return a.distance < b.distance; });
    if (contacts_.size() > kMaxContacts) contacts_.resize(kMaxContacts);
}

void GameSession::Step(double realSeconds, const ShipControls& controls, float focalPixels) {
    World& w = engine_.GetWorld();
    // A bordo pasa el tiempo del jugador; fuera, mas deprisa cuanto mas rapido va y mas hondo esta
    // en un pozo gravitatorio.
    exteriorFactor_ = ExteriorRate(orbiting_ ? 0.0 : ship_.speed, GravityWell(), rules_.world.maxTimeFactor);
    UpdateFocus(realSeconds);
    // La estacion junto a la que vuela, donde estaba antes de que avance el reloj: una mina de un
    // cinturon se mueve con simTime_, y medirla despues dejaria a la nave atras en cada paso.
    const int anchorCandidate = phase_ == GamePhase::Flying ? StationFrame() : -1;
    const Vec3d anchorStart = anchorCandidate >= 0 ? StationPosition(*stations_.Find(anchorCandidate)) : Vec3d{};
    const double proper = realSeconds * timeScale_ * timeFocus_;
    const double seconds = proper * exteriorFactor_;
    const double hours = seconds / 3600.0;
    simTime_ += seconds;
    ++steps_;
    dialogue_.Update(*this, realSeconds);
    approach_.reset();
    // El casco que se pierda en este paso sin pasar por WoundHull se reparte al final entre todas las zonas.
    const double hullStart = ship_.hull;
    directedLoss_ = 0.0;
    UpdateLostPieces(seconds, realSeconds);

    if (phase_ == GamePhase::Exploding) {
        wreckAge_ += realSeconds;
        if (wreckAge_ >= kWreckSeconds) phase_ = GamePhase::GameOver;
    }
    // Apagada, va a la deriva y gira despacio, tambien detras de la pantalla final.
    if (phase_ == GamePhase::Adrift || (phase_ == GamePhase::GameOver && ShutDown())) {
        wreckAge_ += realSeconds;
        wreckPosition_ += driftVelocity_ * realSeconds;
        ship_.position = wreckPosition_;
        const double spin = Length(ship_.angularRate);
        if (spin > 0.0) ship_.orientation = (Quatd::FromAxisAngle(ship_.angularRate, spin * realSeconds) * ship_.orientation).Normalized();
        ship_.aim = ship_.orientation;
        if (phase_ == GamePhase::Adrift && wreckAge_ >= kShutdownSeconds) phase_ = GamePhase::GameOver;
    }
    const bool flying = phase_ == GamePhase::Flying;
    const Vec3d before = ship_.position;
    const Vec3d frameBefore = frame_.position;
    const bool hadFrame = frame_.body >= 0 || frame_.belt >= 0;
    // Junto a una estacion propia la nave vuela en el marco de la estacion (ver StationFrame).
    int anchor = -1;
    Vec3d anchorBefore, anchorOffset;

    if (flying) {
        const bool steering = controls.turnYaw != 0.0f || controls.turnPitch != 0.0f || controls.turnRoll != 0.0f || controls.throttleChange != 0.0;
        if (autopilot_ && steering) CancelAutopilot();
        if (onFoot_) StepFoot(controls.foot, realSeconds);
        else if (orbiting_ && (ship_.throttle != 0.0 || controls.throttleChange != 0.0)) LeaveOrbit();

        Steer(controls, realSeconds);
        // Sin gas, los propulsores de maniobra callan y solo gira el giroscopio.
        const bool noGas = ship_.cargo.Get(Resource::Gas) <= 0.0;
        if (noGas && !gasWarned_) Say("Sin gas para los propulsores de maniobra: solo gira el giroscopio, mucho más despacio.");
        gasWarned_ = noGas;
        StepDock(realSeconds);
        if (tracking_ && (controls.turnYaw != 0.0f || controls.turnPitch != 0.0f || controls.turnRoll != 0.0f || autopilot_ || !AimAtSolution(*tracking_, WeaponKind::Spinal))) tracking_.reset();
        StepHeading(ship_, rules_.ship, realSeconds);
        if (orbiting_) {
            if (!Resolve(*orbiting_)) {
                orbiting_.reset();
                dockPhase_ = DockPhase::None;
                if (onFoot_) Board(true);
            } else {
                // Una orbita asistida es una circunferencia exacta: no decae ni gasta.
                const double angle = orbitRate_ * seconds;
                const Vec3d side = Cross(orbitNormal_, orbitOffset_);
                orbitOffset_ = orbitOffset_ * std::cos(angle) + side * std::sin(angle);
                ship_.speed = orbitRate_ * Length(orbitOffset_);
                ship_.moveDirection = Length(side) > 0.0 ? Normalize(side) : Heading(ship_);
                ship_.shipHours += proper / 3600.0;
                StepReactor(ship_, rules_, proper);
            }
        } else {
            Vec3d wanted;
            DriveInput order;
            if (autopilot_) order = FlyRoute(seconds, wanted);
            if (!orbiting_) {
                if (exploring_ >= 0 && ship_.speed > 1.0) exploring_ = -1;

                // Un paso largo cerca de un cuerpo se saldria de la orbita: se divide.
                int parts = 1;
                const double radius = std::max(Length(ship_.position - frame_.position), 1.0);
                if (frame_.mu > 0.0 && ship_.warpSpeed <= rules_.drive.impulseSpeed) {
                    const double period = 2.0 * kPi * std::sqrt(radius * radius * radius / frame_.mu);
                    parts = std::clamp(static_cast<int>(std::ceil(seconds / (period * 0.01))), 1, 64);
                }
                // Cerca de un planeta, la niebla no debe sacarla de orbita.
                order.dragScale = frame_.body >= 0 && !w.bodies[static_cast<std::size_t>(frame_.body)].isStar ? 0.0 : 1.0;
                const bool stable = Curves(ship_.mode) && ship_.warpCharge >= 1.0;
                // En el marco de una estacion la nave se mueve respecto a ella y sin la gravedad del
                // astro, que la estacion compensa: al soltarse no se queda atras ni cae.
                Vec3d matched;
                if (!autopilot_) anchor = anchorCandidate;
                if (anchor >= 0) {
                    const std::optional<TargetInfo> info = Resolve({Kind::Station, nearestSystem_, anchor});
                    anchorBefore = anchorStart;
                    matched = info->velocity - frame_.velocity;
                    ship_.velocity = ship_.velocity - matched;
                    order.dragScale = 0.0;
                    parts = 1;
                }
                for (int i = 0; i < parts; ++i) {
                    const Vec3d at = ship_.position - frame_.position;
                    const double d = std::max(Length(at), 1.0);
                    order.gravity = frame_.mu > 0.0 && anchor < 0 ? at * (-frame_.mu / (d * d * d)) : Vec3d{};
                    StepDrive(ship_, rules_, proper / parts, seconds / parts, realSeconds / parts, order);
                }
                ship_.velocity += matched;
                if (anchor >= 0) anchorOffset = ship_.position - anchorBefore;
                if (stable && ship_.antimatter <= 0.0) Say("Sin antimateria: la burbuja se colapsa.");
            }
        }
    }

    pins_.clear();
    if (orbiting_ && orbiting_->kind == Kind::Planet) {
        if (const Body* body = FindPlanet(w, orbiting_->system, orbiting_->index)) pins_.push_back(static_cast<int>(body - w.bodies.data()));
    }
    Vec3d position = ship_.position;
    engine_.Recenter(position);
    const Vec3d shift = position - ship_.position;
    wreckPosition_ += shift;
    for (Projectile& p : projectiles_) {
        p.position += shift;
        p.origin += shift;
    }
    for (ImpactFlash& f : impacts_) {
        f.position += shift;
        f.origin += shift;
        f.age += realSeconds;
    }
    impacts_.erase(std::remove_if(impacts_.begin(), impacts_.end(), [](const ImpactFlash& f) { return f.age > 0.6; }), impacts_.end());
    ship_.position = position;

    engine_.Advance(ship_.position, focalPixels, simTime_, pins_, realSeconds);

    nearestSystem_ = eps::NearestSystem(w, ship_.position);
    stations_.Produce(hours);
    stations_.Age(realSeconds);

    // La nave viaja con el cuerpo que domina aqui: en su marco, este no se mueve.
    RefreshFrame(frame_);
    Vec3d carried = hadFrame && (frame_.body >= 0 || frame_.belt >= 0) ? frame_.position - (frameBefore + shift) : Vec3d{};
    if (anchor >= 0) {
        const Station* s = stations_.Find(anchor);
        if (s && StationVisible(*s)) {
            // La estacion lleva a la nave consigo, se mueva como se mueva.
            const Vec3d now = StationPosition(*s);
            carried = now - (anchorBefore + shift);
            ship_.position = now + anchorOffset - carried;  // se suma abajo
        }
    }
    if (!flying) {
        wreckPosition_ += carried;  // los restos siguen al planeta, no se quedan atras
        if (steps_ % kContactEvery == 1) UpdateContacts();
        else RefreshContacts();
        return;
    }
    if (!orbiting_) ship_.position += carried;
    UpdateFrame();
    // Con la nave ya llevada por la estacion: antes, la distancia saldria contra donde va a estar.
    if (!orbiting_ && !autopilot_) LimitNearStations(realSeconds);

    if (!orbiting_) {
        KeepOutOfBodies(before + shift + carried);
        CheckRocks(before + shift + carried);
        if (phase_ == GamePhase::Flying && frame_.belt < 0 && ship_.warpSpeed <= rules_.drive.impulseSpeed) CheckFieldRocks(before + shift + carried);
        if (phase_ == GamePhase::Flying) KeepOutOfStructures(before + shift + carried);
    }
    if (phase_ != GamePhase::Flying) return;
    warning_ = FindWarning();

    ship_.energy = std::min(ship_.energy, rules_.ship.energy);
    UpdateExploration(hours);
    UpdateScan(hours);
    UpdateAnomalies(seconds);
    UpdateCombat(seconds);

    // Orbitas y anomalias acaban de moverse: la nave va con su objetivo en este mismo paso.
    if (orbiting_) {
        if (const std::optional<TargetInfo> info = Resolve(*orbiting_)) ship_.position = info->position + orbitOffset_;
    }

    UpdateHeat(proper);
    UpdateThermal(proper, realSeconds);
    UpdateRepair(seconds);
    if (steps_ % kContactEvery == 1) UpdateContacts();
    else RefreshContacts();
    SpreadWound(hullStart - ship_.hull - directedLoss_);  // calor, presion de ariete: sin direccion

    if (creative_) {
        ship_.zones = IntactHull();
        hullHits_.clear();
        ship_.hull = rules_.ship.hull;
        ship_.energy = rules_.ship.energy;
        ship_.antimatter = AntimatterCapacity(ship_, rules_.ship);
        for (const Resource r : {Resource::Metal, Resource::Silicate, Resource::Ice, Resource::Gas, Resource::StellarMatter})
            if (ship_.cargo.Get(r) < 2000.0) ship_.cargo.Add(r, 2000.0 - ship_.cargo.Get(r));
        // Depositos y placas llenos. El calor sigue contando, para poder probarlo.
        DefenseState& d = ship_.defense;
        d.water = rules_.thermal.waterTank;
        d.nitrogen = rules_.thermal.nitrogenTank;
        const std::size_t a = static_cast<std::size_t>(ShieldKind::Ablative);
        d.charge[a] = ShieldCapacity(rules_.defense, ShieldKind::Ablative, d.shieldTier[a]);
    }
    if (ship_.hull <= 0.0) LoseShip();
}

}  // namespace game
