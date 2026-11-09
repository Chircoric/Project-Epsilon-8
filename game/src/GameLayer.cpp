#include "game/GameLayer.h"

#include <algorithm>
#include <cmath>

#include "epsilon/FrameBuilder.h"

namespace game {

using namespace eps;

namespace {

constexpr float kMetre = 0.001f;
constexpr double kModelRange = 3.0e4;  // km: mas lejos, un modelo ni llega a un pixel
constexpr int kDustMotes = 260;
constexpr double kDustBox = 60.0;      // km: el polvo solo existe alrededor de la nave
constexpr int kCloudCells = 3;
constexpr double kAstronomicalUnit = 1.496e8;  // km

std::uint32_t Hash(std::int64_t x, std::int64_t y, std::int64_t z) {
    std::uint64_t h = static_cast<std::uint64_t>(x) * 73856093ull ^ static_cast<std::uint64_t>(y) * 19349663ull ^ static_cast<std::uint64_t>(z) * 83492791ull;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return static_cast<std::uint32_t>(h);
}

float Unit(std::uint32_t h, int shift) { return static_cast<float>((h >> shift) & 0xFFu) / 255.0f; }

void ApplyLight(MeshInstance& m, const SceneLight& light) {
    m.lightDirection = light.direction;
    m.lightColor = light.color;
    m.light2Direction = light.direction2;
    m.light2Color = light.color2;
    m.ambient = light.ambient;
    m.ambientColor = light.ambientColor;
}

}  // namespace

std::array<Color, 4> NebulaRamp() {
    return {Color{0.30f, 0.035f, 0.03f, 1.0f}, Color{0.36f, 0.13f, 0.03f, 1.0f}, Color{0.34f, 0.25f, 0.07f, 1.0f}, Color{0.17f, 0.17f, 0.18f, 1.0f}};
}

Color NebulaColor(float tone) {
    const std::array<Color, 4> ramp = NebulaRamp();
    const float at = std::clamp(tone, 0.0f, 1.0f) * 3.0f;
    const int k = std::min(static_cast<int>(at), 2);
    const float t = at - static_cast<float>(k);
    const Color& a = ramp[static_cast<std::size_t>(k)];
    const Color& b = ramp[static_cast<std::size_t>(k) + 1];
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f};
}

// Lejos de todo, la niebla reparte una luz suave. Cerca de una estrella o de un agujero negro
// manda una sola fuente: el lado iluminado se quema y las sombras se vuelven negras.
SceneLight GameLayer::LightAt(const FrameContext& c, const Vec3d& position) const {
    const World& w = session_.GetWorld();
    const std::size_t system = static_cast<std::size_t>(session_.NearestSystem());
    const astro::StarSystemData& data = *w.systems[system];
    const SystemState& state = w.state[system];
    const double reach = session_.Visibility() * 250.0;  // lo que deja pasar la niebla

    struct Source {
        Vec3d to{0, 0, 1};
        Color color{0, 0, 0, 0};
        float power = 0;
    };
    Source best, second;
    for (std::size_t m = 0; m < state.starNodes.size(); ++m) {
        const astro::StarData& star = data.StarAt(static_cast<int>(m));
        const bool hole = star.kind == astro::StarClass::BlackHole;
        const Vec3d to = w.scene.WorldPosition(state.starNodes[m]) - position;
        const double distance = std::max(Length(to), 1.0);
        const double flux = hole ? std::pow(star.radius * 3000.0 / distance, 2.0) : star.luminosity / std::pow(distance / kAstronomicalUnit, 2.0);
        const float power = static_cast<float>(1.5 * (1.0 - std::exp(-flux * 0.035)) * (hole ? 1.0 : std::exp(-distance / reach)));
        const Vec3f tint = hole ? Vec3f{1.0f, 0.62f, 0.32f} : astro::BlackbodyColor(star.temperature);
        const Source source{to, {tint.x * power, tint.y * power, tint.z * power, 1.0f}, power};
        if (power > best.power) {
            second = best;
            best = source;
        } else if (power > second.power) {
            second = source;
        }
    }

    // Un planeta cercano devuelve la luz de su estrella con su propio color.
    for (const int index : state.bodies) {
        const Body& b = w.bodies[static_cast<std::size_t>(index)];
        if (!Alive(b) || b.renderable < 0 || b.radius < 100.0) continue;
        const Vec3d to = w.scene.WorldPosition(b.node) - position;
        const double ratio = b.radius / std::max(Length(to), 1.0);
        if (ratio < 0.06) continue;
        const int surface = w.renderables[static_cast<std::size_t>(b.renderable)].surface;
        if (surface < 0) continue;
        // Se ve su lado de dia si la estrella queda a nuestra espalda respecto al planeta.
        const float lit = 0.5f - 0.5f * static_cast<float>(Dot(Normalize(best.to), Normalize(to)));
        const float power = static_cast<float>(std::min(ratio * 1.1, 0.9)) * lit * std::max(best.power, 0.25f);
        if (power <= second.power) continue;
        const Color tint = LookTint(w.surfaceStore[static_cast<std::size_t>(surface)]);
        second = {to, {tint.r * power, tint.g * power, tint.b * power, 1.0f}, power};
    }

    SceneLight light;
    light.direction = Normalize(c.viewRotation.TransformDirection(Vec3f(best.to)));
    light.color = best.color;
    light.direction2 = Normalize(c.viewRotation.TransformDirection(Vec3f(second.to)));
    light.color2 = second.color;
    light.ambient = 0.08f + 0.44f * std::exp(-best.power * 1.8f);

    // La luz ambiente es la de la niebla: lleva su color.
    const Color fog = NebulaColor(session_.NebulaTone());
    const float top = std::max(std::max(fog.r, fog.g), fog.b);
    light.ambientColor = {0.45f + 0.55f * fog.r / top, 0.45f + 0.55f * fog.g / top, 0.45f + 0.55f * fog.b / top, 1.0f};
    return light;
}

// Las estrellas de este sistema dispersan su luz en la niebla, y de cerca muestran su corona.
void GameLayer::CollectStarLights(const FrameContext& c, FramePacket& out) const {
    const World& w = session_.GetWorld();
    const std::size_t system = static_cast<std::size_t>(session_.NearestSystem());
    const astro::StarSystemData& data = *w.systems[system];
    const SystemState& state = w.state[system];
    for (std::size_t m = 0; m < state.starNodes.size(); ++m) {
        const astro::StarData& star = data.StarAt(static_cast<int>(m));
        const bool hole = star.kind == astro::StarClass::BlackHole;
        const Vec3d rel = w.scene.WorldPosition(state.starNodes[m]) - c.cameraPosition;
        const double distance = std::max(Length(rel), 1.0);
        const double size = hole ? star.radius * 12.0 : star.radius;
        const Vec3f tint = hole ? Vec3f{1.0f, 0.62f, 0.32f} : astro::BlackbodyColor(star.temperature);
        StarLight light;
        light.viewCenter = c.viewRotation.TransformDirection(Vec3f(rel));
        light.radius = static_cast<float>(star.radius);
        light.color = {tint.x, tint.y, tint.z, 1.0f};
        light.strength = static_cast<float>(std::min(1.2 * std::pow(size / distance, 0.8), 1.5));
        light.seed = static_cast<float>(system * 7u + m * 3u) * 0.137f;
        light.corona = hole ? 0.0f : 1.0f;
        out.starLights.push_back(light);
    }
    std::sort(out.starLights.begin(), out.starLights.end(), [](const StarLight& a, const StarLight& b) { return a.strength > b.strength; });
}
// Los planetas con aire que mas ocupan en pantalla.
void GameLayer::CollectAtmospheres(const FrameContext& c, FramePacket& out) const {
    const World& w = session_.GetWorld();
    const std::size_t system = static_cast<std::size_t>(session_.NearestSystem());
    const SystemState& state = w.state[system];
    const Vec3d sun = w.scene.WorldPosition(w.sectors[system]);
    struct Found {
        AtmosphereLight air;
        double size = 0;
    };
    std::vector<Found> found;
    for (const int index : state.bodies) {
        const Body& b = w.bodies[static_cast<std::size_t>(index)];
        if (!Alive(b) || b.renderable < 0 || b.kind == astro::PlanetClass::Belt) continue;
        const int surface = w.renderables[static_cast<std::size_t>(b.renderable)].surface;
        if (surface < 0) continue;
        const Color air = w.surfaceStore[static_cast<std::size_t>(surface)].atmosphere;
        if (air.a <= 0.0f) continue;
        const Vec3d center = w.scene.WorldPosition(b.node);
        const Vec3d rel = center - c.cameraPosition;
        const double size = b.radius / std::max(Length(rel), 1.0);
        if (size < 0.004) continue;
        Found f;
        f.size = size;
        f.air.viewCenter = c.viewRotation.TransformDirection(Vec3f(rel));
        f.air.radius = static_cast<float>(b.radius);
        f.air.color = air;
        f.air.sunDirection = Normalize(c.viewRotation.TransformDirection(Vec3f(sun - center)));
        f.air.seed = static_cast<float>(index % 64) * 0.73f;
        const Renderable& drawn = w.renderables[static_cast<std::size_t>(b.renderable)];
        const Vec3f pole = BodyOrientation(drawn, session_.SimTime()).ToMatrix().TransformDirection(Vec3f{0, 1, 0});
        f.air.pole = Normalize(c.viewRotation.TransformDirection(pole));
        f.air.flattening = w.surfaceStore[static_cast<std::size_t>(surface)].flattening;
        // Un gigante no tiene suelo: su aire visible es una bruma fina sobre las nubes, no un halo.
        if (w.surfaceStore[static_cast<std::size_t>(surface)].kind == SurfaceKind::Gas) f.air.color.a *= 0.4f;
        found.push_back(f);
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.size > b.size; });
    for (const Found& f : found) out.atmospheres.push_back(f.air);
}

int GameLayer::RotorOfPart(const std::string& name) {
    if (name.rfind("rotor", 0) != 0) return 0;
    std::size_t i = 5;
    int rotor = 0;
    while (i < name.size() && name[i] >= '0' && name[i] <= '9') rotor = rotor * 10 + (name[i++] - '0');
    return i > 5 && i < name.size() && name[i] == '_' ? rotor : 0;
}

Quatf GameLayer::SpinOf(int spin, double phase) {
    if (spin == 0) return Quatf{};
    // Giran en sentidos opuestos: el par de uno anula el del otro y el casco no se tuerce.
    const double sign = spin % 2 == 1 ? 1.0 : -1.0;
    return Quatf::FromAxisAngle({0, 0, 1}, static_cast<float>(std::fmod(sign * phase, 6.283185307179586)));
}

