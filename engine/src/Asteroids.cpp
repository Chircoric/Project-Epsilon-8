#include "epsilon/Asteroids.h"

#include <algorithm>
#include <cmath>

namespace eps {

namespace {

using astro::BeltProfile;
using astro::Uniform;

constexpr double kDegree = kPi / 180.0;
constexpr int kRockVariants = 6;
constexpr double kRockDensity = 2.5e12;  // kg/km^3

// La columna de la izquierda es el nombre que ve el jugador.
//                                       familias polvo rocas radio  min    max   exp  con nombre  radio km
const BeltProfileInfo kProfiles[] = {
    {"Disperso",                         0.4f, 0.5f, 0.35f, 470.0f, 0.2f,  8.0f, 2.1f, 2, 4, 20.0, 120.0},
    {"Regular",                          1.0f, 1.0f, 1.0f,  400.0f, 0.12f, 14.0f, 1.9f, 3, 6, 30.0, 250.0},
    {"Denso",                            2.0f, 2.2f, 2.2f,  290.0f, 0.1f,  10.0f, 2.0f, 5, 9, 20.0, 180.0},
    {"Escombros",                        1.4f, 3.0f, 3.0f,  250.0f, 0.03f, 2.5f, 2.6f, 1, 3, 8.0, 40.0},
    {"Gigantes",                         0.7f, 0.5f, 0.25f, 610.0f, 2.0f,  90.0f, 1.3f, 6, 10, 100.0, 480.0},
};
static_assert(std::size(kProfiles) == static_cast<std::size_t>(BeltProfile::Count));

Color TypeColor(AsteroidType type, double u) {
    switch (type) {
        case AsteroidType::Carbon: return {0.17f + static_cast<float>(u) * 0.08f, 0.16f + static_cast<float>(u) * 0.07f, 0.15f + static_cast<float>(u) * 0.06f, 1.0f};
        case AsteroidType::Metal: return {0.46f + static_cast<float>(u) * 0.14f, 0.43f + static_cast<float>(u) * 0.12f, 0.40f + static_cast<float>(u) * 0.10f, 1.0f};
        default: return {0.44f + static_cast<float>(u) * 0.18f, 0.35f + static_cast<float>(u) * 0.13f, 0.27f + static_cast<float>(u) * 0.10f, 1.0f};
    }
}

// Mas alla de la linea de hielo dominan los carbonaceos; por dentro, los de silicato.
AsteroidType RandomType(double a, double snowLine, std::mt19937& rng) {
    const double u = Uniform(rng, 0, 1);
    if (u < 0.08) return AsteroidType::Metal;
    const double carbon = std::clamp(0.2 + 0.7 * (a / snowLine - 0.5), 0.1, 0.92);
    return Uniform(rng, 0, 1) < carbon ? AsteroidType::Carbon : AsteroidType::Silicate;
}

OrbitalElements RandomBeltOrbit(double inner, double outer, double mu, double eccentricity, double inclination, std::mt19937& rng) {
    OrbitalElements e;
    e.semiMajorAxis = Uniform(rng, inner, outer);
    e.eccentricity = std::min(astro::Rayleigh(rng, eccentricity), std::min(eccentricity * 3.0, 0.3));  // que no se salgan del anillo
    e.inclination = std::min(astro::Rayleigh(rng, inclination), 25.0 * kDegree);
    e.ascendingNode = Uniform(rng, 0, 2.0 * kPi);
    e.argPeriapsis = Uniform(rng, 0, 2.0 * kPi);
    e.meanAnomaly0 = Uniform(rng, 0, 2.0 * kPi);
    e.mu = mu;
    return e;
}

BeltProfile RandomProfile(std::mt19937& rng) {
    const double u = Uniform(rng, 0, 1);
    if (u < 0.2) return BeltProfile::Sparse;
    if (u < 0.5) return BeltProfile::Regular;
    if (u < 0.7) return BeltProfile::Dense;
    if (u < 0.85) return BeltProfile::Rubble;
    return BeltProfile::Giants;
}

void AddNotables(astro::StarSystemData& system, int beltIndex, const astro::BeltData& belt, std::mt19937& rng) {
    const BeltProfileInfo& info = ProfileInfo(belt.profile);
    const astro::StarData& host = system.nodes.empty() ? system.star : system.nodes[static_cast<std::size_t>(belt.host)].effective;
    const double mu = astro::kGravity * host.mass;
    const double snowLine = std::max(host.snowLine, 1.0);
    const int count = info.notableMin + static_cast<int>(rng() % static_cast<std::uint32_t>(info.notableMax - info.notableMin + 1));
    for (int i = 0; i < count; ++i) {
        astro::BodyData body;
        body.kind = astro::PlanetClass::Asteroid;
        body.host = belt.host;
        body.belt = beltIndex;
        body.radius = astro::LogUniform(rng, info.notableRadiusMin, info.notableRadiusMax);
        body.orbit = RandomBeltOrbit(belt.inner, belt.outer, mu, 0.09, 6.0 * kDegree, rng);
        body.materials = AsteroidMaterials(RandomType(body.orbit.semiMajorAxis, snowLine, rng), rng);
        body.mass = 4.0 / 3.0 * kPi * body.radius * body.radius * body.radius * astro::Density(body.materials);
        body.spin = astro::RandomSpin(body.orbit, 3.0, 40.0, rng);
        body.temperature = astro::EquilibriumTemperature(host.luminosity, body.orbit.semiMajorAxis, 0.1);
        system.bodies.push_back(body);
    }
}

// Volumen medio de una roca: integral de r^3 con la ley de potencias.
double MeanRockVolume(const AsteroidFamily& f) {
    const double k = f.exponent, low = f.minRadius, high = f.maxRadius;
    const double norm = std::pow(low, -k) - std::pow(high, -k);
    const double mean3 = std::abs(k - 3.0) < 1e-6 ? k * std::log(high / low) / norm : k * (std::pow(high, 3.0 - k) - std::pow(low, 3.0 - k)) / ((3.0 - k) * norm);
    return 4.0 / 3.0 * kPi * mean3;
}

}  // namespace

astro::Materials AsteroidMaterials(AsteroidType type, std::mt19937& rng) {
    astro::Materials m;
    switch (type) {
        case AsteroidType::Carbon:
            m.silicate = 0.72;
            m.iron = 0.13;
            m.water = 0.15;
            m.carbon = Uniform(rng, 0.65, 0.9);
            break;
        case AsteroidType::Metal:
            m.iron = 0.85;
            m.silicate = 0.15;
            m.carbon = 0.1;
            break;
        default:
            m.silicate = 0.8;
            m.iron = 0.2;
            m.rust = Uniform(rng, 0.1, 0.4);
            m.carbon = 0.1;
            break;
    }
    return m;
}

const BeltProfileInfo& ProfileInfo(BeltProfile profile) { return kProfiles[static_cast<std::size_t>(profile)]; }

BeltOptions OptionsFor(BeltProfile profile, const BeltOptions& base) {
    const BeltProfileInfo& info = ProfileInfo(profile);
    BeltOptions o = base;
    o.families = static_cast<int>(static_cast<float>(base.families) * info.families);
    o.dust = static_cast<int>(static_cast<float>(base.dust) * info.dust);
    o.rocksPerFamily = static_cast<int>(static_cast<float>(base.rocksPerFamily) * info.rocks);
    o.familyRadius = info.familyRadius;
    o.minRadius = info.minRadius;
    o.maxRadius = info.maxRadius;
    o.exponent = info.exponent;
    return o;
}

std::vector<AsteroidRock> GenerateRocks(const AsteroidFamily& family) {
    std::mt19937 rng(family.seed);
    std::vector<AsteroidRock> rocks(static_cast<std::size_t>(family.rockCount));
    const double kExponent = family.exponent;  // pocas rocas grandes
    const double low = std::pow(static_cast<double>(family.minRadius), -kExponent), high = std::pow(static_cast<double>(family.maxRadius), -kExponent);

    for (AsteroidRock& r : rocks) {
        // Mas densa hacia el centro, sin borde marcado.
        const double z = Uniform(rng, -1, 1), yaw = Uniform(rng, 0, 2.0 * kPi);
        const double flat = std::sqrt(1.0 - z * z);
        const double reach = family.radius * std::pow(Uniform(rng, 0, 1), 0.6);
        r.offset = Vec3f(Vec3d{flat * std::cos(yaw), z, flat * std::sin(yaw)} * reach);
        r.radius = static_cast<float>(std::pow(low + (high - low) * Uniform(rng, 0, 1), -1.0 / kExponent));

        const double az = Uniform(rng, -1, 1), ayaw = Uniform(rng, 0, 2.0 * kPi);
        const double aflat = std::sqrt(1.0 - az * az);
        r.spinAxis = {static_cast<float>(aflat * std::cos(ayaw)), static_cast<float>(az), static_cast<float>(aflat * std::sin(ayaw))};
        r.spinRate = static_cast<float>(2.0 * kPi / (astro::LogUniform(rng, 2.0, 40.0) * astro::kHour));
        r.spinPhase = static_cast<float>(Uniform(rng, 0, 2.0 * kPi));
        r.variant = static_cast<std::uint16_t>(rng() % kRockVariants);
        r.color = TypeColor(family.type, Uniform(rng, 0, 1));
    }
    return rocks;
}

AsteroidBelt MakeBelt(double inner, double outer, double mu, double snowLine, std::uint32_t seed, const BeltOptions& options) {
    std::mt19937 rng(seed);
    AsteroidBelt belt;
    belt.inner = inner;
    belt.outer = outer;

    belt.families.resize(static_cast<std::size_t>(options.families));
    for (AsteroidFamily& f : belt.families) {
        f.orbit = MakeOrbit(kNoNode, RandomBeltOrbit(inner, outer, mu, options.eccentricity, options.inclination, rng));
        f.radius = options.familyRadius * static_cast<float>(Uniform(rng, 0.6, 1.5));
        // Dentro de un mismo cinturon hay cumulos apretados y otros casi vacios.
        f.rockCount = std::max(static_cast<int>(options.rocksPerFamily * Uniform(rng, 0.4, 1.6)), 1);
        f.seed = rng();
        f.type = RandomType(f.orbit.elements.semiMajorAxis, snowLine, rng);
        f.minRadius = options.minRadius;
        f.maxRadius = options.maxRadius;
        f.exponent = options.exponent;
    }

    belt.dust.resize(static_cast<std::size_t>(options.dust));
    for (KeplerOrbit& d : belt.dust) d = MakeOrbit(kNoNode, RandomBeltOrbit(inner, outer, mu, options.eccentricity, options.inclination, rng));

    const double middle = (inner + outer) * 0.5;
    belt.dustColor = middle > snowLine ? Color{0.52f, 0.56f, 0.64f, 0.5f} : Color{0.62f, 0.52f, 0.42f, 0.5f};

    // El medio del anillo gira como sus familias.
    belt.mu = mu;
    belt.seed = seed ^ 0x9E3779B9u;
    belt.thickness = std::max(options.inclination * 0.85, 0.01);
    if (!belt.families.empty()) {
        const Vec3d at = OrbitPosition(belt.families.front().orbit, 0.0), moving = OrbitVelocity(belt.families.front().orbit, 0.0);
        belt.spin = Cross(at, moving).y < 0.0 ? 1.0 : -1.0;
    }
    return belt;
}

namespace {

// Numeros de una celda: rapidos y sin estado grande. Misma celda, mismos numeros.
struct CellRandom {
    std::uint64_t state;
    double Next() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }
};

std::uint64_t CellSeed(std::uint32_t seed, std::uint32_t salt, int i, int j, int k) {
    std::uint64_t h = (static_cast<std::uint64_t>(seed) << 32) ^ salt;
    for (const int v : {i, j, k}) h = (h ^ static_cast<std::uint32_t>(v)) * 0x100000001B3ull + 0x632BE59BD9B4E019ull;
    return h;
}

}  // namespace

