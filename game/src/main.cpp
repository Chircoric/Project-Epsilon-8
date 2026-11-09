// main.cpp - El juego en una ventana.
//
// Controles: raton = rumbo, W/S = mando, 1/2/3/4 = impulso, crucero, curvatura, hiper.
//            Tab = siguiente objetivo, F = ruta asistida, X = pararla, Espacio = frenar y mantenerse.
//            M = desplegar mina, P = desplegar central, E = recoger o explorar, U = mejorar.
//            R = escaner. I = analisis del objetivo. O = orbita aqui. C = cursor. H = teclas. Enter = dialogo.
//            F1 = consola de comandos. Esc = pausa.
//
// Opciones:
//   --frames N         termina tras N fotogramas
//   --screenshot a.png guarda el ultimo fotograma; requiere --frames
//   --cmd "texto"      ejecuta un comando al empezar; se puede repetir
//   --no-vsync         dibuja sin esperar al monitor
//   --size W H         tamano de la ventana
//   --seed N           otra partida
//   --mode N           salta el menu: 0 pacifico, 1 facil, 2 normal, 3 dificil, 4 realista
//   --no-tutorial      sin dialogos
//   --cursor           empieza con el cursor activo
//   --click F X Y      clic simulado en el fotograma F, en esos pixeles; se puede repetir
//   --wheel, --atlas, --atlas-astro  empieza con la rueda, el mapa o la vista de un astro abiertos
//   --save a.sav       usa este archivo de partida. Con --frames no se guarda salvo que se indique
//   --load             carga la partida al empezar, sin pasar por el menu
//   --quality N        calidad grafica: 0 baja, 1 media, 2 alta, 3 ultra. Sin ella, la guardada
//   --page N           pagina del menu: 0 principal, 1 nueva partida, 2 configuracion, 3 creditos
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
#include <sstream>
#include <string>
#include <vector>

#include "epsilon/Engine.h"
#include "epsilon/SdlGpuBackend.h"
#include "game/GameHud.h"
#include "game/GameLayer.h"
#include "game/GameModels.h"
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
    bool vsync = true, debug = false, noTutorial = false, cursor = false;
    int mode = -1;                 // -1 = menu
    int quality = -1;              // -1 = la guardada; si no, GraphicsQuality
    int page = 0;                  // pagina del menu con la que empezar: MenuPage
    struct Click {
        int frame = -1;
        float x = 0, y = 0;
    };
    std::vector<Click> clicks;     // clics simulados; puede haber varios
};

Options ParseOptions(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) o.frames = std::atoi(argv[++i]);
        else if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
        else if (a == "--cmd" && i + 1 < argc) o.commands.push_back(argv[++i]);
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
        else if (a == "--mode" && i + 1 < argc) o.mode = std::atoi(argv[++i]);
        else if (a == "--quality" && i + 1 < argc) o.quality = std::atoi(argv[++i]);
        else if (a == "--page" && i + 1 < argc) o.page = std::atoi(argv[++i]);
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