// Los anillos giran para dar 1 g en su cara interior: w = sqrt(g / r), una vuelta cada ~36 s en un
// anillo de 330 m. Danados giran mas despacio; con la nave apagada se van parando; uno que se ha
// soltado conserva el giro que llevaba.
void GameLayer::UpdateRings(const FrameContext& c) {
    const double dt = lastTime_ < 0.0 ? 0.0 : std::clamp(c.time - lastTime_, 0.0, 0.25);
    lastTime_ = c.time;
    const ZoneLayout* layout = CustomZoneLayout();
    const double radius = layout ? layout->ringRadius : 230.0;
    const double full = std::sqrt(9.81 / std::max(radius, 1.0));
    const HullZones& zones = session_.Ship().zones;
    const int ring = static_cast<int>(HullZone::WarpRing);
    const bool powered = session_.Phase() == GamePhase::Flying;
    const double target = powered ? full * std::sqrt(std::clamp(zones[static_cast<std::size_t>(ring)], 0.0, 1.0)) : 0.0;
    if (ringRate_ < 0.0) ringRate_ = target;
    else if (!ZoneLost(zones, ring)) ringRate_ += (target - ringRate_) * (1.0 - std::exp(-dt / (powered ? 8.0 : 40.0)));
    ringPhase_ = std::fmod(ringPhase_ + ringRate_ * dt, 6.283185307179586 * 64.0);
}

void GameLayer::DrawModel(const FrameContext& c, FramePacket& out, const ModelAsset& model, const Vec3d& position, const Quatd& rotation, int modules, std::uint32_t id, double assembly, const Vec3d* origin) const {
    const Vec3d rel = position - c.cameraPosition;
    if (Length(rel) > kModelRange) return;
    const Quatf toWorld = Quatf::Cast(rotation);
    const Mat4f matrix = toWorld.ToMatrix();
    const Quatf toView = c.camera.Orientation().Conjugate() * toWorld;

    // La propia nave lleva su casco por zonas: lo perdido no se dibuja y lo danado lo ensena el shader.
    const bool ownShip = assets_.ship && &model == assets_.ship.get() && partZones_.size() == model.parts.size();
    // Cerca de la luz, la luz de la nave viene de hacia donde avanza (ShipLight).
    const SceneLight light = ownShip ? ShipLight(c, LightAt(c, position)) : LightAt(c, position);
    const ShipState& ship = session_.Ship();

    std::uint32_t part = 0;
    const float parts = static_cast<float>(std::max<std::size_t>(model.parts.size(), 1));
    for (const ModelPart& p : model.parts) {
        ++part;
        // Las piezas mod_N solo aparecen con N modulos instalados.
        if (modules >= 0 && p.name.rfind("mod_", 0) == 0 && std::atoi(p.name.c_str() + 4) > modules) continue;
        const int zone = ownShip ? partZones_[part - 1] : -1;
        if (zone >= 0 && ZoneLost(ship.zones, zone)) continue;
        if (ownShip && PieceLost(ship, static_cast<int>(part) - 1)) continue;  // arrancada sola
        // Los anillos giran sobre el eje de la nave, que pasa por el origen del modelo.
        const int spin = ownShip && part - 1 < partSpin_.size() ? partSpin_[part - 1] : 0;
        const Quatf turn = SpinOf(spin);
        Vec3f at = Vec3f(rel) + matrix.TransformDirection(turn.ToMatrix().TransformDirection(p.position) * kMetre);

        // Despliegue: cada pieza sale de la nave, viaja y encaja en su sitio, una tras otra.
        float placed = 1.0f;
        if (assembly < 1.0) {
            placed = std::clamp((static_cast<float>(assembly) * 1.6f - static_cast<float>(part - 1) / parts * 0.6f), 0.0f, 1.0f);
            if (placed <= 0.0f) continue;
            placed = placed * placed * (3.0f - 2.0f * placed);
            if (origin) at = Vec3f(*origin - c.cameraPosition) + (at - Vec3f(*origin - c.cameraPosition)) * placed;
        }
        const float radius = p.radius * kMetre * p.scale;
        if (!c.frustum.SphereVisible(at, radius * 1.2f)) continue;
        MeshInstance m;
        m.viewPosition = c.viewRotation.TransformDirection(at);
        m.scale = p.scale * kMetre * (0.35f + 0.65f * placed);
        m.rotation = toView * Quatf::FromAxisAngle({0.3f, 1.0f, 0.2f}, (1.0f - placed) * 3.0f) * turn * p.rotation;
        ApplyLight(m, light);
        m.color = p.color;
        m.emissive = p.emissive + (1.0f - placed) * 1.4f;
        m.id = 0x70000000u + id * 256u + part;
        m.mesh = p.mesh;
        m.texture = p.texture;
        m.foreground = id == 0;  // la propia nave va dentro de la burbuja: su lente no la deforma
        if (zone >= 0 && hullDetail_ > 0) {
            const std::size_t z = static_cast<std::size_t>(zone);
            m.damage = static_cast<float>(1.0 - ship.zones[z]);
            m.damageHeat = static_cast<float>(session_.ZoneHeat()[z]);
            m.damageDetail = hullDetail_;
            m.damageSeed = static_cast<float>(part * 7u % 64u);
            // Lo que brilla por si mismo se apaga con el dano: luces y anillo de campo.
            m.emissive *= static_cast<float>(std::clamp(ship.zones[z] * 1.6, 0.0, 1.0));
        }
        if (ownShip) {
            // El motor de impulso montado cambia el color y la fuerza del escape.
            if (ship.loadout.engine > 1 && p.emissive > 0.0f && (p.name.find("escape") != std::string::npos || p.name.find("tobera") != std::string::npos)) {
                const ImpulseEngineSpec& engine = ImpulseEngine(ship.loadout.engine);
                m.color = {engine.glow[0], engine.glow[1], engine.glow[2], 1.0f};
                m.emissive *= 1.0f + 0.45f * static_cast<float>(ship.loadout.engine - 1);
            }
            m.emissive *= shipLights_;
            if (hullDetail_ >= 3) PartCraters(p, zone, spin, m, out);
        }
        out.meshes.push_back(m);
    }
}

// Lo que se ha soltado del casco: cada zona perdida se aleja girando con sus piezas.
void GameLayer::DrawLostPieces(const FrameContext& c, FramePacket& out) const {
    if (!assets_.ship || partZones_.size() != assets_.ship->parts.size()) return;
    const Vec3d origin = session_.GetWorld().origin;
    for (const GameSession::LostPiece& piece : session_.LostPieces()) {
        const Vec3d center = piece.absolute - origin;
        const Vec3d rel = center - c.cameraPosition;
        if (Length(rel) > kModelRange) continue;
        const SceneLight light = LightAt(c, center);
        const Quatf toWorld = Quatf::Cast(piece.orientation);
        const Mat4f matrix = toWorld.ToMatrix();
        const Quatf toView = c.camera.Orientation().Conjugate() * toWorld;
        const Vec3f zoneCenter = Vec3f(piece.pivot) * kMetre;
        // Se enfria al alejarse: el primer momento aun brilla al rojo.
        const float heat = static_cast<float>(std::exp(-piece.age / 8.0));
        std::uint32_t index = 0;
        for (const ModelPart& p : assets_.ship->parts) {
            ++index;
            // Una pieza arrancada sola, o todas las de su zona.
            if (piece.piece >= 0 ? static_cast<int>(index) - 1 != piece.piece : partZones_[index - 1] != piece.zone) continue;
            // Un anillo suelto sigue girando por inercia.
            const Quatf turn = SpinOf(index - 1 < partSpin_.size() ? partSpin_[index - 1] : 0);
            const Vec3f at = Vec3f(rel) + matrix.TransformDirection(turn.ToMatrix().TransformDirection(p.position) * kMetre - zoneCenter);
            const float radius = p.radius * kMetre * p.scale;
            if (!c.frustum.SphereVisible(at, radius * 1.2f)) continue;
            MeshInstance m;
            m.viewPosition = c.viewRotation.TransformDirection(at);
            m.scale = p.scale * kMetre;
            m.rotation = toView * turn * p.rotation;
            ApplyLight(m, light);
            m.color = p.color;
            m.emissive = 0.0f;  // sin energia: sus luces ya no brillan
            m.id = 0x77000000u + static_cast<std::uint32_t>(piece.zone) * 256u + index;
            m.mesh = p.mesh;
            m.texture = p.texture;
            if (hullDetail_ > 0) {
                m.damage = 0.85f;
                m.damageHeat = heat;
                m.damageDetail = std::min(hullDetail_, 2);  // sin agujeros: ya es un trozo suelto
                m.damageSeed = static_cast<float>(index * 7u % 64u);
            }
            out.meshes.push_back(m);
        }
    }
}

// Nubes en una rejilla fija del mundo: al avanzar, unas se quedan atras y aparecen otras.
void GameLayer::DrawClouds(const FrameContext& c, FramePacket& out) const {
    const World& w = const_cast<GameSession&>(session_).GetWorld();
    const double cell = session_.Visibility() * 1.6;
    const Vec3d absolute = c.cameraPosition + w.origin;
    const auto index = [cell](double v) { return static_cast<std::int64_t>(std::floor(v / cell)); };
    const std::int64_t cx = index(absolute.x), cy = index(absolute.y), cz = index(absolute.z);

    struct Candidate {
        Vec3d position;
        float radius, seed, distance;
    };
    std::vector<Candidate> near;
    for (std::int64_t x = cx - 1; x <= cx + 1; ++x)
        for (std::int64_t y = cy - 1; y <= cy + 1; ++y)
            for (std::int64_t z = cz - 1; z <= cz + 1; ++z) {
                const std::uint32_t h = Hash(x, y, z);
                const Vec3d center = Vec3d{(static_cast<double>(x) + Unit(h, 0)) * cell, (static_cast<double>(y) + Unit(h, 8)) * cell, (static_cast<double>(z) + Unit(h, 16)) * cell};
                const Vec3d rel = center - absolute;
                near.push_back({rel + c.cameraPosition, static_cast<float>(cell) * (0.55f + 0.4f * Unit(h, 24)), static_cast<float>(h % 64u), static_cast<float>(Length(rel))});
            }
    std::sort(near.begin(), near.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });

    for (int i = 0; i < kCloudCells && i < static_cast<int>(near.size()); ++i) {
        const Candidate& n = near[static_cast<std::size_t>(i)];
        const Vec3f rel(n.position - c.cameraPosition);
        // Cada nube tiene su punto de la paleta, cerca del de la zona.
        const Color tone = NebulaColor(session_.NebulaTone() + (n.seed / 64.0f - 0.5f) * 0.6f);
        const Color bright{tone.r * 2.4f, tone.g * 2.4f, tone.b * 2.4f, 1.0f};
        NebulaInstance v;
        v.viewCenter = c.viewRotation.TransformDirection(rel);
        v.radius = n.radius;
        v.inner = bright;
        v.outer = tone;
        v.density = 0.8f;
        v.seed = n.seed;
        v.distance = n.distance;
        out.nebulae.push_back(v);
    }
}

