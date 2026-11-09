// Asteroids.h - Cinturones y campos de asteroides calculados bajo demanda.
//
// Un cinturon real tiene millones de rocas separadas por cientos de miles de kilometros. Aqui
// se divide en dos niveles, ambos con posicion dada por la ecuacion de Kepler:
//
//   Polvo:    miles de puntos sueltos que dibujan el anillo visto de lejos.
//   Familias: cumulos que siguen una misma orbita, como las familias de Hirayama reales.
//             Sus rocas no existen hasta acercarse: se generan con la semilla de la familia y
//             se olvidan al alejarse. Un campo suelto es una familia sin orbita.
//
// Las tablas de tipos C, S y M estan en docs/astrofisica.md.
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "epsilon/Astro.h"
#include "epsilon/Kepler.h"
#include "epsilon/Math3D.h"
#include "epsilon/RenderTypes.h"

namespace eps {

// Elipse dibujada como lineas. Solo aparece si ocupa suficientes pixeles en pantalla.
struct OrbitPathRecord {
    std::size_t orbit = 0;    // indice en World::orbits
    NodeId parent = kNoNode;  // nodo alrededor del que gira
    Color color;
    bool used = true;
    bool quiet = false;       // solo se dibuja si su dueno o su campo estan seleccionados
    int owner = -1;           // cuerpo al que pertenece; -1 = una estrella
};

enum class AsteroidType : std::uint8_t {
    Carbon,    // C: oscuros, ricos en carbono; abundan mas alla de la linea de hielo
    Silicate,  // S: roca clara; dominan la parte interior
    Metal,     // M: hierro y niquel
};

struct AsteroidRock {
    Vec3f offset;        // km, respecto al centro de la familia
    float radius = 1;    // km
    Vec3f spinAxis;      // unitario
    float spinRate = 0;  // rad/s
    float spinPhase = 0;
    std::uint16_t variant = 0;  // que malla usa
    Color color;
};

struct AsteroidFamily {
    KeplerOrbit orbit;         // sin nodo; la posicion se calcula al consultarla
    float radius = 220;        // km
    int rockCount = 800;
    std::uint32_t seed = 0;
    AsteroidType type = AsteroidType::Silicate;
    bool fixed = false;        // true = campo suelto, centrado en el padre
    float minRadius = 0.12f, maxRadius = 14.0f;  // km
    float exponent = 1.9f;     // N(>r) proporcional a r^-exponent: mayor = mas rocas pequenas
};

struct AsteroidBelt {
    int system = -1;           // -1 = campo suelto
    NodeId parent = kNoNode;   // nodo central
    double inner = 0, outer = 0;  // km
    std::vector<AsteroidFamily> families;
    std::vector<KeplerOrbit> dust;
    Color dustColor;
    bool used = true;
    astro::BeltProfile profile = astro::BeltProfile::Regular;
    bool debris = false;       // nacio de un choque
    int body = -1;             // cuerpo que lo representa para seleccionarlo
    // Para el medio del anillo (BeltField): gravedad del anfitrion, semilla, sentido de giro
    // (+1 o -1 en el plano XZ del padre) y grosor, en radianes vistos desde el centro.
    double mu = 0;
    std::uint32_t seed = 0;
    double spin = 1.0;
    double thickness = 0.07;
};

// Lo que llena el anillo entre una familia y otra. El anillo se divide en celdas que giran con la
// velocidad orbital de su radio, y cada celda saca sus rocas de su semilla: alrededor de cualquier
// punto aparecen siempre las mismas, moviendose en orbitas circulares, sin guardar nada.
// Dos niveles: polvo, puntos sueltos que se ven desde lejos y forman la banda; y rocas, con su malla,
// que solo se generan cerca. Mucho mas apretado que un cinturon real, como las familias.
struct BeltFieldTier {
    double cell = 2500.0;      // km
    double perCell = 1.0;      // rocas de media por celda en el centro del anillo
    float minRadius = 0.1f, maxRadius = 5.0f, exponent = 2.0f;  // km
    std::uint32_t salt = 1;
};
struct BeltFieldRock {
    Vec3d position;            // km, mundo
    Vec3d velocity;            // km/s, respecto al padre del cinturon
    float radius = 1;          // km
    std::uint32_t id = 0;
    std::uint16_t variant = 0;
    Color color;
    Vec3f spinAxis{0, 1, 0};
    float spinRate = 0, spinPhase = 0;
};
BeltFieldTier BeltRockTier(const AsteroidBelt& belt);
BeltFieldTier BeltDustTier(const AsteroidBelt& belt);
// De 0 a 1: cuanto anillo hay a ese radio y esa altura sobre el plano (km, en el marco del padre).
double BeltFieldShape(const AsteroidBelt& belt, double radius, double height);
// Las rocas del medio a menos de reach de point. frame es la matriz del padre; center, su posicion.
void BeltField(const AsteroidBelt& belt, const Mat4d& frame, const Vec3d& center, const Vec3d& point, double reach, double simTime, const BeltFieldTier& tier, std::vector<BeltFieldRock>& out);

struct BeltOptions {
    int families = 1200;
    int dust = 3000;
    float familyRadius = 400.0f;   // km; mas denso que uno real: es un sitio donde jugar
    int rocksPerFamily = 4000;
    float minRadius = 0.12f, maxRadius = 14.0f;  // km
    float exponent = 1.9f;
    double eccentricity = 0.05;    // dispersion de las orbitas: las familias se quedan dentro del anillo
    double inclination = 0.105;    // rad
};

// Lo que cambia entre un tipo de cinturon y otro.
struct BeltProfileInfo {
    const char* name;
    float families, dust, rocks;   // multiplican BeltOptions
    float familyRadius;            // km
    float minRadius, maxRadius, exponent;  // rocas, km
    int notableMin, notableMax;    // asteroides con nombre
    double notableRadiusMin, notableRadiusMax;  // km
};

const BeltProfileInfo& ProfileInfo(astro::BeltProfile profile);

// Opciones de un perfil. base da la escala: cuantas familias tiene un cinturon normal.
BeltOptions OptionsFor(astro::BeltProfile profile, const BeltOptions& base = {});

// Misma familia, mismas rocas. El radio sigue una ley de potencias con pocas rocas grandes.
std::vector<AsteroidRock> GenerateRocks(const AsteroidFamily& family);

AsteroidBelt MakeBelt(double inner, double outer, double mu, double snowLine, std::uint32_t seed, const BeltOptions& options = {});

// Campo fijo alrededor de un nodo, sin orbita. Sirve para escenarios del juego.
AsteroidBelt MakeField(NodeId center, float radiusKm, int rocks, AsteroidType type, std::uint32_t seed);
AsteroidBelt MakeField(NodeId center, float radiusKm, int rocks, AsteroidType type, std::uint32_t seed, float minRadius, float maxRadius, float exponent = 1.9f);

// Busca los huecos mas grandes alrededor de cada anfitrion, uno interior y otro mas alla del
// ultimo planeta, les da un perfil y anade a system sus cinturones y asteroides con nombre.
void PlanBelts(astro::StarSystemData& system, std::uint32_t seed);

// Convierte un cinturon planificado en familias y polvo.
AsteroidBelt MakeBelt(const astro::StarSystemData& system, const astro::BeltData& data, const BeltOptions& base = {});

// Atajo: planifica y construye todos los cinturones de una copia del sistema.
std::vector<AsteroidBelt> GenerateBelts(const astro::StarSystemData& system, std::uint32_t seed, const BeltOptions& options = {});

// Rocas por millon de km3 dentro de una familia, de media. Mide lo apretado que esta un campo.
astro::Materials AsteroidMaterials(AsteroidType type, std::mt19937& rng);
double BeltRockDensity(const AsteroidBelt& belt);
std::size_t BeltRockCount(const AsteroidBelt& belt);
double BeltMass(const AsteroidBelt& belt);  // kg, solo las rocas de las familias

}  // namespace eps
