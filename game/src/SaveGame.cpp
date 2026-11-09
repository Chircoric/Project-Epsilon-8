#include "game/SaveGame.h"

#include "game/Defense.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>

namespace game {

using eps::Quatd;
using eps::Vec3d;

namespace {

constexpr const char* kHeader = "EPSILON-NEBULOSA";
constexpr std::size_t kResources = static_cast<std::size_t>(Resource::Count);

std::uint64_t Checksum(const std::string& text) {
    std::uint64_t h = 14695981039346656037ull;  // FNV-1a
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::ostream& operator<<(std::ostream& out, const Vec3d& v) { return out << v.x << ' ' << v.y << ' ' << v.z; }
std::istream& operator>>(std::istream& in, Vec3d& v) { return in >> v.x >> v.y >> v.z; }

void WriteInventory(std::ostream& out, const Inventory& inventory) {
    for (std::size_t r = 0; r < kResources; ++r) out << ' ' << inventory.Get(static_cast<Resource>(r));
}

// Hasta la version 3 habia un recurso mas al final, la materia oscura: se lee y se descarta.
Inventory ReadInventory(std::istream& in, int version) {
    Inventory inventory;
    const std::size_t stored = version < 4 ? kResources + 1 : kResources;
    for (std::size_t r = 0; r < stored; ++r) {
        double amount = 0;
        in >> amount;
        if (r < kResources) inventory.Add(static_cast<Resource>(r), amount);
    }
    return inventory;
}

// Numeros y, tras " | ", un nombre que puede llevar espacios.
void SplitName(const std::string& line, std::string& numbers, std::string& name) {
    const std::size_t bar = line.find(" | ");
    numbers = line.substr(0, bar);
    name = bar == std::string::npos ? "" : line.substr(bar + 3);
}

std::istringstream Numbers(const std::string& text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    return in;
}

}  // namespace

std::filesystem::path FilePath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

// Una partida de la version 1: la curvatura de entonces es el crucero de ahora, y las centrales
// guardaban energia en vez de antimateria.
void UpgradeFromFirstVersion(SaveData& d) {
    constexpr int kOldHyper = 2;
    constexpr int kFirstStepAfterCollecting = 7;  // aqui se anadio el paso de la curvatura
    if (static_cast<int>(d.ship.mode) == kOldHyper) d.ship.mode = DriveMode::Hyper;
    d.ship.warpCharge = 0.0;
    d.ship.antimatter = ShipRules{}.startAntimatter;
    for (Station& t : d.stations) {
        t.storedAntimatter = 0.0;
        t.yield.energyPerHour *= kCollectorScale;
    }
    if (d.tutorialStep >= kFirstStepAfterCollecting) ++d.tutorialStep;
}

std::string WriteSave(const SaveData& d) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(17);  // basta para que un double vuelva a ser exactamente el mismo

    const ShipState& s = d.ship;
    out << kHeader << ' ' << kSaveVersion << '\n';
    out << "semilla " << d.seed << '\n';
    out << "modo " << static_cast<int>(d.mode) << '\n';
    out << "tiempo " << d.simTime << '\n';
    out << "recogidas " << d.collected << '\n';
    out << "tutorial " << d.tutorialStep << '\n';
    out << "cursor " << (d.cursorUsed ? 1 : 0) << '\n';
    out << "posicion " << d.absolutePosition << '\n';
    out << "orientacion " << s.orientation.x << ' ' << s.orientation.y << ' ' << s.orientation.z << ' ' << s.orientation.w << '\n';
    out << "avance " << s.moveDirection << '\n';
    out << "motor " << static_cast<int>(s.mode) << ' ' << s.speed << ' ' << s.throttle << ' ' << s.warpCharge << '\n';
    out << "vuelo " << s.velocity << ' ' << s.warpSpeed << ' ' << s.warpHeading << ' ' << s.holding << '\n';
    out << "estado " << s.energy << ' ' << s.hull << ' ' << s.emitters << ' ' << s.shipHours << '\n';
    out << "zonas";
    for (const double z : s.zones) out << ' ' << z;
    out << '\n';
    out << "antimateria " << s.antimatter << ' ' << s.warpAge << '\n';
    out << "niveles " << s.heatShieldTier << ' ' << s.handlingTier << ' ' << s.scannerTier << ' ' << s.relayTier << '\n';
    out << "bodega";
    WriteInventory(out, s.cargo);
    out << '\n';
    out << "computador " << s.computerTier << '\n';
    out << "escudos " << static_cast<int>(s.defense.field) << ' ' << s.defense.fieldOn;
    for (const int t : s.defense.shieldTier) out << ' ' << t;
    for (const double c : s.defense.charge) out << ' ' << c;
    out << '\n';
    out << "calor " << s.defense.heat << ' ' << s.defense.water << ' ' << s.defense.nitrogen;
    for (const int t : s.defense.coolantTier) out << ' ' << t;
    for (const bool on : s.defense.coolantOn) out << ' ' << (on ? 1 : 0);
    out << '\n';
    // Las torretas de los modulos no se guardan aqui: salen de los modulos al cargar.
    std::size_t builtIn = 0;
    for (const WeaponMount& m : s.weapons) builtIn += m.slot < 0 ? 1 : 0;
    out << "armas " << builtIn;
    for (const WeaponMount& m : s.weapons)
        if (m.slot < 0) out << ' ' << static_cast<int>(m.kind) << ' ' << m.tier << ' ' << m.cooldown;
    out << '\n';
    out << "modulos " << s.loadout.engine << ' ' << s.loadout.modules.size();
    for (const ShipModuleKind k : s.loadout.modules) out << ' ' << static_cast<int>(k);
    out << '\n';
    out << "piezas " << s.pieces.size();
    for (const double p : s.pieces) out << ' ' << p;
    out << '\n';
    out << "orbita " << d.orbitKind << ' ' << d.orbitSystem << ' ' << d.orbitIndex << ' ' << d.orbitOffset << '\n';
    out << "plano " << d.orbitNormal << ' ' << d.orbitRate << '\n';
    out << "atraque " << d.docked << '\n';