BeltFieldTier BeltRockTier(const AsteroidBelt& belt) {
    const BeltProfileInfo& info = ProfileInfo(belt.profile);
    BeltFieldTier t;
    t.cell = 2500.0;
    t.perCell = 1.6 * info.families;
    t.minRadius = info.minRadius;
    t.maxRadius = std::min(info.maxRadius * 0.4f, 12.0f);
    t.exponent = info.exponent;
    t.salt = 1;
    return t;
}

BeltFieldTier BeltDustTier(const AsteroidBelt& belt) {
    const BeltProfileInfo& info = ProfileInfo(belt.profile);
    BeltFieldTier t;
    t.cell = 9000.0;
    t.perCell = 2.0 * info.dust;
    t.minRadius = 0.5f;
    t.maxRadius = 2.0f;
    t.exponent = 2.0f;
    t.salt = 2;
    return t;
}

double BeltFieldShape(const AsteroidBelt& belt, double radius, double height) {
    if (belt.outer <= belt.inner || radius <= 0.0) return 0.0;
    // Las familias tienen orbitas excentricas (hasta 0,3) e inclinadas: muchas quedan fuera de los
    // bordes o lejos del plano, y el medio tiene que llegar hasta ellas. Lleno entre los bordes, y
    // fuera una caida lenta, tanto hacia los lados como con la altura.
    const double outside = std::max({belt.inner - radius, radius - belt.outer, 0.0}) / (radius * 0.15);
    const double sigma = radius * belt.thickness, h = height / sigma;
    return 1.0 / ((1.0 + outside * outside) * (1.0 + h * h));
}

