#include "game/Net.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <optional>
#include <set>
#include <type_traits>

namespace game {

namespace {

// ---- Escribir y leer con el mismo codigo ---------------------------------------------------------
// Out y In se usan igual: a(x) escribe x o lo lee. Lo que se puede copiar byte a byte va tal cual; lo
// demas (cadenas, listas, estructuras con cadenas dentro) tiene su funcion Wire, que vale para los dos.

class Out {
public:
    static constexpr bool reading = false;
    Packet data;
    void Raw(const void* bytes, std::size_t count) {
        const auto* p = static_cast<const std::uint8_t*>(bytes);
        data.insert(data.end(), p, p + count);
    }
    std::size_t Left() const { return 0; }
    void Fail() {}
    template <class T>
    void operator()(T& value);
};

class In {
public:
    static constexpr bool reading = true;
    explicit In(const Packet& packet) : at_(packet.data()), left_(packet.size()) {}
    // Un paquete cortado no lee fuera de sitio: lo que falte queda a cero y ok pasa a false.
    void Raw(void* bytes, std::size_t count) {
        if (count > left_) {
            std::memset(bytes, 0, count);
            Fail();
            return;
        }
        std::memcpy(bytes, at_, count);
        at_ += count;
        left_ -= count;
    }
    std::size_t Left() const { return left_; }
    void Fail() {
        ok = false;
        left_ = 0;
    }
    bool ok = true;
    template <class T>
    void operator()(T& value);

private:
    const std::uint8_t* at_;
    std::size_t left_;
};

template <class A>
void Wire(A& a, std::string& s) {
    std::uint32_t count = static_cast<std::uint32_t>(s.size());
    a(count);
    if (A::reading) {
        if (count > a.Left()) {
            a.Fail();
            count = 0;
        }
        s.resize(count);
    }
    if (count > 0) a.Raw(s.data(), count);
}

template <class A, class T>
void Wire(A& a, std::vector<T>& v) {
    std::uint32_t count = static_cast<std::uint32_t>(v.size());
    a(count);
    const bool plain = std::is_trivially_copyable_v<T>;
    if (A::reading) {
        if (count * (plain ? sizeof(T) : 1) > a.Left()) {  // una cuenta imposible: el paquete esta mal
            a.Fail();
            count = 0;
        }
        v.clear();
        v.resize(count);
    }
    if (count == 0) return;
    if (plain) a.Raw(v.data(), count * sizeof(T));
    else for (T& x : v) a(x);
}

template <class A, class T>
void Wire(A& a, std::deque<T>& d) {
    std::uint32_t count = static_cast<std::uint32_t>(d.size());
    a(count);
    if (A::reading) {
        if (count > a.Left()) {
            a.Fail();
            count = 0;
        }
        d.clear();
        d.resize(count);
    }
    for (T& x : d) a(x);
}

template <class A, class T>
void Wire(A& a, std::set<T>& s) {
    std::vector<T> items(s.begin(), s.end());
    a(items);
    if (A::reading) s = std::set<T>(items.begin(), items.end());
}

template <class A, class T>
void Wire(A& a, std::optional<T>& o) {
    bool has = o.has_value();
    a(has);
    if (A::reading) {
        if (has) o.emplace();
        else o.reset();
    }
    if (has) a(*o);
}

// ---- Las estructuras del juego -------------------------------------------------------------------

template <class A>
void Wire(A& a, ShipLoadout& l) {
    a(l.engine); a(l.modules); a(l.ammo); a(l.cooldown); a(l.blueprints); a(l.pointDefense); a(l.starterBays); a(l.starter);
}

template <class A>
void Wire(A& a, ShipState& s) {
    a(s.position); a(s.orientation); a(s.aim); a(s.angularRate); a(s.angularPush); a(s.velocity); a(s.warpSpeed); a(s.warpHeading);
    a(s.holding); a(s.moveDirection); a(s.speed); a(s.throttle); a(s.mode); a(s.warpCharge); a(s.warpAge); a(s.energy); a(s.hull);
    a(s.zones); a(s.antimatter); a(s.heatShieldTier); a(s.handlingTier); a(s.scannerTier); a(s.relayTier); a(s.computerTier);
    a(s.weapons); a(s.cargo); a(s.elements); a(s.emitters); a(s.shipHours); a(s.defense); a(s.massFactor); a(s.loadout);
    a(s.stationAboard); a(s.pieces);
}

template <class A>
void Wire(A& a, Contact& c) {
    a(c.target); a(c.name); a(c.detail); a(c.position); a(c.distance); a(c.identified); a(c.coarse); a(c.stance);
}

template <class A>
void Wire(A& a, CollisionWarning& w) {
    a(w.name); a(w.seconds); a(w.heat);
}

template <class A>
void Wire(A& a, ApproachInfo& i) {
    a(i.station); a(i.distance); a(i.top); a(i.zone);
}

template <class A>
void Wire(A& a, Vessel& v) {
    a(v.id); a(v.bubble); a(v.settled); a(v.ship);
    a(v.autopilot); a(v.orbiting); a(v.tracking); a(v.orbitOffset); a(v.orbitNormal); a(v.orbitRate); a(v.frame);
    a(v.routePhase); a(v.routeDistance); a(v.fuelWarned); a(v.warning); a(v.exteriorFactor); a(v.timeFocus);
    a(v.nearestSystem); a(v.inHeatZone); a(v.creative);
    a(v.dockPhase); a(v.dockWanted); a(v.undockGrace); a(v.dockedAge); a(v.approach); a(v.homeStation); a(v.respawnPending); a(v.leashWarned);
    a(v.combatAge); a(v.leaving);
    a(v.hitStreak); a(v.hitDamage); a(v.hitShield); a(v.directedLoss); a(v.zoneHeat); a(v.hullHits); a(v.nextHit); a(v.failedZone);
    a(v.driftVelocity); a(v.lastWarpCharge); a(v.radiators); a(v.ramming); a(v.ramWarned); a(v.overheatWarned); a(v.gasWarned);
    a(v.phase); a(v.wreckAge); a(v.wreckPosition); a(v.lossReason);
    a(v.drones); a(v.droneSite); a(v.droneDeposit); a(v.dronesFullWarned); a(v.extractZ); a(v.droneYield);
    a(v.contacts); a(v.scanProgress); a(v.lastScan); a(v.echo); a(v.echoTarget); a(v.scansDone); a(v.exploring);
}

template <class A>
void Wire(A& a, Station& s) {
    a(s.id); a(s.kind); a(s.site); a(s.name); a(s.modules); a(s.yield); a(s.stored); a(s.storedAntimatter); a(s.angle);
    a(s.deployAge); a(s.attitude); a(s.launch); a(s.producedUntil);
    a(s.level); a(s.state); a(s.hull); a(s.shield); a(s.owner);
}

template <class A>
void Wire(A& a, Anomaly& n) {
    a(n.id); a(n.kind); a(n.stance); a(n.system); a(n.offset); a(n.home); a(n.velocity); a(n.yaw); a(n.radius); a(n.name);
    a(n.emitter); a(n.explored); a(n.pursuing); a(n.discovered); a(n.revealed); a(n.signature); a(n.cold);
    a(n.hull); a(n.maxHull); a(n.armed); a(n.weapon); a(n.weaponTier); a(n.computerTier); a(n.cooldown);
    a(n.shield); a(n.shieldCharge); a(n.shieldMax); a(n.shipClass); a(n.variant); a(n.zones); a(n.zoneHeat); a(n.disabled);
    a(n.stationLevel); a(n.boost);
}

template <class A>
void Wire(A& a, SpeciesState& s) {
    a(s.alert); a(s.lastAttack); a(s.surrenderUntil); a(s.revengePending); a(s.revenges); a(s.decayedUntil);
}

template <class A>
void Wire(A& a, Person& p) {
    a(p.body); a(p.name); a(p.goal); a(p.wait); a(p.walked);
}

template <class A>
void Wire(A& a, Dialogue::State& s) {
    std::uint64_t step = s.step, line = s.line;
    a(step); a(line);
    s.step = static_cast<std::size_t>(step);
    s.line = static_cast<std::size_t>(line);
    a(s.typed); a(s.shown); a(s.completions); a(s.lastCompleted); a(s.sinceCompleted); a(s.active);
}

template <class A>
void Wire(A& a, PlayerWire& p) {
    a(p.id); a(p.name); a(p.vesselId); a(p.seats); a(p.dialogue); a(p.analysisUsed); a(p.cursorUsed); a(p.collected); a(p.kills);
    a(p.log); a(p.known); a(p.visited); a(p.scanned); a(p.onFoot); a(p.avatar); a(p.lastStrike); a(p.strikeAge); a(p.cinematic);
}

template <class A>
void Wire(A& a, SnapshotData& d) {
    a(d.seed); a(d.mode); a(d.timeScale); a(d.origin); a(d.simTime); a(d.timeFocus); a(d.steps);
    a(d.projectiles); a(d.impacts); a(d.lostPieces); a(d.deckKind); a(d.crew);
    a(d.stations); a(d.nextStation); a(d.anomalies); a(d.species); a(d.vessels); a(d.players); a(d.me);
}

template <class A>
void Wire(A& a, Hello& h) {
    a(h.protocol); a(h.saveVersion); a(h.id); a(h.name); a(h.join);
}

template <class T>
void Out::operator()(T& value) {
    if constexpr (std::is_trivially_copyable_v<T>) Raw(&value, sizeof(T));
    else Wire(*this, value);
}

template <class T>
void In::operator()(T& value) {
    if constexpr (std::is_trivially_copyable_v<T>) Raw(&value, sizeof(T));
    else Wire(*this, value);
}

template <class T>
Packet Encode(MessageType type, const T& value) {
    Out out;
    out(type);
    out(const_cast<T&>(value));  // Out no cambia lo que escribe; comparte codigo con In
    return std::move(out.data);
}

template <class T>
bool Decode(MessageType type, const Packet& packet, T& value) {
    if (TypeOf(packet) != type) return false;
    In in(packet);
    MessageType head{};
    in(head);
    in(value);
    return in.ok && in.Left() == 0;
}

// ---- Instantaneas por diferencias --------------------------------------------------------------

enum : std::uint8_t { kHead, kProjectiles, kImpacts, kLostPieces, kCrew, kStations, kAnomalies, kSpecies, kVessel, kPlayer };

template <class T>
Packet Bytes(const T& value) {
    Out out;
    out(const_cast<T&>(value));
    return std::move(out.data);
}

// La cabecera: lo suelto de la instantanea, que no es ninguna lista.
template <class A>
void WireHead(A& a, SnapshotData& d) {
    a(d.seed); a(d.mode); a(d.timeScale); a(d.origin); a(d.simTime); a(d.timeFocus); a(d.steps); a(d.deckKind); a(d.nextStation); a(d.me);
}

// Un numero en trozos de 7 bits: los pequenos, que son casi todos, ocupan un byte.
void PutCount(Packet& out, std::size_t n) {
    while (n >= 0x80) {
        out.push_back(static_cast<std::uint8_t>(n | 0x80));
        n >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(n));
}

bool GetCount(const Packet& in, std::size_t& at, std::size_t& n) {
    n = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (at >= in.size()) return false;
        const std::uint8_t b = in[at++];
        n |= static_cast<std::size_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
    }
    return false;
}

// La diferencia entre dos trozos del mismo tamano, comprimida: tantos ceros, tantos bytes tal cual, y asi.
Packet PackDifference(const Packet& now, const Packet& base) {
    Packet out;
    const std::size_t n = now.size();
    std::size_t i = 0;
    while (i < n) {
        std::size_t zeros = 0;
        while (i < n && now[i] == base[i]) {
            ++zeros;
            ++i;
        }
        PutCount(out, zeros);
        // Los bytes distintos, hasta que vengan cuatro iguales seguidos (menos no compensa cortar).
        std::size_t j = i, same = 0;
        while (j < n && same < 4) {
            same = now[j] == base[j] ? same + 1 : 0;
            ++j;
        }
        if (same >= 4) j -= 4;
        PutCount(out, j - i);
        for (; i < j; ++i) out.push_back(static_cast<std::uint8_t>(now[i] ^ base[i]));
    }
    return out;
}

bool UnpackDifference(const Packet& packed, const Packet& base, Packet& out) {
    out = base;
    std::size_t at = 0, i = 0;
    while (at < packed.size()) {
        std::size_t zeros = 0, literal = 0;
        if (!GetCount(packed, at, zeros) || !GetCount(packed, at, literal)) return false;
        i += zeros;
        if (i + literal > out.size() || at + literal > packed.size()) return false;
        for (std::size_t k = 0; k < literal; ++k) out[i++] ^= packed[at++];
    }
    return i <= out.size();
}

const SnapshotSection* FindSection(const SnapshotSections& all, std::uint8_t kind, std::int32_t key) {
    for (const SnapshotSection& s : all)
        if (s.kind == kind && s.key == key) return &s;
    return nullptr;
}

// ---- Transporte en memoria ---------------------------------------------------------------------

struct MemoryPipe {
    std::deque<Packet> toA, toB;
    bool open = true;
};

class MemoryEnd final : public Transport {
public:
    MemoryEnd(std::shared_ptr<MemoryPipe> pipe, bool isA) : pipe_(std::move(pipe)), isA_(isA) {}
    void Send(const Packet& packet, bool) override {
        if (pipe_->open) (isA_ ? pipe_->toB : pipe_->toA).push_back(packet);
    }
    bool Receive(Packet& out) override {
        std::deque<Packet>& mine = isA_ ? pipe_->toA : pipe_->toB;
        if (mine.empty()) return false;
        out = std::move(mine.front());
        mine.pop_front();
        return true;
    }
    bool Open() const override { return pipe_->open; }
    void Close() override { pipe_->open = false; }

private:
    std::shared_ptr<MemoryPipe> pipe_;
    bool isA_;
};

}  // namespace

