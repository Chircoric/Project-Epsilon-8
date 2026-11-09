#include "game/Defense.h"

#include <algorithm>
#include <cmath>

namespace game {

const char* Name(ShieldKind kind) {
    static const char* const names[] = {"Arena magnética", "Plasma", "Electromagnético", "Conos ablativos", "Campo de Amor"};
    return names[static_cast<int>(kind)];
}

const char* Name(CoolantKind kind) {
    static const char* const names[] = {"Agua", "Hielo", "Nitrógeno líquido", "Radiadores"};
    return names[static_cast<int>(kind)];
}

bool IsField(ShieldKind kind) { return kind != ShieldKind::Ablative && kind != ShieldKind::Count; }

double ShieldCapacity(const DefenseRules& r, ShieldKind kind, int tier) {
    if (tier <= 0) return 0.0;
    return r.shields[static_cast<int>(kind)].capacity * (1.0 + r.tierCapacity * (tier - 1));
}

double ShieldRegen(const DefenseRules& r, ShieldKind kind, int tier) {
    if (tier <= 0) return 0.0;
    return r.shields[static_cast<int>(kind)].regen * (1.0 + r.tierCapacity * (tier - 1));
}

double CoolantFlow(const ThermalRules& r, CoolantKind kind, int tier) {
    if (tier <= 0) return 0.0;
    const double base[] = {r.waterFlow, r.iceFlow, r.nitrogenFlow, r.radiatorRate};
    return base[static_cast<int>(kind)] * (1.0 + r.tierFlow * (tier - 1));
}

DefenseState MakeDefense(const DefenseRules& rules, const ThermalRules& thermal) {
    DefenseState d;
    for (int k = 0; k < static_cast<int>(ShieldKind::Count); ++k) d.charge[static_cast<std::size_t>(k)] = ShieldCapacity(rules, static_cast<ShieldKind>(k), d.shieldTier[static_cast<std::size_t>(k)]);
    d.water = thermal.waterTank;
    d.nitrogen = thermal.nitrogenTank;
    return d;
}

HitResult AbsorbHit(DefenseState& d, const DefenseRules& r, DamageType type, double amount, bool frontal, bool fieldActive, double silicate) {
    HitResult out;
    if (amount <= 0.0) return out;
    const int t = static_cast<int>(type);
    double left = amount;

    const std::size_t f = static_cast<std::size_t>(d.field);
    if (fieldActive && d.fieldOn && IsField(d.field) && d.shieldTier[f] > 0 && d.charge[f] > 0.0) {
        if (d.field == ShieldKind::Love) {
            out.field = left;
            if (type != DamageType::Photonic) out.reflected = left;  // la luz es su punto debil: esa no vuelve
            left = 0.0;
        } else {
            double stop = std::min(left * r.shields[f].absorb[t], d.charge[f]);
            if (d.field == ShieldKind::Dust) stop = std::min(stop, silicate / r.dustPerDamage);  // sin polvo no hay arena
            stop = std::max(stop, 0.0);
            d.charge[f] -= stop;
            out.field = stop;
            out.silicate = d.field == ShieldKind::Dust ? stop * r.dustPerDamage : 0.0;
            left -= stop;
        }
    }

    const std::size_t a = static_cast<std::size_t>(ShieldKind::Ablative);
    if (left > 0.0 && frontal && d.shieldTier[a] > 0 && d.charge[a] > 0.0) {
        const double stop = std::min(left * r.shields[a].absorb[t], d.charge[a]);
        d.charge[a] -= stop;
        out.plates = stop;
        left -= stop;
    }
    out.hull = left;
    return out;
}

CoolingResult StepCooling(DefenseState& d, const ThermalRules& r, double seconds, double heatIn, double radiators, double ice) {
    CoolingResult out;
    if (seconds <= 0.0) return out;
    d.heat = std::max(d.heat + heatIn, 0.0);

    // Los radiadores siempre trabajan.
    const auto take = [&](int k, double amount) {
        amount = std::clamp(amount, 0.0, d.heat);
        d.heat -= amount;
        out.removed[k] += amount;
        return amount;
    };
    if (d.coolantOn[3]) take(3, CoolantFlow(r, CoolantKind::Radiator, d.coolantTier[3]) * std::clamp(radiators, 0.0, 1.0) * seconds);

    // Los refrigerantes que se gastan, solo por encima del umbral: primero lo barato.
    const double floor = r.limit * r.coolStart;
    const auto excess = [&] { return std::max(d.heat - floor, 0.0); };
    if (d.coolantOn[0] && d.water > 0.0) {
        const double got = take(0, std::min({CoolantFlow(r, CoolantKind::Water, d.coolantTier[0]) * seconds, d.water * r.waterHeat, excess()}));
        d.water = std::max(d.water - got / r.waterHeat, 0.0);
    }
    if (d.coolantOn[1] && ice > 0.0) {
        const double got = take(1, std::min({CoolantFlow(r, CoolantKind::Ice, d.coolantTier[1]) * seconds, ice * r.iceHeat, excess()}));
        out.iceUsed = std::min(got / r.iceHeat, ice);
        d.water = std::min(d.water + out.iceUsed, r.waterTank);  // el hielo fundido llena el deposito
    }
    if (d.coolantOn[2] && d.nitrogen > 0.0) {
        const double got = take(2, std::min({CoolantFlow(r, CoolantKind::Nitrogen, d.coolantTier[2]) * seconds, d.nitrogen * r.nitrogenHeat, excess()}));
        d.nitrogen = std::max(d.nitrogen - got / r.nitrogenHeat, 0.0);
    }
    d.nitrogen *= std::exp(-r.nitrogenBoil * seconds);  // se evapora aunque no se use
    d.heat = std::min(d.heat, r.maximum);
    return out;
}

bool Overheated(const DefenseState& d, const ThermalRules& r) { return d.heat > r.limit; }

}  // namespace game
