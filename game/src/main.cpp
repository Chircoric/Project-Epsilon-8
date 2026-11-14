// main.cpp - El juego en una ventana.
//
// Controles: raton = rumbo, W/S = mando, 1/2/3/4 = impulso, crucero, curvatura, hiper.
//            Tab = siguiente objetivo, F = ruta asistida, X = pararla, Espacio = frenar y mantenerse.
//            M = drones mineros, P = desplegar central, E = recoger o explorar, U = mejorar.
//            R = escaner. I = analisis del objetivo. O = orbita aqui. C = cursor. H = teclas. Enter = dialogo.
//            F1 = consola de comandos. Esc = pausa.
//
// Opciones:
//   --frames N         termina tras N fotogramas
//   --screenshot a.png guarda el ultimo fotograma; requiere --frames
//   --cmd "texto"      ejecuta un comando al empezar; se puede repetir
//   --no-vsync         dibuja sin esperar al monitor
//   --fixed-step       la partida avanza a pasos fijos de 1/60 s, como lo hara el servidor
//   --host [puerto]    abre la partida a la red: juegas tu y pueden entrar otros (puerto 27300 si no se dice)
//   --connect lan      busca partidas abiertas en la red local y entra en la primera
//   --connect ip[:puerto]  entra en la partida de otro. Con --join ship (por defecto) llevas una nave propia;
//                      con --join crew vas de tripulante en la del anfitrion. --name dice como te llamas
//   --direct           sin servidor: la interfaz lee la partida directamente, como antes de la fase 5. No se
//                      puede abrir la partida a la red ni entrar en otra
//   --loopback         (ya es lo normal) servidor y cliente en este mismo proceso: la partida la simula un servidor y lo que
//                      se ve es la replica de un cliente, unidos en memoria (docs/multijugador.md, fase 5)
//   --size W H         tamano de la ventana
//   --seed N           otra partida
//   --mode N           salta el menu: 0 pacifico, 1 facil, 2 normal, 3 dificil, 4 realista
//   --no-tutorial      sin dialogos
//   --cursor           empieza con el cursor activo
//   --click F X Y      clic simulado en el fotograma F, en esos pixeles; se puede repetir
//   --wheel, --atlas, --atlas-astro  empieza con la rueda, el mapa o la vista de un astro abiertos
//   --save a.sav       usa esta carpeta de partida (dentro van mundo.sav y jugadores/). Con --frames no se guarda salvo que se indique
//   --load             carga la partida al empezar, sin pasar por el menu
//   --quality N        calidad grafica: 0 baja, 1 media, 2 alta, 3 ultra. Sin ella, la guardada
//   --page N           pagina del menu: 0 principal, 1 nueva partida, 2 configuracion, 3 creditos, 4 graficos
//   --nave a.glb       usa este modelo glTF para la nave. Sin ella, modelos/nave.glb (o .gltf) junto
//                      al ejecutable si existe; si no, la maqueta hecha por codigo
//
// La configuracion se guarda en ajustes.txt, junto a la partida. Con --frames no se lee ni se escribe.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "epsilon/Engine.h"
#include "epsilon/SdlGpuBackend.h"
#include "game/Armament.h"
#include "game/Console.h"
#include "game/GameHud.h"
#include "game/Net.h"
#include "game/GameLayer.h"
#include "game/GameModels.h"
#include "game/UiSounds.h"
#include "game/Session.h"
#include "ps_reticle.h"
#include "ps_warp.h"

using namespace eps;
using namespace game;
using Clock = std::chrono::steady_clock;
using Kind = TargetRef::Kind;

namespace {

struct Options {
    int frames = 0, width = 1600, height = 900;
    std::uint32_t seed = 20261105;
    bool seedGiven = false, load = false, analysis = false;
    bool wheel = false, atlas = false, atlasAstro = false, atlasTable = false;  // empezar con la rueda o el mapa abiertos
    std::string savePath;
    std::string screenshot;
    std::string shipModel;         // .glb o .gltf de la nave; vacio = buscarlo junto al ejecutable
    std::vector<std::string> commands;
    bool vsync = true, debug = false, noTutorial = false, cursor = false, fixedStep = false, loopback = false;
    bool direct = false;         // --direct: sin servidor ni cliente
    int hostPort = 0;            // > 0: la partida se abre a la red en ese puerto
    std::string connect;         // ip[:puerto] del servidor al que entrar
    std::string playerName;      // --name
    bool joinCrew = false;       // --join crew
    int mode = -1;                 // -1 = menu
    int quality = -1;              // -1 = la guardada; si no, GraphicsQuality
    int page = 0;                  // pagina del menu con la que empezar: MenuPage
    int ui = -1;                   // --interfaz: 0 minimalista, 1 experta; -1 = la guardada
    int uiScale = 0;               // --tamano: % de la interfaz; 0 = el guardado
    struct Click {
        int frame = -1;
        float x = 0, y = 0;
    };
    std::vector<Click> clicks;     // clics simulados; puede haber varios
    struct TimedCommand {
        int frame = 0;
        std::string text;
    };
    std::vector<TimedCommand> timedCommands;  // --cmd-at F "orden": se ejecuta en el fotograma F
};

Options ParseOptions(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) o.frames = std::atoi(argv[++i]);
        else if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
        else if (a == "--cmd" && i + 1 < argc) o.commands.push_back(argv[++i]);
        else if (a == "--cmd-at" && i + 2 < argc) {
            Options::TimedCommand c;
            c.frame = std::atoi(argv[++i]);
            c.text = argv[++i];
            o.timedCommands.push_back(c);
        }
        else if (a == "--seed" && i + 1 < argc) {
            o.seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            o.seedGiven = true;
        }
        else if (a == "--save" && i + 1 < argc) o.savePath = argv[++i];
        else if (a == "--nave" && i + 1 < argc) o.shipModel = argv[++i];
        else if (a == "--load") o.load = true;
        else if (a == "--analysis") o.analysis = true;
        else if (a == "--wheel") o.wheel = true;
        else if (a == "--atlas") o.atlas = true;
        else if (a == "--atlas-astro") o.atlas = o.atlasAstro = true;
        else if (a == "--atlas-tabla") o.atlas = o.atlasTable = true;
        else if (a == "--no-vsync") o.vsync = false;
        else if (a == "--fixed-step") o.fixedStep = true;
        else if (a == "--loopback") o.loopback = true;
        else if (a == "--direct") o.direct = true;
        else if (a == "--host") {
            o.hostPort = kDefaultPort;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') o.hostPort = std::atoi(argv[++i]);
            o.loopback = true;  // quien abre la partida juega por el mismo camino que los demas
        } else if (a == "--connect" && i + 1 < argc) o.connect = argv[++i];
        else if (a == "--name" && i + 1 < argc) o.playerName = argv[++i];
        else if (a == "--join" && i + 1 < argc) o.joinCrew = std::string(argv[++i]) == "crew";
        else if (a == "--mode" && i + 1 < argc) o.mode = std::atoi(argv[++i]);
        else if (a == "--quality" && i + 1 < argc) o.quality = std::atoi(argv[++i]);
        else if (a == "--page" && i + 1 < argc) o.page = std::atoi(argv[++i]);
        else if (a == "--interfaz" && i + 1 < argc) {
            const std::string v = argv[++i];
            o.ui = v == "minimalista" || v == "0" ? 0 : v == "experta" || v == "experto" || v == "1" ? 1 : -1;
        }
        else if (a == "--tamano" && i + 1 < argc) o.uiScale = std::atoi(argv[++i]);
        else if (a == "--no-tutorial") o.noTutorial = true;
        else if (a == "--cursor") o.cursor = true;
        else if (a == "--click" && i + 3 < argc) {
            Options::Click c;
            c.frame = std::atoi(argv[++i]);
            c.x = static_cast<float>(std::atof(argv[++i]));
            c.y = static_cast<float>(std::atof(argv[++i]));
            o.clicks.push_back(c);
        }
        else if (a == "--debug") o.debug = true;
        else if (a == "--size" && i + 2 < argc) {
            o.width = std::atoi(argv[++i]);
            o.height = std::atoi(argv[++i]);
        }
    }
    return o;
}

TextureId MakeHullTexture(SdlGpuBackend& backend) {
    constexpr int kSize = 256;
    std::vector<std::uint32_t> pixels(kSize * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const bool seam = x % 64 < 2 || y % 32 < 2;
            const std::uint32_t grain = ((static_cast<std::uint32_t>(x / 8) * 73u ^ static_cast<std::uint32_t>(y / 4) * 151u) * 2654435761u) >> 27;
            const std::uint32_t level = seam ? 120 : 205 + grain;
            pixels[static_cast<std::size_t>(y) * kSize + x] = 0xFF000000u | level << 16 | level << 8 | level;
        }
    }
    return backend.CreateTexture(pixels.data(), kSize, kSize);
}


// Sonido de un clic segun lo que hace: abrir un panel suena a panel, confirmar un cambio en la
// nave o en la partida suena a confirmacion, y el resto, a clic.
UiSound SoundFor(HudActionType type) {
    switch (type) {
        case HudActionType::ToggleAtlas:
        case HudActionType::ToggleWorkshop:
        case HudActionType::ToggleKeys:
        case HudActionType::MenuPage:
        case HudActionType::Analyze:
            return UiSound::Panel;
        case HudActionType::Dock:
        case HudActionType::Undock:
        case HudActionType::Board:
        case HudActionType::Disembark:
        case HudActionType::Orbit:
        case HudActionType::StartMode:
        case HudActionType::SaveGame:
        case HudActionType::LoadGame:
        case HudActionType::Continue:
            return UiSound::Confirm;
        default:
            return UiSound::Click;
    }
}

// Resumen de una partida para el menu.
std::string SaveSummary(const SaveData& d) {
    char text[120];
    const ShipSave* ship = d.Ship();
    std::snprintf(text, sizeof(text), "%s   Dia %.1f   %zu estaciones   %d emisores", ModeInfo(d.world.mode).name, d.world.simTime / 86400.0, d.world.stations.size(), ship ? ship->ship.emitters : 0);
    return text;
}

// Un identificador nuevo para este jugador: 16 cifras hexadecimales al azar.
std::string NewPlayerId() {
    std::random_device device;
    char text[20];
    std::snprintf(text, sizeof(text), "%08x%08x", static_cast<unsigned>(device()), static_cast<unsigned>(device()));
    return text;
}