std::pair<std::shared_ptr<Transport>, std::shared_ptr<Transport>> MakeMemoryLink() {
    auto pipe = std::make_shared<MemoryPipe>();
    return {std::make_shared<MemoryEnd>(pipe, true), std::make_shared<MemoryEnd>(pipe, false)};
}

SnapshotSections SplitSnapshot(const SnapshotData& d) {
    SnapshotSections out;
    {
        Out head;
        WireHead(head, const_cast<SnapshotData&>(d));
        out.push_back({kHead, 0, std::move(head.data)});
    }
    out.push_back({kProjectiles, 0, Bytes(d.projectiles)});
    out.push_back({kImpacts, 0, Bytes(d.impacts)});
    out.push_back({kLostPieces, 0, Bytes(d.lostPieces)});
    out.push_back({kCrew, 0, Bytes(d.crew)});
    out.push_back({kStations, 0, Bytes(d.stations)});
    out.push_back({kAnomalies, 0, Bytes(d.anomalies)});
    out.push_back({kSpecies, 0, Bytes(d.species)});
    for (const Vessel& v : d.vessels) out.push_back({kVessel, v.id, Bytes(v)});
    for (std::size_t p = 0; p < d.players.size(); ++p) out.push_back({kPlayer, static_cast<std::int32_t>(p), Bytes(d.players[p])});
    return out;
}

