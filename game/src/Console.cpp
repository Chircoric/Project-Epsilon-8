// Console.cpp - Los comandos de consola del juego y las categorias de quien puede usarlos.
#include "game/Console.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "game/Tutorial.h"

namespace game {

using namespace eps;
using Kind = TargetRef::Kind;

namespace {

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

}  // namespace

const char* Name(Rank rank) {
    switch (rank) {
        case Rank::Player: return "jugador";
        case Rank::Moderator: return "moderador";
        case Rank::Admin: return "administrador";
        case Rank::Owner: return "dueño";
        case Rank::Developer: return "desarrollador";
    }
    return "jugador";
}

bool ParseRank(const std::string& word, Rank& out) {
    const std::string w = Lower(word);
    if (w == "jugador" || w == "player") out = Rank::Player;
    else if (w == "moderador" || w == "mod") out = Rank::Moderator;
    else if (w == "administrador" || w == "admin") out = Rank::Admin;
    else if (w == "dueño" || w == "dueno" || w == "owner") out = Rank::Owner;
    else if (w == "desarrollador" || w == "dev" || w == "developer") out = Rank::Developer;
    else return false;
    return true;
}

Rank RankBook::Of(const std::string& playerId) const {
    const auto it = ranks_.find(playerId);
    return it == ranks_.end() ? Rank::Player : it->second;
}

void RankBook::Set(const std::string& playerId, Rank rank) {
    if (rank == Rank::Player) ranks_.erase(playerId);
    else ranks_[playerId] = rank;
}

bool RankBook::Load(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) return true;  // sin archivo, nadie tiene categoria
    std::map<std::string, Rank> loaded;
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream row(line);
        std::string id, word;
        if (!(row >> id)) continue;
        Rank rank = Rank::Player;
        if (!(row >> word) || !ParseRank(word, rank)) {
            if (error) *error = "linea " + std::to_string(number) + ": categoria desconocida";
            return false;
        }
        if (rank != Rank::Player) loaded[id] = rank;
    }
    ranks_ = std::move(loaded);
    return true;
}

bool RankBook::Save(const std::string& path, std::string* error) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "no se pudo escribir " + path;
        return false;
    }
    out << "# Categorias del personal: <id del jugador> <moderador|administrador|dueño|desarrollador>\n";
    for (const auto& [id, rank] : ranks_) out << id << ' ' << Name(rank) << '\n';
    return static_cast<bool>(out);
}

std::string RunCommand(CommandRegistry& commands, Rank rank, const std::string& line, bool* allowed) {
    if (allowed) *allowed = CanUseCommands(rank);
    if (!CanUseCommands(rank)) return "No tienes permiso para usar comandos: hace falta ser moderador, administrador, dueño o desarrollador.";
    return commands.Execute(line);
}

