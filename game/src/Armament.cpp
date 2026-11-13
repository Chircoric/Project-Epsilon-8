#include "game/Armament.h"

#include <cmath>

#include "game/Units.h"

namespace game {

const char* Name(ArmType type) {
    static const char* const names[] = {"Coilgun", "Railgun espinal", "Railgun 360", "Bahía de misiles", "Misil nuclear"};
    return names[static_cast<int>(type)];
}

const HumanWeaponSpec* HumanWeaponOf(const ArmamentRules& rules, ShipModuleKind kind) {
    if (!IsHumanWeapon(kind)) return nullptr;
    return &rules.weapons[static_cast<int>(kind) - static_cast<int>(kFirstHumanWeapon)];
}

std::string BlueprintName(int id) {
    if (id < 0 || id >= kBlueprintCount) return "Plano desconocido";
    return std::string("Plano: ") + Name(static_cast<ArmType>(id / 2)) + (id % 2 ? " pesada" : " ligera");
}

bool HasBlueprint(const ShipLoadout& loadout, int id) { return id >= 0 && id < kBlueprintCount && (loadout.blueprints >> id & 1u) != 0; }

void GrantBlueprint(ShipLoadout& loadout, int id) {
    if (id >= 0 && id < kBlueprintCount) loadout.blueprints |= 1u << id;
}

double BlueprintEnergy(const ArmamentRules& rules, int id) {
    if (id < 0 || id >= kBlueprintCount) return 0.0;
    return rules.blueprintEnergy[id / 2] * (id % 2 ? rules.blueprintHeavy : 1.0);
}

double BlueprintMetal(const ArmamentRules& rules, int id) { return BlueprintEnergy(rules, id) * rules.blueprintMetal; }

bool MountAllowed(const ArmamentRules& rules, const HumanWeaponSpec& weapon, ShipClassId id, ShipVariant variant, int spinalMounted, std::string* why) {
    const auto fail = [&](const char* reason) {
        if (why) *why = reason;
        return false;
    };
    if (weapon.heavy && !ShipClassOf(id, variant).heavyWeapons) return fail("Las armas pesadas solo las montan las naves de categoría pesada.");
    if (weapon.type == ArmType::RailSpinal) {
        if (!rules.spinalClass[static_cast<int>(id)]) return fail("Esta clase de nave no admite una railgun espinal.");
        if (spinalMounted >= rules.spinalMax) return fail("Ya lleva una railgun espinal: va fija al eje y solo cabe una.");
    }
    return true;
}

int DefensePoints(const ArmamentRules& rules, ShipClassId id, ShipVariant variant) { return ShipClassOf(id, variant).weaponSlots * rules.pointsPerSlot; }

double PointDefenseRate(const ArmamentRules& rules, int points, int platforms) { return rules.pointRate * points + rules.platformRate * platforms; }

std::string MissingElements(const ElementStock& stock, const ElementCost* costs, int count, double times) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (costs[i].z == 0) continue;
        const double need = costs[i].kg * times, have = stock.Get(costs[i].z);
        if (have + 1e-9 >= need) continue;
        if (!out.empty()) out += ", ";
        out += std::string(eps::astro::Periodic(costs[i].z).symbol) + " " + ElementMassText(need - have);
    }
    return out;
}

bool PayElements(ElementStock& stock, const ElementCost* costs, int count, double times) {
    if (!MissingElements(stock, costs, count, times).empty()) return false;
    for (int i = 0; i < count; ++i)
        if (costs[i].z != 0) stock.Add(costs[i].z, -costs[i].kg * times);
    return true;
}

void RefundElements(ElementStock& stock, const ElementCost* costs, int count, double times) {
    for (int i = 0; i < count; ++i)
        if (costs[i].z != 0) stock.Add(costs[i].z, costs[i].kg * times);
}

std::string ElementCostText(const ElementCost* costs, int count, double times) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (costs[i].z == 0) continue;
        if (!out.empty()) out += ", ";
        out += std::string(eps::astro::Periodic(costs[i].z).symbol) + " " + ElementMassText(costs[i].kg * times);
    }
    return out;
}

MissileCost MissileBatch(const HumanWeaponSpec& weapon, int count) {
    MissileCost c;
    c.energy = weapon.missileEnergy * count;
    c.metal = weapon.missileMass * count;
    return c;
}

}  // namespace game
