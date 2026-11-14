// Anomalies.h - Lo que hay escondido en la nebulosa.
//
//   Naves y estaciones alienigenas, hostiles o neutrales.
//   Estructuras abandonadas: guardan un emisor de energia negativa.
//
// Se guardan respecto a su sistema, independientes de la carga bajo demanda: existen aunque sus
// planetas no esten en memoria. Las posiciones son relativas para que el origen flotante no
// las afecte.
#pragma once

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "epsilon/Math3D.h"
#include "game/Balance.h"
#include "game/HullZones.h"

namespace game {

enum class AnomalyKind : int { AlienShip, AlienStation, Derelict };
enum class Stance : int { None, Neutral, Hostile };

const char* Name(AnomalyKind kind);
const char* Name(Stance stance);

struct Anomaly {
    int id = 0;
    AnomalyKind kind = AnomalyKind::Derelict;
    Stance stance = Stance::None;
    int system = 0;
    eps::Vec3d offset;     // km, respecto al centro de su sistema
    eps::Vec3d home;       // centro de su patrulla
    eps::Vec3d velocity;   // km/s
    float yaw = 0;
    double radius = 1;     // km
    std::string name;
    bool emitter = false;  // estructura: aun guarda su emisor de energia negativa
    double explored = 0;   // horas dedicadas a explorarla
    bool pursuing = false;
    bool discovered = false;  // ya la ha visto de cerca
    bool revealed = false;    // un barrido la encontro
    int signature = 1;        // nivel de escaner necesario para encontrarla
    bool cold = false;        // no emite nada: los sensores normales no la ven

