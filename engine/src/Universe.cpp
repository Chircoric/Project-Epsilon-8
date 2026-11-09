#include "epsilon/Universe.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "epsilon/BlackHole.h"

namespace eps {

namespace {

using namespace astro;

constexpr float kDiscInSunSprite = 0.72f;     // radio del disco dentro de la imagen
constexpr float kDiscInPlanetSprite = 0.90f;
constexpr double kVisibleOrbits = 300.0;      // un planeta deja de verse a 300 veces su orbita

std::uint32_t SystemSeed(std::uint32_t seed, int index) {
    return seed * 2654435761u + static_cast<std::uint32_t>(index) * 40503u + 1u;
}

int TakeSlot(std::vector<int>& free, std::size_t size) {
    if (free.empty()) return static_cast<int>(size);
    const int slot = free.back();
    free.pop_back();
    return slot;
}

template <class T>
void Store(std::vector<T>& items, int slot, T value) {
    if (static_cast<std::size_t>(slot) == items.size()) items.push_back(std::move(value));
    else items[static_cast<std::size_t>(slot)] = std::move(value);
}

int AddSurface(World& w, const SurfaceLook& look) {
    const int slot = TakeSlot(w.freeSurfaces, w.surfaceStore.size());
    Store(w.surfaceStore, slot, look);
    w.dirty = true;
    return slot;
}

// Puntos de la galaxia: separados como las estrellas reales, en 3D.
std::vector<Vec3d> PlaceStars(int systems, std::mt19937& rng, std::vector<int>& parents) {
    std::vector<Vec3d> positions;
    parents.assign(static_cast<std::size_t>(systems), -1);
    for (int i = 0; i < systems; ++i) {
        Vec3d p;
        if (i > 0) {
            const int tries = systems > 5000 ? 1 : 200;
            for (int attempt = 0; attempt < tries; ++attempt) {
                const int parent = static_cast<int>(rng() % static_cast<std::uint32_t>(i));
                parents[static_cast<std::size_t>(i)] = parent;
                const double z = Uniform(rng, -1, 1), yaw = Uniform(rng, 0, 2.0 * kPi);
                const double flat = std::sqrt(1.0 - z * z);
                const double r = Uniform(rng, 3.0, 8.0) * kLightYear;
                p = positions[static_cast<std::size_t>(parent)] + Vec3d{flat * std::cos(yaw), z, flat * std::sin(yaw)} * r;
                bool clear = true;
                if (systems <= 5000)
                    for (const Vec3d& q : positions) clear = clear && Length(q - p) >= 2.5 * kLightYear;
                if (clear) break;
            }
        }
        positions.push_back(p);
    }
    return positions;
}

// Una estrella y su halo: lo que siempre existe. node es el sector si va sola.
void AddStarBody(World& w, int i, int member, const StarData& star, NodeId node, const Quatd& plane, const std::string& name, int orbitSlot, std::mt19937& rng) {
    const NodeId sector = w.sectors[static_cast<std::size_t>(i)];
    const bool blackHole = star.kind == StarClass::BlackHole;
    const Vec3f color = blackHole ? Vec3f{1.0f, 0.62f, 0.32f} : BlackbodyColor(star.temperature);
    const float brightness = static_cast<float>(std::clamp(1.6 + 0.5 * std::log10(star.luminosity), 1.0, 7.0));
    const float radius = static_cast<float>(star.radius);

    Renderable halo;
    halo.node = node;
    halo.radius = radius * (blackHole ? 14.0f : 2.6f);
    halo.minPixelRadius = brightness * 2.5f;
    halo.maxPixelRadius = 6000.0f;
    halo.tint = {color.x, color.y, color.z, 0.55f};
    halo.texture = w.art.halo;
    halo.blend = BlendMode::Additive;
    halo.fogReach = 250.0f;  // una estrella se adivina a traves de la niebla mucho antes de verse
    if (blackHole) halo.hideInside = radius * 900.0f;  // de cerca lo dibuja la lente
    w.renderables.push_back(halo);

    const auto& starModels = w.art.stars[static_cast<std::size_t>(star.kind)];
    Renderable core;
    core.node = node;
    core.radius = radius;
    core.spriteScale = 1.0f / kDiscInSunSprite;
    core.minPixelRadius = brightness;
    core.tint = w.art.hasArt && !starModels.empty() ? Color{} : Color{color.x, color.y, color.z, 1.0f};
    core.texture = starModels.empty() ? w.art.plainStar : starModels[rng() % starModels.size()];
    if (!blackHole) core.surface = AddSurface(w, StarLook(star.temperature));
    core.seed = static_cast<float>(Uniform(rng, 0, 200));
    core.spinRate = star.spinRate;
    core.spinPhase = Uniform(rng, 0, 6.28);
    core.tilt = Quatf::Cast(plane);
    if (blackHole) core.hideInside = radius * 900.0f;
    w.renderables.push_back(core);

    SystemState& state = w.state[static_cast<std::size_t>(i)];
    const int starBody = static_cast<int>(w.bodies.size());
    Body body{node, sector, star.radius, i, true, PlanetClass::Rock, name, -1, orbitSlot, -1, static_cast<int>(w.renderables.size()) - 1};
    body.member = member;
    w.bodies.push_back(std::move(body));
    if (state.starBody < 0) state.starBody = starBody;
    state.starBodies.push_back(starBody);
    if (state.starNodes.size() <= static_cast<std::size_t>(member)) state.starNodes.resize(static_cast<std::size_t>(member) + 1, kNoNode);
    state.starNodes[static_cast<std::size_t>(member)] = node;

    if (blackHole) {
        BlackHoleRecord record;
        record.system = i;
        record.node = node;
        record.schwarzschildRadius = star.radius;
        record.spin = static_cast<float>(star.blackHoleSpin);
        record.discInner = static_cast<float>(InnermostStableOrbit(star.blackHoleSpin));
        record.discOuter = 14.0f;
        record.brightness = 0.42f;
        record.seed = core.seed;
        record.orientation = Quatf::Cast(plane);
        w.blackHoles.push_back(record);
    }
}

// Sector y estrellas de un sistema. Con varias, cada nodo del arbol es un nodo de la escena.
void AddSystem(World& w, int i, const StarSystemData& system, const Vec3d& position, std::mt19937& rng) {
    Transform t;
    t.position = position;
    const Quatd plane = Quatd::FromAxisAngle({Uniform(rng, -1, 1), Uniform(rng, -1, 1), Uniform(rng, -1, 1)}, Uniform(rng, 0.0, 0.6));
    t.rotation = plane;
    const NodeId sector = w.scene.CreateNode(kNoNode, t);
    w.sectors.push_back(sector);
    w.sectorBase.push_back(position);
    w.state.emplace_back();
    w.history.emplace_back();

    const std::string name = "SYS-" + std::to_string(i);
    if (system.nodes.empty()) {
        w.state.back().stellarNodes.push_back(sector);
        AddStarBody(w, i, 0, system.star, sector, plane, name, -1, rng);
        return;
    }

    std::vector<NodeId> nodes(system.nodes.size(), sector);
    std::vector<int> orbits(system.nodes.size(), -1);
    for (std::size_t n = 1; n < system.nodes.size(); ++n) {
        const StellarNode& node = system.nodes[n];
        const NodeId center = nodes[static_cast<std::size_t>(node.parent)];
        nodes[n] = w.scene.CreateNode(center);
        orbits[n] = static_cast<int>(w.orbits.size());
        w.orbits.push_back(MakeOrbit(nodes[n], node.orbit));
        w.orbitPaths.push_back({static_cast<std::size_t>(orbits[n]), center, Color{1.0f, 0.78f, 0.45f, 0.40f}, true});
    }
    SystemState& state = w.state.back();
    state.stellarNodes = nodes;
    state.stellarReach = StellarReach(system);
    for (const int orbit : orbits)
        if (orbit >= 0) state.starOrbits.push_back(orbit);
    for (std::size_t n = 0; n < system.nodes.size(); ++n) {
        const StellarNode& node = system.nodes[n];
        if (node.first < 0) AddStarBody(w, i, node.star, system.StarAt(node.star), nodes[n], plane, name + " " + StellarName(system, static_cast<int>(n)), orbits[n], rng);
    }
}

void LeavesOf(const StarSystemData& system, int node, std::vector<int>& out) {
    const StellarNode& n = system.nodes[static_cast<std::size_t>(node)];
    if (n.first < 0) {
        out.push_back(n.star);
        return;
    }
    LeavesOf(system, n.first, out);
    LeavesOf(system, n.second, out);
}

Color SoftColor(const Vec3f& c) { return {0.5f + 0.5f * c.x, 0.5f + 0.5f * c.y, 0.5f + 0.5f * c.z, 1.0f}; }

// Dos fuentes de luz como mucho: las de lo que orbita y, si falta, la mas brillante del resto.
void AssignLights(const World& w, int i, const StarSystemData& system, int host, Renderable& r) {
    const SystemState& state = w.state[static_cast<std::size_t>(i)];
    if (system.nodes.empty()) {
        r.lightNode = w.sectors[static_cast<std::size_t>(i)];
        return;
    }
    const auto brighter = [&](int a, int b) { return system.StarAt(a).luminosity > system.StarAt(b).luminosity; };
    std::vector<int> close, all, order;
    LeavesOf(system, host, close);
    LeavesOf(system, 0, all);
    std::sort(close.begin(), close.end(), brighter);
    std::sort(all.begin(), all.end(), brighter);
    order = close;
    for (const int star : all)
        if (std::find(order.begin(), order.end(), star) == order.end()) order.push_back(star);

    const StarData& first = system.StarAt(order[0]);
    r.lightNode = state.starNodes[static_cast<std::size_t>(order[0])];
    r.lightPower = static_cast<float>(first.luminosity);
    r.lightColor = SoftColor(BlackbodyColor(first.temperature));
    if (order.size() > 1) {
        const StarData& second = system.StarAt(order[1]);
        r.lightNode2 = state.starNodes[static_cast<std::size_t>(order[1])];
        r.lightPower2 = static_cast<float>(second.luminosity);
        r.lightColor2 = SoftColor(BlackbodyColor(second.temperature));
    }
}

SurfaceLook MoltenLook(SurfaceLook look) {
    if (look.kind == SurfaceKind::Gas || look.kind == SurfaceKind::Star) return look;
    look.kind = SurfaceKind::Lava;
    look.a = {0.08f, 0.03f, 0.02f};
    look.b = {0.30f, 0.11f, 0.05f};
    look.c = {1.0f, 0.55f, 0.12f};
    look.atmosphere = {1.0f, 0.42f, 0.12f, 0.45f};
    return look;
}

// El sistema tal como lo dejaron sus choques. Los que debian ocurrir mientras estaba descargado
// se resuelven aqui, sin explosion: nadie estaba mirando.
void ApplyHistory(World& w, int i, StarSystemData& system) {
    SystemHistory& history = w.history[static_cast<std::size_t>(i)];
    const auto gone = [&](int data) {
        for (const BodyChange& c : history.changes)
            if (c.data == data && c.destroyed) return true;
        return false;
    };
    for (std::size_t b = 0; b < system.bodies.size(); ++b) {
        const BodyData& body = system.bodies[b];
        if (body.impactTime < 0.0 || body.impactTime > w.simTime || gone(static_cast<int>(b)) || gone(body.impactTarget)) continue;
        const BodyData& target = system.bodies[static_cast<std::size_t>(body.impactTarget)];
        const double speed = Length(OrbitVelocity(MakeOrbit(kNoNode, body.orbit), body.impactTime) - OrbitVelocity(MakeOrbit(kNoNode, target.orbit), body.impactTime));
        RecordImpact(history, system, static_cast<int>(b), body.mass, body.radius, body.impactTarget, target.mass, target.radius, speed, static_cast<std::uint32_t>(b) * 2654435761u + static_cast<std::uint32_t>(i));
    }
    for (const BodyChange& c : history.changes) {
        BodyData& body = system.bodies[static_cast<std::size_t>(c.data)];
        body.destroyed = c.destroyed;
        if (c.mass > 0.0) body.mass = c.mass;
        if (c.radius > 0.0) body.radius = c.radius;
    }
    for (BodyData& body : system.bodies)
        if (body.parent >= 0 && system.bodies[static_cast<std::size_t>(body.parent)].destroyed) body.destroyed = true;
    system.belts.insert(system.belts.end(), history.debris.begin(), history.debris.end());
}

template <class T>
void EraseValue(std::vector<T>& items, T value) {
    items.erase(std::remove(items.begin(), items.end(), value), items.end());
}

template <class T, class F>
void EraseIf(std::vector<T>& items, F test) {
    items.erase(std::remove_if(items.begin(), items.end(), test), items.end());
}

struct Heart {
    Vec3d center;
    double radius = 1;   // nube central
    double reach = 1;    // radio de la galaxia
};

// Centro de la galaxia, apartado lo justo para que el sistema inicial quede fuera de la nube.
Heart GalaxyHeart(const std::vector<Vec3d>& positions) {
    Heart h;
    for (const Vec3d& p : positions) h.center = h.center + p;
    h.center = h.center * (1.0 / static_cast<double>(positions.size()));
    for (const Vec3d& p : positions) h.reach = std::max(h.reach, Length(p - h.center));
    h.radius = h.reach * 0.30;

    const Vec3d away = h.center - positions.front();
    const double gap = Length(away);
    const double wanted = h.radius * 1.25;
    if (gap < wanted) h.center = positions.front() + (gap > 1.0 ? away * (wanted / gap) : Vec3d{wanted, 0.0, 0.0});
    return h;
}

void AddNebulae(World& w, const std::vector<Vec3d>& positions, std::mt19937& rng) {
    const Heart heart = GalaxyHeart(positions);
    const double reach = heart.reach;

    // Palabras de la emision real: rojo del hidrogeno, verde azulado del oxigeno, azul del polvo.
    const Color inners[] = {{1.0f, 0.42f, 0.30f}, {0.32f, 0.86f, 0.80f}, {0.58f, 0.46f, 1.0f}};
    const Color outers[] = {{0.30f, 0.12f, 0.46f}, {0.10f, 0.22f, 0.50f}, {0.42f, 0.10f, 0.26f}};
    for (int n = 0; n < w.options.nebulae; ++n) {
        NebulaRecord record;
        const int palette = n % 3;
        record.inner = inners[palette];
        record.outer = outers[palette];
        record.seed = static_cast<float>(Uniform(rng, 0, 50));
        if (n == 0) {
            record.position = heart.center;
            record.radius = heart.radius;
            record.density = 1.0f;
        } else {
            // Lejos del sistema inicial: de otro modo se empezaria dentro de la niebla.
            record.radius = reach * Uniform(rng, 0.12, 0.2);
            for (int attempt = 0; attempt < 32; ++attempt) {
                const Vec3d anchor = positions[rng() % positions.size()];
                record.position = anchor + Vec3d{Uniform(rng, -1, 1), Uniform(rng, -1, 1), Uniform(rng, -1, 1)} * (reach * 0.3);
                if (Length(record.position - positions.front()) > record.radius * 1.3) break;
            }
            record.density = static_cast<float>(Uniform(rng, 0.6, 1.0));
        }
        w.nebulae.push_back(record);
    }
}

void Publish(World& w) {
    w.surfaces = std::make_shared<const std::vector<SurfaceLook>>(w.surfaceStore);
    auto catalog = std::make_shared<WorldCatalog>();
    catalog->bodies = w.bodies;
    catalog->orbits = w.orbits;
    catalog->systems = w.systems;
    catalog->extents = w.extents;
    catalog->systemBodies.resize(w.state.size());
    for (std::size_t i = 0; i < w.state.size(); ++i) catalog->systemBodies[i] = w.state[i].bodies;
    catalog->rockData = w.rockData;
    catalog->belts = w.beltInfo;
    w.catalog = std::move(catalog);
    w.dirty = false;
}

}  // namespace

SurfaceLook DefaultLook(PlanetClass kind) {
    using P = PlanetClass;
    using K = SurfaceKind;
    switch (kind) {
        case P::Lava: return {K::Lava, {0.09f, 0.025f, 0.015f}, {0.36f, 0.10f, 0.03f}, {1.0f, 0.68f, 0.08f}, {1.0f, 0.32f, 0.05f, 0.35f}};
        case P::Rock: return {K::Rock, {0.22f, 0.20f, 0.19f}, {0.58f, 0.54f, 0.50f}, {}, {}};
        case P::Desert: return {K::Rock, {0.42f, 0.22f, 0.11f}, {0.86f, 0.62f, 0.38f}, {}, {0.95f, 0.65f, 0.40f, 0.30f}};
        case P::Terran: return {K::Terrestrial, {0.07f, 0.24f, 0.62f}, {0.13f, 0.47f, 0.17f}, {0.44f, 0.42f, 0.36f}, {0.35f, 0.58f, 1.0f, 0.75f}};
        case P::Ice: return {K::Rock, {0.52f, 0.62f, 0.72f}, {0.92f, 0.95f, 0.98f}, {}, {0.70f, 0.85f, 1.0f, 0.20f}};
        case P::HotJupiter: return {K::Gas, {0.20f, 0.05f, 0.03f}, {0.62f, 0.22f, 0.06f}, {0.95f, 0.55f, 0.18f}, {1.0f, 0.45f, 0.15f, 0.70f}};
        case P::GasGiant: return {K::Gas, {0.40f, 0.25f, 0.15f}, {0.80f, 0.64f, 0.46f}, {0.94f, 0.88f, 0.76f}, {0.90f, 0.72f, 0.52f, 0.45f}};
        case P::IceGiant: return {K::Gas, {0.20f, 0.46f, 0.64f}, {0.36f, 0.66f, 0.80f}, {0.62f, 0.84f, 0.90f}, {0.45f, 0.78f, 0.95f, 0.60f}};
        case P::Comet: return {K::Rock, {0.10f, 0.10f, 0.11f}, {0.34f, 0.33f, 0.33f}, {}, {}};
        default: return {K::Rock, {0.20f, 0.20f, 0.22f}, {0.62f, 0.61f, 0.60f}, {}, {}};
    }
}

// Las manchas son mas frias y las zonas calientes mas blancas que el cuerpo de la estrella.
SurfaceLook StarLook(double temperature) {
    const Vec3f c = BlackbodyColor(temperature);
    const Vec3f spot = BlackbodyColor(temperature * 0.75);
    SurfaceLook look;
    look.kind = SurfaceKind::Star;
    look.a = {spot.x * 0.62f, spot.y * 0.62f, spot.z * 0.62f};
    look.b = {c.x, c.y, c.z};
    look.c = {0.5f + 0.5f * c.x, 0.5f + 0.5f * c.y, 0.5f + 0.5f * c.z};
    return look;
}

SurfaceLook LookFromMaterials(const BodyData& body) {
    const Appearance p = DeriveAppearance(body);
    const auto color = [](const Vec3f& c) { return Color{c.x, c.y, c.z, 1.0f}; };
    SurfaceLook look;
    look.kind = p.kind;
    look.a = color(p.a);
    look.b = color(p.b);
    look.c = color(p.c);
    look.atmosphere = {p.atmosphere.x, p.atmosphere.y, p.atmosphere.z, p.atmosphereStrength};
    look.oceanLevel = p.oceanLevel;
    look.iceCap = p.iceCap;
    look.cloudCover = p.cloudCover;
    look.relief = p.relief;
    look.flattening = p.flattening;
    return look;
}

Color LookTint(const SurfaceLook& look) {
    return {(look.a.r + look.b.r) * 0.5f, (look.a.g + look.b.g) * 0.5f, (look.a.b + look.b.b) * 0.5f, 1.0f};
}

double SystemExtent(const StarSystemData& system) {
    double extent = system.star.radius * (system.star.kind == StarClass::BlackHole ? 60.0 : 40.0);
    extent = std::max(extent, StellarReach(system) * 1.5);
    for (const BodyData& b : system.bodies)
        if (b.parent < 0 && b.kind != PlanetClass::Comet && b.kind != PlanetClass::Asteroid) extent = std::max(extent, Apoapsis(b.orbit) + (system.nodes.empty() ? 0.0 : StellarReach(system, b.host)));
    return extent;
}

StarSystemData GenerateWorldSystem(const GalaxyOptions& options, int index) {
    GeneratorOptions generator;
    generator.planetsMin = options.planetsMin;
    generator.planetsMax = options.planetsMax;
    generator.moonsMax = options.moonsMax;
    generator.cometsMax = options.cometsMax;
    generator.realFrequencies = options.realFrequencies;
    generator.maxStars = options.maxStars;

    std::mt19937 rng(SystemSeed(options.seed, index));
    StarSystemData system;

    // El sistema extra del final es el agujero supermasivo del centro de la galaxia.
    if (options.blackHoles && index == options.systems) {
        generator.blackHoleMass = 4.3e6;  // como Sagitario A*
        generator.blackHoleSpin = 0.9;
        generator.planetsMin = generator.planetsMax = 0;
        generator.cometsMax = 0;
        generator.layout = StarLayout::Single;
        system = GenerateSystem(StarClass::BlackHole, rng, generator);
    } else if (options.blackHoles && (index == 1 || index == 2)) {
        generator.blackHoleSpin = index == 1 ? 0.0 : 0.95;  // uno de cada forma, siempre
        generator.layout = StarLayout::Single;
        system = GenerateSystem(StarClass::BlackHole, rng, generator);
    } else if (index == 0) {
        // El primer sistema se parece al nuestro, para tener un punto de partida conocido.
        if (generator.planetsMin < 0) generator.planetsMin = 7;
        generator.layout = StarLayout::Single;
        system = GenerateSystem(StarClass::G, rng, generator);
    } else if (options.showcaseStars && options.maxStars >= 2 && index >= 3 && index <= 6) {
        // Una muestra de cada forma, con estrellas normales y planetas suficientes para verla.
        const StarLayout layouts[] = {StarLayout::Close, StarLayout::Wide, StarLayout::Triple, StarLayout::Quadruple};
        const StarLayout wanted = layouts[index - 3];
        const int stars = wanted == StarLayout::Quadruple ? 4 : wanted == StarLayout::Triple ? 3 : 2;
        generator.layout = stars <= options.maxStars ? wanted : StarLayout::Close;
        if (generator.planetsMin < 0) generator.planetsMin = 3;
        system = GenerateSystem(index % 2 == 0 ? StarClass::F : StarClass::G, rng, generator);
    } else {
        system = GenerateSystem(rng, generator);
    }

    if (options.belts) PlanBelts(system, SystemSeed(options.seed, index) ^ 0x3C6EF372u);

    // Un sistema inestable: alguien viene de camino hacia su mayor planeta.
    if (options.impacts && index == 7) {
        int target = -1;
        for (std::size_t b = 0; b < system.bodies.size(); ++b)
            if (system.bodies[b].parent < 0 && system.bodies[b].kind != PlanetClass::Comet && system.bodies[b].kind != PlanetClass::Asteroid)
                if (target < 0 || system.bodies[b].mass > system.bodies[static_cast<std::size_t>(target)].mass) target = static_cast<int>(b);
        if (target >= 0) {
            const double radius = system.bodies[static_cast<std::size_t>(target)].radius * 0.35;
            AddImpactor(system, target, options.impactDay * kDay, radius, rng);
        }
    }
    return system;
}

World BuildGalaxy(const GalaxyOptions& options, const AstroArt& art) {
    World w;
    w.options = options;
    w.art = art;
    std::mt19937 rng(options.seed);
    w.sky = GenerateSky(2500, options.seed);

    std::vector<int> parents;
    std::vector<Vec3d> positions = PlaceStars(options.systems, rng, parents);
    for (Vec3d& p : positions) p = p * options.starSpacing;
    const std::vector<Vec3d> stars = positions;
    if (options.blackHoles) {
        positions.push_back(GalaxyHeart(stars).center);
        parents.push_back(-1);
    }
    const int total = static_cast<int>(positions.size());

    for (int i = 0; i < total; ++i) {
        StarSystemData system = GenerateWorldSystem(options, i);
        std::mt19937 starRng(SystemSeed(options.seed, i) ^ 0x9E3779B9u);
        AddSystem(w, i, system, positions[static_cast<std::size_t>(i)], starRng);
        const int parent = parents[static_cast<std::size_t>(i)];
        if (parent >= 0) w.links.push_back({w.sectors[static_cast<std::size_t>(parent)], w.sectors.back(), Color{0.2f, 0.4f, 0.8f, 0.35f}});

        w.extents.push_back(SystemExtent(system));
        w.stats.totalOrbits += system.bodies.size() + w.state.back().starOrbits.size();
        w.stats.totalRenderables += 2 * static_cast<std::size_t>(system.StarCount()) + system.bodies.size();
        // Sin cargar solo se guardan las estrellas: lo demas se regenera con la semilla.
        system.bodies.clear();
        system.bodies.shrink_to_fit();
        system.belts.clear();
        system.belts.shrink_to_fit();
        w.systems.push_back(std::make_shared<const StarSystemData>(std::move(system)));
    }

    AddNebulae(w, stars, rng);
    Publish(w);

    if (!options.streaming) {
        for (int i = 0; i < total; ++i) LoadSystem(w, i);
        w.stats.loads = 0;
        Publish(w);
    }
    return w;
}

void LoadSystem(World& w, int i) {
    SystemState& state = w.state[static_cast<std::size_t>(i)];
    if (state.loaded) return;
    state.loaded = true;

    auto editable = std::make_shared<StarSystemData>(GenerateWorldSystem(w.options, i));
    ApplyHistory(w, i, *editable);
    const StarSystemData& system = *editable;
    w.systems[static_cast<std::size_t>(i)] = editable;

    // Los campos van primero: sus asteroides con nombre cuelgan de ellos.
    state.beltBodies.assign(system.belts.size(), -1);
    for (std::size_t b = 0; b < system.belts.size(); ++b) InstallBelt(w, i, static_cast<int>(b));

    const NodeId sector = w.sectors[static_cast<std::size_t>(i)];
    const Quatd plane = w.scene.Local(sector).rotation;
    const std::string systemName = "SYS-" + std::to_string(i);
    const double loadRadius = w.extents[static_cast<std::size_t>(i)] * w.options.loadExtents;
    const auto& models = w.art;
    std::mt19937 rng(SystemSeed(w.options.seed, i) ^ 0x7F4A7C15u);

    std::vector<NodeId> nodes(system.bodies.size(), kNoNode);
    std::vector<int> bodyIndex(system.bodies.size(), -1);
    for (std::size_t b = 0; b < system.bodies.size(); ++b) {
        const BodyData& body = system.bodies[b];
        if (body.destroyed) continue;
        const bool moon = body.parent >= 0;
        const bool asteroid = body.kind == PlanetClass::Asteroid;
        const NodeId center = moon ? nodes[static_cast<std::size_t>(body.parent)] : state.stellarNodes[static_cast<std::size_t>(body.host)];

        // El nodo guarda la orientacion del ecuador: sus lunas la heredan.
        Transform local;
        const Quatd axis = SpinAxisRotation(body.spin.axis);
        local.rotation = axis;
        const NodeId node = w.scene.CreateNode(center, local);
        nodes[b] = node;
        if (!moon) state.topNodes.push_back(node);

        const int orbitSlot = TakeSlot(w.freeOrbits, w.orbits.size());
        Store(w.orbits, orbitSlot, MakeOrbit(node, body.orbit));
        state.orbits.push_back(orbitSlot);

        const Color pathColor = moon ? Color{0.55f, 0.55f, 0.60f, 0.35f}
                              : body.kind == PlanetClass::Comet ? Color{0.45f, 0.75f, 0.85f, 0.30f}
                              : asteroid ? Color{0.75f, 0.68f, 0.55f, 0.55f}
                              : Color{0.35f, 0.55f, 0.95f, 0.45f};
        const int pathSlot = TakeSlot(w.freePaths, w.orbitPaths.size());
        const int bodySlot = TakeSlot(w.freeBodies, w.bodies.size());
        OrbitPathRecord path{static_cast<std::size_t>(orbitSlot), center, pathColor, true};
        path.quiet = asteroid && body.impactTime < 0.0;  // decenas de elipses taparian el sistema
        path.owner = bodySlot;
        Store(w.orbitPaths, pathSlot, path);
        state.paths.push_back(pathSlot);

        // De cerca manda la composicion. El sprite lejano es el modelo de color mas parecido.
        SurfaceLook look = LookFromMaterials(body);
        const BodyChange* change = nullptr;
        for (const BodyChange& c : w.history[static_cast<std::size_t>(i)].changes)
            if (c.data == static_cast<int>(b)) change = &c;
        if (change && change->molten) look = MoltenLook(look);
        const Color target = LookTint(look);
        const auto& variants = models.planets[static_cast<std::size_t>(body.kind)];
        const ModelVariant* model = nullptr;
        float best = 1.0e9f;
        for (const ModelVariant& v : variants) {
            const Color c = LookTint(v.look);
            const float d = std::abs(c.r - target.r) + std::abs(c.g - target.g) + std::abs(c.b - target.b);
            if (d < best) {
                best = d;
                model = &v;
            }
        }

        Quatd world = plane;
        if (moon) world = world * SpinAxisRotation(system.bodies[static_cast<std::size_t>(body.parent)].spin.axis);

        Renderable r;
        r.node = node;
        r.radius = static_cast<float>(body.radius);
        r.spriteScale = 1.0f / kDiscInPlanetSprite;
        r.minPixelRadius = moon || body.kind == PlanetClass::Comet ? 1.2f : asteroid ? 0.9f : 2.4f;
        if (asteroid) {
            r.asteroid = true;
            if (!models.asteroidMeshes.empty()) r.mesh = models.asteroidMeshes[rng() % models.asteroidMeshes.size()];
        }
        if (change) {
            r.scarDirection = change->scarDirection;
            r.scarSize = change->scarSize;
        }
        if (w.options.fade) {
            double fade = body.orbit.semiMajorAxis * kVisibleOrbits;
            if (w.options.streaming) fade = std::min(fade, loadRadius * 0.45);  // se apaga antes de descargarse
            r.fadeDistance = static_cast<float>(fade);
        }
        r.tint = model ? model->tint : target;
        r.texture = model ? model->texture : models.plainPlanet;
        r.surface = AddSurface(w, look);
        state.surfaces.push_back(r.surface);
        AssignLights(w, i, system, body.host, r);
        r.seed = static_cast<float>(Uniform(rng, 0, 200));
        r.spinRate = body.spin.rate;
        r.spinPhase = body.spin.phase;
        r.tilt = Quatf::Cast((world * axis).Normalized());
        const int renderSlot = TakeSlot(w.freeRenderables, w.renderables.size());
        Store(w.renderables, renderSlot, r);
        state.renderables.push_back(renderSlot);

        const int parentBody = moon ? bodyIndex[static_cast<std::size_t>(body.parent)] : body.belt >= 0 ? state.beltBodies[static_cast<std::size_t>(body.belt)] : state.starBody;
        Store(w.bodies, bodySlot, Body{node, sector, body.radius, i, false, body.kind, BodyName(system, b, systemName), static_cast<int>(b), orbitSlot, parentBody, renderSlot});
        bodyIndex[b] = bodySlot;
        state.bodies.push_back(bodySlot);
        if (moon) state.moons.push_back(bodySlot);
        if (asteroid) state.asteroids.push_back(bodySlot);
    }
    // Despues de los planetas: asi aparecen al final de la lista del sistema.
    for (const int belt : state.beltBodies) state.bodies.push_back(belt);

    ++w.stats.loads;
    w.dirty = true;
}

int InstallBelt(World& w, int i, int beltData) {
    SystemState& state = w.state[static_cast<std::size_t>(i)];
    const StarSystemData& system = *w.systems[static_cast<std::size_t>(i)];
    const BeltData& data = system.belts[static_cast<std::size_t>(beltData)];
    const NodeId host = state.stellarNodes[static_cast<std::size_t>(data.host)];

    AsteroidBelt belt = MakeBelt(system, data);
    belt.system = i;
    belt.parent = host;  // su nodo aporta el plano del sistema
    const std::string name = "SYS-" + std::to_string(i) + (data.debris ? " escombros " : " cinturon ") + std::to_string(beltData + 1);

    BeltInfo info;
    info.name = name;
    info.profile = data.profile;
    info.inner = data.inner;
    info.outer = data.outer;
    info.density = BeltRockDensity(belt);
    info.mass = BeltMass(belt);
    info.rocks = BeltRockCount(belt);
    info.families = belt.families.size();
    if (!belt.families.empty()) {
        info.minRadius = belt.families.front().minRadius;
        info.maxRadius = belt.families.front().maxRadius;
    }
    info.debris = data.debris;
    for (const BodyData& b : system.bodies) info.notable += b.belt == beltData && !b.destroyed ? 1 : 0;

    std::size_t slot = w.belts.size();
    for (std::size_t s = 0; s < w.belts.size(); ++s) {
        if (!w.belts[s].used) {
            slot = s;
            break;
        }
    }
    if (slot == w.belts.size()) {
        w.belts.emplace_back();
        w.beltInfo.emplace_back();
    }

    // El campo no tiene una posicion: su nodo es el punto del anillo mas cercano a la camara.
    const NodeId node = w.scene.CreateNode(host);
    state.topNodes.push_back(node);
    const int bodySlot = TakeSlot(w.freeBodies, w.bodies.size());
    Body body{node, w.sectors[static_cast<std::size_t>(i)], 1.0, i, false, PlanetClass::Belt, name, -1, -1, state.starBody, -1};
    body.belt = static_cast<int>(slot);
    Store(w.bodies, bodySlot, std::move(body));

    belt.body = bodySlot;
    w.belts[slot] = std::move(belt);
    w.beltInfo[slot] = std::move(info);
    state.beltSlots.push_back(static_cast<int>(slot));
    if (state.beltBodies.size() <= static_cast<std::size_t>(beltData)) state.beltBodies.resize(static_cast<std::size_t>(beltData) + 1, -1);
    state.beltBodies[static_cast<std::size_t>(beltData)] = bodySlot;
    w.dirty = true;
    return bodySlot;
}

StarSystemData& EditSystem(World& w, int system) {
    auto copy = std::make_shared<StarSystemData>(*w.systems[static_cast<std::size_t>(system)]);
    StarSystemData& out = *copy;
    w.systems[static_cast<std::size_t>(system)] = std::move(copy);
    w.dirty = true;
    return out;
}

ImpactResult RecordImpact(SystemHistory& history, const StarSystemData& system, int attacker, double attackerMass, double attackerRadius,
                          int target, double targetMass, double targetRadius, double speed, std::uint32_t seed) {
    ImpactResult result = ResolveImpact(attackerMass, attackerRadius, targetMass, targetRadius, speed);
    if (attacker >= 0) {
        BodyChange change;
        change.data = attacker;
        change.destroyed = true;
        history.changes.push_back(change);
    }
    if (target < 0) return result;  // una roca suelta no tiene ficha que cambiar
    const BodyData& body = system.bodies[static_cast<std::size_t>(target)];

    std::mt19937 rng(seed);
    BodyChange change;
    change.data = target;
    change.mass = result.remnantMass;
    change.radius = targetRadius * std::cbrt(result.remnantMass / targetMass);
    const double z = Uniform(rng, -1, 1), yaw = Uniform(rng, 0, 2.0 * kPi), flat = std::sqrt(1.0 - z * z);
    change.scarDirection = {static_cast<float>(flat * std::cos(yaw)), static_cast<float>(z), static_cast<float>(flat * std::sin(yaw))};
    change.scarSize = static_cast<float>(std::clamp(1.5 * attackerRadius / targetRadius, 0.04, 1.2));

    switch (result.outcome) {
        case ImpactOutcome::Crater:
            change.mass = targetMass + attackerMass;
            change.radius = targetRadius;
            break;
        case ImpactOutcome::Merger:
        case ImpactOutcome::Partial:
            change.molten = true;
            break;
        default:
            change.destroyed = true;
            break;
    }
    history.changes.push_back(change);

    const bool scattered = result.outcome == ImpactOutcome::Partial || result.outcome == ImpactOutcome::Shattered;
    if (scattered && body.parent < 0) {
        BeltData debris;
        debris.inner = body.orbit.semiMajorAxis * 0.94;
        debris.outer = body.orbit.semiMajorAxis * 1.06;
        debris.host = body.host;
        debris.profile = BeltProfile::Rubble;
        debris.seed = rng();
        debris.debris = true;
        history.debris.push_back(debris);
    }
    return result;
}

void ApplyChange(World& w, int index, const BodyChange& change) {
    const Body& body = w.bodies[static_cast<std::size_t>(index)];
    if (body.renderable >= 0) {
        Renderable& r = w.renderables[static_cast<std::size_t>(body.renderable)];
        if (change.radius > 0.0) r.radius = static_cast<float>(change.radius);
        r.scarDirection = change.scarDirection;
        r.scarSize = change.scarSize;
        if (change.molten && r.surface >= 0) {
            w.surfaceStore[static_cast<std::size_t>(r.surface)] = MoltenLook(w.surfaceStore[static_cast<std::size_t>(r.surface)]);
            w.dirty = true;
        }
    }
    if (change.radius > 0.0) w.bodies[static_cast<std::size_t>(index)].radius = change.radius;

    if (body.data >= 0) {
        BodyData& data = EditSystem(w, body.system).bodies[static_cast<std::size_t>(body.data)];
        if (change.mass > 0.0) data.mass = change.mass;
        if (change.radius > 0.0) data.radius = change.radius;
    }
}

void RemoveBody(World& w, int index) {
    const Body body = w.bodies[static_cast<std::size_t>(index)];
    if (!Alive(body)) return;
    if (body.extra >= 0) {
        const World::RockLink link = w.rockLinks[static_cast<std::size_t>(body.extra)];
        w.deadRocks[link.seed].insert(link.rock);
        ReleaseRock(w, index);
        return;
    }

    SystemState& state = w.state[static_cast<std::size_t>(body.system)];
    const std::vector<int> moons = state.moons;
    for (const int moon : moons)
        if (w.bodies[static_cast<std::size_t>(moon)].parent == index) RemoveBody(w, moon);

    w.scene.DestroyNode(body.node);
    EraseValue(state.topNodes, body.node);
    EraseValue(state.bodies, index);
    EraseValue(state.moons, index);
    EraseValue(state.asteroids, index);

    if (body.renderable >= 0) {
        const Renderable r = w.renderables[static_cast<std::size_t>(body.renderable)];
        if (r.surface >= 0) {
            EraseValue(state.surfaces, r.surface);
            w.freeSurfaces.push_back(r.surface);
        }
        w.renderables[static_cast<std::size_t>(body.renderable)] = Renderable{};
        EraseValue(state.renderables, body.renderable);
        w.freeRenderables.push_back(body.renderable);
    }
    if (body.orbit >= 0) {
        w.orbits[static_cast<std::size_t>(body.orbit)] = KeplerOrbit{};
        EraseValue(state.orbits, body.orbit);
        w.freeOrbits.push_back(body.orbit);
        for (const int path : state.paths) {
            if (w.orbitPaths[static_cast<std::size_t>(path)].owner == index) {
                w.orbitPaths[static_cast<std::size_t>(path)].used = false;
                w.freePaths.push_back(path);
                EraseValue(state.paths, path);
                break;
            }
        }
    }
    w.bodies[static_cast<std::size_t>(index)] = Body{};
    w.freeBodies.push_back(index);
    w.dirty = true;
}

int PromoteRock(World& w, int beltIndex, int familyIndex, int rockIndex, const AsteroidRock& rock) {
    const AsteroidBelt& belt = w.belts[static_cast<std::size_t>(beltIndex)];
    const AsteroidFamily& family = belt.families[static_cast<std::size_t>(familyIndex)];
    const int i = belt.system;
    SystemState& state = w.state[static_cast<std::size_t>(i)];

    // La familia sigue su orbita y la roca va dentro de ella, en su sitio.
    const NodeId familyNode = w.scene.CreateNode(belt.parent);
    const int orbitSlot = TakeSlot(w.freeOrbits, w.orbits.size());
    Store(w.orbits, orbitSlot, MakeOrbit(familyNode, family.orbit.elements));
    // El desplazamiento de una roca va en ejes del mundo; el nodo vive en el plano del sistema.
    const Mat4d& frame = w.scene.World(belt.parent);
    const Vec3d offset(rock.offset);
    Transform local;
    local.position = {Dot(offset, frame.TransformDirection(Vec3d{1, 0, 0})), Dot(offset, frame.TransformDirection(Vec3d{0, 1, 0})), Dot(offset, frame.TransformDirection(Vec3d{0, 0, 1}))};
    const NodeId node = w.scene.CreateNode(familyNode, local);

    std::mt19937 rng(family.seed ^ static_cast<std::uint32_t>(rockIndex));
    BodyData data;
    data.kind = PlanetClass::Asteroid;
    data.radius = rock.radius;
    data.orbit = family.orbit.elements;
    data.materials = AsteroidMaterials(family.type, rng);
    data.mass = 4.0 / 3.0 * kPi * std::pow(static_cast<double>(rock.radius), 3.0) * Density(data.materials);
    data.spin = RandomSpin(data.orbit, 3.0, 40.0, rng);
    data.spin.rate = rock.spinRate;
    const StarSystemData& system = *w.systems[static_cast<std::size_t>(i)];
    data.temperature = EquilibriumTemperature(system.star.luminosity, data.orbit.semiMajorAxis, 0.1);

    const int extra = TakeSlot(w.freeRocks, w.rockData.size());
    Store(w.rockData, extra, data);
    Store(w.rockLinks, extra, World::RockLink{family.seed, rockIndex});

    // Sin dibujo propio: la roca ya la pinta su campo. Esto solo alimenta la miniatura.
    Renderable r;
    r.node = node;
    r.hidden = true;
    r.radius = rock.radius;
    r.surface = AddSurface(w, LookFromMaterials(data));
    r.spinRate = rock.spinRate;
    r.seed = rock.spinPhase;
    const int renderSlot = TakeSlot(w.freeRenderables, w.renderables.size());
    Store(w.renderables, renderSlot, r);

    const int bodySlot = TakeSlot(w.freeBodies, w.bodies.size());
    Body body{node, w.sectors[static_cast<std::size_t>(i)], rock.radius, i, false, PlanetClass::Asteroid, "SYS-" + std::to_string(i) + " roca " + std::to_string(rockIndex + 1), -1, orbitSlot, belt.body, renderSlot};
    body.extra = extra;
    Store(w.bodies, bodySlot, std::move(body));
    state.rocks.push_back(bodySlot);
    w.dirty = true;
    return bodySlot;
}

void ReleaseRock(World& w, int index) {
    const Body body = w.bodies[static_cast<std::size_t>(index)];
    if (!Alive(body) || body.extra < 0) return;
    SystemState& state = w.state[static_cast<std::size_t>(body.system)];
    w.scene.DestroyNode(w.scene.Parent(body.node));  // la familia y la roca con ella
    const Renderable r = w.renderables[static_cast<std::size_t>(body.renderable)];
    w.freeSurfaces.push_back(r.surface);
    w.renderables[static_cast<std::size_t>(body.renderable)] = Renderable{};
    w.freeRenderables.push_back(body.renderable);
    w.orbits[static_cast<std::size_t>(body.orbit)] = KeplerOrbit{};
    w.freeOrbits.push_back(body.orbit);
    w.freeRocks.push_back(body.extra);
    w.bodies[static_cast<std::size_t>(index)] = Body{};
    w.freeBodies.push_back(index);
    EraseValue(state.rocks, index);
    w.dirty = true;
}

double BodyMass(const World& w, const Body& body) {
    if (body.isStar) return w.systems[static_cast<std::size_t>(body.system)]->StarAt(body.member).mass;
    const BodyData* data = BodyInfo(w, body);
    return data ? data->mass : 0.0;
}

const BodyData* BodyInfo(const World& w, const Body& body) {
    if (body.extra >= 0) return &w.rockData[static_cast<std::size_t>(body.extra)];
    if (body.data >= 0) return &w.systems[static_cast<std::size_t>(body.system)]->bodies[static_cast<std::size_t>(body.data)];
    return nullptr;
}

void UnloadSystem(World& w, int i) {
    SystemState& state = w.state[static_cast<std::size_t>(i)];
    if (!state.loaded) return;

    const std::vector<int> rocks = state.rocks;
    for (const int rock : rocks) ReleaseRock(w, rock);
    for (const int belt : state.beltBodies) {
        if (belt >= 0) {
            w.bodies[static_cast<std::size_t>(belt)] = Body{};
            w.freeBodies.push_back(belt);
        }
    }
    EraseIf(state.bodies, [&](int b) { return !Alive(w.bodies[static_cast<std::size_t>(b)]); });

    for (const NodeId node : state.topNodes) w.scene.DestroyNode(node);  // sus lunas caen con ellos

    // Al reves: los huecos se reutilizan en el mismo orden en que se ocuparon.
    for (auto s = state.bodies.rbegin(); s != state.bodies.rend(); ++s) {
        w.bodies[static_cast<std::size_t>(*s)] = Body{};
        w.freeBodies.push_back(*s);
    }
    for (auto s = state.renderables.rbegin(); s != state.renderables.rend(); ++s) {
        w.renderables[static_cast<std::size_t>(*s)] = Renderable{};
        w.freeRenderables.push_back(*s);
    }
    for (auto s = state.orbits.rbegin(); s != state.orbits.rend(); ++s) {
        w.orbits[static_cast<std::size_t>(*s)] = KeplerOrbit{};
        w.freeOrbits.push_back(*s);
    }
    for (auto s = state.paths.rbegin(); s != state.paths.rend(); ++s) {
        w.orbitPaths[static_cast<std::size_t>(*s)].used = false;
        w.freePaths.push_back(*s);
    }
    for (auto s = state.surfaces.rbegin(); s != state.surfaces.rend(); ++s) w.freeSurfaces.push_back(*s);

    for (const int slot : state.beltSlots) {
        w.belts[static_cast<std::size_t>(slot)] = AsteroidBelt{};
        w.belts[static_cast<std::size_t>(slot)].used = false;
    }

    // Solo quedan las estrellas. La semilla basta para volver a crear el resto.
    auto starsOnly = std::make_shared<StarSystemData>(*w.systems[static_cast<std::size_t>(i)]);
    starsOnly->bodies.clear();
    starsOnly->belts.clear();
    w.systems[static_cast<std::size_t>(i)] = std::move(starsOnly);

    SystemState kept;
    kept.starBody = state.starBody;
    kept.starBodies = std::move(state.starBodies);
    kept.starNodes = std::move(state.starNodes);
    kept.stellarNodes = std::move(state.stellarNodes);
    kept.starOrbits = std::move(state.starOrbits);
    kept.stellarReach = state.stellarReach;
    state = std::move(kept);

    ++w.stats.unloads;
    w.dirty = true;
}

void StreamWorld(World& w, const Vec3d& camera, float focalPx, const std::vector<int>& pinnedBodies, const FeatureSet& features) {
    const bool streaming = w.options.streaming && features.Enabled(Feature::Streaming);
    const bool culling = features.Enabled(Feature::BodyCulling);

    std::vector<char> pinnedSystems;
    if (streaming) {
        pinnedSystems.assign(w.state.size(), 0);
        for (const int body : pinnedBodies)
            if (body >= 0 && Alive(w.bodies[static_cast<std::size_t>(body)])) pinnedSystems[static_cast<std::size_t>(w.bodies[static_cast<std::size_t>(body)].system)] = 1;
    }

    int loads = 0;
    w.stats.loadedSystems = 0;
    w.stats.activeOrbits = 0;
    w.stats.liveOrbits = 0;
    w.stats.liveRenderables = 0;

    for (std::size_t i = 0; i < w.state.size(); ++i) {
        SystemState& state = w.state[i];
        w.stats.liveRenderables += state.starBodies.size() * 2;

        // Mientras las estrellas no se separen un pixel, da igual donde este cada una.
        if (!state.starOrbits.empty()) {
            const double distance = std::max(Length(w.scene.WorldPosition(w.sectors[i]) - camera), 1.0);
            const bool active = !culling || state.loaded || static_cast<float>(state.stellarReach / distance) * focalPx >= 1.0f;
            for (const int orbit : state.starOrbits) w.orbits[static_cast<std::size_t>(orbit)].active = active;
            w.stats.liveOrbits += state.starOrbits.size();
            if (active) w.stats.activeOrbits += state.starOrbits.size();
        }

        if (streaming) {
            const double distance = Length(w.scene.WorldPosition(w.sectors[i]) - camera);
            const double extent = w.extents[i];
            if (!state.loaded && loads < w.options.maxLoadsPerStep && (pinnedSystems[i] || distance < extent * w.options.loadExtents)) {
                LoadSystem(w, static_cast<int>(i));
                ++loads;
            } else if (state.loaded && !pinnedSystems[i] && distance > extent * w.options.unloadExtents) {
                UnloadSystem(w, static_cast<int>(i));
            }
        } else if (w.options.streaming && !state.loaded && loads < w.options.maxLoadsPerStep) {
            LoadSystem(w, static_cast<int>(i));  // al apagar la funcion, se carga todo poco a poco
            ++loads;
        }
        if (!state.loaded) continue;

        ++w.stats.loadedSystems;
        w.stats.liveOrbits += state.orbits.size();
        w.stats.liveRenderables += state.renderables.size();
        w.stats.activeOrbits += state.orbits.size();

        // Una luna gira a su planeta. Si toda su orbita cabe en un pixel, no hay nada que ver:
        // el planeta sigue dibujandose y la luna se calcula al recuperar el tamano.
        for (const int moon : state.moons) {
            const Body& b = w.bodies[static_cast<std::size_t>(moon)];
            KeplerOrbit& orbit = w.orbits[static_cast<std::size_t>(b.orbit)];
            bool active = true;
            if (culling) {
                const Vec3d host = w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(b.parent)].node);
                const double reach = Apoapsis(orbit.elements) + b.radius;
                const double surface = std::max(Length(host - camera) - reach, 1.0);
                const float pixels = static_cast<float>(reach / surface) * focalPx;
                active = pixels >= (orbit.active ? w.options.moonOffPixels : w.options.moonOnPixels);
                active = active || std::find(pinnedBodies.begin(), pinnedBodies.end(), moon) != pinnedBodies.end();
            }
            orbit.active = active;
            w.renderables[static_cast<std::size_t>(b.renderable)].hidden = !active;
            if (!active) --w.stats.activeOrbits;
        }

