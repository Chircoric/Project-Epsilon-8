// Net.h - Servidor y cliente: la partida la simula uno y los demas la miran y mandan ordenes.
//
// El servidor (GameServer) tiene la sesion de verdad. Cada cliente (GameClient) tiene otra GameSession
// que es una replica: no simula; aplica las instantaneas que le llegan y manda su mando y sus ordenes.
// La interfaz y el dibujo leen la replica igual que leian la sesion, sin saber que lo es.
//
// Lo que viaja (docs/multijugador.md, apartado 4.3):
//
//   cliente -> servidor   Hello      quien es y como quiere entrar
//                         Controls   su mando en este fotograma (giro, gases, andar)
//                         Command    una orden (ShipCommand)
//   servidor -> cliente   Welcome    aceptado: la semilla de la galaxia
//                         Refused    no puede entrar, y por que
//                         Snapshot   todo lo que ve ahora: el mundo y su burbuja (SnapshotData)
//
// Por donde viaja lo decide un Transport. Aqui esta el de memoria, para tener servidor y cliente en el
// mismo proceso; el de red (ENet) es la fase 6.
//
// Los mensajes llevan los datos tal como estan en memoria: servidor y clientes tienen que ser el mismo
// ejecutable. El saludo lo comprueba con la version del protocolo y la del guardado.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/Session.h"

namespace game {

using Packet = std::vector<std::uint8_t>;

// Un extremo de una conexion. reliable: tiene que llegar, y en orden (ordenes); si no, puede perderse
// (mandos e instantaneas: el siguiente lo arregla). El de memoria lo entrega todo.
class Transport {
public:
    virtual ~Transport() = default;
    virtual void Send(const Packet& packet, bool reliable = true) = 0;
    virtual bool Receive(Packet& out) = 0;  // false si no hay nada esperando
    virtual bool Open() const = 0;
    virtual void Close() = 0;
};

// Dos extremos unidos dentro del mismo proceso: lo que manda uno lo recibe el otro.
std::pair<std::shared_ptr<Transport>, std::shared_ptr<Transport>> MakeMemoryLink();

// ---- Red (fase 6): el mismo Transport, sobre ENet ----
// Quien hace de servidor escucha en un puerto y recibe un Transport por cada cliente que llega; el
// cliente obtiene el suyo al conectarse. Si el juego se compilo sin ENet, NetworkAvailable() es false y
// las otras dos devuelven nulo con su motivo.
inline constexpr int kDefaultPort = 27300;
bool NetworkAvailable();
class NetListener {
public:
    virtual ~NetListener() = default;
    virtual void Pump() = 0;                            // atiende la red; llamar en cada fotograma
    virtual std::shared_ptr<Transport> Accept() = 0;    // la siguiente conexion nueva, o nulo
};
std::unique_ptr<NetListener> ListenOn(int port, int maxClients, std::string* error = nullptr);
// Bloquea hasta timeoutSeconds esperando al servidor.
std::shared_ptr<Transport> ConnectTo(const std::string& host, int port, double timeoutSeconds, std::string* error = nullptr);

inline constexpr std::uint32_t kProtocolVersion = 1;

enum class MessageType : std::uint8_t { Hello = 1, Welcome, Refused, Controls, Command, Snapshot };

// Como entra un jugador nuevo en la partida.
enum class JoinAs : std::uint8_t {
    Host,     // el jugador que ya tiene la sesion (el primero); si ya esta cogido, como Crew
    Crew,     // en la nave del anfitrion, con los puestos que queden
    OwnShip,  // con una nave nueva, junto a la del anfitrion
};

struct Hello {
    std::uint32_t protocol = kProtocolVersion;
    int saveVersion = kSaveVersion;
    std::string id, name;
    JoinAs join = JoinAs::Host;
};

Packet EncodeHello(const Hello& hello);
Packet EncodeControls(const ShipControls& controls);
Packet EncodeCommand(const ShipCommand& command);
Packet EncodeSnapshot(const SnapshotData& data);
// false si el paquete no es de ese tipo o esta cortado; out queda sin tocar o a medias.
bool DecodeHello(const Packet& packet, Hello& out);
bool DecodeControls(const Packet& packet, ShipControls& out);
bool DecodeCommand(const Packet& packet, ShipCommand& out);
bool DecodeSnapshot(const Packet& packet, SnapshotData& out);
MessageType TypeOf(const Packet& packet);  // el primer byte; 0 si esta vacio

// El lado que simula. No es dueño de la sesion: la avanza a paso fijo con los mandos que le llegan y
// le manda a cada cliente lo que ve.
class GameServer {
public:
    explicit GameServer(GameSession& session) : session_(session) {}

    void Add(std::shared_ptr<Transport> link);  // una conexion nueva; el jugador llega con su Hello
    void Listen(std::unique_ptr<NetListener> listener) { listener_ = std::move(listener); }  // y las que lleguen por la red
    // Lee lo que haya llegado, avanza los pasos de 1/60 s que quepan en realSeconds y, tras cada
    // snapshotEvery pasos, manda una instantanea a cada cliente. Devuelve los pasos dados.
    int Update(double realSeconds, float focalPixels = 800.0f);
    // Manda ya una instantanea a todos: tras cargar una partida o cambiar algo sin avanzar el reloj.
    void SendSnapshots();

    int snapshotEvery = 1;
    int Clients() const;            // conexiones abiertas que ya han entrado
    std::uint64_t Tick() const { return tick_; }
    std::size_t BytesSent() const { return bytesSent_; }

private:
    struct Peer {
        std::shared_ptr<Transport> link;
        int player = -1;          // su jugador en la sesion; -1 hasta que saluda
        ShipControls controls;    // su mando, acumulado desde el ultimo paso
        bool gone = false;        // la conexion se ha cerrado y ya se ha avisado a la sesion
    };
    void Read(Peer& peer);
    void Greet(Peer& peer, const Hello& hello);
    void Refuse(Peer& peer, const std::string& why);

    GameSession& session_;
    std::unique_ptr<NetListener> listener_;
    std::vector<Peer> peers_;
    double pending_ = 0;
    std::uint64_t tick_ = 0;
    std::size_t bytesSent_ = 0;
};

// El lado que mira. Convierte la sesion que recibe en una replica y la mantiene al dia.
class GameClient {
public:
    GameClient(GameSession& replica, std::shared_ptr<Transport> link, const Hello& hello);

    void Update();  // aplica lo que haya llegado; llamar antes de dibujar
    bool Ready() const { return snapshots_ > 0; }  // ya ha recibido el mundo
    bool Refused() const { return !refusal_.empty(); }
    const std::string& Refusal() const { return refusal_; }
    std::uint64_t Snapshots() const { return snapshots_; }

private:
    GameSession& replica_;
    std::shared_ptr<Transport> link_;
    std::uint64_t snapshots_ = 0;
    std::string refusal_;
};

}  // namespace game