    // Combate. Una estructura abandonada no tiene casco: no se puede destruir.
    double hull = 0, maxHull = 0;
    bool armed = false;
    WeaponKind weapon = WeaponKind::Turret;
    int weaponTier = 1;
    int computerTier = 1;
    double cooldown = 0;      // segundos simulados hasta el proximo disparo
    double stationCooldown = 0;  // lo mismo, contra estaciones (no se guarda): el otro solo corre con una nave cerca
    ShieldKind shield = ShieldKind::EM;  // su campo: elige con que conviene dispararle
    double shieldCharge = 0, shieldMax = 0;
    // Solo las naves: su clase en la tabla ShipClass (Balance.h). De ella salen casco, escudo y velocidad.
    ShipClassId shipClass = ShipClassId::Frigate;
    ShipVariant variant = ShipVariant::Light;
    // Casco por zonas, como la nave (HullZones.h), con la forma de su modelo (ZonesOf). Una zona
    // periferica a 0 se suelta; el nucleo a 0 la destruye; la espina o el puente la dejan apagada.
    HullZones zones = IntactHull();
    HullZones zoneHeat{};   // lo recien golpeado, que aun brilla: no se guarda
    bool disabled = false;  // apagada: ni se mueve por si misma ni dispara
    // Solo las estaciones: su nivel, de 1 a 6 (kStationLevelNames). El 6 es el planeta coraza.
    int stationLevel = 0;
    // La revancha tras una rendicion: multiplica su velocidad y su dano (casco y escudo ya van escalados).
    double boost = 1.0;
};

// ---- Estaciones por nivel y planeta coraza (fase F) ----
// Vida de una estacion de ese nivel (1 a 6).
double StationHull(const AnomalyRules& rules, int level);
// Nivel de 1 a 5 segun su id, con el reparto de stationLevelShare: no gasta numeros aleatorios.
int StationLevelFor(int id, const AnomalyRules& rules);
inline bool IsArmorPlanet(const Anomaly& a) { return a.kind == AnomalyKind::AlienStation && a.stationLevel == kArmorPlanetLevel; }
// Lo pone de planeta coraza: radio de Jupiter, hostil, inmovil, con su casco y su nombre.
void MakeArmorPlanet(Anomaly& a, const AnomalyRules& rules);
// El planeta coraza de la galaxia, si le toca uno: lo decide la semilla, sin tocar el reparto de lo demas.
// Va en un sistema de la mitad lejana, por fuera de sus planetas.
bool PlaceArmorPlanet(const std::vector<double>& systemExtents, const AnomalyRules& rules, std::uint32_t seed, int id, Anomaly& out);

// ---- Alerta de la especie, rendicion y revancha (fase F) ----
struct SpeciesState {
    std::vector<double> alert;       // de 0 a 1, por sistema
    std::vector<double> lastAttack;  // simTime del ultimo ataque del jugador alli; < 0: nunca
    double surrenderUntil = -1.0;    // simTime hasta el que dura la rendicion; < 0: no hay
    bool revengePending = false;     // al acabar la rendicion, vuelven con revengeFactor
    int revenges = 0;                // veces que han vuelto reforzadas
    std::vector<double> decayedUntil;  // reloj (s) hasta el que ha bajado la alerta de cada sistema; < 0: sin sello. No se guarda
};

int AlertRegion(int system, const AlertRules& rules);
void ResizeSpecies(SpeciesState& s, std::size_t systems);
// Sube la alerta en toda la region del sistema. Con spread > 0 y las posiciones de los sistemas, tambien
// en los spread sistemas mas cercanos a el (el hangar militar).
void RaiseAlert(SpeciesState& s, int system, double amount, double now, const AlertRules& rules, const std::vector<eps::Vec3d>* positions = nullptr, int spread = 0);
// Lo que sube al destruir una estacion de ese nivel, o una nave (nivel 0).
double DestroyRaise(const AlertRules& rules, int stationLevel);
// Baja con el tiempo, mas deprisa donde hace calmHours que no se ataca.
void DecayAlert(SpeciesState& s, double now, double seconds, const AlertRules& rules);
// Lo mismo, por sello de tiempo: cada sistema baja lo que le falte hasta now y lo anota (como
// StationNetwork::ProduceUntil). Un sistema sin sello empieza a contar en from.
void DecayAlertUntil(SpeciesState& s, double now, double from, const AlertRules& rules);
// Una capital suprema o un planeta coraza destruidos: la flota se rinde un rato.
bool TriggersSurrender(const Anomaly& a);
void BeginSurrender(SpeciesState& s, double now, const AlertRules& rules);
inline bool Surrendered(const SpeciesState& s, double now) { return s.surrenderUntil >= 0.0 && now < s.surrenderUntil; }
// Al acabar la rendicion: todas sus naves vuelven con +50 %. Devuelve true en el paso en que vuelven.
bool EndSurrender(SpeciesState& s, std::vector<Anomaly>& anomalies, double now, const AlertRules& rules);
// Lo que se lleva una nave nueva tras las revanchas que ya hubo.
double RevengeBoost(const SpeciesState& s, const AlertRules& rules);
void ApplyBoost(Anomaly& a, double factor);

// Los modelos alienigenas que tienen zonas. Una nave neutral que pasa a hostil cambia de modelo.
// HostileShip y NeutralShip son las maquetas de las clases que aun no tienen modelo propio; las de
// dron, corveta y fragata (fase E) van al final para no cambiar los numeros de las partidas guardadas.
enum class AlienModel : int {
    HostileShip, NeutralShip, HostileStation, NeutralStation,
    HostileDrone, HostileCorvette, HostileFrigate,
    NeutralDrone, NeutralCorvette, NeutralFrigate,
};
inline constexpr int kAlienModelCount = 10;
// Las clases con modelo propio (dron, corveta y fragata), en el orden de ShipClassId.
inline constexpr int kAlienClassModelCount = 3;
inline constexpr bool HasClassModel(ShipClassId id) { return static_cast<int>(id) < kAlienClassModelCount; }
inline constexpr AlienModel ClassModel(ShipClassId id, bool hostile) {
    return static_cast<AlienModel>(static_cast<int>(hostile ? AlienModel::HostileDrone : AlienModel::NeutralDrone) + static_cast<int>(id));
}
// -1: no tiene modelo con zonas (una estructura abandonada).
int AlienModelOf(const Anomaly& a);
// La forma de las zonas de cada maqueta: la fija el juego al cargar (RegisterAlienZones). Sin ella,
// la anomalia solo tiene su casco entero.
void SetAlienZones(AlienModel model, std::shared_ptr<const ZoneLayout> layout);
const ZoneLayout* AlienZones(AlienModel model);
const ZoneLayout* ZonesOf(const Anomaly& a);

// La fila de la tabla que le toca a una nave.
inline const ShipClass& ClassOf(const Anomaly& a) { return ShipClassOf(a.shipClass, a.variant); }

// Por ahora las naves son dron, corveta o fragata ligera, segun su id (sin numeros aleatorios, para no
// mover las semillas). Las demas clases llegan con sus modelos.
void AssignShipClass(Anomaly& a);

// Escudo de una anomalia segun lo que es; las naves lo eligen por su id. Lo llama InitCombat.
void InitShield(Anomaly& a, const AnomalyRules& rules);

// Un impacto contra una anomalia: su escudo para lo que pueda y devuelve lo que llega al casco.
double ShieldAnomaly(Anomaly& a, const DefenseRules& rules, DamageType type, double amount);

// Cada bando dispara contra los demas: 0 es el jugador, 1 los hostiles, 2 los neutrales.
int TeamOf(const Anomaly& a);

// Casco y arma de una anomalia, segun lo que es. Se llama al crearla y al cargar una partida antigua.
void InitCombat(Anomaly& a, const AnomalyRules& rules);

// Una estructura con su emisor siempre en el sistema 0: la curvatura hibrida debe ser alcanzable a
// velocidad subluminica. El resto se reparte con la semilla.
std::vector<Anomaly> GenerateAnomalies(const std::vector<double>& systemExtents, const AnomalyRules& rules, std::uint32_t seed);

// Mueve una anomalia. shipOffset es la posicion de la nave respecto al mismo sistema. Las
// naves hostiles persiguen y se quedan a distancia de disparo; disparar es cosa de la sesion.
// sight multiplica la distancia a la que un hostil ve la nave: el escudo de plasma la delata.
void StepAnomaly(Anomaly& a, const eps::Vec3d& shipOffset, const AnomalyRules& rules, double seconds, std::mt19937& rng, bool aggressive = true, double sight = 1.0);

// Explora una estructura. Devuelve true en el paso en que se saca el emisor.
bool Explore(Anomaly& a, const AnomalyRules& rules, double hours);

}  // namespace game
