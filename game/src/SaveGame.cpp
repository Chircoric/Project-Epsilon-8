#include "game/SaveGame.h"

#include "game/Defense.h"

#include <algorithm>
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

constexpr const char* kOldHeader = "EPSILON-NEBULOSA";  // hasta la version 12: todo en un archivo
constexpr const char* kWorldHeader = "EPSILON-NEBULOSA-MUNDO";
constexpr const char* kPlayerHeader = "EPSILON-NEBULOSA-JUGADOR";
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

Inventory ReadInventory(std::istream& in) {
    Inventory inventory;
    for (std::size_t r = 0; r < kResources; ++r) {
        double amount = 0;
        in >> amount;
        inventory.Add(static_cast<Resource>(r), amount);
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

std::ostringstream Writer() {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(17);  // basta para que un double vuelva a ser exactamente el mismo
    return out;
}

// Cierra un documento con su suma de control.
std::string Sealed(const std::string& text) {
    char sum[32];
    std::snprintf(sum, sizeof(sum), "suma %016llx\n", static_cast<unsigned long long>(Checksum(text)));
    return text + sum;
}

// Comprueba la suma, la cabecera y la version de un documento, y deja en body sus lineas de datos.
bool Open(const std::string& text, const char* header, std::string& body, std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    // La suma cubre todo lo que hay antes de su propia linea.
    const std::size_t last = text.rfind("suma ");
    if (last == std::string::npos) return fail("archivo incompleto");
    const unsigned long long stored = std::strtoull(text.c_str() + last + 5, nullptr, 16);
    if (stored != Checksum(text.substr(0, last))) return fail("archivo danado");

    const std::size_t end = text.find('\n');
    if (end == std::string::npos || end > last) return fail("archivo vacio");
    std::istringstream head = Numbers(text.substr(0, end));
    std::string name;
    int version = 0;
    head >> name >> version;
    const bool old = name == kOldHeader;  // una partida de antes de separar mundo y jugador
    if (name != header && !old) return fail("no es una partida");
    // Solo la version actual: las partidas de antes no se migran.
    if (version < kSaveVersion || old)
        return fail("partida de la versión " + std::to_string(version) + ", anterior a la actual (" + std::to_string(kSaveVersion) + "). Ya no se puede cargar: empiece una misión nueva.");
    if (version > kSaveVersion) return fail("partida de una versión posterior (" + std::to_string(version) + "): actualice el juego para cargarla.");
    body = text.substr(end + 1, last - end - 1);
    return true;
}

void WriteShip(std::ostream& out, const ShipSave& v) {
    const ShipState& s = v.ship;
    // Todo lo que sigue a esta linea, hasta la siguiente "nave", es de esta.
    out << "nave " << v.id << '\n';
    out << "posicion " << v.absolutePosition << '\n';
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
    // Los elementos, solo los que hay: pares "numero atomico kg".
    out << "elementos";
    for (int z = 1; z <= ElementStock::kCount; ++z)
        if (s.elements.Get(z) > 0.0) out << ' ' << z << ' ' << s.elements.Get(z);
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
    // Armamento humano: misiles de cada bahia, planos fabricados, defensa puntual y bahias de serie.
    out << "municion " << s.loadout.ammo.size();
    for (const int a : s.loadout.ammo) out << ' ' << a;
    out << '\n';
    out << "armamento " << s.loadout.blueprints << ' ' << s.loadout.pointDefense << ' ' << s.loadout.starterBays << '\n';
    out << "origami-a-bordo " << (s.stationAboard ? 1 : 0) << '\n';
    out << "piezas " << s.pieces.size();
    for (const double p : s.pieces) out << ' ' << p;
    out << '\n';
    out << "orbita " << v.orbitKind << ' ' << v.orbitSystem << ' ' << v.orbitIndex << ' ' << v.orbitOffset << '\n';
    out << "plano " << v.orbitNormal << ' ' << v.orbitRate << '\n';
    out << "atraque " << v.docked << '\n';
    out << "base " << v.homeStation << ' ' << (v.lost ? 1 : 0) << '\n';
    out << "reloj " << v.clock << '\n';
    if (v.away) out << "ausente 1\n";  // solo si lo esta: su jugador no estaba conectado
}

// Una linea de una nave. Devuelve false si la clave no es de nave; error se rellena si el dato no vale.
bool ReadShipLine(const std::string& key, std::istream& in, ShipSave& v, std::string& error) {
    ShipState& s = v.ship;
    int number = 0;
    if (key == "posicion") in >> v.absolutePosition;
    else if (key == "orientacion") {
        in >> s.orientation.x >> s.orientation.y >> s.orientation.z >> s.orientation.w;
        s.aim = s.orientation;
    } else if (key == "avance") in >> s.moveDirection;
    else if (key == "motor") {
        in >> number >> s.speed >> s.throttle >> s.warpCharge;
        s.mode = static_cast<DriveMode>(number);
    } else if (key == "vuelo") in >> s.velocity >> s.warpSpeed >> s.warpHeading >> s.holding;
    else if (key == "estado") in >> s.energy >> s.hull >> s.emitters >> s.shipHours;
    else if (key == "zonas") {
        for (double& z : s.zones) {
            double value = 1.0;
            if (!(in >> value)) break;
            z = std::clamp(value, 0.0, 1.0);
        }
    } else if (key == "antimateria") in >> s.antimatter >> s.warpAge;
    else if (key == "niveles") in >> s.heatShieldTier >> s.handlingTier >> s.scannerTier >> s.relayTier;
    else if (key == "bodega") s.cargo = ReadInventory(in);
    else if (key == "elementos") {
        s.elements = {};
        int z = 0;
        double kilograms = 0;
        while (in >> z >> kilograms) s.elements.Add(z, kilograms);
        in.clear();
    } else if (key == "computador") in >> s.computerTier;
    else if (key == "escudos") {
        DefenseState& e = s.defense;
        int field = 0;
        in >> field >> e.fieldOn;
        if (field < 0 || field >= static_cast<int>(ShieldKind::Count)) {
            error = "escudo desconocido";
            return true;
        }
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
    } else if (key == "armas") {
        std::size_t count = 0;
        in >> count;
        s.weapons.clear();
        for (std::size_t i = 0; i < count && in; ++i) {
            WeaponMount m;
            int kind = 0;
            in >> kind >> m.tier >> m.cooldown;
            if (kind < 0 || kind >= static_cast<int>(WeaponKind::Count)) {
                error = "arma desconocida";
                return true;
            }
            m.kind = static_cast<WeaponKind>(kind);
            s.weapons.push_back(m);
        }
    } else if (key == "modulos") {
        std::size_t count = 0;
        in >> s.loadout.engine >> count;
        s.loadout.modules.clear();
        for (std::size_t i = 0; i < count && in; ++i) {
            int kind = 0;
            in >> kind;
            s.loadout.modules.push_back(kind > 0 && kind < static_cast<int>(ShipModuleKind::Count) ? static_cast<ShipModuleKind>(kind) : ShipModuleKind::None);
        }
    } else if (key == "municion") {
        std::size_t count = 0;
        in >> count;
        s.loadout.ammo.clear();
        for (std::size_t i = 0; i < count && in; ++i) {
            int a = 0;
            in >> a;
            s.loadout.ammo.push_back(std::max(a, 0));
        }
    } else if (key == "armamento") {
        in >> s.loadout.blueprints >> s.loadout.pointDefense >> s.loadout.starterBays;
        s.loadout.pointDefense = std::max(s.loadout.pointDefense, 0);
        s.loadout.starterBays = std::max(s.loadout.starterBays, 0);
    } else if (key == "origami-a-bordo") {
        int aboard = 1;
        in >> aboard;
        s.stationAboard = aboard != 0;
    } else if (key == "piezas") {
        std::size_t count = 0;
        in >> count;
        s.pieces.clear();
        for (std::size_t i = 0; i < count && in; ++i) {
            double value = 1.0;
            in >> value;
            s.pieces.push_back(std::clamp(value, 0.0, 1.0));
        }
    } else if (key == "orbita") in >> v.orbitKind >> v.orbitSystem >> v.orbitIndex >> v.orbitOffset;
    else if (key == "plano") in >> v.orbitNormal >> v.orbitRate;
    else if (key == "atraque") in >> v.docked;
    else if (key == "base") {
        int lost = 0;
        in >> v.homeStation >> lost;
        v.lost = lost != 0;
    } else if (key == "reloj") in >> v.clock;
    else if (key == "ausente") in >> v.away;
    else return false;
    return true;
}

// Indices de los sistemas marcados, en una linea.
void WriteFlags(std::ostream& out, const char* key, const std::vector<char>& flags) {
    out << key;
    for (std::size_t i = 0; i < flags.size(); ++i)
        if (flags[i]) out << ' ' << i;
    out << '\n';
}

void ReadFlags(std::istream& in, std::vector<char>& flags) {
    std::size_t index = 0;
    while (in >> index) {
        if (index >= flags.size()) flags.resize(index + 1, 0);
        flags[index] = 1;
    }
    in.clear();
}

bool WriteFileAtomic(const std::filesystem::path& file, const std::string& text, std::string* error) {
    namespace fs = std::filesystem;
    fs::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
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

std::optional<std::string> ReadFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::filesystem::path WorldFile(const std::string& folder) { return FilePath(folder) / "mundo.sav"; }
std::filesystem::path PlayerFolder(const std::string& folder) { return FilePath(folder) / "jugadores"; }
std::filesystem::path PlayerFile(const std::string& folder, const std::string& id) { return PlayerFolder(folder) / FilePath(SafePlayerId(id) + ".sav"); }

}  // namespace

std::filesystem::path FilePath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

const ShipSave* SaveData::Ship() const {
    for (const ShipSave& v : world.ships)
        if (v.id == player.ship) return &v;
    return world.ships.empty() ? nullptr : &world.ships.front();
}

ShipSave* SaveData::Ship() { return const_cast<ShipSave*>(static_cast<const SaveData*>(this)->Ship()); }

std::string WriteWorld(const WorldSave& d) {
    std::ostringstream out = Writer();
    out << kWorldHeader << ' ' << kSaveVersion << '\n';
    out << "semilla " << d.seed << '\n';
    out << "modo " << static_cast<int>(d.mode) << '\n';
    out << "tiempo " << d.simTime << '\n';

    out << "proxima-estacion " << d.nextStation << '\n';
    for (const Station& t : d.stations) {
        out << "estacion " << t.id << ' ' << static_cast<int>(t.kind) << ' ' << t.site.system << ' ' << t.site.body << ' ' << t.site.belt << ' ' << t.site.family << ' ' << t.site.star << ' ' << t.angle << ' ' << t.storedAntimatter;
        WriteInventory(out, t.stored);
        out << ' ' << t.yield.energyPerHour << ' ' << t.yield.matterPerHour << ' ' << t.yield.requiredTier << ' ' << t.yield.orbitRadius;
        out << ' ' << t.producedUntil;  // sello: hasta que instante ha producido
        out << ' ' << t.modules.size();
        for (const Module& m : t.modules) out << ' ' << static_cast<int>(m.kind) << ' ' << m.level;
        out << " | " << t.name << '\n';
        // Defensa: solo si no es la de una estacion recien desplegada y sin duenio (docs/multijugador.md, 9.2).
        if (t.level != 1 || t.state != StationState::Active || t.hull < 1.0 || t.shield < 1.0 || !t.owner.empty())
            out << "defensa " << t.id << ' ' << t.level << ' ' << static_cast<int>(t.state) << ' ' << t.hull << ' ' << t.shield << " | " << t.owner << '\n';
        // La origami no orbita: su sitio y su orientacion van en una linea aparte.
        if (t.kind == StationKind::Shipyard)
            out << "lugar " << t.id << ' ' << t.site.offset << ' ' << t.attitude.x << ' ' << t.attitude.y << ' ' << t.attitude.z << ' ' << t.attitude.w << '\n';
    }
    // Alerta por sistema: trios "alerta ultimo-ataque sello", en el orden de los sistemas.
    out << "alerta " << d.species.alert.size();
    for (std::size_t k = 0; k < d.species.alert.size(); ++k)
        out << ' ' << d.species.alert[k] << ' ' << (k < d.species.lastAttack.size() ? d.species.lastAttack[k] : -1.0) << ' ' << (k < d.species.decayedUntil.size() ? d.species.decayedUntil[k] : -1.0);
    out << '\n';
    out << "rendicion " << d.species.surrenderUntil << ' ' << (d.species.revengePending ? 1 : 0) << ' ' << d.species.revenges << '\n';
    for (const Anomaly& a : d.anomalies) {
        out << "anomalia " << a.id << ' ' << static_cast<int>(a.kind) << ' ' << static_cast<int>(a.stance) << ' ' << a.system << ' ' << a.offset << ' ' << a.home << ' ' << a.velocity << ' ' << a.yaw << ' ' << a.radius << ' ' << (a.emitter ? 1 : 0) << ' ' << a.explored;
        out << ' ' << a.pursuing << ' ' << a.discovered << ' ' << a.revealed << ' ' << a.signature << ' ' << a.cold << " | " << a.name << '\n';
        out << "combate " << a.id << ' ' << a.hull << ' ' << a.maxHull << ' ' << a.armed << ' ' << static_cast<int>(a.weapon) << ' ' << a.weaponTier << ' ' << a.computerTier << ' ' << a.cooldown;
        out << ' ' << static_cast<int>(a.shield) << ' ' << a.shieldCharge << ' ' << a.shieldMax << '\n';
        if (a.kind == AnomalyKind::AlienShip) out << "clase " << a.id << ' ' << static_cast<int>(a.shipClass) << ' ' << static_cast<int>(a.variant) << '\n';
        // Nivel de estacion y refuerzo de la revancha.
        if (a.kind != AnomalyKind::Derelict) out << "nivel " << a.id << ' ' << a.stationLevel << ' ' << a.boost << '\n';
        // Su casco por zonas: solo si tiene alguna tocada o esta apagada.
        bool touched = a.disabled;
        for (const double z : a.zones) touched = touched || z < 1.0;
        if (touched) {
            out << "zonas_anomalia " << a.id << ' ' << (a.disabled ? 1 : 0);
            for (const double z : a.zones) out << ' ' << z;
            out << '\n';
        }
    }
    for (const ShipSave& v : d.ships) WriteShip(out, v);
    return Sealed(out.str());
}

std::optional<WorldSave> ReadWorld(const std::string& text, std::string* error) {
    const auto fail = [&](const std::string& why) -> std::optional<WorldSave> {
        if (error) *error = why;
        return std::nullopt;
    };
    std::string content;
    if (!Open(text, kWorldHeader, content, error)) return std::nullopt;

    WorldSave d;
    std::istringstream lines(content);
    std::string line;
    const auto anomaly = [&](int id) -> Anomaly* {
        for (Anomaly& a : d.anomalies)
            if (a.id == id) return &a;
        return nullptr;
    };
    while (std::getline(lines, line)) {
        std::string body, name;
        SplitName(line, body, name);
        std::istringstream in = Numbers(body);
        std::string key;
        in >> key;
        int number = 0;

        if (key == "semilla") in >> d.seed;
        else if (key == "modo") {
            in >> number;
            d.mode = static_cast<GameModeId>(number);
        } else if (key == "tiempo") in >> d.simTime;
        else if (key == "nave") {
            ShipSave v;
            in >> v.id;
            d.ships.push_back(std::move(v));
        } else if (key == "proxima-estacion") in >> d.nextStation;
        else if (key == "estacion") {
            Station t;
            t.name = name;
            int kind = 0;
            in >> t.id >> kind >> t.site.system >> t.site.body >> t.site.belt >> t.site.family >> t.site.star >> t.angle >> t.storedAntimatter;
            t.kind = static_cast<StationKind>(kind);
            t.stored = ReadInventory(in);
            in >> t.yield.energyPerHour >> t.yield.matterPerHour >> t.yield.requiredTier >> t.yield.orbitRadius >> t.producedUntil;
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
        } else if (key == "defensa") {
            int id = 0, level = 1, state = 0;
            double hull = 1.0, shield = 1.0;
            in >> id >> level >> state >> hull >> shield;
            for (Station& t : d.stations) {
                if (t.id != id) continue;
                t.level = std::clamp(level, 1, 1000);  // el tope real, el de las reglas, lo aplica la red de estaciones
                t.state = state >= 0 && state <= static_cast<int>(StationState::Folded) ? static_cast<StationState>(state) : StationState::Active;
                t.hull = std::clamp(hull, 0.0, 1.0);
                t.shield = std::clamp(shield, 0.0, 1.0);
                t.owner = name;
                break;
            }
        } else if (key == "lugar") {
            int id = 0;
            in >> id;
            for (Station& t : d.stations) {
                if (t.id != id) continue;
                in >> t.site.offset >> t.attitude.x >> t.attitude.y >> t.attitude.z >> t.attitude.w;
                t.launch = t.site.offset;
                break;
            }
        } else if (key == "alerta") {
            std::size_t count = 0;
            in >> count;
            d.species.alert.assign(count, 0.0);
            d.species.lastAttack.assign(count, -1.0);
            d.species.decayedUntil.assign(count, -1.0);
            for (std::size_t k = 0; k < count && in; ++k) {
                in >> d.species.alert[k] >> d.species.lastAttack[k] >> d.species.decayedUntil[k];
                d.species.alert[k] = std::clamp(d.species.alert[k], 0.0, 1.0);
            }
        } else if (key == "rendicion") {
            int pending = 0;
            in >> d.species.surrenderUntil >> pending >> d.species.revenges;
            d.species.revengePending = pending != 0;
            d.species.revenges = std::max(d.species.revenges, 0);
        } else if (key == "anomalia") {
            Anomaly a;
            a.name = name;
            int kind = 0, stance = 0, emitter = 0;
            in >> a.id >> kind >> stance >> a.system >> a.offset >> a.home >> a.velocity >> a.yaw >> a.radius >> emitter >> a.explored;
            a.emitter = emitter != 0;
            in >> a.pursuing >> a.discovered >> a.revealed >> a.signature >> a.cold;
            a.kind = static_cast<AnomalyKind>(kind);
            a.stance = static_cast<Stance>(stance);
            d.anomalies.push_back(std::move(a));
        } else if (key == "combate") {
            int id = 0, weapon = 0, shield = 0;
            in >> id;
            if (Anomaly* a = anomaly(id)) {
                in >> a->hull >> a->maxHull >> a->armed >> weapon >> a->weaponTier >> a->computerTier >> a->cooldown >> shield >> a->shieldCharge >> a->shieldMax;
                if (weapon >= 0 && weapon < static_cast<int>(WeaponKind::Count)) a->weapon = static_cast<WeaponKind>(weapon);
                if (shield >= 0 && shield < static_cast<int>(ShieldKind::Count)) a->shield = static_cast<ShieldKind>(shield);
            }
        } else if (key == "clase") {
            int id = 0, shipClass = 0, variant = 0;
            in >> id >> shipClass >> variant;
            if (Anomaly* a = anomaly(id)) {
                if (shipClass >= 0 && shipClass < kShipClassCount) a->shipClass = static_cast<ShipClassId>(shipClass);
                if (variant >= 0 && variant < kShipVariantCount) a->variant = static_cast<ShipVariant>(variant);
            }
        } else if (key == "nivel") {
            int id = 0, level = 0;
            double boost = 1.0;
            in >> id >> level >> boost;
            if (Anomaly* a = anomaly(id)) {
                if (level >= 0 && level <= kStationLevels) a->stationLevel = level;
                a->boost = boost > 0.0 ? boost : 1.0;
            }
        } else if (key == "zonas_anomalia") {
            int id = 0, disabled = 0;
            in >> id >> disabled;
            HullZones zones = IntactHull();
            for (double& z : zones) {
                in >> z;
                z = std::clamp(z, 0.0, 1.0);
            }
            if (Anomaly* a = anomaly(id)) {
                a->zones = zones;
                a->disabled = disabled != 0;
            }
        } else {
            // Lo demas son lineas de la ultima nave abierta. Una linea desconocida se ignora.
            ShipSave scratch;
            std::string problem;
            ShipSave& target = d.ships.empty() ? scratch : d.ships.back();
            const bool shipLine = ReadShipLine(key, in, target, problem);
            if (!problem.empty()) return fail(problem);
            if (shipLine && d.ships.empty()) return fail("dato de una nave antes de la nave");
        }
        if (in.fail()) return fail("dato ilegible");
    }
    if (d.seed == 0) return fail("sin semilla");
    return d;
}

std::string WritePlayer(const PlayerSave& p) {
    std::ostringstream out = Writer();
    out << kPlayerHeader << ' ' << kSaveVersion << '\n';
    out << "id " << SafePlayerId(p.id) << '\n';
    out << "nombre | " << p.name << '\n';
    out << "nave " << p.ship << '\n';
    out << "tutorial " << p.tutorialStep << '\n';
    out << "cursor " << (p.cursorUsed ? 1 : 0) << '\n';
    out << "analisis " << (p.analysisUsed ? 1 : 0) << '\n';
    out << "recogidas " << p.collected << '\n';
    out << "bajas " << p.kills << '\n';
    WriteFlags(out, "sistemas", p.knownSystems);
    WriteFlags(out, "visitados", p.visitedSystems);
    out << "analizados";
    for (const std::uint64_t key : p.scanned) out << ' ' << key;
    out << '\n';
    return Sealed(out.str());
}

std::optional<PlayerSave> ReadPlayer(const std::string& text, std::string* error) {
    std::string content;
    if (!Open(text, kPlayerHeader, content, error)) return std::nullopt;

    PlayerSave p;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        std::string body, name;
        SplitName(line, body, name);
        std::istringstream in = Numbers(body);
        std::string key;
        in >> key;
        if (key == "id") {
            in >> p.id;
            p.id = SafePlayerId(p.id);
        } else if (key == "nombre") p.name = name;
        else if (key == "nave") in >> p.ship;
        else if (key == "tutorial") in >> p.tutorialStep;
        else if (key == "cursor") in >> p.cursorUsed;
        else if (key == "analisis") in >> p.analysisUsed;
        else if (key == "recogidas") in >> p.collected;
        else if (key == "bajas") in >> p.kills;
        else if (key == "sistemas") ReadFlags(in, p.knownSystems);
        else if (key == "visitados") ReadFlags(in, p.visitedSystems);
        else if (key == "analizados") {
            std::uint64_t scanned = 0;
            while (in >> scanned) p.scanned.push_back(scanned);
            in.clear();
        }
        // Una linea desconocida se ignora.
        if (in.fail()) {
            if (error) *error = "dato ilegible";
            return std::nullopt;
        }
    }
    return p;
}

std::string WriteSave(const SaveData& data) { return WriteWorld(data.world) + WritePlayer(data.player); }

std::optional<SaveData> ReadSave(const std::string& text, std::string* error) {
    // El documento del jugador empieza en su cabecera, al principio de una linea.
    const std::size_t split = text.find(std::string("\n") + kPlayerHeader + " ");
    SaveData data;
    std::optional<WorldSave> world = ReadWorld(split == std::string::npos ? text : text.substr(0, split + 1), error);
    if (!world) return std::nullopt;
    if (split == std::string::npos) {
        if (error) *error = "archivo incompleto";
        return std::nullopt;
    }
    std::optional<PlayerSave> player = ReadPlayer(text.substr(split + 1), error);
    if (!player) return std::nullopt;
    data.world = std::move(*world);
    data.player = std::move(*player);
    return data;
}

std::string SafePlayerId(const std::string& id) {
    std::string safe;
    for (const char c : id) {
        const bool plain = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        if (safe.size() < 40) safe.push_back(plain ? c : '_');
    }
    return safe.empty() ? "local" : safe;
}

bool SaveGame(const std::string& folder, const SaveData& data, std::string* error) {
    std::error_code problem;
    if (std::filesystem::is_regular_file(FilePath(folder), problem)) {
        if (error) *error = "ya hay un archivo con ese nombre, de una versión anterior: una partida es ahora una carpeta";
        return false;
    }
    std::filesystem::create_directories(PlayerFolder(folder), problem);
    if (problem) {
        if (error) *error = problem.message();
        return false;
    }
    // El jugador primero: si se corta entre los dos, queda el mundo anterior con un jugador mas
    // adelantado, que es lo que menos se nota.
    return WriteFileAtomic(PlayerFile(folder, data.player.id), WritePlayer(data.player), error) && WriteFileAtomic(WorldFile(folder), WriteWorld(data.world), error);
}

std::optional<SaveData> LoadGame(const std::string& folder, const std::string& playerId, std::string* error) {
    const std::optional<std::string> worldText = ReadFile(WorldFile(folder));
    if (!worldText) {
        if (error) *error = "sin partida guardada";
        return std::nullopt;
    }
    SaveData data;
    std::optional<WorldSave> world = ReadWorld(*worldText, error);
    if (!world) return std::nullopt;
    data.world = std::move(*world);
    if (const std::optional<std::string> playerText = ReadFile(PlayerFile(folder, playerId))) {
        std::optional<PlayerSave> player = ReadPlayer(*playerText, error);
        if (!player) return std::nullopt;
        data.player = std::move(*player);
    } else {
        // Un jugador nuevo en un mundo que ya existe.
        data.player = PlayerSave{};
        if (!data.world.ships.empty()) data.player.ship = data.world.ships.front().id;
    }
    data.player.id = SafePlayerId(playerId);
    return data;
}

bool SaveWorld(const std::string& folder, const WorldSave& world, std::string* error) {
    std::error_code problem;
    std::filesystem::create_directories(PlayerFolder(folder), problem);
    if (problem) {
        if (error) *error = problem.message();
        return false;
    }
    return WriteFileAtomic(WorldFile(folder), WriteWorld(world), error);
}

bool SavePlayer(const std::string& folder, const PlayerSave& player, std::string* error) {
    std::error_code problem;
    std::filesystem::create_directories(PlayerFolder(folder), problem);
    if (problem) {
        if (error) *error = problem.message();
        return false;
    }
    return WriteFileAtomic(PlayerFile(folder, player.id), WritePlayer(player), error);
}

std::optional<WorldSave> LoadWorld(const std::string& folder, std::string* error) {
    const std::optional<std::string> text = ReadFile(WorldFile(folder));
    if (!text) {
        if (error) *error = "sin partida guardada";
        return std::nullopt;
    }
    return ReadWorld(*text, error);
}

std::optional<PlayerSave> LoadPlayer(const std::string& folder, const std::string& playerId) {
    const std::optional<std::string> text = ReadFile(PlayerFile(folder, playerId));
    if (!text) return std::nullopt;
    return ReadPlayer(*text);
}

void DeleteGame(const std::string& folder) {
    std::error_code ignored;
    std::filesystem::remove(WorldFile(folder), ignored);
    std::filesystem::remove_all(PlayerFolder(folder), ignored);
}

}  // namespace game
