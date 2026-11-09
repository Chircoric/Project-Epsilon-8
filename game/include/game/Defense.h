// Defense.h - Escudos, calor y refrigeracion de la nave.
//
// Cinco defensas con ventajas cruzadas (ver DefenseRules en Balance.h):
//
//   Arena magnetica   para haces y calor; los proyectiles rapidos la atraviesan. Gasta silicato.
//   Plasma            vaporiza proyectiles y asteroides pequenos; la nave brilla y la ven de lejos.
//   Electromagnetico  para la antimateria y ciega los sensores enemigos; nulo contra lo cinetico.
//   Conos ablativos   placas en la proa: sin energia, aguantan la presion de ariete en curvatura.
//                     Pesan y el gas de la nebulosa las desgasta.
//   Campo de Amor     de los Unicornios: lo para todo, devuelve el dano y lo convierte en casco.
//
// Un impacto pasa primero por el campo encendido y lo que queda, si viene por delante, por los
// conos. Lo que pasa de los dos llega al casco.
//
// El calor lo suben las armas, los escudos y la curvatura. Lo bajan los radiadores, sin gastar
// nada pero despacio y mal junto a una estrella, y tres refrigerantes que se gastan: agua, hielo de
// la bodega (al fundirse rellena el agua) y nitrogeno liquido (se evapora; se licua del gas).
#pragma once

#include "game/Balance.h"
#include "game/Ship.h"

namespace game {

const char* Name(ShieldKind kind);
const char* Name(CoolantKind kind);

// Los campos se encienden de uno en uno. Los conos no son un campo.
bool IsField(ShieldKind kind);

double ShieldCapacity(const DefenseRules& rules, ShieldKind kind, int tier);
double ShieldRegen(const DefenseRules& rules, ShieldKind kind, int tier);  // por segundo
double CoolantFlow(const ThermalRules& rules, CoolantKind kind, int tier);  // calor por segundo

// Defensas de salida: los cuatro primeros con nivel 1 y llenos, depositos llenos, nave fria.
DefenseState MakeDefense(const DefenseRules& rules, const ThermalRules& thermal);

// Lo que pasa con un impacto.
struct HitResult {
    double hull = 0;       // lo que llega al casco
    double field = 0;      // lo que para el campo
    double plates = 0;     // lo que paran los conos
    double reflected = 0;  // lo que el Amor devuelve al atacante
    double silicate = 0;   // silicato que gasta la arena
};

// Reparte un impacto. frontal: llega por delante, donde estan los conos. fieldActive: el campo
// puede parar algo (encendido, con energia y sin sobrecalentar). silicate: lo que hay en la bodega.
HitResult AbsorbHit(DefenseState& d, const DefenseRules& rules, DamageType type, double amount, bool frontal, bool fieldActive, double silicate);

// Un paso de refrigeracion. heatIn ya suma todo lo que calienta en este paso. radiators es la
// eficacia de los radiadores, de 0 (junto a una estrella) a 1. ice, el hielo de la bodega.
struct CoolingResult {
    double iceUsed = 0;
    double removed[4] = {0, 0, 0, 0};  // calor que quita cada sistema
};
CoolingResult StepCooling(DefenseState& d, const ThermalRules& rules, double seconds, double heatIn, double radiators, double ice);

bool Overheated(const DefenseState& d, const ThermalRules& rules);

}  // namespace game