void BeltField(const AsteroidBelt& belt, const Mat4d& frame, const Vec3d& center, const Vec3d& point, double reach, double simTime, const BeltFieldTier& tier, std::vector<BeltFieldRock>& out) {
    if (belt.mu <= 0.0 || belt.outer <= belt.inner || reach <= 0.0) return;
    const Vec3d local = InverseAffine(frame).TransformDirection(point - center);
    const double r = std::sqrt(local.x * local.x + local.z * local.z);
    if (r <= reach || BeltFieldShape(belt, r, std::max(std::abs(local.y) - reach, 0.0)) < 0.005) return;  // aqui no queda anillo
    const double cell = tier.cell, phi = std::atan2(local.z, local.x);
    const int i0 = static_cast<int>(std::floor((r - reach) / cell)), i1 = static_cast<int>(std::floor((r + reach) / cell));
    const int k0 = static_cast<int>(std::floor((local.y - reach) / cell)), k1 = static_cast<int>(std::floor((local.y + reach) / cell));
    const double low = std::pow(static_cast<double>(tier.minRadius), -static_cast<double>(tier.exponent));
    const double high = std::pow(static_cast<double>(tier.maxRadius), -static_cast<double>(tier.exponent));
    for (int i = i0; i <= i1; ++i) {
        const double ri = (i + 0.5) * cell;
        if (ri <= cell) continue;
        if (BeltFieldShape(belt, ri, 0.0) <= 0.0) continue;
        const int ring = std::max(static_cast<int>(std::floor(2.0 * kPi * ri / cell)), 1);
        const double arc = 2.0 * kPi / ring;
        // Cada banda gira entera con la velocidad de su radio: las rocas no se salen de su celda.
        const double omega = belt.spin * std::sqrt(belt.mu / (ri * ri * ri));
        const double turned = std::fmod(omega * simTime, 2.0 * kPi);
        const double span = reach / ri;
        int j0 = static_cast<int>(std::floor((phi - turned - span) / arc)), j1 = static_cast<int>(std::floor((phi - turned + span) / arc));
        if (j1 - j0 + 1 >= ring) {
            j0 = 0;
            j1 = ring - 1;
        }
        for (int j = j0; j <= j1; ++j) {
            const int wrapped = ((j % ring) + ring) % ring;
            for (int k = k0; k <= k1; ++k) {
                const double shape = BeltFieldShape(belt, ri, (k + 0.5) * cell);
                if (shape < 0.01) continue;
                CellRandom rng{CellSeed(belt.seed, tier.salt, i, wrapped, k)};
                const int count = static_cast<int>(tier.perCell * shape + rng.Next());
                for (int n = 0; n < count; ++n) {
                    const double rr = ri + (rng.Next() - 0.5) * cell;
                    const double angle = (wrapped + rng.Next()) * arc + turned;
                    const double height = (k + rng.Next()) * cell;
                    const double size = std::pow(low + (high - low) * rng.Next(), -1.0 / static_cast<double>(tier.exponent));
                    const double az = rng.Next() * 2.0 - 1.0, ayaw = rng.Next() * 2.0 * kPi, shade = rng.Next(), phase = rng.Next(), period = rng.Next();
                    const std::uint32_t id = static_cast<std::uint32_t>(rng.Next() * 4294967295.0);
                    const Vec3d at = center + frame.TransformDirection(Vec3d{rr * std::cos(angle), height, rr * std::sin(angle)});
                    if (Length(at - point) > reach) continue;
                    BeltFieldRock rock;
                    rock.position = at;
                    rock.velocity = frame.TransformDirection(Vec3d{-std::sin(angle), 0.0, std::cos(angle)} * (omega * rr));
                    rock.radius = static_cast<float>(size);
                    rock.id = id;
                    rock.variant = static_cast<std::uint16_t>(id % kRockVariants);
                    const double aflat = std::sqrt(1.0 - az * az);
                    rock.spinAxis = {static_cast<float>(aflat * std::cos(ayaw)), static_cast<float>(az), static_cast<float>(aflat * std::sin(ayaw))};
                    rock.spinRate = static_cast<float>(2.0 * kPi / ((2.0 + 38.0 * period) * 3600.0));
                    rock.spinPhase = static_cast<float>(phase * 2.0 * kPi);
                    rock.color = TypeColor(id % 11 == 0 ? AsteroidType::Metal : (belt.dustColor.b > belt.dustColor.r ? AsteroidType::Carbon : AsteroidType::Silicate), shade);
                    out.push_back(rock);
                }
            }
        }
    }
}

