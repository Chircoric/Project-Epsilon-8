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
#include <random>
#include <string>
#include <vector>

#include "epsilon/Math3D.h"
#include "game/Balance.h"

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
    ShieldKind shield = ShieldKind::EM;  // su campo: elige con que conviene dispararle
    double shieldCharge = 0, shieldMax = 0;
};

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
