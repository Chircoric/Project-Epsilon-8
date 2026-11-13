// Armament.h - Armamento humano (docs/diseno-combate.md, fase B): reglas de montaje, costes y defensa puntual.
//
// Son funciones sobre los datos de ArmamentRules (Balance.h), asi que se prueban solas. La sesion
// (GameSession) las usa al montar un modulo, fabricar un plano o un misil y disparar.
//
//   Coilgun         debil y de cadencia alta.
//   Railgun         mas potente y mas lenta que la coilgun. La espinal va fija al eje (hay que girar la nave
//                   entera) y no la admite cualquier clase; la 360 va en torreta movil y es peor en todo.
//   Bahia de misiles / misil nuclear
//                   dano explosivo y nuclear, guiados y derribables por la defensa puntual. Una bahia lleva 10,
//                   25 o 50 misiles; cada uno se fabrica con energia, metal y elementos. El nuclear es de un solo uso.
//   Defensa puntual puntos repartidos por la nave (modulo aparte de las torretas) y plataformas que se montan
//                   en huecos de torreta clasica.
//
// Cada arma tiene variante ligera y pesada; la pesada solo la montan las clases con heavyWeapons. Montar una pide
// su plano (se fabrica en la estacion origami), metal a granel con su masa real y unos elementos escasos.
#pragma once

#include <string>

#include "game/Balance.h"
#include "game/Economy.h"
#include "game/ShipModules.h"

namespace game {

const char* Name(ArmType type);

// La ficha del arma que es ese modulo, o nullptr si no es un arma humana.
const HumanWeaponSpec* HumanWeaponOf(const ArmamentRules& rules, ShipModuleKind kind);

// Un plano por tipo y tamano (las bahias de 10, 25 y 50 comparten el suyo): 0 a 2 * kArmTypeCount - 1.
inline int BlueprintId(const HumanWeaponSpec& weapon) { return static_cast<int>(weapon.type) * 2 + (weapon.heavy ? 1 : 0); }
inline constexpr int kBlueprintCount = kArmTypeCount * 2;
std::string BlueprintName(int id);  // "Plano: coilgun pesada"
bool HasBlueprint(const ShipLoadout& loadout, int id);
void GrantBlueprint(ShipLoadout& loadout, int id);
double BlueprintEnergy(const ArmamentRules& rules, int id);  // EJ
double BlueprintMetal(const ArmamentRules& rules, int id);   // kt

// Si esa clase de nave puede montarla. spinalMounted: cuantas espinales lleva ya (sin contar la que se cambia).
bool MountAllowed(const ArmamentRules& rules, const HumanWeaponSpec& weapon, ShipClassId id, ShipVariant variant, int spinalMounted, std::string* why = nullptr);

// Puntos de defensa puntual que tiene una clase: tantos por hueco de arma.
int DefensePoints(const ArmamentRules& rules, ShipClassId id, ShipVariant variant);

// Probabilidad por segundo de que la defensa puntual derribe un guiado dentro de su alcance:
// los puntos montados mas las plataformas que funcionan.
double PointDefenseRate(const ArmamentRules& rules, int points, int platforms);

// Elementos: lo que falta de una lista de costes, multiplicada por times, o una cadena vacia si hay de sobra.
std::string MissingElements(const ElementStock& stock, const ElementCost* costs, int count, double times = 1.0);
bool PayElements(ElementStock& stock, const ElementCost* costs, int count, double times = 1.0);
void RefundElements(ElementStock& stock, const ElementCost* costs, int count, double times);
std::string ElementCostText(const ElementCost* costs, int count, double times = 1.0);  // "74 W 2 t, 75 Re 150 kg"

// Lo que cuesta fabricar n misiles de esa bahia: energia (EJ), metal (kt) y elementos (por misil).
struct MissileCost {
    double energy = 0, metal = 0;
};
MissileCost MissileBatch(const HumanWeaponSpec& weapon, int count);

// Plazas libres de una bahia con ammo misiles dentro.
inline int MissileRoom(const HumanWeaponSpec& weapon, int ammo) { return weapon.capacity > ammo ? weapon.capacity - ammo : 0; }

}  // namespace game