AsteroidBelt MakeField(NodeId center, float radiusKm, int rocks, AsteroidType type, std::uint32_t seed) {
    return MakeField(center, radiusKm, rocks, type, seed, 0.12f, 14.0f);
}

AsteroidBelt MakeField(NodeId center, float radiusKm, int rocks, AsteroidType type, std::uint32_t seed, float minRadius, float maxRadius, float exponent) {
    AsteroidBelt belt;
    belt.parent = center;
    belt.outer = radiusKm;
    AsteroidFamily f;
    f.fixed = true;
    f.radius = radiusKm;
    f.rockCount = rocks;
    f.seed = seed;
    f.type = type;
    f.minRadius = minRadius;
    f.maxRadius = maxRadius;
    f.exponent = exponent;
    belt.families.push_back(f);
    return belt;
}

void PlanBelts(astro::StarSystemData& system, std::uint32_t seed) {
    std::mt19937 rng(seed);
    const std::size_t hosts = std::max<std::size_t>(system.nodes.size(), 1);
    std::vector<astro::BeltData> planned;

    for (std::size_t host = 0; host < hosts; ++host) {
        std::vector<double> planets;
        for (const astro::BodyData& b : system.bodies)
            if (b.parent < 0 && b.host == static_cast<int>(host) && b.kind != astro::PlanetClass::Comet && b.kind != astro::PlanetClass::Asteroid) planets.push_back(b.orbit.semiMajorAxis);
        if (planets.empty()) continue;
        std::sort(planets.begin(), planets.end());

        const auto add = [&](double inner, double outer) {
            astro::BeltData belt;
            belt.inner = inner;
            belt.outer = outer;
            belt.host = static_cast<int>(host);
            belt.profile = RandomProfile(rng);
            belt.seed = rng();
            planned.push_back(belt);
        };

        double bestGap = 1.9, inner = 0, outer = 0;  // solo huecos con un factor 1,9 o mas
        for (std::size_t i = 0; i + 1 < planets.size(); ++i) {
            const double gap = planets[i + 1] / planets[i];
            if (gap > bestGap) {
                bestGap = gap;
                inner = planets[i] * 1.18;
                outer = planets[i + 1] * 0.82;
            }
        }
        if (outer > inner) add(inner, outer);

        // Una estrella cercana recorta el cinturon exterior o lo impide.
        double farInner = planets.back() * 1.4, farOuter = planets.back() * 2.0;
        if (!system.nodes.empty() && system.nodes[host].bounded) farOuter = std::min(farOuter, system.nodes[host].planetOuter * 0.97);
        if (farOuter > farInner * 1.08) add(farInner, farOuter);
    }

    const int first = static_cast<int>(system.belts.size());
    system.belts.insert(system.belts.end(), planned.begin(), planned.end());
    for (std::size_t i = 0; i < planned.size(); ++i) AddNotables(system, first + static_cast<int>(i), planned[i], rng);
}

