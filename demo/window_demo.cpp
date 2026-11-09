// window_demo.cpp - Simulador de sistemas solares en una ventana real.
//
//   Hilo principal:   eventos de SDL, swapchain y comandos de GPU.
//   Hilo simulacion:  camara, orbitas, carga de sistemas y FramePacket (con JobSystem).
//
// Controles: raton = mirar, WASD = mover, Q/E = bajar/subir, Shift = rapido, Esc = salir.
//            Tab = siguiente cuerpo, Retroceso = dejar de seguirlo.
//            Coma/Punto = menos/mas tiempo, Espacio = pausa, O = lineas de orbita.
//            H = interfaz, clic = seleccionar lo apuntado, N/P = lista, F = seguir la seleccion.
//            J = entrar o salir de la estacion, a escala humana. R = reparar lo apuntado.
//            F1 = consola de comandos. Escribe 'ayuda'; 'chocar c b' lanza un planeta contra otro.
//
// Opciones:
//   --frames N         termina tras N fotogramas (con camara automatica)
//   --screenshot a.png guarda el ultimo fotograma; requiere --frames
//   --assets carpeta   carpeta "assets" del juego
//   --models carpeta   carpeta con catalogo.tsv
//   --debug            activa la capa de depuracion de Direct3D 12
//   --focus tipo       sigue un cuerpo: star lava rock desert terra ice hot gas icegiant moon comet
//                      blackhole schwarzschild kerr supermassive asteroids
//   --system N         busca el cuerpo o coloca la camara en el sistema N
//   --view top         mira el sistema desde arriba, con todas sus orbitas
//   --radii X          distancia al centro del cuerpo enfocado, en radios
//   --days D           empieza en el dia D simulado
//   --timescale X      segundos simulados por segundo real; por defecto, 1 dia por segundo
//   --seed N           otra galaxia
//   --systems N        numero de sistemas
//   --real-frequencies usa la proporcion real de estrellas: 76 % enanas rojas
//   --routes           dibuja las rutas entre sistemas del juego original
//   --no-orbits        oculta las lineas de orbita
//   --no-hud           empieza con la interfaz oculta
//   --no-streaming     crea todos los cuerpos al empezar
//   --size W H         tamano de la ventana
//   --select tipo      selecciona un cuerpo sin mover la camara; admite los tipos de --focus
//   --click            hace clics simulados hasta seleccionar algo
//   --describe a.txt   escribe los datos y las ecuaciones de todos los cuerpos
//   --hold teclas      mantiene pulsadas w a s d q e shift; sirve para probar el vuelo
//   --tab N            pulsa Tab N veces al empezar
//   --cmd "texto"      ejecuta un comando al empezar; se puede repetir
//   --config a.cfg     ejecuta un archivo de comandos
//   --still            la camara automatica no gira; sirve para mirar una explosion
//   --eva              empieza junto a la estacion, a escala humana
//   --repair N         repara N paneles al empezar, con el mismo rayo que usa la tecla R
//   --model archivo    coloca junto a la estacion un .obj, .gltf o .glb
//   --no-vsync         dibuja sin esperar al monitor; sirve para medir
//   --no-lod           dibuja todas las esferas con el detalle maximo
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "DemoStation.h"
#include "epsilon/Engine.h"
#include "epsilon/HudModel.h"
#include "epsilon/JobSystem.h"
#include "epsilon/SciFiHud.h"
#include "epsilon/SdlGpuBackend.h"
#include "epsilon/TripleBuffer.h"
#include "ps_scanlines.h"

using namespace eps;
using namespace eps::demo;
using Clock = std::chrono::steady_clock;

namespace {

struct InputState {
    float mouseDx = 0, mouseDy = 0;  // acumulado desde la ultima lectura
    int timeSteps = 0, nextBody = 0, pauses = 0, orbitToggles = 0, releases = 0;  // pulsaciones acumuladas
    int hudToggles = 0, clicks = 0, listSteps = 0, follows = 0, evaToggles = 0, repairs = 0;
    bool forward = false, back = false, left = false, right = false, up = false, down = false, fast = false;
    int width = 1600, height = 900;
    bool consoleOpen = false;
    std::string consoleText;
    std::vector<std::string> commands;
};

class SharedInput {
public:
    void Set(const InputState& s) {
        std::lock_guard lock(mutex_);
        InputState merged = s;
        merged.mouseDx += state_.mouseDx;
        merged.mouseDy += state_.mouseDy;
        merged.timeSteps += state_.timeSteps;
        merged.nextBody += state_.nextBody;
        merged.pauses += state_.pauses;
        merged.orbitToggles += state_.orbitToggles;
        merged.releases += state_.releases;
        merged.hudToggles += state_.hudToggles;
        merged.clicks += state_.clicks;
        merged.listSteps += state_.listSteps;
        merged.follows += state_.follows;
        merged.evaToggles += state_.evaToggles;
        merged.repairs += state_.repairs;
        merged.commands.insert(merged.commands.begin(), state_.commands.begin(), state_.commands.end());
        state_ = merged;
    }
    InputState Take() {
        std::lock_guard lock(mutex_);
        InputState s = state_;
        state_.mouseDx = state_.mouseDy = 0;
        state_.timeSteps = state_.nextBody = state_.pauses = state_.orbitToggles = state_.releases = 0;
        state_.hudToggles = state_.clicks = state_.listSteps = state_.follows = state_.evaToggles = state_.repairs = 0;
        state_.commands.clear();
        return s;
    }

private:
    std::mutex mutex_;
    InputState state_;
};

// Lo que entrega la simulacion por fotograma: la escena y lo que muestra la interfaz.
struct SimFrame {
    FramePacket packet;
    HudFrame hud;
};

// Texto de la barra de titulo, escrito por la simulacion.
class SharedStatus {
public:
    void Set(const std::string& text) {
        std::lock_guard lock(mutex_);
        text_ = text;
    }
    std::string Get() {
        std::lock_guard lock(mutex_);
        return text_;
    }

private:
    std::mutex mutex_;
    std::string text_;
};

struct Options {
    int frames = 0, tabs = 0, system = -1, width = 1600, height = 900, systems = 60, repair = 0;
    std::uint32_t seed = 20261105;
    std::string screenshot, assets, models, focus, view, describe, hold, select, config, modelFile;
    std::vector<std::string> commands;
    double radii = 2.2, days = 0, timeScale = astro::kDay;
    bool debug = false, vsync = true, lod = true, orbits = true, routes = false, realFrequencies = false, hud = true, click = false, streaming = true, eva = false, still = false;
};

Options ParseOptions(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) o.frames = std::atoi(argv[++i]);
        else if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
        else if (a == "--assets" && i + 1 < argc) o.assets = argv[++i];
        else if (a == "--focus" && i + 1 < argc) o.focus = argv[++i];
        else if (a == "--radii" && i + 1 < argc) o.radii = std::atof(argv[++i]);
        else if (a == "--days" && i + 1 < argc) o.days = std::atof(argv[++i]);
        else if (a == "--timescale" && i + 1 < argc) o.timeScale = std::atof(argv[++i]);
        else if (a == "--system" && i + 1 < argc) o.system = std::atoi(argv[++i]);
        else if (a == "--systems" && i + 1 < argc) o.systems = std::atoi(argv[++i]);
        else if (a == "--view" && i + 1 < argc) o.view = argv[++i];
        else if (a == "--seed" && i + 1 < argc) o.seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--models" && i + 1 < argc) o.models = argv[++i];
        else if (a == "--describe" && i + 1 < argc) o.describe = argv[++i];
        else if (a == "--hold" && i + 1 < argc) o.hold = argv[++i];
        else if (a == "--tab" && i + 1 < argc) o.tabs = std::atoi(argv[++i]);
        else if (a == "--cmd" && i + 1 < argc) o.commands.push_back(argv[++i]);
        else if (a == "--config" && i + 1 < argc) o.config = argv[++i];
        else if (a == "--repair" && i + 1 < argc) o.repair = std::atoi(argv[++i]);
        else if (a == "--model" && i + 1 < argc) o.modelFile = argv[++i];
        else if (a == "--eva") o.eva = true;
        else if (a == "--still") o.still = true;
        else if (a == "--real-frequencies") o.realFrequencies = true;
        else if (a == "--routes") o.routes = true;
        else if (a == "--no-orbits") o.orbits = false;
        else if (a == "--no-hud") o.hud = false;
        else if (a == "--no-streaming") o.streaming = false;
        else if (a == "--size" && i + 2 < argc) {
            o.width = std::atoi(argv[++i]);
            o.height = std::atoi(argv[++i]);
        }
        else if (a == "--click") o.click = true;
        else if (a == "--select" && i + 1 < argc) o.select = argv[++i];
        else if (a == "--debug") o.debug = true;
        else if (a == "--no-vsync") o.vsync = false;
        else if (a == "--no-lod") o.lod = false;
    }
    return o;
}

