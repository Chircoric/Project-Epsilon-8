// Universe.h - Convierte sistemas generados en una escena y los carga solo cuando hacen falta.
//
// Las estrellas siempre existen. Los planetas de un sistema se crean al acercarse y se
// destruyen al alejarse, sin perder nada: su semilla los regenera iguales y las ecuaciones
// de Kepler dan su posicion en cualquier instante, sin haberlos simulado mientras no estaban.
//
//   Nivel 1, sistema:  dentro de loadExtents veces su tamano se crean sus cuerpos.
//   Nivel 2, luna:     si su orbita no llega a separarse un pixel de su planeta, no se calcula.
//
// Unidad: 1 = 1 km. Lo que se comparte con el hilo de render esta en WorldCatalog, que es
// inmutable y se vuelve a publicar cuando cambia.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "epsilon/Astro.h"
#include "epsilon/Asteroids.h"
#include "epsilon/Camera.h"
#include "epsilon/Features.h"
#include "epsilon/FrameBuilder.h"
#include "epsilon/Kepler.h"
#include "epsilon/SceneGraph.h"

namespace eps {

// Cuerpo solido: sirve para la velocidad de vuelo, evitar atravesarlo, enfocarlo y nombrarlo.
struct Body {
    NodeId node = kNoNode;  // kNoNode = hueco libre
    NodeId star = kNoNode;
    double radius = 0;
    int system = 0;
    bool isStar = false;
    astro::PlanetClass kind = astro::PlanetClass::Rock;
    std::string name;
    int data = -1;        // indice en StarSystemData::bodies; -1 = la estrella
    int orbit = -1;       // indice en World::orbits; -1 = la estrella
    int parent = -1;      // indice en World::bodies del cuerpo alrededor del que gira
    int renderable = -1;  // indice en World::renderables; -1 = un campo, que no tiene dibujo propio
    int member = 0;       // estrella: indice para StarSystemData::StarAt
    int belt = -1;        // campo: indice en World::belts
    int extra = -1;       // roca seleccionada: indice en World::rockData
};

struct GalaxyOptions {
    int systems = 60;
    double starSpacing = 1.0;      // multiplica la distancia entre estrellas, de 3 a 8 anos luz
    int planetsMin = -1, planetsMax = -1;  // -1 = segun la clase de estrella
    int moonsMax = 6;
    int cometsMax = 2;
    std::uint32_t seed = 20261105;
    bool realFrequencies = false;  // por defecto, mas variedad de la que hay en el cielo real
    bool fade = true;              // apaga los planetas vistos desde otras estrellas

    bool streaming = false;        // false = crea todos los cuerpos al empezar
    double loadExtents = 14.0;     // carga a esta distancia, en tamanos del sistema
    double unloadExtents = 18.0;   // descarga mas lejos, para no repetir en el borde
    float moonOnPixels = 1.0f;     // una luna se activa si su orbita ocupa estos pixeles
    float moonOffPixels = 0.6f;    // y se desactiva por debajo de estos
    int maxLoadsPerStep = 2;

