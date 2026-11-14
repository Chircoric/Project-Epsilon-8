// epsilon_server - La partida de Nebulosa sin ventana: un servidor al que entran los jugadores.
//
// No dibuja nada ni necesita tarjeta grafica: lleva el mundo, escucha en un puerto, se anuncia en la
// red local y guarda la partida solo. Sin nadie dentro, el mundo se detiene.
//
//   --world carpeta   la partida (mundo.sav y jugadores/). Si existe, se sigue; si no, se crea al guardar.
//                     Sin esta opcion la partida no se guarda
//   --port N          puerto de juego (27300)
//   --name "texto"    como se anuncia en la red local
//   --players N       conexiones a la vez (8)
//   --mode N          partida nueva: 0 Pacifico, 1 Facil, 2 Normal, 3 Dificil, 4 Realista (2)
//   --seed N          partida nueva: la galaxia. Los que entren tienen que usar la misma
//   --no-tutorial     partida nueva: sin el tutorial para el primero que entre
//   --save-every S    segundos entre guardados (120)
//   --seconds S       se cierra solo pasados S segundos: para pruebas
//   --ranks archivo   categorias del personal (por defecto, rangos.txt dentro de --world; sin ninguno, solo en memoria)
//   --no-console      no lee la entrada estandar
//
// Consola por la entrada estandar (docs/multijugador.md, 4.6): quien escribe en la terminal del servidor
// es el dueño y puede usar todos los comandos del juego (los de game/Console.cpp: saltar, creativo, nivel,
// aparecer...). Ademas:
//   ayuda | jugadores | rangos | guardar | salir
//   rango <id> <categoria>   da una categoria (moderador, administrador, dueño, desarrollador; jugador la quita)
//   como <id> <orden...>     ejecuta la orden con la categoria de ese jugador: sin categoria de personal, se rechaza
// Solo el personal puede usar comandos; las categorias las da el servidor, nunca el cliente.
//
// Se cierra con Ctrl+C o con "salir", guardando antes.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "game/Console.h"
#include "game/Net.h"
#include "game/SaveGame.h"
#include "game/Session.h"

using namespace eps;
using namespace game;

namespace {

std::atomic<bool> gStop{false};
void OnInterrupt(int) { gStop = true; }

struct Options {
    std::string world, name = "Nebulosa", ranks;
    int port = kDefaultPort, players = 8, mode = 2;
    std::uint32_t seed = 20261105;
    bool seedGiven = false, noTutorial = false, noConsole = false;
    double saveEvery = 120.0, seconds = 0.0;
};

Options Parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--world" && more) o.world = argv[++i];
        else if (a == "--port" && more) o.port = std::atoi(argv[++i]);
        else if (a == "--name" && more) o.name = argv[++i];
        else if (a == "--players" && more) o.players = std::max(std::atoi(argv[++i]), 1);
        else if (a == "--mode" && more) o.mode = std::clamp(std::atoi(argv[++i]), 0, 4);
        else if (a == "--seed" && more) {
            o.seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            o.seedGiven = true;
        } else if (a == "--no-tutorial") o.noTutorial = true;
        else if (a == "--save-every" && more) o.saveEvery = std::max(std::atof(argv[++i]), 1.0);
        else if (a == "--seconds" && more) o.seconds = std::atof(argv[++i]);
        else if (a == "--ranks" && more) o.ranks = argv[++i];
        else if (a == "--no-console") o.noConsole = true;
        else std::printf("Aviso: no conozco la opcion %s\n", a.c_str());
    }
    return o;
}

// Las lineas que llegan por la entrada estandar. Se leen en otro hilo (getline bloquea) y las ejecuta
// el bucle principal, que es el unico que toca la partida.
class StdinLines {
public:
    void Start() {
        std::thread([this] {
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // la marca UTF-8 que algunas terminales ponen delante
                std::lock_guard<std::mutex> lock(mutex_);
                lines_.push_back(line);
            }
            // Fin de la entrada (se cerro la tuberia): el servidor sigue; se para con Ctrl+C.
        }).detach();
    }
    bool Next(std::string& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lines_.empty()) return false;
        out = std::move(lines_.front());
        lines_.pop_front();
        return true;
    }

private:
    std::mutex mutex_;
    std::deque<std::string> lines_;
};