bool JoinSnapshot(const SnapshotSections& sections, SnapshotData& d) {
    d = SnapshotData{};
    bool head = false;
    for (const SnapshotSection& s : sections) {
        In in(s.bytes);
        switch (s.kind) {
            case kHead:
                WireHead(in, d);
                head = true;
                break;
            case kProjectiles: in(d.projectiles); break;
            case kImpacts: in(d.impacts); break;
            case kLostPieces: in(d.lostPieces); break;
            case kCrew: in(d.crew); break;
            case kStations: in(d.stations); break;
            case kAnomalies: in(d.anomalies); break;
            case kSpecies: in(d.species); break;
            case kVessel:
                d.vessels.emplace_back();
                in(d.vessels.back());
                break;
            case kPlayer:
                d.players.emplace_back();
                in(d.players.back());
                break;
            default: return false;
        }
        if (!in.ok || in.Left() != 0) return false;
    }
    return head;
}

Packet EncodeDelta(std::uint32_t seq, std::uint32_t baseSeq, const SnapshotSections& now, const SnapshotSections* base) {
    Out out;
    MessageType type = MessageType::SnapshotDelta;
    std::uint32_t count = static_cast<std::uint32_t>(now.size());
    if (!base) baseSeq = 0;
    out(type); out(seq); out(baseSeq); out(count);
    for (const SnapshotSection& s : now) {
        std::uint8_t kind = s.kind, mode = 0;  // 0 entero, 1 diferencia, 2 igual que en la referencia
        std::int32_t key = s.key;
        const SnapshotSection* old = base ? FindSection(*base, s.kind, s.key) : nullptr;
        if (old && old->bytes == s.bytes) mode = 2;
        else if (old && old->bytes.size() == s.bytes.size()) mode = 1;
        out(kind); out(key); out(mode);
        if (mode == 0) out(const_cast<Packet&>(s.bytes));
        else if (mode == 1) {
            Packet packed = PackDifference(s.bytes, old->bytes);
            out(packed);
        }
    }
    return std::move(out.data);
}