// Motas fijas en el espacio que se envuelven alrededor de la nave. La estela crece con la velocidad.
void GameLayer::DrawDust(const FrameContext& c, FramePacket& out) const {
    const ShipState& ship = session_.Ship();
    const Vec3d heading = ship.moveDirection;
    const double streakKm = std::min(ship.speed * 0.012, kDustBox * 0.6);

    const float focal = c.camera.FocalPx();
    const Color fog = NebulaColor(session_.NebulaTone());
    for (int i = 0; i < kDustMotes; ++i) {
        const std::uint32_t h = Hash(i, i * 7 + 3, i * 13 + 5);
        Vec3d p{Unit(h, 0) * kDustBox * 2.0, Unit(h, 8) * kDustBox * 2.0, Unit(h, 16) * kDustBox * 2.0};
        const Vec3d world = c.cameraPosition + session_.GetWorld().origin;
        const auto wrap = [](double v, double base) {
            const double size = kDustBox * 2.0;
            double r = std::fmod(v - base, size);
            if (r < 0.0) r += size;
            return r - kDustBox;
        };
        const Vec3d rel{wrap(p.x, world.x), wrap(p.y, world.y), wrap(p.z, world.z)};
        const Vec3f view = c.viewRotation.TransformDirection(Vec3f(rel));
        if (view.z <= 0.5f) continue;

        const float fade = std::min(1.0f - static_cast<float>(Length(rel) / (kDustBox * 1.7)), 1.0f);
        if (fade <= 0.02f) continue;
        Color tint{fog.r * 2.5f + 0.25f, fog.g * 2.5f + 0.25f, fog.b * 2.5f + 0.25f, fade * 0.55f};
        if (streakKm > 0.3) {
            // La cola suele quedar detras de la camara: se recorta justo delante de ella.
            Vec3f tail = c.viewRotation.TransformDirection(Vec3f(rel - heading * streakKm));
            if (tail.z < 0.5f) tail = view + (tail - view) * ((view.z - 0.5f) / (view.z - tail.z));
            out.lines.push_back({view, tail, tint});
        } else {
            SpriteInstance s;
            s.viewPosition = view;
            s.pixelRadius = std::max(0.05f / view.z * focal, 1.1f);
            s.tint = tint;
            s.id = 0x71000000u + static_cast<std::uint32_t>(i);
            s.texture = glow_;
            s.blend = BlendMode::Additive;
            out.sprites.push_back(s);
        }
    }
}

// La nave se desmantela: cada pieza sale por su lado, girando, entre explosiones sucesivas.
void GameLayer::DrawWreck(const FrameContext& c, FramePacket& out) const {
    const double age = session_.WreckAge();
    const Vec3d center = session_.WreckPosition();
    const SceneLight light = LightAt(c, center);
    const Vec3d rel = center - c.cameraPosition;
    const Mat4f matrix = Quatf::Cast(wreckRotation_).ToMatrix();
    const Quatf toView = c.camera.Orientation().Conjugate() * Quatf::Cast(wreckRotation_);
    const float focal = c.camera.FocalPx();
    const float heat = static_cast<float>(std::exp(-age * 0.8));

    std::uint32_t index = 0;
    const bool zoned = partZones_.size() == assets_.ship->parts.size();
    for (const ModelPart& p : assets_.ship->parts) {
        ++index;
        if (zoned && partZones_[index - 1] >= 0 && ZoneLost(session_.Ship().zones, partZones_[index - 1])) continue;  // ya se habia soltado
        const std::uint32_t h = Hash(index, 17, 91);
        // Las piezas tardan en soltarse: primero se abre la popa, y el resto la sigue.
        const float release = Unit(h, 0) * 2.2f;
        const float free = std::max(static_cast<float>(age) - release, 0.0f);
        const Quatf turn = SpinOf(index - 1 < partSpin_.size() ? partSpin_[index - 1] : 0);
        const Vec3f local = turn.ToMatrix().TransformDirection(p.position) * kMetre;
        const Vec3f away = Normalize(local + Vec3f{Unit(h, 8) - 0.5f, Unit(h, 16) - 0.5f, Unit(h, 24) - 0.5f} * 0.5f);
        const Vec3f at = Vec3f(rel) + matrix.TransformDirection(local + away * (free * (0.12f + 0.20f * Unit(h, 4))));
        const float radius = p.radius * kMetre * p.scale;
        if (!c.frustum.SphereVisible(at, radius * 1.2f)) continue;

        const Quatf tumble = Quatf::FromAxisAngle(Normalize(Vec3f{Unit(h, 2) - 0.5f, Unit(h, 10) - 0.5f, Unit(h, 18) - 0.2f}), free * (0.2f + Unit(h, 6)));
        MeshInstance m;
        m.viewPosition = c.viewRotation.TransformDirection(at);
        m.scale = p.scale * kMetre;
        m.rotation = toView * tumble * turn * p.rotation;
        ApplyLight(m, light);
        m.color = {p.color.r * (0.45f + heat), p.color.g * (0.35f + heat * 0.6f), p.color.b * (0.30f + heat * 0.3f), 1.0f};
        m.emissive = heat * 1.4f * Unit(h, 12);
        m.id = 0x72000000u + index;
        m.mesh = p.mesh;
        m.texture = p.texture;
        out.meshes.push_back(m);
    }

    const auto flash = [&](const Vec3f& where, float km, const Color& tint, std::uint32_t id) {
        const Vec3f view = c.viewRotation.TransformDirection(where);
        if (view.z <= 0.05f) return;
        SpriteInstance s;
        s.viewPosition = view;
        s.pixelRadius = km / view.z * focal;
        s.tint = tint;
        s.id = 0x73000000u + id;
        s.texture = glow_;
        s.blend = BlendMode::Additive;
        s.fogReach = 50.0f;
        out.sprites.push_back(s);
    };

    // Estallidos a lo largo del casco, cada uno con su momento.
    for (std::uint32_t i = 0; i < 14; ++i) {
        const std::uint32_t h = Hash(i, 3, 41);
        const float born = Unit(h, 0) * 4.0f;
        const float life = static_cast<float>(age) - born;
        if (life < 0.0f || life > 1.3f) continue;
        const float fade = 1.0f - life / 1.3f;
        const Vec3f local{(Unit(h, 8) - 0.5f) * 0.6f, (Unit(h, 16) - 0.5f) * 0.4f, (Unit(h, 24) - 0.5f) * 1.2f};
        const float bright = 5.0f * fade * fade;
        flash(Vec3f(rel) + matrix.TransformDirection(local), 0.3f + life * 0.7f, {bright, bright * 0.62f, bright * 0.25f, 1.0f}, i);
    }
    // La explosion final, cuando cede el reactor.
    const float final = static_cast<float>(age) - 4.2f;
    if (final > 0.0f && final < 2.8f) {
        const float fade = std::exp(-final * 2.2f);
        flash(Vec3f(rel), 1.2f + final * 3.0f, {9.0f * fade, 7.0f * fade, 4.0f * fade, 1.0f}, 100);
    }

    NebulaInstance cloud;
    cloud.viewCenter = c.viewRotation.TransformDirection(Vec3f(rel));
    cloud.radius = 0.9f + static_cast<float>(age) * 0.7f;
    const float glow = 0.5f + 3.0f * heat;
    cloud.inner = {1.0f * glow, 0.5f * glow, 0.14f * glow, 1.0f};
    cloud.outer = {0.25f, 0.10f, 0.05f, 1.0f};
    cloud.density = 2.4f * std::max(1.0f - static_cast<float>(age) / 9.0f, 0.0f);
    cloud.seed = 7.0f;
    cloud.distance = Length(Vec3f(rel));
    out.nebulae.push_back(cloud);
}

// Al formarse la curvatura, el espacio se pliega delante de la nave. Es la misma lente del
// agujero negro: dobla de verdad lo que hay detras, y la burbuja crece y se traga la vista.
void GameLayer::DrawWarpLens(const FrameContext& c, FramePacket& out) const {
    const ShipState& ship = session_.Ship();
    const double charge = ship.warpCharge;
    if (charge <= 0.01 || charge >= 0.995) return;
    const float focal = c.camera.FocalPx();
    const float grown = static_cast<float>(charge * charge * charge);
    const Vec3f rel = Vec3f(ship.position + Heading(ship) * 26.0 - c.cameraPosition);

    BlackHoleInstance h;
    h.viewCenter = c.viewRotation.TransformDirection(rel);
    if (h.viewCenter.z <= 1.0f) return;
    h.schwarzschildRadius = 0.03f + 10.0f * grown;  // crece hasta tragarse la pantalla
    h.viewAxis = Normalize(c.viewRotation.TransformDirection(Vec3f(Heading(ship))));
    h.spin = 0.0f;
    h.discInner = 2.4f;
    h.discOuter = 7.0f;
    h.discBrightness = 0.0f;  // sin disco: solo la curvatura del espacio
    h.seed = 3.0f;
    h.reach = 60.0f;
    h.distance = Length(rel);
    h.pixelRadius = h.reach * h.schwarzschildRadius / h.distance * focal;
    out.blackHoles.push_back(h);
}