        // Desde fuera del sistema un asteroide no se distingue: no se calcula, salvo que sea el
        // seleccionado, que lo sea su campo o que tenga un choque pendiente.
        const double asteroidRange = w.extents[i] * 3.0;
        const double sectorDistance = Length(w.scene.WorldPosition(w.sectors[i]) - camera);
        for (const int asteroid : state.asteroids) {
            const Body& b = w.bodies[static_cast<std::size_t>(asteroid)];
            KeplerOrbit& orbit = w.orbits[static_cast<std::size_t>(b.orbit)];
            bool active = true;
            if (culling) {
                active = sectorDistance < asteroidRange;
                active = active || b.parent == w.highlightBody || asteroid == w.highlightBody;
                active = active || w.systems[i]->bodies[static_cast<std::size_t>(b.data)].impactTime >= 0.0;
                active = active || std::find(pinnedBodies.begin(), pinnedBodies.end(), asteroid) != pinnedBodies.end();
            }
            orbit.active = active;
            w.renderables[static_cast<std::size_t>(b.renderable)].hidden = !active;
            if (!active) --w.stats.activeOrbits;
        }

        // El nodo de cada campo se pone en el punto del anillo mas cercano a la camara: es donde
        // se le coloca la etiqueta. No hay que calcular ninguna roca para saberlo.
        for (const int slot : state.beltSlots) {
            const AsteroidBelt& belt = w.belts[static_cast<std::size_t>(slot)];
            const Mat4d& frame = w.scene.World(belt.parent);
            const Vec3d offset = camera - frame.Translation();
            const Vec3d ex = frame.TransformDirection(Vec3d{1, 0, 0}), ez = frame.TransformDirection(Vec3d{0, 0, 1});
            const double x = Dot(offset, ex), z = Dot(offset, ez);
            const double length = std::sqrt(x * x + z * z);
            const double radius = std::clamp(length, belt.inner, belt.outer);
            const Body& b = w.bodies[static_cast<std::size_t>(belt.body)];
            w.scene.Local(b.node).position = length > 1.0 ? Vec3d{x / length * radius, 0.0, z / length * radius} : Vec3d{radius, 0.0, 0.0};
        }
    }
}