int FindBlackHole(const World& w, const std::string& name) {
    int best = -1;
    double bestSize = -1.0;
    for (const SystemState& state : w.state) {
        const Body& b = w.bodies[static_cast<std::size_t>(state.starBody)];
        const astro::StarData& star = w.systems[static_cast<std::size_t>(b.system)]->star;
        if (star.kind != astro::StarClass::BlackHole) continue;
        if (name == "schwarzschild" && star.blackHoleSpin > 0.0) continue;
        if (name == "kerr" && star.blackHoleSpin <= 0.0) continue;
        if (name == "supermassive") {
            if (star.radius > bestSize) {
                bestSize = star.radius;
                best = state.starBody;
            }
            continue;
        }
        return state.starBody;
    }
    return best;
}

// Primer cuerpo del tipo pedido, dentro de un sistema si se indica. Si hace falta, carga el
// sistema para mirar: un cuerpo que no esta en memoria tambien se puede encontrar.
int FindFocus(World& w, const std::string& name, int system) {
    using P = astro::PlanetClass;
    if (name.empty()) return -1;
    if (name == "blackhole" || name == "schwarzschild" || name == "kerr" || name == "supermassive") return FindBlackHole(w, name);
    const struct { const char* name; P kind; } kinds[] = {
        {"lava", P::Lava}, {"rock", P::Rock}, {"desert", P::Desert}, {"terra", P::Terran}, {"ice", P::Ice},
        {"hot", P::HotJupiter}, {"gas", P::GasGiant}, {"icegiant", P::IceGiant}, {"moon", P::Moon}, {"comet", P::Comet},
        {"asteroid", P::Asteroid}, {"belt", P::Belt}};

    for (std::size_t s = 0; s < w.state.size(); ++s) {
        if (system >= 0 && static_cast<int>(s) != system) continue;
        if (name == "star") {
            if (w.systems[s]->star.kind != astro::StarClass::BlackHole) return w.state[s].starBody;
            continue;
        }
        const bool wasLoaded = w.state[s].loaded;
        LoadSystem(w, static_cast<int>(s));
        for (const int i : w.state[s].bodies)
            for (const auto& k : kinds)
                if (name == k.name && w.bodies[static_cast<std::size_t>(i)].kind == k.kind) return i;
        if (!wasLoaded) UnloadSystem(w, static_cast<int>(s));
    }
    return -1;
}

// Primer sistema con ese numero de estrellas, o -1.
int FindSystemWithStars(const World& w, int stars) {
    for (std::size_t s = 0; s < w.systems.size(); ++s)
        if (w.systems[s]->StarCount() == stars) return static_cast<int>(s);
    return -1;
}

// Cuerpo por lo que se escribe en la consola: "seleccion", un tipo o la letra de un planeta.
int ResolveBody(World& w, const std::string& word, int system, int selected, const Vec3d& around) {
    if (word == "seleccion") return selected;
    const SystemState& state = w.state[static_cast<std::size_t>(system)];
    if (word == "asteroide") {
        int best = -1;
        double nearest = 1.0e300;
        for (const int index : state.asteroids) {
            const double d = Length(w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(index)].node) - around);
            if (d < nearest && index != selected) {
                nearest = d;
                best = index;
            }
        }
        return best;
    }
    const std::string suffix = " " + word;
    for (const int index : state.bodies) {
        const std::string& name = w.bodies[static_cast<std::size_t>(index)].name;
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) return index;
    }
    return FindFocus(w, word, system);
}

// Empuja la camara fuera de los cuerpos y devuelve la distancia a la superficie mas cercana.
double KeepOutsideBodies(const World& w, Vec3d& position) {
    double nearest = 1.0e30;
    for (const Body& b : w.bodies) {
        if (!Alive(b) || b.kind == astro::PlanetClass::Belt) continue;
        const Vec3d center = w.scene.WorldPosition(b.node);
        const Vec3d offset = position - center;
        const double distance = Length(offset);
        const double limit = b.radius * 1.03;
        if (distance < limit && distance > 0.0) position = center + offset * (limit / distance);
        nearest = std::min(nearest, std::max(distance, limit) - b.radius);
    }
    return nearest;
}

std::string FormatDistance(double km) {
    char text[64];
    if (km >= 0.1 * astro::kLightYear) std::snprintf(text, sizeof(text), "%.2f al", km / astro::kLightYear);
    else if (km >= 0.01 * astro::kAstronomicalUnit) std::snprintf(text, sizeof(text), "%.3f UA", km / astro::kAstronomicalUnit);
    else std::snprintf(text, sizeof(text), "%.0f km", km);
    return text;
}

std::string FormatTimeScale(double scale) {
    char text[64];
    if (scale >= astro::kYear) std::snprintf(text, sizeof(text), "%.3g anos/s", scale / astro::kYear);
    else if (scale >= astro::kDay) std::snprintf(text, sizeof(text), "%.3g dias/s", scale / astro::kDay);
    else if (scale >= astro::kHour) std::snprintf(text, sizeof(text), "%.3g h/s", scale / astro::kHour);
    else std::snprintf(text, sizeof(text), "%.3g s/s", scale);
    return text;
}

// Vista lateral de un cuerpo: del lado iluminado y algo de costado para ver la sombra.
void LookAtBody(const World& w, const Body& body, double radii, Vec3d& position, float& yaw, float& pitch) {
    const Vec3d center = w.scene.WorldPosition(body.node);
    const astro::StarData& star = w.systems[static_cast<std::size_t>(body.system)]->star;
    Vec3d side = body.isStar ? Vec3d{0.2, -0.3, -1.0} : w.scene.WorldPosition(body.star) - center;
    if (body.isStar && star.kind == astro::StarClass::BlackHole) side = w.scene.World(body.node).TransformDirection(Vec3d{0.18, -0.20, -1.0});
    else side = Mat4d(RotationY(0.75)).TransformDirection(side);
    position = center + Normalize(side) * (body.radius * radii);
    const Vec3f look = Normalize(Vec3f(center - position));
    yaw = std::atan2(look.x, look.z);
    pitch = std::asin(look.y);
}

// Busca una carpeta que contenga un archivo, junto al ejecutable y en la estructura del proyecto.
std::string FindFolder(const std::string& requested, const std::string& file, std::initializer_list<const char*> places) {
    const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    std::vector<std::string> candidates;
    if (!requested.empty()) candidates.push_back(requested);
    for (const char* p : places) candidates.push_back(base + p);
    for (const std::string& c : candidates) {
        SDL_PathInfo info;
        if (SDL_GetPathInfo((c + "/" + file).c_str(), &info)) return c;
    }
    return {};
}