// El barrido se ve como una onda que sale de la nave, en dos frecuencias.
void GameLayer::DrawScanPulse(const FrameContext& c, FramePacket& out) const {
    if (!session_.Scanning()) return;
    const float focal = c.camera.FocalPx();
    const Vec3f rel(session_.Ship().position - c.cameraPosition);
    const Vec3f view = c.viewRotation.TransformDirection(rel);
    if (view.z <= 0.1f) return;
    const float clock = static_cast<float>(c.time);
    for (int i = 0; i < 3; ++i) {
        const float phase = std::fmod(clock * 0.45f + static_cast<float>(i) / 3.0f, 1.0f);
        const float fade = (1.0f - phase) * (1.0f - phase) * 0.9f;
        SpriteInstance s;
        s.viewPosition = view;
        s.pixelRadius = (0.8f + phase * 30.0f) / view.z * focal;
        s.tint = i % 2 == 0 ? Color{0.75f * fade, 0.35f * fade, 1.2f * fade, 1.0f} : Color{1.2f * fade, 0.62f * fade, 0.16f * fade, 1.0f};
        s.id = 0x74000000u + static_cast<std::uint32_t>(i);
        s.texture = ring_;
        s.blend = BlendMode::Additive;
        out.sprites.push_back(s);
    }
}

// --- Dano en 3D: solo con la calidad maxima ---------------------------------------------------

namespace {

constexpr float kCraterMinimum = 10.0f;  // m: radio del golpe mas pequeno
constexpr float kCraterGrowth = 220.0f;  // m por raiz de la parte del casco que se lleva

float Rand(std::uint32_t seed, std::uint32_t salt) { return Unit(Hash(seed, salt, 977), 0) * (255.0f / 256.0f) + Unit(Hash(seed, salt, 31), 8) / 256.0f; }

// Giro que lleva +Z a direction, con un alabeo alrededor de ella.
Quatf AlongZ(const Vec3f& direction, float roll) {
    const Vec3f z{0, 0, 1};
    const Vec3f d = Normalize(direction);
    const Vec3f axis = Cross(z, d);
    const float s = Length(axis), cosine = std::clamp(Dot(z, d), -1.0f, 1.0f);
    const Quatf turn = s > 1e-6f ? Quatf::FromAxisAngle(axis * (1.0f / s), std::atan2(s, cosine)) : (cosine > 0.0f ? Quatf{} : Quatf::FromAxisAngle({1, 0, 0}, 3.14159265f));
    return Quatf::FromAxisAngle(d, roll) * turn;
}

Vec3d ToDouble(const Vec3f& v) { return {v.x, v.y, v.z}; }

// Dos ejes perpendiculares a n.
void Tangents(const Vec3f& n, Vec3f& t1, Vec3f& t2) {
    t1 = Normalize(Cross(n, std::fabs(n.y) < 0.9f ? Vec3f{0, 1, 0} : Vec3f{1, 0, 0}));
    t2 = Cross(n, t1);
}

}  // namespace

// Rayo contra los triangulos de las piezas que siguen en la nave, en metros del modelo.
bool GameLayer::RayShip(const Vec3f& origin, const Vec3f& direction, int onlyZone, Vec3f& hit, Vec3f& normal, int& zone, int* spinOut) const {
    if (!assets_.ship) return false;
    const ShipState& ship = session_.Ship();
    float best = 1e30f;
    for (std::size_t i = 0; i < assets_.ship->parts.size() && i < partZones_.size(); ++i) {
        const ModelPart& p = assets_.ship->parts[i];
        const int z = partZones_[i];
        if (!p.collision || z < 0 || ZoneLost(ship.zones, z) || PieceLost(ship, static_cast<int>(i)) || (onlyZone >= 0 && z != onlyZone)) continue;
        // Una pieza que gira se mira en el marco de su rotor: ahi quedan su malla y el crater.
        const int spin = i < partSpin_.size() ? partSpin_[i] : 0;
        const Mat4f unspin = SpinOf(spin).Conjugate().ToMatrix();
        const Vec3f so = unspin.TransformDirection(origin), sd = unspin.TransformDirection(direction);
        const Mat4f back = p.rotation.Conjugate().ToMatrix();
        const float scale = std::max(p.scale, 1e-6f);
        const Vec3f o = back.TransformDirection(so - p.position) * (1.0f / scale);
        const Vec3f d = back.TransformDirection(sd);
        // Fuera de su esfera, ni se mira.
        const Vec3f toCenter = o * -1.0f;
        const float along = Dot(toCenter, d);
        if (Dot(toCenter, toCenter) - along * along > p.radius * p.radius * 1.1f) continue;
        const MeshData& mesh = *p.collision;
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const Vec3f a = mesh.vertices[mesh.indices[t]].position, b = mesh.vertices[mesh.indices[t + 1]].position, cc = mesh.vertices[mesh.indices[t + 2]].position;
            const Vec3f e1 = b - a, e2 = cc - a;
            const Vec3f pv = Cross(d, e2);
            const float det = Dot(e1, pv);
            if (std::fabs(det) < 1e-9f) continue;
            const float inv = 1.0f / det;
            const Vec3f tv = o - a;
            const float u = Dot(tv, pv) * inv;
            if (u < 0.0f || u > 1.0f) continue;
            const Vec3f qv = Cross(tv, e1);
            const float v = Dot(d, qv) * inv;
            if (v < 0.0f || u + v > 1.0f) continue;
            const float distance = Dot(e2, qv) * inv * scale;
            if (distance <= 0.0f || distance >= best) continue;
            best = distance;
            zone = z;
            hit = so + sd * distance;
            Vec3f n = Normalize(p.rotation.ToMatrix().TransformDirection(Cross(e1, e2)));
            if (Dot(n, sd) > 0.0f) n = n * -1.0f;
            normal = n;
            if (spinOut) *spinOut = spin;
        }
    }
    return best < 1e29f;
}

// Cada golpe nuevo se clava una vez en el casco: un rayo desde donde vino hasta la zona que recibio.
void GameLayer::SyncCraters() const {
    const auto& hits = session_.HullHits();
    for (auto it = craters_.begin(); it != craters_.end();) {
        const bool alive = std::any_of(hits.begin(), hits.end(), [&](const GameSession::HullHit& h) { return h.id == it->first; });
        it = alive ? std::next(it) : craters_.erase(it);
    }
    for (const GameSession::HullHit& h : hits) {
        if (craters_.count(h.id)) continue;
        Crater crater;
        crater.seed = static_cast<std::uint32_t>(h.id) * 2654435761u;
        const Vec3f from = Normalize(Vec3f(h.from) + Vec3f{0, 0, 1e-6f});
        // Cerca del centro de la zona, pero no siempre en el mismo punto. El centro del anillo es su
        // hueco: ahi se apunta al tramo del lado por el que llego el golpe.
        const Vec3f jitter{Rand(crater.seed, 1) - 0.5f, Rand(crater.seed, 2) - 0.5f, Rand(crater.seed, 3) - 0.5f};
        Vec3f target = Vec3f(ZoneInfo(h.zone).center);
        float spread = 80.0f;
        if (h.zone == static_cast<int>(HullZone::WarpRing)) {
            const float across = std::sqrt(from.x * from.x + from.y * from.y);
            const Vec3f side = across > 1e-3f ? Vec3f{from.x / across, from.y / across, 0.0f} : Vec3f{Rand(crater.seed, 4) - 0.5f, Rand(crater.seed, 5) - 0.5f, 0.0f};
            const ZoneLayout* layout = CustomZoneLayout();
            target = target + Normalize(side + Vec3f{0, 0, 1e-6f}) * (layout ? static_cast<float>(layout->ringRadius) : 230.0f);
            spread = 24.0f;
        }
        target = target + jitter * spread;
        // Un proyectil sabe donde dio: su trayectoria sigue hasta la superficie real del modelo.
        if (h.located) target = Vec3f(h.point);
        const Vec3f origin = target + from * 3000.0f;
        const Vec3f direction = Normalize(target - origin);
        Vec3f point, normal;
        int zone = -1, spin = 0;
        // Con la trayectoria, lo primero que encuentra en el modelo de verdad; sin ella, la zona que recibio.
        const bool found = h.located ? RayShip(origin, direction, -1, point, normal, zone, &spin) || RayShip(origin, direction, h.zone, point, normal, zone, &spin)
                                     : RayShip(origin, direction, h.zone, point, normal, zone, &spin) || RayShip(origin, direction, -1, point, normal, zone, &spin);
        if (found) {
            const float intensity = static_cast<float>(h.intensity);
            crater.valid = true;
            crater.zone = zone;
            crater.spin = spin;
            crater.phase = ringPhase_;
            crater.center = point;
            crater.normal = normal;
            crater.radius = (kCraterMinimum + kCraterGrowth * std::sqrt(intensity)) * (h.blunt ? 1.4f : 1.0f);
            crater.depth = std::clamp(0.35f + 4.0f * intensity, 0.35f, 1.0f) * (h.blunt ? 0.45f : 1.0f);
        }
        craters_[h.id] = crater;
    }
}

// Los crateres que tocan esta pieza, en sus propias unidades: los cuatro mas grandes.
void GameLayer::PartCraters(const ModelPart& p, int zone, int spin, MeshInstance& m, FramePacket& out) const {
    if (zone < 0 || craters_.empty()) return;
    struct Near {
        const Crater* crater;
        float heat, size;
    };
    std::vector<Near> near;
    const float reach = p.radius * p.scale;
    for (const GameSession::HullHit& h : session_.HullHits()) {
        const auto found = craters_.find(h.id);
        if (found == craters_.end() || !found->second.valid || ZoneLost(session_.Ship().zones, found->second.zone)) continue;
        const Crater& k = found->second;
        // Un crater del anillo esta en el marco del rotor: no toca lo que va fijo, ni al reves.
        if (k.spin != spin) continue;
        if (Length(k.center - p.position) > reach + k.radius * 1.35f) continue;
        near.push_back({&k, static_cast<float>(std::exp(-h.age / 12.0)), k.radius * k.depth});
    }
    if (near.empty()) return;
    std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.size > b.size; });
    const Mat4f back = p.rotation.Conjugate().ToMatrix();
    const float inv = 1.0f / std::max(p.scale, 1e-6f);
    m.craterFirst = static_cast<std::uint32_t>(out.craters.size());
    m.craterCount = static_cast<std::uint32_t>(std::min<std::size_t>(near.size(), kMaxMeshCraters));
    m.doubleSided = true;
    for (std::uint32_t i = 0; i < m.craterCount; ++i) {
        const Crater& k = *near[i].crater;
        MeshCrater mc;
        mc.center = back.TransformDirection(k.center - p.position) * inv;
        mc.radius = k.radius * inv;
        mc.depth = k.depth;
        mc.heat = near[i].heat;
        mc.seed = static_cast<float>(k.seed % 97u);
        out.craters.push_back(mc);
    }
}