// Lo que se puede escribir en la consola o pasar con --cmd.
void RegisterGameCommands(CommandRegistry& commands, GameSession& g, TargetRef& target) {
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
            const bool armor = word == "coraza" && IsArmorPlanet(a);
            if (structure || hostile || neutral || (bastion && !IsArmorPlanet(a)) || haven || armor) consider({Kind::Anomaly, a.system, a.id});
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
    commands.Add("eclipse", "pone la nave en la sombra del planeta objetivo, de espaldas al sol, para probar: eclipse [km]", [&, findContact](const CommandRegistry::Args& args) -> std::string {
        const std::optional<TargetInfo> planet = g.Resolve(target);
        const std::optional<TargetRef> starRef = findContact("estrella");
        const std::optional<TargetInfo> star = starRef ? g.Resolve(*starRef) : std::nullopt;
        if (!planet || !star) return commands.Fail("Hace falta un planeta como objetivo y una estrella");
        const double km = args.empty() ? planet->arrival : std::atof(args[0].c_str());
        // JumpTo coloca la nave en la direccion en que ya esta: se la pone antes detras del planeta.
        g.Ship().position = planet->position + Normalize(planet->position - star->position) * km;
        g.JumpTo(target, km);
        const Vec3d toStar = star->position - g.Ship().position, toPlanet = planet->position - g.Ship().position;
        char line[160];
        std::snprintf(line, sizeof(line), " (sol a %.3g km, planeta a %.3g km, angulo %.1f grados)", Length(toStar), Length(toPlanet), std::acos(std::clamp(Dot(Normalize(toStar), Normalize(toPlanet)), -1.0, 1.0)) * 180.0 / kPi);
        return "En la sombra de " + planet->name + line;
    });
    // Las ordenes del jugador pasan por GameSession::Apply, como las de la interfaz. Lo que queda
    // llamando a la sesion directamente son trucos de prueba (saltar, regalo, nivel, aparecer...):
    // en red se ejecutaran en el servidor, con el texto del comando.
    using C = CommandType;
    // ---- Flota: varias naves en el mismo mundo (docs/multijugador.md, fase 3a) ----
    commands.Add("nave", "varias naves: nave (lista) | nave nueva [km] | nave ver <id> (pasa a llevarla; tambien F2) | nave <id> <orden...> (cualquier comando, para esa nave)",
                 [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) {
            std::string list = "Naves:";
            for (int i = 0; i < g.VesselCount(); ++i) {
                const Vessel& v = g.VesselAt(i);
                char line[160];
                std::snprintf(line, sizeof(line), " %d%s (%s, casco %.0f %%, burbuja %d, sistema %d, dia %.2f)", v.id, i == g.Focus() ? "*" : "", v.phase == GamePhase::Flying ? Name(v.ship.mode) : v.phase == GamePhase::Away ? "ausente" : "perdida",
                              100.0 * v.ship.hull / v.rules.hull, g.BubbleOf(i), v.nearestSystem, g.ClockOf(i) / 86400.0);
                list += line;
            }
            return list;
        }
        if (args[0] == "nueva") {
            // nave nueva [km] [clase 0-9] [variante 0-2]: sin clase, una de serie
            const int id = g.AddVessel(args.size() >= 2 ? std::atof(args[1].c_str()) : 20.0, args.size() >= 3 ? std::atoi(args[2].c_str()) : -1, args.size() >= 4 ? std::atoi(args[3].c_str()) : 0);
            target = TargetRef{Kind::Ship, g.NearestSystem(), id};
            return "Nave " + std::to_string(id) + " a tu costado; es el objetivo";
        }
        const bool look = args[0] == "ver";
        if (look && args.size() < 2) return commands.Fail("Uso: nave ver <id>");
        const int index = g.IndexOfVessel(std::atoi(args[look ? 1 : 0].c_str()));
        if (index < 0) return commands.Fail("No hay ninguna nave con ese numero");
        if (look) {
            g.SetFocus(index);
            target = {};
            return "Llevas la nave " + std::to_string(g.VesselId());
        }
        if (args.size() < 2) return commands.Fail("Uso: nave <id> <orden...>");
        // El resto de la linea es un comando cualquiera, que se ejecuta con esa nave como la del foco.
        std::string line;
        for (std::size_t i = 1; i < args.size(); ++i) line += (i > 1 ? " " : "") + args[i];
        // El objetivo es uno solo para toda la consola: "nave 2 objetivo planeta" y luego "nave 2 ir".
        const int back = g.Focus();
        g.SetFocus(index);
        const std::string answer = commands.Execute(line);
        g.SetFocus(back);
        return "[nave " + args[0] + "] " + answer;
    });
    // ---- Tripulacion: varios jugadores en la misma nave (fase 3b) ----
    const auto seatArg = [](const std::string& w) {
        if (w == "pilotaje" || w == "piloto") return Seat::Pilot;
        if (w == "armas" || w == "tactico") return Seat::Tactical;
        if (w == "ingenieria") return Seat::Engineering;
        return Seat::Count;
    };
    const auto seatsOf = [](const PlayerState& p) {
        std::string list;
        for (int s = 0; s < static_cast<int>(Seat::Count); ++s)
            if ((p.seats & SeatBit(static_cast<Seat>(s))) != 0) list += (list.empty() ? "" : ", ") + std::string(Name(static_cast<Seat>(s)));
        return list.empty() ? std::string("sin puesto") : list;
    };
    commands.Add("jugador", "varios jugadores: jugador (lista) | jugador nuevo [nombre] (entra en tu nave) | jugador ver <n> (pasas a ser el; tambien F3) | jugador <n> <orden...>",
                 [&, seatsOf](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) {
            std::string list = "Jugadores:";
            for (int p = 0; p < g.PlayerCount(); ++p) {
                const PlayerState& player = g.PlayerAt(p);
                list += " " + std::to_string(p + 1) + (p == g.FocusPlayer() ? "*" : "") + " " + player.name + " (nave " + std::to_string(g.VesselAt(player.vessel).id) + ": " + (player.connected ? seatsOf(player) : std::string("desconectado")) + (player.onFoot ? ", a pie" : "") + ")";
            }
            return list;
        }
        if (args[0] == "nuevo") {
            std::string name;
            for (std::size_t i = 1; i < args.size(); ++i) name += (i > 1 ? " " : "") + args[i];
            const int p = g.AddPlayer(name);
            if (p < 0) return commands.Fail("No se pudo");
            return "Jugador " + std::to_string(p + 1) + ", " + g.PlayerAt(p).name + ": " + seatsOf(g.PlayerAt(p));
        }
        const bool look = args[0] == "ver";
        if (look && args.size() < 2) return commands.Fail("Uso: jugador ver <n>");
        const int player = std::atoi(args[look ? 1 : 0].c_str()) - 1;
        if (player < 0 || player >= g.PlayerCount()) return commands.Fail("No hay ningun jugador con ese numero");
        if (look) {
            g.SetFocusPlayer(player);
            return "Eres " + g.PlayerAt(player).name + ": " + seatsOf(g.PlayerAt(player));
        }
        if (args.size() < 2) return commands.Fail("Uso: jugador <n> <orden...>");
        std::string line;
        for (std::size_t i = 1; i < args.size(); ++i) line += (i > 1 ? " " : "") + args[i];
        const int back = g.FocusPlayer();
        g.SetFocusPlayer(player);
        const std::string answer = commands.Execute(line);
        g.SetFocusPlayer(back);
        return "[jugador " + args[0] + "] " + answer;
    });
    commands.Add("puesto", "tus puestos en la nave: puesto (los dice) | puesto <pilotaje|armas|ingenieria> (lo ocupas si esta libre) | puesto dejar <...>",
                 [&, seatArg, seatsOf](const CommandRegistry::Args& args) -> std::string {
        const int me = g.FocusPlayer();
        if (args.empty()) return seatsOf(g.PlayerAt(me));
        const bool leave = args[0] == "dejar";
        if (leave && args.size() < 2) return commands.Fail("Uso: puesto dejar <pilotaje|armas|ingenieria>");
        const Seat seat = seatArg(args[leave ? 1 : 0]);
        if (seat == Seat::Count) return commands.Fail("Los puestos son pilotaje, armas e ingenieria");
        if (leave) return g.LeaveSeat(me, seat) ? "Puesto libre. Te quedan: " + seatsOf(g.PlayerAt(me)) : commands.Fail("Ese puesto no es tuyo");
        if (g.TakeSeat(me, seat)) return "Tuyo. Llevas: " + seatsOf(g.PlayerAt(me));
        const int holder = g.SeatHolder(seat);
        return commands.Fail("Lo lleva " + (holder >= 0 ? g.PlayerAt(holder).name : std::string("otro")));
    });
    commands.Add("ir", "piloto automatico al objetivo", [&](const CommandRegistry::Args&) -> std::string { return g.Apply(Order(C::Autopilot, target)) ? "Rumbo fijado" : commands.Fail("Sin objetivo"); });
    commands.Add("drones", "manda los drones mineros al objetivo (planeta o campo)", [&](const CommandRegistry::Args&) -> std::string { return g.Apply(Order(C::DeployDrones, target)) ? "Drones fuera" : commands.Fail("No se pudo"); });
    commands.Add("recoger-drones", "los drones mineros vuelven a sus bahias", [&](const CommandRegistry::Args&) -> std::string {
        g.Apply(Order(C::RecallDrones));
        return "Vuelven";
    });
    commands.Add("extraer", "elemento que extraen los drones (numero atomico; 0 = a granel)", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Apply(Order(C::ExtractElement, std::atoi(args[0].c_str())));
        const int z = g.ExtractElement();
        return z > 0 ? std::string(astro::Periodic(z).name) : std::string("A granel");
    });
    commands.Add("elemento", "para probar: suma kg de un elemento al inventario (elemento <z> <kg>)", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.size() < 2) return commands.Fail("Uso: elemento <z> <kg>");
        g.Ship().elements.Add(std::atoi(args[0].c_str()), std::atof(args[1].c_str()));
        return "Hecho";
    });
    commands.Add("estacion", "suelta la estacion origami donde esta la nave", [&](const CommandRegistry::Args&) -> std::string { return g.Apply(Order(C::DeployShipyard)) ? "Estacion soltada" : commands.Fail("No se pudo"); });
    commands.Add("central", "despliega una central en el objetivo", [&](const CommandRegistry::Args&) -> std::string { return g.Apply(Order(C::DeployPower, target)) ? "Desplegada" : commands.Fail("No se pudo"); });
    commands.Add("usar", "recoge de una estacion, entrega material a la origami o explora",[&](const CommandRegistry::Args&) -> std::string { return g.Apply(Order(C::Interact, target)) ? "Hecho" : commands.Fail("No se pudo"); });
    commands.Add("motor", "motor impulso|crucero|hibrida|hiper", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: motor <modo>");
        const DriveMode mode = args[0] == "hiper" ? DriveMode::Hyper : args[0] == "hibrida" ? DriveMode::Warp : args[0] == "crucero" ? DriveMode::Cruise : DriveMode::Impulse;
        return g.Apply(Order(C::SetDrive, static_cast<int>(mode))) ? std::string(Name(mode)) : commands.Fail("No disponible");
    });
    commands.Add("analisis", "abre el analisis del objetivo", [&](const CommandRegistry::Args&) -> std::string {
        g.Apply(Order(C::AnalysisUsed));
        return "Hecho";
    });
    commands.Add("mando", "mando -100..100", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: mando <porcentaje>");
        ShipCommand order = Order(C::SetThrottle);
        order.amount = std::atof(args[0].c_str()) / 100.0;
        g.Apply(order);
        return args[0];
    });
    commands.Add("frenar", "como pulsar Espacio: mando a cero y la nave quieta", [&](const CommandRegistry::Args&) -> std::string {
        g.Apply(Order(C::HoldPosition));
        return "Frenando";
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
        return g.Apply(Order(C::StartScan)) ? "Barrido" : commands.Fail("No se pudo");
    });
    commands.Add("casco", "fija el casco, para probar", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().hull = std::atof(args[0].c_str());
        return "Hecho";
    });
    commands.Add("revelar", "analiza todo el sistema actual, para probar", [&](const CommandRegistry::Args&) -> std::string {
        g.MarkScanned(g.NearestSystem());
        return "Hecho";
    });
    commands.Add("sondeo", "para probar: distancia en UA a las anomalias frias de este sistema y alcance del escaner", [&](const CommandRegistry::Args&) -> std::string {
        const World& w = g.GetWorld();
        const int system = g.NearestSystem();
        const Vec3d center = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(system)]);
        char line[256];
        std::snprintf(line, sizeof(line), "Sistema %d: radio %.2f UA, nave a %.2f UA del centro, alcance %.2f UA", system, w.extents[static_cast<std::size_t>(system)] / astro::kAstronomicalUnit,
                      Length(g.Ship().position - center) / astro::kAstronomicalUnit, g.ScanRange() / astro::kAstronomicalUnit);
        std::string out = line;
        for (const Anomaly& a : g.Anomalies()) {
            if (a.system != system || !a.cold) continue;
            const Vec3d p = g.AnomalyPosition(a);
            std::snprintf(line, sizeof(line), "\n  %s: a %.2f UA de la nave, %.2f UA del centro, firma %d, %s", a.name.c_str(), Length(p - g.Ship().position) / astro::kAstronomicalUnit, Length(p - center) / astro::kAstronomicalUnit,
                          a.signature, a.revealed ? "revelada" : "oculta");
            out += line;
        }
        std::snprintf(line, sizeof(line), "\n  Eco hasta %.2f UA%s", g.EchoRange() / astro::kAstronomicalUnit, g.EchoGuaranteed() ? " (tutorial: pista exacta garantizada)" : "");
        out += line;
        if (const std::optional<Vec3d> echo = g.EchoPosition()) {
            const EchoHint& e = g.Echo();
            std::snprintf(line, sizeof(line), "\n  Pista: %.2f UA, rumbo %.0f, elevacion %.0f, error %.2f UA%s, el punto queda a %.2f UA", e.distance / astro::kAstronomicalUnit, e.bearing, e.elevation,
                          e.error / astro::kAstronomicalUnit, e.exact ? " (exacta)" : "", Length(*echo - g.Ship().position) / astro::kAstronomicalUnit);
            out += line;
        }
        if (g.Echo().weak > 0) out += "\n  Senal debil mas alla del alcance: nivel " + std::to_string(g.Echo().needTier);
        return out;
    });
    // ---- Modo creativo ----
    commands.Add("creativo", "modo creativo: invulnerable, todo sin fin, todos los motores y sistemas: creativo [si|no]", [&](const CommandRegistry::Args& args) -> std::string {
        const bool on = args.empty() ? !g.Creative() : (args[0] == "si" || args[0] == "1");
        g.SetCreative(on);
        return on ? "Creativo activado. Prueba: aparecer, viajar, emisores, nivel, tiempo" : "Creativo desactivado";
    });
    commands.Add("cinematica", "modo cinematica: la camara sigue cada impacto en el blanco, con tiempo lento (tecla N): cinematica [si|no]", [&](const CommandRegistry::Args& args) -> std::string {
        const bool on = args.empty() ? !g.Cinematic() : (args[0] == "si" || args[0] == "1");
        g.Apply(Order(C::Cinematic, on ? 1 : 0));
        return on ? "Cinematica activada" : "Cinematica desactivada";
    });
    commands.Add("aparecer", "crea delante de la nave: aparecer <hostil|neutral|bastion|bastion1..5|estacion|estructura|capital|coraza> [km] [cantidad]", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: aparecer <hostil|neutral|bastion|bastion1..5|estacion|estructura|capital|coraza> [km] [cantidad]");
        const std::string& what = args[0];
        AnomalyKind kind = AnomalyKind::AlienShip;
        Stance stance = Stance::Hostile;
        int level = 0, shipClass = -1;
        if (what == "neutral") stance = Stance::Neutral;
        else if (what == "bastion") kind = AnomalyKind::AlienStation;
        else if (what.size() == 8 && what.rfind("bastion", 0) == 0 && what[7] >= '1' && what[7] <= '5') {
            kind = AnomalyKind::AlienStation;
            level = what[7] - '0';
        } else if (what == "coraza") {
            kind = AnomalyKind::AlienStation;
            level = kArmorPlanetLevel;
        } else if (what == "capital") shipClass = static_cast<int>(ShipClassId::SupremeCapital);
        else if (what == "estacion") { kind = AnomalyKind::AlienStation; stance = Stance::Neutral; }
        else if (what == "estructura") kind = AnomalyKind::Derelict;
        else if (what != "hostil") return commands.Fail("No conozco eso");
        const double km = args.size() >= 2 ? std::atof(args[1].c_str()) : 2000.0;
        const int count = args.size() >= 3 ? std::clamp(std::atoi(args[2].c_str()), 1, 20) : 1;
        int id = -1;
        for (int i = 0; i < count; ++i) id = g.SpawnAnomaly(kind, stance, km * (1.0 + 0.15 * i), level, shipClass);
        if (id < 0) return commands.Fail("No se pudo");
        target = TargetRef{Kind::Anomaly, g.NearestSystem(), id};
        return std::to_string(count) + " creado(s); el ultimo es el objetivo";
    });
    commands.Add("alerta", "alerta de la especie en el sistema actual (o en otro: alerta <sistema>), y si hay rendicion", [&](const CommandRegistry::Args& args) -> std::string {
        const int system = args.empty() ? g.NearestSystem() : std::atoi(args[0].c_str());
        char line[160];
        std::snprintf(line, sizeof(line), "Sistema %d (region %d): alerta %.0f %%%s, revanchas %d", system, AlertRegion(system, g.Rules().alert), 100.0 * g.SystemAlert(system),
                      g.Surrender() ? ", flota rendida" : "", g.Species().revenges);
        return line;
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
        if (args.empty()) return Name(static_cast<ShieldKind>(g.Apply(Order(C::CycleShield)).count));
        if (args[0] == "no") {
            g.Apply(Order(C::ShieldOn, 0));
            return "Apagado";
        }
        ShieldKind kind = ShieldKind::Dust;
        if (!shieldArg(args[0], kind)) return commands.Fail("Uso: escudo <arena|plasma|em|amor|no>");
        return g.Apply(Order(C::SetShield, static_cast<int>(kind))) ? std::string(Name(kind)) : commands.Fail("No lo tienes o no es un campo");
    });
    commands.Add("calor", "muestra el calor; con un numero, lo fija (100 es el limite)", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().defense.heat = std::clamp(std::atof(args[0].c_str()), 0.0, g.Rules().thermal.maximum);
        return std::to_string(static_cast<int>(g.Ship().defense.heat));
    });
    commands.Add("refrigerante", "enciende o apaga uno: refrigerante <agua|hielo|nitrogeno|radiadores> <si|no>", [&, coolantArg](const CommandRegistry::Args& args) -> std::string {
        CoolantKind kind = CoolantKind::Water;
        if (args.size() < 2 || !coolantArg(args[0], kind)) return commands.Fail("Uso: refrigerante <agua|hielo|nitrogeno|radiadores> <si|no>");
        g.Apply(Order(C::Coolant, static_cast<int>(kind), args[1] == "si" || args[1] == "1" ? 1 : 0));
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
        if (weaponArg(args, kind)) return g.Apply(Order(C::Fire, target, static_cast<int>(kind))) ? "Disparo" : commands.Fail("No se pudo");
        const int fired = g.Apply(Order(C::FireAll, target)).count;
        return fired > 0 ? std::to_string(fired) + " armas" : commands.Fail("Nada listo");
    });
    commands.Add("apuntar", "gira el rumbo a la solucion de tiro: apuntar [torreta|espinal|misil|drones]", [&, weaponArg](const CommandRegistry::Args& args) -> std::string {
        WeaponKind kind = WeaponKind::Spinal;
        weaponArg(args, kind);
        return g.Apply(Order(C::Aim, target, static_cast<int>(kind))) ? "Apuntando" : commands.Fail("Sin solucion");
    });
    commands.Add("alinear", "el rumbo sigue la solucion del canon espinal, como B", [&](const CommandRegistry::Args&) -> std::string {
        return g.Apply(Order(C::Track, target)) ? "Alineando" : commands.Fail("Sin solucion");
    });
    commands.Add("astro", "fija como objetivo el astro numero N de este sistema, como sale en los datos", [&](const CommandRegistry::Args& args) -> std::string {
        if (args.empty()) return commands.Fail("Uso: astro <indice>");
        target = TargetRef{Kind::Planet, g.NearestSystem(), std::atoi(args[0].c_str())};
        const std::optional<TargetInfo> info = g.Resolve(target);
        return info ? "Objetivo: " + info->name : commands.Fail("No existe");
    });
    commands.Add("atracar","atraca en la estacion objetivo; con 'ya', sin esperar al haz", [&](const CommandRegistry::Args& args) -> std::string {
        return g.Apply(Order(C::Dock, target, !args.empty() && args[0] == "ya" ? 1 : 0)) ? "Atracando" : commands.Fail("No se pudo");
    });
    commands.Add("soltar", "suelta las amarras", [&](const CommandRegistry::Args&) -> std::string {
        g.Apply(Order(C::Undock));
        return "Hecho";
    });
    commands.Add("apie", "baja a la cubierta de la estacion donde esta atracada", [&](const CommandRegistry::Args&) -> std::string { return g.Apply(Order(C::Disembark)) ? "A pie" : commands.Fail("Hay que estar atracada"); });
    commands.Add("abordo", "vuelve a la nave desde cualquier punto de la cubierta", [&](const CommandRegistry::Args&) -> std::string { return g.Apply(Order(C::Board, 1)) ? "A bordo" : commands.Fail("No estas a pie"); });
    commands.Add("vistapie", "cambia entre primera y tercera persona", [&](const CommandRegistry::Args&) -> std::string {
        g.ToggleFootView();
        return g.FirstPerson() ? "Primera persona" : "Tercera persona";
    });
    commands.Add("computador","fija el nivel del computador de punteria, para probar", [&](const CommandRegistry::Args& args) -> std::string {
        if (!args.empty()) g.Ship().computerTier = std::atoi(args[0].c_str());
        return std::to_string(g.Ship().computerTier);
    });
    commands.Add("orbitar", "circulariza alrededor de lo que domina aqui", [&](const CommandRegistry::Args&) -> std::string { return g.Apply(Order(C::Orbit)) ? "En orbita" : commands.Fail("No se pudo"); });
    commands.Add("seguir", "sigue tras perder la nave", [&](const CommandRegistry::Args&) -> std::string {
        g.Apply(Order(C::Continue));
        return "Hecho";
    });
    commands.Add("tutorial", "salta a un paso del tutorial, con su objetivo en pantalla: tutorial <paso> [leer]; sin nada, dice en cual esta", [&](const CommandRegistry::Args& args) -> std::string {
        Dialogue& talk = g.Conversation();
        if (!args.empty()) {
            if (talk.StepCount() == 0) talk.Start(MakeTutorial());
            talk.Resume(static_cast<std::size_t>(std::max(std::atoi(args[0].c_str()), 0)));
            if (args.size() < 2 || args[1] != "leer") talk.SkipLines();  // con "leer", empieza por sus lineas
        }
        if (!talk.Active()) return "Tutorial terminado (" + std::to_string(talk.StepCount()) + " pasos)";
        return "Paso " + std::to_string(talk.Step()) + " de " + std::to_string(talk.StepCount()) + ": " + (talk.Current()->objective.empty() ? std::string("solo diálogo") : talk.Current()->objective);
    });
}

}  // namespace game