std::string FindAssets(const std::string& requested) {
    return FindFolder(requested, "images/sprites/sol_amarillo.png",
                      {"assets", "../assets", "../../assets", "../../../../bin/Debug/assets", "../../../../../bin/Debug/assets"});
}

std::string FindModels(const std::string& requested) {
    return FindFolder(requested, "catalogo.tsv",
                      {"assets/modelos", "../assets/modelos", "../../assets/modelos", "../../../assets/modelos", "../../../../assets/modelos"});
}

// Lee catalogo.tsv: categoria, archivo, tipo y cuatro colores. Lo escribe tools/generar_modelos.py.
int LoadCatalog(SdlGpuBackend& backend, const std::string& folder, int spriteSize, AstroArt& models) {
    const char* const stars[] = {"O", "B", "A", "F", "G", "K", "M", "RG", "RSG", "BSG", "WD", "NS"};
    const char* const planets[] = {"lava", "rock", "desert", "terran", "ice", "hotjupiter", "gas", "icegiant", "moon", "comet"};

    std::ifstream file(folder + "/catalogo.tsv");
    std::string line;
    int loaded = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string category, name;
        int kind = 0;
        SurfaceLook look;
        row >> category >> name >> kind >> look.a.r >> look.a.g >> look.a.b >> look.b.r >> look.b.g >> look.b.b >> look.c.r >> look.c.g >> look.c.b >>
            look.atmosphere.r >> look.atmosphere.g >> look.atmosphere.b >> look.atmosphere.a;
        if (!row) {
            std::printf("Aviso: fila invalida en catalogo.tsv: %s\n", line.c_str());
            continue;
        }
        look.kind = static_cast<SurfaceKind>(kind);

        const TextureId texture = backend.LoadTexture(folder + "/" + name, spriteSize);
        if (texture == kNoTexture) {
            std::printf("Aviso: %s\n", backend.LastError().c_str());
            continue;
        }
        for (std::size_t i = 0; i < std::size(stars); ++i)
            if (category == stars[i]) models.stars[i].push_back(texture);
        for (std::size_t i = 0; i < std::size(planets); ++i)
            if (category == planets[i]) models.planets[i].push_back({texture, look, Color{}});
        ++loaded;
    }
    return loaded;
}

// Todo lo que el modo a escala humana comparte con el resto de la simulacion.
struct StationState {
    LocalSpace* space = nullptr;
    LocalSpace::InstanceId station = -1;
    int planetBody = -1;       // no se descarga mientras la estacion lo orbita
    int damaged = 0, repaired = 0;
    double orbitRadius = 0, orbitRate = 0;
};

// Reparar: el rayo sale del visitante y encuentra la pieza real, no una caja que la envuelve.
// Devuelve el numero del panel apuntado o -1.
int AimedPanel(const StationState& s, const Vec3d& origin, const Vec3d& direction, double reach, double* distance = nullptr) {
    const RayHit hit = s.space->Raycast(origin, direction, reach);
    if (hit.instance != s.station) return -1;
    const std::string& name = s.space->PartName(s.station, hit.part);
    if (name.rfind("roto_", 0) != 0) return -1;
    if (distance) *distance = hit.distance;
    return std::atoi(name.c_str() + 5);
}

void RepairPanel(StationState& s, int panel) {
    const std::string number = std::to_string(panel);
    s.space->SetPartVisible(s.station, "roto_" + number, false);
    s.space->SetPartVisible(s.station, "nuevo_" + number, true);
    ++s.repaired;
}