// Lo que asoma de cada crater: vigas en I dobladas y chapas abiertas como petalos, al rojo al
// principio. Va pegado a la nave, con su giro.
void GameLayer::DrawCraterDetail(const FrameContext& c, FramePacket& out, const Vec3d& position, const Quatd& rotation, const SceneLight& light) const {
    if (assets_.girder == kNoMesh) return;
    const Vec3d rel = position - c.cameraPosition;
    const Quatf toWorld = Quatf::Cast(rotation);
    const Mat4f matrix = toWorld.ToMatrix();
    const Quatf toView = c.camera.Orientation().Conjugate() * toWorld;
    const Color steel{0.30f, 0.30f, 0.33f, 1.0f}, hot{1.0f, 0.42f, 0.14f, 1.0f};
    std::uint32_t index = 0;
    for (const GameSession::HullHit& h : session_.HullHits()) {
        const auto found = craters_.find(h.id);
        if (found == craters_.end() || !found->second.valid || ZoneLost(session_.Ship().zones, found->second.zone)) continue;
        const Crater& k = found->second;
        const float heat = static_cast<float>(std::exp(-h.age / 10.0));
        // Lo que asoma de un crater del anillo gira con el.
        const Mat4f turn = SpinOf(k.spin).ToMatrix();
        const Vec3f center = turn.TransformDirection(k.center), normal = turn.TransformDirection(k.normal);
        Vec3f t1, t2;
        Tangents(normal, t1, t2);
        const bool hole = k.depth > 0.4f;
        const int count = 2 + static_cast<int>(k.depth * 6.0f) + (hole ? 3 : 0);
        for (int j = 0; j < count; ++j) {
            const std::uint32_t s = k.seed + static_cast<std::uint32_t>(j) * 7919u;
            const float angle = Rand(s, 11) * 6.2831853f;
            const Vec3f around = t1 * std::cos(angle) + t2 * std::sin(angle);
            const bool petal = hole && j % 3 == 2;
            // Las vigas nacen en el fondo y asoman por el borde; las chapas se abren desde el labio.
            const float ring = petal ? 0.45f + 0.25f * Rand(s, 12) : 0.2f + 0.55f * Rand(s, 12);
            const Vec3f base = center + around * (k.radius * ring) - normal * (k.radius * (petal ? 0.05f : 0.15f));
            const Vec3f out1 = petal ? around * 0.9f + normal * 0.5f : normal * (0.7f + 0.3f * Rand(s, 13)) + around * (0.25f + 0.6f * Rand(s, 14));
            const Vec3f tilt{Rand(s, 15) - 0.5f, Rand(s, 16) - 0.5f, Rand(s, 17) - 0.5f};
            const Vec3f direction = Normalize(out1 + tilt * 0.35f);
            const float length = k.radius * (petal ? 0.3f + 0.15f * Rand(s, 18) : 0.22f + 0.35f * Rand(s, 18));
            const Vec3f at = Vec3f(rel) + matrix.TransformDirection(base * kMetre);
            if (!c.frustum.SphereVisible(at, length * kMetre)) continue;
            MeshInstance m;
            m.viewPosition = c.viewRotation.TransformDirection(at);
            m.scale = length * kMetre;
            m.rotation = toView * AlongZ(direction, Rand(s, 19) * 6.2831853f);
            ApplyLight(m, light);
            const float glow = heat * (0.4f + 0.6f * Rand(s, 20));
            m.color = {steel.r + (hot.r - steel.r) * glow * 0.6f, steel.g + (hot.g - steel.g) * glow * 0.6f, steel.b + (hot.b - steel.b) * glow * 0.6f, 1.0f};
            m.emissive = glow * 1.2f;
            m.damage = 0.55f;
            m.damageHeat = heat;
            m.damageDetail = 2;
            m.damageSeed = static_cast<float>(s % 64u);
            m.mesh = petal ? assets_.plate : assets_.girder;
            m.id = 0x78000000u + (index++ & 0xFFFFFFu);
            out.meshes.push_back(m);
        }
    }
}

// Escombros: lo que cada golpe arranca sale despedido del crater y se queda atras, girando. Las
// chispas y el gas que escapa por un agujero duran unos segundos.
void GameLayer::DrawDebris(const FrameContext& c, FramePacket& out) const {
    const Vec3d origin = session_.GetWorld().origin;
    const double now = session_.SimTime();
    const float focal = c.camera.FocalPx();
    std::uint32_t index = 0, sparkIndex = 0;
    for (const GameSession::HullHit& h : session_.HullHits()) {
        if (h.age > 40.0) continue;
        const auto found = craters_.find(h.id);
        if (found == craters_.end() || !found->second.valid) continue;
        const Crater& k = found->second;
        const auto turn = h.orientation.ToMatrix();
        // En el anillo, el crater estaba donde lo tenia el giro en el momento del golpe.
        const Mat4f spun = SpinOf(k.spin, k.phase).ToMatrix();
        const Vec3d normal = Normalize(turn.TransformDirection(ToDouble(spun.TransformDirection(k.normal))));
        // Donde estaba el crater en el momento del golpe, y como se ha movido la nave desde entonces.
        const Vec3d start = h.absolute + turn.TransformDirection(ToDouble(spun.TransformDirection(k.center)) * 0.001) - origin + h.velocity * (now - h.time);
        const float age = static_cast<float>(h.age);
        const float intensity = static_cast<float>(h.intensity);
        const SceneLight light = LightAt(c, start);
        const int chunks = std::min(8 + static_cast<int>(50.0f * std::sqrt(intensity)), 32);
        const float shrink = std::clamp(1.0f - (age - 25.0f) / 15.0f, 0.0f, 1.0f);
        for (int j = 0; j < chunks && shrink > 0.0f && !h.blunt; ++j) {
            const std::uint32_t s = k.seed + static_cast<std::uint32_t>(j) * 104729u;
            const Vec3d spread{Rand(s, 21) - 0.5, Rand(s, 22) - 0.5, Rand(s, 23) - 0.5};
            const Vec3d direction = Normalize(normal * 1.1 + spread * 1.6);
            const double speed = 0.008 + 0.05 * Rand(s, 24);  // km/s
            const Vec3d at = start + direction * (speed * age) - c.cameraPosition;
            // Casi todo son trozos de uno a pocos metros; alguno llega a la decena. La nave mide 1,1 km.
            const float r = Rand(s, 25);
            const float size = (1.0f + 3.0f * r + 6.0f * r * r * r * r) * (0.8f + 1.5f * intensity) * shrink;
            const Vec3f view = c.viewRotation.TransformDirection(Vec3f(at));
            if (!c.frustum.SphereVisible(Vec3f(at), size * kMetre)) continue;
            MeshInstance m;
            m.viewPosition = view;
            m.scale = size * kMetre;
            const Vec3f axis = Normalize(Vec3f{Rand(s, 26) - 0.5f, Rand(s, 27) - 0.5f, Rand(s, 28) - 0.5f} + Vec3f{0, 0, 1e-3f});
            m.rotation = c.camera.Orientation().Conjugate() * Quatf::FromAxisAngle(axis, age * (0.5f + 2.5f * Rand(s, 29)) + Rand(s, 30) * 6.28f);
            ApplyLight(m, light);
            const float glow = std::exp(-age / 2.5f) * (0.5f + Rand(s, 31));
            m.color = {0.32f + 0.6f * glow, 0.30f + 0.2f * glow, 0.32f, 1.0f};
            m.emissive = glow * 1.5f;
            const int kind = j % 4;
            m.mesh = kind == 0 ? assets_.girder : kind == 1 ? assets_.plate : kind == 2 || assets_.debris.empty() ? assets_.shard : assets_.debris[s % assets_.debris.size()];
            m.id = 0x79000000u + (index++ & 0xFFFFFFu);
            out.meshes.push_back(m);
        }

        // Chispas al principio, y en un agujero, el aire que escapa en un chorro blanco.
        const auto glowAt = [&](const Vec3d& where, float km, const Color& tint) {
            const Vec3f view = c.viewRotation.TransformDirection(Vec3f(where - c.cameraPosition));
            if (view.z <= 0.05f) return;
            SpriteInstance sp;
            sp.viewPosition = view;
            sp.pixelRadius = std::max(km / view.z * focal, 1.5f);
            sp.tint = tint;
            sp.id = 0x7A000000u + (sparkIndex++ & 0xFFFFFFu);
            sp.texture = glow_;
            sp.blend = BlendMode::Additive;
            sp.fogReach = 50.0f;
            out.sprites.push_back(sp);
        };
        if (age < 2.5f) {
            for (int j = 0; j < 10; ++j) {
                const std::uint32_t s = k.seed + static_cast<std::uint32_t>(j) * 31337u;
                const Vec3d direction = Normalize(normal + Vec3d{Rand(s, 41) - 0.5, Rand(s, 42) - 0.5, Rand(s, 43) - 0.5} * 1.8);
                const float fade = 1.0f - age / 2.5f;
                glowAt(start + direction * (0.15 * age * (0.5 + Rand(s, 44))), 0.006f, Color{3.0f * fade, 1.6f * fade, 0.5f * fade, 1.0f});
            }
            glowAt(start, k.radius * kMetre * (1.0f + age), Color{2.4f * (1.0f - age / 2.5f), 1.0f * (1.0f - age / 2.5f), 0.3f * (1.0f - age / 2.5f), 1.0f});
        }
        if (k.depth > 0.4f && age < 9.0f && !h.blunt) {
            const float fade = 1.0f - age / 9.0f;
            for (int j = 0; j < 4; ++j) {
                const float t = std::fmod(age * 1.3f + static_cast<float>(j) * 0.25f, 1.0f);
                glowAt(start + normal * (0.01 + static_cast<double>(t * k.radius) * 0.0015), k.radius * kMetre * (0.3f + t * 0.9f), Color{0.55f * fade * (1.0f - t), 0.62f * fade * (1.0f - t), 0.75f * fade * (1.0f - t), 1.0f});
            }
        }
    }
}