    bool belts = true;             // cinturones de asteroides
    int maxStars = 4;              // 1 = todas las estrellas van solas
    bool showcaseStars = true;     // sistemas 3 a 6: cerrada, abierta, triple y cuadruple
    bool impacts = false;          // el sistema 7 tiene un choque programado
    double impactDay = 25;         // dia en que ocurre
    bool blackHoles = false;       // uno de cada tipo garantizado y uno supermasivo en el centro
    int nebulae = 0;               // nubes; la primera rodea el centro de la galaxia
};

// Lo que un sistema ocupa mientras esta cargado.
struct SystemState {
    bool loaded = false;
    int starBody = -1;
    std::vector<int> bodies, moons, renderables, orbits, paths, surfaces;  // sin la estrella
    std::vector<NodeId> topNodes;  // planetas y cometas; al destruirlos caen sus lunas
    std::vector<int> starBodies;      // una por estrella; siempre existen
    std::vector<NodeId> starNodes;    // nodo de cada estrella, por indice de StarAt
    std::vector<NodeId> stellarNodes; // nodo de cada entrada de StarSystemData::nodes
    std::vector<int> starOrbits;      // orbitas de las estrellas alrededor de sus baricentros
    double stellarReach = 0;          // km, lo que se separan las estrellas del centro
    std::vector<int> asteroids, beltSlots, beltBodies, rocks;  // beltBodies: por indice de StarSystemData::belts
};

// Lo que un choque cambio para siempre. Se guarda aparte porque la semilla regeneraria el
// sistema tal como era: al volver a cargarlo se le aplica esto encima.
struct BodyChange {
    int data = -1;
    bool destroyed = false, molten = false;
    double mass = 0, radius = 0;
    Vec3f scarDirection;
    float scarSize = 0;
};

struct SystemHistory {
    std::vector<BodyChange> changes;
    std::vector<astro::BeltData> debris;
};

// Lo que la interfaz cuenta de un campo sin tener que recorrer sus familias.
struct BeltInfo {
    std::string name;
    astro::BeltProfile profile = astro::BeltProfile::Regular;
    double inner = 0, outer = 0, density = 0, mass = 0;
    std::size_t rocks = 0, families = 0;
    float minRadius = 0, maxRadius = 0;
    int notable = 0;
    bool debris = false;
};

// Vista inmutable para el hilo de render.
struct WorldCatalog {
    std::vector<Body> bodies;
    std::vector<KeplerOrbit> orbits;
    std::vector<std::shared_ptr<const astro::StarSystemData>> systems;
    std::vector<double> extents;
    std::vector<std::vector<int>> systemBodies;  // cuerpos cargados, en orden, sin la estrella
    std::vector<astro::BodyData> rockData;       // rocas seleccionadas
    std::vector<BeltInfo> belts;                 // por indice de World::belts
};

// Modelos pre generados: sprite lejano y paleta para la esfera cercana.
struct ModelVariant {
    TextureId texture = 0;
    SurfaceLook look;
    Color tint;
};

// Variantes por tipo. Sin catalogo se usan las paletas de DefaultLook y texturas lisas.
struct AstroArt {
    bool hasArt = false;
    TextureId halo = 1;
    TextureId plainStar = 0, plainPlanet = 2;
    std::array<std::vector<TextureId>, static_cast<std::size_t>(astro::StarClass::Count)> stars;
    std::array<std::vector<ModelVariant>, static_cast<std::size_t>(astro::PlanetClass::Count)> planets;
    std::vector<MeshId> asteroidMeshes;  // de cerca, un asteroide con nombre deja de ser una esfera
};

struct BlackHoleRecord {
    int system = 0;
    NodeId node = kNoNode;
    double schwarzschildRadius = 1;
    float spin = 0;
    float discInner = 3, discOuter = 12, brightness = 1, seed = 0;
    Quatf orientation;  // lleva el norte del motor (-Y) al eje de giro
};

struct NebulaRecord {
    Vec3d position;  // km, absoluta: se le resta World::origin
    double radius = 1;
    Color inner, outer;
    float density = 1, seed = 0;
};

struct StreamStats {
    std::size_t loadedSystems = 0;
    std::size_t activeOrbits = 0;    // calculadas este paso
    std::size_t liveOrbits = 0;      // existentes
    std::size_t totalOrbits = 0;     // las que habria sin carga bajo demanda
    std::size_t liveRenderables = 0, totalRenderables = 0;
    std::size_t loads = 0, unloads = 0;  // acumulados
};

struct World {
    SceneGraph scene;
    std::vector<KeplerOrbit> orbits;
    std::vector<OrbitPathRecord> orbitPaths;
    std::vector<Renderable> renderables;
    std::vector<LinkRenderable> links;
    std::vector<NodeId> sectors;
    std::vector<Body> bodies;
    std::vector<std::shared_ptr<const astro::StarSystemData>> systems;  // sin cargar: solo la estrella
    std::vector<double> extents;
    std::vector<SystemState> state;
    std::vector<AsteroidBelt> belts;
    std::vector<BlackHoleRecord> blackHoles;
    std::vector<NebulaRecord> nebulae;
    std::shared_ptr<const std::vector<SkyStar>> sky;
    std::shared_ptr<const std::vector<SurfaceLook>> surfaces;  // se vuelve a publicar al cambiar
    std::shared_ptr<const WorldCatalog> catalog;               // igual

    // Origen flotante: el mundo se mueve para que la camara siga cerca de cero.
    Vec3d origin;
    std::vector<Vec3d> sectorBase;  // posicion absoluta de cada sistema