void PublishWorld(World& w) {
    if (w.dirty) Publish(w);
}

Vec3d RecenterOrigin(World& w, Vec3d& camera, double threshold) {
    if (Dot(camera, camera) < threshold * threshold) return {};
    const Vec3d shift = camera;
    w.origin = w.origin + shift;
    for (std::size_t i = 0; i < w.sectors.size(); ++i) w.scene.Local(w.sectors[i]).position = w.sectorBase[i] - w.origin;
    camera = {};
    return shift;
}

int NearestSystem(const World& w, const Vec3d& position, double* distance) {
    int best = 0;
    double nearest = 1.0e300;
    for (std::size_t i = 0; i < w.sectors.size(); ++i) {
        const double d = Length(w.scene.WorldPosition(w.sectors[i]) - position);
        if (d < nearest) {
            nearest = d;
            best = static_cast<int>(i);
        }
    }
    if (distance) *distance = nearest;
    return best;
}

void InitialView(const World& w, Vec3d& position, float& yaw, float& pitch, int system) {
    const std::size_t index = static_cast<std::size_t>(system);
    const Vec3d target = w.scene.WorldPosition(w.sectors[index]);
    const double extent = w.extents[index];
    const Vec3d above = w.scene.World(w.sectors[index]).TransformDirection(Vec3d{0.35, -0.72, -0.60});
    position = target + above * (extent * 1.15);
    const Vec3f look = Normalize(Vec3f(target - position));
    yaw = std::atan2(look.x, look.z);
    pitch = std::asin(look.y);
}

Camera MakeCamera(const World& w) {
    Camera cam;
    cam.SetViewport(1920, 1080, 960.0f, 1.0f);  // igual que el juego: focal = ancho / 2
    Vec3d position;
    float yaw = 0, pitch = 0;
    InitialView(w, position, yaw, pitch);
    cam.SetPosition(position);
    cam.SetYawPitchRoll(yaw, pitch, 0.0f);
    return cam;
}

}  // namespace eps