    out << "sistemas";
    for (std::size_t i = 0; i < d.knownSystems.size(); ++i)
        if (d.knownSystems[i]) out << ' ' << i;
    out << '\n';

    out << "visitados";
    for (std::size_t i = 0; i < d.visitedSystems.size(); ++i)
        if (d.visitedSystems[i]) out << ' ' << i;
    out << '\n';

    out << "analizados";
    for (const std::uint64_t key : d.scanned) out << ' ' << key;
    out << '\n';

    out << "proxima-estacion " << d.nextStation << '\n';
    for (const Station& t : d.stations) {
        out << "estacion " << t.id << ' ' << static_cast<int>(t.kind) << ' ' << t.site.system << ' ' << t.site.body << ' ' << t.site.belt << ' ' << t.site.family << ' ' << t.site.star << ' ' << t.angle << ' ' << t.storedAntimatter;
        WriteInventory(out, t.stored);
        for (const double share : t.deposit.share) out << ' ' << share;
        out << ' ' << t.deposit.richness << ' ' << t.yield.energyPerHour << ' ' << t.yield.matterPerHour << ' ' << t.yield.requiredTier << ' ' << t.yield.orbitRadius;
        out << ' ' << t.modules.size();
        for (const Module& m : t.modules) out << ' ' << static_cast<int>(m.kind) << ' ' << m.level;
        out << " | " << t.name << '\n';
    }
    for (const Anomaly& a : d.anomalies) {
        out << "anomalia " << a.id << ' ' << static_cast<int>(a.kind) << ' ' << static_cast<int>(a.stance) << ' ' << a.system << ' ' << a.offset << ' ' << a.home << ' ' << a.velocity << ' ' << a.yaw << ' ' << a.radius << ' ' << (a.emitter ? 1 : 0) << ' ' << a.explored;
        out << ' ' << a.pursuing << ' ' << a.discovered << ' ' << a.revealed << ' ' << a.signature << ' ' << a.cold << " | " << a.name << '\n';
        out << "combate " << a.id << ' ' << a.hull << ' ' << a.maxHull << ' ' << a.armed << ' ' << static_cast<int>(a.weapon) << ' ' << a.weaponTier << ' ' << a.computerTier << ' ' << a.cooldown;
        out << ' ' << static_cast<int>(a.shield) << ' ' << a.shieldCharge << ' ' << a.shieldMax << '\n';
    }

