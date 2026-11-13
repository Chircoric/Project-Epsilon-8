// Combat.h - Armas, proyectiles y la balistica que los dirige.
//
// Nada de esto conoce la sesion: son funciones sobre vectores, asi que se prueban solas.
//
//   Torreta:        proyectil cinetico, rapido de recargar y de corto alcance. Tambien derriba guiados.
//   Canon espinal:  haz de luz fijo al eje del casco. Viaja a c y la gravedad lo desvia.
//   Misil:          cabeza de antimateria guiada, lenta y de mucho alcance.
//   Drones:         enjambre guiado que quema el casco.
//
// Los proyectiles siguen la gravedad de los astros del sistema. El computador de punteria resuelve
// la interseccion con un metodo de tiro: dispara hacia donde estara el blanco, simula el vuelo con la
// gravedad que conoce y corrige la desviacion. Su nivel decide cuantos astros cuenta, cuantas
// correcciones hace y cuanto se equivoca al final. Lo que no cuenta, desvia el tiro.
//
// Un proyectil que falla sigue su camino: cae a un planeta, entra en orbita o vuelve a cruzar el
// sistema. Eso tambien es un peligro del espacio.
#pragma once

#include <random>
#include <vector>

#include "epsilon/Math3D.h"
#include "game/Balance.h"

namespace game {

const char* Name(DamageType type);
const char* Name(WeaponKind kind);

struct GravitySource {
    eps::Vec3d position;  // km
    double mu = 0;        // km3/s2
    double radius = 0;    // km: dentro, el proyectil se estrella
};

// Los datos de un arma con su nivel: mas dano y recarga mas corta.
WeaponRules Scaled(const CombatRules& rules, WeaponKind kind, int tier);

// Aceleracion en un punto. La luz se desvia el doble que la materia, como en la relatividad.
eps::Vec3d GravityAt(const std::vector<GravitySource>& sources, const eps::Vec3d& at, double scale);

// Los n astros que mas tiran en un punto, para un computador que no puede contarlos todos.
std::vector<GravitySource> Strongest(const std::vector<GravitySource>& sources, const eps::Vec3d& at, std::size_t count);

struct Projectile {
    WeaponKind kind = WeaponKind::Turret;
    DamageType type = DamageType::Kinetic;
    int team = 0;           // 0 jugador, 1 alienigenas hostiles, 2 neutrales
    int owner = 0;          // id de la anomalia que lo lanzo; 0 = el jugador
    int target = -1;        // guiados: id de la anomalia; 0 = una nave de jugador (la de ship); -1 = ninguno
    int ship = 0;           // id de nave (Vessel::id): la que lo disparo si es de un jugador; si no, a la que apunta
    eps::Vec3d position;    // km
    eps::Vec3d velocity;    // km/s
    eps::Vec3d origin;      // desde donde salio: la estela y el haz parten de aqui
    double age = 0;         // segundos simulados en vuelo
    double damage = 0;
    double proximity = 0;   // km
    double life = 0;        // segundos simulados que le quedan
    double cruise = 0;      // km/s de crucero de los guiados
    double turn = 0;        // rad/s
    bool guided = false;
    bool beam = false;      // viaja a c y se desvia como la luz
    bool dead = false;
};

// Un destello donde acaba un proyectil. Solo se dibuja; dura segundos reales.
struct ImpactFlash {
    eps::Vec3d position;
    eps::Vec3d origin;      // un haz se sigue viendo un instante, de punta a punta
    DamageType type = DamageType::Kinetic;
    bool beam = false;
    bool hit = false;       // dio en algo; si no, se perdio
    double age = 0;
};

struct FiringSolution {
    bool valid = false;
    eps::Vec3d direction;   // hacia donde apuntar
    double flightTime = 0;  // segundos simulados hasta el impacto
    eps::Vec3d impact;      // donde estara el blanco entonces
    double miss = 0;        // km que falla la simulacion del propio computador al final
};

// shooterVelocity y targetVelocity van en el mundo. Con nivel de computador tier, cuenta menos astros y
// corrige menos veces. speed es la del proyectil respecto al casco; beam lo manda a c.
FiringSolution Solve(const eps::Vec3d& origin, const eps::Vec3d& shooterVelocity, const eps::Vec3d& targetPosition,
                     const eps::Vec3d& targetVelocity, double speed, bool beam, const std::vector<GravitySource>& sources,
                     int tier, const CombatRules& rules);

// Lo que se equivoca el computador de un nivel, en radianes.
double AimError(const CombatRules& rules, int tier);

// Desvia un rumbo con el error de ese nivel, en una direccion al azar.
eps::Vec3d Scatter(const eps::Vec3d& direction, double error, std::mt19937& rng);

// Avanza un proyectil dt segundos. target, si lo hay, es donde esta lo que persigue un guiado.
void StepProjectile(Projectile& p, const std::vector<GravitySource>& sources, const CombatRules& rules, double dt,
                    const eps::Vec3d* targetPosition = nullptr, const eps::Vec3d* targetVelocity = nullptr);

// El segmento a-b pasa a menos de radius del centro. Devuelve en t (0 a 1) donde se acerca mas.
bool SegmentNear(const eps::Vec3d& a, const eps::Vec3d& b, const eps::Vec3d& center, double radius, double* t = nullptr);

}  // namespace game