bool DecodeDelta(const Packet& packet, const std::map<std::uint32_t, SnapshotSections>& known, std::uint32_t& seq, SnapshotSections& out, bool& missingBase) {
    missingBase = false;
    out.clear();
    if (TypeOf(packet) != MessageType::SnapshotDelta) return false;
    In in(packet);
    MessageType type{};
    std::uint32_t baseSeq = 0, count = 0;
    in(type); in(seq); in(baseSeq); in(count);
    if (!in.ok || count > 4096) return false;
    const SnapshotSections* base = nullptr;
    if (baseSeq != 0) {
        const auto found = known.find(baseSeq);
        if (found == known.end()) {
            missingBase = true;
            return false;
        }
        base = &found->second;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        SnapshotSection s;
        std::uint8_t mode = 0;
        in(s.kind); in(s.key); in(mode);
        if (!in.ok) return false;
        const SnapshotSection* old = base ? FindSection(*base, s.kind, s.key) : nullptr;
        if (mode == 0) in(s.bytes);
        else if (mode == 2 && old) s.bytes = old->bytes;
        else if (mode == 1 && old) {
            Packet packed;
            in(packed);
            if (!in.ok || !UnpackDifference(packed, old->bytes, s.bytes)) return false;
        } else return false;
        if (!in.ok) return false;
        out.push_back(std::move(s));
    }
    return in.Left() == 0;
}

