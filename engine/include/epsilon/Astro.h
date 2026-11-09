// Astro.h - Constantes fisicas y generador de sistemas solares.
//
// Solo produce datos: no sabe nada de la escena ni de la GPU. El juego convierte un
// StarSystemData en nodos y Renderables. Unidades: km, s, rad; masas en kg.
// Las tablas, las formulas y sus fuentes estan en docs/astrofisica.md.
#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "epsilon/Kepler.h"
#include "epsilon/RenderTypes.h"

namespace eps::astro {

inline constexpr double kGravity = 6.6743e-20;           // km^3 / (kg s^2)
inline constexpr double kAstronomicalUnit = 1.495978707e8;  // km
inline constexpr double kLightYear = 9.4607304725808e12;    // km
inline constexpr double kSolarRadius = 695700.0;         // km
inline constexpr double kSolarMass = 1.98847e30;         // kg
inline constexpr double kSolarTemperature = 5772.0;      // K
inline constexpr double kEarthRadius = 6371.0;           // km
inline constexpr double kEarthMass = 5.9722e24;          // kg
inline constexpr double kJupiterRadius = 69911.0;        // km
inline constexpr double kHour = 3600.0;
inline constexpr double kDay = 86400.0;
inline constexpr double kYear = 365.25 * kDay;

enum class StarClass : std::uint8_t {
    O, B, A, F, G, K, M,
    RedGiant, RedSupergiant, BlueSupergiant, WhiteDwarf, NeutronStar, BlackHole,
    Count
};

enum class PlanetClass : std::uint8_t {
    Lava, Rock, Desert, Terran, Ice, HotJupiter, GasGiant, IceGiant,
    Moon, Comet,
    Asteroid,  // asteroide con nombre: tiene orbita y ficha propias
    Belt,      // el campo entero; solo existe como cuerpo para poder seleccionarlo
    Count
};

// Caracter de un cinturon: cuanto material tiene y de que tamano.
enum class BeltProfile : std::uint8_t {
    Sparse,   // pocas rocas muy separadas
    Regular,
    Dense,    // muchas rocas juntas
    Rubble,   // grava: casi todo pequeno, restos de choques
    Giants,   // pocas rocas, pero enormes
    Count
};

// Como se reparten las estrellas de un sistema.
enum class StarLayout : std::uint8_t {
    Auto,        // segun la clase de estrella y maxStars
    Single,
    Close,       // pareja cerrada: los planetas giran alrededor de las dos
    Wide,        // pareja abierta: cada estrella tiene sus planetas
    Triple,      // pareja cerrada y una tercera lejana
    Quadruple,   // dos parejas cerradas, lejos una de otra
};

// Rango tipico de una clase. Un mismo valor u coloca masa, radio y temperatura en su rango.
struct StarClassInfo {
    const char* name;
    double massMin, massMax;       // masas solares
    double radiusMin, radiusMax;   // radios solares
    double tempMin, tempMax;       // K
    double rotationMin, rotationMax;  // periodo en dias
    double frequency;              // fraccion real aproximada
    double showcase;               // peso para una galaxia variada
    int planetsMin, planetsMax;
};

const StarClassInfo& Info(StarClass c);
const char* Name(PlanetClass c);

// De que esta hecho un cuerpo. Depende de su distancia a la estrella, asi que la composicion
// tambien informa de donde se formo. De ella salen su densidad, su color y su forma.
struct Materials {
    // Masa total, en fracciones que suman 1.
    double iron = 0, silicate = 0, water = 0, ices = 0, gas = 0;  // ices = metano y amoniaco; gas = H y He
    // Superficie o nubes, de 0 a 1.
    double rust = 0;      // oxidos de hierro: rojo
    double sulfur = 0;    // azufre: amarillo
    double carbon = 0;    // carbono y tolinas: oscuro
    double methane = 0;   // absorbe el rojo: azul
    // Clima.
    double ocean = 0;     // superficie cubierta por agua liquida
    double iceCap = 0;    // superficie cubierta por hielo: 0 = nada, 1 = todo
    double clouds = 0;    // cobertura de nubes
    double pressure = 0;  // atmosfera en la superficie, en bares
    double surfaceTemperature = 0;  // K, con efecto invernadero
    bool life = false;    // la tierra firme es verde
};

// Resultado visual de los materiales. El juego lo convierte en un SurfaceLook.
struct Appearance {
    SurfaceKind kind = SurfaceKind::Rock;
    Vec3f a, b, c;
    Vec3f atmosphere;
    float atmosphereStrength = 0;
    float oceanLevel = 0, iceCap = 0, cloudCover = 0, relief = 1, flattening = 0;
};

struct StarData {
    StarClass kind = StarClass::G;
    double mass = kSolarMass;        // kg
    double radius = kSolarRadius;    // km
    double temperature = kSolarTemperature;  // K
    double luminosity = 1;           // luminosidades solares
    double spinRate = 0;             // rad/s
    double habitableInner = 0, habitableOuter = 0, snowLine = 0;  // km
    double hydrogen = 0.73, helium = 0.25, metals = 0.02;  // fracciones de masa
    double blackHoleSpin = 0;        // a/M: 0 = Schwarzschild, mayor = Kerr
};

// Cuerpo con orbita: planeta, luna o cometa.
struct BodyData {
    PlanetClass kind = PlanetClass::Rock;
    double mass = 0;       // kg
    double radius = 0;     // km
    OrbitalElements orbit;
    SpinModel spin;
    double temperature = 0;  // K, equilibrio con su estrella
    Materials materials;
    bool tidallyLocked = false;
    int parent = -1;       // indice del planeta en bodies; -1 = orbita su anfitrion
    int host = 0;          // indice en StarSystemData::nodes de lo que orbita
    int belt = -1;         // asteroide: indice en StarSystemData::belts
    double impactTime = -1;  // >= 0: su orbita acaba en un choque en este instante
    int impactTarget = -1;   // indice en bodies del cuerpo al que golpea
    bool destroyed = false;  // ya no existe; el indice se conserva para no mover los demas
};

// Cinturon descrito con datos. Sus rocas se calculan al cargar el sistema.
struct BeltData {
    double inner = 0, outer = 0;  // km
    int host = 0;
    BeltProfile profile = BeltProfile::Regular;
    std::uint32_t seed = 0;
    bool debris = false;  // nacio de un choque
};

// Arbol de estrellas. Una hoja es una estrella; un nodo con dos hijos es su baricentro. Asi
// una estrella doble, triple o cuadruple es la misma estructura, y los planetas pueden
// orbitar tanto una estrella como una pareja.
struct StellarNode {
    int parent = -1;        // -1 = raiz: el centro de masas del sistema, que no se mueve
    int first = -1, second = -1;  // hijos; -1 = hoja
    int star = -1;          // hoja: indice para StarAt
    OrbitalElements orbit;  // alrededor del baricentro de parent; las dos partes de una pareja
                            // comparten periodo, y por eso su mu no es el de ninguna masa real
    double separation = 0, eccentricity = 0;  // pareja: orbita relativa de sus dos hijos
    StarData effective;     // lo que nota un planeta: masa y luz sumadas
    double planetInner = 0, planetOuter = 0;  // zona estable; planetOuter = 0 = sin planetas
    bool bounded = false;   // la zona termina en planetOuter por culpa de otra estrella
};

struct StarSystemData {
    StarData star;                  // la principal
    std::vector<StarData> companions;
    std::vector<StellarNode> nodes;  // vacio = una sola estrella
    std::vector<BodyData> bodies;    // cada luna va despues de su planeta
    std::vector<BeltData> belts;