// Lo que se puede escribir en la consola o pasar con --cmd.
void RegisterCommands(CommandRegistry& commands, GameSession& g, TargetRef& target) {
    const auto findContact = [&g](const std::string& word) -> std::optional<TargetRef> {
        const World& w = g.GetWorld();
        // Por tipo, lo mas cercano. Asi una prueba no depende de como se llame cada cosa.
        std::optional<TargetRef> best;
        double nearest = 1.0e300;
        const auto consider = [&](const TargetRef& t) {
            const std::optional<TargetInfo> info = g.Resolve(t);
            if (!info) return;
            const double d = Length(info->position - g.Ship().position);
            if (d < nearest) {
                nearest = d;
                best = t;
            }
        };
        const int system = g.NearestSystem();
        const astro::StarSystemData& data = *w.systems[static_cast<std::size_t>(system)];
        if (word == "estrella") for (int m = 0; m < data.StarCount(); ++m) consider({Kind::Star, system, m});
        if (word == "planeta")
            for (std::size_t b = 0; b < data.bodies.size(); ++b)
                if (data.bodies[b].parent < 0 && data.bodies[b].kind != astro::PlanetClass::Comet && data.bodies[b].kind != astro::PlanetClass::Asteroid) consider({Kind::Planet, system, static_cast<int>(b)});
        if (word == "campo") for (std::size_t b = 0; b < data.belts.size(); ++b) consider({Kind::Belt, system, static_cast<int>(b)});
        if (word == "estacion") for (const Station& s : g.Stations().All()) consider({Kind::Station, s.site.system, s.id});
        for (const Anomaly& a : g.Anomalies()) {
            const bool structure = word == "estructura" && a.kind == AnomalyKind::Derelict;
            const bool hostile = word == "hostil" && a.stance == Stance::Hostile && a.kind == AnomalyKind::AlienShip;
            const bool bastion = word == "bastion" && a.stance == Stance::Hostile && a.kind == AnomalyKind::AlienStation;
            const bool neutral = word == "neutral" && a.stance == Stance::Neutral && a.kind == AnomalyKind::AlienShip;
            const bool haven = word == "refugio" && a.stance == Stance::Neutral && a.kind == AnomalyKind::AlienStation;
            if (structure || hostile || neutral || bastion || haven) consider({Kind::Anomaly, a.system, a.id});
        }
        return best;
    };

    commands.Add("objetivo", "fija el objetivo: planeta, campo, estrella, estacion, estructura, hostil, neutral, bastion, refugio", [&, findContact](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: objetivo <tipo>");
        const std::optional<TargetRef> found = findContact(args[0]);
        if (!found) return commands.Fail("No hay ninguno");
        target = *found;
        return "Objetivo: " + g.Resolve(target)->name;
    });
    commands.Add("saltar", "mueve la nave junto a un objetivo, para probar: saltar <tipo> [km]", [&, findContact](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: saltar <tipo> [km]");
        const std::optional<TargetRef> found = findContact(args[0]);
        if (!found) return commands.Fail("No hay ninguno");
        target = *found;
        const std::optional<TargetInfo> info = g.Resolve(target);
        g.JumpTo(target, args.size() >= 2 ? std::atof(args[1].c_str()) : info->arrival);
        return "Junto a " + info->name;
    });
    commands.Add("ir", "piloto automatico al objetivo", [&](const CommandRegistry::Args&) -> std::string { return g.SetAutopilot(target) ? "Rumbo fijado" : commands.Fail("Sin objetivo"); });
    commands.Add("mina", "despliega una mina en el objetivo", [&](const CommandRegistry::Args&) -> std::string { return g.DeployMining(target) ? "Desplegada" : commands.Fail("No se pudo"); });
    commands.Add("central", "despliega una central en el objetivo", [&](const CommandRegistry::Args&) -> std::string { return g.DeployPower(target) ? "Desplegada" : commands.Fail("No se pudo"); });
    commands.Add("usar", "recoge de una estacion o explora", [&](const CommandRegistry::Args&) -> std::string { return g.Interact(target) ? "Hecho" : commands.Fail("No se pudo"); });
    commands.Add("motor", "motor impulso|crucero|hibrida|hiper", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: motor <modo>");
        const DriveMode mode = args[0] == "hiper" ? DriveMode::Hyper : args[0] == "hibrida" ? DriveMode::Warp : args[0] == "crucero" ? DriveMode::Cruise : DriveMode::Impulse;
        return g.SetDrive(mode) ? std::string(Name(mode)) : commands.Fail("No disponible");
    });
    commands.Add("analisis", "abre el analisis del objetivo", [&](const CommandRegistry::Args&) -> std::string {
        g.MarkAnalysisUsed();
        return "Hecho";
    });
    commands.Add("mando", "mando -100..100", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: mando <porcentaje>");
        g.SetThrottle(std::atof(args[0].c_str()) / 100.0);
        return args[0];
    });
    commands.Add("tiempo", "segundos simulados por segundo real", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.TimeScale() = std::atof(args[0].c_str());
        return std::to_string(g.TimeScale());
    });
    commands.Add("regalo", "emisores de energia negativa y recursos, para probar: regalo <emisores>", [&](const CommandRegistry::Args& args) -> std::string {
        g.Ship().emitters += args.empty() ? 2 : std::max(std::atoi(args[0].c_str()), 0);
        g.Ship().cargo.Add(Resource::Metal, 2000.0);
        g.Ship().cargo.Add(Resource::Silicate, 800.0);
        return "Hecho";
    });
    commands.Add("blindaje", "fija el blindaje, para probar", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().heatShieldTier = std::atoi(args[0].c_str());
        return std::to_string(g.Ship().heatShieldTier);
    });
    commands.Add("maniobra", "fija el nivel de maniobra, para probar", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().handlingTier = std::atoi(args[0].c_str());
        return std::to_string(g.Ship().handlingTier);
    });
    commands.Add("escaner", "lanza un barrido; con un numero, fija su nivel", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) {
            g.Ship().scannerTier = std::atoi(args[0].c_str());
            return std::to_string(g.Ship().scannerTier);
        }
        return g.StartScan() ? "Barrido" : commands.Fail("No se pudo");
    });
    commands.Add("casco", "fija el casco, para probar", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().hull = std::atof(args[0].c_str());
        return "Hecho";
    });
    commands.Add("revelar", "analiza todo el sistema actual, para probar", [&](const CommandRegistry::Args&) -> std::string {
        g.MarkScanned(g.NearestSystem());
        return "Hecho";
    });
    // ---- Modo creativo ----
    commands.Add("creativo", "modo creativo: invulnerable, todo sin fin, todos los motores y sistemas: creativo [si|no]", [&](const CommandRegistry::Args& args) -> std::string {
        const bool on = args.empty() ? !g.Creative() : (args[0] == "si" || args[0] == "1");
        g.SetCreative(on);
        return on ? "Creativo activado. Prueba: aparecer, viajar, emisores, nivel, tiempo" : "Creativo desactivado";
    });
    commands.Add("aparecer", "crea delante de la nave: aparecer <hostil|neutral|bastion|estacion|estructura> [km] [cantidad]", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: aparecer <hostil|neutral|bastion|estacion|estructura> [km] [cantidad]");
        const std::string& what = args[0];
        AnomalyKind kind = AnomalyKind::AlienShip;
        Stance stance = Stance::Hostile;
        if (what == "neutral") stance = Stance::Neutral;
        else if (what == "bastion") kind = AnomalyKind::AlienStation;
        else if (what == "estacion") { kind = AnomalyKind::AlienStation; stance = Stance::Neutral; }
        else if (what == "estructura") kind = AnomalyKind::Derelict;
        else if (what != "hostil") return commands.Fail("No conozco eso");
        const double km = args.size() >= 2 ? std::atof(args[1].c_str()) : 2000.0;
        const int count = args.size() >= 3 ? std::clamp(std::atoi(args[2].c_str()), 1, 20) : 1;
        int id = -1;
        for (int i = 0; i < count; ++i) id = g.SpawnAnomaly(kind, stance, km * (1.0 + 0.15 * i));
        if (id < 0) return commands.Fail("No se pudo");
        target = TargetRef{Kind::Anomaly, g.NearestSystem(), id};
        return std::to_string(count) + " creado(s); el ultimo es el objetivo";
    });
    commands.Add("viajar", "lleva la nave a la estrella de otro sistema: viajar <sistema>", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: viajar <sistema>");
        const int system = std::atoi(args[0].c_str());
        if (system < 0 || static_cast<std::size_t>(system) >= g.GetWorld().systems.size()) return commands.Fail("No existe");
        g.MarkKnown(system);
        const TargetRef star{Kind::Star, system, 0};
        const std::optional<TargetInfo> info = g.Resolve(star);
        if (!info || !g.JumpTo(star, info->arrival)) return commands.Fail("No se pudo");
        target = star;
        return "En " + g.SystemName(system);
    });
    commands.Add("buscar", "lleva la nave junto al primer astro de una clase: buscar <terrestre|hielo|lava|roca|desierto|gaseoso|helado|caliente|luna> [n]", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: buscar <clase> [n]");
        using PC = astro::PlanetClass;
        const struct { const char* word; PC kind; } classes[] = {{"terrestre", PC::Terran}, {"hielo", PC::Ice}, {"lava", PC::Lava}, {"roca", PC::Rock}, {"desierto", PC::Desert},
                                                                 {"gaseoso", PC::GasGiant}, {"helado", PC::IceGiant}, {"caliente", PC::HotJupiter}, {"luna", PC::Moon}};
        const auto* chosen = std::find_if(std::begin(classes), std::end(classes), [&](const auto& c) { return args[0] == c.word; });
        if (chosen == std::end(classes)) return commands.Fail("No conozco esa clase");
        int skip = args.size() >= 2 ? std::max(std::atoi(args[1].c_str()) - 1, 0) : 0;
        const World& w = g.GetWorld();
        for (std::size_t s = 0; s < w.systems.size(); ++s) {
            const astro::StarSystemData& data = *w.systems[s];
            for (std::size_t b = 0; b < data.bodies.size(); ++b) {
                if (data.bodies[b].kind != chosen->kind || data.bodies[b].destroyed || skip-- > 0) continue;
                const int system = static_cast<int>(s);
                g.MarkKnown(system);
                const TargetRef star{Kind::Star, system, 0};
                if (const std::optional<TargetInfo> info = g.Resolve(star)) g.JumpTo(star, info->arrival);
                const TargetRef planet{Kind::Planet, system, static_cast<int>(b)};
                for (int i = 0; i < 60 && !g.Resolve(planet); ++i) g.Step(1.0 / 60.0, {});  // que se cargue el sistema
                const std::optional<TargetInfo> info = g.Resolve(planet);
                if (!info) return commands.Fail("No se pudo cargar");
                g.JumpTo(planet, info->radius * 2.2);
                if (const std::optional<TargetInfo> now = g.Resolve(planet)) FaceTo(g.Ship(), now->position - g.Ship().position);
                target = planet;
                return "Junto a " + info->name + " (sistema " + std::to_string(system) + ", astro " + std::to_string(b) + ")";
            }
        }
        return commands.Fail("No hay ninguno");
    });
    commands.Add("emisores","fija los emisores de energia negativa: emisores <n>", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().emitters = std::max(std::atoi(args[0].c_str()), 0);
        return std::to_string(g.Ship().emitters);
    });
    commands.Add("nivel", "fija un nivel: nivel <escaner|blindaje|maniobra|computador|armas|enlace|arena|plasma|em|conos|amor|agua|hielo|nitrogeno|radiadores> <n>", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.size() < 2) return commands.Fail("Uso: nivel <escaner|blindaje|maniobra|computador|armas|enlace|arena|plasma|em|conos|amor|agua|hielo|nitrogeno|radiadores> <n>");
        const int n = std::max(std::atoi(args[1].c_str()), 0);
        ShipState& s = g.Ship();
        const std::string& w = args[0];
        const struct { const char* word; int shield; int coolant; } parts[] = {
            {"arena", 0, -1}, {"plasma", 1, -1}, {"em", 2, -1}, {"conos", 3, -1}, {"amor", 4, -1},
            {"agua", -1, 0}, {"hielo", -1, 1}, {"nitrogeno", -1, 2}, {"radiadores", -1, 3}};
        for (const auto& p : parts) {
            if (w != p.word) continue;
            const int tier = std::clamp(n, 0, g.Rules().combat.maxTier);
            if (p.shield >= 0) {
                s.defense.shieldTier[static_cast<std::size_t>(p.shield)] = tier;
                s.defense.charge[static_cast<std::size_t>(p.shield)] = ShieldCapacity(g.Rules().defense, static_cast<ShieldKind>(p.shield), tier);
            } else {
                s.defense.coolantTier[static_cast<std::size_t>(p.coolant)] = tier;
            }
            return "Hecho";
        }
        if (args[0] == "escaner") s.scannerTier = n;
        else if (args[0] == "blindaje") s.heatShieldTier = n;
        else if (args[0] == "maniobra") s.handlingTier = n;
        else if (args[0] == "computador") s.computerTier = std::clamp(n, 1, g.Rules().combat.maxTier);
        else if (args[0] == "armas") for (WeaponMount& m : s.weapons) m.tier = std::clamp(n, 1, g.Rules().combat.maxTier);
        else if (args[0] == "enlace") s.relayTier = n;
        else return commands.Fail("No conozco ese nivel");
        return "Hecho";
    });
    // ---- Escudos y calor ----
    const auto shieldArg = [](const std::string& w, ShieldKind& kind) {
        if (w == "arena") kind = ShieldKind::Dust;
        else if (w == "plasma") kind = ShieldKind::Plasma;
        else if (w == "em" || w == "electromagnetico") kind = ShieldKind::EM;
        else if (w == "conos" || w == "ablativos") kind = ShieldKind::Ablative;
        else if (w == "amor") kind = ShieldKind::Love;
        else return false;
        return true;
    };
    const auto coolantArg = [](const std::string& w, CoolantKind& kind) {
        if (w == "agua") kind = CoolantKind::Water;
        else if (w == "hielo") kind = CoolantKind::Ice;
        else if (w == "nitrogeno") kind = CoolantKind::Nitrogen;
        else if (w == "radiadores") kind = CoolantKind::Radiator;
        else return false;
        return true;
    };
    commands.Add("escudo", "elige el campo: escudo <arena|plasma|em|amor|no>; sin argumento, el siguiente (como K)", [&, shieldArg](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return Name(g.CycleShield());
        if (args[0] == "no") {
            g.SetShieldOn(false);
            return "Apagado";
        }
        ShieldKind kind = ShieldKind::Dust;
        if (!shieldArg(args[0], kind)) return commands.Fail("Uso: escudo <arena|plasma|em|amor|no>");
        return g.SetShield(kind) ? std::string(Name(kind)) : commands.Fail("No lo tienes o no es un campo");
    });
    commands.Add("calor", "muestra el calor; con un numero, lo fija (100 es el limite)", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().defense.heat = std::clamp(std::atof(args[0].c_str()), 0.0, g.Rules().thermal.maximum);
        return std::to_string(static_cast<int>(g.Ship().defense.heat));
    });
    commands.Add("refrigerante", "enciende o apaga uno: refrigerante <agua|hielo|nitrogeno|radiadores> <si|no>", [&, coolantArg](const CommandRegistry::Args& args) -> std::string {
        CoolantKind kind = CoolantKind::Water;
        if (args.size() < 2 || !coolantArg(args[0], kind)) return commands.Fail("Uso: refrigerante <agua|hielo|nitrogeno|radiadores> <si|no>");
        g.SetCoolant(kind, args[1] == "si" || args[1] == "1");
        return "Hecho";
    });
    commands.Add("destruir", "destruye la anomalia objetivo, para probar", [&](const CommandRegistry::Args&) -> std::string {
        for (Anomaly& a : g.Anomalies())
            if (target.kind == Kind::Anomaly && a.id == target.index && a.maxHull > 0.0) {
                a.hull = 0.0;
                return "Hecho";
            }
        return commands.Fail("El objetivo no se puede destruir");
    });
    const auto weaponArg = [](const CommandRegistry::Args& args, WeaponKind& kind) {
        if (args.empty()) return false;
        const std::string& w = args[0];
        if (w == "torreta") kind = WeaponKind::Turret;
        else if (w == "espinal") kind = WeaponKind::Spinal;
        else if (w == "misil") kind = WeaponKind::Missile;
        else if (w == "drones") kind = WeaponKind::Drone;
        else return false;
        return true;
    };
    commands.Add("disparar", "dispara al objetivo: disparar [torreta|espinal|misil|drones]; sin argumento, todo lo que este listo", [&, weaponArg](const CommandRegistry::Args& args) -> std::string {
        WeaponKind kind = WeaponKind::Turret;
        if (weaponArg(args, kind)) return g.Fire(kind, target) ? "Disparo" : commands.Fail("No se pudo");
        const int fired = g.FireAll(target);
        return fired > 0 ? std::to_string(fired) + " armas" : commands.Fail("Nada listo");
    });
    commands.Add("apuntar", "gira el rumbo a la solucion de tiro: apuntar [torreta|espinal|misil|drones]", [&, weaponArg](const CommandRegistry::Args& args) -> std::string {
        WeaponKind kind = WeaponKind::Spinal;
        weaponArg(args, kind);
        return g.AimAtSolution(target, kind) ? "Apuntando" : commands.Fail("Sin solucion");
    });
    commands.Add("alinear", "el rumbo sigue la solucion del canon espinal, como B", [&](const CommandRegistry::Args&) -> std::string {
        g.TrackTarget(target);
        return g.Tracking() ? "Alineando" : commands.Fail("Sin solucion");
    });
    commands.Add("astro", "fija como objetivo el astro numero N de este sistema, como sale en los datos", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: astro <indice>");
        target = TargetRef{Kind::Planet, g.NearestSystem(), std::atoi(args[0].c_str())};
        const std::optional<TargetInfo> info = g.Resolve(target);
        return info ? "Objetivo: " + info->name : commands.Fail("No existe");
    });
    commands.Add("atracar","atraca en la estacion objetivo; con 'ya', sin esperar al haz", [&](const CommandRegistry::Args& args) -> std::string {
        return g.Dock(target, !args.empty() && args[0] == "ya") ? "Atracando" : commands.Fail("No se pudo");
    });
    commands.Add("soltar", "suelta las amarras", [&](const CommandRegistry::Args&) -> std::string {
        g.Undock();
        return "Hecho";
    });
    commands.Add("apie", "baja a la cubierta de la estacion donde esta atracada", [&](const CommandRegistry::Args&) -> std::string { return g.Disembark() ? "A pie" : commands.Fail("Hay que estar atracada"); });
    commands.Add("abordo", "vuelve a la nave desde cualquier punto de la cubierta", [&](const CommandRegistry::Args&) -> std::string { return g.Board(true) ? "A bordo" : commands.Fail("No estas a pie"); });
    commands.Add("vistapie", "cambia entre primera y tercera persona", [&](const CommandRegistry::Args&) -> std::string {
        g.ToggleFootView();
        return g.FirstPerson() ? "Primera persona" : "Tercera persona";
    });
    commands.Add("computador","fija el nivel del computador de punteria, para probar", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().computerTier = std::atoi(args[0].c_str());
        return std::to_string(g.Ship().computerTier);
    });
    commands.Add("orbitar", "circulariza alrededor de lo que domina aqui", [&](const CommandRegistry::Args&) -> std::string { return g.EnterOrbitHere() ? "En orbita" : commands.Fail("No se pudo"); });
    commands.Add("seguir", "sigue tras perder la nave", [&](const CommandRegistry::Args&) -> std::string {
        g.ContinueAfterDefeat();
        return "Hecho";
    });
}