MessageType TypeOf(const Packet& packet) { return packet.empty() ? MessageType{} : static_cast<MessageType>(packet[0]); }

Packet EncodeHello(const Hello& hello) { return Encode(MessageType::Hello, hello); }
Packet EncodeControls(const ShipControls& controls) { return Encode(MessageType::Controls, controls); }
Packet EncodeCommand(const ShipCommand& command) { return Encode(MessageType::Command, command); }
Packet EncodeSnapshot(const SnapshotData& data) { return Encode(MessageType::Snapshot, data); }
bool DecodeHello(const Packet& packet, Hello& out) { return Decode(MessageType::Hello, packet, out); }
bool DecodeControls(const Packet& packet, ShipControls& out) { return Decode(MessageType::Controls, packet, out); }
bool DecodeCommand(const Packet& packet, ShipCommand& out) { return Decode(MessageType::Command, packet, out); }
bool DecodeSnapshot(const Packet& packet, SnapshotData& out) { return Decode(MessageType::Snapshot, packet, out); }

#ifndef EPSILON_WITH_ENET
// Compilado sin ENet: no hay red, solo el transporte en memoria.
std::unique_ptr<LanBeacon> AnnounceOnLan(std::string* error) {
    if (error) *error = "este juego se compiló sin red";
    return nullptr;
}
std::vector<LanGame> FindLanGames(double) { return {}; }
bool NetworkAvailable() { return false; }
std::unique_ptr<NetListener> ListenOn(int, int, std::string* error) {
    if (error) *error = "este juego se compiló sin red";
    return nullptr;
}
std::shared_ptr<Transport> ConnectTo(const std::string&, int, double, std::string* error) {
    if (error) *error = "este juego se compiló sin red";
    return nullptr;
}
#endif

// ---- Servidor ----------------------------------------------------------------------------------

void GameServer::Add(std::shared_ptr<Transport> link) {
    Peer peer;
    peer.link = std::move(link);
    peers_.push_back(std::move(peer));
}

int GameServer::Clients() const {
    int count = 0;
    for (const Peer& p : peers_) count += p.player >= 0 && p.link->Open() ? 1 : 0;
    return count;
}

void GameServer::Refuse(Peer& peer, const std::string& why) {
    peer.link->Send(Encode(MessageType::Refused, why));
    peer.link->Close();
}