    int StarCount() const { return 1 + static_cast<int>(companions.size()); }
    const StarData& StarAt(int index) const { return index <= 0 ? star : companions[static_cast<std::size_t>(index) - 1]; }
    // Lo que orbita un cuerpo: una estrella o, en una pareja, las dos juntas.
    const StarData& HostOf(const BodyData& body) const { return nodes.empty() ? star : nodes[static_cast<std::size_t>(body.host)].effective; }
};

struct GeneratorOptions {
    int planetsMin = -1, planetsMax = -1;  // -1 = el rango de la clase de estrella
    int moonsMax = 6;
    int cometsMax = 2;
    bool realFrequencies = false;  // true = 76 % enanas rojas y casi ningun gigante
    double blackHoleSpin = -1;     // -1 = al azar; 0 = Schwarzschild; mayor = Kerr
    double blackHoleMass = 0;      // masas solares; 0 = al azar, dentro de la clase
    int maxStars = 1;              // en StarLayout::Auto, tope de estrellas del sistema
    StarLayout layout = StarLayout::Auto;
};

// mt19937 es igual en cualquier compilador; las distribuciones de <random> no lo son.
double Uniform(std::mt19937& rng, double a, double b);
double LogUniform(std::mt19937& rng, double a, double b);
double Normal(std::mt19937& rng);
double Rayleigh(std::mt19937& rng, double sigma);
double Beta(std::mt19937& rng, double alpha, double beta);

SpinModel RandomSpin(const OrbitalElements& orbit, double periodMinHours, double periodMaxHours, std::mt19937& rng);
StarClass RandomStarClass(std::mt19937& rng, bool realFrequencies);
StarData MakeStar(StarClass kind, std::mt19937& rng, double solarMasses = 0);  // 0 = masa al azar
StarSystemData GenerateSystem(std::mt19937& rng, const GeneratorOptions& options = {});
StarSystemData GenerateSystem(StarClass kind, std::mt19937& rng, const GeneratorOptions& options = {});

// Formulas sueltas: sirven para el juego y para las pruebas.
double Luminosity(double radiusSolar, double temperature);          // luminosidades solares
double EquilibriumTemperature(double luminosity, double distanceKm, double albedo = 0.3);  // K
double HillRadius(double semiMajorAxis, double eccentricity, double bodyMass, double centralMass);  // km
double RocheLimit(double primaryRadius, double primaryDensity, double satelliteDensity);  // km, rigido
double RadiusFromMass(double earthMasses);                          // radios terrestres
// Orbitas estables en una estrella doble (Holman y Wiegert, 1999). mu = m2 / (m1 + m2) e es la
// excentricidad de la pareja. Devuelven la fraccion de su separacion.
double CircumstellarLimit(double mu, double eccentricity);          // alrededor de una estrella: por dentro de esto
double CircumbinaryLimit(double mu, double eccentricity);           // alrededor de las dos: por fuera de esto
// Menor semieje exterior que no rompe una pareja interior (Mardling y Aarseth, 2001).
double HierarchyLimit(double innerSeparation, double innerMass, double outerMass, double outerEccentricity);
// Excentricidad mediana de un sistema con n planetas (Limbach y Turner, 2015).
double MedianEccentricity(int planets);
Vec3f BlackbodyColor(double temperature);                           // RGB de 0 a 1

double Density(const Materials& materials);                         // kg/km^3, sin comprimir
double SurfaceTemperature(double equilibrium, double pressureBar);  // K, con efecto invernadero
double Flattening(double mass, double radius, double spinRate, bool gaseous);  // (ecuador - polo) / ecuador
Appearance DeriveAppearance(const BodyData& body);

// Composicion por elementos, en fracciones de masa que suman 1. Sale de los materiales: el hierro
// del nucleo, las rocas de silicato, el agua, los hielos y el gas tienen cada uno su receta. De ella
// salen tambien los colores de la superficie.
enum class Element : std::uint8_t { H, He, C, N, O, Mg, Al, Si, S, Ca, Fe, Ni, Count };
inline constexpr std::size_t kElementCount = static_cast<std::size_t>(Element::Count);
const char* ElementSymbol(Element e);
const char* ElementName(Element e);

struct Composition {
    std::array<double, kElementCount> share{};
    double Of(Element e) const { return share[static_cast<std::size_t>(e)]; }
};

Composition ElementsOf(const Materials& m);
Composition ElementsOf(const StarData& star);  // hidrogeno, helio y los metales en proporcion solar

// La tabla periodica entera. Los doce de arriba salen de las recetas; el resto, de las abundancias
// de las condritas CI y del Sol, repartidas segun a que se une cada elemento (Goldschmidt): los
// siderofilos van con el hierro, los calcofilos con el azufre, los litofilos con la roca y los
// volatiles con el agua y los hielos. Cada astro varia un poco segun su semilla, y a veces tiene una
// veta. Los de la serie del uranio salen del equilibrio secular; del 95 en adelante no hay en la
// naturaleza.
inline constexpr int kPeriodicCount = 118;
struct PeriodicInfo {
    const char* symbol;
    const char* name;
    int row;     // 1 a 7; 9 y 10 son los lantanidos y los actinidos, debajo
    int column;  // 1 a 18
};
PeriodicInfo Periodic(int z);  // z de 1 a 118
using PeriodicShares = std::array<double, kPeriodicCount>;  // fraccion de masa; el indice es z - 1
PeriodicShares AllElementsOf(const Composition& main, std::uint32_t seed);

// Resultado de un choque, segun su energia por unidad de masa.
enum class ImpactOutcome : std::uint8_t {
    Crater,     // el proyectil es pequeno: deja una marca
    Merger,     // dos cuerpos grandes se funden en uno
    Partial,    // sobrevive un resto y lo demas sale despedido
    Shattered,  // no queda nada mayor que un fragmento
    Absorbed,   // contra una estrella o un agujero negro
};

struct ImpactResult {
    ImpactOutcome outcome = ImpactOutcome::Crater;
    double speed = 0;           // km/s
    double energy = 0;          // J
    double specificEnergy = 0;  // Q, J/kg
    double threshold = 0;       // Q* que dispersa la mitad de la masa, J/kg
    double remnantMass = 0;     // kg, mayor resto
};

// Energia especifica que dispersa la mitad de un cuerpo (Benz y Asphaug, 1999, basalto).
double DisruptionThreshold(double radius, double density);  // km, kg/km^3 > J/kg
const char* Name(ImpactOutcome outcome);
ImpactResult ResolveImpact(double massA, double radiusA, double massB, double radiusB, double speed);

// Comprueba lo que el generador promete. Devuelve "" si todo esta bien, o el primer fallo.
std::string CheckStability(const StarSystemData& system);

// "SYS-3 b" para un planeta, "SYS-3 b II" para su segunda luna, "SYS-3 cometa 1" para un cometa.
// Con varias estrellas se dice lo que orbita: "SYS-3 AB b" gira a la pareja, "SYS-3 C b" a la tercera.
std::string BodyName(const StarSystemData& system, std::size_t body, const std::string& systemName);

// Nombre de un hijo del arbol: "A", "B", "AB"...
std::string StellarName(const StarSystemData& system, int node);
// "Binaria cerrada", "Triple"...
const char* LayoutName(const StarSystemData& system);
// Distancia maxima de un nodo del arbol al centro del sistema.
double StellarReach(const StarSystemData& system, int node);
double StellarReach(const StarSystemData& system);  // la mayor de todas
// Masa, posicion y demas de lo que orbita un cuerpo; sirve aunque nodes este vacio.
double HostMass(const StarSystemData& system, const BodyData& body);
// Crea un impactor: un cuerpo cuya orbita llega a target justo en el instante time.
bool AddImpactor(StarSystemData& system, int target, double time, double radius, std::mt19937& rng);

// Informe con los datos y las ecuaciones de cada cuerpo.
std::string DescribeSystem(const StarSystemData& system, const std::string& name);

}  // namespace eps::astro