AsteroidBelt MakeBelt(const astro::StarSystemData& system, const astro::BeltData& data, const BeltOptions& base) {
    const astro::StarData& host = system.nodes.empty() ? system.star : system.nodes[static_cast<std::size_t>(data.host)].effective;
    BeltOptions options = OptionsFor(data.profile, base);
    if (data.debris) {
        // Recien formado: todas las piezas conservan casi la orbita del cuerpo que se rompio.
        options.eccentricity = 0.02;
        options.inclination = 1.5 * kDegree;
    }
    AsteroidBelt belt = MakeBelt(data.inner, data.outer, astro::kGravity * host.mass, std::max(host.snowLine, 1.0), data.seed, options);
    belt.profile = data.profile;
    belt.debris = data.debris;
    return belt;
}

std::vector<AsteroidBelt> GenerateBelts(const astro::StarSystemData& system, std::uint32_t seed, const BeltOptions& options) {
    astro::StarSystemData copy = system;
    copy.belts.clear();
    PlanBelts(copy, seed);
    std::vector<AsteroidBelt> belts;
    belts.reserve(copy.belts.size());
    for (const astro::BeltData& data : copy.belts) belts.push_back(MakeBelt(copy, data, options));
    return belts;
}

double BeltRockDensity(const AsteroidBelt& belt) {
    double rocks = 0, volume = 0;
    for (const AsteroidFamily& f : belt.families) {
        rocks += f.rockCount;
        volume += 4.0 / 3.0 * kPi * std::pow(static_cast<double>(f.radius), 3.0);
    }
    return volume > 0.0 ? rocks / volume * 1.0e6 : 0.0;
}

std::size_t BeltRockCount(const AsteroidBelt& belt) {
    std::size_t rocks = 0;
    for (const AsteroidFamily& f : belt.families) rocks += static_cast<std::size_t>(f.rockCount);
    return rocks;
}

double BeltMass(const AsteroidBelt& belt) {
    double mass = 0;
    for (const AsteroidFamily& f : belt.families) mass += f.rockCount * MeanRockVolume(f) * kRockDensity;
    return mass;
}

}  // namespace eps