// Un jugador que llega: se comprueba que habla lo mismo y se le da sitio en la partida.
void GameServer::Greet(Peer& peer, const Hello& hello) {
    if (hello.protocol != kProtocolVersion || hello.saveVersion != kSaveVersion) return Refuse(peer, "El servidor es de otra versión del juego.");
    const std::string id = SafePlayerId(hello.id);
    const std::string name = hello.name.empty() ? "Jugador" : hello.name;
    // Quien ya estaba (se le corto la conexion) vuelve a ser el mismo.
    int player = -1;
    for (int p = 0; p < session_.PlayerCount(); ++p)
        if (session_.PlayerAt(p).id == id) player = p;
    const auto taken = [&](int p) {
        for (const Peer& other : peers_)
            if (&other != &peer && other.player == p && other.link->Open()) return true;
        return false;
    };
    if (player >= 0 && taken(player)) return Refuse(peer, "Ese jugador ya está en la partida.");
    // Lo que guardo la ultima vez, si la partida tiene carpeta: dice que nave lleva.
    std::optional<PlayerSave> saved;
    if (player < 0 && !saveFolder_.empty()) saved = LoadPlayer(saveFolder_, id);
    // Un sitio libre: un jugador sin dueño (nadie ha entrado nunca por el) y sin conexion.
    const auto vacant = [&](int p) { return !session_.PlayerAt(p).connected && session_.PlayerAt(p).id.rfind("local", 0) == 0 && !taken(p); };
    if (player < 0 && saved) {
        // Su nave sigue en el mundo: vuelve a ella.
        const int vessel = session_.IndexOfVessel(saved->ship);
        for (int p = 0; p < session_.PlayerCount() && player < 0; ++p)
            if (vessel >= 0 && session_.PlayerAt(p).vessel == vessel && vacant(p)) player = p;
        if (player >= 0) {
            session_.RestorePlayer(player, *saved);
            session_.PlayerBack(player);
        }
    }
    if (player < 0 && hello.join != JoinAs::Crew) {
        // Una nave que espera a alguien (la de un servidor recien abierto): es para el primero que llegue.
        for (int p = 0; p < session_.PlayerCount() && player < 0; ++p)
            if (vacant(p) && session_.VesselAt(session_.PlayerAt(p).vessel).phase == GamePhase::Away) player = p;
        if (player >= 0) {
            session_.NamePlayer(player, id, name);
            session_.PlayerBack(player);
        }
    }
    if (player < 0) {
        const int hostShip = session_.VesselAt(session_.PlayerAt(0).vessel).id;
        if (hello.join == JoinAs::Host && !taken(0)) player = 0;
        else if (hello.join == JoinAs::OwnShip) {
            const int back = session_.FocusPlayer();
            session_.SetFocusPlayer(0);  // la nave nueva aparece junto a la del anfitrion
            session_.AddVessel(50.0);
            session_.SetFocusPlayer(back);
            player = session_.PlayerCount() - 1;
        } else {
            player = session_.AddPlayer(name, hostShip);
        }
        if (player < 0) return Refuse(peer, "No hay sitio en la partida.");
        if (saved) session_.RestorePlayer(player, *saved);  // entra por un sitio nuevo, pero con lo que sabia
        session_.NamePlayer(player, id, name);
    } else {
        session_.NamePlayer(player, id, name);
        session_.PlayerBack(player);  // vuelve: su nave reaparece donde estaba
    }
    peer.player = player;
    std::uint32_t seed = session_.Seed();
    peer.link->Send(Encode(MessageType::Welcome, seed));
    SendSnapshot(peer);
}

// Lo que ve ese jugador, respecto a la ultima instantanea que confirmo tener.
void GameServer::SendSnapshot(Peer& peer) {
    SnapshotSections now = SplitSnapshot(session_.MakeSnapshot(peer.player));
    const auto base = peer.acked != 0 ? peer.history.find(peer.acked) : peer.history.end();
    const bool have = base != peer.history.end();
    const std::uint32_t seq = ++peer.sent;
    const Packet packet = EncodeDelta(seq, have ? peer.acked : 0, now, have ? &base->second : nullptr);
    peer.history[seq] = std::move(now);
    // Las anteriores a la confirmada ya no sirven; y como mucho se guardan unas pocas.
    while (!peer.history.empty() && (peer.history.begin()->first < peer.acked || peer.history.size() > 64)) {
        if (peer.history.begin()->first == peer.acked) peer.acked = 0;  // se queda sin referencia: la siguiente ira entera
        peer.history.erase(peer.history.begin());
    }
    bytesSent_ += packet.size();
    peer.link->Send(packet, false);
}

