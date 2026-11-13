// Ship.h - La nave: motores, manejo, energia y casco.
//
//   Impulso:    hidrogeno ionizado, para maniobrar. Funciona incluso sin energia, muy despacio.
//   Crucero:    curvatura positiva suave, para viajar dentro de un sistema. Se acerca a c, sin alcanzarla.
//   Hibrida:    hasta 10 c. Formar la burbuja cuesta 10^24 J de antimateria.
//   Hiper:      la metrica mas fina de las ruinas. Hasta 250 c, casi gratis.
//
// Hibrida e Hiper salen de la estructura abandonada del primer sistema: hasta explorarla, ni existen.
//
// Formar la burbuja es lo caro; mantenerla cuesta por segundo de 10 000 a 1 000 000 de veces menos,
// cuanto mas rapido va y mas tiempo lleva estable. La antimateria viene de las centrales, y el
// reactor saca de ella la energia de lo demas.
//
// Es una nave enorme y se maneja como tal: se apunta un rumbo y el casco lo sigue con retraso,
// le cuesta empezar y parar de girar, y derrapa antes de avanzar hacia donde mira. El nivel de
// maniobra lo mejora. La curvatura ademas tarda en formarse.
#pragma once

#include <array>
#include <vector>

#include "epsilon/Math3D.h"
#include "game/Balance.h"
#include "game/Economy.h"
#include "game/HullZones.h"
#include "game/ShipModules.h"

namespace game {

inline constexpr double kLightSpeed = 299792.458;  // km/s

enum class DriveMode : int { Impulse, Cruise, Warp, Hyper };

const char* Name(DriveMode mode);

// Un arma montada en el casco. La recarga cuenta en segundos simulados.
struct WeaponMount {
    WeaponKind kind = WeaponKind::Turret;
    int tier = 1;
    double cooldown = 0;
    int slot = -1;  // ranura del modulo que la monta (ShipModules.h); -1 = de serie
};

// Escudos, calor y refrigeracion. Las reglas de cada uno estan en Balance.h (DefenseRules, ThermalRules).
struct DefenseState {
    ShieldKind field = ShieldKind::Dust;  // el campo elegido; los conos no son un campo
    bool fieldOn = true;
    std::array<int, 5> shieldTier{1, 1, 1, 1, 0};  // 0 = no se tiene; el Amor llega con los Unicornios
    std::array<double, 5> charge{};                // dano que aun aguanta cada uno; en los conos, las placas
    double heat = 0;                               // 0 a ThermalRules::maximum; 100 es el limite
    std::array<int, 4> coolantTier{1, 1, 1, 1};    // agua, hielo, nitrogeno, radiadores; 0 = no se tiene
    std::array<bool, 4> coolantOn{true, true, true, true};
    double water = 0, nitrogen = 0;                // lo que hay en los depositos
};

struct ShipState {
    eps::Vec3d position;       // km
    // En el espacio no hay arriba ni abajo: la orientacion es un cuaternion, sin limites.
    eps::Quatd orientation;    // hacia donde mira
    eps::Quatd aim;            // hacia donde se le ha mandado mirar
    eps::Vec3d angularRate;    // rad/s reales, en el mundo
    eps::Vec3d angularPush;    // rad/s2 reales que dieron los propulsores de maniobra en el ultimo paso, en el mundo
    eps::Vec3d velocity;       // km/s respecto al cuerpo que domina aqui: inercia pura
    double warpSpeed = 0;      // km/s de la burbuja, que se suma a lo anterior
    eps::Vec3d warpHeading{0, 0, 1};  // hacia donde va la burbuja: sigue al rumbo con retraso
    bool holding = true;       // los propulsores la mantienen quieta contra la gravedad

    // Calculados en cada paso, para quien solo quiere saber como se mueve.
    eps::Vec3d moveDirection{0, 0, 1};
    double speed = 0;          // km/s

    double throttle = 0;       // -1 a 1: cuanto empuja el motor, no a que velocidad va; negativo, hacia atras
    DriveMode mode = DriveMode::Impulse;
    double warpCharge = 0;     // 0 a 1: burbuja de curvatura
    double warpAge = 0;        // segundos de fuera con la burbuja estable

