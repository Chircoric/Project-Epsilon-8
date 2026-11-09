// HullZones.h - El casco por zonas: donde pega cada golpe, que se rompe y que se pierde.
//
// Cada zona lleva su integridad, de 1 (entera) a 0 (deshecha). Un golpe con direccion cae en la
// zona de ese lado; el dano sin direccion (calor, presion) se reparte entre todas. Una zona
// periferica que llega a 0 se suelta de la nave y se pierde, y lo que sobra del golpe sigue hacia
// la zona de dentro (inner): asi el dano estructural acaba llegando al centro.
//
// Zonas rojas: el puente y la espina apagan la nave al caer (sin mando, sin energia); el nucleo
// la hace estallar. Un golpe directo en una zona roja hace mas dano (kCriticalHit).
//
// Cada zona sostiene un sistema: las gondolas empujan, el anillo curva el espacio, los radiadores
// enfrian, la proa lleva las placas de los conos, las bodegas la carga, el puente apunta, la
// espina reparte la maniobra y el nucleo alimenta el reactor.
//
// Esto es logica de juego y corre igual en todas las calidades. Lo que cambia con la calidad es
// solo si el casco ensena el dano (GameLayer y mesh.hlsl).
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "epsilon/Math3D.h"

namespace game {

enum class HullZone : int {
    Bow, Bridge,
    Hold1, Hold2, Hold3, Hold4, Hold5, Hold6,
    WarpRing,
    PortNacelle, StarboardNacelle,
    PortRadiator, StarboardRadiator,
    Spine, Core,
    Count
};

inline constexpr int kHullZoneCount = static_cast<int>(HullZone::Count);

// Lo que pasa cuando una zona roja llega a 0.
enum class RedZone : int { None, Shutdown, Explode };

struct HullZoneInfo {
    const char* name;
    eps::Vec3d center;  // m, en el modelo de la nave: +Z es la proa
    bool losable;       // se suelta al llegar a 0
    RedZone red;
    int inner;          // zona a la que pasa lo que sobra del golpe; -1 = ninguna
    bool masculine;     // para el articulo: "el puente", "la proa"
};

const HullZoneInfo& ZoneInfo(int zone);

// La forma de las zonas sacada de un modelo de verdad (una nave de un .glb): bolas que cubren las
// piezas de cada zona, su centro y su planta para el panel Estructura. Sin ella, las zonas tienen
// la forma de la maqueta. Se fija una vez, al cargar la nave, y vale para todo el juego.
struct ZoneBall {
    eps::Vec3d center;  // m, en el modelo
    double radius = 0;
};
struct ZoneRect {
    double x0 = 0, z0 = 0, x1 = 0, z1 = 0;  // m: lo que ocupa una pieza vista desde arriba
};
struct ZoneLayout {
    std::array<std::vector<ZoneBall>, kHullZoneCount> balls;
    std::array<ZoneBall, kHullZoneCount> bounds{};  // una bola que envuelve cada zona: para descartar rapido
    std::array<eps::Vec3d, kHullZoneCount> centers{};
    std::array<std::vector<ZoneRect>, kHullZoneCount> plan;
    eps::Vec3d extent{250, 250, 560};  // m: media caja de la nave, desde el origen
    double ringRadius = 230.0;          // m: del eje al anillo de curvatura
};
// nullptr vuelve a la maqueta.
void SetZoneLayout(std::shared_ptr<const ZoneLayout> layout);
const ZoneLayout* CustomZoneLayout();

// Zona de una pieza del modelo de la nave. Primero por el nombre (el de la maqueta o el de la zona,
// tambien en un .glb: "proa", "gondola_babor", "bodega_3"...); si no, la zona mas cercana a su centro.
int ZoneOfPart(const std::string& name, const eps::Vec3f& position);

// Zona que recibe un golpe que llega desde esa direccion (unitaria, en el modelo). roll, de 0 a 1,
// reparte los golpes entre las zonas de ese lado: no siempre cae en la misma.
int ZoneHitFrom(const eps::Vec3d& localDirection, double roll);

// La zona mas cercana a un punto del modelo (m).
int NearestZone(const eps::Vec3d& point);

// Sigue una trayectoria por el modelo (m; direction unitaria) y devuelve la primera zona que
// toca y donde. false si pasa entre las piezas sin tocar ninguna.
bool TraceZones(const eps::Vec3d& origin, const eps::Vec3d& direction, double length, int& zone, eps::Vec3d& point);

// Donde da de verdad un proyectil: su trayectoria respecto a la nave, en el modelo (m). Si pasa
// rozando sin tocar nada (o estalla por proximidad), cuenta la zona mas cercana a su paso.
struct ZoneImpact {
    int zone = 0;
    eps::Vec3d point;               // m, en el modelo
    eps::Vec3d from{0, 0, 1};       // unitaria: de donde llego
};
ZoneImpact ProjectileImpact(const eps::Vec3d& start, const eps::Vec3d& end);

// Lo que una zona aguanta de golpes dirigidos, en partes del casco entero: con 0.3, la zona se
// deshace tras recibir de lleno el 30 % del casco.
inline constexpr double kZoneToughness = 0.3;
inline constexpr double kCriticalHit = 1.5;     // un golpe directo en zona roja
inline constexpr double kInnerShare = 0.7;      // parte del sobrante que llega a la zona de dentro
inline constexpr double kWarpRingMinimum = 0.25;  // por debajo, el anillo ya no curva el espacio

using HullZones = std::array<double, kHullZoneCount>;

inline HullZones IntactHull() {
    HullZones z;
    z.fill(1.0);
    return z;
}

inline double Zone(const HullZones& zones, HullZone zone) { return zones[static_cast<std::size_t>(zone)]; }
inline bool ZoneLost(const HullZones& zones, int zone) { return ZoneInfo(zone).losable && zones[static_cast<std::size_t>(zone)] <= 0.0; }

// Lo que queda de cada sistema, de 0 a 1.
double EngineOutput(const HullZones& zones);      // empuje: dos gondolas y, sin ellas, los propulsores de maniobra
double WarpIntegrity(const HullZones& zones);     // el anillo; por debajo de kWarpRingMinimum no hay curvatura
double RadiatorOutput(const HullZones& zones);    // lo que enfrian los radiadores
double SteeringOutput(const HullZones& zones);    // la espina reparte la maniobra
double ReactorOutput(const HullZones& zones);     // el nucleo alimenta el reactor
double AimSpread(const HullZones& zones);         // multiplica el error de punteria: el puente apunta
int HoldsLeft(const HullZones& zones);

}  // namespace game