// Nave apagada: las luces parpadean y mueren, y va a la deriva girando. Solo queda la baliza.
void GameLayer::DrawAdrift(const FrameContext& c, FramePacket& out) const {
    const float age = static_cast<float>(session_.WreckAge());
    float lights = 0.0f;
    if (age < 0.4f) lights = 1.0f;
    else if (age < 3.2f) lights = (Unit(Hash(static_cast<std::int64_t>(age * 14.0f), 5, 9), 0) > 0.45f ? 1.0f : 0.1f) * (1.0f - (age - 0.4f) / 2.8f);
    shipLights_ = lights;
    const ShipState& ship = session_.Ship();
    DrawModel(c, out, *assets_.ship, ship.position, ship.orientation, -1, 0);
    DrawModules(c, out, ship.position, ship.orientation, LightAt(c, ship.position));
    shipLights_ = 1.0f;
    if (hullDetail_ >= 3) {
        DrawCraterDetail(c, out, ship.position, ship.orientation, LightAt(c, ship.position));
        DrawDebris(c, out);
    }
    // La baliza de socorro, en lo alto del puente: un destello rojo cada segundo y medio.
    if (age > 4.0f && std::fmod(age, 1.5f) < 0.18f) {
        // Sobre el puente: el de la maqueta, o el centro del puente de la nave cargada, un poco por encima.
        const Vec3d beacon = CustomZoneLayout() ? ZoneInfo(static_cast<int>(HullZone::Bridge)).center + Vec3d{0, -60, 0} : Vec3d{0, -110, 360};
        const Vec3f view = c.viewRotation.TransformDirection(Vec3f(ship.position - c.cameraPosition) + Vec3f(ship.orientation.ToMatrix().TransformDirection(beacon * 0.001)));
        if (view.z > 0.05f) {
            SpriteInstance s;
            s.viewPosition = view;
            s.pixelRadius = std::max(0.05f / view.z * c.camera.FocalPx(), 4.0f);
            s.tint = {4.0f, 0.3f, 0.2f, 1.0f};
            s.id = 0x7B000000u;
            s.texture = glow_;
            s.blend = BlendMode::Additive;
            s.fogReach = 1.0e9f;
            out.sprites.push_back(s);
        }
    }
}

// --- Modulos y mejoras visibles -----------------------------------------------------------------

namespace {

// Giro que lleva +Y a up y deja +Z lo mas cerca posible de forward.
Quatf UpAndForward(const Vec3f& up, const Vec3f& forward) {
    const Vec3f y{0, 1, 0};
    const Vec3f u = Normalize(up);
    const Vec3f axis = Cross(y, u);
    const float s = Length(axis), cosine = std::clamp(Dot(y, u), -1.0f, 1.0f);
    const Quatf lift = s > 1e-6f ? Quatf::FromAxisAngle(axis * (1.0f / s), std::atan2(s, cosine)) : (cosine > 0.0f ? Quatf{} : Quatf::FromAxisAngle({1, 0, 0}, 3.14159265f));
    const Vec3f f0 = lift.ToMatrix().TransformDirection(Vec3f{0, 0, 1});
    Vec3f f = forward - u * Dot(forward, u);
    if (Length(f) < 1e-4f) return lift;
    f = Normalize(f);
    const float roll = std::atan2(Dot(Cross(f0, f), u), Dot(f0, f));
    return Quatf::FromAxisAngle(u, roll) * lift;
}

}  // namespace

// --- Luz relativista, estela, propulsores de maniobra y ducto de atraque -------------------------

// Cerca de la luz y mas alla, la luz de las estrellas se amontona delante (aberracion) y se vuelve
// azul (Doppler): la nave queda iluminada desde hacia donde avanza, y por detras solo llega un rojo
// tenue. Por debajo de 0,2 c no cambia nada; a partir de c, del todo.
SceneLight GameLayer::ShipLight(const FrameContext& c, SceneLight light) const {
    const ShipState& ship = session_.Ship();
    const double beta = ship.speed / kLightSpeed;
    const float t = static_cast<float>(std::clamp((beta - 0.2) / 0.8, 0.0, 1.0));
    if (t <= 0.0f) return light;
    const float w = t * t * (3.0f - 2.0f * t);
    const Vec3f ahead = Normalize(c.viewRotation.TransformDirection(Vec3f(ship.moveDirection)));
    light.direction = Normalize(light.direction * (1.0f - w) + ahead * w + Vec3f{0, 0, 1e-6f});
    const float bright = 1.0f + 2.5f * w;
    const auto mix = [&](float a, float b) { return a * (1.0f - w) + b * w; };
    light.color = {mix(light.color.r, 0.75f * bright), mix(light.color.g, 0.9f * bright), mix(light.color.b, 1.35f * bright), 1.0f};
    light.direction2 = ahead * -1.0f;
    light.color2 = {mix(light.color2.r, 0.35f), mix(light.color2.g, 0.06f), mix(light.color2.b, 0.04f), 1.0f};
    light.ambient *= 1.0f - 0.7f * w;
    return light;
}

// Estela del motor de impulso: sale de cada escape hacia atras y cambia con el motor montado. El
// ionico de serie deja un chorro azul tenue y largo; el de alto flujo, mas denso y cian; el de plasma,
// pulsos violetas que se alejan; el de fusion, un nucleo blanco con discos de choque y un penacho largo.
void GameLayer::DrawExhaust(const FrameContext& c, FramePacket& out) const {
    const ShipState& ship = session_.Ship();
    const double push = ship.warpSpeed <= session_.Rules().drive.impulseSpeed ? std::max(ship.throttle, 0.0) : 0.0;
    if (push <= 0.01 || !assets_.ship || partZones_.size() != assets_.ship->parts.size()) return;
    const int tier = std::clamp(ship.loadout.engine, 1, kImpulseEngineCount);
    const ImpulseEngineSpec& engine = ImpulseEngine(tier);
    const float focal = c.camera.FocalPx();
    const Mat4f matrix = Quatf::Cast(ship.orientation).ToMatrix();
    const Vec3f back = Normalize(matrix.TransformDirection(Vec3f{0, 0, -1}));
    const Vec3f rel(ship.position - c.cameraPosition);
    const float lengths[] = {260.0f, 380.0f, 520.0f, 760.0f};
    const float density[] = {0.35f, 0.65f, 0.8f, 1.0f};
    const float length = lengths[tier - 1] * static_cast<float>(0.4 + 0.6 * push);
    const float time = static_cast<float>(c.time);
    std::uint32_t id = 0;
    const auto glow = [&](const Vec3f& at, float metres, const Color& tint) {
        const Vec3f view = c.viewRotation.TransformDirection(at);
        if (view.z <= 0.05f) return;
        SpriteInstance s;
        s.viewPosition = view;
        s.pixelRadius = std::max(metres * kMetre / view.z * focal, 1.0f);
        s.tint = tint;
        s.id = 0x7D000000u + id++;
        s.texture = glow_;
        s.blend = BlendMode::Additive;
        s.fogReach = 50.0f;
        out.sprites.push_back(s);
    };
    for (std::size_t i = 0; i < assets_.ship->parts.size(); ++i) {
        const ModelPart& p = assets_.ship->parts[i];
        if (p.name.find("escape") == std::string::npos) continue;
        if (ZoneLost(ship.zones, partZones_[i]) || PieceLost(ship, static_cast<int>(i))) continue;
        const float radius = p.radius * p.scale * 0.7f;
        const Vec3f nozzle = rel + matrix.TransformDirection(p.position * kMetre) + back * (radius * kMetre * 0.4f);
        const float strength = static_cast<float>(push) * density[tier - 1] * std::clamp(static_cast<float>(Zone(ship.zones, static_cast<HullZone>(partZones_[i]))) * 1.4f, 0.2f, 1.0f);
        constexpr int kSteps = 16;
        for (int k = 0; k < kSteps; ++k) {
            const float u = (static_cast<float>(k) + 0.5f) / kSteps;
            const float along = u * length;
            float bright = std::exp(-u * 3.0f) * strength;
            if (tier == 3) bright *= 0.45f + 0.55f * std::max(std::sin(along * 0.05f - time * 18.0f), 0.0f);  // pulsos que se alejan
            const float spread = radius * (0.7f + (tier == 1 ? 0.6f : 1.4f) * u);
            const Color tint{engine.glow[0] * bright * 1.6f, engine.glow[1] * bright * 1.6f, engine.glow[2] * bright * 1.6f, 1.0f};
            glow(nozzle + back * (along * kMetre), spread * 1.3f, tint);
        }
        // Fusion: nucleo blanco y discos de choque cada vez mas tenues.
        if (tier >= 3) {
            glow(nozzle, radius * 1.1f, Color{2.5f * strength, 2.4f * strength, 2.2f * strength, 1.0f});
            for (int d = 1; d <= (tier == 4 ? 5 : 3); ++d) {
                const float fade = strength * std::exp(-static_cast<float>(d) * 0.45f);
                glow(nozzle + back * (static_cast<float>(d) * radius * 2.2f * kMetre), radius * 0.55f, Color{2.2f * fade, 2.0f * fade, 1.7f * fade, 1.0f});
            }
        }
    }
}