void SimulationLoop(Engine& engine, StationState& station, const Options& options, SharedInput& input, SharedStatus& status,
                    TripleBuffer<SimFrame>& mailbox, std::atomic<bool>& stop, std::atomic<int>& target,
                    std::atomic<std::uint64_t>& steps, std::atomic<bool>& scanlines) {
    World& w = engine.GetWorld();
    FeatureSet& features = engine.Features();
    CommandRegistry& commands = engine.Commands();
    LodSettings& lod = engine.Lod();

    w.scene.UpdateWorld(engine.Jobs());  // InitialView lee posiciones del mundo
    const int startSystem = std::clamp(options.system, 0, static_cast<int>(w.sectors.size()) - 1);
    Vec3d position;
    float yaw = 0, pitch = 0;
    InitialView(w, position, yaw, pitch, startSystem);
    const bool fixedView = options.view == "top";

    std::vector<Vec3d> sectorPositions(w.sectors.size());

    features.Set(Feature::OrbitLines, options.orbits);
    features.Set(Feature::Routes, options.routes);
    features.Set(Feature::Hud, options.hud);
    if (!options.lod) {
        lod.sphereMinPixels = 0.0f;
        lod.detailBias = 100.0f;
    }

    constexpr double dt = 1.0 / 120.0;
    constexpr double kTimeLevels[] = {1.0, 60.0, astro::kHour, astro::kDay, 7.0 * astro::kDay, 30.0 * astro::kDay, astro::kYear};
    const float speedLimit = static_cast<float>(kPi) * 0.49f;

    const bool asteroidFocus = options.focus == "asteroids";
    int focus = asteroidFocus ? -1 : FindFocus(w, options.focus, options.system);
    if (!options.focus.empty() && !asteroidFocus && focus < 0) std::printf("Aviso: no hay ningun cuerpo '%s' en esa busqueda.\n", options.focus.c_str());

    double simTime = options.days * astro::kDay;
    double timeScale = options.timeScale;
    bool paused = false;
    int follow = -1;
    Vec3d followOffset;
    int pendingTabs = options.tabs;
    // Sin sistema indicado, se prefiere un cuerpo cercano; si no lo hay, cualquiera.
    int selected = focus >= 0 ? focus : FindFocus(w, options.select, startSystem);
    if (selected < 0) selected = FindFocus(w, options.select, -1);
    int previousSelected = -1;
    double miniSpin = 0;
    int pendingClicks = options.click ? 1 : 0;
    HudFrame lastHud;
    std::vector<std::string> consoleLog;
    double realTime = 0;

    bool eva = options.eva;
    Vec3d viewer{-11.0, -7.0, -44.0};  // m
    Vec3d asteroidOffset{10.0, -14.0, -62.0};
    bool inField = asteroidFocus, fieldReady = false;
    int fieldBelt = -1;      // -1 = el campo mas cercano
    int promotedRock = -1;   // roca convertida en cuerpo mientras esta seleccionada
    std::size_t knownReports = 0;
    Vec3d impactView;
    int impactBody = -1;
    double bannerUntil = 0;
    std::string banner;

    // Se llega por el lado del sol: en el vacio, la cara a la sombra casi no se ve.
    bool arriving = eva;
    const auto faceStation = [&] {
        const Vec3d sun = Normalize(w.scene.WorldPosition(w.sectors[0]) - station.space->ToWorldKm(w.scene, {}));
        const Vec3d sideways = Normalize(Cross(sun, Vec3d{0.0, 1.0, 0.0}));
        viewer = (sun * 0.82 + sideways * 0.5 + Vec3d{0.0, -0.25, 0.0}) * 52.0;
        const Vec3f look = Normalize(Vec3f(Vec3d{0.0, 0.0, -2.0} - viewer));
        yaw = std::atan2(look.x, look.z);
        pitch = std::asin(look.y);
    };

    std::vector<std::string> startup = options.commands;
    std::vector<int> pinned;
    Vec3f lastForward{0, 0, 1};

    const auto say = [&](const std::string& text) {
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) consoleLog.push_back(line);
        while (consoleLog.size() > 8) consoleLog.erase(consoleLog.begin());
    };

    commands.Add("ir", "viaja a: estacion, asteroides, kerr, schwarzschild, supermassive o un tipo de planeta", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: ir <destino>");
        if (args[0] == "estacion") {
            eva = true;
            arriving = true;
            return "Acoplando con la estacion";
        }
        eva = false;
        inField = false;
        if (args[0] == "asteroides") {
            focus = -1;
            follow = -1;
            inField = true;
            fieldReady = false;
            fieldBelt = -1;
            return "Buscando el campo mas cercano";
        }
        const int stars = args[0] == "binaria" ? 2 : args[0] == "triple" ? 3 : args[0] == "cuadruple" ? 4 : 0;
        if (stars > 0 || args[0] == "abierta" || args[0] == "inestable") {
            const int system = args[0] == "abierta" ? 4 : args[0] == "inestable" ? 7 : FindSystemWithStars(w, stars);
            if (system < 0 || system >= static_cast<int>(w.sectors.size())) return commands.Fail("No hay ningun sistema asi");
            focus = follow = -1;
            InitialView(w, position, yaw, pitch, system);
            return "Rumbo a SYS-" + std::to_string(system);
        }
        const int found = FindFocus(w, args[0], -1);
        if (found < 0) return commands.Fail("No hay ningun '" + args[0] + "'");
        selected = follow = found;
        const Body& body = w.bodies[static_cast<std::size_t>(found)];
        const bool hole = body.isStar && w.systems[static_cast<std::size_t>(body.system)]->star.kind == astro::StarClass::BlackHole;
        LookAtBody(w, body, hole ? 40.0 : 4.0, position, yaw, pitch);
        followOffset = position - w.scene.WorldPosition(body.node);
        return "Rumbo a " + body.name;
    });
    commands.Add("chocar", "chocar <a> <b> [dias]: a y b son seleccion, asteroide, la letra de un planeta o un tipo. chocar rocas", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: chocar <atacante> [objetivo] [dias], o chocar rocas");
        if (args[0] == "rocas") return engine.Asteroids().StartRockCollision(position) ? "Dos rocas en curso de choque" : commands.Fail("Acercate a un campo de asteroides");
        const int system = selected >= 0 ? w.bodies[static_cast<std::size_t>(selected)].system : NearestSystem(w, position);
        LoadSystem(w, system);
        const int victim = args.size() >= 2 ? ResolveBody(w, args[1], system, selected, position) : selected;
        if (victim < 0) return commands.Fail("No encuentro el objetivo");
        const int attacker = ResolveBody(w, args[0], system, victim, w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(victim)].node));
        if (attacker < 0) return commands.Fail("No encuentro el atacante");
        const double days = args.size() >= 3 ? std::atof(args[2].c_str()) : 0.0;
        std::string error;
        if (!engine.Collisions().Schedule(attacker, victim, simTime, days * astro::kDay, &error)) return commands.Fail(error);
        const double eta = (engine.Collisions().Pending().back().time - simTime) / astro::kDay;
        return w.bodies[static_cast<std::size_t>(attacker)].name + " > " + w.bodies[static_cast<std::size_t>(victim)].name + " en " + std::to_string(eta) + " dias";
    });
    commands.Add("tiempo", "segundos simulados por segundo real", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return FormatTimeScale(timeScale);
        timeScale = std::atof(args[0].c_str());
        return FormatTimeScale(timeScale);
    });
    commands.Add("efecto", "efecto de pantalla propio: efecto cabina on|off", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.size() < 2 || args[0] != "cabina") return commands.Fail("Uso: efecto cabina on|off");
        scanlines.store(args[1] == "on", std::memory_order_relaxed);
        return "cabina: " + args[1];
    });

    // Antes de la primera imagen, para que las capturas ya muestren su efecto.
    for (const std::string& line : startup) {
        const std::string answer = commands.Execute(line);
        say(answer);
        std::printf("> %s\n%s\n", line.c_str(), answer.c_str());
    }
    if (!options.config.empty()) {
        std::ifstream file(options.config);
        std::stringstream text;
        text << file.rdbuf();
        if (!file) std::printf("Aviso: no se pudo leer %s\n", options.config.c_str());
        const int failed = commands.ExecuteScript(text.str());
        if (failed > 0) std::printf("Aviso: %d lineas no reconocidas en %s\n", failed, options.config.c_str());
    }

    auto next = Clock::now();

    for (std::uint64_t step = 1; !stop.load(std::memory_order_acquire); ++step) {
        InputState in = input.Take();
        in.nextBody += pendingTabs;
        pendingTabs = 0;
        for (const char c : options.hold) {
            in.forward |= c == 'w';
            in.back |= c == 's';
            in.left |= c == 'a';
            in.right |= c == 'd';
            in.down |= c == 'q';
            in.up |= c == 'e';
            in.fast |= c == 'f';
        }
        for (const std::string& line : in.commands) {
            say("> " + line);
            say(commands.Execute(line));
        }

        if (in.pauses % 2 != 0) paused = !paused;
        if (in.orbitToggles % 2 != 0) features.Toggle(Feature::OrbitLines);
        if (in.releases > 0) follow = -1;
        if (in.hudToggles % 2 != 0) features.Toggle(Feature::Hud);
        if (in.evaToggles % 2 != 0) {
            eva = !eva;
            arriving = eva;
        }
        const bool hudVisible = features.Enabled(Feature::Hud);
        if (options.click && selected < 0 && step % 30 == 0) pendingClicks = 1;  // hasta acertar: la camara automatica va girando
        if (!lastHud.labels.empty() || step > 60) {  // el clic simulado espera a la primera imagen
            in.clicks += pendingClicks;
            pendingClicks = 0;
        }
        if (in.clicks > 0 && hudVisible) {
            // Primero la roca que se esta apuntando; si no hay, la etiqueta mas cercana.
            RockPick rock;
            if (!eva && engine.Asteroids().PickRock(position, Vec3d(lastForward), simTime, 0.03f, rock)) selected = PromoteRock(w, rock.belt, rock.family, rock.rock, rock.data);
            else {
                const int picked = PickHudBody(lastHud);
                if (picked >= 0) selected = picked;
            }
        }
        if (in.listSteps != 0 && hudVisible) selected = StepHudSelection(lastHud, in.listSteps);
        for (; in.timeSteps > 0; --in.timeSteps)
            timeScale = *std::upper_bound(std::begin(kTimeLevels), std::end(kTimeLevels) - 1, timeScale);
        for (; in.timeSteps < 0; ++in.timeSteps) {
            const double* level = std::lower_bound(std::begin(kTimeLevels), std::end(kTimeLevels), timeScale);
            timeScale = level == std::begin(kTimeLevels) ? kTimeLevels[0] : *(level - 1);
        }

        // Un choque puede haber quitado lo que se estaba mirando.
        const auto gone = [&](int body) { return body >= 0 && !Alive(w.bodies[static_cast<std::size_t>(body)]); };
        if (gone(selected)) selected = -1;
        if (gone(follow)) follow = -1;
        if (gone(focus)) focus = -1;
        if (gone(station.planetBody)) station.planetBody = -1;

        // Una roca solo es un cuerpo mientras esta seleccionada.
        if (promotedRock >= 0 && promotedRock != selected && follow != promotedRock) {
            bool busy = false;
            for (const ScheduledImpact& p : engine.Collisions().Pending()) busy = busy || p.attacker == promotedRock || p.target == promotedRock;
            if (!busy) ReleaseRock(w, promotedRock);
            promotedRock = -1;
        }
        if (selected >= 0 && w.bodies[static_cast<std::size_t>(selected)].extra >= 0) promotedRock = selected;
        w.highlightBody = selected;

        // Lo que no puede desaparecer: la seleccion, lo seguido y el planeta de la estacion.
        pinned.clear();
        for (const int body : {selected, follow, focus, station.planetBody})
            if (body >= 0) pinned.push_back(body);

        if (!paused) simTime += dt * timeScale;
        engine.Recenter(position);
        engine.Advance(position, static_cast<float>(in.width) * 0.5f, simTime, pinned, paused ? 0.0 : dt);
        if (gone(selected)) selected = -1;
        if (gone(follow)) follow = -1;
        if (gone(focus)) focus = -1;

        // La estacion da la vuelta a su planeta en una orbita circular baja.
        const double phase = station.orbitRate * simTime + 2.2;
        station.space->anchorOffset = Vec3d{std::cos(phase), 0.15 * std::sin(phase), std::sin(phase)} * station.orbitRadius;
        station.space->hasViewer = eva;
        if (arriving) {
            faceStation();
            arriving = false;
        }

        // Sistema mas cercano: se usa para Tab y para la barra de titulo.
        double nearestStar = 0;
        const int nearestSystem = NearestSystem(w, position, &nearestStar);
        for (std::size_t i = 0; i < w.sectors.size(); ++i) sectorPositions[i] = w.scene.WorldPosition(w.sectors[i]);

        for (; in.nextBody > 0; --in.nextBody) {
            const int system = follow >= 0 ? w.bodies[static_cast<std::size_t>(follow)].system : nearestSystem;
            const SystemState& state = w.state[static_cast<std::size_t>(system)];
            // Estrellas y planetas: los campos y sus asteroides se eligen desde la lista.
            std::vector<int> stops = state.starBodies;
            for (const int b : state.bodies) {
                const astro::PlanetClass kind = w.bodies[static_cast<std::size_t>(b)].kind;
                if (kind != astro::PlanetClass::Belt && kind != astro::PlanetClass::Asteroid) stops.push_back(b);
            }
            int candidate = stops.front();
            for (std::size_t k = 0; k + 1 < stops.size(); ++k)
                if (follow == stops[k]) candidate = stops[k + 1];
            follow = candidate;
            selected = candidate;
            const Body& body = w.bodies[static_cast<std::size_t>(follow)];
            LookAtBody(w, body, body.kind == astro::PlanetClass::Comet && !body.isStar ? 60.0 : 4.0, position, yaw, pitch);
            followOffset = position - w.scene.WorldPosition(body.node);
        }
        if (in.follows > 0 && selected >= 0 && w.bodies[static_cast<std::size_t>(selected)].kind == astro::PlanetClass::Belt) {
            // Un campo no es un punto al que seguir: se viaja al cumulo mas cercano.
            follow = -1;
            focus = -1;
            inField = true;
            fieldReady = false;
            fieldBelt = w.bodies[static_cast<std::size_t>(selected)].belt;
        } else if (in.follows > 0 && selected >= 0) {
            inField = false;
            follow = selected;
            const Body& body = w.bodies[static_cast<std::size_t>(follow)];
            LookAtBody(w, body, body.kind == astro::PlanetClass::Comet && !body.isStar ? 60.0 : 4.0, position, yaw, pitch);
            followOffset = position - w.scene.WorldPosition(body.node);
        }

        Camera cam;
        cam.SetViewport(in.width, in.height, static_cast<float>(in.width) * 0.5f, kNearPlaneKm);

        if (impactBody >= 0 && impactBody == follow) {
            const Body& body = w.bodies[static_cast<std::size_t>(follow)];
            followOffset = impactView * Length(followOffset);
            const Vec3f look = Normalize(Vec3f(followOffset * -1.0));
            yaw = std::atan2(look.x, look.z);
            pitch = std::asin(look.y);
            (void)body;
            impactBody = -1;
        }
        if (impactBody >= 0 && impactBody == focus) {
            const Body& body = w.bodies[static_cast<std::size_t>(focus)];
            const Vec3d center = w.scene.WorldPosition(body.node);
            position = center + impactView * (body.radius * options.radii);
            const Vec3f look = Normalize(Vec3f(center - position));
            yaw = std::atan2(look.x, look.z);
            pitch = std::asin(look.y);
        } else if (focus >= 0) {
            LookAtBody(w, w.bodies[static_cast<std::size_t>(focus)], options.radii, position, yaw, pitch);
        } else if (follow >= 0) {
            position = w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(follow)].node) + followOffset;
        } else if (fixedView) {
            InitialView(w, position, yaw, pitch, startSystem);
        } else if (options.frames > 0 && options.hold.empty() && !eva && !options.still) {
            yaw += static_cast<float>(dt) * 0.12f;  // camara automatica
        }

        // Ir a un campo de asteroides: se viaja con el, que sigue su propia orbita.
        if (inField && !eva) {
            Vec3d center;
            if (!w.belts.empty() && AsteroidLayer::NearestFamily(w, position, simTime, center, fieldBelt)) {
                if (!fieldReady) {
                    const Vec3d sun = Normalize(w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(0)].star) - center);
                    asteroidOffset = (sun * 0.9 + Normalize(Cross(sun, Vec3d{0.0, 1.0, 0.0})) * 0.4) * 64.0;
                    for (const AsteroidBelt& belt : w.belts) {
                        if (!belt.used) continue;
                        const Vec3d near = Normalize(w.scene.WorldPosition(belt.parent) - center);
                        asteroidOffset = (near * 0.9 + Normalize(Cross(near, Vec3d{0.0, 1.0, 0.0})) * 0.4) * 64.0;
                        break;
                    }
                    const Vec3f look = Normalize(Vec3f(-asteroidOffset));
                    yaw = std::atan2(look.x, look.z);
                    pitch = std::asin(look.y);
                    fieldReady = true;
                }
                position = center + asteroidOffset;
                if (options.frames == 0) {  // con el raton se mira alrededor; el campo sigue su orbita
                    asteroidOffset = position - center;
                }
            } else {
                for (int s = 0; s < static_cast<int>(w.state.size()) && w.belts.empty(); ++s) LoadSystem(w, s);
            }
        }

        if (options.frames == 0 && !in.consoleOpen) {
            yaw += in.mouseDx * 0.002f;
            pitch = std::clamp(pitch + in.mouseDy * 0.002f, -speedLimit, speedLimit);
        }
        cam.SetYawPitchRoll(yaw, pitch, 0.0f);
        lastForward = cam.Forward();

        std::string objective;
        if (eva) {
            // Escala humana: metros y metros por segundo, dentro de la burbuja de la estacion.
            const double speed = (in.fast ? 12.0 : 3.0) * dt;
            const Vec3d forward(cam.Forward()), right(cam.Right()), down(cam.Down());
            if (!in.consoleOpen) {
                if (in.forward) viewer += forward * speed;
                if (in.back) viewer += forward * -speed;
                if (in.right) viewer += right * speed;
                if (in.left) viewer += right * -speed;
                if (in.down) viewer += down * speed;
                if (in.up) viewer += down * -speed;
            }
            station.space->viewer = viewer;
            position = station.space->ToWorldKm(w.scene, viewer);

            double distance = 0;
            const int aimed = AimedPanel(station, viewer, forward, 8.0, &distance);
            if (in.repairs > 0 && aimed >= 0) RepairPanel(station, aimed);
            char text[128];
            if (station.repaired == station.damaged) std::snprintf(text, sizeof(text), "ESTACION REPARADA  %d/%d", station.repaired, station.damaged);
            else if (aimed >= 0) std::snprintf(text, sizeof(text), "PANEL AVERIADO A %.1f m  -  PULSA R  (%d/%d)", distance, station.repaired, station.damaged);
            else std::snprintf(text, sizeof(text), "REPARACIONES %d/%d  -  ACERCATE A UN PANEL ROJO", station.repaired, station.damaged);
            objective = text;
        } else {
            // Aviso de choques: la cuenta atras y, cuando ocurre, lo que paso.
            const std::vector<ImpactReport>& reports = engine.Collisions().Reports();
            if (reports.size() != knownReports) {
                if (!reports.empty()) {
                    const ImpactReport& r = reports.back();
                    char text[192];
                    std::snprintf(text, sizeof(text), "IMPACTO  %s > %s  -  %s  -  %.1f km/s", r.attacker.c_str(), r.target.c_str(), astro::Name(r.result.outcome), r.result.speed);
                    banner = text;
                    bannerUntil = realTime + 9.0;
                    say(text);
                    // Una explosion dura segundos: con el tiempo a dias por segundo, su planeta
                    // saldria de cuadro antes de que se pudiera ver.
                    if (r.system == nearestSystem && timeScale > 60.0) timeScale = 60.0;
                    // Si se estaba mirando ese cuerpo, la camara pasa al lado del golpe.
                    if (r.targetBody >= 0 && (r.targetBody == focus || r.targetBody == follow)) {
                        const Body& hit = w.bodies[static_cast<std::size_t>(r.targetBody)];
                        const Vec3d sun = Normalize(w.scene.WorldPosition(hit.star) - w.scene.WorldPosition(hit.node));
                        impactView = Normalize(Vec3d(r.direction) * 0.85 + sun * 0.5);
                        impactBody = r.targetBody;
                    }
                }
                knownReports = reports.size();
            }
            if (realTime < bannerUntil) {
                objective = banner;
            } else if (!engine.Collisions().Pending().empty()) {
                const ScheduledImpact& p = engine.Collisions().Pending().front();
                const Body& a = w.bodies[static_cast<std::size_t>(p.attacker)];
                const Body& t = w.bodies[static_cast<std::size_t>(p.target)];
                if (Alive(a) && Alive(t)) {
                    char text[192];
                    std::snprintf(text, sizeof(text), "IMPACTO EN %.1f DIAS  -  %s > %s", (p.time - simTime) / astro::kDay, a.name.c_str(), t.name.c_str());
                    objective = text;
                }
            }

            // La velocidad depende de la distancia a la superficie: el viaje es exponencial, asi
            // que cruzar anos luz y maniobrar a pocos kilometros usan los mismos controles.
            const double surfaceDistance = KeepOutsideBodies(w, position);
            const double speed = std::clamp(surfaceDistance * 0.8, 0.05, 5.0e13) * (in.fast ? 6.0 : 1.0) * dt;
            if (focus < 0 && !in.consoleOpen) {
                const Vec3d forward(cam.Forward()), right(cam.Right()), down(cam.Down());
                if (in.forward) position += forward * speed;
                if (in.back) position += forward * -speed;
                if (in.right) position += right * speed;
                if (in.left) position += right * -speed;
                if (in.down) position += down * speed;
                if (in.up) position += down * -speed;
                KeepOutsideBodies(w, position);
                if (follow >= 0) followOffset = position - w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(follow)].node);
            }
        }
        cam.SetPosition(position);

        target.store(PickNearestToCenter(cam, sectorPositions.data(), sectorPositions.size()), std::memory_order_relaxed);

        realTime += dt;
        SimFrame& slot = mailbox.WriteSlot();
        engine.Build(cam, realTime, slot.packet);

        HudFrame& hud = slot.hud;
        hud.visible = hudVisible;
        hud.simTime = simTime;
        hud.simClock = realTime;
        hud.timeScale = timeScale;
        hud.paused = paused;
        hud.followed = follow;
        hud.objective = objective;
        hud.consoleOpen = in.consoleOpen;
        hud.console = in.consoleText;
        hud.consoleLog = consoleLog;
        BuildHud(w, cam, selected, hud);
        lastHud.labels = hud.labels;
        lastHud.list = hud.list;
        lastHud.width = hud.width;
        lastHud.height = hud.height;
        lastHud.selected = hud.selected;

        slot.packet.hasMiniature = false;
        if (hudVisible && selected >= 0) {
            const Body& body = w.bodies[static_cast<std::size_t>(selected)];
            Renderable shown;
            if (body.renderable >= 0) shown = w.renderables[static_cast<std::size_t>(body.renderable)];
            if (selected != previousSelected) miniSpin = shown.spinPhase;
            previousSelected = selected;
            // Se limita para que un dia por segundo no se convierta en un parpadeo.
            if (!paused) miniSpin += std::clamp(std::abs(shown.spinRate * timeScale), 0.0, 1.5) * dt;
            shown.spinPhase = miniSpin;
            shown.spinRate = 0.0;

            if (shown.surface >= 0) {  // un agujero negro no tiene superficie que mostrar
                const HudLayout layout = ComputeHudLayout(hud.width, hud.height);
                const float focal = static_cast<float>(layout.miniSize) * 1.25f;
                constexpr float kDistance = 3.2f;
                // Vista fija respecto al plano del sistema: se aprecia la inclinacion real del eje.
                const Quatf plane = Quatf::Cast(w.scene.Local(body.star).rotation);
                const Quatf viewer3d = plane * Quatf::FromAxisAngle({1, 0, 0}, -0.3f);

                SphereInstance& m = slot.packet.miniature;
                m.viewCenter = {0, 0, kDistance};
                m.radius = 1.0f;
                m.viewToBody = BodyOrientation(shown, 0.0).Conjugate() * viewer3d;
                m.lightDirection = body.isStar ? Vec3f{} : Normalize(Vec3f{-0.62f, -0.42f, -0.66f});
                m.pixelRadius = SilhouettePixels(1.0f, kDistance, focal);
                m.detail = SurfaceDetail(focal / (kDistance - 1.0f), lod);
                m.seed = shown.seed;
                m.distance = kDistance;
                m.surface = static_cast<std::uint16_t>(shown.surface);
                slot.packet.hasMiniature = true;
                slot.packet.miniatureX = layout.miniX;
                slot.packet.miniatureY = layout.miniY;
                slot.packet.miniatureSize = layout.miniSize;
                slot.packet.miniatureFocalPx = focal;
            }
        }
        mailbox.Publish();
        steps.store(step, std::memory_order_relaxed);

        if (step % 30 == 1) {
            const std::string where = eva ? std::string("Estacion, a escala humana")
                                      : follow >= 0 ? "Siguiendo " + w.bodies[static_cast<std::size_t>(follow)].name
                                                    : "SYS-" + std::to_string(nearestSystem) + " (" + astro::Info(w.systems[static_cast<std::size_t>(nearestSystem)]->star.kind).name + ")";
            char clock[96];
            std::snprintf(clock, sizeof(clock), "Dia %.1f", simTime / astro::kDay);
            const StreamStats& s = w.stats;
            char loaded[96];
            std::snprintf(loaded, sizeof(loaded), "%zu/%zu sistemas, %zu/%zu orbitas", s.loadedSystems, w.state.size(), s.activeOrbits, s.totalOrbits);
            status.Set(where + " a " + FormatDistance(nearestStar) + " de su estrella | " + clock + " | " + (paused ? std::string("Pausa") : FormatTimeScale(timeScale)) +
                       " | " + loaded + (selected >= 0 ? " | Seleccion: " + w.bodies[static_cast<std::size_t>(selected)].name : std::string()));
        }

        next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(dt));
        std::this_thread::sleep_until(next);
    }
}