    GalaxyOptions options;
    AstroArt art;
    StreamStats stats;

    // Reciclaje de huecos: los indices de lo que sigue cargado no cambian nunca.
    std::vector<int> freeBodies, freeRenderables, freeOrbits, freePaths, freeSurfaces;
    std::vector<SurfaceLook> surfaceStore;
    bool dirty = false;

    double simTime = 0;     // ultimo instante pedido; decide que choques ya ocurrieron al cargar
    std::vector<SystemHistory> history;
    std::vector<BeltInfo> beltInfo;
    std::vector<astro::BodyData> rockData;  // rocas convertidas en cuerpo al seleccionarlas
    struct RockLink { std::uint32_t seed = 0; int rock = -1; };
    std::vector<RockLink> rockLinks;
    std::vector<int> freeRocks;
    std::map<std::uint32_t, std::set<int>> deadRocks;  // por semilla de familia
    int highlightBody = -1;  // lo seleccionado: enciende su campo y su orbita tenue
};

inline bool Alive(const Body& b) { return b.node != kNoNode; }

SurfaceLook DefaultLook(astro::PlanetClass kind);
SurfaceLook StarLook(double temperature);
SurfaceLook LookFromMaterials(const astro::BodyData& body);
Color LookTint(const SurfaceLook& look);

// Mayor distancia a la estrella de los planetas de un sistema, sin contar cometas.
double SystemExtent(const astro::StarSystemData& system);

// El mismo indice y la misma semilla dan siempre el mismo sistema.
astro::StarSystemData GenerateWorldSystem(const GalaxyOptions& options, int index);

World BuildGalaxy(const GalaxyOptions& options, const AstroArt& art = {});

void LoadSystem(World& w, int system);
void UnloadSystem(World& w, int system);

// Anota en history lo que produce un choque y devuelve el resultado. No toca la escena, asi que
// sirve tambien para un choque que ocurrio con el sistema descargado. attacker = -1 es una roca.
astro::ImpactResult RecordImpact(SystemHistory& history, const astro::StarSystemData& system, int attacker, double attackerMass, double attackerRadius,
                                 int target, double targetMass, double targetRadius, double speed, std::uint32_t seed);

// Quita un cuerpo cargado junto con sus lunas.
void RemoveBody(World& w, int body);

// Copia que se puede modificar y que sustituye a la publicada.
astro::StarSystemData& EditSystem(World& w, int system);

// Crea el campo correspondiente a una entrada de StarSystemData::belts. Devuelve su cuerpo.
int InstallBelt(World& w, int system, int beltData);

// Aplica a un cuerpo cargado el ultimo cambio de su historia.
void ApplyChange(World& w, int body, const BodyChange& change);

// Una roca de un campo es solo una posicion dentro de su familia. Para seleccionarla pasa a ser
// un cuerpo con ficha propia, y al soltarla vuelve a ser una roca mas.
int PromoteRock(World& w, int belt, int family, int rock, const AsteroidRock& data);
void ReleaseRock(World& w, int body);

// Masa del cuerpo, sea estrella, planeta o roca.
double BodyMass(const World& w, const Body& body);
// Datos del cuerpo; nullptr si es una estrella o un campo.
const astro::BodyData* BodyInfo(const World& w, const Body& body);
// Decide que esta cargado y que se calcula. Llamar antes de UpdateOrbits.
// pinnedBodies: indices en World::bodies que no pueden descargarse, como lo seleccionado.
void StreamWorld(World& w, const Vec3d& camera, float focalPx, const std::vector<int>& pinnedBodies, const FeatureSet& features);

// Vuelve a publicar surfaces y catalog si algo cambio.
void PublishWorld(World& w);

// Si la camara se alejo del origen, lo trae a ella. Devuelve lo que se resto a las posiciones.
Vec3d RecenterOrigin(World& w, Vec3d& camera, double threshold = 1.0e10);

// Sistema mas cercano a un punto.
int NearestSystem(const World& w, const Vec3d& position, double* distance = nullptr);

// Punto de vista inicial: sobre el plano del sistema, con varias orbitas a la vista.
void InitialView(const World& w, Vec3d& position, float& yaw, float& pitch, int system = 0);
Camera MakeCamera(const World& w);

}  // namespace eps
