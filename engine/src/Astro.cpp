#include "epsilon/Astro.h"

#include "epsilon/BlackHole.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

namespace eps::astro {

namespace {

constexpr double kTau = 2.0 * kPi;
constexpr double kDegree = kPi / 180.0;
constexpr double kHillSpacing = 10.0;       // separacion minima entre planetas, en radios de Hill mutuos
constexpr double kMoonHillFraction = 0.4;   // mas alla, la estrella se lleva a la luna
constexpr double kRockyDensity = 2.5e12;    // kg/km^3; 2500 kg/m^3
constexpr double kHeaviestPlanet = 0.004;   // respecto a su estrella; mas alla es una enana marron
constexpr double kCloseBinary = 3.0 * kAstronomicalUnit;  // por debajo, los planetas rodean la pareja
constexpr double kNoLimit = 1.0e300;

// Tabla documentada en docs/astrofisica.md. En las enanas blancas el radio va al reves:
// a mayor masa, menor radio.
const StarClassInfo kStars[] = {
    //  nombre               masa            radio                temperatura     giro (dias)     real      vista  planetas
    {"O",                    16, 60,         6.6, 13.4,           33000, 45000,   1, 5,           3e-7,     0.05,  0, 3},
    {"B",                    2.1, 16,        1.8, 6.6,            10000, 33000,   0.5, 3,         0.0012,   0.08,  1, 4},
    {"A",                    1.4, 2.1,       1.4, 1.8,            7300, 10000,    0.5, 2,         0.0061,   0.09,  2, 6},
    {"F",                    1.04, 1.4,      1.15, 1.4,           6000, 7300,     2, 12,          0.03,     0.11,  2, 8},
    {"G",                    0.8, 1.04,      0.96, 1.15,          5300, 6000,     10, 35,         0.076,    0.17,  3, 9},
    {"K",                    0.45, 0.8,      0.7, 0.96,           3900, 5300,     15, 45,         0.12,     0.14,  3, 8},
    {"M",                    0.08, 0.45,     0.1, 0.7,            2300, 3900,     20, 130,        0.76,     0.14,  2, 7},
    {"Gigante roja",         0.8, 3,         10, 100,             3500, 5000,     50, 500,        0.005,    0.08,  0, 4},
    {"Supergigante roja",    8, 25,          200, 1500,           3400, 4200,     3000, 15000,    1e-6,     0.04,  0, 2},
    {"Supergigante azul",    15, 40,         20, 80,              12000, 30000,   20, 80,         1e-7,     0.03,  0, 2},
    {"Enana blanca",         0.5, 1.1,       0.015, 0.0075,       6000, 40000,    0.05, 3,        0.06,     0.05,  0, 2},
    {"Estrella de neutrones", 1.2, 2.0,      1.58e-5, 2.01e-5,    100000, 1000000, 2e-7, 6e-5,    0.005,    0.02,  0, 1},
    {"Agujero negro",        5, 40,          2.12e-5, 1.70e-4,    1000000, 10000000, 1, 1,        0.001,    0.03,  0, 2},
};

double Lerp(double a, double b, double t) { return a + (b - a) * t; }
double GeoLerp(double a, double b, double t) { return std::exp(Lerp(std::log(a), std::log(b), t)); }
int IntRange(std::mt19937& rng, int a, int b) { return a + static_cast<int>(rng() % static_cast<std::uint32_t>(b - a + 1)); }

double Gamma(std::mt19937& rng, double shape) {
    // Marsaglia y Tsang. Para forma < 1 se usa la relacion G(a) = G(a + 1) * U^(1/a).
    if (shape < 1.0) return Gamma(rng, shape + 1.0) * std::pow(std::max(Uniform(rng, 0, 1), 1e-300), 1.0 / shape);
    const double d = shape - 1.0 / 3.0, c = 1.0 / std::sqrt(9.0 * d);
    for (;;) {
        const double x = Normal(rng), v = 1.0 + c * x;
        if (v <= 0.0) continue;
        const double cube = v * v * v;
        const double u = std::max(Uniform(rng, 0, 1), 1e-300);
        if (std::log(u) < 0.5 * x * x + d * (1.0 - cube + std::log(cube))) return d * cube;
    }
}

struct Sampled {
    PlanetClass kind;
    double earthMasses;
    double radius;  // km
    Materials materials;
};

constexpr double kReferenceDensity = 4.05e12;  // mezcla terrestre sin comprimir: 32 % hierro, 68 % silicatos

// Roca de la zona interior: cerca de la estrella se condensa mas hierro, lejos mas agua.
Materials RockyBulk(double a, const StarData& star, std::mt19937& rng) {
    const double x = a / star.snowLine;
    Materials m;
    m.iron = std::clamp(0.75 - 1.15 * x, 0.15, 0.60) + Uniform(rng, -0.08, 0.08);
    m.water = std::clamp(0.5 * (x - 0.3) / 0.7, 0.0, 0.5) * Uniform(rng, 0.0, 0.3);
    m.silicate = 1.0 - m.iron - m.water;
    return m;
}

void SetAtmosphere(Materials& m, double equilibrium, double pressure) {
    m.pressure = pressure;
    m.surfaceTemperature = SurfaceTemperature(equilibrium, pressure);
}

Materials SampleMaterials(PlanetClass kind, double a, double earthMasses, const StarData& star, std::mt19937& rng) {
    const double equilibrium = EquilibriumTemperature(star.luminosity, a);
    Materials m;
    switch (kind) {
        case PlanetClass::Lava:
            m = RockyBulk(a, star, rng);
            m.sulfur = Uniform(rng, 0.0, 0.4);
            m.carbon = Uniform(rng, 0.0, 0.5);
            // La roca fundida se evapora: un aire tenue de vapor, tanto mas cuanto mas azufre.
            SetAtmosphere(m, equilibrium, 0.02 + 0.2 * m.sulfur);
            break;
        case PlanetClass::Rock:
            m = RockyBulk(a, star, rng);
            m.rust = Uniform(rng, 0.0, 0.3);
            m.sulfur = Uniform(rng, 0.0, 0.1);
            m.carbon = Uniform(rng, 0.0, 0.35);
            SetAtmosphere(m, equilibrium, earthMasses > 0.5 ? LogUniform(rng, 0.001, 0.1) : 0.0);
            break;
        case PlanetClass::Desert:
            m = RockyBulk(a, star, rng);
            m.rust = Uniform(rng, 0.4, 0.95);
            m.sulfur = Uniform(rng, 0.0, 0.15);
            SetAtmosphere(m, equilibrium, LogUniform(rng, 0.005, 2.0));
            m.clouds = Uniform(rng, 0.0, 0.2);
            break;
        case PlanetClass::Terran: {
            m = RockyBulk(a, star, rng);
            m.water = LogUniform(rng, 3e-5, 2e-3);
            m.silicate = 1.0 - m.iron - m.water;
            m.rust = Uniform(rng, 0.0, 0.3);
            // La presion es la necesaria para tener agua liquida a esta distancia.
            const double ratio = Uniform(rng, 280.0, 320.0) / equilibrium;
            SetAtmosphere(m, equilibrium, std::clamp((ratio * ratio * ratio * ratio - 1.0) / 0.63, 0.05, 20.0));
            m.ocean = 0.1 + 0.8 * std::clamp(std::log10(m.water / 1e-5) / 2.0, 0.0, 1.0);
            m.clouds = Uniform(rng, 0.25, 0.7);
            m.life = Uniform(rng, 0, 1) < 0.6;
            break;
        }
        case PlanetClass::Ice:
            m.water = Uniform(rng, 0.3, 0.6);
            m.ices = Uniform(rng, 0.02, 0.2);
            m.iron = Uniform(rng, 0.05, 0.15);
            m.silicate = 1.0 - m.water - m.ices - m.iron;
            m.carbon = Uniform(rng, 0.0, 0.3);
            m.rust = Uniform(rng, 0.0, 0.25);  // tolinas rojizas, como en Pluton
            m.methane = Uniform(rng, 0.0, 0.5);
            SetAtmosphere(m, equilibrium, LogUniform(rng, 1e-5, 0.01));
            break;
        case PlanetClass::GasGiant: case PlanetClass::HotJupiter:
            m.gas = Uniform(rng, 0.75, 0.95);
            m.ices = (1.0 - m.gas) * Uniform(rng, 0.3, 0.6);
            m.silicate = (1.0 - m.gas - m.ices) * 0.7;
            m.iron = 1.0 - m.gas - m.ices - m.silicate;
            m.sulfur = Uniform(rng, 0.0, 1.0);
            m.carbon = Uniform(rng, 0.0, 1.0);
            m.methane = Uniform(rng, 0.0, 0.3);
            m.clouds = Uniform(rng, 0.4, 0.9);
            SetAtmosphere(m, equilibrium, 1000.0);
            m.surfaceTemperature = equilibrium;  // no tienen superficie
            break;
        case PlanetClass::IceGiant:
            m.gas = Uniform(rng, 0.1, 0.25);
            m.ices = Uniform(rng, 0.35, 0.55);
            m.water = Uniform(rng, 0.1, 0.25);
            m.silicate = (1.0 - m.gas - m.ices - m.water) * 0.7;
            m.iron = 1.0 - m.gas - m.ices - m.water - m.silicate;
            m.methane = Uniform(rng, 0.2, 1.0);
            m.clouds = Uniform(rng, 0.2, 0.6);
            SetAtmosphere(m, equilibrium, 1000.0);
            m.surfaceTemperature = equilibrium;
            break;
        default:
            break;
    }
    if (kind != PlanetClass::Terran && kind != PlanetClass::GasGiant && kind != PlanetClass::HotJupiter && kind != PlanetClass::IceGiant && kind != PlanetClass::Lava) {
        // Fuera de la zona templada, el agua que haya se congela desde los polos.
        const double frozen = std::clamp((285.0 - m.surfaceTemperature) / 110.0, 0.0, 1.0);
        m.iceCap = kind == PlanetClass::Ice ? 1.0 : frozen * std::min(m.water * 40.0 + 0.1, 1.0);
    } else if (kind == PlanetClass::Terran) {
        m.iceCap = std::clamp((310.0 - m.surfaceTemperature) / 90.0, 0.0, 0.6);
    }
    return m;
}

Materials MoonMaterials(const BodyData& host, double a, const StarData& star, std::mt19937& rng) {
    const bool icy = host.temperature < 170.0;
    Materials m;
    m.water = icy ? Uniform(rng, 0.2, 0.55) : Uniform(rng, 0.0, 0.03);
    m.iron = Uniform(rng, 0.05, 0.3);
    m.silicate = 1.0 - m.water - m.iron;
    m.carbon = Uniform(rng, 0.0, 0.4);
    m.rust = Uniform(rng, 0.0, 0.2);
    // Cerca de un gigante, las mareas calientan el interior: volcanes de azufre, como en Io.
    const bool giant = host.kind == PlanetClass::GasGiant || host.kind == PlanetClass::IceGiant;
    if (giant && a < 8.0 * host.radius) m.sulfur = Uniform(rng, 0.4, 0.9);
    SetAtmosphere(m, EquilibriumTemperature(star.luminosity, host.orbit.semiMajorAxis), 0.0);
    m.iceCap = icy && m.sulfur == 0.0 ? Uniform(rng, 0.6, 1.0) : 0.0;
    return m;
}

Vec3f Mix(const Vec3f& a, const Vec3f& b, double t) { return a + (b - a) * static_cast<float>(std::clamp(t, 0.0, 1.0)); }

// Color del suelo a partir de sus minerales.
// El color de la roca sale de sus elementos: el hierro la oscurece, el magnesio, el aluminio y el
// calcio la aclaran, el azufre la amarillea y el carbono la ennegrece. Los gases no cuentan. Luego,
// lo que hay solo en la superficie: oxidos, azufre volcanico y tolinas.
Vec3f Mineral(const Materials& m) {
    static const Vec3f kTint[kElementCount] = {
        {0, 0, 0}, {0, 0, 0}, {0.08f, 0.08f, 0.09f}, {0, 0, 0}, {0.60f, 0.58f, 0.55f}, {0.68f, 0.63f, 0.55f},
        {0.80f, 0.78f, 0.74f}, {0.55f, 0.52f, 0.48f}, {0.86f, 0.74f, 0.22f}, {0.85f, 0.82f, 0.75f}, {0.34f, 0.29f, 0.26f}, {0.56f, 0.55f, 0.51f}};
    const Composition c = ElementsOf(m);
    Vec3f sum{0, 0, 0};
    double weight = 0;
    for (std::size_t e = 0; e < kElementCount; ++e) {
        if (kTint[e].x + kTint[e].y + kTint[e].z <= 0.0f) continue;
        const double w = std::pow(c.share[e], 0.7);  // lo escaso tambien tine un poco
        sum = sum + kTint[e] * static_cast<float>(w);
        weight += w;
    }
    Vec3f base = weight > 0.0 ? sum * static_cast<float>(1.0 / weight) : Vec3f{0.4f, 0.38f, 0.36f};
    base = Mix(base, {0.62f, 0.25f, 0.10f}, m.rust * 0.9);
    base = Mix(base, {0.86f, 0.74f, 0.22f}, m.sulfur * 0.8);
    return Mix(base, {0.06f, 0.06f, 0.07f}, m.carbon * 0.9);
}

// Nubes de un gigante segun su temperatura: las clases de Sudarsky.
void GasColors(double t, Appearance& out) {
    if (t < 150.0) { out.a = {0.50f, 0.33f, 0.20f}; out.b = {0.85f, 0.70f, 0.50f}; out.c = {0.96f, 0.91f, 0.80f}; }        // amoniaco
    else if (t < 350.0) { out.a = {0.55f, 0.52f, 0.50f}; out.b = {0.85f, 0.85f, 0.82f}; out.c = {0.98f, 0.98f, 0.97f}; }   // agua
    else if (t < 900.0) { out.a = {0.10f, 0.18f, 0.40f}; out.b = {0.20f, 0.34f, 0.66f}; out.c = {0.40f, 0.56f, 0.86f}; }   // sin nubes
    else if (t < 1400.0) { out.a = {0.10f, 0.07f, 0.08f}; out.b = {0.30f, 0.18f, 0.16f}; out.c = {0.52f, 0.32f, 0.24f}; }  // metales alcalinos
    else { out.a = {0.30f, 0.08f, 0.04f}; out.b = {0.70f, 0.25f, 0.08f}; out.c = {1.00f, 0.60f, 0.20f}; }                  // silicatos
}

// El tipo de planeta depende de lo que se puede formar y sobrevivir a esa distancia.
Sampled SamplePlanet(const StarData& star, double a, std::mt19937& rng) {
    const double temperature = EquilibriumTemperature(star.luminosity, a);
    const double unit = Uniform(rng, 0, 1);
    const double solarMasses = star.mass / kSolarMass;

    const auto solid = [&](PlanetClass kind, double m) {
        Materials materials = SampleMaterials(kind, a, m, star, rng);
        // El hierro encoge un planeta y el agua lo hincha, a igual masa.
        const double radius = RadiusFromMass(m) * kEarthRadius * std::cbrt(kReferenceDensity / Density(materials));
        return Sampled{kind, m, radius, materials};
    };
    const auto fluid = [&](PlanetClass kind, double m, double radius) { return Sampled{kind, m, radius, SampleMaterials(kind, a, m, star, rng)}; };

    if (a >= star.snowLine) {
        const double gas = 0.45 * std::min(solarMasses, 1.0);
        if (unit < gas) return fluid(PlanetClass::GasGiant, LogUniform(rng, 30, 3000), kJupiterRadius * Uniform(rng, 0.8, 1.05));
        if (unit < gas + 0.35) {
            const double m = LogUniform(rng, 8, 30);
            return fluid(PlanetClass::IceGiant, m, RadiusFromMass(m) * kEarthRadius);
        }
        return solid(PlanetClass::Ice, LogUniform(rng, 0.01, 2));
    }

    if (a < 0.15 * kAstronomicalUnit && solarMasses > 0.5 && unit < 0.08)
        return fluid(PlanetClass::HotJupiter, LogUniform(rng, 100, 2000), kJupiterRadius * Uniform(rng, 1.0, 1.7));

    const double m = LogUniform(rng, 0.05, 10);
    if (temperature > 900) return solid(PlanetClass::Lava, m);
    if (a >= star.habitableInner && a <= star.habitableOuter && m > 0.3) return solid(unit < 0.7 ? PlanetClass::Terran : PlanetClass::Desert, m);
    if (temperature > 300 && unit < 0.5) return solid(PlanetClass::Desert, m);
    if (temperature < 170) return solid(PlanetClass::Ice, m);
    return solid(PlanetClass::Rock, m);
}

Vec3d OrbitNormal(const OrbitalElements& elements) {
    const KeplerOrbit o = MakeOrbit(kNoNode, elements);
    return Normalize(Cross(o.p, o.q));
}

// Angulo de giro para que la misma cara mire al centro de la orbita.
double LockedPhase(const OrbitalElements& e) { return e.ascendingNode + e.argPeriapsis + e.meanAnomaly0; }

SpinModel FreeSpin(const OrbitalElements& orbit, double periodMin, double periodMax, std::mt19937& rng) {
    const KeplerOrbit o = MakeOrbit(kNoNode, orbit);
    double obliquity = std::min(Rayleigh(rng, 20.0 * kDegree), 60.0 * kDegree);
    const double unit = Uniform(rng, 0, 1);
    if (unit < 0.05) obliquity = Uniform(rng, 60.0, 120.0) * kDegree;  // tumbado, como Urano
    else if (unit < 0.15) obliquity = kPi - obliquity;                 // retrogrado, como Venus

    const double azimuth = Uniform(rng, 0, kTau);
    SpinModel s;
    s.axis = Normalize(OrbitNormal(orbit) * std::cos(obliquity) +
                       (o.p * std::cos(azimuth) + o.q * std::sin(azimuth)) * std::sin(obliquity));
    s.rate = kTau / (LogUniform(rng, periodMin, periodMax) * kHour);
    s.phase = Uniform(rng, 0, kTau);
    return s;
}

SpinModel LockedSpin(const OrbitalElements& orbit, double resonance) {
    SpinModel s;
    s.axis = OrbitNormal(orbit);
    s.rate = resonance * std::sqrt(orbit.mu / (orbit.semiMajorAxis * orbit.semiMajorAxis * orbit.semiMajorAxis));
    s.phase = LockedPhase(orbit);
    return s;
}

OrbitalElements RandomOrientation(double a, double e, double inclination, double mu, std::mt19937& rng) {
    OrbitalElements o;
    o.semiMajorAxis = a;
    o.eccentricity = e;
    o.inclination = inclination;
    o.ascendingNode = Uniform(rng, 0, kTau);
    o.argPeriapsis = Uniform(rng, 0, kTau);
    o.meanAnomaly0 = Uniform(rng, 0, kTau);
    o.mu = mu;
    return o;
}

double CentralMass(const StarSystemData& system, const BodyData& body) {
    return body.parent >= 0 ? system.bodies[static_cast<std::size_t>(body.parent)].mass : HostMass(system, body);
}

void AddMoons(StarSystemData& system, int planet, int count, std::mt19937& rng) {
    if (count <= 0) return;
    const BodyData host = system.bodies[static_cast<std::size_t>(planet)];
    const double density = host.mass / (4.0 / 3.0 * kPi * host.radius * host.radius * host.radius);
    const StarData& sun = system.HostOf(host);
    const double hill = HillRadius(host.orbit.semiMajorAxis, host.orbit.eccentricity, host.mass, sun.mass);
    const double outer = kMoonHillFraction * hill;
    const bool giant = host.kind == PlanetClass::GasGiant || host.kind == PlanetClass::IceGiant;

    double a = std::max(2.0 * RocheLimit(host.radius, density, kRockyDensity), 2.5 * host.radius);
    a *= Uniform(rng, 1.0, 2.5);
    for (int m = 0; m < count && a * 1.02 < outer; ++m) {
        BodyData moon;
        moon.kind = PlanetClass::Moon;
        moon.parent = planet;
        moon.host = host.host;
        moon.radius = giant ? LogUniform(rng, 150, 2600) : host.radius * LogUniform(rng, 0.03, 0.27);
        moon.materials = MoonMaterials(host, a, sun, rng);
        moon.mass = 4.0 / 3.0 * kPi * moon.radius * moon.radius * moon.radius * Density(moon.materials);
        moon.temperature = host.temperature;
        moon.tidallyLocked = true;
        // La inclinacion se mide desde el ecuador del planeta: las lunas regulares viven ahi.
        moon.orbit = RandomOrientation(a, std::min(Rayleigh(rng, 0.01), 0.015), Rayleigh(rng, 0.5 * kDegree),
                                       kGravity * (host.mass + moon.mass), rng);
        moon.spin = LockedSpin(moon.orbit, 1.0);
        system.bodies.push_back(moon);
        a *= Uniform(rng, 1.5, 2.4);
    }
}

void AddComets(StarSystemData& system, double innerEdge, double outerOrbit, int count, std::mt19937& rng) {
    const StarData& center = system.nodes.empty() ? system.star : system.nodes[0].effective;
    for (int c = 0; c < count; ++c) {
        BodyData comet;
        comet.kind = PlanetClass::Comet;
        const double a = LogUniform(rng, outerOrbit * 0.5, outerOrbit * 3.0);
        const double e = std::min(Uniform(rng, 0.6, 0.97), 1.0 - innerEdge * 1.5 / a);
        double inclination = std::min(Rayleigh(rng, 20.0 * kDegree), 80.0 * kDegree);
        if (Uniform(rng, 0, 1) < 0.1) inclination = kPi - inclination;  // retrogrado, como el Halley
        comet.radius = LogUniform(rng, 2, 30);
        comet.mass = 4.0 / 3.0 * kPi * comet.radius * comet.radius * comet.radius * 6e11;
        comet.materials.water = 0.4;
        comet.materials.ices = 0.1;
        comet.materials.silicate = 0.5;
        comet.materials.carbon = 0.75;  // la superficie de un cometa es casi negra
        comet.orbit = RandomOrientation(a, std::max(e, 0.1), inclination, kGravity * center.mass, rng);
        comet.spin = FreeSpin(comet.orbit, 6, 60, rng);
        comet.temperature = EquilibriumTemperature(center.luminosity, a, 0.05);
        system.bodies.push_back(comet);
    }
}

// Distancias que dependen solo de la luz de la estrella.
void SetZones(StarData& s) {
    const double root = std::sqrt(s.luminosity) * kAstronomicalUnit;
    s.habitableInner = 0.99 * root;
    s.habitableOuter = 1.70 * root;
    s.snowLine = 2.7 * root;
}

// Por dentro, el polvo se evapora; una estrella gigante ademas ya ocupa ese espacio.
double PlanetInnerEdge(const StarData& star) {
    return std::max({3.0 * star.radius, 0.05 * std::sqrt(star.luminosity) * kAstronomicalUnit, 0.005 * kAstronomicalUnit});
}

double PlanetOuterEdge(const StarData& star, double inner) {
    return std::max(60.0 * std::sqrt(star.mass / kSolarMass) * kAstronomicalUnit, inner * 20.0);
}

// Lo que ve un planeta desde lejos: una sola fuente con la masa y la luz de las dos.
StarData Combined(const StarData& a, const StarData& b) {
    StarData s = a.mass >= b.mass ? a : b;
    s.mass = a.mass + b.mass;
    s.luminosity = a.luminosity + b.luminosity;
    s.radius = std::max(a.radius, b.radius);
    SetZones(s);
    return s;
}

StarClass ClassForMass(double solarMasses) {
    for (int c = static_cast<int>(StarClass::M); c >= static_cast<int>(StarClass::O); --c)
        if (solarMasses <= kStars[c].massMax) return static_cast<StarClass>(c);
    return StarClass::O;
}

// Fraccion de sistemas con mas de una estrella segun la clase de la principal.
double Multiplicity(StarClass kind) {
    switch (kind) {
        case StarClass::O: return 0.9;
        case StarClass::B: return 0.8;
        case StarClass::A: return 0.7;
        case StarClass::F: return 0.5;
        case StarClass::G: return 0.45;
        case StarClass::K: return 0.4;
        case StarClass::M: return 0.27;
        default: return 0.3;
    }
}

StarLayout ChooseLayout(StarClass kind, const GeneratorOptions& options, std::mt19937& rng) {
    if (options.layout != StarLayout::Auto) return options.layout;
    if (options.maxStars <= 1 || Uniform(rng, 0, 1) > Multiplicity(kind)) return StarLayout::Single;
    const double u = Uniform(rng, 0, 1);
    if (u < 0.07 && options.maxStars >= 4) return StarLayout::Quadruple;
    if (u < 0.25 && options.maxStars >= 3) return StarLayout::Triple;
    return u < 0.62 ? StarLayout::Close : StarLayout::Wide;
}

double NodeMass(const StarSystemData& s, int node) { return s.nodes[static_cast<std::size_t>(node)].effective.mass; }
bool IsPair(const StarSystemData& s, int node) { return s.nodes[static_cast<std::size_t>(node)].first >= 0; }

// Radio de lo que ocupa un hijo: una estrella o una pareja completa.
double NodeSize(const StarSystemData& s, int node) {
    const StellarNode& n = s.nodes[static_cast<std::size_t>(node)];
    return IsPair(s, node) ? n.separation * (1.0 + n.eccentricity) : n.effective.radius;
}

// Crea un nodo del arbol con sus hijos. Los hijos se resuelven antes que el padre porque su
// separacion decide la minima que puede tener la pareja de fuera.
int BuildStars(StarSystemData& s, const std::vector<int>& members, int parent, bool wide, std::mt19937& rng) {
    const int index = static_cast<int>(s.nodes.size());
    s.nodes.emplace_back();
    s.nodes.back().parent = parent;
    if (members.size() == 1) {
        s.nodes.back().star = members.front();
        s.nodes.back().effective = s.StarAt(members.front());
        return index;
    }

    const std::size_t half = (members.size() + 1) / 2;
    const int a = BuildStars(s, {members.begin(), members.begin() + static_cast<std::ptrdiff_t>(half)}, index, false, rng);
    const int b = BuildStars(s, {members.begin() + static_cast<std::ptrdiff_t>(half), members.end()}, index, false, rng);
    const double m1 = NodeMass(s, a), m2 = NodeMass(s, b), total = m1 + m2;
    const bool outer = wide || IsPair(s, a) || IsPair(s, b);

    // Las parejas cerradas pierden excentricidad por las mareas; las abiertas la conservan.
    const double e = outer ? Uniform(rng, 0.0, 0.55) : std::min(Rayleigh(rng, 0.06), 0.2);
    double separation = (outer ? LogUniform(rng, 25.0, 250.0) : LogUniform(rng, 0.04, 1.2)) * kAstronomicalUnit;
    separation = std::max(separation, 3.5 * (NodeSize(s, a) + NodeSize(s, b)) / (1.0 - e));
    if (IsPair(s, a)) separation = std::max(separation, 1.1 * HierarchyLimit(s.nodes[static_cast<std::size_t>(a)].separation, m1, m2, e));
    if (IsPair(s, b)) separation = std::max(separation, 1.1 * HierarchyLimit(s.nodes[static_cast<std::size_t>(b)].separation, m2, m1, e));

    // Cada una recorre su propia elipse alrededor del baricentro, en lados opuestos y con el
    // mismo periodo. Por eso mu no es G * M: es el valor que da ese periodo a esa elipse.
    const double n2 = kGravity * total / (separation * separation * separation);
    OrbitalElements orbit = RandomOrientation(separation, e, std::min(Rayleigh(rng, 3.0 * kDegree), 15.0 * kDegree), 1.0, rng);
    const auto place = [&](int child, double share, double extraPhase) {
        OrbitalElements o = orbit;
        o.semiMajorAxis = separation * share;
        o.mu = n2 * o.semiMajorAxis * o.semiMajorAxis * o.semiMajorAxis;
        o.argPeriapsis += extraPhase;
        s.nodes[static_cast<std::size_t>(child)].orbit = o;
    };
    place(a, m2 / total, 0.0);
    place(b, m1 / total, kPi);

    StellarNode& node = s.nodes[static_cast<std::size_t>(index)];
    node.first = a;
    node.second = b;
    node.separation = separation;
    node.eccentricity = e;
    node.effective = Combined(s.nodes[static_cast<std::size_t>(a)].effective, s.nodes[static_cast<std::size_t>(b)].effective);
    return index;
}

// Reparte de arriba abajo el espacio donde cabe un planeta. Una pareja cerrada se comporta como
// una sola fuente; en una abierta, cada mitad solo conserva lo que no le quita la otra.
void SetPlanetZones(StarSystemData& s, int index, double limit) {
    StellarNode& n = s.nodes[static_cast<std::size_t>(index)];
    const double edge = PlanetInnerEdge(n.effective);
    const double outerEdge = PlanetOuterEdge(n.effective, edge);
    if (n.first < 0) {
        n.planetInner = edge;
        n.planetOuter = std::min(limit, outerEdge);
        n.bounded = limit < outerEdge;
        return;
    }
    const int a = n.first, b = n.second;
    const double m1 = NodeMass(s, a), m2 = NodeMass(s, b), total = m1 + m2;
    n.planetInner = std::max(edge, 1.05 * CircumbinaryLimit(std::min(m1, m2) / total, n.eccentricity) * n.separation);
    if (n.separation < kCloseBinary) {
        n.planetOuter = std::min(limit, std::max(outerEdge, n.planetInner * 20.0));
        n.bounded = limit < outerEdge;
        return;
    }
    const double separation = n.separation, e = n.eccentricity;
    SetPlanetZones(s, a, std::min(limit, CircumstellarLimit(m2 / total, e) * separation));
    SetPlanetZones(s, b, std::min(limit, CircumstellarLimit(m1 / total, e) * separation));
}

// Coloca los planetas de un anfitrion. El paso entre orbitas sale de cuantos hay y del espacio
// disponible: pocos planetas quedan separados y muchos se aprietan. Si no caben sin romper la
// separacion de Hill, sobran los ultimos.
void PlacePlanets(StarSystemData& system, int host, const StarData& star, double innerEdge, double softEdge, double hardEdge, const GeneratorOptions& options, std::mt19937& rng) {
    const StarClassInfo& info = Info(star.kind);
    const int low = options.planetsMin >= 0 ? options.planetsMin : info.planetsMin;
    const int high = options.planetsMax >= 0 ? options.planetsMax : info.planetsMax;
    const int count = IntRange(rng, low, std::max(low, high));
    if (count <= 0 || innerEdge * 1.3 >= hardEdge) return;

    const double solarMasses = star.mass / kSolarMass;
    const double lockLimit = 0.4 * std::cbrt(solarMasses) * kAstronomicalUnit;
    double a = innerEdge * LogUniform(rng, 1.2, 2.2);
    const double room = std::max(std::min(softEdge, hardEdge) / a, 1.0);
    const double pitch = std::clamp(std::pow(room, 1.0 / std::max(count - 1, 1)), 1.3, 3.2);

    // Con mas planetas, orbitas mas circulares y mas planas.
    const double eccentricityScale = std::min(MedianEccentricity(count) / 0.17, 1.5);  // 0,17 = mediana de la Beta
    const double tilt = std::clamp(8.0 / count, 1.0, 3.0) * kDegree;

    std::vector<BodyData> planets;
    for (int k = 0; k < count; ++k) {
        const BodyData* previous = planets.empty() ? nullptr : &planets.back();
        if (previous) a = previous->orbit.semiMajorAxis * pitch * Uniform(rng, 0.85, 1.15);
        if (a > softEdge && k >= low) break;

        const Sampled sample = SamplePlanet(star, a, rng);
        const bool heavy = sample.kind == PlanetClass::GasGiant || sample.kind == PlanetClass::HotJupiter;
        const bool gasPlanet = sample.kind == PlanetClass::GasGiant || sample.kind == PlanetClass::IceGiant;
        // Beta de Kipping (2013), llevada a la mediana que corresponde a este numero de planetas.
        double e = std::min(Beta(rng, 0.867, 3.03) * eccentricityScale * (heavy ? 0.6 : 1.0), 0.25);
        const double mass = std::min(sample.earthMasses * kEarthMass, kHeaviestPlanet * star.mass);

        if (previous) {
            // a2 - a1 >= K * (a1 + a2) / 2 * h, despejando a2. El tope de masa garantiza half < 1.
            const double h = std::cbrt((mass + previous->mass) / (3.0 * star.mass));
            const double half = kHillSpacing * 1.02 * h * 0.5;
            a = std::max(a, previous->orbit.semiMajorAxis * (1.0 + half) / (1.0 - half));
            a = std::max(a, Apoapsis(previous->orbit) * 1.02 / (1.0 - e));
        }
        e = std::min(e, std::max(1.0 - 1.02 * innerEdge / a, 0.0));  // no entra en la zona inestable
        if (a * (1.0 + e) > hardEdge) break;

        BodyData planet;
        planet.kind = sample.kind;
        planet.host = host;
        planet.mass = mass;
        planet.radius = sample.radius;
        planet.temperature = EquilibriumTemperature(star.luminosity, a);
        planet.materials = sample.materials;
        planet.orbit = RandomOrientation(a, e, std::min(Rayleigh(rng, tilt), 12.0 * kDegree), kGravity * (star.mass + mass), rng);

        planet.tidallyLocked = sample.kind == PlanetClass::HotJupiter || (a < lockLimit && !gasPlanet);
        if (planet.tidallyLocked) planet.spin = LockedSpin(planet.orbit, e > 0.1 ? 1.5 : 1.0);  // resonancia 3:2, como Mercurio
        else if (gasPlanet) planet.spin = FreeSpin(planet.orbit, 9, 18, rng);
        else planet.spin = FreeSpin(planet.orbit, 10, 60, rng);
        planets.push_back(planet);
    }

    for (const BodyData& planet : planets) {
        system.bodies.push_back(planet);
        int moons = 0;
        switch (planet.kind) {
            case PlanetClass::GasGiant: moons = IntRange(rng, 1, std::max(1, options.moonsMax)); break;
            case PlanetClass::IceGiant: moons = IntRange(rng, 1, std::clamp(options.moonsMax, 1, 4)); break;
            case PlanetClass::Terran: case PlanetClass::Rock: case PlanetClass::Ice: moons = IntRange(rng, 0, 1); break;
            case PlanetClass::Desert: moons = IntRange(rng, 0, 2); break;
            default: break;
        }
        AddMoons(system, static_cast<int>(system.bodies.size()) - 1, std::min(moons, options.moonsMax), rng);
    }
}

}  // namespace

const StarClassInfo& Info(StarClass c) { return kStars[static_cast<std::size_t>(c)]; }

SpinModel RandomSpin(const OrbitalElements& orbit, double periodMinHours, double periodMaxHours, std::mt19937& rng) {
    return FreeSpin(orbit, periodMinHours, periodMaxHours, rng);
}

double CircumstellarLimit(double mu, double e) {
    const double m = std::clamp(mu, 0.1, 0.9), x = std::clamp(e, 0.0, 0.7);
    return 0.464 - 0.380 * m - 0.631 * x + 0.586 * m * x + 0.150 * x * x - 0.198 * m * x * x;
}

double CircumbinaryLimit(double mu, double e) {
    const double m = std::clamp(mu, 0.1, 0.9), x = std::clamp(e, 0.0, 0.7);
    return 1.60 + 5.10 * x - 2.22 * x * x + 4.12 * m - 4.27 * x * m - 5.09 * m * m + 4.61 * x * x * m * m;
}

double HierarchyLimit(double innerSeparation, double innerMass, double outerMass, double e) {
    const double q = outerMass / innerMass;
    return innerSeparation * 2.8 / (1.0 - e) * std::pow((1.0 + q) * (1.0 + e) / std::sqrt(1.0 - e), 0.4);
}

double MedianEccentricity(int planets) { return 0.584 * std::pow(static_cast<double>(std::max(planets, 2)), -1.20); }

double DisruptionThreshold(double radius, double density) {
    // Q* = Q0 * R^a + B * rho * R^b, con R en cm, rho en g/cm3 y Q* en erg/g.
    const double centimeters = radius * 1.0e5, gramsPerCc = density / 1.0e12;
    const double erg = 3.5e7 * std::pow(centimeters, -0.38) + 0.3 * gramsPerCc * std::pow(centimeters, 1.36);
    return erg * 1.0e-4;
}

const char* Name(ImpactOutcome outcome) {
    static const char* const names[] = {"Crater", "Fusion", "Rotura parcial", "Destruccion total", "Absorbido"};
    return names[static_cast<std::size_t>(outcome)];
}

ImpactResult ResolveImpact(double massA, double radiusA, double massB, double radiusB, double speed) {
    const double total = massA + massB;
    const double reduced = massA * massB / total;
    const double metersPerSecond = speed * 1000.0;

    ImpactResult r;
    r.speed = speed;
    r.energy = 0.5 * reduced * metersPerSecond * metersPerSecond;
    r.specificEnergy = r.energy / total;

    // Se mide contra el cuerpo que resultaria de juntar los dos.
    const double density = massB / (4.0 / 3.0 * kPi * radiusB * radiusB * radiusB);
    const double merged = radiusB * std::cbrt(total / massB);
    r.threshold = DisruptionThreshold(merged, density);

    // Ley del mayor resto: Leinhardt y Stewart (2012).
    const double ratio = r.specificEnergy / r.threshold;
    const double kept = ratio < 1.8 ? 1.0 - 0.5 * ratio : 0.1 * std::pow(ratio / 1.8, -1.5);
    r.remnantMass = kept * total;
    if (kept >= 0.9) r.outcome = massA / massB < 0.02 ? ImpactOutcome::Crater : ImpactOutcome::Merger;
    else if (kept >= 0.1) r.outcome = ImpactOutcome::Partial;
    else r.outcome = ImpactOutcome::Shattered;
    (void)radiusA;
    return r;
}

double HostMass(const StarSystemData& system, const BodyData& body) { return system.HostOf(body).mass; }

double StellarReach(const StarSystemData& system, int node) {
    double reach = 0;
    for (int n = node; n >= 0 && system.nodes[static_cast<std::size_t>(n)].parent >= 0; n = system.nodes[static_cast<std::size_t>(n)].parent)
        reach += Apoapsis(system.nodes[static_cast<std::size_t>(n)].orbit);
    return reach;
}

double StellarReach(const StarSystemData& system) {
    double reach = 0;
    for (int n = 0; n < static_cast<int>(system.nodes.size()); ++n) reach = std::max(reach, StellarReach(system, n));
    return reach;
}

std::string StellarName(const StarSystemData& system, int node) {
    if (system.nodes.empty()) return "A";
    const StellarNode& n = system.nodes[static_cast<std::size_t>(node)];
    if (n.first < 0) return std::string(1, static_cast<char>('A' + n.star));
    return StellarName(system, n.first) + StellarName(system, n.second);
}

const char* LayoutName(const StarSystemData& system) {
    switch (system.StarCount()) {
        case 1: return "Estrella unica";
        case 2: return system.nodes[0].separation < kCloseBinary ? "Binaria cerrada" : "Binaria abierta";
        case 3: return "Triple";
        case 4: return "Cuadruple";
        default: return "Multiple";
    }
}

bool AddImpactor(StarSystemData& system, int target, double time, double radius, std::mt19937& rng) {
    const BodyData& goal = system.bodies[static_cast<std::size_t>(target)];
    if (goal.parent >= 0) return false;  // solo planetas: comparten marco con su anfitrion
    const StarData& host = system.HostOf(goal);
    const double mu = kGravity * host.mass;
    const KeplerOrbit orbit = MakeOrbit(kNoNode, goal.orbit);
    const Vec3d position = OrbitPosition(orbit, time);
    const Vec3d velocity = OrbitVelocity(orbit, time);
    const Vec3d outward = Normalize(position);
    const Vec3d up = Normalize(Cross(position, velocity));

    // Llega en una orbita eliptica que cruza la del planeta: mas lento y por un costado.
    for (int attempt = 0; attempt < 32; ++attempt) {
        const Vec3d speed = velocity * Uniform(rng, 0.55, 0.9) + up * (Length(velocity) * Uniform(rng, -0.35, 0.35)) + outward * (Length(velocity) * Uniform(rng, -0.45, 0.45));
        BodyData impactor;
        if (!OrbitFromState(position, speed, mu, time, impactor.orbit)) continue;
        if (Periapsis(impactor.orbit) < std::max(host.radius * 2.0, StellarReach(system) * 2.0)) continue;
        impactor.kind = PlanetClass::Asteroid;
        impactor.host = goal.host;
        impactor.radius = radius;
        impactor.materials.iron = 0.2;
        impactor.materials.silicate = 0.8;
        impactor.materials.carbon = 0.3;
        impactor.mass = 4.0 / 3.0 * kPi * radius * radius * radius * Density(impactor.materials);
        impactor.spin = FreeSpin(impactor.orbit, 3, 30, rng);
        impactor.temperature = EquilibriumTemperature(host.luminosity, impactor.orbit.semiMajorAxis, 0.1);
        impactor.impactTime = time;
        impactor.impactTarget = target;
        system.bodies.push_back(impactor);
        return true;
    }
    return false;
}

const char* Name(PlanetClass c) {
    static const char* const names[] = {"Lava", "Roca", "Desierto", "Terrestre", "Hielo", "Jupiter caliente", "Gigante gaseoso", "Gigante de hielo", "Luna", "Cometa", "Asteroide", "Campo de asteroides"};
    return names[static_cast<std::size_t>(c)];
}

double Uniform(std::mt19937& rng, double a, double b) { return a + (b - a) * (rng() / 4294967296.0); }
double LogUniform(std::mt19937& rng, double a, double b) { return std::exp(Uniform(rng, std::log(a), std::log(b))); }

double Normal(std::mt19937& rng) {
    const double u = 1.0 - Uniform(rng, 0, 1);  // (0, 1]
    return std::sqrt(-2.0 * std::log(u)) * std::cos(kTau * Uniform(rng, 0, 1));
}

double Rayleigh(std::mt19937& rng, double sigma) {
    return sigma * std::sqrt(-2.0 * std::log(1.0 - Uniform(rng, 0, 1)));
}

double Beta(std::mt19937& rng, double alpha, double beta) {
    const double x = Gamma(rng, alpha), y = Gamma(rng, beta);
    return x / (x + y);
}

StarClass RandomStarClass(std::mt19937& rng, bool realFrequencies) {
    double total = 0;
    for (const StarClassInfo& s : kStars) total += realFrequencies ? s.frequency : s.showcase;
    double pick = Uniform(rng, 0, total);
    for (std::size_t i = 0; i < std::size(kStars); ++i) {
        pick -= realFrequencies ? kStars[i].frequency : kStars[i].showcase;
        if (pick <= 0.0) return static_cast<StarClass>(i);
    }
    return StarClass::M;
}

double Luminosity(double radiusSolar, double temperature) {
    const double t = temperature / kSolarTemperature;
    return radiusSolar * radiusSolar * t * t * t * t;
}

double EquilibriumTemperature(double luminosity, double distanceKm, double albedo) {
    // 278,5 K es la temperatura de un cuerpo negro a 1 UA del Sol.
    return 278.5 * std::pow(luminosity * (1.0 - albedo), 0.25) * std::sqrt(kAstronomicalUnit / distanceKm);
}

double HillRadius(double a, double e, double bodyMass, double centralMass) {
    return a * (1.0 - e) * std::cbrt(bodyMass / (3.0 * centralMass));
}

double RocheLimit(double primaryRadius, double primaryDensity, double satelliteDensity) {
    return primaryRadius * std::cbrt(2.0 * primaryDensity / satelliteDensity);
}

double Density(const Materials& m) {
    // Mezcla por volumen, en kg/km^3. Sirve para comparar: no incluye la compresion interna.
    const double volume = m.iron / 7.87e12 + m.silicate / 3.3e12 + m.water / 1.0e12 + m.ices / 0.9e12 + m.gas / 0.2e12;
    return 1.0 / std::max(volume, 1e-30);
}

double SurfaceTemperature(double equilibrium, double pressure) {
    // Modelo gris: T^4 crece con la opacidad. El factor reproduce la Tierra con 1 bar.
    return equilibrium * std::pow(1.0 + 0.63 * pressure, 0.25);
}

double Flattening(double mass, double radius, double spinRate, bool gaseous) {
    // q compara la fuerza centrifuga con la gravedad en el ecuador.
    const double q = spinRate * spinRate * radius * radius * radius / (kGravity * mass);
    return std::min((gaseous ? 0.73 : 0.97) * q, 0.2);
}

const char* ElementSymbol(Element e) {
    static const char* const names[] = {"H", "He", "C", "N", "O", "Mg", "Al", "Si", "S", "Ca", "Fe", "Ni"};
    return names[static_cast<int>(e)];
}

const char* ElementName(Element e) {
    static const char* const names[] = {"Hidrógeno", "Helio", "Carbono", "Nitrógeno", "Oxígeno", "Magnesio", "Aluminio", "Silicio", "Azufre", "Calcio", "Hierro", "Níquel"};
    return names[static_cast<int>(e)];
}

namespace {

void Normalize(Composition& c) {
    double total = 0;
    for (const double v : c.share) total += v;
    if (total <= 0.0) return;
    for (double& v : c.share) v /= total;
}

void AddRecipe(Composition& c, double amount, std::initializer_list<std::pair<Element, double>> recipe) {
    if (amount <= 0.0) return;
    for (const auto& [element, share] : recipe) c.share[static_cast<std::size_t>(element)] += amount * share;
}

}  // namespace

Composition ElementsOf(const Materials& m) {
    using E = Element;
    Composition c;
    // Recetas en masa: un nucleo de hierro y niquel, rocas como el manto de la Tierra, agua, hielos de
    // metano, amoniaco y monoxido de carbono, y gas como el del Sol.
    AddRecipe(c, m.iron, {{E::Fe, 0.86}, {E::Ni, 0.06}, {E::S, 0.05}, {E::Si, 0.03}});
    AddRecipe(c, m.silicate, {{E::O, 0.445}, {E::Si, 0.21}, {E::Mg, 0.19}, {E::Fe, 0.07}, {E::Ca, 0.025}, {E::Al, 0.025}, {E::S, 0.005}});
    AddRecipe(c, m.water, {{E::H, 0.112}, {E::O, 0.888}});
    AddRecipe(c, m.ices, {{E::C, 0.30}, {E::H, 0.22}, {E::N, 0.25}, {E::O, 0.23}});
    AddRecipe(c, m.gas, {{E::H, 0.74}, {E::He, 0.245}, {E::O, 0.008}, {E::C, 0.005}, {E::N, 0.002}});
    // Lo que tine la superficie apenas pesa, pero existe.
    AddRecipe(c, 0.02 * m.rust, {{E::Fe, 0.7}, {E::O, 0.3}});
    AddRecipe(c, 0.03 * m.sulfur, {{E::S, 1.0}});
    AddRecipe(c, 0.03 * m.carbon, {{E::C, 1.0}});
    AddRecipe(c, 0.02 * m.methane, {{E::C, 0.75}, {E::H, 0.25}});
    Normalize(c);
    return c;
}

Composition ElementsOf(const StarData& star) {
    using E = Element;
    Composition c;
    AddRecipe(c, star.hydrogen, {{E::H, 1.0}});
    AddRecipe(c, star.helium, {{E::He, 1.0}});
    // Los metales del Sol, por masa; el neon y el resto van con el oxigeno.
    AddRecipe(c, star.metals, {{E::O, 0.47}, {E::C, 0.17}, {E::Fe, 0.097}, {E::N, 0.05}, {E::Si, 0.052}, {E::Mg, 0.046}, {E::S, 0.024}, {E::Ni, 0.006}, {E::Ca, 0.005}, {E::Al, 0.004}});
    Normalize(c);
    return c;
}

namespace {

// A que se une cada elemento, y por tanto con que crece o mengua.
enum class Affinity : std::uint8_t {
    Main,        // uno de los doce de las recetas
    Siderophile, // con el hierro
    Chalcophile, // con el azufre
    Refractory,  // litofilo refractario: con el calcio y el aluminio
    Lithophile,  // litofilo: con el magnesio y el silicio
    Volatile,    // con el agua y los hielos
    Noble,       // gas noble: con el helio, y un poco atrapado en la roca
    Decay,       // hijo radiactivo del uranio y el torio: value es su masa respecto al uranio
    Synthetic,   // no existe en la naturaleza
};

struct ElementData {
    const char* symbol;
    const char* name;
    double value;  // ppm en masa en las condritas CI; Decay: masa por unidad de uranio
    Affinity affinity;
};

// Lodders (2003) para las condritas; el Sol tiene lo mismo respecto al silicio, salvo los gases.
constexpr ElementData kElements[kPeriodicCount] = {
    {"H", "Hidrógeno", 0, Affinity::Main},          {"He", "Helio", 0, Affinity::Main},
    {"Li", "Litio", 1.46, Affinity::Lithophile},    {"Be", "Berilio", 0.0252, Affinity::Refractory},
    {"B", "Boro", 0.713, Affinity::Lithophile},     {"C", "Carbono", 0, Affinity::Main},
    {"N", "Nitrógeno", 0, Affinity::Main},          {"O", "Oxígeno", 0, Affinity::Main},
    {"F", "Flúor", 58.2, Affinity::Volatile},       {"Ne", "Neón", 1.8e-4, Affinity::Noble},
    {"Na", "Sodio", 4982, Affinity::Lithophile},    {"Mg", "Magnesio", 0, Affinity::Main},
    {"Al", "Aluminio", 0, Affinity::Main},          {"Si", "Silicio", 0, Affinity::Main},
    {"P", "Fósforo", 920, Affinity::Siderophile},   {"S", "Azufre", 0, Affinity::Main},
    {"Cl", "Cloro", 698, Affinity::Volatile},       {"Ar", "Argón", 1.33e-3, Affinity::Noble},
    {"K", "Potasio", 530, Affinity::Lithophile},    {"Ca", "Calcio", 0, Affinity::Main},
    {"Sc", "Escandio", 5.83, Affinity::Refractory}, {"Ti", "Titanio", 440, Affinity::Refractory},
    {"V", "Vanadio", 55.0, Affinity::Refractory},   {"Cr", "Cromo", 2590, Affinity::Lithophile},
    {"Mn", "Manganeso", 1910, Affinity::Lithophile},{"Fe", "Hierro", 0, Affinity::Main},
    {"Co", "Cobalto", 502, Affinity::Siderophile},  {"Ni", "Níquel", 0, Affinity::Main},
    {"Cu", "Cobre", 127, Affinity::Chalcophile},    {"Zn", "Cinc", 310, Affinity::Chalcophile},
    {"Ga", "Galio", 9.51, Affinity::Chalcophile},   {"Ge", "Germanio", 33.2, Affinity::Siderophile},
    {"As", "Arsénico", 1.73, Affinity::Chalcophile},{"Se", "Selenio", 19.7, Affinity::Chalcophile},
    {"Br", "Bromo", 3.43, Affinity::Volatile},      {"Kr", "Kriptón", 5.2e-5, Affinity::Noble},
    {"Rb", "Rubidio", 2.13, Affinity::Lithophile},  {"Sr", "Estroncio", 7.74, Affinity::Refractory},
    {"Y", "Itrio", 1.53, Affinity::Refractory},     {"Zr", "Circonio", 3.96, Affinity::Refractory},
    {"Nb", "Niobio", 0.265, Affinity::Refractory},  {"Mo", "Molibdeno", 1.02, Affinity::Siderophile},
    {"Tc", "Tecnecio", 1.0e-12, Affinity::Decay},   {"Ru", "Rutenio", 0.692, Affinity::Siderophile},
    {"Rh", "Rodio", 0.141, Affinity::Siderophile},  {"Pd", "Paladio", 0.588, Affinity::Siderophile},
    {"Ag", "Plata", 0.201, Affinity::Chalcophile},  {"Cd", "Cadmio", 0.675, Affinity::Chalcophile},
    {"In", "Indio", 0.0788, Affinity::Chalcophile}, {"Sn", "Estaño", 1.68, Affinity::Chalcophile},
    {"Sb", "Antimonio", 0.152, Affinity::Chalcophile}, {"Te", "Telurio", 2.33, Affinity::Chalcophile},
    {"I", "Yodo", 0.48, Affinity::Volatile},        {"Xe", "Xenón", 1.74e-4, Affinity::Noble},
    {"Cs", "Cesio", 0.185, Affinity::Lithophile},   {"Ba", "Bario", 2.31, Affinity::Refractory},
    {"La", "Lantano", 0.232, Affinity::Refractory}, {"Ce", "Cerio", 0.621, Affinity::Refractory},
    {"Pr", "Praseodimio", 0.0928, Affinity::Refractory}, {"Nd", "Neodimio", 0.457, Affinity::Refractory},
    {"Pm", "Prometio", 4.0e-19, Affinity::Decay},   {"Sm", "Samario", 0.145, Affinity::Refractory},
    {"Eu", "Europio", 0.0546, Affinity::Refractory},{"Gd", "Gadolinio", 0.198, Affinity::Refractory},
    {"Tb", "Terbio", 0.0356, Affinity::Refractory}, {"Dy", "Disprosio", 0.238, Affinity::Refractory},
    {"Ho", "Holmio", 0.0562, Affinity::Refractory}, {"Er", "Erbio", 0.162, Affinity::Refractory},
    {"Tm", "Tulio", 0.0237, Affinity::Refractory},  {"Yb", "Iterbio", 0.163, Affinity::Refractory},
    {"Lu", "Lutecio", 0.0237, Affinity::Refractory},{"Hf", "Hafnio", 0.115, Affinity::Refractory},
    {"Ta", "Tantalio", 0.0144, Affinity::Refractory}, {"W", "Wolframio", 0.089, Affinity::Siderophile},
    {"Re", "Renio", 0.037, Affinity::Siderophile},  {"Os", "Osmio", 0.486, Affinity::Siderophile},
    {"Ir", "Iridio", 0.47, Affinity::Siderophile},  {"Pt", "Platino", 1.0, Affinity::Siderophile},
    {"Au", "Oro", 0.146, Affinity::Siderophile},    {"Hg", "Mercurio", 0.35, Affinity::Chalcophile},
    {"Tl", "Talio", 0.143, Affinity::Chalcophile},  {"Pb", "Plomo", 2.56, Affinity::Chalcophile},
    {"Bi", "Bismuto", 0.111, Affinity::Chalcophile},{"Po", "Polonio", 7.4e-11, Affinity::Decay},
    {"At", "Astato", 3.0e-20, Affinity::Decay},     {"Rn", "Radón", 1.6e-12, Affinity::Decay},
    {"Fr", "Francio", 3.0e-18, Affinity::Decay},    {"Ra", "Radio", 3.4e-7, Affinity::Decay},
    {"Ac", "Actinio", 1.5e-10, Affinity::Decay},    {"Th", "Torio", 0.0298, Affinity::Refractory},
    {"Pa", "Protactinio", 3.3e-7, Affinity::Decay}, {"U", "Uranio", 0.0078, Affinity::Refractory},
    {"Np", "Neptunio", 1.2e-12, Affinity::Decay},   {"Pu", "Plutonio", 2.0e-11, Affinity::Decay},
    {"Am", "Americio", 0, Affinity::Synthetic},     {"Cm", "Curio", 0, Affinity::Synthetic},
    {"Bk", "Berkelio", 0, Affinity::Synthetic},     {"Cf", "Californio", 0, Affinity::Synthetic},
    {"Es", "Einstenio", 0, Affinity::Synthetic},    {"Fm", "Fermio", 0, Affinity::Synthetic},
    {"Md", "Mendelevio", 0, Affinity::Synthetic},   {"No", "Nobelio", 0, Affinity::Synthetic},
    {"Lr", "Laurencio", 0, Affinity::Synthetic},    {"Rf", "Rutherfordio", 0, Affinity::Synthetic},
    {"Db", "Dubnio", 0, Affinity::Synthetic},       {"Sg", "Seaborgio", 0, Affinity::Synthetic},
    {"Bh", "Bohrio", 0, Affinity::Synthetic},       {"Hs", "Hasio", 0, Affinity::Synthetic},
    {"Mt", "Meitnerio", 0, Affinity::Synthetic},    {"Ds", "Darmstatio", 0, Affinity::Synthetic},
    {"Rg", "Roentgenio", 0, Affinity::Synthetic},   {"Cn", "Copernicio", 0, Affinity::Synthetic},
    {"Nh", "Nihonio", 0, Affinity::Synthetic},      {"Fl", "Flerovio", 0, Affinity::Synthetic},
    {"Mc", "Moscovio", 0, Affinity::Synthetic},     {"Lv", "Livermorio", 0, Affinity::Synthetic},
    {"Ts", "Teneso", 0, Affinity::Synthetic},       {"Og", "Oganesón", 0, Affinity::Synthetic},
};

// Donde va cada uno de los doce de las recetas.
constexpr int kMainZ[kElementCount] = {1, 2, 6, 7, 8, 12, 13, 14, 16, 20, 26, 28};

// Gases nobles del Sol, en fraccion de masa, con su helio (0,2485).
double SolarNoble(int z) {
    switch (z) {
        case 10: return 1.27e-3;
        case 18: return 7.4e-5;
        case 36: return 1.1e-7;
        case 54: return 1.7e-8;
        default: return 0.0;
    }
}

// Un numero de 0 a 1 que solo depende de la semilla y del elemento.
double Hash01(std::uint32_t seed, int z, std::uint32_t salt) {
    std::uint32_t h = seed * 2654435761u ^ (static_cast<std::uint32_t>(z) * 40503u + salt * 2246822519u);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return (static_cast<double>(h) + 0.5) / 4294967296.0;
}

}  // namespace

PeriodicInfo Periodic(int z) {
    z = std::clamp(z, 1, kPeriodicCount);
    PeriodicInfo p{kElements[z - 1].symbol, kElements[z - 1].name, 1, 1};
    if (z <= 2) {
        p.row = 1;
        p.column = z == 1 ? 1 : 18;
    } else if (z <= 10) {
        p.row = 2;
        p.column = z <= 4 ? z - 2 : z + 8;
    } else if (z <= 18) {
        p.row = 3;
        p.column = z <= 12 ? z - 10 : z;
    } else if (z <= 36) {
        p.row = 4;
        p.column = z - 18;
    } else if (z <= 54) {
        p.row = 5;
        p.column = z - 36;
    } else if (z <= 86) {
        if (z >= 57 && z <= 71) {
            p.row = 9;
            p.column = z - 54;  // La en la columna 3
        } else {
            p.row = 6;
            p.column = z <= 56 ? z - 54 : z - 68;
        }
    } else {
        if (z >= 89 && z <= 103) {
            p.row = 10;
            p.column = z - 86;
        } else {
            p.row = 7;
            p.column = z <= 88 ? z - 86 : z - 100;
        }
    }
    return p;
}

PeriodicShares AllElementsOf(const Composition& c, std::uint32_t seed) {
    using E = Element;
    // Cuanto hay de cada familia respecto a una condrita CI. Nada se separa del todo: siempre queda
    // una milesima de contaminacion.
    constexpr double kFloor = 1e-3;
    const double siderophile = std::max(c.Of(E::Fe) / 0.1828, kFloor);
    const double chalcophile = std::max(c.Of(E::S) / 0.0541, kFloor);
    const double refractory = std::max((c.Of(E::Ca) + c.Of(E::Al)) / 0.01757, kFloor);
    const double lithophile = std::max((c.Of(E::Mg) + c.Of(E::Si)) / 0.2024, kFloor);
    const double volatiles = std::max((c.Of(E::H) + c.Of(E::C) + c.Of(E::N)) / 0.0579, kFloor);
    const double helium = c.Of(E::He) / 0.2485;

    PeriodicShares out{};
    double traces = 0;
    for (int z = 1; z <= kPeriodicCount; ++z) {
        const ElementData& e = kElements[z - 1];
        double share = 0;
        switch (e.affinity) {
            case Affinity::Siderophile: share = e.value * 1e-6 * siderophile; break;
            case Affinity::Chalcophile: share = e.value * 1e-6 * chalcophile; break;
            case Affinity::Refractory: share = e.value * 1e-6 * refractory; break;
            case Affinity::Lithophile: share = e.value * 1e-6 * lithophile; break;
            case Affinity::Volatile: share = e.value * 1e-6 * volatiles; break;
            case Affinity::Noble: share = e.value * 1e-6 * volatiles + SolarNoble(z) * helium; break;
            default: break;
        }
        if (share > 0.0) {
            // Cada astro se aparta un poco de la media, y uno de cada treinta elementos forma una veta,
            // que enriquece lo escaso pero no convierte en abundante lo que ya lo era.
            const double spread = (Hash01(seed, z, 1) + Hash01(seed, z, 2) + Hash01(seed, z, 3) - 1.5) * 0.5;
            share *= std::pow(10.0, spread);
            if (Hash01(seed, z, 4) < 1.0 / 30.0) share = std::min(share * (8.0 + 20.0 * Hash01(seed, z, 5)), std::max(share, 5e-3));
        }
        out[static_cast<std::size_t>(z - 1)] = share;
    }
    // Los hijos del uranio, en equilibrio con el que haya.
    const double uranium = out[91];
    for (int z = 1; z <= kPeriodicCount; ++z)
        if (kElements[z - 1].affinity == Affinity::Decay) out[static_cast<std::size_t>(z - 1)] = kElements[z - 1].value * uranium;
    for (const double v : out) traces += v;

    // Las trazas no pueden pasar de lo que dejan los doce; el resto se reparte entre ellos.
    const double scale = traces > 0.2 ? 0.2 / traces : 1.0;
    traces *= scale;
    for (double& v : out) v *= scale;
    for (std::size_t i = 0; i < kElementCount; ++i) out[static_cast<std::size_t>(kMainZ[i] - 1)] = c.share[i] * (1.0 - traces);
    return out;
}

Appearance DeriveAppearance(const BodyData& body) {
    const Materials& m = body.materials;
    const bool gaseous = body.kind == PlanetClass::GasGiant || body.kind == PlanetClass::HotJupiter || body.kind == PlanetClass::IceGiant;
    const Vec3f mineral = Mineral(m);

    Appearance out;
    // Un cuerpo pequeno lo sostiene la roca, no la gravedad: no se achata por girar.
    const bool small = body.kind == PlanetClass::Asteroid || body.kind == PlanetClass::Comet;
    out.flattening = small ? 0.0f : static_cast<float>(Flattening(body.mass, body.radius, body.spin.rate, gaseous));
    out.oceanLevel = static_cast<float>(m.ocean);
    out.iceCap = static_cast<float>(m.iceCap);
    out.cloudCover = static_cast<float>(m.clouds);
    // Con menos gravedad, el relieve se sostiene mejor.
    const double gravity = (body.mass / kEarthMass) / std::pow(body.radius / kEarthRadius, 2.0);
    out.relief = static_cast<float>(std::clamp(1.0 / std::sqrt(std::max(gravity, 0.01)), 0.5, 2.5));

    // En escala logaritmica: de 0,1 milibares a 1 bar. Asi un aire fino, como el de Marte, se ve.
    const double air = m.pressure > 1.0e-6 ? std::clamp((std::log10(m.pressure) + 4.0) / 4.0, 0.0, 1.0) : 0.0;
    switch (body.kind) {
        case PlanetClass::Terran:
            out.kind = SurfaceKind::Terrestrial;
            out.a = Mix({0.06f, 0.24f, 0.56f}, {0.03f, 0.12f, 0.36f}, m.ocean);  // mas agua, mas profundidad
            out.b = m.life ? Mix({0.12f, 0.42f, 0.14f}, mineral, m.rust) : mineral;
            out.c = mineral * 0.8f;
            out.atmosphere = {0.35f, 0.58f, 1.0f};
            out.atmosphereStrength = static_cast<float>(0.75 * air);
            break;
        case PlanetClass::Lava: {
            out.kind = SurfaceKind::Lava;
            out.a = Mix({0.09f, 0.03f, 0.02f}, {0.03f, 0.03f, 0.03f}, m.carbon);
            out.b = mineral * 0.55f;
            out.c = BlackbodyColor(std::max(body.temperature * 1.1, 1100.0));  // la roca fundida brilla por su temperatura
            out.atmosphere = out.c;
            out.atmosphereStrength = 0.35f;
            break;
        }
        case PlanetClass::GasGiant: case PlanetClass::HotJupiter:
            out.kind = SurfaceKind::Gas;
            GasColors(body.temperature, out);
            out.a = Mix(out.a, {0.55f, 0.22f, 0.10f}, m.sulfur * 0.35);
            out.b = Mix(out.b, {0.70f, 0.42f, 0.22f}, m.carbon * 0.25);
            out.atmosphere = out.b;
            out.atmosphereStrength = 0.5f;
            break;
        case PlanetClass::IceGiant:
            out.kind = SurfaceKind::Gas;
            out.a = Mix({0.22f, 0.50f, 0.58f}, {0.08f, 0.16f, 0.55f}, m.methane);  // el metano absorbe el rojo
            out.b = Mix({0.40f, 0.72f, 0.76f}, {0.17f, 0.32f, 0.78f}, m.methane);
            out.c = Mix({0.72f, 0.90f, 0.90f}, {0.45f, 0.62f, 0.92f}, m.methane);
            out.atmosphere = out.b;
            out.atmosphereStrength = 0.6f;
            break;
        default: {
            out.kind = SurfaceKind::Rock;
            out.a = mineral * 0.42f;
            out.b = mineral * 1.25f;
            // Mares de basalto: lava antigua, mas oscura y con mas hierro que el resto.
            const Composition elements = ElementsOf(m);
            out.c = Mix(mineral * 0.38f, Vec3f{0.20f, 0.17f, 0.15f}, std::clamp(elements.Of(Element::Fe) * 2.5, 0.0, 0.8));
            // Sin aire que los borre, los crateres se acumulan. En el sombreador de roca, oceanLevel es su densidad.
            out.oceanLevel = static_cast<float>(std::clamp(0.85 - air * 1.2, 0.1, 0.85));
            out.atmosphere = Mix({0.95f, 0.65f, 0.40f}, {0.70f, 0.85f, 1.0f}, m.iceCap);
            out.atmosphereStrength = static_cast<float>(0.4 * air);
            break;
        }
    }
    if (gaseous) out.relief = static_cast<float>(std::clamp(body.spin.rate * 10.0 * kHour / kTau, 0.5, 1.6));  // mas giro, mas bandas
    return out;
}

double RadiusFromMass(double m) {
    // Chen y Kipping (2017): cuerpos rocosos hasta 2,04 masas terrestres, y despues neptunianos.
    constexpr double kTransition = 2.04;
    if (m <= kTransition) return std::pow(m, 0.279);
    return std::pow(kTransition, 0.279) * std::pow(m / kTransition, 0.589);
}

Vec3f BlackbodyColor(double temperature) {
    // Ajuste de Tanner Helland a los datos de cuerpo negro de Mitchell Charity.
    const double t = std::clamp(temperature, 1000.0, 40000.0) / 100.0;
    const double r = t <= 66.0 ? 255.0 : 329.698727446 * std::pow(t - 60.0, -0.1332047592);
    const double g = t <= 66.0 ? 99.4708025861 * std::log(t) - 161.1195681661 : 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
    const double b = t >= 66.0 ? 255.0 : t <= 19.0 ? 0.0 : 138.5177312231 * std::log(t - 10.0) - 305.0447927307;
    const auto unit = [](double v) { return static_cast<float>(std::clamp(v / 255.0, 0.0, 1.0)); };
    return {unit(r), unit(g), unit(b)};
}

StarData MakeStar(StarClass kind, std::mt19937& rng, double solarMasses) {
    const StarClassInfo& c = Info(kind);
    double u = Uniform(rng, 0, 1);
    if (solarMasses > 0.0) u = std::clamp(std::log(solarMasses / c.massMin) / std::log(c.massMax / c.massMin), 0.0, 1.0);

    StarData s;
    s.kind = kind;
    s.mass = GeoLerp(c.massMin, c.massMax, u) * kSolarMass;
    // Dos estrellas de la misma clase y masa no son identicas: varian un 20-30 %.
    const double radiusNoise = std::exp(std::clamp(Normal(rng) * 0.07, -0.15, 0.15));
    const double tempNoise = std::exp(std::clamp(Normal(rng) * 0.02, -0.05, 0.05));
    const double radiusSolar = GeoLerp(c.radiusMin, c.radiusMax, u) * radiusNoise;
    const double tu = kind == StarClass::WhiteDwarf ? Uniform(rng, 0, 1) : u;  // depende de la edad, no de la masa
    s.radius = radiusSolar * kSolarRadius;
    s.temperature = GeoLerp(c.tempMin, c.tempMax, tu) * tempNoise;
    s.luminosity = Luminosity(radiusSolar, s.temperature);
    s.spinRate = kTau / (LogUniform(rng, c.rotationMin, c.rotationMax) * kDay);

    const double root = std::sqrt(s.luminosity) * kAstronomicalUnit;
    s.habitableInner = 0.99 * root;
    s.habitableOuter = 1.70 * root;
    s.snowLine = 2.7 * root;
    s.metals = LogUniform(rng, 0.006, 0.04);
    s.helium = Uniform(rng, 0.24, 0.29);
    s.hydrogen = 1.0 - s.helium - s.metals;

    if (kind == StarClass::BlackHole) {
        // El radio es el del horizonte; solo brilla lo que cae en su disco.
        s.radius = SchwarzschildRadius(s.mass);
        s.luminosity = LogUniform(rng, 1e-4, 0.01);
        s.blackHoleSpin = Uniform(rng, 0, 1) < 0.5 ? 0.0 : Uniform(rng, 0.3, 0.98);
        s.spinRate = 0.0;
        const double dim = std::sqrt(s.luminosity) * kAstronomicalUnit;
        s.habitableInner = 0.99 * dim;
        s.habitableOuter = 1.70 * dim;
        s.snowLine = 2.7 * dim;
    }
    return s;
}

StarSystemData GenerateSystem(std::mt19937& rng, const GeneratorOptions& options) {
    return GenerateSystem(RandomStarClass(rng, options.realFrequencies), rng, options);
}

StarSystemData GenerateSystem(StarClass kind, std::mt19937& rng, const GeneratorOptions& options) {
    StarSystemData system;
    system.star = MakeStar(kind, rng);
    if (kind == StarClass::BlackHole) {
        if (options.blackHoleSpin >= 0.0) system.star.blackHoleSpin = options.blackHoleSpin;
        if (options.blackHoleMass > 0.0) {
            system.star.mass = options.blackHoleMass * kSolarMass;
            system.star.radius = SchwarzschildRadius(system.star.mass);
        }
    }
    // Las companeras se reparten antes que los planetas: deciden el espacio que les queda.
    const StarLayout layout = ChooseLayout(kind, options, rng);
    const int starCount = layout == StarLayout::Single || layout == StarLayout::Auto ? 1 : layout == StarLayout::Quadruple ? 4 : layout == StarLayout::Triple ? 3 : 2;
    for (int c = 1; c < starCount; ++c) {
        // Razon de masas plana: una companera puede pesar casi lo mismo o mucho menos.
        const double mass = std::max(Uniform(rng, 0.1, 1.0) * system.star.mass / kSolarMass, 0.08);
        system.companions.push_back(MakeStar(ClassForMass(mass), rng, mass));
    }

    if (starCount == 1) {
        const StarData& star = system.star;
        const double inner = PlanetInnerEdge(star);
        PlacePlanets(system, 0, star, inner, PlanetOuterEdge(star, inner), kNoLimit, options, rng);
    } else {
        std::vector<int> members(static_cast<std::size_t>(starCount));
        for (int i = 0; i < starCount; ++i) members[static_cast<std::size_t>(i)] = i;
        BuildStars(system, members, -1, layout == StarLayout::Wide, rng);
        SetPlanetZones(system, 0, kNoLimit);
        for (int n = 0; n < static_cast<int>(system.nodes.size()); ++n) {
            const StellarNode node = system.nodes[static_cast<std::size_t>(n)];
            if (node.planetOuter > 0.0) PlacePlanets(system, n, node.effective, node.planetInner, node.planetOuter, node.bounded ? node.planetOuter : kNoLimit, options, rng);
        }
    }

    const double innerEdge = system.nodes.empty() ? PlanetInnerEdge(system.star) : std::max(system.nodes[0].planetInner, StellarReach(system) * 1.5);
    double outermost = innerEdge * 1.5;
    for (const BodyData& b : system.bodies)
        if (b.parent < 0) outermost = std::max(outermost, Apoapsis(b.orbit) + (system.nodes.empty() ? 0.0 : StellarReach(system, b.host)));
    if (options.cometsMax > 0) AddComets(system, innerEdge, outermost, IntRange(rng, 0, options.cometsMax), rng);
    return system;
}

std::string CheckStability(const StarSystemData& system) {
    const StarData& star = system.star;
    std::vector<const BodyData*> previousOf(std::max<std::size_t>(system.nodes.size(), 1), nullptr);
    char text[160];
    const auto fail = [&](const char* what, std::size_t i) {
        std::snprintf(text, sizeof(text), "cuerpo %zu: %s", i, what);
        return std::string(text);
    };

    // Cada pareja debe aguantar: sus dos miembros no se tocan y las de dentro no se rompen.
    for (std::size_t n = 0; n < system.nodes.size(); ++n) {
        const StellarNode& node = system.nodes[n];
        if (node.first < 0) continue;
        const StellarNode& a = system.nodes[static_cast<std::size_t>(node.first)];
        const StellarNode& b = system.nodes[static_cast<std::size_t>(node.second)];
        const auto sizeOf = [](const StellarNode& x) { return x.first >= 0 ? x.separation * (1.0 + x.eccentricity) : x.effective.radius; };
        if (node.separation * (1.0 - node.eccentricity) <= 2.0 * (sizeOf(a) + sizeOf(b))) return fail("las estrellas se tocan", n);
        if (a.first >= 0 && node.separation < HierarchyLimit(a.separation, a.effective.mass, b.effective.mass, node.eccentricity)) return fail("jerarquia inestable", n);
        if (b.first >= 0 && node.separation < HierarchyLimit(b.separation, b.effective.mass, a.effective.mass, node.eccentricity)) return fail("jerarquia inestable", n);
    }

    for (std::size_t i = 0; i < system.bodies.size(); ++i) {
        const BodyData& b = system.bodies[i];
        if (!(b.orbit.eccentricity >= 0.0) || b.orbit.eccentricity >= 1.0) return fail("excentricidad fuera de rango", i);
        if (!(b.radius > 0.0) || !(b.mass > 0.0) || !(b.orbit.mu > 0.0)) return fail("valor no positivo", i);
        if (b.kind == PlanetClass::Asteroid) continue;  // sus orbitas pueden cruzarse: de ahi los choques
        const double central = CentralMass(system, b);
        const StarData& sun = system.HostOf(b);
        if (std::abs(b.orbit.mu / (kGravity * (central + b.mass)) - 1.0) > 1e-3 && b.kind != PlanetClass::Comet) return fail("mu no coincide con las masas", i);

        if (b.kind == PlanetClass::Comet) {
            if (Periapsis(b.orbit) <= std::max(star.radius, StellarReach(system))) return fail("el cometa toca la estrella", i);
        } else if (b.parent >= 0) {
            const BodyData& host = system.bodies[static_cast<std::size_t>(b.parent)];
            const double hill = HillRadius(host.orbit.semiMajorAxis, host.orbit.eccentricity, host.mass, sun.mass);
            if (Periapsis(b.orbit) <= host.radius + b.radius) return fail("la luna toca el planeta", i);
            if (Apoapsis(b.orbit) >= 0.5 * hill) return fail("la luna escapa de la esfera de Hill", i);
        } else {
            if (Periapsis(b.orbit) <= sun.radius * 1.5) return fail("el planeta roza la estrella", i);
            if (!system.nodes.empty()) {
                const StellarNode& zone = system.nodes[static_cast<std::size_t>(b.host)];
                if (Periapsis(b.orbit) < zone.planetInner * 0.999) return fail("el planeta entra en la zona inestable", i);
                if (zone.bounded && Apoapsis(b.orbit) > zone.planetOuter * 1.001) return fail("el planeta sale de la zona estable", i);
            }
            const BodyData*& previous = previousOf[static_cast<std::size_t>(b.host)];
            if (previous) {
                if (Apoapsis(previous->orbit) >= Periapsis(b.orbit)) return fail("las orbitas se cruzan", i);
                const double mutual = 0.5 * (previous->orbit.semiMajorAxis + b.orbit.semiMajorAxis) * std::cbrt((previous->mass + b.mass) / (3.0 * sun.mass));
                if ((b.orbit.semiMajorAxis - previous->orbit.semiMajorAxis) / mutual < kHillSpacing * 0.99) return fail("menos de 10 radios de Hill", i);
            }
            previous = &b;
        }
    }
    return {};
}

std::string BodyName(const StarSystemData& system, std::size_t body, const std::string& systemName) {
    static const char* const roman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    std::vector<int> planets(std::max<std::size_t>(system.nodes.size(), 1), 0);
    int comet = 0, asteroid = 0;
    std::string planetName;
    for (std::size_t i = 0; i <= body; ++i) {
        const BodyData& b = system.bodies[i];
        if (b.kind == PlanetClass::Comet) {
            ++comet;
            if (i == body) return systemName + " cometa " + std::to_string(comet);
        } else if (b.kind == PlanetClass::Asteroid) {
            ++asteroid;
            if (i == body) return systemName + " asteroide " + std::to_string(asteroid);
        } else if (b.parent < 0) {
            int& planet = planets[static_cast<std::size_t>(b.host)];
            planetName = systemName + " ";
            if (!system.nodes.empty()) planetName += StellarName(system, b.host) + " ";
            planetName += static_cast<char>('b' + planet % 25);
            if (planet >= 25) planetName += std::to_string(planet / 25);
            ++planet;
            if (i == body) return planetName;
        } else if (i == body) {
            int index = 0;
            for (std::size_t j = static_cast<std::size_t>(b.parent) + 1; j < i; ++j) ++index;
            return planetName + " " + roman[index % 10];
        }
    }
    return systemName;
}

std::string DescribeSystem(const StarSystemData& system, const std::string& name) {
    const StarData& s = system.star;
    std::string out;
    char line[320];
    std::snprintf(line, sizeof(line), "%s - estrella %s\n  masa %.3f Msol, radio %.5g Rsol (%.0f km), %.0f K, %.4g Lsol, giro %.3g dias\n  zona habitable %.3f-%.3f UA, linea de hielo %.3f UA\n\n",
                  name.c_str(), Info(s.kind).name, s.mass / kSolarMass, s.radius / kSolarRadius, s.radius, s.temperature, s.luminosity,
                  kTau / s.spinRate / kDay, s.habitableInner / kAstronomicalUnit, s.habitableOuter / kAstronomicalUnit, s.snowLine / kAstronomicalUnit);
    out += line;

    for (int c = 1; c < system.StarCount(); ++c) {
        const StarData& k = system.StarAt(c);
        std::snprintf(line, sizeof(line), "Companera %c - estrella %s\n  masa %.3f Msol, radio %.5g Rsol, %.0f K, %.4g Lsol\n", 'A' + c, Info(k.kind).name, k.mass / kSolarMass, k.radius / kSolarRadius, k.temperature, k.luminosity);
        out += line;
    }
    for (std::size_t n = 0; n < system.nodes.size(); ++n) {
        const StellarNode& node = system.nodes[n];
        if (node.first < 0) continue;
        std::snprintf(line, sizeof(line), "Pareja %s: separacion %.4f UA, e = %.3f\n", StellarName(system, static_cast<int>(n)).c_str(), node.separation / kAstronomicalUnit, node.eccentricity);
        out += line;
    }

    for (std::size_t i = 0; i < system.bodies.size(); ++i) {
        const BodyData& b = system.bodies[i];
        const double period = OrbitPeriod(b.orbit);
        std::snprintf(line, sizeof(line), "%s - %s\n  masa %.4g Mtierra, radio %.0f km, %.0f K%s\n  a = %.6g km (%.5f UA), e = %.4f, i = %.2f grados, periodo %.2f dias\n",
                      BodyName(system, i, name).c_str(), Name(b.kind), b.mass / kEarthMass, b.radius, b.temperature,
                      b.tidallyLocked ? ", acoplado por marea" : "", b.orbit.semiMajorAxis, b.orbit.semiMajorAxis / kAstronomicalUnit,
                      b.orbit.eccentricity, b.orbit.inclination / kDegree, period / kDay);
        out += line;
        const Materials& m = b.materials;
        std::snprintf(line, sizeof(line), "  materiales: hierro %.0f %%, silicatos %.0f %%, agua %.1f %%, hielos %.0f %%, gas %.0f %%; densidad %.2f g/cm3\n  superficie: %.0f K, %.3g bar, oceano %.0f %%, hielo %.0f %%, oxidos %.0f %%, azufre %.0f %%, carbono %.0f %%\n",
                      m.iron * 100, m.silicate * 100, m.water * 100, m.ices * 100, m.gas * 100, Density(m) / 1e12, m.surfaceTemperature, m.pressure,
                      m.ocean * 100, m.iceCap * 100, m.rust * 100, m.sulfur * 100, m.carbon * 100);
        out += line;
        out += OrbitEquation(MakeOrbit(kNoNode, b.orbit));
        out += SpinEquation(b.spin);
        out += "\n";
    }
    return out;
}

}  // namespace eps::astro