int Run(Options options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    // Las partidas viven en la carpeta de datos del usuario, no junto al ejecutable.
    // Una partida es una carpeta: mundo.sav y jugadores/<id>.sav (SaveGame.h).
    const bool joining = !options.connect.empty();  // se entra en la partida de otro: la guarda su servidor
    if (joining && options.mode < 0) options.mode = 2;  // sin menu: el modo es el del servidor
    // Lo normal: la partida la lleva un servidor en este mismo proceso y lo que se ve es su replica. Asi
    // se puede abrir a la red o entrar en la de otro desde el menu, con el juego en marcha.
    if (!options.direct && !joining) options.loopback = true;
    std::string savePath = joining ? std::string() : options.savePath;
    std::string settingsPath, legacySave;
    std::string playerId = "local";
    if (options.frames == 0) {
        if (char* folder = SDL_GetPrefPath("Epsilon", "Nebulosa")) {
            if (savePath.empty() && !joining) {
                savePath = std::string(folder) + "partida";
                legacySave = std::string(folder) + "partida.sav";  // hasta la version 12, un solo archivo
            }
            settingsPath = std::string(folder) + "ajustes.txt";
            SDL_free(folder);
        }
    }
    // El identificador del jugador se crea una vez y se queda en ajustes.txt.
    if (!settingsPath.empty()) {
        GameSettings stored = LoadSettings(settingsPath);
        if (stored.playerId.empty()) {
            stored.playerId = NewPlayerId();
            SaveSettings(settingsPath, stored);
        }
        playerId = stored.playerId;
    }
    // Con --frames no hay identificador guardado: el nombre hace de el, para poder probar con varios.
    if (playerId == "local" && !options.playerName.empty()) playerId = SafePlayerId(options.playerName);
    const std::string playerName = options.playerName.empty() ? "Piloto" : options.playerName;
    std::optional<SaveData> saved;
    std::string saveProblem;
    if (!savePath.empty()) {
        std::string problem;
        saved = LoadGame(savePath, playerId, &problem);
        if (!saved && problem != "sin partida guardada") {
            std::printf("Aviso: no se pudo leer la partida: %s\n", problem.c_str());
            saveProblem = problem;  // el menu lo ensena
        } else if (!saved && !legacySave.empty() && std::filesystem::exists(FilePath(legacySave))) {
            saveProblem = "es de una versión anterior, de un solo archivo. Ya no se puede cargar: empiece una misión nueva.";
        }
    }
    // La galaxia sale de la semilla: tiene que ser la de la partida guardada.
    if (saved && !options.seedGiven) options.seed = saved->world.seed;
    if (saved && saved->world.seed != options.seed) saved.reset();
    SDL_Window* window = SDL_CreateWindow("Nebulosa", options.width, options.height, SDL_WINDOW_RESIZABLE);
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
            Balance rules;  // la eslora cambia si la nave viene de un .glb
            const Color fog = NebulaColor(0.3f);

            EngineConfig config;
            config.galaxy.seed = options.seed;
            config.galaxy.systems = rules.world.systems;
            config.galaxy.starSpacing = rules.world.starSpacing;
            config.galaxy.streaming = true;
            config.galaxy.blackHoles = true;
            config.galaxy.nebulae = 0;
            config.clearColor = fog;
            config.post.starBoost = 0.95f;  // de cerca hay que poder ver el plasma, no un disco blanco
            config.asteroids.dot = SdlGpuBackend::kGlowTexture;
            for (std::uint32_t seed = 1; seed <= 6; ++seed) config.asteroids.meshes.push_back(backend.CreateMesh(MakeAsteroid(seed * 37u)));

            AstroArt art;
            art.halo = SdlGpuBackend::kGlowTexture;
            art.plainStar = art.plainPlanet = SdlGpuBackend::kGlowTexture;
            art.asteroidMeshes = config.asteroids.meshes;

            Engine engine(config, art);
            engine.Commands().Execute("desactivar cielo orbitas colisiones grano");

            // La nave se carga antes que la partida: su forma decide las zonas del casco y la eslora.
            const MeshUpload upload = [&](const MeshData& m) { return backend.CreateMesh(m); };
            GameAssets assets = BuildGameAssets(upload, MakeHullTexture(backend));
            RegisterAlienZones(assets);  // los alienigenas, con su casco por zonas
            {
                // La nave de un .glb, si lo hay: --nave o modelos/nave.glb junto al ejecutable.
                const std::string folder = SDL_GetBasePath() ? std::string(SDL_GetBasePath()) + "modelos/" : "modelos/";
                std::string path = options.shipModel;
                if (path.empty()) {
                    std::error_code ignored;
                    for (const char* name : {"nave.glb", "nave.gltf"})
                        if (std::filesystem::exists(folder + name, ignored)) {
                            path = folder + name;
                            break;
                        }
                }
                std::vector<ImportedMarker> markers;
                // Cada imagen se sube una vez, aunque la usen veinte piezas y varios modelos. Si junto a
                // "x_color.png" hay "x_normal.png" y "x_orm.png", son su relieve y su rugosidad y metal.
                std::map<std::string, TextureId> loaded;
                const auto texture = [&](const std::string& file) {
                    const std::filesystem::path p(file);
                    const std::string full = p.is_absolute() || path.empty() ? file : (std::filesystem::path(path).parent_path() / p).string();
                    if (const auto found = loaded.find(full); found != loaded.end()) return found->second;
                    const TextureId id = backend.LoadTexture(full, 2048);
                    loaded[full] = id;
                    const std::string suffix = "_color.png";
                    if (id != kNoTexture && full.size() > suffix.size() && full.compare(full.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        const std::string stem = full.substr(0, full.size() - suffix.size());
                        std::error_code ignored;
                        const auto map = [&](const char* name) { return std::filesystem::exists(stem + name, ignored) ? backend.LoadTexture(stem + name, 2048) : kNoTexture; };
                        backend.SetTextureMaps(id, map("_normal.png"), map("_orm.png"));
                    }
                    return id;
                };
                // La estacion origami (con su animacion), la bahia de drones y el dron.
                {
                    std::string log;
                    LoadStationAndDrones(assets, folder, upload, texture, &log);
                    if (!log.empty()) std::printf("%s", log.c_str());
                    if (assets.origami) std::printf("Estacion origami: %zu piezas%s\n", assets.origami->parts.size(), assets.origamiDeploy ? ", con su despliegue" : "");
                }
                // Naves alienigenas por clase y torretas humanas; sus zonas cambian con el modelo.
                {
                    std::string log;
                    LoadCombatModels(assets, folder, upload, texture, &log);
                    if (!log.empty()) std::printf("%s", log.c_str());
                    RegisterAlienZones(assets);
                }
                if (!path.empty()) {
                    std::string error;
                    if (ModelRef custom = LoadShipModel(path, upload, texture, &error)) {
                        assets.ship = custom;
                        assets.customShip = true;
                        // Las zonas toman la forma de sus piezas, y la eslora y el ancho, sus medidas.
                        const std::shared_ptr<ZoneLayout> zones = BuildZoneLayout(*custom);
                        SetZoneLayout(zones);
                        rules.ship.bow = zones->extent.z * 0.001;
                        rules.ship.beam = std::max(zones->extent.x, zones->extent.y) * 0.001;
                        std::printf("Nave cargada de %s: %zu piezas, %.0f m de eslora\n", path.c_str(), custom->parts.size(), zones->extent.z * 2.0);
                        // Los nodos vacios "hp_..." son las ranuras exteriores de los modulos.
                        if (!LoadGltfMarkers(path, markers, &error)) std::printf("Aviso: sin anclajes de modulos (%s)\n", error.c_str());
                    } else {
                        std::printf("Aviso: no se pudo cargar la nave de %s (%s); se usa la maqueta.\n", path.c_str(), error.c_str());
                    }
                }
                // Ranuras y piezas de la nave que se va a usar, sea el .glb o la maqueta.
                const std::shared_ptr<ShipDesign> design = BuildShipDesign(*assets.ship, markers);
                SetShipDesign(design);
                std::size_t exterior = 0;
                for (const ModuleSlot& s : design->slots) exterior += s.kind == SlotKind::External ? 1 : 0;
                std::printf("Modulos: %zu ranuras exteriores, %zu bahias interiores, %zu piezas\n", exterior, design->slots.size() - exterior, design->pieces.size());
            }
            GameSession session(engine, rules, options.seed);
            session.SetPlayer(playerId, "Piloto");
            session.RefitToDesign();
            // Con --loopback la partida de verdad la lleva un servidor, con su propio motor, y `session`
            // pasa a ser la replica de un cliente: la interfaz y el dibujo la leen igual, y sus ordenes y su
            // mando viajan al servidor. `authority` es siempre la sesion que manda: la del servidor, o la
            // unica que hay. Lo que cambia la partida sin ser una orden (trucos, cargar, guardar) va a ella.
            std::unique_ptr<Engine> serverEngine;
            std::unique_ptr<GameSession> serverSession;
            if (options.loopback && !joining) {
                serverEngine = std::make_unique<Engine>(config, art);
                serverEngine->Commands().Execute("desactivar cielo orbitas colisiones grano");
                serverSession = std::make_unique<GameSession>(*serverEngine, rules, options.seed);
                serverSession->SetPlayer(playerId, playerName);
                serverSession->RefitToDesign();
            }
            GameSession& authority = serverSession ? *serverSession : session;
            GameLayer layer(session, assets, SdlGpuBackend::kGlowTexture, SdlGpuBackend::kRingTexture, fog);
            engine.AddLayer(&layer);
            // Una nave que sale del sistema donde hay otras se lleva su propia burbuja, con su mundo
            // cargado: la sesion pide aqui un motor mas, igual que este (docs/multijugador.md, fase 4).
            authority.SetEngineFactory([&]() {
                auto another = std::make_unique<Engine>(config, art);
                another->Commands().Execute("desactivar cielo orbitas colisiones grano");
                another->AddLayer(&layer);
                return another;
            });
            std::unique_ptr<GameServer> host;
            std::unique_ptr<GameClient> client;
            std::shared_ptr<Transport> localServerEnd, clientLink;  // el lado del servidor de la conexion en memoria, y la conexion que usa ahora el cliente
            bool remote = false;                       // desde el menu, se ha entrado en la partida de otro
            std::string remoteName;
            std::future<std::vector<LanGame>> search;  // la busqueda en la red local, en marcha
            Hello hello;
            hello.id = playerId;
            hello.name = playerName;
            if (joining) {
                // Entrar en la partida de otro: esta sesion es solo la replica; no hay servidor aqui.
                std::string address = options.connect;
                int port = kDefaultPort;
                if (const std::size_t colon = address.rfind(':'); colon != std::string::npos) {
                    port = std::atoi(address.c_str() + colon + 1);
                    address = address.substr(0, colon);
                }
                // "lan": se pregunta en la red local y se entra en la primera partida que conteste.
                if (address == "lan") {
                    const std::vector<LanGame> games = FindLanGames(1.5);
                    for (const LanGame& g : games)
                        std::printf("Partida en la red local: \"%s\" en %s:%d, %d de %d jugadores, modo %s\n", g.name.c_str(), g.address.c_str(), g.port, g.players, g.maxPlayers,
                                    ModeInfo(static_cast<GameModeId>(std::clamp(g.mode, 0, 4))).name);
                    if (games.empty()) {
                        std::printf("No hay ninguna partida abierta en la red local.\n");
                        return 1;
                    }
                    address = games.front().address;
                    port = games.front().port;
                }
                std::string problem;
                std::shared_ptr<Transport> link = ConnectTo(address, port, 5.0, &problem);
                if (!link) {
                    std::printf("No se pudo entrar en %s:%d: %s\n", address.c_str(), port, problem.c_str());
                    return 1;
                }
                hello.join = options.joinCrew ? JoinAs::Crew : JoinAs::OwnShip;
                clientLink = link;
                client = std::make_unique<GameClient>(session, link, hello);
                // Hasta que llega el mundo no hay nada que dibujar: se espera un momento.
                for (int i = 0; i < 300 && !client->Ready() && !client->Refused(); ++i) {
                    client->Update();
                    SDL_Delay(10);
                }
                if (!client->Ready()) {
                    std::printf("El servidor no dejó entrar: %s\n", client->Refused() ? client->Refusal().c_str() : "no responde");
                    return 1;
                }
                std::printf("Conectado a %s:%d como %s, en la nave %d\n", address.c_str(), port, playerName.c_str(), session.VesselId());
            } else if (options.loopback) {
                host = std::make_unique<GameServer>(authority);
                auto link = MakeMemoryLink();
                host->Add(link.first);
                localServerEnd = link.first;
                clientLink = link.second;
                client = std::make_unique<GameClient>(session, link.second, hello);
                host->Update(0.0);
                client->Update();
                if (!client->Ready()) std::printf("Aviso: el servidor en memoria no responde\n");
                // Quien entre por la red recupera lo suyo de la carpeta de la partida, y se guarda con los demas.
                if (!savePath.empty()) host->SetSaveFolder(savePath);
                if (options.hostPort > 0) {
                    std::string problem;
                    if (host->OpenToNetwork(options.hostPort, "Partida de " + playerName, 8, &problem)) {
                        std::printf("Partida abierta a la red en el puerto %d, anunciada en la red local\n", options.hostPort);
                    } else {
                        std::printf("Aviso: no se pudo abrir la partida a la red: %s\n", problem.c_str());
                    }
                }
            }

            TargetRef target;
            Quatd viewOrbit;  // camara respecto a la nave, sin limites
            Quatd smoothed;   // orientacion que sigue la camara
            bool cameraReady = false;
            Vec3d warpUp{0, 1, 0};  // eje de las lineas de la curvatura
            // La camara se aparta en proporcion a la nave: la maqueta mide 0,55 km de proa al centro.
            const double shipScale = std::max(rules.ship.bow / 0.55, 1.0);
            double viewDistance = 2.4 * shipScale;
            // Lo mas cerca que puede ponerse la camara sin entrar en el casco, por donde mira ahora (km).
            double hullFloor = 0.0;
            Vec3f hullLook{0, 0, 0};  // direccion en que se midio, en el modelo
            RegisterGameCommands(engine.Commands(), authority, target);  // la consola actua sobre la partida de verdad (game/Console.cpp)
            engine.Commands().Add("vista", "camara: vista <grados> [km] [grados de elevacion; positivos, desde abajo]", [&](const CommandRegistry::Args& args) -> std::string {
                if (!args.empty()) viewOrbit = Quatd::FromAxisAngle({0, 1, 0}, -std::atof(args[0].c_str()) * kPi / 180.0);
                if (args.size() >= 3) viewOrbit = viewOrbit * Quatd::FromAxisAngle({1, 0, 0}, std::atof(args[2].c_str()) * kPi / 180.0);
                if (args.size() >= 2) viewDistance = std::atof(args[1].c_str());
                return "Hecho";
            });

            engine.Commands().Add("girar", "gira la nave de golpe, para probar: girar <derecha> <abajo> en grados", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.size() < 2) return engine.Commands().Fail("Uso: girar <derecha> <abajo>");
                ShipState& ship = authority.Ship();
                ship.orientation = (ship.orientation * Quatd::FromAxisAngle({0, 1, 0}, std::atof(args[0].c_str()) * kPi / 180.0) * Quatd::FromAxisAngle({1, 0, 0}, -std::atof(args[1].c_str()) * kPi / 180.0)).Normalized();
                ship.aim = ship.orientation;
                ship.moveDirection = ship.warpHeading = Heading(ship);
                return "Hecho";
            });

            engine.Commands().Add("virar", "manda girar la nave, que gira sola con sus propulsores de maniobra: virar <derecha> <abajo> en grados", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.size() < 2) return engine.Commands().Fail("Uso: virar <derecha> <abajo>");
                ShipState& ship = authority.Ship();
                ship.aim = (ship.orientation * Quatd::FromAxisAngle({0, 1, 0}, std::atof(args[0].c_str()) * kPi / 180.0) * Quatd::FromAxisAngle({1, 0, 0}, -std::atof(args[1].c_str()) * kPi / 180.0)).Normalized();
                return "Virando";
            });

            GameHud hud;
            std::string console;
            bool consoleOpen = false;
            Camera camera;
            const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
            const bool hudReady = hud.Initialize(backend, base + "fonts");
            if (!hudReady) std::printf("Aviso: no se pudo iniciar la interfaz.\n");
            if (hudReady) backend.SetOverlay(&hud);
            // Sonidos de la interfaz. Sin dispositivo de audio, el juego sigue en silencio.
            UiSounds sounds;
            if (!sounds.Initialize(base + "audio/ui")) std::printf("Aviso: sin sonidos de interfaz.\n");

            const int warpEffect = backend.AddPostEffect("curvatura", gPsWarp, sizeof(gPsWarp), true);
            if (warpEffect < 0) std::printf("Aviso: %s\n", backend.LastError().c_str());
            const int reticleEffect = backend.AddPostEffect("reticula", gPsReticle, sizeof(gPsReticle));
            if (reticleEffect < 0) std::printf("Aviso: %s\n", backend.LastError().c_str());

            HudInput ui;
            engine.Commands().Add("modulo", "monta un modulo (atracada): modulo <ranura> <tipo>; sin nada, lista ranuras y tipos", [&](const CommandRegistry::Args& args) -> std::string {
                const ShipDesign& design = DesignOf(session.Ship());
                if (args.size() < 2) {
                    std::string list = "Tipos:";
                    for (int k = 0; k < static_cast<int>(ShipModuleKind::Count); ++k) list += " " + std::to_string(k) + "=" + ModuleSpec(static_cast<ShipModuleKind>(k)).name;
                    list += ". Ranuras:";
                    for (std::size_t i = 0; i < design.slots.size(); ++i)
                        list += " " + std::to_string(i) + "=" + PrettyName(design.slots[i].name) + (i < session.Ship().loadout.modules.size() && session.Ship().loadout.modules[i] != ShipModuleKind::None ? std::string(" [") + ModuleSpec(session.Ship().loadout.modules[i]).name + "]" : "");
                    return list;
                }
                const CommandResult done = session.Apply(Order(CommandType::InstallModule, std::atoi(args[1].c_str()), std::atoi(args[0].c_str())));
                return done ? "Hecho" : done.why;
            });
            engine.Commands().Add("mover", "mueve una bahia de drones de serie a otro hueco exterior (atracada): mover <de> <a>", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.size() < 2) return "Uso: mover <ranura origen> <ranura destino>";
                const CommandResult done = session.Apply(Order(CommandType::MoveModule, std::atoi(args[0].c_str()), std::atoi(args[1].c_str())));
                return done ? "Hecho" : done.why;
            });
            engine.Commands().Add("plano", "fabrica el plano de un arma humana (atracada en la origami): plano <0.." + std::to_string(kBlueprintCount - 1) + ">; sin nada, lista los planos", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.empty()) {
                    std::string list;
                    for (int id = 0; id < kBlueprintCount; ++id) list += std::to_string(id) + "=" + BlueprintName(id).substr(7) + (HasBlueprint(session.Ship().loadout, id) ? " [hecho]" : "") + (id + 1 < kBlueprintCount ? ", " : "");
                    return list;
                }
                const CommandResult done = session.Apply(Order(CommandType::Blueprint, std::atoi(args[0].c_str())));
                return done ? "Hecho" : done.why;
            });
            engine.Commands().Add("misiles", "fabrica misiles para una bahia (atracada): misiles <ranura> [cantidad]; sin cantidad, hasta llenarla", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.empty()) return "Uso: misiles <ranura> [cantidad]";
                const CommandResult made = session.Apply(Order(CommandType::Missiles, std::atoi(args[0].c_str()), args.size() > 1 ? std::atoi(args[1].c_str()) : 0));
                return made ? std::to_string(made.count) + " fabricados" : made.why;
            });
            engine.Commands().Add("puntual", "monta puntos de defensa puntual (atracada): puntual <n>; sin nada, dice cuantos hay", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.empty()) return std::to_string(session.Ship().loadout.pointDefense) + " de " + std::to_string(session.PointDefenseCapacity()) + " puntos";
                const CommandResult done = session.Apply(Order(CommandType::PointDefense, std::atoi(args[0].c_str())));
                return done ? "Hecho" : done.why;
            });
            engine.Commands().Add("impulsor","cambia el motor de impulso (atracada): impulsor <1.." + std::to_string(kImpulseEngineCount) + ">", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.empty()) return std::string("Motor: ") + ImpulseEngine(session.Ship().loadout.engine).name;
                const CommandResult done = session.Apply(Order(CommandType::ImpulseEngine, std::atoi(args[0].c_str())));
                return done ? "Hecho" : done.why;
            });
            engine.Commands().Add("taller", "abre o cierra el taller de modulos (atracada)", [&](const CommandRegistry::Args&) -> std::string {
                ui.workshop = !ui.workshop && session.CanRefit();
                if (ui.workshop) ui.cursor = true;
                return ui.workshop ? "Taller abierto" : (session.CanRefit() ? "Taller cerrado" : "Hay que estar atracada en una estacion propia");
            });
            engine.Commands().Add("trayectoria", "muestra u oculta la linea de la ruta que lleva la nave, como en la rueda", [&](const CommandRegistry::Args&) -> std::string {
                ui.trajectory = !ui.trajectory;
                return ui.trajectory ? "Trayectoria visible" : "Trayectoria oculta";
            });
            engine.Commands().Add("radar", "alcance del radar: radar <0.." + std::to_string(kRadarRangeCount - 1) + ">", [&](const CommandRegistry::Args& args) -> std::string {
                if (!args.empty()) ui.radarRange = std::clamp(std::atoi(args[0].c_str()), 0, kRadarRangeCount - 1);
                return std::to_string(ui.radarRange);
            });
            engine.Commands().Add("dano", "golpea una zona del casco como un impacto sin escudos: dano <zona 0.." + std::to_string(kHullZoneCount - 1) + "> <casco> [x y z de donde llega]; sin nada, lista las zonas", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.size() < 2) {
                    std::string list;
                    for (int z = 0; z < kHullZoneCount; ++z)
                        list += std::to_string(z) + " " + ZoneInfo(z).name + " " + std::to_string(static_cast<int>(session.Ship().zones[static_cast<std::size_t>(z)] * 100.0)) + "%" + (z + 1 < kHullZoneCount ? ", " : "");
                    return list;
                }
                const int zone = std::atoi(args[0].c_str());
                if (zone < 0 || zone >= kHullZoneCount) return "Zona desconocida";
                // Como un impacto sin escudos: llega desde fuera de la zona (o desde x y z, en el modelo).
                Vec3d from = ZoneInfo(zone).center;
                if (args.size() >= 5) from = {std::atof(args[2].c_str()), std::atof(args[3].c_str()), std::atof(args[4].c_str())};
                if (Length(from) < 1.0) from = {0, -1, 0};
                from = Normalize(from);
                authority.DamageZone(zone, std::atof(args[1].c_str()), &from);
                return std::string(ZoneInfo(zone).name) + ": " + std::to_string(static_cast<int>(session.Ship().zones[static_cast<std::size_t>(zone)] * 100.0)) + "%";
            });
            engine.Commands().Add("disparo", "proyectil sin escudos que sigue su trayectoria: disparo <dx dy dz> <px py pz> [casco]; llega desde d y pasa por p (m, en el modelo)", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.size() < 6) return "Uso: disparo dx dy dz px py pz [casco]. Ej.: disparo 0 0 1 -150 0 -380 60 (de frente, a la gondola de babor)";
                const Vec3d d = Normalize(Vec3d{std::atof(args[0].c_str()), std::atof(args[1].c_str()), std::atof(args[2].c_str())} + Vec3d{0, 0, 1e-9});
                const Vec3d p{std::atof(args[3].c_str()), std::atof(args[4].c_str()), std::atof(args[5].c_str())};
                const double amount = args.size() >= 7 ? std::atof(args[6].c_str()) : 40.0;
                const int zone = authority.ShootHull(p + d * 2000.0, p - d * 2000.0, amount);
                return std::string("Da en: ") + ZoneInfo(zone).name;
            });
            // El alienigena mas cercano: el objetivo si es uno, y si no el que este mas cerca de la nave.
            const auto nearestAlien = [&]() -> Anomaly* {
                Anomaly* best = nullptr;
                double closest = 1e300;
                for (Anomaly& a : authority.Anomalies()) {
                    if (a.hull <= 0.0 || !ZonesOf(a)) continue;
                    if (target.kind == TargetRef::Kind::Anomaly && target.index == a.id) return &a;
                    const double d = Length(session.AnomalyPosition(a) - session.Ship().position);
                    if (d < closest) {
                        closest = d;
                        best = &a;
                    }
                }
                return best;
            };
            engine.Commands().Add("danar_enemigo", "golpe del jugador, sin escudos, en una zona del alienigena mas cercano: danar_enemigo <zona> <% de su casco>; sin nada, lista sus zonas", [&](const CommandRegistry::Args& args) -> std::string {
                Anomaly* a = nearestAlien();
                if (!a) return engine.Commands().Fail("No hay ningun alienigena con zonas cerca. Prueba: aparecer hostil 3");
                const ZoneLayout* layout = ZonesOf(*a);
                if (args.size() < 2) {
                    std::string list = a->name + ":";
                    for (int z = 0; z < kHullZoneCount; ++z)
                        if (ZonePresent(*layout, z)) list += " " + std::to_string(z) + " " + ZoneLabel(layout, z) + " " + std::to_string(static_cast<int>(a->zones[static_cast<std::size_t>(z)] * 100.0)) + "%";
                    return list;
                }
                const int zone = std::atoi(args[0].c_str());
                if (zone < 0 || zone >= kHullZoneCount) return engine.Commands().Fail("Zona desconocida");
                const int id = a->id;
                authority.DamageAnomalyZone(id, zone, a->maxHull * std::atof(args[1].c_str()) / 100.0);
                const Anomaly* after = nullptr;
                for (const Anomaly& b : authority.Anomalies())
                    if (b.id == id) after = &b;
                if (!after || after->hull <= 0.0) return "Destruida";
                return ZoneLabel(ZonesOf(*after), zone) + ": " + std::to_string(static_cast<int>(after->zones[static_cast<std::size_t>(zone)] * 100.0)) + "%, casco " + std::to_string(static_cast<int>(after->hull / after->maxHull * 100.0)) + "%" + (after->disabled ? ", apagada" : "");
            });
            engine.Commands().Add("interfaz", "cambia la interfaz: interfaz minimalista|experta [tamano 100..150]", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.empty()) return ui.settings.Minimal() ? "Minimalista" : "Experta";
                if (args[0] == "minimalista") ui.settings.uiStyle = UiStyle::Minimal;
                else if (args[0] == "experta" || args[0] == "experto") ui.settings.uiStyle = UiStyle::Expert;
                else return "Uso: interfaz minimalista|experta [tamano]";
                if (args.size() >= 2) ui.settings.uiScale = std::clamp(std::atoi(args[1].c_str()), kUiScales[0], kUiScales[kUiScaleCount - 1]);
                if (!settingsPath.empty()) SaveSettings(settingsPath, ui.settings);
                return ui.settings.Minimal() ? "Interfaz minimalista" : "Interfaz experta";
            });
            engine.Commands().Add("plegar", "pliega o despliega una ventana: plegar <0.." + std::to_string(static_cast<int>(HudPanel::Count) - 1) + ">", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.empty()) return "Uso: plegar <ventana>. 0 nave, 1 bodega, 2 sensores, 3 objetivo, 4 radar, 5 estructura, 6 soles, 7 nave (mín.), 8 señales (mín.), 9 objetivo (mín.), 10 detalles (mín.)";
                const int p = std::atoi(args[0].c_str());
                if (p < 0 || p >= static_cast<int>(HudPanel::Count)) return "Ventana desconocida";
                ui.settings.folded ^= 1u << p;
                return (ui.settings.folded >> p) & 1u ? "Plegada" : "Desplegada";
            });
            ui.screen = options.mode >= 0 ? HudScreen::Playing : HudScreen::Menu;
            if (options.mode >= 0) authority.SetMode(static_cast<GameModeId>(options.mode));
            ui.console = &console;
            if (saved) ui.savedGame = SaveSummary(*saved);
            ui.saveProblem = saveProblem;
            ui.analysis = options.analysis;
            bool playing = ui.screen == HudScreen::Playing;
            if (options.noTutorial) authority.Conversation().Stop();
            bool cursor = ui.screen == HudScreen::Menu || options.cursor;
            ui.wheel = options.wheel;
            ui.atlas.open = options.atlas;
            ui.atlas.astro = options.atlasAstro;
            ui.atlas.table = options.atlasTable;
            if (options.atlas) ui.atlas.selected = TargetRef{Kind::Star, 0, 0};

            session.Step(1.0 / 60.0, {});
            // En loopback ese paso lo da la replica, que no simula: el servidor da el suyo, para que los
            // comandos de arranque encuentren el sistema cargado igual que sin red.
            if (host) {
                authority.Step(1.0 / 60.0, {});
                host->SendSnapshots();
                client->Update();
            }
            // Con --load los comandos corren despues de cargar la partida: si no, la carga los pisaria.
            const auto runCommands = [&] {
                for (const std::string& line : options.commands) std::printf("> %s\n%s\n", line.c_str(), engine.Commands().Execute(line).c_str());
                if (options.atlas && target.kind != Kind::None) {
                    ui.atlas.selected = target;  // para ver un astro concreto desde la linea de ordenes
                    ui.atlas.system = target.system;
                }
            };
            if (!options.load) runCommands();

            // La configuracion: cada calidad cambia la resolucion de la escena, el bloom, el grano
            // y el detalle de las superficies. Se parte de lo que el motor trae de serie.
            if (!settingsPath.empty()) ui.settings = LoadSettings(settingsPath);
            if (options.quality >= 0 && options.quality < static_cast<int>(GraphicsQuality::Count)) {
                ui.settings.quality = static_cast<GraphicsQuality>(options.quality);
                ui.settings.graphics = OptionsOf(ui.settings.quality);
            }
            if (options.ui == 0) ui.settings.uiStyle = UiStyle::Minimal;
            else if (options.ui == 1) ui.settings.uiStyle = UiStyle::Expert;
            if (options.uiScale > 0) ui.settings.uiScale = std::clamp(options.uiScale, kUiScales[0], kUiScales[kUiScaleCount - 1]);
            ui.page = static_cast<MenuPage>(std::clamp(options.page, 0, static_cast<int>(MenuPage::Network)));
            // La primera vez se elige la interfaz antes que nada.
            if (ui.screen == HudScreen::Menu && ui.settings.uiStyle == UiStyle::Unset && ui.page == MenuPage::Main) ui.page = MenuPage::Interface;
            const PostSettings basePost = engine.Post();
            const LodSettings baseLod = engine.Lod();
            const auto applySettings = [&] {
                // Cada opcion por separado (la pestaña de graficos); una calidad solo las rellena.
                const GraphicsOptions& g = ui.settings.graphics;
                const QualityProfile& surfaces = Profile(static_cast<GraphicsQuality>(std::clamp(g.surfaces, 0, static_cast<int>(GraphicsQuality::Count) - 1)));
                backend.SetRenderScale(static_cast<float>(g.renderScale) / 100.0f);
                backend.SetShadows(g.shadows);
                engine.Post().bloom = basePost.bloom && g.bloom;
                engine.Post().grain = g.film ? basePost.grain : 0.0f;
                engine.Post().aberration = g.film ? basePost.aberration : 0.0f;
                engine.Lod().maxOctaves = surfaces.maxOctaves;
                engine.Post().detail = std::clamp(g.lighting, 0, 3);  // la luz de los shaders: 3 = ultra
                engine.Lod().detailBias = baseLod.detailBias + surfaces.detailBias;
                engine.Lod().sphereMinPixels = surfaces.sphereMinPixels;
                layer.SetHullDetail(std::clamp(g.hullDamage, 0, 3));
                backend.SetVsync(options.vsync && ui.settings.vsync);
                if (options.frames == 0) SDL_SetWindowFullscreen(window, ui.settings.fullscreen);
                sounds.SetVolume(static_cast<float>(ui.settings.uiVolume) / 100.0f);
            };
            const auto storeSettings = [&] {
                applySettings();
                if (!settingsPath.empty() && !SaveSettings(settingsPath, ui.settings)) std::printf("Aviso: no se pudo guardar la configuracion\n");
            };
            applySettings();

            FramePacket packet;
            auto last = Clock::now();
            const auto start = last;
            std::uint64_t rendered = 0;
            bool running = true, orbitingCamera = false, atlasDrag = false;
            int contactIndex = -1;
            std::size_t lastTutorialStep = session.Conversation().Step();
            std::string lastLogLine;
            double titleClock = 0, frameSeconds = 1.0 / 60.0;

            // El raton dirige la nave, o mira a pie, solo si nada de la interfaz lo necesita.
            const auto uiFree = [&] { return !ui.wheel && !ui.atlas.open && ui.screen == HudScreen::Playing; };
            const auto walking = [&] { return uiFree() && session.OnFoot(); };
            const auto flying = [&] { return uiFree() && !cursor && !session.OnFoot(); };
            const auto syncMouse = [&] { SDL_SetWindowRelativeMouseMode(window, (flying() || walking()) && options.frames == 0); };

            const auto setCursor = [&](bool on) {
                cursor = on;
                syncMouse();
                if (on) session.Apply(Order(CommandType::CursorUsed));
            };

            const auto openWheel = [&](bool on) {
                ui.wheel = on;
                syncMouse();
                if (!on) return;
                int w = 0, h = 0;
                SDL_GetWindowSize(window, &w, &h);
                SDL_WarpMouseInWindow(window, static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f);
            };

            // Una linea de consola. Lo que devuelve el comando se ve en el registro.
            const auto runConsole = [&] {
                if (console.empty()) return;
                // Es la consola de quien tiene la partida. En la de otro (--connect, o entrando desde el menu) eres un
                // jugador mas: sin categoria de personal no hay comandos, y los trucos no tocarian la partida de verdad.
                const Rank me = remote || !options.connect.empty() ? Rank::Player : Rank::Owner;
                const std::string answer = RunCommand(engine.Commands(), me, console);
                authority.Announce("> " + console + (answer.empty() ? std::string() : ": " + answer));
                console.clear();
            };

            // Pegar varias lineas las ejecuta una a una; la ultima, sin salto, queda para editarla.
            const auto paste = [&] {
                char* text = SDL_GetClipboardText();
                if (!text) return;
                for (const char* c = text; *c; ++c) {
                    if (*c == '\n') runConsole();
                    else if (*c != '\r') console.push_back(*c);
                }
                SDL_free(text);
            };
            setCursor(cursor);
            cursor = ui.screen == HudScreen::Menu || options.cursor;

            const auto beginGame = [&](GameModeId mode) {
                authority.SetMode(mode);
                ui.screen = HudScreen::Playing;
                ui.page = MenuPage::Main;
                SDL_SetWindowRelativeMouseMode(window, options.frames == 0);
                cursor = false;
                playing = true;
            };

            const auto writeGame = [&] {
                if (savePath.empty() || !playing) return;
                std::string problem;
                // Con la partida abierta a la red se guardan tambien los archivos de los demas jugadores.
                if (host && host->Hosting() ? host->Save(&problem) : SaveGame(savePath, authority.Snapshot(), &problem)) return;
                std::printf("Aviso: no se pudo guardar: %s\n", problem.c_str());
                authority.Announce("No se pudo guardar la partida.");
            };

            const auto loadGame = [&] {
                std::string problem;
                if (!saved || !authority.Restore(*saved, &problem)) {
                    std::printf("Aviso: %s\n", problem.c_str());
                    return;
                }
                beginGame(saved->world.mode);
                authority.TakeSaveRequest();
                authority.Announce("Partida cargada.");
            };
            if (options.load) {
                loadGame();
                if (options.cursor) session.Apply(Order(CommandType::CursorUsed));  // la carga borra lo que setCursor marco al empezar
                runCommands();
            }

            const auto selectNext = [&] {
                if (session.Contacts().empty()) return;
                contactIndex = (contactIndex + 1) % static_cast<int>(session.Contacts().size());
                target = session.Contacts()[static_cast<std::size_t>(contactIndex)].target;
            };

            // Volver de la partida de otro a la propia: el cliente se conecta otra vez al servidor de aqui.
            const auto leaveRemote = [&](const std::string& why) {
                if (!remote || !host) return;
                if (clientLink) clientLink->Close();
                auto link = MakeMemoryLink();
                host->Add(link.first);
                localServerEnd = link.first;
                clientLink = link.second;
                Hello back = hello;
                back.join = JoinAs::Host;
                client = std::make_unique<GameClient>(session, clientLink, back);
                remote = false;
                host->Update(0.0);
                client->Update();
                ui.net.status = why;
                target = {};
            };
            // Entrar en la partida de otro: el cliente deja el servidor de aqui (su nave sale de este mundo,
            // como al desconectarse) y se conecta al de alli, con nave propia.
            const auto joinRemote = [&](const NetGame& game) {
                if (!host || remote) return;
                std::string problem;
                std::shared_ptr<Transport> link = ConnectTo(game.address, game.port, 3.0, &problem);
                if (!link) {
                    ui.net.status = "No se pudo entrar en " + game.name + ": " + problem + ".";
                    return;
                }
                if (clientLink) clientLink->Close();
                clientLink = link;
                Hello guest = hello;
                guest.join = JoinAs::OwnShip;
                client = std::make_unique<GameClient>(session, clientLink, guest);
                remote = true;
                remoteName = game.name;
                ui.net.status.clear();
                target = {};
                ui.screen = HudScreen::Playing;
                ui.page = MenuPage::Main;
                SDL_SetWindowRelativeMouseMode(window, options.frames == 0);
            };

            const auto perform = [&](const HudAction& a) {
                using T = HudActionType;
                using C = CommandType;
                // Lo que cambia la partida va como orden (GameSession::Apply); lo demas es de la interfaz.
                switch (a.type) {
                    case T::Drive: session.Apply(Order(C::SetDrive, a.value)); break;
                    case T::Go: session.Apply(Order(C::Autopilot, target)); break;
                    case T::Stop: session.Apply(Order(C::CancelAutopilot)); break;
                    case T::Mine:  // drones: al objetivo, o de vuelta si ya trabajan
                        if (session.DronesOut() > 0 || session.DroneSite()) {
                            if (session.DroneSite() && *session.DroneSite() != target && (target.kind == Kind::Planet || target.kind == Kind::Belt)) session.Apply(Order(C::DeployDrones, target));
                            else session.Apply(Order(C::RecallDrones));
                        } else {
                            session.Apply(Order(C::DeployDrones, target));
                        }
                        break;
                    case T::Shipyard: session.Apply(Order(C::DeployShipyard)); break;
                    case T::ExtractElement: session.Apply(Order(C::ExtractElement, a.value)); break;
                    case T::Power: session.Apply(Order(C::DeployPower, target)); break;
                    case T::Use: session.Apply(Order(C::Interact, target)); break;
                    case T::Scan:
                        if (session.Apply(Order(C::StartScan))) sounds.Play(UiSound::RadarScan);
                        break;
                    case T::Upgrade:
                        if (target.kind == Kind::Station) session.Apply(Order(C::UpgradeStation, target.index, static_cast<int>(ModuleKind::Collector)));
                        break;
                    case T::SelectContact:
                        if (static_cast<std::size_t>(a.value) < session.Contacts().size()) target = session.Contacts()[static_cast<std::size_t>(a.value)].target;
                        break;
                    case T::NextDialogue: session.Apply(Order(C::NextDialogue)); break;
                    case T::ReplayTip: session.Apply(Order(C::ReplayTip)); break;
                    case T::ToggleCursor: setCursor(!cursor); break;
                    case T::ToggleKeys: ui.showKeys = !ui.showKeys; break;
                    case T::StartMode: beginGame(static_cast<GameModeId>(a.value)); break;
                    case T::Continue: session.Apply(Order(C::Continue)); break;
                    case T::Resume: ui.screen = HudScreen::Playing; syncMouse(); break;
                    case T::Orbit: session.Apply(Order(C::Orbit)); break;
                    case T::Shield:
                        if (a.value == -1) session.Apply(Order(C::CycleShield));
                        else if (a.value == -2) session.Apply(Order(C::ShieldOn, 0));
                        else session.Apply(Order(C::SetShield, a.value));
                        break;
                    case T::Coolant:
                        if (a.value >= 0 && a.value < static_cast<int>(CoolantKind::Count))
                            session.Apply(Order(C::Coolant, a.value, session.Ship().defense.coolantOn[static_cast<std::size_t>(a.value)] ? 0 : 1));
                        break;
                    case T::CloseWheel: openWheel(false); break;
                    case T::RadarRange: ui.radarRange = std::clamp(ui.radarRange + a.value, 0, kRadarRangeCount - 1); break;
                    case T::ToggleTrajectory: ui.trajectory = !ui.trajectory; break;
                    case T::ToggleWorkshop:
                        ui.workshop = !ui.workshop && session.CanRefit();
                        if (ui.workshop) ui.cursor = true;  // el taller se maneja con el raton
                        openWheel(false);
                        break;
                    case T::WorkshopSlot: ui.workshopSlot = a.value; break;
                    case T::InstallModule:
                    case T::ImpulseEngine: {
                        const CommandResult done = session.Apply(a.type == T::InstallModule ? Order(C::InstallModule, a.value, a.index) : Order(C::ImpulseEngine, a.value));
                        if (!done) {
                            authority.Notice(done.why);
                            sounds.Play(UiSound::Error);
                        }
                        break;
                    }
                    case T::ToggleAtlas:
                        ui.atlas.open = !ui.atlas.open;
                        if (ui.atlas.open) {
                            ui.atlas.system = session.NearestSystem();
                            ui.atlas.selected = target.kind != Kind::None && target.system == ui.atlas.system ? target : TargetRef{Kind::Star, ui.atlas.system, 0};
                            ui.atlas.scroll = 0;
                            ui.atlas.zoom = 1.0;
                        }
                        atlasDrag = false;
                        syncMouse();
                        break;
                    case T::AtlasSystem:
                        ui.atlas.system = a.value;
                        ui.atlas.selected = TargetRef{Kind::Star, a.value, 0};
                        ui.atlas.zoom = 1.0;
                        break;
                    case T::AtlasSelect:
                        ui.atlas.selected = TargetRef{static_cast<Kind>(a.value), a.system, a.index};
                        break;
                    case T::AtlasMode:
                        ui.atlas.table = a.value == 2;
                        if (!ui.atlas.table) ui.atlas.astro = a.value == 1;
                        ui.atlas.zoom = 1.0;
                        break;
                    case T::AtlasTarget: target = ui.atlas.selected; break;
                    case T::Dock: session.Apply(Order(C::Dock, target)); break;
                    case T::Undock: session.Apply(Order(C::Undock)); break;
                    case T::Disembark:
                        session.Apply(Order(C::Disembark));
                        syncMouse();
                        break;
                    case T::Board:
                        session.Apply(Order(C::Board));
                        syncMouse();
                        break;
                    case T::Quit: running = false; break;
                    case T::MenuPage:
                        ui.page = static_cast<MenuPage>(a.value);
                        ui.pageClock = 0.0;
                        break;
                    case T::Quality:
                        ui.settings.quality = static_cast<GraphicsQuality>(std::clamp(a.value, 0, static_cast<int>(GraphicsQuality::Count) - 1));
                        ui.settings.graphics = OptionsOf(ui.settings.quality);
                        storeSettings();
                        break;
                    case T::Graphics: {
                        // value: la opcion; index: su nuevo valor (en las de si o no se ignora).
                        GraphicsOptions& g = ui.settings.graphics;
                        switch (static_cast<GraphicsOption>(a.value)) {
                            case GraphicsOption::RenderScale: g.renderScale = std::clamp(a.index, 25, 100); break;
                            case GraphicsOption::Bloom: g.bloom = !g.bloom; break;
                            case GraphicsOption::Film: g.film = !g.film; break;
                            case GraphicsOption::Surfaces: g.surfaces = std::clamp(a.index, 0, 3); break;
                            case GraphicsOption::Lighting: g.lighting = std::clamp(a.index, 0, 3); break;
                            case GraphicsOption::HullDamage: g.hullDamage = std::clamp(a.index, 0, 3); break;
                            case GraphicsOption::Shadows: g.shadows = !g.shadows; break;
                        }
                        storeSettings();
                        break;
                    }
                    case T::ToggleFullscreen:
                        ui.settings.fullscreen = !ui.settings.fullscreen;
                        storeSettings();
                        break;
                    case T::ToggleVsync:
                        ui.settings.vsync = !ui.settings.vsync;
                        storeSettings();
                        break;
                    case T::UiVolume:
                        ui.settings.uiVolume = std::clamp(a.value * 25, 0, 100);
                        storeSettings();
                        break;
                    case T::SetUiStyle: {
                        const bool firstTime = ui.settings.uiStyle == UiStyle::Unset;
                        ui.settings.uiStyle = a.value == 0 ? UiStyle::Minimal : UiStyle::Expert;
                        storeSettings();
                        // La primera eleccion sale antes del menu: elegida, se pasa a el.
                        if (firstTime && ui.page == MenuPage::Interface) {
                            ui.page = MenuPage::Main;
                            ui.pageClock = 0.0;
                        }
                        break;
                    }
                    case T::UiScale:
                        ui.settings.uiScale = kUiScales[std::clamp(a.value, 0, kUiScaleCount - 1)];
                        storeSettings();
                        break;
                    case T::TogglePanel:
                        if (a.value >= 0 && a.value < static_cast<int>(HudPanel::Count)) {
                            ui.settings.folded ^= 1u << a.value;
                            storeSettings();
                        }
                        break;
                    case T::Analyze:
                        ui.analysis = !ui.analysis;
                        session.Apply(Order(C::AnalysisUsed));
                        break;
                    case T::LoadGame: loadGame(); break;
                    case T::Fire:
                        if (a.value >= 0) session.Apply(Order(C::Fire, target, a.value));
                        else if (!session.Apply(Order(C::FireAll, target))) {
                            authority.Announce("Nada listo para disparar.");
                            sounds.Play(UiSound::Error);
                        }
                        break;
                    case T::Track:
                        if (session.Tracking()) session.Apply(Order(C::StopTracking));
                        else session.Apply(Order(C::Track, target));
                        break;
                    case T::SaveGame:
                        writeGame();
                        authority.Announce("Partida guardada.");
                        break;
                    case T::NetHost:
                        if (!host || remote) break;
                        ui.net.status.clear();
                        if (host->Hosting()) {
                            host->CloseNetwork();
                        } else {
                            std::string problem;
                            if (!host->OpenToNetwork(kDefaultPort, "Partida de " + playerName, 8, &problem)) ui.net.status = "No se pudo abrir la partida: " + problem + ".";
                        }
                        break;
                    case T::NetSearch:
                        if (search.valid()) break;  // ya hay una en marcha
                        ui.net.searching = true;
                        ui.net.status.clear();
                        search = std::async(std::launch::async, [] { return FindLanGames(1.5); });
                        break;
                    case T::NetJoin:
                        if (a.value >= 0 && static_cast<std::size_t>(a.value) < ui.net.games.size()) joinRemote(NetGame(ui.net.games[static_cast<std::size_t>(a.value)]));
                        break;
                    case T::NetLeave: leaveRemote(""); break;
                }
            };

            // Lo mismo que los botones de la pagina de red del menu, para probar sin manos.
            engine.Commands().Add("red", "partida en red, como en el menu: red (estado) | red abrir | red cerrar | red buscar | red entrar <n> | red volver", [&](const CommandRegistry::Args& args) -> std::string {
                const auto state = [&]() -> std::string {
                    if (!ui.net.available) return "Sin red (el juego va con --direct o entro con --connect)";
                    std::string text = remote ? "En la partida de " + remoteName : host->Hosting() ? "Abierta en el puerto " + std::to_string(host->Port()) + ", " + std::to_string(ui.net.guests) + " dentro" : std::string("Solo en este equipo");
                    for (std::size_t i = 0; i < ui.net.games.size(); ++i) text += "\n  " + std::to_string(i + 1) + ": " + ui.net.games[i].name + " en " + ui.net.games[i].address + ":" + std::to_string(ui.net.games[i].port);
                    if (!ui.net.status.empty()) text += "\n  " + ui.net.status;
                    return text;
                };
                if (args.empty()) return state();
                if (args[0] == "abrir" && host && !host->Hosting()) perform({HudActionType::NetHost});
                else if (args[0] == "cerrar" && host && host->Hosting()) perform({HudActionType::NetHost});
                else if (args[0] == "buscar") {
                    // Aqui se espera a la respuesta, para que el comando siguiente ya tenga la lista.
                    ui.net.games.clear();
                    for (const LanGame& g : FindLanGames(1.5))
                        if (!(host && host->Hosting() && g.port == host->Port())) ui.net.games.push_back({g.name, g.address, g.port, g.players, g.maxPlayers, g.mode});
                    ui.net.searched = true;
                } else if (args[0] == "entrar") perform({HudActionType::NetJoin, args.size() >= 2 ? std::atoi(args[1].c_str()) - 1 : 0});
                else if (args[0] == "volver") perform({HudActionType::NetLeave});
                else return engine.Commands().Fail("Uso: red [abrir|cerrar|buscar|entrar <n>|volver]");
                return state();
            });

            bool wasScanning = false;
            int tutorialDone = session.Conversation().Completions();
            while (running) {
                ShipControls controls;
                bool clicked = false;
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_EVENT_QUIT) running = false;
                    else if (e.type == SDL_EVENT_TEXT_INPUT && consoleOpen) console += e.text.text;
                    else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1 && !e.key.repeat) {
                        consoleOpen = !consoleOpen;
                        if (consoleOpen) SDL_StartTextInput(window);
                        else SDL_StopTextInput(window);
                    } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F2 && !e.key.repeat && authority.VesselCount() > 1 && !remote) {
                        // Con varias naves, F2 pasa a llevar la siguiente.
                        authority.SetFocus((authority.Focus() + 1) % authority.VesselCount());
                        target = {};
                        contactIndex = -1;
                        authority.Announce("Llevas la nave " + std::to_string(authority.VesselId()) + ".");
                    } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F3 && !e.key.repeat && authority.PlayerCount() > 1 && !remote) {
                        // Con varios jugadores, F3 pasa a ser el siguiente: su nave, su puesto, su registro.
                        authority.SetFocusPlayer((authority.FocusPlayer() + 1) % authority.PlayerCount());
                        target = {};
                        contactIndex = -1;
                        syncMouse();
                        authority.Announce("Eres " + authority.PlayerAt(authority.FocusPlayer()).name + ".");
                    } else if (e.type == SDL_EVENT_KEY_DOWN && consoleOpen) {
                        if (e.key.key == SDLK_ESCAPE) {
                            consoleOpen = false;
                            SDL_StopTextInput(window);
                        } else if (e.key.key == SDLK_BACKSPACE && !console.empty()) {
                            // Una letra con tilde son varios bytes en UTF-8.
                            while (console.size() > 1 && (static_cast<unsigned char>(console.back()) & 0xC0) == 0x80) console.pop_back();
                            console.pop_back();
                        } else if (e.key.key == SDLK_RETURN && !e.key.repeat) {
                            runConsole();
                        } else if ((e.key.mod & SDL_KMOD_CTRL) != 0 && e.key.key == SDLK_V) {
                            paste();
                        } else if ((e.key.mod & SDL_KMOD_CTRL) != 0 && e.key.key == SDLK_C) {
                            SDL_SetClipboardText(console.c_str());
                        } else if ((e.key.mod & SDL_KMOD_CTRL) != 0 && e.key.key == SDLK_X) {
                            SDL_SetClipboardText(console.c_str());
                            console.clear();
                        }
                    } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && ui.screen == HudScreen::Menu) {
                        const bool choosing = ui.page == MenuPage::Main || ui.page == MenuPage::NewGame;
                        if (choosing && e.key.key >= SDLK_1 && e.key.key <= SDLK_5) beginGame(static_cast<GameModeId>(e.key.key - SDLK_1));
                        else if (ui.page == MenuPage::Main && e.key.key == SDLK_RETURN) loadGame();
                        else if (e.key.key == SDLK_ESCAPE) {
                            if (ui.page == MenuPage::Main) running = false;
                            else if (ui.page == MenuPage::Interface && ui.settings.uiStyle == UiStyle::Unset) perform({HudActionType::SetUiStyle, 1});  // sin elegir, la de siempre
                            else perform({HudActionType::MenuPage, static_cast<int>(ui.page == MenuPage::Graphics ? MenuPage::Settings : MenuPage::Main)});
                        }
                    } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && ui.screen == HudScreen::Paused) {
                        if (e.key.key == SDLK_ESCAPE) {
                            if (ui.page == MenuPage::Graphics) perform({HudActionType::MenuPage, static_cast<int>(MenuPage::Settings)});
                            else if (ui.page == MenuPage::Settings || ui.page == MenuPage::Network) perform({HudActionType::MenuPage, static_cast<int>(MenuPage::Main)});
                            else perform({HudActionType::Resume});
                        }
                    } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                        switch (e.key.key) {
                            case SDLK_ESCAPE:
                                if (ui.wheel) {
                                    openWheel(false);
                                    break;
                                }
                                if (ui.atlas.open) {
                                    perform({HudActionType::ToggleAtlas});
                                    break;
                                }
                                if (ui.analysis) {
                                    ui.analysis = false;
                                    break;
                                }
                                ui.screen = HudScreen::Paused;
                                ui.page = MenuPage::Main;
                                syncMouse();
                                break;
                            case SDLK_1: perform({HudActionType::Drive, 0}); break;
                            case SDLK_2: perform({HudActionType::Drive, 1}); break;
                            case SDLK_3: perform({HudActionType::Drive, 2}); break;
                            case SDLK_4: perform({HudActionType::Drive, 3}); break;
                            case SDLK_SPACE: controls.manualBrake = true; break;
                            case SDLK_TAB: selectNext(); break;
                            case SDLK_T:
                                if (!session.OnFoot()) openWheel(!ui.wheel);
                                break;
                            case SDLK_E:
                                if (session.OnFoot()) perform({HudActionType::Board});
                                break;
                            case SDLK_M: perform({HudActionType::ToggleAtlas}); break;
                            case SDLK_C: perform({HudActionType::ToggleCursor}); break;
                            case SDLK_H: perform({HudActionType::ToggleKeys}); break;
                            case SDLK_V:
                                if (session.OnFoot()) session.ToggleFootView();
                                else viewOrbit = Quatd{};
                                break;
                            case SDLK_G: perform({HudActionType::Fire, -1}); break;
                            case SDLK_B: perform({HudActionType::Track}); break;
                            case SDLK_K: perform({HudActionType::Shield, -1}); break;
                            case SDLK_N: session.Apply(Order(CommandType::Cinematic, session.Cinematic() ? 0 : 1)); break;
                            case SDLK_RETURN:
                                if (session.Phase() == GamePhase::GameOver) perform({HudActionType::Continue});
                                else perform({HudActionType::NextDialogue});
                                break;
                            case SDLK_BACKSPACE: perform({HudActionType::ReplayTip}); break;
                            default: break;
                        }
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                        clicked = true;
                        if (ui.atlas.open) {
                            int pixelW = 0, pixelH = 0;
                            SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);
                            const AtlasLayout l = ComputeAtlas(pixelW, pixelH, ui.atlas.zoom);
                            atlasDrag = e.button.x >= l.viewX && e.button.x <= l.viewX + l.viewW && e.button.y >= l.viewY && e.button.y <= l.viewY + l.viewH;
                        }
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                        atlasDrag = false;
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_MIDDLE) {
                        if (ui.screen == HudScreen::Playing && !ui.atlas.open && session.Phase() == GamePhase::Flying) openWheel(true);
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_MIDDLE) {
                        clicked = ui.wheel;  // al soltar, elige lo que tenga debajo
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
                        orbitingCamera = true;
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
                        orbitingCamera = false;
                    } else if (e.type == SDL_EVENT_MOUSE_WHEEL && ui.atlas.open) {
                        int pixelW = 0, pixelH = 0;
                        SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);
                        const AtlasLayout l = ComputeAtlas(pixelW, pixelH, ui.atlas.zoom);
                        if (e.wheel.mouse_x < l.viewX) ui.atlas.scroll = std::max(ui.atlas.scroll + (e.wheel.y > 0.0f ? -2 : 2), 0);
                        else ui.atlas.zoom = std::clamp(ui.atlas.zoom * (e.wheel.y > 0.0f ? 1.2 : 1.0 / 1.2), ui.atlas.astro ? 0.05 : 0.5, 2.0e5);
                    } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                        // Se puede acercar hasta rozar el casco (hullFloor), pero no meterse dentro.
                        viewDistance = std::clamp(viewDistance * (e.wheel.y > 0.0f ? 0.9 : 1.1), std::max(0.15 * shipScale, hullFloor), 60.0);
                    } else if (e.type == SDL_EVENT_MOUSE_MOTION && !consoleOpen) {
                        if (atlasDrag) {
                            // Lo que hay bajo el raton lo acompana, como con la camara de la nave.
                            ui.atlas.yaw -= static_cast<double>(e.motion.xrel) * 0.008;
                            ui.atlas.pitch = std::clamp(ui.atlas.pitch - static_cast<double>(e.motion.yrel) * 0.008, -1.5, 1.5);
                        } else if (orbitingCamera) {
                            // Arrastre: lo que hay bajo el raton lo acompana. Sobre los ejes de la
                            // propia camara, asi que puede dar la vuelta completa en cualquier sentido.
                            const double dx = static_cast<double>(e.motion.xrel) * 0.004, dy = static_cast<double>(e.motion.yrel) * 0.004;
                            viewOrbit = (viewOrbit * Quatd::FromAxisAngle({0, 1, 0}, dx) * Quatd::FromAxisAngle({1, 0, 0}, -dy)).Normalized();
                        } else if (walking() && options.frames == 0) {
                            controls.foot.yaw += e.motion.xrel * 0.0022;
                            controls.foot.pitch += e.motion.yrel * 0.0022;
                        } else if (flying() && options.frames == 0) {
                            controls.turnYaw += e.motion.xrel * 0.0016f;
                            controls.turnPitch += e.motion.yrel * 0.0016f;
                        }
                    }
                }
                const bool* keys = SDL_GetKeyboardState(nullptr);
                const auto axis = [&](SDL_Scancode minus, SDL_Scancode plus) { return (keys[plus] ? 1.0f : 0.0f) - (keys[minus] ? 1.0f : 0.0f); };
                if (!consoleOpen && walking()) {
                    controls.foot.move.forward = axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
                    controls.foot.move.strafe = axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
                    controls.foot.move.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                    controls.foot.move.jump = keys[SDL_SCANCODE_SPACE];
                } else if (!consoleOpen && uiFree() && !session.OnFoot()) {
                    // Tambien con el cursor activo. W y S cabecean, A y D guinan, Q y E alabean; R sube el mando y F lo baja.
                    const float step = static_cast<float>(frameSeconds) * 0.9f;
                    controls.turnYaw += axis(SDL_SCANCODE_A, SDL_SCANCODE_D) * step;
                    controls.turnPitch += axis(SDL_SCANCODE_W, SDL_SCANCODE_S) * step;
                    controls.turnRoll = axis(SDL_SCANCODE_E, SDL_SCANCODE_Q) * step * 1.5f;
                    controls.throttleChange = axis(SDL_SCANCODE_F, SDL_SCANCODE_R) * 0.35;
                }

                if (hud.TakeHoverSound()) sounds.Play(UiSound::Hover);
                for (const HudAction& a : hud.TakeActions()) {
                    sounds.Play(SoundFor(a.type));
                    perform(a);
                }
                // El sonido del barrido acaba con el barrido: al terminar, al cancelarse o al perder la nave.
                const bool scanningNow = session.Scanning();
                if (wasScanning && !scanningNow) sounds.Stop(UiSound::RadarScan);
                wasScanning = scanningNow;
                controls.view = viewOrbit;

                for (const Options::TimedCommand& c : options.timedCommands)
                    if (c.frame == static_cast<int>(rendered)) std::printf("[f%llu] > %s\n%s\n", static_cast<unsigned long long>(rendered), c.text.c_str(), engine.Commands().Execute(c.text).c_str());
                const auto now = Clock::now();
                const double realSeconds = std::min(std::chrono::duration<double>(now - last).count(), 0.05);
                frameSeconds = realSeconds;
                last = now;
                const double stepSeconds = ui.screen == HudScreen::Playing ? realSeconds : 0.0;
                if (options.fixedStep) session.StepFixed(stepSeconds, controls, static_cast<float>(options.width) * 0.5f);
                else session.Step(stepSeconds, controls, static_cast<float>(options.width) * 0.5f);
                // En loopback, el paso de arriba solo ha mandado el mando: ahora avanza el servidor y la
                // replica aplica lo que le llega. En pausa el servidor no avanza, pero sigue contando lo que hay.
                if (host) {
                    // "nave ver", "jugador ver", F2 y F3 cambian el foco de la partida de verdad: la conexion de
                    // este proceso pasa a ver y mandar como ese jugador.
                    if (!remote) host->Follow(localServerEnd.get(), authority.FocusPlayer());
                    // Con la partida abierta a la red, el servidor no se para aunque el anfitrion este en pausa;
                    // sin abrir, y con el jugador en la partida de otro, este mundo espera.
                    const double serverSeconds = host->Hosting() ? realSeconds : remote ? 0.0 : stepSeconds;
                    host->Update(serverSeconds, static_cast<float>(options.width) * 0.5f);
                    if (serverSeconds <= 0.0 && !remote) host->SendSnapshots();
                }
                if (client) {
                    client->Update();
                    const bool dropped = client->Refused() || (clientLink && !clientLink->Open());
                    if (dropped && remote) {
                        leaveRemote(client->Refused() ? "No se pudo entrar: " + client->Refusal() : "Se perdió la conexión con " + remoteName + ".");
                        ui.screen = HudScreen::Paused;
                        ui.page = MenuPage::Network;
                        SDL_SetWindowRelativeMouseMode(window, false);
                    } else if (dropped && !host) {
                        std::printf("El servidor ha cerrado la conexión%s%s\n", client->Refused() ? ": " : ".", client->Refusal().c_str());
                        running = false;
                    }
                }
                // Lo que ensena la pagina de red del menu.
                if (search.valid() && search.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    ui.net.games.clear();
                    for (const LanGame& g : search.get()) {
                        if (host && host->Hosting() && g.port == host->Port() && g.name == "Partida de " + playerName) continue;  // la mia no
                        ui.net.games.push_back({g.name, g.address, g.port, g.players, g.maxPlayers, g.mode});
                    }
                    ui.net.searching = false;
                    ui.net.searched = true;
                }
                ui.net.available = host != nullptr && NetworkAvailable();
                ui.net.hosting = host && host->Hosting();
                ui.net.port = host ? host->Port() : 0;
                ui.net.guests = host ? std::max(host->Clients() - (remote ? 0 : 1), 0) : 0;
                ui.net.joined = remote || (!host && client);
                ui.net.canLeave = remote;
                ui.net.where = remote ? remoteName : options.connect;
                // Con --frames, cada cambio de paso del tutorial se anota con el tiempo real que lleva la partida.
                if (options.frames > 0 && session.Conversation().Step() != lastTutorialStep) {
                    lastTutorialStep = session.Conversation().Step();
                    std::printf("[f%llu, %.1f s reales] Tutorial: paso %zu\n", static_cast<unsigned long long>(rendered), std::chrono::duration<double>(now - start).count(), lastTutorialStep);
                }
                if (options.frames > 0 && !session.Log().empty() && session.Log().back() != lastLogLine) {
                    lastLogLine = session.Log().back();
                    std::printf("[f%llu, %.1f s reales] Registro: %s\n", static_cast<unsigned long long>(rendered), std::chrono::duration<double>(now - start).count(), lastLogLine.c_str());
                }
                // Un objetivo del tutorial cumplido suena; el destello lo pinta la interfaz.
                if (session.Conversation().Completions() != tutorialDone) {
                    tutorialDone = session.Conversation().Completions();
                    sounds.Play(UiSound::Confirm);
                }
                if (authority.TakeSaveDeletion() && !savePath.empty()) {
                    DeleteGame(savePath);
                    saved.reset();
                    ui.savedGame.clear();
                    playing = false;  // no queda nada que guardar hasta empezar otra vez
                }
                if (session.Phase() == GamePhase::Flying && ModeInfo(session.Mode()).permadeath && !playing && ui.screen == HudScreen::Playing) playing = true;
                if (authority.TakeSaveRequest()) writeGame();

                // Camara de persecucion: detras y algo por encima de una nave de 1,2 km.
                int width = options.width, height = options.height;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                const ShipState& ship = session.Ship();
                const bool alive = session.Phase() == GamePhase::Flying;
                const Vec3d center = alive ? ship.position : session.WreckPosition();
                camera.SetViewport(width, height, static_cast<float>(width) * 0.5f, kNearPlaneKm);
                if (!cameraReady) {
                    smoothed = ship.orientation;
                    cameraReady = true;
                }
                // Con el boton derecho, la camara da vueltas alrededor de la nave. Al perderla,
                // se aleja para ver como se deshace.
                // Apagada, la camara se aparta despacio y rodea la nave mientras se queda a oscuras.
                const bool adrift = !alive && session.ShutDown();
                const double age = session.WreckAge();
                double reach = alive ? viewDistance : adrift ? viewDistance * 0.6 + std::min(age, 14.0) * 0.12 : viewDistance + std::min(age, 6.0) * 0.9;
                const Quatd drift = alive ? Quatd{} : adrift ? Quatd::FromAxisAngle({0, 1, 0}, -(age * 0.035 + 0.9)) * Quatd::FromAxisAngle({1, 0, 0}, -0.25 + std::min(age, 14.0) * 0.012)
                                                             : Quatd::FromAxisAngle({0, 1, 0}, -(age * 0.08 + 0.5));
                // La camara no va clavada al casco: lo sigue con un poco de retraso, y asi los giros
                // y los cambios de orbita se ven suaves. El avance sigue siendo el mismo. A la deriva
                // no sigue el giro de la nave: la deja dar vueltas delante.
                Quatd follow = adrift ? smoothed : ship.orientation;
                if (follow.x * smoothed.x + follow.y * smoothed.y + follow.z * smoothed.z + follow.w * smoothed.w < 0.0) follow = Quatd{-follow.x, -follow.y, -follow.z, -follow.w};
                const double blend = 1.0 - std::exp(-realSeconds / 0.22);
                Quatd mixed;
                mixed.x = smoothed.x + (follow.x - smoothed.x) * blend;
                mixed.y = smoothed.y + (follow.y - smoothed.y) * blend;
                mixed.z = smoothed.z + (follow.z - smoothed.z) * blend;
                mixed.w = smoothed.w + (follow.w - smoothed.w) * blend;
                smoothed = mixed.Normalized();
                const Quatd chase = smoothed * viewOrbit * drift;
                // De cerca, la camara se para fuera del casco: se ven las piezas de cerca sin meterse
                // dentro. Un rayo desde lejos hacia el centro dice hasta donde sobresale la nave por
                // ese lado; solo se repite cuando la vista gira.
                if (alive) {
                    const Vec3d back = chase.ToMatrix().TransformDirection(Vec3d{0.0, -0.26, -1.0});
                    const double along = Length(back);  // la camara queda a reach * along del centro
                    if (viewDistance * along < rules.ship.bow * 1.6 + 0.1) {
                        const Vec3f look(Normalize(ship.orientation.Conjugate().ToMatrix().TransformDirection(back)));
                        if (Dot(look, hullLook) < 0.9995f) {
                            hullLook = look;
                            const double hull = layer.HullReach(look) * 0.001;
                            hullFloor = hull > 0.0 ? (hull * 1.08 + 0.06) / along : 0.0;  // a 60 m o mas del casco
                        }
                    } else {
                        hullLook = Vec3f{0, 0, 0};
                        hullFloor = 0.0;
                    }
                    reach = std::max(reach, hullFloor);
                }
                const Vec3d offset = chase.ToMatrix().TransformDirection(Vec3d{0.0, -reach * 0.26, -reach});
                camera.SetPosition(center + offset);
                camera.SetOrientation(Quatf::Cast(chase * Quatd::FromAxisAngle({1, 0, 0}, -(std::atan(0.26) + 0.08))));
                // Modo cinematica: tras un impacto en el blanco, la camara lo rodea mirando al golpe.
                Vec3d shotEye;
                Quatd shotLook;
                if (session.CinematicShot(shotEye, shotLook)) {
                    camera.SetPosition(shotEye);
                    camera.SetOrientation(Quatf::Cast(shotLook));
                }
                if (session.OnFoot()) {
                    // A pie: la cabeza mira con el raton; en tercera persona, la camara va detras y no
                    // atraviesa las paredes.
                    const HumanoidState& me = session.Player();
                    // En el mundo arriba es -Y: inclinar hacia abajo es girar en negativo sobre X.
                    const Quatd look = Quatd::FromAxisAngle({0, 1, 0}, me.yaw) * Quatd::FromAxisAngle({1, 0, 0}, -me.pitch);
                    const Vec3d eye = me.position + Vec3d{0, session.PersonRules().eyeHeight, 0};
                    Vec3d at = eye;
                    if (!session.FirstPerson()) {
                        const Vec3d back{-std::sin(me.yaw) * std::cos(me.pitch) * 3.2, 0.5 + std::sin(me.pitch) * 3.2, -std::cos(me.yaw) * std::cos(me.pitch) * 3.2};
                        at = eye + back * ClearRay(eye, eye + back, session.Deck().area, 0.25);
                    }
                    camera.SetPosition(session.DeckOrigin() + DeckToWorld(at));
                    camera.SetOrientation(Quatf::Cast(look));
                }

                // Con clics simulados el raton esta donde el ultimo que ya ocurrio, o el primero.
                bool simulated = false, simulatedClick = false;
                float mouseX = 0, mouseY = 0;
                for (const Options::Click& c : options.clicks) {
                    if (c.frame > static_cast<int>(rendered) && simulated) break;
                    simulated = true;
                    mouseX = c.x;
                    mouseY = c.y;
                    simulatedClick = simulatedClick || c.frame == static_cast<int>(rendered);
                }
                ui.clicked = clicked || simulatedClick;
                if (!simulated) SDL_GetMouseState(&mouseX, &mouseY);
                ui.mouseX = mouseX;
                ui.mouseY = mouseY;
                ui.cursor = cursor || ui.wheel || ui.atlas.open || ui.screen != HudScreen::Playing || session.Phase() == GamePhase::GameOver;
                ui.consoleOpen = consoleOpen;
                const double clockBefore = ui.clock;
                ui.clock = std::chrono::duration<double>(now - start).count();
                ui.pageClock += ui.clock - clockBefore;
                if (!session.CanRefit()) ui.workshop = false;  // al soltarse, el taller se cierra
                hud.Set(&session, &camera, &target, ui);

                const World& world = session.GetWorld();
                const Vec3d absolute = ship.position + world.origin;
                const Vec3f fogDrift(Vec3d{std::fmod(absolute.x / 3.0e8, 16.0), std::fmod(absolute.y / 3.0e8, 16.0), std::fmod(absolute.z / 3.0e8, 16.0)});
                // Se dibuja el mundo de la burbuja donde esta la nave que se lleva, con los mismos ajustes.
                Engine& view = session.ViewEngine();
                if (&view != &engine) {
                    view.Post() = engine.Post();
                    view.Lod() = engine.Lod();
                }
                view.SetFog(static_cast<float>(session.Visibility()), fog, fogDrift);
                {
                    // La nebulosa es una nube alrededor: la camara se mueve dentro de una baldosa de
                    // ruido de 12 veces la visibilidad tipica. Se cuenta en double para no temblar.
                    const double tile = rules.world.fogDistance * 12.0;
                    const Vec3d cameraAbsolute = camera.Position() + world.origin;
                    const auto wrap = [&](double v) { const double x = v / tile; return static_cast<float>(x - std::floor(x)); };
                    session.ViewEngine().SetFogCloud(Vec3f{wrap(cameraAbsolute.x), wrap(cameraAbsolute.y), wrap(cameraAbsolute.z)}, static_cast<float>(tile));
                }
                session.ViewEngine().SetFogRamp(NebulaRamp(), session.NebulaTone());

                // Curvatura: cuanto mas cerca de c, mas se concentra el cielo por delante.
                const double beta = std::min(ship.speed / kLightSpeed, 0.9999);
                const bool hyper = ship.speed > kLightSpeed;
                // Primero la lente se traga la vista; al cerrarse, las estrellas se vuelven lineas.
                const float forming = static_cast<float>(std::clamp((ship.warpCharge - 0.75) / 0.25, 0.0, 1.0));
                const float aberration = std::max(hyper ? 40.0f : static_cast<float>(std::sqrt((1.0 + beta) / (1.0 - beta))), 1.0f + forming * 6.0f);
                const float warp = alive ? std::max(hyper ? 1.0f : static_cast<float>(std::clamp((beta - 0.04) / 0.5, 0.0, 1.0)), forming) : 0.0f;
                const float blackout = forming * (1.0f - static_cast<float>(std::clamp(beta / 0.5, 0.0, 1.0)));
                // Se usa el rumbo de la burbuja, que siempre cambia suave, y un eje que la acompana
                // sin saltos. Con un eje fijo, las lineas se recolocaban de golpe en ciertos angulos.
                warpUp = TransportUp(warpUp, ship.warpHeading);
                const Vec3f forward = camera.ViewRotation().TransformDirection(Vec3f(ship.warpHeading));
                const Vec3f lineUp = camera.ViewRotation().TransformDirection(Vec3f(warpUp));
                const float params[12] = {forward.x, forward.y, forward.z, static_cast<float>(width) * 0.5f, aberration, hyper ? 2.4f : 1.2f, blackout, 0.0f, lineUp.x, lineUp.y, lineUp.z, 0.0f};
                backend.SetPostEffectParams(warpEffect, params);
                backend.SetPostEffect(warpEffect, warp > 0.005f, warp);

                // El punto de mira va en la imagen final: asi puede invertir lo que tiene debajo.
                // Fuera de la pantalla (o detras de la camara) la marca se queda en el borde, con una
                // flecha hacia donde esta, y la linea entre las dos sigue dibujada.
                const float reticleScale = std::clamp(static_cast<float>(height) / 900.0f, 0.7f, 2.0f);
                const auto place = [&](const Vec3d& direction, float& x, float& y) -> float {
                    const Vec3f v = camera.ViewRotation().TransformDirection(Vec3f(direction));
                    const float halfW = static_cast<float>(width) * 0.5f, halfH = static_cast<float>(height) * 0.5f;
                    const ScreenPoint p = camera.WorldToScreen(ship.position + direction * 1.0e6);
                    if (p.visible && p.x >= 0.0f && p.y >= 0.0f && p.x < static_cast<float>(width) && p.y < static_cast<float>(height)) {
                        x = p.x;
                        y = p.y;
                        return 1.0f;
                    }
                    float dx = v.x, dy = v.y;
                    if (v.z > 0.0f) {
                        dx = p.x - halfW;
                        dy = p.y - halfH;
                    }
                    const float length = std::sqrt(dx * dx + dy * dy);
                    if (length < 1e-6f) {
                        dx = 0.0f;
                        dy = 1.0f;
                    }
                    const float margin = 34.0f * reticleScale;
                    const float t = std::min((halfW - margin) / std::max(std::abs(dx), 1e-6f), (halfH - margin) / std::max(std::abs(dy), 1e-6f));
                    x = halfW + dx * t;
                    y = halfH + dy * t;
                    return 2.0f;
                };
                float noseX = 0, noseY = 0, aimX = 0, aimY = 0;
                const float noseShown = place(Heading(ship), noseX, noseY), aimShown = place(AimHeading(ship), aimX, aimY);
                const bool apart = std::abs(noseX - aimX) + std::abs(noseY - aimY) > 4.0f;
                const float reticle[12] = {noseX, noseY, aimX, aimY, reticleScale, noseShown, aimShown, apart ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                backend.SetPostEffectParams(reticleEffect, reticle);
                backend.SetPostEffect(reticleEffect, alive && ui.screen == HudScreen::Playing && !session.OnFoot(), cursor ? 0.55f : 1.0f);

                session.ViewEngine().Build(camera, ui.clock, packet);

                // La esfera de la ventana de analisis es la real, con su superficie y su giro.
                packet.hasMiniature = false;
                const bool atlasBody = ui.atlas.open && ui.atlas.astro && !ui.atlas.table;
                const TargetRef shown3d = atlasBody ? ui.atlas.selected : target;
                if ((ui.analysis || atlasBody) && ui.screen == HudScreen::Playing && session.Scanned(shown3d) && (shown3d.kind == Kind::Star || shown3d.kind == Kind::Planet)) {
                    const Body* body = nullptr;
                    if (shown3d.kind == Kind::Star) {
                        const SystemState& state = world.state[static_cast<std::size_t>(shown3d.system)];
                        if (static_cast<std::size_t>(shown3d.index) < state.starBodies.size() && state.starBodies[static_cast<std::size_t>(shown3d.index)] >= 0) body = &world.bodies[static_cast<std::size_t>(state.starBodies[static_cast<std::size_t>(shown3d.index)])];
                    } else {
                        for (const int index : world.state[static_cast<std::size_t>(shown3d.system)].bodies) {
                            const Body& candidate = world.bodies[static_cast<std::size_t>(index)];
                            if (candidate.data == shown3d.index) body = &candidate;
                        }
                    }
                    if (body && body->renderable >= 0 && world.renderables[static_cast<std::size_t>(body->renderable)].surface >= 0) {
                        Renderable shown = world.renderables[static_cast<std::size_t>(body->renderable)];
                        shown.spinPhase = ui.clock * 0.25;
                        shown.spinRate = 0.0;
                        AnalysisLayout layout = ComputeAnalysis(width, height);
                        if (atlasBody) {
                            const AtlasLayout atlas = ComputeAtlas(width, height, ui.atlas.zoom);
                            layout.miniX = atlas.miniX;
                            layout.miniY = atlas.miniY;
                            layout.miniSize = atlas.miniSize;
                        }
                        constexpr float kDistance = 3.2f;
                        const float focal = static_cast<float>(layout.miniSize) * 1.25f;
                        const Quatf viewer = atlasBody ? Quatf::FromAxisAngle({1, 0, 0}, static_cast<float>(-ui.atlas.pitch)) * Quatf::FromAxisAngle({0, 1, 0}, static_cast<float>(-ui.atlas.yaw)) : Quatf::FromAxisAngle({1, 0, 0}, -0.3f);
                        SphereInstance& m = packet.miniature;
                        m = SphereInstance{};
                        m.viewCenter = {0, 0, kDistance};
                        m.radius = 1.0f;
                        m.viewToBody = BodyOrientation(shown, 0.0).Conjugate() * viewer;
                        // Luz de lado: el relieve solo se ve con sombras largas.
                        m.lightDirection = body->isStar ? Vec3f{} : Normalize(Vec3f{-0.86f, -0.34f, -0.38f});
                        m.pixelRadius = SilhouettePixels(1.0f, kDistance, focal);
                        m.detail = SurfaceDetail(focal / (kDistance - 1.0f), config.lod);
                        m.seed = shown.seed;
                        m.distance = kDistance;
                        m.surface = static_cast<std::uint16_t>(shown.surface);
                        packet.hasMiniature = true;
                        packet.miniatureX = layout.miniX;
                        packet.miniatureY = layout.miniY;
                        packet.miniatureSize = layout.miniSize;
                        packet.miniatureFocalPx = focal;
                    }
                }

                ui.atlas.realSphere = atlasBody && packet.hasMiniature;

                // Una estrella del mapa lleva su corona: el mismo calculo que en el juego, atenuado
                // para que el disco se siga viendo. Va aunque su superficie no este en memoria.
                packet.miniatureCorona = false;
                if (atlasBody && ui.screen == HudScreen::Playing && shown3d.kind == Kind::Star && session.Scanned(shown3d) && shown3d.system >= 0 &&
                    static_cast<std::size_t>(shown3d.system) < world.systems.size() && shown3d.index < world.systems[static_cast<std::size_t>(shown3d.system)]->StarCount()) {
                    const astro::StarData& star = world.systems[static_cast<std::size_t>(shown3d.system)]->StarAt(shown3d.index);
                    if (star.kind != astro::StarClass::BlackHole) {
                        const AtlasLayout atlas = ComputeAtlas(width, height, ui.atlas.zoom);
                        constexpr float kDistance = 3.2f;  // la misma de la miniatura
                        packet.miniatureX = atlas.miniX;
                        packet.miniatureY = atlas.miniY;
                        packet.miniatureSize = atlas.miniSize;
                        packet.miniatureFocalPx = static_cast<float>(atlas.miniSize) * 1.25f;
                        packet.miniature.viewCenter = {0, 0, kDistance};
                        packet.miniature.radius = 1.0f;
                        packet.miniatureCorona = true;
                        packet.coronaX = static_cast<int>(atlas.viewX);
                        packet.coronaY = static_cast<int>(atlas.viewY);
                        packet.coronaW = static_cast<int>(atlas.viewW);
                        packet.coronaH = static_cast<int>(atlas.viewH);
                        packet.coronaColor = astro::BlackbodyColor(star.temperature);
                        packet.coronaIntensity = 0.7f;
                        packet.coronaSeed = static_cast<float>(shown3d.system * 7 + shown3d.index) * 3.7f;
                        packet.coronaView = (Quatf::FromAxisAngle({1, 0, 0}, static_cast<float>(-ui.atlas.pitch)) * Quatf::FromAxisAngle({0, 1, 0}, static_cast<float>(-ui.atlas.yaw))).Conjugate();
                    }
                }

                const bool lastFrame = options.frames > 0 && static_cast<int>(rendered) + 1 >= options.frames;
                if (lastFrame && !options.screenshot.empty()) backend.RequestScreenshot(options.screenshot);
                backend.Render(packet);
                ++rendered;
                if (lastFrame) running = false;

                titleClock += realSeconds;
                if (titleClock > 0.5) {
                    titleClock = 0;
                    SDL_SetWindowTitle(window, ("Nebulosa | " + std::to_string(static_cast<int>(static_cast<double>(rendered) / std::max(std::chrono::duration<double>(now - start).count(), 0.01))) + " FPS").c_str());
                }
            }

            writeGame();
            const std::chrono::duration<double> total = Clock::now() - start;
            std::printf("Driver: %s\n", backend.DriverName().c_str());
            std::printf("Fotogramas: %llu  Presentados: %llu  Media: %.1f FPS\n", static_cast<unsigned long long>(rendered), static_cast<unsigned long long>(backend.PresentedFrames()), static_cast<double>(rendered) / total.count());
            std::printf("Mallas: %zu  Esferas: %zu  Sprites: %zu  Lineas: %zu  Nubes: %zu\n", packet.stats.meshesVisible, packet.stats.spheresVisible, packet.stats.spritesVisible, packet.stats.linesVisible, packet.nebulae.size());
            for (const std::string& line : session.Log()) std::printf("Registro: %s\n", line.c_str());
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
