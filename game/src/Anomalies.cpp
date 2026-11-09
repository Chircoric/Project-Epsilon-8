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

void InitCombat(Anomaly& a, const AnomalyRules& rules) {
    switch (a.kind) {
        case AnomalyKind::AlienShip:
            a.maxHull = rules.shipHull;
            a.weapon = WeaponKind::Turret;
            a.armed = true;
            break;
        case AnomalyKind::AlienStation:
            a.maxHull = rules.stationHull;
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
        a.shieldMax = rules.shipShield;
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

        // El principio del juego: un sitio seguro con una estructura y un encuentro lejano.
        if (i == 0) {
            out.push_back(Make(id, system, AnomalyKind::Derelict, Stance::None, RandomPlacement(extent, rng, 0.35, 0.5), rules, rng));
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
    return out;
}

void StepAnomaly(Anomaly& a, const Vec3d& ship, const AnomalyRules& r, double seconds, std::mt19937& rng, bool aggressive, double sight) {
    if (a.kind == AnomalyKind::Derelict) return;
    if (a.hull > 0.0) a.shieldCharge = std::min(a.shieldCharge + a.shieldMax * r.shieldRegen * seconds, a.shieldMax);

    const Vec3d toShip = ship - a.offset;
    const double distance = eps::Length(toShip);
    const bool hostile = a.stance == Stance::Hostile && aggressive;

    if (a.kind == AnomalyKind::AlienShip) {
        a.pursuing = hostile && distance < r.hostileSightRange * sight;
        Vec3d wanted;
        if (a.pursuing) {
            // Se acerca hasta tenerte a tiro y ahi se queda: no embiste.
            const double standoff = r.hostileWeaponRange * 0.8;
            wanted = distance > standoff ? toShip * (r.hostileSpeed / std::max(distance, 1.0)) : Vec3d{};
        } else {
            // Patrulla: da vueltas lentas a su punto y vuelve si se alejo persiguiendo.
            const Vec3d home = a.home - a.offset;
            const double homeDistance = eps::Length(home);
            if (homeDistance > 1.0e6) {
                wanted = home * (r.patrolSpeed / homeDistance);
            } else {
                const double turn = Uniform(rng, -0.5, 0.5);
                a.yaw += static_cast<float>(turn * seconds * 0.01);
                wanted = Vec3d{std::sin(static_cast<double>(a.yaw)), 0.0, std::cos(static_cast<double>(a.yaw))} * r.patrolSpeed;
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