    double energy = 0, hull = 0;
    HullZones zones = IntactHull();  // integridad de cada zona del casco (HullZones.h)
    double antimatter = 0;     // kt
    int heatShieldTier = 0;
    int handlingTier = 0;      // mejora giro, derrape y aceleracion
    int scannerTier = 1;
    int relayTier = 0;         // con enlace, las estaciones se vacian a distancia
    int computerTier = 1;      // computador de punteria: astros que cuenta y error que comete
    std::vector<WeaponMount> weapons;
    Inventory cargo;
    ElementStock elements;       // elementos puros por numero atomico, en kg (Economy.h)
    int emitters = 0;            // emisores de energia negativa recuperados de las estructuras
    double shipHours = 0;        // tiempo a bordo: va mas lento que el de fuera
    DefenseState defense;
    double massFactor = 1.0;     // la maniobra se divide por esto: las placas de los conos pesan
    ShipLoadout loadout;         // modulos montados y motor de impulso (ShipModules.h)
    bool stationAboard = true;   // la estacion origami va plegada alrededor de las bodegas (Stations.h)
    std::vector<double> pieces;  // integridad de cada pieza del modelo, de 0 a 1; vacio = sin piezas
};

ShipState MakeShip(const Balance& rules);

// Ajusta loadout y pieces a las ranuras y piezas de CurrentShipDesign(): lo que sobre se quita,
// lo que falte entra vacio o entero.
void FitToDesign(ShipState& ship);
bool PieceLost(const ShipState& ship, int piece);
// El modulo de esa ranura funciona: su zona sigue ahi y, si va montado en una pieza, la pieza tambien.
bool SlotWorking(const ShipState& ship, int slot);
// Cuantos modulos de ese tipo funcionan; con zone >= 0, solo los de esa zona.
int WorkingModules(const ShipState& ship, ShipModuleKind kind, int zone = -1);
double CargoCapacity(const ShipState& ship, const ShipRules& rules);       // kt
double AntimatterCapacity(const ShipState& ship, const ShipRules& rules);  // kt
// Lo que aguanta de mas una zona por placas y refuerzos: divide el dano que recibe.
double ZoneHardness(const ShipState& ship, int zone);

// Una de cada familia, con el nivel inicial.
std::vector<WeaponMount> DefaultWeapons(const ShipRules& rules);

eps::Vec3d Forward(const eps::Quatd& rotation);
eps::Vec3d Heading(const ShipState& ship);     // adonde mira
eps::Vec3d AimHeading(const ShipState& ship);  // adonde se le ha mandado mirar

// Gira lo justo para que rotation mire hacia direction, sin tocar lo demas.
eps::Quatd TurnedToward(const eps::Quatd& rotation, const eps::Vec3d& direction);
void AimAt(ShipState& ship, const eps::Vec3d& direction);
void FaceTo(ShipState& ship, const eps::Vec3d& direction);  // sin maniobra: de golpe

// Impide apuntar mas alla de maxAngle respecto al casco.
void LimitAim(ShipState& ship, double maxAngle);

// La curvatura hibrida pide energia negativa: un emisor, el de la estructura del sistema de inicio.
bool HybridUnlocked(const ShipState& ship);
// El hiper pide rules.hyperEmitters emisores; con cada uno mas va mas rapido.
bool HyperUnlocked(const ShipState& ship, const DriveRules& rules);
// Si la nave tiene la tecnologia de ese modo.
bool DriveAvailable(const ShipState& ship, DriveMode mode, const DriveRules& rules);
double HyperFactor(const ShipState& ship, const DriveRules& rules);  // veces c

// Los motores de iones propulsan con gas: sin el, la velocidad maxima del impulso baja (DriveRules::ionNoGas).
double IonGasFactor(const ShipState& ship, const DriveRules& rules);

// Velocidad a fondo en un modo, en km/s.
double TopSpeed(const ShipState& ship, DriveMode mode, const DriveRules& rules);

// Velocidad pedida por el mando. La curvatura sube rapido al principio y se frena al
// acercarse a c, como si cada km/s costara mas que el anterior.
double RequestedSpeed(const ShipState& ship, const DriveRules& rules);

// Los modos que curvan el espacio de verdad.
bool Curves(DriveMode mode);

// Lo que cuesta formar la burbuja de un modo, en EJ y en kt de antimateria.
double FormationEnergy(const ShipState& ship, DriveMode mode, const DriveRules& rules);
double FormationCost(const ShipState& ship, DriveMode mode, const DriveRules& rules);

// Queda antimateria para terminar de formarla, o para seguir manteniendola.
bool CanCurve(const ShipState& ship, DriveMode mode, const DriveRules& rules);

// Cuantas veces menos que la formacion gasta por segundo mantener la burbuja actual, y a que
// potencia en EJ/s.
double MaintenanceRatio(const ShipState& ship, const DriveRules& rules);
double MaintenancePower(const ShipState& ship, const DriveRules& rules);

double LorentzFactor(double speed);  // 1 si no aplica

// Cuanto mas rapido pasa el tiempo fuera que a bordo. gravity es rs / r, sumado sobre lo cercano.
// Por encima de c va al limite: asi un viaje entre estrellas lleva dias fuera y minutos a bordo.
double ExteriorRate(double speed, double gravity, double limit);

// Mantiene perpendicular a forward un eje que ya lo era, girandolo lo minimo. Asi el eje acompana
// a un giro continuo sin saltos, en cualquier direccion.
eps::Vec3d TransportUp(const eps::Vec3d& up, const eps::Vec3d& forward);

// Lleva el rumbo hacia el apuntado, sin pasarse y con inercia. Va en segundos reales: una
// maniobra debe durar lo mismo aunque se acelere el tiempo simulado.
void StepHeading(ShipState& ship, const ShipRules& rules, double realSeconds);

// Angulo entre el rumbo y lo apuntado, en radianes.
double AimError(const ShipState& ship);

// Lo que recibe el motor en cada paso.
struct DriveInput {
    eps::Vec3d gravity;            // km/s2
    double dragScale = 1.0;        // cuanta nebulosa frena aqui
    const eps::Vec3d* wanted = nullptr;  // piloto: la velocidad que debe llevar
    double warpLimit = 0.0;        // piloto: tope de la burbuja; 0 = sin tope
};

// Fisica de un paso. seconds es tiempo a bordo; exterior, el del resto del universo, que es el que
// cuenta para recorrer distancia. Sin empuje sigue por inercia, la gravedad la curva y la nebulosa
// la frena poco a poco. La burbuja de curvatura no nota la gravedad. Devuelve los km recorridos.
double StepDrive(ShipState& ship, const Balance& rules, double seconds, double exterior, double realSeconds, const DriveInput& input = {});

// El reactor: rellena la energia con antimateria y, con el motor apagado, recupera un poco de ambas.
void StepReactor(ShipState& ship, const Balance& rules, double seconds);

}  // namespace game