int Run(const Options& options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Proyecto Epsilon", options.width, options.height, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::printf("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int exitCode = 0;
    {
        SdlGpuBackend backend;
        if (!backend.Initialize(window, options.debug)) {
            std::printf("Backend: %s\n", backend.LastError().c_str());
            exitCode = 1;
        } else {
            SciFiHud hud;
            const bool hudReady = hud.Initialize(backend);
            if (!hudReady) std::printf("Aviso: no se pudo iniciar la interfaz.\n");

            const std::string assets = FindAssets(options.assets);
            const std::string modelFolder = FindModels(options.models);
            AstroArt models;
            models.halo = SdlGpuBackend::kGlowTexture;
            models.plainStar = models.plainPlanet = SdlGpuBackend::kGlowTexture;
            TextureId sky = kNoTexture, sprite2d = SdlGpuBackend::kWhiteTexture;
            // Los sprites solo se dibujan por debajo de lod.sphereMinPixels, unos 16 pixeles
            // de ancho, asi que 64 ya sobra y el resto de la imagen no se sube a la GPU.
            const int spriteSize = options.lod ? 64 : 0;

            int catalogModels = 0;
            if (!modelFolder.empty()) catalogModels = LoadCatalog(backend, modelFolder, spriteSize, models);
            else std::printf("Aviso: no se encontro catalogo.tsv; ejecuta tools/generar_modelos.py.\n");

            if (assets.empty()) {
                std::printf("Aviso: no se encontro la carpeta assets; no habra panorama de fondo.\n");
            } else {
                const std::string images = assets + "/images/";
                const auto load = [&](const char* file, int maxSize) {
                    const TextureId id = backend.LoadTexture(images + file, maxSize);
                    if (id == kNoTexture) std::printf("Aviso: %s\n", backend.LastError().c_str());
                    return id;
                };
                using S = astro::StarClass;
                using P = astro::PlanetClass;
                const auto addStar = [&](const char* file, std::initializer_list<S> classes) {
                    const TextureId id = load(file, spriteSize);
                    if (id == kNoTexture) return;
                    for (const S c : classes) models.stars[static_cast<std::size_t>(c)].push_back(id);
                };
                const auto addPlanet = [&](const char* file, P kind) {
                    const TextureId id = load(file, spriteSize);
                    if (id != kNoTexture) models.planets[static_cast<std::size_t>(kind)].push_back({id, DefaultLook(kind), Color{}});
                };
                // Tu arte original sigue en la mezcla, junto a los modelos generados.
                addStar("sprites/sol_amarillo.png", {S::F, S::G, S::K});
                addStar("sprites/sol_azul.png", {S::O, S::B, S::A, S::BlueSupergiant});
                addStar("sprites/sol_rojo.png", {S::M, S::RedGiant, S::RedSupergiant});
                addStar("sprites/sol_neutrones.png", {S::WhiteDwarf, S::NeutronStar});
                addStar("sprites/agujero_negro.png", {S::BlackHole});
                addPlanet("sprites/planeta_terra.png", P::Terran);
                addPlanet("sprites/planeta_gas.png", P::GasGiant);
                addPlanet("sprites/planeta_lava.png", P::Lava);
                sprite2d = load("sprites/planeta_terra.png", 256);
                sky = load("backgrounds/fondo_panoramico.png", 0);
            }
            models.hasArt = catalogModels > 0 || !assets.empty();

            EngineConfig config;
            config.galaxy.seed = options.seed;
            config.galaxy.systems = options.systems;
            config.galaxy.realFrequencies = options.realFrequencies;
            config.galaxy.streaming = options.streaming;
            config.galaxy.blackHoles = true;
            config.galaxy.nebulae = 3;
            config.skyTexture = sky;
            config.galaxy.impacts = true;
            config.asteroids.dot = SdlGpuBackend::kGlowTexture;
            for (std::uint32_t seed = 1; seed <= 6; ++seed) config.asteroids.meshes.push_back(backend.CreateMesh(MakeAsteroid(seed * 37u)));
            models.asteroidMeshes = config.asteroids.meshes;
            config.blasts.glow = SdlGpuBackend::kGlowTexture;
            config.blasts.ring = SdlGpuBackend::kRingTexture;
            config.blasts.meshes = config.asteroids.meshes;
            config.rockCollisionsPerMinute = 5.0f;

            Engine engine(config, models);
            World& world = engine.GetWorld();

            if (!options.describe.empty()) {
                std::ofstream out(options.describe);
                for (std::size_t i = 0; i < world.state.size(); ++i)
                    out << astro::DescribeSystem(GenerateWorldSystem(config.galaxy, static_cast<int>(i)), "SYS-" + std::to_string(i)) << "\n";
                std::printf("Ecuaciones escritas en %s\n", options.describe.c_str());
            }
            if (hudReady) backend.SetOverlay(&hud);

            // La estacion orbita un planeta del primer sistema; su modulo no sabe que es un planeta.
            StationState station;
            station.planetBody = FindFocus(world, "terra", 0);
            if (station.planetBody < 0) station.planetBody = FindFocus(world, "rock", 0);
            if (station.planetBody < 0) station.planetBody = world.state[0].bodies.at(0);
            {
                const Body& planet = world.bodies[static_cast<std::size_t>(station.planetBody)];
                const astro::BodyData& data = world.systems[0]->bodies[static_cast<std::size_t>(planet.data)];
                station.orbitRadius = planet.radius * 1.07;
                station.orbitRate = std::sqrt(astro::kGravity * data.mass / (station.orbitRadius * station.orbitRadius * station.orbitRadius));

                StationAssets assets3d = BuildStation(backend);
                station.damaged = assets3d.damagedPanels;
                LocalSpace& space = engine.CreateLocalSpace(planet.node, world.sectors[0]);
                space.ambient = 0.10f;
                station.space = &space;
                station.station = space.Spawn(assets3d.station);
                space.Spawn(assets3d.astronaut, {1.4, -4.1, -7.0});
                for (int i = 0; i < station.damaged; ++i) space.SetPartVisible(station.station, "nuevo_" + std::to_string(i), false);

                // Modelo 2D: un cuadro que siempre mira a la camara. Es el mismo sprite del juego.
                const MeshId quad = backend.CreateMesh(MakeQuad(1.0f, 1.0f));
                auto flat = std::make_shared<ModelAsset>();
                flat->name = "holograma";
                flat->parts.push_back(MakeSpritePart("holograma", quad, sprite2d, 2.4f, 2.4f));
                space.Spawn(flat, {-5.5, -4.0, -8.0});

                if (!options.modelFile.empty()) {
                    std::vector<ImportedPart> parts;
                    std::string error;
                    const bool isObj = options.modelFile.size() > 4 && options.modelFile.compare(options.modelFile.size() - 4, 4, ".obj") == 0;
                    bool loaded = false;
                    if (isObj) {
                        ImportedPart part;
                        part.name = "obj";
                        loaded = LoadObj(options.modelFile, part.mesh, &error);
                        parts.push_back(std::move(part));
                    } else {
                        loaded = LoadGltf(options.modelFile, parts, &error);
                    }
                    if (loaded) {
                        ModelRef imported = BuildModel(options.modelFile, parts, [&](const MeshData& m) { return backend.CreateMesh(m); });
                        space.Spawn(imported, {8.0, -5.0, -14.0});
                        std::printf("Modelo cargado: %s (%zu partes)\n", options.modelFile.c_str(), parts.size());
                    } else {
                        std::printf("Aviso: %s\n", error.c_str());
                    }
                }

                // Prueba sin teclado: el mismo rayo de la tecla R, disparado desde fuera del casco.
                const float zones[] = {-8.0f, -3.0f, 2.5f, 7.0f};
                for (int i = 0; i < std::min(options.repair, station.damaged); ++i) {
                    const int panel = AimedPanel(station, Vec3d{0.0, -7.0, zones[i]}, Vec3d{0.0, 1.0, 0.0}, 8.0);
                    if (panel >= 0) RepairPanel(station, panel);
                    else std::printf("Aviso: el rayo no encontro el panel %d\n", i);
                }
                if (options.repair > 0) std::printf("Paneles reparados: %d de %d\n", station.repaired, station.damaged);
            }

            const int scanlineEffect = backend.AddPostEffect("cabina", gPsScanlines, sizeof(gPsScanlines));
            if (scanlineEffect < 0) std::printf("Aviso: %s\n", backend.LastError().c_str());

            SharedInput input;
            InputState state;
            SDL_GetWindowSizeInPixels(window, &state.width, &state.height);
            input.Set(state);

            TripleBuffer<SimFrame> mailbox;
            std::atomic<bool> stop{false};
            std::atomic<int> target{-1};
            std::atomic<std::uint64_t> steps{0};
            std::atomic<bool> scanlines{false};
            SharedStatus status;
            std::thread simulation(SimulationLoop, std::ref(engine), std::ref(station), std::cref(options), std::ref(input), std::ref(status),
                                   std::ref(mailbox), std::ref(stop), std::ref(target), std::ref(steps), std::ref(scanlines));

            if (options.frames == 0) SDL_SetWindowRelativeMouseMode(window, true);
            if (!options.vsync && !backend.SetVsync(false)) std::printf("Aviso: no se pudo desactivar la sincronizacion vertical.\n");
            std::size_t lastSprites = 0, lastSpheres = 0, lastLines = 0, lastMeshes = 0, lastAsteroids = 0, lastBlackHoles = 0, lastNebulae = 0;

            std::uint64_t rendered = 0;
            const auto start = Clock::now();
            auto lastTitle = start;
            std::uint64_t lastTitleFrames = 0;
            bool running = true;
            bool consoleOpen = false;
            std::string consoleText;

            while (running) {
                state = {};
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_EVENT_QUIT) running = false;
                    else if (e.type == SDL_EVENT_TEXT_INPUT && consoleOpen) {
                        consoleText += e.text.text;
                    } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1 && !e.key.repeat) {
                        consoleOpen = !consoleOpen;
                        if (consoleOpen) SDL_StartTextInput(window);
                        else SDL_StopTextInput(window);
                    } else if (e.type == SDL_EVENT_KEY_DOWN && consoleOpen) {
                        if (e.key.key == SDLK_ESCAPE) {
                            consoleOpen = false;
                            SDL_StopTextInput(window);
                        } else if (e.key.key == SDLK_BACKSPACE && !consoleText.empty()) {
                            consoleText.pop_back();
                        } else if (e.key.key == SDLK_RETURN && !e.key.repeat) {
                            if (!consoleText.empty()) state.commands.push_back(consoleText);
                            consoleText.clear();
                        }
                    } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                        switch (e.key.key) {
                            case SDLK_ESCAPE: running = false; break;
                            case SDLK_TAB: ++state.nextBody; break;
                            case SDLK_BACKSPACE: ++state.releases; break;
                            case SDLK_SPACE: ++state.pauses; break;
                            case SDLK_O: ++state.orbitToggles; break;
                            case SDLK_H: ++state.hudToggles; break;
                            case SDLK_N: ++state.listSteps; break;
                            case SDLK_P: --state.listSteps; break;
                            case SDLK_F: ++state.follows; break;
                            case SDLK_J: ++state.evaToggles; break;
                            case SDLK_R: ++state.repairs; break;
                            case SDLK_PERIOD: ++state.timeSteps; break;
                            case SDLK_COMMA: --state.timeSteps; break;
                            default: break;
                        }
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                        ++state.clicks;
                    } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                        state.mouseDx += e.motion.xrel;
                        state.mouseDy += e.motion.yrel;
                    }
                }
                const bool* keys = SDL_GetKeyboardState(nullptr);
                state.forward = keys[SDL_SCANCODE_W];
                state.back = keys[SDL_SCANCODE_S];
                state.left = keys[SDL_SCANCODE_A];
                state.right = keys[SDL_SCANCODE_D];
                state.down = keys[SDL_SCANCODE_Q];
                state.up = keys[SDL_SCANCODE_E];
                state.fast = keys[SDL_SCANCODE_LSHIFT];
                state.consoleOpen = consoleOpen;
                state.consoleText = consoleText;
                SDL_GetWindowSizeInPixels(window, &state.width, &state.height);
                input.Set(state);

                backend.SetPostEffect(scanlineEffect, scanlines.load(std::memory_order_relaxed));

                // Al medir se repite el ultimo paquete: asi el limite es la GPU y no los 120 Hz de la simulacion.
                const bool fresh = mailbox.AcquireLatest();
                if (fresh || (!options.vsync && rendered > 0)) {
                    const bool last = options.frames > 0 && static_cast<int>(rendered) + 1 >= options.frames;
                    if (last && !options.screenshot.empty()) backend.RequestScreenshot(options.screenshot);
                    const SimFrame& simFrame = mailbox.ReadSlot();
                    const FramePacket& packet = simFrame.packet;
                    hud.SetFrame(simFrame.hud);
                    backend.Render(packet);
                    lastSprites = packet.stats.spritesVisible;
                    lastSpheres = packet.stats.spheresVisible;
                    lastLines = packet.stats.linesVisible;
                    lastMeshes = packet.stats.meshesVisible;
                    lastAsteroids = packet.stats.asteroidsCalculated;
                    lastBlackHoles = packet.blackHoles.size();
                    lastNebulae = packet.nebulae.size();
                    ++rendered;
                    if (last) running = false;
                } else {
                    SDL_Delay(1);
                }

                const auto now = Clock::now();
                const std::chrono::duration<double> elapsed = now - lastTitle;
                if (elapsed.count() >= 0.5) {
                    const double fps = static_cast<double>(rendered - lastTitleFrames) / elapsed.count();
                    const int t = target.load(std::memory_order_relaxed);
                    const std::string title = "Proyecto Epsilon | " + std::to_string(static_cast<int>(fps + 0.5)) + " FPS | " + status.Get() +
                                              (t >= 0 ? " | Objetivo: SYS-" + std::to_string(t) : std::string());
                    SDL_SetWindowTitle(window, title.c_str());
                    lastTitle = now;
                    lastTitleFrames = rendered;
                }
            }

            stop.store(true, std::memory_order_release);
            simulation.join();

            const std::chrono::duration<double> total = Clock::now() - start;
            std::printf("Driver: %s\n", backend.DriverName().c_str());
            std::printf("Fotogramas dibujados: %llu  Presentados: %llu  Pasos de simulacion: %llu  Tiempo: %.2f s  Media: %.1f FPS\n",
                        static_cast<unsigned long long>(rendered), static_cast<unsigned long long>(backend.PresentedFrames()),
                        static_cast<unsigned long long>(steps.load()), total.count(), static_cast<double>(rendered) / total.count());
            std::printf("Esferas: %zu  Sprites: %zu  Lineas: %zu  Mallas: %zu  Nubes: %zu  Agujeros: %zu  Rocas calculadas: %zu  Modelos: %d  Texturas en GPU: %.1f MB\n",
                        lastSpheres, lastSprites, lastLines, lastMeshes, lastNebulae, lastBlackHoles, lastAsteroids, catalogModels,
                        static_cast<double>(backend.TextureBytes()) / (1024.0 * 1024.0));
            const StreamStats& loaded = world.stats;
            std::printf("Cargados: %zu de %zu sistemas  Orbitas calculadas: %zu de %zu  Objetos vivos: %zu de %zu  Cargas: %zu  Descargas: %zu\n",
                        loaded.loadedSystems, world.state.size(), loaded.activeOrbits, loaded.totalOrbits, loaded.liveRenderables, loaded.totalRenderables, loaded.loads, loaded.unloads);
            for (const ImpactReport& r : engine.Collisions().Reports())
                std::printf("Impacto: %s > %s: %s, %.2f km/s, Q/Q* = %.3g, energia %.3g J\n", r.attacker.c_str(), r.target.c_str(), astro::Name(r.result.outcome), r.result.speed,
                            r.result.threshold > 0.0 ? r.result.specificEnergy / r.result.threshold : 0.0, r.result.energy);
            std::printf("Estado: %s\n", status.Get().c_str());
            if (!backend.LastError().empty()) std::printf("Ultimo error: %s\n", backend.LastError().c_str());
            if (backend.PresentedFrames() == 0) exitCode = 1;
            backend.SetOverlay(nullptr);
            SDL_WaitForGPUIdle(backend.Device());
            hud.Shutdown();
            backend.Shutdown();
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}

}  // namespace

int main(int argc, char** argv) { return Run(ParseOptions(argc, argv)); }