// Propulsores de maniobra: rendijas de iones a babor, estribor, arriba y abajo, en la proa y en la
// popa. Al girar se encienden las que dan par hacia donde gira, con un soplo azul corto. Sin gas
// (la nave gira con el giroscopio) se quedan apagadas.
void GameLayer::DrawThrusters(const FrameContext& c, FramePacket& out, const SceneLight& light) const {
    if (assets_.rcsSlit == kNoMesh) return;
    const ShipState& ship = session_.Ship();
    const Quatf toWorld = Quatf::Cast(ship.orientation);
    const Mat4f matrix = toWorld.ToMatrix();
    const Quatf toView = c.camera.Orientation().Conjugate() * toWorld;
    const Vec3f rel(ship.position - c.cameraPosition);
    const Vec3f alpha = toWorld.Conjugate().ToMatrix().TransformDirection(Vec3f(ship.angularPush));
    const float push = std::clamp(Length(alpha) / 0.05f, 0.0f, 1.0f);
    const ZoneLayout* layout = CustomZoneLayout();
    const float reach = layout ? static_cast<float>(layout->extent.z) : 540.0f;
    const float focal = c.camera.FocalPx();
    const float time = static_cast<float>(c.time);
    const Vec3f sides[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    std::uint32_t id = 0;
    for (int end = 0; end < 2; ++end) {
        const Vec3f target{0, 0, (end == 0 ? 0.62f : -0.62f) * reach};
        for (int k = 0; k < 4; ++k) {
            Vec3f at, normal;
            if (!UpgradeMount(600 + end * 4 + k, -1, target, sides[k], at, normal)) continue;
            // Empuja hacia dentro (contra su normal): da par r x (-n).
            const Vec3f torque = Cross(at, normal * -1.0f);
            const float fire = Length(torque) > 1e-3f && push > 0.0f ? std::clamp(Dot(Normalize(torque), Normalize(alpha + Vec3f{0, 0, 1e-9f})), 0.0f, 1.0f) * push : 0.0f;
            const Vec3f where = rel + matrix.TransformDirection(at * kMetre);
            MeshInstance m;
            m.viewPosition = c.viewRotation.TransformDirection(where);
            m.scale = 2.5f * kMetre;  // rendijas de unos 45 m: la nave mide mas de un kilometro
            m.rotation = toView * UpAndForward(normal, Vec3f{0, 0, 1});
            ApplyLight(m, light);
            m.color = {0.55f, 0.75f, 1.0f, 1.0f};
            m.emissive = (0.25f + 3.0f * fire) * shipLights_;
            m.mesh = assets_.rcsSlit;
            m.id = 0x7E000000u + id++;
            m.foreground = true;
            out.meshes.push_back(m);
            if (fire < 0.05f) continue;
            const Vec3f outward = Normalize(matrix.TransformDirection(normal));
            for (int j = 0; j < 4; ++j) {
                const float flicker = 0.75f + 0.25f * std::sin(time * 40.0f + static_cast<float>(j * 3 + k));
                const float d = (18.0f + 40.0f * static_cast<float>(j)) * (0.5f + fire);
                const Vec3f view = c.viewRotation.TransformDirection(where + outward * (d * kMetre));
                if (view.z <= 0.05f) continue;
                const float b = fire * flicker * std::exp(-static_cast<float>(j) * 0.6f);
                SpriteInstance s;
                s.viewPosition = view;
                s.pixelRadius = std::max((14.0f + 14.0f * static_cast<float>(j)) * kMetre / view.z * focal, 1.5f);
                s.tint = {1.0f * b, 1.25f * b, 2.4f * b, 1.0f};
                s.id = 0x7E800000u + id * 8u + static_cast<std::uint32_t>(j);
                s.texture = glow_;
                s.blend = BlendMode::Additive;
                s.fogReach = 50.0f;
                out.sprites.push_back(s);
            }
        }
    }
}

// Ducto de atraque origami: anillos octogonales que se despliegan desde la compuerta de la nave hasta
// la escotilla. Cada anillo va girado respecto al anterior y los paneles que los unen se pliegan en
// zigzag, como un tubo de Kresling: plegado ocupa casi nada y al estirarse se abre en facetas.
void GameLayer::DrawDockTube(const FrameContext& c, FramePacket& out) const {
    const GameSession::DockTube tube = session_.Tube();
    if (!tube.active || tube.extension <= 0.0 || assets_.unitBox == kNoMesh) return;
    const Vec3d axis = tube.stationEnd - tube.shipEnd;
    const double length = Length(axis);
    if (length < 1e-6) return;
    const Vec3d dir = axis * (1.0 / length);
    Vec3d u = Cross(dir, Vec3d{0, 1, 0});
    if (Length(u) < 1e-6) u = Cross(dir, Vec3d{1, 0, 0});
    u = Normalize(u);
    const Vec3d v = Cross(dir, u);
    const double ext = tube.extension * tube.extension * (3.0 - 2.0 * tube.extension);
    const SceneLight light = LightAt(c, tube.shipEnd);
    const Quatf toView = c.camera.Orientation().Conjugate();
    constexpr int kRings = 12, kSides = 8;
    constexpr double kRadius = 0.022;  // km
    std::uint32_t id = 0;
    const auto bar = [&](const Vec3d& a, const Vec3d& b, double width, double thick, const Color& color, float emissive) {
        const Vec3d mid = (a + b) * 0.5;
        const double len = Length(b - a);
        if (len < 1e-7) return;
        const Vec3f rel(mid - c.cameraPosition);
        if (!c.frustum.SphereVisible(rel, static_cast<float>(len))) return;
        MeshInstance m;
        m.viewPosition = c.viewRotation.TransformDirection(rel);
        m.scale = static_cast<float>(len);
        m.stretchX = static_cast<float>(width / len);
        m.stretchY = static_cast<float>(thick / len);
        // Caja de lado 1 a lo largo de su Z, con su X hacia fuera del tubo para que las facetas se vean.
        const Vec3f along = Normalize(Vec3f(b - a));
        const Vec3f outward = Normalize(Vec3f(mid - (tube.shipEnd + dir * Dot(mid - tube.shipEnd, dir))) + Vec3f{0, 0, 1e-6f});
        const float roll = std::atan2(Dot(Cross(AlongZ(along, 0.0f).ToMatrix().TransformDirection(Vec3f{0, 1, 0}), outward), along), Dot(AlongZ(along, 0.0f).ToMatrix().TransformDirection(Vec3f{0, 1, 0}), outward));
        m.rotation = toView * AlongZ(along, roll);
        ApplyLight(m, light);
        m.color = color;
        m.emissive = emissive;
        m.mesh = assets_.unitBox;
        m.id = 0x7F000000u + id++;
        out.meshes.push_back(m);
    };
    const auto vertex = [&](int ring, int side) {
        // Desplegado de la nave a la estacion: solo la parte ya abierta.
        const double along = length * ext * static_cast<double>(ring) / kRings;
        const double twist = static_cast<double>(ring) * (3.14159265 / kSides) * (1.0 - 0.5 * ext);
        const double r = kRadius * (ring % 2 == 0 ? 1.0 : 0.84 + 0.1 * (1.0 - ext));
        const double a = static_cast<double>(side) * 2.0 * 3.14159265 / kSides + twist;
        return tube.shipEnd + dir * along + (u * std::cos(a) + v * std::sin(a)) * r;
    };
    const Color frame{0.35f, 0.37f, 0.42f, 1.0f}, panel{0.82f, 0.84f, 0.88f, 1.0f};
    for (int ring = 0; ring <= kRings; ++ring) {
        for (int side = 0; side < kSides; ++side) {
            const Vec3d a = vertex(ring, side), b = vertex(ring, (side + 1) % kSides);
            bar(a, b, 0.0025, 0.0025, frame, ring % 3 == 0 ? 0.9f : 0.15f);  // un anillo de luces cada tres
            if (ring == kRings) continue;
            // Las facetas: dos triangulos por lado, en zigzag entre este anillo y el siguiente.
            const Vec3d c0 = vertex(ring + 1, side), c1 = vertex(ring + 1, (side + 1) % kSides);
            bar(a, c1, Length(b - a) * 0.6, 0.0008, panel, 0.0f);
            bar(b, c1, 0.0012, 0.0012, frame, 0.0f);
            (void)c0;
        }
    }
    // Collar en la escotilla cuando llega.
    if (tube.extension >= 0.999) {
        for (int side = 0; side < kSides; ++side) {
            const double a0 = side * 2.0 * 3.14159265 / kSides, a1 = (side + 1) * 2.0 * 3.14159265 / kSides;
            const Vec3d p0 = tube.stationEnd + (u * std::cos(a0) + v * std::sin(a0)) * kRadius * 1.25;
            const Vec3d p1 = tube.stationEnd + (u * std::cos(a1) + v * std::sin(a1)) * kRadius * 1.25;
            bar(p0, p1, 0.005, 0.004, Color{0.3f, 0.9f, 0.5f, 1.0f}, 1.5f);
        }
    }
}

// Donde va cada mejora visible: un rayo desde fuera la posa sobre la superficie de verdad de la
// zona, sea la maqueta o un .glb. Se calcula una vez por sitio.
bool GameLayer::UpgradeMount(int key, int zone, const Vec3f& target, const Vec3f& from, Vec3f& at, Vec3f& normal) const {
    const auto found = upgradeMounts_.find(key);
    if (found != upgradeMounts_.end()) {
        at = found->second.first;
        normal = found->second.second;
        return true;
    }
    int hitZone = -1;
    const Vec3f origin = target + from * 4000.0f;
    if (!RayShip(origin, from * -1.0f, zone, at, normal, hitZone)) return false;
    upgradeMounts_[key] = {at, normal};
    return true;
}

// Los modulos exteriores montados, en sus ranuras, y las mejoras que se ven: antenas del escaner en
// la espina, cupulas del computador en el puente, propulsores de maniobra y escudo termico en la proa.
void GameLayer::DrawModules(const FrameContext& c, FramePacket& out, const Vec3d& position, const Quatd& rotation, const SceneLight& light) const {
    const ShipState& ship = session_.Ship();
    const Vec3d rel = position - c.cameraPosition;
    if (Length(rel) > kModelRange) return;
    const Quatf toWorld = Quatf::Cast(rotation);
    const Mat4f matrix = toWorld.ToMatrix();
    const Quatf toView = c.camera.Orientation().Conjugate() * toWorld;
    const Color metal{0.62f, 0.64f, 0.68f, 1.0f}, dark{0.30f, 0.31f, 0.34f, 1.0f};
    std::uint32_t index = 0;
    const auto place = [&](MeshId mesh, const Vec3f& at, const Vec3f& up, float scale, const Color& color, float emissive) {
        if (mesh == kNoMesh) return;
        const Vec3f where = Vec3f(rel) + matrix.TransformDirection(at * kMetre);
        if (!c.frustum.SphereVisible(where, 90.0f * scale * kMetre)) return;
        MeshInstance m;
        m.viewPosition = c.viewRotation.TransformDirection(where);
        m.scale = scale * kMetre;
        m.rotation = toView * UpAndForward(up, Vec3f{0, 0, 1});
        ApplyLight(m, light);
        m.color = color;
        m.emissive = emissive * shipLights_;
        m.mesh = mesh;
        m.id = 0x7C000000u + index++;
        m.foreground = true;
        out.meshes.push_back(m);
    };

    const ShipDesign& design = CurrentShipDesign();
    for (std::size_t i = 0; i < design.slots.size() && i < ship.loadout.modules.size(); ++i) {
        const ShipModuleKind kind = ship.loadout.modules[i];
        const ModuleSlot& s = design.slots[i];
        if (kind == ShipModuleKind::None || s.kind != SlotKind::External || !SlotWorking(ship, static_cast<int>(i))) continue;
        place(assets_.moduleMeshes[static_cast<std::size_t>(kind)], Vec3f(s.position), Vec3f(s.normal), 1.6f, kind == ShipModuleKind::ArmorPlate ? dark : metal, 0.0f);
    }

    const ZoneLayout* layout = CustomZoneLayout();
    const auto center = [&](HullZone z) { return layout ? Vec3f(layout->centers[static_cast<std::size_t>(z)]) : Vec3f(ZoneInfo(static_cast<int>(z)).center); };
    const auto reach = [&](HullZone z, float fallback) { return layout && layout->bounds[static_cast<std::size_t>(z)].radius > 0.0 ? static_cast<float>(layout->bounds[static_cast<std::size_t>(z)].radius) : fallback; };
    Vec3f at, normal;
    // Escaner: una antena por nivel, a lo largo de la espina, por encima (arriba es -Y).
    if (!ZoneLost(ship.zones, static_cast<int>(HullZone::Spine))) {
        const Vec3f spine = center(HullZone::Spine);
        const float r = reach(HullZone::Spine, 450.0f);
        const float along[] = {-0.25f, 0.25f, -0.5f, 0.5f};
        for (int k = 0; k < std::min(ship.scannerTier - 1, 4); ++k)
            if (UpgradeMount(100 + k, static_cast<int>(HullZone::Spine), spine + Vec3f{0, 0, along[k] * r}, Vec3f{0, -1, 0}, at, normal)) place(assets_.moduleMeshes[static_cast<std::size_t>(ShipModuleKind::SensorArray)], at, normal, 0.8f, metal, 0.0f);
    }
    // Computador: cupulas sobre el puente.
    if (Zone(ship.zones, HullZone::Bridge) > 0.0) {
        const Vec3f bridge = center(HullZone::Bridge);
        const float r = reach(HullZone::Bridge, 90.0f);
        const float side[] = {0.0f, -0.35f, 0.35f};
        for (int k = 0; k < std::min(ship.computerTier - 1, 3); ++k)
            if (UpgradeMount(200 + k, static_cast<int>(HullZone::Bridge), bridge + Vec3f{side[k] * r, 0, -0.2f * r}, Vec3f{0, -1, 0}, at, normal)) place(assets_.dome, at, normal, 1.0f, Color{0.75f, 0.82f, 0.9f, 1.0f}, 0.25f);
    }
    // Maniobra: cuatro propulsores por nivel, a los lados de la proa y del nucleo.
    for (int k = 0; k < std::min(ship.handlingTier, 3); ++k) {
        for (int j = 0; j < 4; ++j) {
            const HullZone zone = j < 2 ? HullZone::Bow : HullZone::Core;
            if (ZoneLost(ship.zones, static_cast<int>(zone))) continue;
            const float side = j % 2 == 0 ? -1.0f : 1.0f;
            const Vec3f target = center(zone) + Vec3f{0, (static_cast<float>(k) - 1.0f) * 0.3f * reach(zone, 60.0f), 0};
            if (UpgradeMount(300 + k * 4 + j, static_cast<int>(zone), target, Vec3f{side, 0, 0}, at, normal)) place(assets_.thrusterPod, at, normal, 1.2f, dark, 0.0f);
        }
    }
    // Blindaje termico: un escudo abombado mas por nivel, en la punta de la proa.
    if (!ZoneLost(ship.zones, static_cast<int>(HullZone::Bow)) && ship.heatShieldTier > 1 && UpgradeMount(400, static_cast<int>(HullZone::Bow), center(HullZone::Bow), Vec3f{0, 0, 1}, at, normal)) {
        const float r = reach(HullZone::Bow, 70.0f);
        for (int k = 0; k < std::min(ship.heatShieldTier - 1, 3); ++k)
            place(assets_.heatCap, at + normal * (2.0f + 4.0f * static_cast<float>(k)), normal, r * (0.45f - 0.08f * static_cast<float>(k)), Color{0.45f, 0.30f, 0.22f, 1.0f}, 0.0f);
    }
}

void GameLayer::Collect(const FrameContext& c, FramePacket& out) {
    // A pie, dentro de la cubierta: es una caja cerrada, y lo de fuera ni se ve ni se dibuja.
    if (session_.OnFoot()) {
        const Vec3d origin = session_.DeckOrigin();
        DrawModel(c, out, *(session_.DeckKind() == StationKind::Mining ? assets_.miningDeck : assets_.powerDeck), origin, Quatd{}, -1, 1u);
        std::uint32_t id = 2;
        const auto person = [&](const ModelAsset& model, const HumanoidState& body) {
            DrawModel(c, out, model, origin + DeckToWorld(body.position), Quatd::FromAxisAngle({0, 1, 0}, body.yaw), -1, id++);
        };
        for (const Person& p : session_.Crew()) person(*assets_.crew, p.body);
        if (!session_.FirstPerson()) person(*assets_.pilot, session_.Player());
        return;
    }
    CollectStarLights(c, out);
    CollectAtmospheres(c, out);
    DrawClouds(c, out);
    DrawDust(c, out);
    UpdateRings(c);

    const ShipState& ship = session_.Ship();
    // Con la calidad maxima, cada golpe sin escudos se clava en el casco; si no, ni se calcula.
    if (hullDetail_ >= 3) SyncCraters();
    else craters_.clear();
    if (session_.Phase() == GamePhase::Flying) {
        wreckRotation_ = ship.orientation;
        DrawModel(c, out, *assets_.ship, ship.position, wreckRotation_, -1, 0);
        const SceneLight shipLight = ShipLight(c, LightAt(c, ship.position));
        DrawModules(c, out, ship.position, wreckRotation_, shipLight);
        DrawThrusters(c, out, shipLight);
        DrawExhaust(c, out);
        DrawDockTube(c, out);
        if (hullDetail_ >= 3) {
            DrawCraterDetail(c, out, ship.position, wreckRotation_, LightAt(c, ship.position));
            DrawDebris(c, out);
        }
        DrawWarpLens(c, out);
        DrawScanPulse(c, out);
    } else if (session_.Phase() == GamePhase::Adrift || (session_.Phase() == GamePhase::GameOver && session_.ShutDown())) {
        DrawAdrift(c, out);
    } else if (session_.Phase() == GamePhase::Exploding || session_.WreckAge() < 20.0) {
        DrawWreck(c, out);
    }
    DrawLostPieces(c, out);

    for (const Station& s : session_.Stations().All()) {
        if (!session_.StationVisible(s)) continue;
        const Vec3d position = session_.StationPosition(s);
        const ModelRef& model = s.kind == StationKind::Mining ? assets_.miningStation : assets_.powerStation;
        // La de energia mira a su estrella con la corola. La sesion la gira igual: ahi esta la escotilla.
        const Quatd rotation = session_.StationRotation(s);
        constexpr double kDeploySeconds = 6.0;
        const double assembly = s.deployAge / kDeploySeconds;
        DrawModel(c, out, *model, position, rotation, static_cast<int>(s.modules.size()), static_cast<std::uint32_t>(s.id) + 16u, assembly, &ship.position);
        if (assembly < 1.0) {
            // Un haz guia las piezas, y la estacion chispea mientras se monta.
            const float fade = static_cast<float>(1.0 - assembly);
            const Vec3f from = c.viewRotation.TransformDirection(Vec3f(ship.position - c.cameraPosition));
            const Vec3f to = c.viewRotation.TransformDirection(Vec3f(position - c.cameraPosition));
            if (from.z > 0.05f || to.z > 0.05f) out.lines.push_back({from, to, Color{1.0f, 0.75f, 0.3f, 0.55f * fade}});
            if (to.z > 0.05f) {
                SpriteInstance glow;
                glow.viewPosition = to;
                glow.pixelRadius = 1.2f / to.z * c.camera.FocalPx() * (0.6f + 0.4f * static_cast<float>(std::sin(c.time * 14.0)));
                glow.tint = {2.0f * fade, 1.4f * fade, 0.6f * fade, 1.0f};
                glow.id = 0x75000000u + static_cast<std::uint32_t>(s.id);
                glow.texture = glow_;
                glow.blend = BlendMode::Additive;
                glow.fogReach = 50.0f;
                out.sprites.push_back(glow);
            }
        }
    }

    for (const Anomaly& a : session_.Anomalies()) {
        const Vec3d position = session_.AnomalyPosition(a);
        DrawModel(c, out, *ModelFor(assets_, a), position, Quatd::FromAxisAngle({0, 1, 0}, static_cast<double>(a.yaw)), -1, static_cast<std::uint32_t>(a.id) + 4096u);
    }

    // Proyectiles: un punto de luz con estela, de un color por tipo de dano. Lo que vuela hacia el blanco
    // cae casi en el mismo punto de la pantalla, asi que la estela arranca del arma y es larga.
    static const Color kTints[] = {{2.0f, 1.7f, 0.9f, 1.0f}, {0.6f, 1.6f, 2.0f, 1.0f}, {2.0f, 0.5f, 1.8f, 1.0f}, {2.0f, 0.9f, 0.3f, 1.0f}};
    std::uint32_t lightId = 0;
    const auto view = [&](const Vec3d& at) { return c.viewRotation.TransformDirection(Vec3f(at - c.cameraPosition)); };
    const auto spark = [&](const Vec3f& at, float radius, const Color& tint) {
        if (at.z <= 0.05f) return;
        SpriteInstance glow;
        glow.viewPosition = at;
        glow.pixelRadius = radius;
        glow.tint = tint;
        glow.id = 0x76000000u + lightId++;
        glow.texture = glow_;
        glow.blend = BlendMode::Additive;
        glow.fogReach = 1.0e9f;
        out.sprites.push_back(glow);
    };
    const auto ray = [&](const Vec3d& from, const Vec3d& to, const Color& color) {
        const Vec3f a = view(from), b = view(to);
        if (a.z > 0.05f || b.z > 0.05f) out.lines.push_back({a, b, color});
    };
    for (const Projectile& p : session_.Projectiles()) {
        const Color tint = kTints[static_cast<int>(p.type)];
        const Vec3f at = view(p.position);
        spark(at, std::max(7.0f, 2.0f / std::max(at.z, 0.05f) * c.camera.FocalPx()), tint);
        const double flown = Length(p.position - p.origin);
        if (flown <= 0.0) continue;
        const double trail = std::min(flown, p.beam ? 1.0e7 : 3.0e4);
        ray(p.position + (p.origin - p.position) * (trail / flown), p.position, Color{tint.r, tint.g, tint.b, p.beam ? 1.0f : 0.8f});
    }
    for (const ImpactFlash& f : session_.Impacts()) {
        const float life = static_cast<float>(1.0 - f.age / 0.6);
        const Color tint = kTints[static_cast<int>(f.type)];
        if (f.hit) spark(view(f.position), 26.0f * (1.6f - life), Color{tint.r, tint.g, tint.b, life});
        if (f.beam) ray(f.origin, f.position, Color{tint.r, tint.g, tint.b, life});
    }
}

}  // namespace game