void GameServer::Read(Peer& peer) {
    Packet packet;
    while (peer.link->Open() && peer.link->Receive(packet)) {
        switch (TypeOf(packet)) {
            case MessageType::Hello: {
                Hello hello;
                if (peer.player < 0 && DecodeHello(packet, hello)) Greet(peer, hello);
                break;
            }
            case MessageType::Controls: {
                ShipControls c;
                if (peer.player < 0 || !DecodeControls(packet, c)) break;
                // Los giros son incrementos: se suman hasta que un paso los gaste. Lo demas, el ultimo valor.
                ShipControls& k = peer.controls;
                k.turnYaw += c.turnYaw;
                k.turnPitch += c.turnPitch;
                k.turnRoll += c.turnRoll;
                k.foot.yaw += c.foot.yaw;
                k.foot.pitch += c.foot.pitch;
                k.foot.move = c.foot.move;
                k.view = c.view;
                k.throttleChange = c.throttleChange;
                k.manualBrake = k.manualBrake || c.manualBrake;
                break;
            }
            case MessageType::Command: {
                ShipCommand command;
                if (peer.player >= 0 && DecodeCommand(packet, command)) session_.ApplyAs(peer.player, command);
                break;
            }
            case MessageType::Ack: {
                std::uint32_t seq = 0;
                if (!Decode(MessageType::Ack, packet, seq)) break;
                if (seq == kNeedFull) peer.acked = 0;  // no tiene la referencia: la siguiente, entera
                else if (seq > peer.acked && peer.history.count(seq) != 0) peer.acked = seq;
                break;
            }
            default: break;  // lo que no se entiende se ignora
        }
    }
}

bool GameServer::OpenToNetwork(int port, const std::string& name, int maxClients, std::string* error) {
    std::unique_ptr<NetListener> listener = ListenOn(port, maxClients, error);
    if (!listener) return false;
    listener_ = std::move(listener);
    announced_.port = port;
    announced_.name = name;
    announced_.maxPlayers = maxClients;
    beacon_ = AnnounceOnLan();  // si no se puede, la partida sigue abierta: se entra con la direccion
    return true;
}

void GameServer::CloseNetwork() {
    listener_.reset();  // al destruirse, sus conexiones quedan cerradas: el siguiente Update avisa a la sesion
    beacon_.reset();
}

void GameServer::Follow(const Transport* link, int player) {
    if (player < 0 || player >= session_.PlayerCount()) return;
    for (Peer& peer : peers_)
        if (peer.link.get() == link && peer.player >= 0) peer.player = player;
}

// El mundo una vez, y el archivo de cada jugador que haya entrado alguna vez.
bool GameServer::Save(std::string* error) {
    if (saveFolder_.empty()) {
        if (error) *error = "la partida no tiene carpeta";
        return false;
    }
    if (!SaveWorld(saveFolder_, session_.Snapshot().world, error)) return false;
    bool ok = true;
    for (int p = 0; p < session_.PlayerCount(); ++p) {
        if (session_.PlayerAt(p).id.rfind("local-", 0) == 0) continue;  // un sitio por el que no ha entrado nadie
        ok = SavePlayer(saveFolder_, session_.SnapshotPlayer(p), error) && ok;
    }
    return ok;
}

void GameServer::SendSnapshots() {
    for (Peer& peer : peers_) {
        if (peer.player < 0 || !peer.link->Open()) continue;
        SendSnapshot(peer);
    }
}