    std::string text = out.str();
    char sum[32];
    std::snprintf(sum, sizeof(sum), "suma %016llx\n", static_cast<unsigned long long>(Checksum(text)));
    return text + sum;
}

std::optional<SaveData> ReadSave(const std::string& text, std::string* error) {
    const auto fail = [&](const char* why) -> std::optional<SaveData> {
        if (error) *error = why;
        return std::nullopt;
    };

    // La suma cubre todo lo que hay antes de su propia linea.
    const std::size_t last = text.rfind("suma ");
    if (last == std::string::npos) return fail("archivo incompleto");
    const unsigned long long stored = std::strtoull(text.c_str() + last + 5, nullptr, 16);
    if (stored != Checksum(text.substr(0, last))) return fail("archivo danado");

    std::istringstream lines(text.substr(0, last));
    std::string line;
    if (!std::getline(lines, line)) return fail("archivo vacio");
    int version = 0;
    {
        std::istringstream head = Numbers(line);
        std::string name;
        head >> name >> version;
        if (name != kHeader) return fail("no es una partida");
        if (version > kSaveVersion || version < 1) return fail("version desconocida");
    }

    SaveData d;
    while (std::getline(lines, line)) {
        std::string body, name;
        SplitName(line, body, name);
        std::istringstream in = Numbers(body);
        std::string key;
        in >> key;
        ShipState& s = d.ship;
        int number = 0;

        if (key == "semilla") in >> d.seed;
        else if (key == "modo") {
            in >> number;
            d.mode = static_cast<GameModeId>(number);
        } else if (key == "tiempo") in >> d.simTime;
        else if (key == "recogidas") in >> d.collected;
        else if (key == "tutorial") in >> d.tutorialStep;
        else if (key == "cursor") in >> d.cursorUsed;
        else if (key == "posicion") in >> d.absolutePosition;
        else if (key == "orientacion") {
            in >> s.orientation.x >> s.orientation.y >> s.orientation.z >> s.orientation.w;
            s.aim = s.orientation;
        } else if (key == "avance") in >> s.moveDirection;
        else if (key == "motor") {
            in >> number >> s.speed >> s.throttle >> s.warpCharge;
            s.mode = static_cast<DriveMode>(number);
        } else if (key == "vuelo") in >> s.velocity >> s.warpSpeed >> s.warpHeading >> s.holding;
        else if (key == "plano") in >> d.orbitNormal >> d.orbitRate;
        else if (key == "atraque") in >> d.docked;
        else if (key == "estado") {
            // Antes era la materia oscura reunida: haberla encontrado equivale al primer emisor.
            double found = 0;
            in >> s.energy >> s.hull >> found >> s.shipHours;
            s.emitters = version < 4 ? (found > 0.0 ? 1 : 0) : static_cast<int>(found);
        }
        else if (key == "modulos") {  // sin esta linea, motor de serie y nada montado
            std::size_t count = 0;
            in >> s.loadout.engine >> count;
            s.loadout.modules.clear();
            for (std::size_t i = 0; i < count && in; ++i) {
                int kind = 0;
                in >> kind;
                s.loadout.modules.push_back(kind > 0 && kind < static_cast<int>(ShipModuleKind::Count) ? static_cast<ShipModuleKind>(kind) : ShipModuleKind::None);
            }
        } else if (key == "piezas") {
            std::size_t count = 0;
            in >> count;
            s.pieces.clear();
            for (std::size_t i = 0; i < count && in; ++i) {
                double v = 1.0;
                in >> v;
                s.pieces.push_back(std::clamp(v, 0.0, 1.0));
            }
        }
        else if (key == "zonas") {  // sin esta linea (partidas anteriores), el casco esta entero
            for (double& z : s.zones) {
                double v = 1.0;
                if (!(in >> v)) break;
                z = std::clamp(v, 0.0, 1.0);
            }
        }
        else if (key == "antimateria") in >> s.antimatter >> s.warpAge;
        else if (key == "niveles") in >> s.heatShieldTier >> s.handlingTier >> s.scannerTier >> s.relayTier;
        else if (key == "bodega") s.cargo = ReadInventory(in, version);
        else if (key == "computador") in >> s.computerTier;
        else if (key == "escudos") {
            DefenseState& e = s.defense;
            int field = 0;
            in >> field >> e.fieldOn;
            if (field < 0 || field >= static_cast<int>(ShieldKind::Count)) return fail("escudo desconocido");
            e.field = static_cast<ShieldKind>(field);
            for (int& t : e.shieldTier) in >> t;
            for (double& c : e.charge) in >> c;
        } else if (key == "calor") {
            DefenseState& e = s.defense;
            in >> e.heat >> e.water >> e.nitrogen;
            for (int& t : e.coolantTier) in >> t;
            for (std::size_t i = 0; i < e.coolantOn.size(); ++i) {
                int on = 1;
                in >> on;
                e.coolantOn[i] = on != 0;
            }
        }
        else if (key == "armas") {
            std::size_t count = 0;
            in >> count;
            s.weapons.clear();
            for (std::size_t i = 0; i < count && in; ++i) {
                WeaponMount m;
                int kind = 0;
                in >> kind >> m.tier >> m.cooldown;
                if (kind < 0 || kind >= static_cast<int>(WeaponKind::Count)) return fail("arma desconocida");
                m.kind = static_cast<WeaponKind>(kind);
                s.weapons.push_back(m);
            }
        } else if (key == "combate") {
            int id = 0, weapon = 0;
            in >> id;
            for (Anomaly& a : d.anomalies) {
                if (a.id != id) continue;
                in >> a.hull >> a.maxHull >> a.armed >> weapon >> a.weaponTier >> a.computerTier >> a.cooldown;
                if (weapon >= 0 && weapon < static_cast<int>(WeaponKind::Count)) a.weapon = static_cast<WeaponKind>(weapon);
                if (version >= 5) {
                    int shield = 0;
                    in >> shield >> a.shieldCharge >> a.shieldMax;
                    if (shield >= 0 && shield < static_cast<int>(ShieldKind::Count)) a.shield = static_cast<ShieldKind>(shield);
                }
                break;
            }
        }
        else if (key == "orbita") in >> d.orbitKind >> d.orbitSystem >> d.orbitIndex >> d.orbitOffset;
        else if (key == "sistemas") {
            std::size_t index = 0;
            while (in >> index) {
                if (index >= d.knownSystems.size()) d.knownSystems.resize(index + 1, 0);
                d.knownSystems[index] = 1;
            }
            in.clear();
        } else if (key == "visitados") {
            std::size_t index = 0;
            while (in >> index) {
                if (index >= d.visitedSystems.size()) d.visitedSystems.resize(index + 1, 0);
                d.visitedSystems[index] = 1;
            }
            in.clear();
        } else if (key == "analizados") {
            std::uint64_t scanned = 0;
            while (in >> scanned) d.scanned.push_back(scanned);
            in.clear();
        } else if (key == "proxima-estacion") in >> d.nextStation;
        else if (key == "estacion") {
            Station t;
            t.name = name;
            int kind = 0;
            in >> t.id >> kind >> t.site.system >> t.site.body >> t.site.belt >> t.site.family >> t.site.star >> t.angle >> t.storedAntimatter;
            t.kind = static_cast<StationKind>(kind);
            t.stored = ReadInventory(in, version);
            for (double& share : t.deposit.share) in >> share;
            if (version < 4) {
                double darkShare = 0;
                in >> darkShare;
            }
            in >> t.deposit.richness >> t.yield.energyPerHour >> t.yield.matterPerHour >> t.yield.requiredTier >> t.yield.orbitRadius;
            std::size_t modules = 0;
            in >> modules;
            for (std::size_t m = 0; m < modules && in; ++m) {
                Module module;
                int moduleKind = 0;
                in >> moduleKind >> module.level;
                module.kind = static_cast<ModuleKind>(moduleKind);
                t.modules.push_back(module);
            }
            d.stations.push_back(std::move(t));
        } else if (key == "anomalia") {
            Anomaly a;
            a.name = name;
            int kind = 0, stance = 0;
            double inside = 0;  // antes, la materia oscura que quedaba; ahora, 1 si guarda su emisor
            in >> a.id >> kind >> stance >> a.system >> a.offset >> a.home >> a.velocity >> a.yaw >> a.radius >> inside >> a.explored;
            a.emitter = inside > 0.0;
            in >> a.pursuing >> a.discovered >> a.revealed >> a.signature >> a.cold;
            a.kind = static_cast<AnomalyKind>(kind);
            a.stance = static_cast<Stance>(stance);
            d.anomalies.push_back(std::move(a));
        }
        // Una linea desconocida se ignora: puede venir de una version posterior.
        if (in.fail()) return fail("dato ilegible");
    }
    if (d.seed == 0) return fail("sin semilla");
    if (version < 2) UpgradeFromFirstVersion(d);
    // Una estructura empezada antes ya dio su materia oscura, que ahora es el primer emisor.
    if (version < 4)
        for (Anomaly& a : d.anomalies)
            if (a.explored > 0.0) a.emitter = false;
    // Antes no habia escudos ni calor: se sale con los de serie, llenos. Los alienigenas los
    // reciben al cargar la partida (GameSession::Restore).
    if (version < 5) d.ship.defense = MakeDefense(DefenseRules{}, ThermalRules{});
    return d;
}

bool SaveToFile(const std::string& path, const SaveData& data, std::string* error) {
    namespace fs = std::filesystem;
    const fs::path file = FilePath(path);
    fs::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << WriteSave(data);
        if (!out.flush()) {
            if (error) *error = "no se pudo escribir";
            return false;
        }
    }
    std::error_code problem;
    fs::rename(temporary, file, problem);
    if (problem) {
        if (error) *error = problem.message();
        fs::remove(temporary, problem);
        return false;
    }
    return true;
}

std::optional<SaveData> LoadFromFile(const std::string& path, std::string* error) {
    std::ifstream in(FilePath(path), std::ios::binary);
    if (!in) {
        if (error) *error = "sin partida guardada";
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return ReadSave(text.str(), error);
}

}  // namespace game
