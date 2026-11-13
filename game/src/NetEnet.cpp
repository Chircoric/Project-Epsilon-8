// NetEnet.cpp - El transporte de red, sobre ENet (UDP con canales fiables y no fiables).
//
// Va aparte de Net.cpp porque enet.h arrastra las cabeceras de Windows. Si el proyecto se configura sin
// ENet, este archivo no se compila y Net.cpp da las versiones que dicen que no hay red.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <deque>

#include "game/Net.h"

namespace game {

namespace {

// ENet se inicia una vez por proceso, la primera vez que hace falta.
bool EnsureEnet() {
    static const bool ready = [] {
        if (enet_initialize() != 0) return false;
        std::atexit(enet_deinitialize);
        return true;
    }();
    return ready;
}

constexpr enet_uint8 kUnreliableChannel = 0, kReliableChannel = 1;
constexpr std::size_t kChannels = 2;

ENetPacket* MakePacket(const Packet& packet, bool reliable) {
    // Una instantanea no cabe en un datagrama: sin fiabilidad va en trozos, y si se pierde uno se pierde
    // entera, que es lo que se quiere (la siguiente la sustituye).
    return enet_packet_create(packet.data(), packet.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
}

// Un cliente visto desde el servidor. El host de ENet es del NetListener, que es quien reparte lo que llega.
class PeerLink final : public Transport {
public:
    PeerLink(ENetHost* host, ENetPeer* peer) : host_(host), peer_(peer) {}
    void Send(const Packet& packet, bool reliable) override {
        if (!peer_ || packet.empty()) return;
        if (enet_peer_send(peer_, reliable ? kReliableChannel : kUnreliableChannel, MakePacket(packet, reliable)) == 0) enet_host_flush(host_);
    }
    bool Receive(Packet& out) override {
        if (inbox.empty()) return false;
        out = std::move(inbox.front());
        inbox.pop_front();
        return true;
    }
    bool Open() const override { return peer_ != nullptr; }
    void Close() override {
        if (!peer_) return;
        enet_peer_disconnect(peer_, 0);
        enet_host_flush(host_);
        peer_->data = nullptr;
        peer_ = nullptr;
    }
    void Dropped() { peer_ = nullptr; }  // la conexion se ha caido o el servidor se cierra
    std::deque<Packet> inbox;

private:
    ENetHost* host_;
    ENetPeer* peer_;
};

class EnetListener final : public NetListener {
public:
    explicit EnetListener(ENetHost* host) : host_(host) {}
    ~EnetListener() override {
        for (const std::shared_ptr<PeerLink>& link : links_) link->Dropped();
        enet_host_destroy(host_);
    }
    void Pump() override {
        ENetEvent event;
        while (enet_host_service(host_, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    auto link = std::make_shared<PeerLink>(host_, event.peer);
                    event.peer->data = link.get();
                    links_.push_back(link);
                    waiting_.push_back(link);
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE:
                    if (auto* link = static_cast<PeerLink*>(event.peer->data)) link->inbox.emplace_back(event.packet->data, event.packet->data + event.packet->dataLength);
                    enet_packet_destroy(event.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    if (auto* link = static_cast<PeerLink*>(event.peer->data)) link->Dropped();
                    event.peer->data = nullptr;
                    break;
                default: break;
            }
        }
        links_.erase(std::remove_if(links_.begin(), links_.end(), [](const std::shared_ptr<PeerLink>& l) { return !l->Open() && l.use_count() == 1; }), links_.end());
    }
    std::shared_ptr<Transport> Accept() override {
        if (waiting_.empty()) return nullptr;
        std::shared_ptr<Transport> next = waiting_.front();
        waiting_.pop_front();
        return next;
    }

private:
    ENetHost* host_;
    std::vector<std::shared_ptr<PeerLink>> links_;
    std::deque<std::shared_ptr<PeerLink>> waiting_;
};

// El lado del cliente: su propio host de ENet, con una sola conexion.
class ClientLink final : public Transport {
public:
    ClientLink(ENetHost* host, ENetPeer* peer) : host_(host), peer_(peer) {}
    ~ClientLink() override {
        if (peer_) {
            enet_peer_disconnect(peer_, 0);
            enet_host_flush(host_);  // que el servidor se entere ya, sin esperar a que caduque
        }
        enet_host_destroy(host_);
    }
    void Send(const Packet& packet, bool reliable) override {
        if (!peer_ || packet.empty()) return;
        if (enet_peer_send(peer_, reliable ? kReliableChannel : kUnreliableChannel, MakePacket(packet, reliable)) == 0) enet_host_flush(host_);
    }
    bool Receive(Packet& out) override {
        Pump();
        if (inbox_.empty()) return false;
        out = std::move(inbox_.front());
        inbox_.pop_front();
        return true;
    }
    bool Open() const override { return peer_ != nullptr; }
    void Close() override {
        if (!peer_) return;
        enet_peer_disconnect(peer_, 0);
        enet_host_flush(host_);
        peer_ = nullptr;
    }

private:
    void Pump() {
        ENetEvent event;
        while (enet_host_service(host_, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                inbox_.emplace_back(event.packet->data, event.packet->data + event.packet->dataLength);
                enet_packet_destroy(event.packet);
            } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                peer_ = nullptr;
            }
        }
    }
    ENetHost* host_;
    ENetPeer* peer_;
    std::deque<Packet> inbox_;
};

}  // namespace

bool NetworkAvailable() { return true; }

std::unique_ptr<NetListener> ListenOn(int port, int maxClients, std::string* error) {
    const auto fail = [&](const char* why) -> std::unique_ptr<NetListener> {
        if (error) *error = why;
        return nullptr;
    };
    if (!EnsureEnet()) return fail("no se pudo iniciar la red");
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = static_cast<enet_uint16>(port);
    ENetHost* host = enet_host_create(&address, static_cast<std::size_t>(std::max(maxClients, 1)), kChannels, 0, 0);
    if (!host) return fail("no se pudo abrir el puerto: puede que ya lo use otro servidor");
    return std::make_unique<EnetListener>(host);
}

std::shared_ptr<Transport> ConnectTo(const std::string& hostName, int port, double timeoutSeconds, std::string* error) {
    const auto fail = [&](const char* why) -> std::shared_ptr<Transport> {
        if (error) *error = why;
        return nullptr;
    };
    if (!EnsureEnet()) return fail("no se pudo iniciar la red");
    ENetHost* host = enet_host_create(nullptr, 1, kChannels, 0, 0);
    if (!host) return fail("no se pudo iniciar la red");
    ENetAddress address;
    address.port = static_cast<enet_uint16>(port);
    if (enet_address_set_host(&address, hostName.c_str()) != 0) {
        enet_host_destroy(host);
        return fail("no se encuentra ese servidor");
    }
    ENetPeer* peer = enet_host_connect(host, &address, kChannels, 0);
    if (!peer) {
        enet_host_destroy(host);
        return fail("no se pudo iniciar la conexión");
    }
    // Se espera la respuesta del servidor, a trozos cortos, hasta el tiempo dado.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);
    ENetEvent event;
    while (std::chrono::steady_clock::now() < deadline) {
        if (enet_host_service(host, &event, 50) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) return std::make_shared<ClientLink>(host, peer);
    }
    enet_peer_reset(peer);
    enet_host_destroy(host);
    return fail("el servidor no responde");
}

}  // namespace game