int GameServer::Update(double realSeconds, float focalPixels) {
    if (listener_) {
        listener_->Pump();
        while (std::shared_ptr<Transport> arrived = listener_->Accept()) Add(std::move(arrived));
    }
    if (beacon_) {
        announced_.players = 0;
        for (int p = 0; p < session_.PlayerCount(); ++p) announced_.players += session_.PlayerAt(p).connected ? 1 : 0;
        announced_.mode = static_cast<int>(session_.Mode());
        beacon_->Pump(announced_);
    }
    for (std::size_t i = 0; i < peers_.size(); ++i) Read(peers_[i]);
    // Las conexiones que se han cerrado: su jugador se queda sin nadie al mando (GameSession::PlayerLeft).
    for (Peer& peer : peers_) {
        if (peer.link->Open() || peer.gone) continue;
        peer.gone = true;
        if (peer.player >= 0) session_.PlayerLeft(peer.player);
    }
    peers_.erase(std::remove_if(peers_.begin(), peers_.end(), [](const Peer& p) { return p.gone; }), peers_.end());
    // Con un tiron largo no se intenta recuperar todo: como mucho, ocho pasos de golpe.
    pending_ = std::min(pending_ + std::max(realSeconds, 0.0), GameSession::kFixedStep * 8.0);
    int steps = 0;
    while (pending_ >= GameSession::kFixedStep) {
        pending_ -= GameSession::kFixedStep;
        std::vector<ShipControls> controls(static_cast<std::size_t>(session_.PlayerCount()));
        for (Peer& peer : peers_) {
            if (peer.player < 0 || peer.player >= session_.PlayerCount()) continue;
            controls[static_cast<std::size_t>(peer.player)] = peer.controls;
            peer.controls.turnYaw = peer.controls.turnPitch = peer.controls.turnRoll = 0.0f;
            peer.controls.foot.yaw = peer.controls.foot.pitch = 0.0;
            peer.controls.manualBrake = false;
        }
        session_.StepPlayers(GameSession::kFixedStep, controls, focalPixels);
        ++steps;
        ++tick_;
        if (tick_ % static_cast<std::uint64_t>(std::max(snapshotEvery, 1)) == 0) SendSnapshots();
    }
    return steps;
}

// ---- Cliente -----------------------------------------------------------------------------------

GameClient::GameClient(GameSession& replica, std::shared_ptr<Transport> link, const Hello& hello) : replica_(replica), link_(std::move(link)) {
    GameSession::Remote remote;
    std::shared_ptr<Transport> out = link_;
    remote.command = [out](const ShipCommand& command) { out->Send(EncodeCommand(command), true); };
    remote.controls = [out](const ShipControls& controls) { out->Send(EncodeControls(controls), false); };
    replica_.SetRemote(std::move(remote));
    link_->Send(EncodeHello(hello), true);
}

void GameClient::Update() {
    Packet packet;
    while (link_->Receive(packet)) {
        switch (TypeOf(packet)) {
            case MessageType::Welcome: {
                std::uint32_t seed = 0;
                if (Decode(MessageType::Welcome, packet, seed) && seed != replica_.Seed()) {
                    refusal_ = "La partida es de otra galaxia.";
                    link_->Close();
                }
                break;
            }
            case MessageType::Refused: {
                std::string why;
                if (Decode(MessageType::Refused, packet, why)) refusal_ = why.empty() ? "El servidor no admite la conexión." : why;
                break;
            }
            case MessageType::Snapshot: {
                SnapshotData data;
                if (!DecodeSnapshot(packet, data) || data.seed != replica_.Seed()) break;  // uno cortado se salta: ya llegara otro
                replica_.ApplySnapshot(data);
                ++snapshots_;
                break;
            }
            case MessageType::SnapshotDelta: {
                std::uint32_t seq = 0;
                SnapshotSections sections;
                bool missing = false;
                if (!DecodeDelta(packet, history_, seq, sections, missing)) {
                    // Sin la referencia no se puede reconstruir: se le pide entera.
                    if (missing) link_->Send(Encode(MessageType::Ack, kNeedFull), false);
                    break;
                }
                // Se confirma siempre, para que el servidor pueda usarla de referencia.
                link_->Send(Encode(MessageType::Ack, seq), false);
                SnapshotData data;
                const bool fresh = seq > lastSeq_;
                if (fresh && JoinSnapshot(sections, data) && data.seed == replica_.Seed()) {
                    replica_.ApplySnapshot(data);
                    ++snapshots_;
                    lastSeq_ = seq;
                }
                history_[seq] = std::move(sections);
                while (history_.size() > 64) history_.erase(history_.begin());
                break;
            }
            default: break;
        }
    }
}

}  // namespace game