std::vector<std::string> Words(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    return words;
}

// Lo que sigue a las n primeras palabras de la linea, tal cual.
std::string After(const std::string& line, int n) {
    std::size_t pos = 0;
    for (int i = 0; i < n; ++i) {
        pos = line.find_first_not_of(" \t", pos);
        if (pos == std::string::npos) return {};
        pos = line.find_first_of(" \t", pos);
        if (pos == std::string::npos) return {};
    }
    const std::size_t start = line.find_first_not_of(" \t", pos);
    return start == std::string::npos ? std::string() : line.substr(start);
}

}  // namespace

int main(int argc, char** argv) {
    Options options = Parse(argc, argv);
    if (!NetworkAvailable()) {
        std::printf("Este servidor se compilo sin red (falta ENet).\n");
        return 1;
    }
    const Balance rules;

    // La partida guardada, si la hay, decide la galaxia.
    std::optional<WorldSave> saved;
    if (!options.world.empty()) {
        std::string problem;
        saved = LoadWorld(options.world, &problem);
        if (!saved && problem != "sin partida guardada") {
            std::printf("No se pudo leer la partida de %s: %s\n", options.world.c_str(), problem.c_str());
            return 1;
        }
        if (saved) options.seed = saved->seed;
    }

    EngineConfig config;
    config.galaxy.seed = options.seed;
    config.galaxy.systems = rules.world.systems;
    config.galaxy.starSpacing = rules.world.starSpacing;
    config.galaxy.streaming = true;
    config.galaxy.blackHoles = true;
    config.galaxy.nebulae = 0;
    Engine engine(config);
    GameSession session(engine, rules, options.seed, static_cast<GameModeId>(options.mode));
    session.SetEngineFactory([&] { return std::make_unique<Engine>(config); });
    session.Step(GameSession::kFixedStep, {});  // carga el primer sistema

    // Los comandos del juego, los mismos que en el juego con ventana (game/Console.cpp).
    TargetRef target;
    RegisterGameCommands(engine.Commands(), session, target);
    RankBook ranks;
    std::string ranksFile = options.ranks;
    if (ranksFile.empty() && !options.world.empty()) ranksFile = (std::filesystem::path(options.world) / "rangos.txt").string();
    if (!ranksFile.empty()) {
        std::string error;
        if (!ranks.Load(ranksFile, &error)) {
            std::printf("No se pudieron leer las categorias de %s: %s\n", ranksFile.c_str(), error.c_str());
            return 1;
        }
        std::printf("Categorias del personal: %zu (%s)\n", ranks.All().size(), ranksFile.c_str());
    }

    GameServer host(session);
    if (saved) {
        std::string problem;
        if (!session.RestoreWorld(*saved, &problem)) {
            std::printf("No se pudo cargar la partida: %s\n", problem.c_str());
            return 1;
        }
        std::printf("Partida de %s: dia %.1f, %zu naves, %zu estaciones\n", options.world.c_str(), saved->simTime / 86400.0, saved->ships.size(), saved->stations.size());
    } else {
        // Partida nueva: la nave de salida espera, fuera del mundo, al primero que entre.
        if (options.noTutorial) session.Conversation().Stop();
        session.PlayerLeft(0);
        std::printf("Partida nueva: semilla %u, modo %s\n", options.seed, ModeInfo(session.Mode()).name);
    }
    if (!options.world.empty()) host.SetSaveFolder(options.world);

    std::string problem;
    if (!host.OpenToNetwork(options.port, options.name, options.players, &problem)) {
        std::printf("No se pudo abrir el puerto %d: %s\n", options.port, problem.c_str());
        return 1;
    }
    std::printf("Servidor \"%s\" en el puerto %d (hasta %d jugadores). Ctrl+C para cerrar.\n", options.name.c_str(), options.port, options.players);
    std::fflush(stdout);
    std::signal(SIGINT, OnInterrupt);

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start, lastSave = start;
    int connected = -1;
    const auto save = [&](const char* why) {
        if (options.world.empty()) return;
        std::string error;
        if (host.Save(&error)) std::printf("Guardado (%s): dia %.2f\n", why, session.WorldTime() / 86400.0);
        else std::printf("Aviso: no se pudo guardar: %s\n", error.c_str());
        std::fflush(stdout);
        lastSave = Clock::now();
    };

    // ---- La consola: una linea de la entrada estandar ----
    const auto say = [](const std::string& text) {
        std::printf("%s\n", text.c_str());
        std::fflush(stdout);
    };
    const auto rankOf = [&](const std::string& id) { return ranks.Of(id); };
    const auto handle = [&](const std::string& line) {
        const std::vector<std::string> w = Words(line);
        if (w.empty()) return;
        if (w[0] == "ayuda") {
            say("Consola del servidor. Aqui eres el dueño: vale cualquier comando del juego (saltar, creativo, nivel, aparecer, tiempo...).\n"
                "  jugadores | rangos | guardar | salir\n"
                "  rango <id> <jugador|moderador|administrador|dueño|desarrollador>\n"
                "  como <id> <orden...>   la orden con la categoria de ese jugador (sin categoria de personal, se rechaza)");
        } else if (w[0] == "salir" || w[0] == "parar") {
            gStop = true;
        } else if (w[0] == "guardar") {
            if (options.world.empty()) say("Este servidor no guarda: falta --world.");
            else save("a peticion");
        } else if (w[0] == "jugadores") {
            std::string text = "Jugadores:";
            for (int p = 0; p < session.PlayerCount(); ++p) {
                const PlayerState& player = session.PlayerAt(p);
                text += "\n  " + std::to_string(p + 1) + " " + player.name + " [" + player.id + "] " + Name(rankOf(player.id)) + (player.connected ? "" : " (fuera)");
            }
            say(text);
        } else if (w[0] == "rangos") {
            std::string text = "Personal:";
            for (const auto& [id, rank] : ranks.All()) text += "\n  " + id + " " + Name(rank);
            say(ranks.All().empty() ? "Personal: nadie" : text);
        } else if (w[0] == "rango") {
            Rank rank = Rank::Player;
            if (w.size() != 3 || !ParseRank(w[2], rank)) {
                say("Uso: rango <id> <jugador|moderador|administrador|dueño|desarrollador>");
                return;
            }
            ranks.Set(w[1], rank);
            say(w[1] + ": " + Name(rank));
            if (!ranksFile.empty()) {
                std::string error;
                std::error_code ignored;
                const std::filesystem::path folder = std::filesystem::path(ranksFile).parent_path();
                if (!folder.empty()) std::filesystem::create_directories(folder, ignored);
                if (!ranks.Save(ranksFile, &error)) say("Aviso: no se pudieron guardar las categorias: " + error);
            }
        } else if (w[0] == "como") {
            if (w.size() < 3) {
                say("Uso: como <id> <orden...>");
                return;
            }
            const Rank rank = rankOf(w[1]);
            bool allowed = false;
            const std::string answer = RunCommand(engine.Commands(), rank, After(line, 2), &allowed);
            say("[" + w[1] + ", " + Name(rank) + "] " + answer);
        } else {
            say(RunCommand(engine.Commands(), Rank::Owner, line));
        }
    };
    StdinLines input;
    if (!options.noConsole) input.Start();

    while (!gStop) {
        for (std::string line; input.Next(line);) handle(line);
        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - last).count();
        last = now;
        host.Update(elapsed);
        if (host.Clients() != connected) {
            connected = host.Clients();
            std::printf("[%.0f s] %d jugador%s, %d nave%s, %d burbuja%s\n", std::chrono::duration<double>(now - start).count(), connected, connected == 1 ? "" : "es", session.VesselCount(),
                        session.VesselCount() == 1 ? "" : "s", session.BubbleCount(), session.BubbleCount() == 1 ? "" : "s");
            std::fflush(stdout);
            if (connected == 0 && host.Tick() > 0) save("se ha ido el ultimo");
        }
        // Se guarda cuando la partida lo pide (una estacion, un atraque...) y cada cierto tiempo si hay alguien.
        const bool wanted = session.TakeSaveRequest();
        if (wanted || (connected > 0 && std::chrono::duration<double>(now - lastSave).count() >= options.saveEvery)) save(wanted ? "la partida lo pide" : "periodico");
        if (options.seconds > 0.0 && std::chrono::duration<double>(now - start).count() >= options.seconds) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    save("al cerrar");
    std::printf("Servidor cerrado tras %llu pasos.\n", static_cast<unsigned long long>(host.Tick()));
    return 0;
}
