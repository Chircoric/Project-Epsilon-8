// ShipModules.h - La nave por modulos: ranuras, modulos, motores de impulso y piezas.
//
//   Ranuras exteriores: los anclajes del modelo (nodos vacios "hp_..." del .glb, como hp_torreta_3).
//     Lo que se monta en ellas se ve en el casco: torretas, radiadores, sensores, blindaje.
//   Ranuras interiores: bahias dentro de cada zona, que no se ven. Ahi van almacenes, tanques de
//     antimateria y refuerzos de la estructura.
//   Motor de impulso: una seccion que se cambia entera. Cada modelo sube la velocidad maxima y la
//     aceleracion del impulso, y gasta mas.
//   Piezas: cada pieza del modelo (una antena, una torre, un tanque) tiene su propia integridad. Un
//     impacto la gasta; a 0 se suelta sola, aunque su zona aguante, y se lleva lo que hubiera montado.
//
// Todo se cambia atracado en una estacion propia (GameSession::InstallModule). Un modulo deja de
// funcionar si su zona se pierde o, en las exteriores, si se suelta la pieza en la que va montado.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "epsilon/Math3D.h"
#include "game/HullZones.h"

namespace game {

enum class SlotKind : int { External, Internal };

enum class ShipModuleKind : int {
    None,
    // Exteriores
    Turret,          // torreta extra que dispara desde su ranura
    Radiator,        // aleta de radiador: enfria mas
    SensorArray,     // antena: mas alcance de escaner
    ArmorPlate,      // placa sobre el casco: su zona aguanta mas
    // Interiores
    Storage,         // almacen: mas bodega
    AntimatterTank,  // mas antimateria
    Reinforcement,   // cuadernas de refuerzo: su zona aguanta mas
    // Exterior, tras los interiores para no cambiar los numeros de las partidas guardadas
    DroneBay,        // bahia con drones mineros (GameSession::DeployDrones)
    // Armamento humano (Armament.h, fase B). Exteriores; el orden es el de ArmamentRules::weapons.
    PointDefensePlatform,  // plataforma de defensa puntual: va en un hueco de torreta clasica
    CoilgunLight, CoilgunHeavy,
    RailSpinalLight, RailSpinalHeavy,
    Rail360Light, Rail360Heavy,
    BayLight10, BayLight25, BayLight50,
    BayHeavy10, BayHeavy25, BayHeavy50,
    NuclearLight, NuclearHeavy,
    Count
};

// El primer arma humana de la lista, y cuantas hay (ArmamentRules::weapons).
inline constexpr ShipModuleKind kFirstHumanWeapon = ShipModuleKind::CoilgunLight;
inline constexpr bool IsHumanWeapon(ShipModuleKind kind) { return kind >= kFirstHumanWeapon && kind < ShipModuleKind::Count; }

struct ShipModuleSpec {
    const char* name;
    const char* description;
    SlotKind slot;
    double metal = 0, silicate = 0;  // lo que cuesta montarlo; al quitarlo se recupera la mitad
};
const ShipModuleSpec& ModuleSpec(ShipModuleKind kind);

// Valores de cada modulo. Decision de juego.
inline constexpr double kStorageCargo = 1000.0;        // kt por almacen
inline constexpr double kTankAntimatter = 40.0;        // kt por tanque
inline constexpr double kRadiatorBoost = 0.25;         // parte de refrigeracion por aleta
inline constexpr double kSensorBoost = 0.3;            // parte de alcance por antena
inline constexpr double kArmorShare = 0.4;             // la zona aguanta 1 + esto por placa
inline constexpr double kReinforcementShare = 0.3;     // y 1 + esto por refuerzo

// Motores de impulso, del 1 (el de serie) al kImpulseEngineCount.
struct ImpulseEngineSpec {
    const char* name;
    const char* description;
    double speed;     // multiplica la velocidad maxima del impulso
    double accel;     // multiplica la aceleracion
    double burn;      // multiplica el gasto de energia del impulso
    double metal, silicate;
    float glow[3];    // color del escape
};
inline constexpr int kImpulseEngineCount = 4;
const ImpulseEngineSpec& ImpulseEngine(int tier);

struct ModuleSlot {
    std::string name;      // "hp_torreta_3" o "bahia de la bodega 2"
    SlotKind kind = SlotKind::Internal;
    int zone = 0;
    eps::Vec3d position;   // m, en el modelo
    eps::Vec3d normal{0, -1, 0};  // hacia fuera del casco
    int piece = -1;        // pieza en la que va montada (exteriores); -1 = ninguna
};

struct DesignPiece {
    std::string name;
    int zone = 0;
    eps::Vec3d center;     // m, en el modelo
    double radius = 1;     // m
    // Puntos de su superficie (vertices, hasta unos cien): un impacto va a la pieza que tenga mas
    // cerca, aunque sea un anillo hueco cuya esfera envuelve a las demas.
    std::vector<eps::Vec3d> samples;
};

// Distancia de un punto (m, en el modelo) a una pieza: a su punto mas cercano, o a su esfera.
double PieceDistance(const DesignPiece& piece, const eps::Vec3d& point);

// Las ranuras y las piezas de la nave que se esta usando. Se fija al cargarla (main), como las
// formas de las zonas. Sin fijar, vale la de la maqueta: sus ranuras, sin piezas.
struct ShipDesign {
    std::vector<ModuleSlot> slots;
    std::vector<DesignPiece> pieces;  // en el orden de las piezas del modelo
    // Compuerta de atraque: de ahi sale el ducto que se acopla a la estacion. Un nodo vacio "atraque"
    // en el .glb la fija, con su +Y local hacia fuera (dockNormal); si no, el punto mas a estribor
    // del puente, mirando a estribor (+X). Atracada, la nave gira hasta que dockNormal mira a la estacion.
    eps::Vec3d dockPort{45, -80, 340};  // m, en el modelo
    eps::Vec3d dockNormal{1, 0, 0};     // unitaria, en el modelo
};
void SetShipDesign(std::shared_ptr<const ShipDesign> design);
const ShipDesign& CurrentShipDesign();

// Bahias interiores de cada zona: las mismas en cualquier nave.
std::vector<ModuleSlot> InteriorBays();
// Las ranuras exteriores de la maqueta, a falta de un modelo con anclajes.
std::vector<ModuleSlot> MaquetteHardpoints();

// Lo montado en la nave. modules va en el orden de CurrentShipDesign().slots.
struct ShipLoadout {
    int engine = 1;
    std::vector<ShipModuleKind> modules;
    // Armamento humano. ammo y cooldown van en el orden de modules: misiles que le quedan a cada bahia y
    // segundos simulados hasta que recarga (este no se guarda).
    std::vector<int> ammo;
    std::vector<double> cooldown;
    std::uint32_t blueprints = 0;  // planos fabricados en la origami, un bit por tipo y tamano (Armament.h)
    int pointDefense = 0;          // puntos de defensa puntual montados (aparte de las torretas)
    int starterBays = 2;           // bahias de drones de serie: se cambian de hueco, no se sacan de la nave
    // Nave recien hecha: FitToDesign le monta las bahias de drones de serie en las ranuras de popa
    // (StarterSlots). Se apaga en cuanto se toca un modulo o se carga una partida.
    bool starter = false;
};

// Las ranuras exteriores donde van las bahias de drones de serie: hp_torreta_5 y hp_torreta_6 si
// existen (el modulo de popa, junto a las bodegas); si no, las dos ultimas exteriores.
std::vector<int> StarterSlots(const ShipDesign& design);

// Nombre legible de una pieza o un anclaje: "puente_cupula" -> "puente cupula".
std::string PrettyName(const std::string& name);

}  // namespace game
