#include "game/Anomalies.h"

#include <algorithm>
#include <cmath>

#include "epsilon/Astro.h"

namespace game {

using eps::astro::Uniform;
using eps::Vec3d;

namespace {

Vec3d RandomPlacement(double extent, std::mt19937& rng, double near, double far) {
    const double angle = Uniform(rng, 0, 2.0 * eps::kPi);
    const double reach = extent * Uniform(rng, near, far);
    return Vec3d{std::cos(angle) * reach, Uniform(rng, -0.08, 0.08) * reach, std::sin(angle) * reach};
}

// Junto a la estrella, entre esas UA. Gasta los mismos tres numeros que RandomPlacement, asi que lo
// que viene despues sale igual con cada semilla.
Vec3d NearStar(std::mt19937& rng, double nearAu, double farAu) {
    const double angle = Uniform(rng, 0, 2.0 * eps::kPi);
    const double reach = eps::astro::kAstronomicalUnit * Uniform(rng, nearAu, farAu);
    return Vec3d{std::cos(angle) * reach, Uniform(rng, -0.08, 0.08) * reach, std::sin(angle) * reach};
}

Anomaly Make(int& id, int system, AnomalyKind kind, Stance stance, const Vec3d& offset, const AnomalyRules& rules, std::mt19937& rng) {
    Anomaly a;
    a.id = id++;
    a.kind = kind;
    a.stance = stance;
    a.system = system;
    a.offset = offset;
    a.home = offset;
    a.yaw = static_cast<float>(Uniform(rng, 0, 6.28));
    switch (kind) {
        case AnomalyKind::AlienShip:
            a.radius = stance == Stance::Hostile ? 0.5 : 0.7;
            a.name = stance == Stance::Hostile ? "Nave hostil" : "Nave neutral";
            break;
        case AnomalyKind::AlienStation:
            a.radius = 2.5;
            a.name = stance == Stance::Hostile ? "Bastion hostil" : "Estacion neutral";
            break;
        case AnomalyKind::Derelict:
            a.radius = 12.0;
            a.name = "Estructura abandonada";
            a.emitter = true;
            static_cast<void>(Uniform(rng, 0.0, 1.0));  // la cantidad de antes: mantiene el reparto de cada semilla
            a.cold = true;  // lleva eones apagada: hay que buscarla
            break;
    }
    InitCombat(a, rules);
    a.name += " " + std::to_string(a.id);
    // Cuanto mas lejos del principio, mejor escondido: el escaner tambien progresa.
    if (a.cold || kind == AnomalyKind::AlienStation) a.signature = system == 0 ? 1 : 1 + static_cast<int>(rng() % 4u);
    return a;
}

}  // namespace

int TeamOf(const Anomaly& a) { return a.stance == Stance::Hostile ? 1 : 2; }

namespace {
std::shared_ptr<const ZoneLayout> gAlienZones[kAlienModelCount];
}  // namespace

int AlienModelOf(const Anomaly& a) {
    const bool hostile = a.stance == Stance::Hostile;
    switch (a.kind) {
        case AnomalyKind::AlienShip:
            if (HasClassModel(a.shipClass)) return static_cast<int>(ClassModel(a.shipClass, hostile));
            return static_cast<int>(hostile ? AlienModel::HostileShip : AlienModel::NeutralShip);
        case AnomalyKind::AlienStation:
            if (IsArmorPlanet(a)) return -1;  // una esfera blindada sin zonas: GameModels la dibuja aparte
            return static_cast<int>(hostile ? AlienModel::HostileStation : AlienModel::NeutralStation);
        default: return -1;
    }
}

void SetAlienZones(AlienModel model, std::shared_ptr<const ZoneLayout> layout) {
    const int m = static_cast<int>(model);
    if (m >= 0 && m < kAlienModelCount) gAlienZones[m] = std::move(layout);
}

const ZoneLayout* AlienZones(AlienModel model) {
    const int m = static_cast<int>(model);
    return m >= 0 && m < kAlienModelCount ? gAlienZones[m].get() : nullptr;
}

const ZoneLayout* ZonesOf(const Anomaly& a) {
    const int m = AlienModelOf(a);
    return m >= 0 ? gAlienZones[m].get() : nullptr;
}

void AssignShipClass(Anomaly& a) {
    static const ShipClassId classes[] = {ShipClassId::Drone, ShipClassId::Corvette, ShipClassId::Frigate};
    a.shipClass = classes[static_cast<unsigned>(a.id) % 3u];
    a.variant = a.shipClass == ShipClassId::Frigate ? ShipVariant::Light : ShipVariant::Normal;
}

void InitCombat(Anomaly& a, const AnomalyRules& rules) {
    switch (a.kind) {
        case AnomalyKind::AlienShip:
            AssignShipClass(a);
            a.maxHull = ClassOf(a).hull * rules.referenceHull;
            a.weapon = WeaponKind::Turret;
            a.armed = true;
            break;
        case AnomalyKind::AlienStation:
            if (a.stationLevel < 1 || a.stationLevel > kStationLevels) a.stationLevel = StationLevelFor(a.id, rules);
            a.maxHull = StationHull(rules, a.stationLevel);
            a.weapon = WeaponKind::Missile;
            a.armed = true;
            break;
        case AnomalyKind::Derelict:
            a.maxHull = 0.0;
            a.armed = false;
            break;
    }
    a.hull = a.maxHull;
    InitShield(a, rules);
}

void InitShield(Anomaly& a, const AnomalyRules& rules) {
    static const ShieldKind kinds[] = {ShieldKind::Dust, ShieldKind::Plasma, ShieldKind::EM};
    a.shieldMax = 0.0;
    if (a.kind == AnomalyKind::AlienShip) {
        a.shield = kinds[static_cast<unsigned>(a.id) % 3u];  // por su id: no gasta numeros aleatorios
        a.shieldMax = ClassOf(a).shield * rules.referenceShield;
    } else if (a.kind == AnomalyKind::AlienStation) {
        a.shield = ShieldKind::EM;
        a.shieldMax = rules.stationShield;
    }
    a.shieldCharge = a.shieldMax;
}

double ShieldAnomaly(Anomaly& a, const DefenseRules& rules, DamageType type, double amount) {
    if (a.shieldCharge <= 0.0 || amount <= 0.0) return amount;
    const double stop = std::min(amount * rules.shields[static_cast<int>(a.shield)].absorb[static_cast<int>(type)], a.shieldCharge);
    a.shieldCharge -= stop;
    return amount - stop;
}

const char* Name(AnomalyKind kind) {
    static const char* const names[] = {"Nave alienigena", "Estacion alienigena", "Estructura abandonada"};
    return names[static_cast<int>(kind)];
}

const char* Name(Stance stance) {
    static const char* const names[] = {"Abandonada", "Neutral", "Hostil"};
    return names[static_cast<int>(stance)];
}

std::vector<Anomaly> GenerateAnomalies(const std::vector<double>& extents, const AnomalyRules& rules, std::uint32_t seed) {
    std::vector<Anomaly> out;
    int id = 1;
    for (std::size_t i = 0; i < extents.size(); ++i) {
        const int system = static_cast<int>(i);
        const double extent = extents[i];
        std::mt19937 rng(seed * 2654435761u + static_cast<std::uint32_t>(i) * 40503u + 7u);

        // El principio del juego: un sitio seguro con una estructura y un encuentro lejano. La estructura,
        // al alcance de un barrido desde la orbita de la estrella, donde el tutorial pide la central.
        if (i == 0) {
            out.push_back(Make(id, system, AnomalyKind::Derelict, Stance::None, NearStar(rng, rules.firstDerelictMinAu, rules.firstDerelictMaxAu), rules, rng));
            out.push_back(Make(id, system, AnomalyKind::AlienShip, Stance::Neutral, RandomPlacement(extent, rng, 0.5, 0.7), rules, rng));
            out.push_back(Make(id, system, AnomalyKind::AlienShip, Stance::Hostile, RandomPlacement(extent, rng, 1.2, 1.5), rules, rng));
            continue;
        }

        if (Uniform(rng, 0, 1) < 0.55) out.push_back(Make(id, system, AnomalyKind::Derelict, Stance::None, RandomPlacement(extent, rng, 0.2, 1.3), rules, rng));
        if (Uniform(rng, 0, 1) < 0.5) {
            const Stance stance = Uniform(rng, 0, 1) < 0.6 ? Stance::Hostile : Stance::Neutral;
            out.push_back(Make(id, system, AnomalyKind::AlienStation, stance, RandomPlacement(extent, rng, 0.3, 1.1), rules, rng));
        }
        const int ships = static_cast<int>(rng() % 4u);
        for (int s = 0; s < ships; ++s) {
            const Stance stance = Uniform(rng, 0, 1) < 0.55 ? Stance::Hostile : Stance::Neutral;
            out.push_back(Make(id, system, AnomalyKind::AlienShip, stance, RandomPlacement(extent, rng, 0.2, 1.4), rules, rng));
        }
    }
    // El planeta coraza va al final: no cambia los ids ni el reparto de lo anterior.
    Anomaly planet;
    if (PlaceArmorPlanet(extents, rules, seed, id, planet)) out.push_back(planet);
    return out;
}

// ---- Estaciones por nivel y planeta coraza ----

namespace {
// Un numero de 0 a 1 que sale de un entero, sin tocar ningun generador.
double HashUnit(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<double>(x) / 4294967296.0;
}
}  // namespace

double StationHull(const AnomalyRules& rules, int level) {
    const int l = std::clamp(level, 1, kStationLevels);
    return rules.stationLevelHull[l - 1] * rules.referenceHull;
}

int StationLevelFor(int id, const AnomalyRules& rules) {
    const double u = HashUnit(static_cast<std::uint32_t>(id) * 2654435761u + 0x51u);
    double total = 0.0;
    for (const double share : rules.stationLevelShare) total += share;
    double acc = 0.0;
    for (int l = 0; l < kStationLevels - 1; ++l) {
        acc += rules.stationLevelShare[l] / std::max(total, 1e-12);
        if (u < acc) return l + 1;
    }
    return kStationLevels - 1;
}

void MakeArmorPlanet(Anomaly& a, const AnomalyRules& rules) {
    a.kind = AnomalyKind::AlienStation;
    a.stance = Stance::Hostile;
    a.stationLevel = kArmorPlanetLevel;
    a.radius = rules.armorPlanetRadius;
    a.velocity = {};
    a.name = std::string(kStationLevelNames[kArmorPlanetLevel - 1]) + " " + std::to_string(a.id);
    a.signature = rules.armorPlanetSignature;
    InitCombat(a, rules);
}

bool PlaceArmorPlanet(const std::vector<double>& extents, const AnomalyRules& rules, std::uint32_t seed, int id, Anomaly& out) {
    if (extents.size() < 2) return false;
    if (HashUnit(seed * 0x9e3779b9u + 0xc0aau) >= rules.armorPlanetChance) return false;
    const std::size_t half = extents.size() / 2;
    const std::size_t system = half + static_cast<std::size_t>(HashUnit(seed ^ 0x5eed0c0au) * static_cast<double>(extents.size() - half)) % (extents.size() - half);
    const double angle = HashUnit(seed + 0x0a4e1u) * 2.0 * eps::kPi;
    // Por fuera de sus planetas, con su radio de holgura: no se mueve y no debe pisar ninguna orbita.
    const double reach = extents[system] * 1.3 + rules.armorPlanetRadius * 2.0;
    out = Anomaly{};
    out.id = id;
    out.system = static_cast<int>(system);
    out.offset = out.home = Vec3d{std::cos(angle) * reach, 0.0, std::sin(angle) * reach};
    MakeArmorPlanet(out, rules);
    return true;
}

// ---- Alerta, rendicion y revancha ----

int AlertRegion(int system, const AlertRules& rules) { return system / std::max(rules.regionSize, 1); }

void ResizeSpecies(SpeciesState& s, std::size_t systems) {
    s.alert.resize(systems, 0.0);
    s.lastAttack.resize(systems, -1.0);
}

void RaiseAlert(SpeciesState& s, int system, double amount, double now, const AlertRules& rules, const std::vector<Vec3d>* positions, int spread) {
    const int count = static_cast<int>(s.alert.size());
    if (system < 0 || system >= count || amount <= 0.0) return;
    const auto raise = [&](int k) {
        s.alert[static_cast<std::size_t>(k)] = std::min(s.alert[static_cast<std::size_t>(k)] + amount, 1.0);
        s.lastAttack[static_cast<std::size_t>(k)] = now;
    };
    const int region = AlertRegion(system, rules);
    for (int k = 0; k < count; ++k)
        if (AlertRegion(k, rules) == region) raise(k);
    if (spread <= 0 || !positions || positions->size() < s.alert.size()) return;
    // Los spread mas cercanos, el propio incluido: a los que ya estaban en la region no se les suma dos veces.
    std::vector<int> order(static_cast<std::size_t>(count));
    for (int k = 0; k < count; ++k) order[static_cast<std::size_t>(k)] = k;
    const Vec3d at = (*positions)[static_cast<std::size_t>(system)];
    std::sort(order.begin(), order.end(), [&](int x, int y) { return eps::Length((*positions)[static_cast<std::size_t>(x)] - at) < eps::Length((*positions)[static_cast<std::size_t>(y)] - at); });
    for (int i = 0; i < std::min(spread, count); ++i) {
        const int k = order[static_cast<std::size_t>(i)];
        if (AlertRegion(k, rules) != region) raise(k);
    }
}

double DestroyRaise(const AlertRules& rules, int stationLevel) {
    if (stationLevel < 1 || stationLevel > kStationLevels) return rules.shipRaise;
    return rules.destroyRaise[stationLevel - 1];
}

namespace {

void DecayOne(SpeciesState& s, std::size_t k, double now, double hours, const AlertRules& rules) {
    if (s.alert[k] <= 0.0) return;
    const bool calm = s.lastAttack[k] < 0.0 || now - s.lastAttack[k] > rules.calmHours * 3600.0;
    s.alert[k] *= std::exp(-(calm ? rules.calmDecayPerHour : rules.decayPerHour) * hours);
    if (s.alert[k] < 1e-4) s.alert[k] = 0.0;
}

}  // namespace

void DecayAlert(SpeciesState& s, double now, double seconds, const AlertRules& rules) {
    for (std::size_t k = 0; k < s.alert.size(); ++k) DecayOne(s, k, now, seconds / 3600.0, rules);
}

void DecayAlertUntil(SpeciesState& s, double now, double from, const AlertRules& rules) {
    s.decayedUntil.resize(s.alert.size(), -1.0);
    for (std::size_t k = 0; k < s.alert.size(); ++k) {
        double& stamp = s.decayedUntil[k];
        if (stamp < 0.0) stamp = std::min(from, now);
        if (now <= stamp) continue;
        DecayOne(s, k, now, (now - stamp) / 3600.0, rules);
        stamp = now;
    }
}

bool TriggersSurrender(const Anomaly& a) {
    return IsArmorPlanet(a) || (a.kind == AnomalyKind::AlienShip && a.shipClass == ShipClassId::SupremeCapital);
}

void BeginSurrender(SpeciesState& s, double now, const AlertRules& rules) {
    s.surrenderUntil = now + rules.surrenderHours * 3600.0;
    s.revengePending = true;
}

void ApplyBoost(Anomaly& a, double factor) {
    if (factor == 1.0 || a.kind != AnomalyKind::AlienShip) return;
    a.boost *= factor;
    a.maxHull *= factor;
    a.hull *= factor;
    a.shieldMax *= factor;
    a.shieldCharge *= factor;
}

double RevengeBoost(const SpeciesState& s, const AlertRules& rules) { return std::pow(rules.revengeFactor, s.revenges); }

bool EndSurrender(SpeciesState& s, std::vector<Anomaly>& anomalies, double now, const AlertRules& rules) {
    if (s.surrenderUntil < 0.0 || now < s.surrenderUntil) return false;
    s.surrenderUntil = -1.0;
    if (!s.revengePending) return false;
    s.revengePending = false;
    ++s.revenges;
    for (Anomaly& a : anomalies)
        if (a.hull > 0.0) ApplyBoost(a, rules.revengeFactor);
    return true;
}

void StepAnomaly(Anomaly& a, const Vec3d& ship, const AnomalyRules& r, double seconds, std::mt19937& rng, bool aggressive, double sight) {
    if (a.kind == AnomalyKind::Derelict) return;
    if (a.hull > 0.0) a.shieldCharge = std::min(a.shieldCharge + a.shieldMax * r.shieldRegen * seconds, a.shieldMax);

    const Vec3d toShip = ship - a.offset;
    const double distance = eps::Length(toShip);
    const bool hostile = a.stance == Stance::Hostile && aggressive;

    if (a.kind == AnomalyKind::AlienShip) {
        // La velocidad sale de la clase; la patrulla guarda con ella la proporcion de siempre.
        const double speed = ClassOf(a).speed * a.boost;
        const double patrol = r.patrolSpeed * speed / r.hostileSpeed;
        a.pursuing = hostile && distance < r.hostileSightRange * sight;
        Vec3d wanted;
        if (a.pursuing) {
            // Se acerca hasta tenerte a tiro y ahi se queda: no embiste.
            const double standoff = r.hostileWeaponRange * 0.8;
            wanted = distance > standoff ? toShip * (speed / std::max(distance, 1.0)) : Vec3d{};
        } else {
            // Patrulla: da vueltas lentas a su punto y vuelve si se alejo persiguiendo.
            const Vec3d home = a.home - a.offset;
            const double homeDistance = eps::Length(home);
            if (homeDistance > 1.0e6) {
                wanted = home * (patrol / homeDistance);
            } else {
                const double turn = Uniform(rng, -0.5, 0.5);
                a.yaw += static_cast<float>(turn * seconds * 0.01);
                wanted = Vec3d{std::sin(static_cast<double>(a.yaw)), 0.0, std::cos(static_cast<double>(a.yaw))} * patrol;
            }
        }
        a.velocity += (wanted - a.velocity) * (1.0 - std::exp(-seconds / 4.0));
        a.offset += a.velocity * seconds;
        if (eps::Length(a.velocity) > 1.0) a.yaw = static_cast<float>(std::atan2(a.velocity.x, a.velocity.z));
    }

}

bool Explore(Anomaly& a, const AnomalyRules& r, double hours) {
    if (a.kind != AnomalyKind::Derelict || !a.emitter) return false;
    a.explored += hours;
    if (a.explored < r.exploreHours) return false;
    a.emitter = false;
    return true;
}

}  // namespace game