// Resumen de una partida para el menu.
std::string SaveSummary(const SaveData& d) {
    char text[120];
    std::snprintf(text, sizeof(text), "%s   Dia %.1f   %zu estaciones   %d emisores", ModeInfo(d.mode).name, d.simTime / 86400.0, d.stations.size(), d.ship.emitters);
    return text;
}

int Run(Options options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    // Las partidas viven en la carpeta de datos del usuario, no junto al ejecutable.
    std::string savePath = options.savePath;
    std::string settingsPath;
    if (options.frames == 0) {
        if (char* folder = SDL_GetPrefPath("Epsilon", "Nebulosa")) {
            if (savePath.empty()) savePath = std::string(folder) + "partida.sav";
            settingsPath = std::string(folder) + "ajustes.txt";
            SDL_free(folder);
        }
    }
    std::optional<SaveData> saved;
    if (!savePath.empty()) {
        std::string problem;
        saved = LoadFromFile(savePath, &problem);
        if (!saved && problem != "sin partida guardada") std::printf("Aviso: no se pudo leer la partida: %s\n", problem.c_str());
    }
    // La galaxia sale de la semilla: tiene que ser la de la partida guardada.
    if (saved && !options.seedGiven) options.seed = saved->seed;
    if (saved && saved->seed != options.seed) saved.reset();
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
                if (!path.empty()) {
                    std::string error;
                    const auto texture = [&](const std::string& file) {
                        const std::filesystem::path p(file);
                        const std::string full = p.is_absolute() ? file : (std::filesystem::path(path).parent_path() / p).string();
                        return backend.LoadTexture(full, 2048);
                    };
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
            session.RefitToDesign();
            GameLayer layer(session, assets, SdlGpuBackend::kGlowTexture, SdlGpuBackend::kRingTexture, fog);
            engine.AddLayer(&layer);

            TargetRef target;
            Quatd viewOrbit;  // camara respecto a la nave, sin limites
            Quatd smoothed;   // orientacion que sigue la camara
            bool cameraReady = false;
            Vec3d warpUp{0, 1, 0};  // eje de las lineas de la curvatura
            // La camara se aparta en proporcion a la nave: la maqueta mide 0,55 km de proa al centro.
            const double shipScale = std::max(rules.ship.bow / 0.55, 1.0);
            double viewDistance = 2.4 * shipScale;
            RegisterCommands(engine.Commands(), session, target);
            engine.Commands().Add("vista", "camara: vista <grados> [km]", [&](const CommandRegistry::Args& args) -> std::string {
                if (!args.empty()) viewOrbit = Quatd::FromAxisAngle({0, 1, 0}, -std::atof(args[0].c_str()) * kPi / 180.0);
                if (args.size() >= 2) viewDistance = std::atof(args[1].c_str());
                return "Hecho";
            });

            engine.Commands().Add("girar", "gira la nave de golpe, para probar: girar <derecha> <abajo> en grados", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.size() < 2) return engine.Commands().Fail("Uso: girar <derecha> <abajo>");
                ShipState& ship = session.Ship();
                ship.orientation = (ship.orientation * Quatd::FromAxisAngle({0, 1, 0}, std::atof(args[0].c_str()) * kPi / 180.0) * Quatd::FromAxisAngle({1, 0, 0}, -std::atof(args[1].c_str()) * kPi / 180.0)).Normalized();
                ship.aim = ship.orientation;
                ship.moveDirection = ship.warpHeading = Heading(ship);
                return "Hecho";
            });

            engine.Commands().Add("virar", "manda girar la nave, que gira sola con sus propulsores de maniobra: virar <derecha> <abajo> en grados", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.size() < 2) return engine.Commands().Fail("Uso: virar <derecha> <abajo>");
                ShipState& ship = session.Ship();
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

            const int warpEffect = backend.AddPostEffect("curvatura", gPsWarp, sizeof(gPsWarp), true);
            if (warpEffect < 0) std::printf("Aviso: %s\n", backend.LastError().c_str());
            const int reticleEffect = backend.AddPostEffect("reticula", gPsReticle, sizeof(gPsReticle));
            if (reticleEffect < 0) std::printf("Aviso: %s\n", backend.LastError().c_str());

            HudInput ui;
            engine.Commands().Add("modulo", "monta un modulo (atracada): modulo <ranura> <tipo 0..7>; sin nada, lista ranuras y tipos", [&](const CommandRegistry::Args& args) -> std::string {
                const ShipDesign& design = CurrentShipDesign();
                if (args.size() < 2) {
                    std::string list = "Tipos:";
                    for (int k = 0; k < static_cast<int>(ShipModuleKind::Count); ++k) list += " " + std::to_string(k) + "=" + ModuleSpec(static_cast<ShipModuleKind>(k)).name;
                    list += ". Ranuras:";
                    for (std::size_t i = 0; i < design.slots.size(); ++i)
                        list += " " + std::to_string(i) + "=" + PrettyName(design.slots[i].name) + (i < session.Ship().loadout.modules.size() && session.Ship().loadout.modules[i] != ShipModuleKind::None ? std::string(" [") + ModuleSpec(session.Ship().loadout.modules[i]).name + "]" : "");
                    return list;
                }
                std::string why;
                const int kind = std::atoi(args[1].c_str());
                if (kind < 0 || kind >= static_cast<int>(ShipModuleKind::Count)) return "Tipo desconocido";
                return session.InstallModule(std::atoi(args[0].c_str()), static_cast<ShipModuleKind>(kind), &why) ? "Hecho" : why;
            });
            engine.Commands().Add("impulsor", "cambia el motor de impulso (atracada): impulsor <1.." + std::to_string(kImpulseEngineCount) + ">", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.empty()) return std::string("Motor: ") + ImpulseEngine(session.Ship().loadout.engine).name;
                std::string why;
                return session.SetImpulseEngine(std::atoi(args[0].c_str()), &why) ? "Hecho" : why;
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
                session.DamageZone(zone, std::atof(args[1].c_str()), &from);
                return std::string(ZoneInfo(zone).name) + ": " + std::to_string(static_cast<int>(session.Ship().zones[static_cast<std::size_t>(zone)] * 100.0)) + "%";
            });
            engine.Commands().Add("disparo", "proyectil sin escudos que sigue su trayectoria: disparo <dx dy dz> <px py pz> [casco]; llega desde d y pasa por p (m, en el modelo)", [&](const CommandRegistry::Args& args) -> std::string {
                if (args.size() < 6) return "Uso: disparo dx dy dz px py pz [casco]. Ej.: disparo 0 0 1 -150 0 -380 60 (de frente, a la gondola de babor)";
                const Vec3d d = Normalize(Vec3d{std::atof(args[0].c_str()), std::atof(args[1].c_str()), std::atof(args[2].c_str())} + Vec3d{0, 0, 1e-9});
                const Vec3d p{std::atof(args[3].c_str()), std::atof(args[4].c_str()), std::atof(args[5].c_str())};
                const double amount = args.size() >= 7 ? std::atof(args[6].c_str()) : 40.0;
                const int zone = session.ShootHull(p + d * 2000.0, p - d * 2000.0, amount);
                return std::string("Da en: ") + ZoneInfo(zone).name;
            });
            ui.screen = options.mode >= 0 ? HudScreen::Playing : HudScreen::Menu;
            if (options.mode >= 0) session.SetMode(static_cast<GameModeId>(options.mode));
            ui.console = &console;
            if (saved) ui.savedGame = SaveSummary(*saved);
            ui.analysis = options.analysis;
            bool playing = ui.screen == HudScreen::Playing;
            if (options.noTutorial) session.Conversation().Stop();
            bool cursor = ui.screen == HudScreen::Menu || options.cursor;
            ui.wheel = options.wheel;
            ui.atlas.open = options.atlas;
            ui.atlas.astro = options.atlasAstro;
            ui.atlas.table = options.atlasTable;
            if (options.atlas) ui.atlas.selected = TargetRef{Kind::Star, 0, 0};

            session.Step(1.0 / 60.0, {});
            for (const std::string& line : options.commands) std::printf("> %s\n%s\n", line.c_str(), engine.Commands().Execute(line).c_str());
            if (options.atlas && target.kind != Kind::None) {
                ui.atlas.selected = target;  // para ver un astro concreto desde la linea de ordenes
                ui.atlas.system = target.system;
            }

            // La configuracion: cada calidad cambia la resolucion de la escena, el bloom, el grano
            // y el detalle de las superficies. Se parte de lo que el motor trae de serie.
            if (!settingsPath.empty()) ui.settings = LoadSettings(settingsPath);
            if (options.quality >= 0 && options.quality < static_cast<int>(GraphicsQuality::Count)) ui.settings.quality = static_cast<GraphicsQuality>(options.quality);
            ui.page = static_cast<MenuPage>(std::clamp(options.page, 0, static_cast<int>(MenuPage::Credits)));
            const PostSettings basePost = engine.Post();
            const LodSettings baseLod = engine.Lod();
            const auto applySettings = [&] {
                const QualityProfile& p = Profile(ui.settings.quality);
                backend.SetRenderScale(p.renderScale);
                engine.Post().bloom = basePost.bloom && p.bloom;
                engine.Post().grain = p.film ? basePost.grain : 0.0f;
                engine.Post().aberration = p.film ? basePost.aberration : 0.0f;
                engine.Lod().maxOctaves = p.maxOctaves;
                engine.Lod().detailBias = baseLod.detailBias + p.detailBias;
                engine.Lod().sphereMinPixels = p.sphereMinPixels;
                layer.SetHullDetail(p.hullDamage);
                backend.SetVsync(options.vsync && ui.settings.vsync);
                if (options.frames == 0) SDL_SetWindowFullscreen(window, ui.settings.fullscreen);
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
            double titleClock = 0, frameSeconds = 1.0 / 60.0;

            // El raton dirige la nave, o mira a pie, solo si nada de la interfaz lo necesita.
            const auto uiFree = [&] { return !ui.wheel && !ui.atlas.open && ui.screen == HudScreen::Playing; };
            const auto walking = [&] { return uiFree() && session.OnFoot(); };
            const auto flying = [&] { return uiFree() && !cursor && !session.OnFoot(); };
            const auto syncMouse = [&] { SDL_SetWindowRelativeMouseMode(window, (flying() || walking()) && options.frames == 0); };

            const auto setCursor = [&](bool on) {
                cursor = on;
                syncMouse();
                if (on) session.MarkCursorUsed();
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
                const std::string answer = engine.Commands().Execute(console);
                session.Announce("> " + console + (answer.empty() ? std::string() : ": " + answer));
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
                session.SetMode(mode);
                ui.screen = HudScreen::Playing;
                ui.page = MenuPage::Main;
                SDL_SetWindowRelativeMouseMode(window, options.frames == 0);
                cursor = false;
                playing = true;
            };

            const auto writeGame = [&] {
                if (savePath.empty() || !playing) return;
                std::string problem;
                if (SaveToFile(savePath, session.Snapshot(), &problem)) return;
                std::printf("Aviso: no se pudo guardar: %s\n", problem.c_str());
                session.Announce("No se pudo guardar la partida.");
            };

            const auto loadGame = [&] {
                std::string problem;
                if (!saved || !session.Restore(*saved, &problem)) {
                    std::printf("Aviso: %s\n", problem.c_str());
                    return;
                }
                beginGame(saved->mode);
                session.TakeSaveRequest();
                session.Announce("Partida cargada.");
            };
            if (options.load) loadGame();

            const auto selectNext = [&] {
                if (session.Contacts().empty()) return;
                contactIndex = (contactIndex + 1) % static_cast<int>(session.Contacts().size());
                target = session.Contacts()[static_cast<std::size_t>(contactIndex)].target;
            };

            const auto perform = [&](const HudAction& a) {
                using T = HudActionType;
                switch (a.type) {
                    case T::Drive: session.SetDrive(static_cast<DriveMode>(a.value)); break;
                    case T::Go: session.SetAutopilot(target); break;
                    case T::Stop: session.CancelAutopilot(); break;
                    case T::Mine: session.DeployMining(target); break;
                    case T::Power: session.DeployPower(target); break;
                    case T::Use: session.Interact(target); break;
                    case T::Scan: session.StartScan(); break;
                    case T::Upgrade:
                        if (target.kind == Kind::Station) {
                            const Station* s = session.Stations().Find(target.index);
                            if (s) session.UpgradeStation(s->id, s->kind == StationKind::Mining ? ModuleKind::Drill : ModuleKind::Collector);
                        }
                        break;
                    case T::SelectContact:
                        if (static_cast<std::size_t>(a.value) < session.Contacts().size()) target = session.Contacts()[static_cast<std::size_t>(a.value)].target;
                        break;
                    case T::NextDialogue: session.Conversation().Next(); break;
                    case T::ToggleCursor: setCursor(!cursor); break;
                    case T::ToggleKeys: ui.showKeys = !ui.showKeys; break;
                    case T::StartMode: beginGame(static_cast<GameModeId>(a.value)); break;
                    case T::Continue: session.ContinueAfterDefeat(); break;
                    case T::Resume: ui.screen = HudScreen::Playing; syncMouse(); break;
                    case T::Orbit: session.EnterOrbitHere(); break;
                    case T::Shield:
                        if (a.value == -1) session.CycleShield();
                        else if (a.value == -2) session.SetShieldOn(false);
                        else session.SetShield(static_cast<ShieldKind>(a.value));
                        break;
                    case T::Coolant: {
                        const CoolantKind kind = static_cast<CoolantKind>(a.value);
                        session.SetCoolant(kind, !session.Ship().defense.coolantOn[static_cast<std::size_t>(a.value)]);
                        break;
                    }
                    case T::CloseWheel: openWheel(false); break;
                    case T::RadarRange: ui.radarRange = std::clamp(ui.radarRange + a.value, 0, kRadarRangeCount - 1); break;
                    case T::ToggleTrajectory: ui.trajectory = !ui.trajectory; break;
                    case T::ToggleWorkshop:
                        ui.workshop = !ui.workshop && session.CanRefit();
                        if (ui.workshop) ui.cursor = true;  // el taller se maneja con el raton
                        openWheel(false);
                        break;
                    case T::WorkshopSlot: ui.workshopSlot = a.value; break;
                    case T::InstallModule: {
                        std::string why;
                        if (!session.InstallModule(a.index, static_cast<ShipModuleKind>(a.value), &why)) session.Notice(why);
                        break;
                    }
                    case T::ImpulseEngine: {
                        std::string why;
                        if (!session.SetImpulseEngine(a.value, &why)) session.Notice(why);
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
                    case T::Dock: session.Dock(target); break;
                    case T::Undock: session.Undock(); break;
                    case T::Disembark:
                        session.Disembark();
                        syncMouse();
                        break;
                    case T::Board:
                        session.Board();
                        syncMouse();
                        break;
                    case T::Quit: running = false; break;
                    case T::MenuPage:
                        ui.page = static_cast<MenuPage>(a.value);
                        ui.pageClock = 0.0;
                        break;
                    case T::Quality:
                        ui.settings.quality = static_cast<GraphicsQuality>(std::clamp(a.value, 0, static_cast<int>(GraphicsQuality::Count) - 1));
                        storeSettings();
                        break;
                    case T::ToggleFullscreen:
                        ui.settings.fullscreen = !ui.settings.fullscreen;
                        storeSettings();
                        break;
                    case T::ToggleVsync:
                        ui.settings.vsync = !ui.settings.vsync;
                        storeSettings();
                        break;
                    case T::Analyze:
                        ui.analysis = !ui.analysis;
                        session.MarkAnalysisUsed();
                        break;
                    case T::LoadGame: loadGame(); break;
                    case T::Fire:
                        if (a.value >= 0) session.Fire(static_cast<WeaponKind>(a.value), target);
                        else if (session.FireAll(target) == 0) session.Announce("Nada listo para disparar.");
                        break;
                    case T::Track:
                        if (session.Tracking()) session.StopTracking();
                        else session.TrackTarget(target);
                        break;
                    case T::SaveGame:
                        writeGame();
                        session.Announce("Partida guardada.");
                        break;
                }
            };

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
                            else perform({HudActionType::MenuPage, static_cast<int>(MenuPage::Main)});
                        }
                    } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && ui.screen == HudScreen::Paused) {
                        if (e.key.key == SDLK_ESCAPE) {
                            if (ui.page == MenuPage::Settings) perform({HudActionType::MenuPage, static_cast<int>(MenuPage::Main)});
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
                            case SDLK_RETURN:
                                if (session.Phase() == GamePhase::GameOver) perform({HudActionType::Continue});
                                else perform({HudActionType::NextDialogue});
                                break;
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
                        viewDistance = std::clamp(viewDistance * (e.wheel.y > 0.0f ? 0.9 : 1.1), 1.2 * shipScale, 60.0);
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

                for (const HudAction& a : hud.TakeActions()) perform(a);
                controls.view = viewOrbit;

                const auto now = Clock::now();
                const double realSeconds = std::min(std::chrono::duration<double>(now - last).count(), 0.05);
                frameSeconds = realSeconds;
                last = now;
                session.Step(ui.screen == HudScreen::Playing ? realSeconds : 0.0, controls, static_cast<float>(options.width) * 0.5f);
                if (session.TakeSaveDeletion() && !savePath.empty()) {
                    std::error_code ignored;
                    std::filesystem::remove(FilePath(savePath), ignored);
                    saved.reset();
                    ui.savedGame.clear();
                    playing = false;  // no queda nada que guardar hasta empezar otra vez
                }
                if (session.Phase() == GamePhase::Flying && ModeInfo(session.Mode()).permadeath && !playing && ui.screen == HudScreen::Playing) playing = true;
                if (session.TakeSaveRequest()) writeGame();

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
                const double reach = alive ? viewDistance : adrift ? viewDistance * 0.6 + std::min(age, 14.0) * 0.12 : viewDistance + std::min(age, 6.0) * 0.9;
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
                const Vec3d offset = chase.ToMatrix().TransformDirection(Vec3d{0.0, -reach * 0.26, -reach});
                camera.SetPosition(center + offset);
                camera.SetOrientation(Quatf::Cast(chase * Quatd::FromAxisAngle({1, 0, 0}, -(std::atan(0.26) + 0.08))));
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
                engine.SetFog(static_cast<float>(session.Visibility()), fog, fogDrift);
                engine.SetFogRamp(NebulaRamp(), session.NebulaTone());

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

                engine.Build(camera, ui.clock, packet);

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
