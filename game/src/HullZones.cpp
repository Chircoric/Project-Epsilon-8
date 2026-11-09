#include "game/HullZones.h"

#include <algorithm>
#include <cmath>

namespace game {

using namespace eps;

namespace {

constexpr double kSixth = 1.0471975511965976;  // 60 grados: el paso entre bodegas
constexpr double kHoldStart = 0.5;             // angulo de la primera bodega (GameModels.cpp)

Vec3d HoldCenter(int i) {
    const double a = kSixth * static_cast<double>(i) + kHoldStart;
    return {std::cos(a) * 95.0, std::sin(a) * 95.0, 90.0};
}

// Distancia de p al tramo a-b.
double ToSegment(const Vec3d& p, const Vec3d& a, const Vec3d& b) {
    const Vec3d ab = b - a;
    const double t = std::clamp(Dot(p - a, ab) / Dot(ab, ab), 0.0, 1.0);
    return Length(p - (a + ab * t));
}

// La forma de una nave cargada de un .glb (SetZoneLayout), y su ficha de zonas con esos centros.
std::shared_ptr<const ZoneLayout> gLayout;
HullZoneInfo gCustomInfo[kHullZoneCount];

double BallsDistance(const std::vector<ZoneBall>& balls, const Vec3d& p) {
    double best = 1e300;
    for (const ZoneBall& b : balls) best = std::min(best, Length(p - b.center) - b.radius);
    return std::max(best, 0.0);
}

// Lo mas cerca que puede estar una zona: si ya hay otra mas cerca, no hace falta mirar sus bolas.
double ZoneBound(int zone, const Vec3d& p) {
    if (!gLayout) return 0.0;
    const ZoneBall& b = gLayout->bounds[static_cast<std::size_t>(zone)];
    return std::max(Length(p - b.center) - b.radius, 0.0);
}

// Distancia de un punto del casco a la superficie de cada zona, con su forma aproximada (m).
double ZoneDistance(int zone, const Vec3d& p) {
    if (gLayout) return BallsDistance(gLayout->balls[static_cast<std::size_t>(zone)], p);
    switch (static_cast<HullZone>(zone)) {
        case HullZone::Bow: return std::max(Length(p - Vec3d{0, 0, 470}) - 70.0, 0.0);
        case HullZone::Bridge: return std::max(Length(p - Vec3d{0, -80, 340}) - 90.0, 0.0);
        case HullZone::WarpRing: {
            const double across = std::sqrt(p.x * p.x + p.y * p.y) - 230.0;
            const double along = p.z + 120.0;
            return std::max(std::sqrt(across * across + along * along) - 18.0, 0.0);
        }
        case HullZone::PortNacelle:
        case HullZone::StarboardNacelle: {
            const double side = zone == static_cast<int>(HullZone::PortNacelle) ? -150.0 : 150.0;
            return std::max(ToSegment(p, {side, 0, -540}, {side, 0, -230}) - 55.0, 0.0);
        }
        case HullZone::PortRadiator:
        case HullZone::StarboardRadiator: {
            const double side = zone == static_cast<int>(HullZone::PortRadiator) ? -70.0 : 70.0;
            return std::max(ToSegment(p, {side, 110, -400}, {side, 110, -160}) - 90.0, 0.0);
        }
        case HullZone::Spine: return std::max(ToSegment(p, {0, 0, -450}, {0, 0, 450}) - 45.0, 0.0);
        case HullZone::Core: return std::max(Length(p - Vec3d{0, 0, -300}) - 38.0, 0.0);
        default: {
            const int hold = zone - static_cast<int>(HullZone::Hold1);
            return std::max(Length(p - HoldCenter(hold)) - 70.0, 0.0);
        }
    }
}

}  // namespace

const HullZoneInfo& ZoneInfo(int zone) {
    constexpr int kSpine = static_cast<int>(HullZone::Spine), kCore = static_cast<int>(HullZone::Core), kBridge = static_cast<int>(HullZone::Bridge);
    static const HullZoneInfo info[kHullZoneCount] = {
        {"proa", {0, 0, 470}, true, RedZone::None, kBridge, false},
        {"puente", {0, -80, 340}, false, RedZone::Shutdown, kSpine, true},
        {"bodega 1", HoldCenter(0), true, RedZone::None, kSpine, false},
        {"bodega 2", HoldCenter(1), true, RedZone::None, kSpine, false},
        {"bodega 3", HoldCenter(2), true, RedZone::None, kSpine, false},
        {"bodega 4", HoldCenter(3), true, RedZone::None, kSpine, false},
        {"bodega 5", HoldCenter(4), true, RedZone::None, kSpine, false},
        {"bodega 6", HoldCenter(5), true, RedZone::None, kSpine, false},
        {"anillo de curvatura", {0, 0, -120}, true, RedZone::None, kSpine, true},
        {"góndola de babor", {-150, 0, -380}, true, RedZone::None, kCore, false},
        {"góndola de estribor", {150, 0, -380}, true, RedZone::None, kCore, false},
        {"radiador de babor", {-70, 110, -280}, true, RedZone::None, kCore, true},
        {"radiador de estribor", {70, 110, -280}, true, RedZone::None, kCore, true},
        {"espina", {0, 0, 0}, false, RedZone::Shutdown, kCore, false},
        {"núcleo", {0, 0, -300}, false, RedZone::Explode, -1, true},
    };
    const int i = std::clamp(zone, 0, kHullZoneCount - 1);
    return gLayout ? gCustomInfo[i] : info[i];
}

// Se llama al cargar la nave, antes de que empiece la partida: luego solo se lee.
void SetZoneLayout(std::shared_ptr<const ZoneLayout> layout) {
    gLayout.reset();
    if (!layout) return;
    for (int i = 0; i < kHullZoneCount; ++i) {
        // La misma ficha, con el centro de la zona en la nave cargada.
        gCustomInfo[i] = ZoneInfo(i);
        gCustomInfo[i].center = layout->centers[static_cast<std::size_t>(i)];
    }
    gLayout = std::move(layout);
}

const ZoneLayout* CustomZoneLayout() { return gLayout.get(); }

namespace {

bool Has(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

std::string Lower(const std::string& text) {
    std::string out = text;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

}  // namespace

int ZoneOfPart(const std::string& rawName, const Vec3f& position) {
    const std::string name = Lower(rawName);
    // Babor y estribor: por el nombre si lo dice; si no, por el lado en que esta.
    const bool port = Has(name, "babor") || Has(name, "port") ? true : Has(name, "estribor") || Has(name, "starboard") ? false : position.x < 0.0f;
    const auto side = [&](HullZone p, HullZone s) { return static_cast<int>(port ? p : s); };
    if (Has(name, "nucleo") || Has(name, "core") || Has(name, "reactor")) return static_cast<int>(HullZone::Core);
    if (Has(name, "puente") || Has(name, "ventanas") || Has(name, "bridge") || Has(name, "cabina")) return static_cast<int>(HullZone::Bridge);
    if (Has(name, "proa") || Has(name, "bow") || Has(name, "nose")) return static_cast<int>(HullZone::Bow);
    if (Has(name, "anillo") || Has(name, "ring") || Has(name, "brazo")) return static_cast<int>(HullZone::WarpRing);
    if (Has(name, "gondola") || Has(name, "tobera") || Has(name, "escape") || Has(name, "nacelle") || Has(name, "motor") || Has(name, "engine")) return side(HullZone::PortNacelle, HullZone::StarboardNacelle);
    if (Has(name, "radiador") || Has(name, "radiator")) return side(HullZone::PortRadiator, HullZone::StarboardRadiator);
    if (Has(name, "espina") || Has(name, "spine")) return static_cast<int>(HullZone::Spine);
    if (Has(name, "bodega") || Has(name, "hold")) {
        // Con numero ("bodega_3"), esa; sin el, la del angulo en que esta.
        for (char d = '1'; d <= '6'; ++d)
            if (name.find(d) != std::string::npos) return static_cast<int>(HullZone::Hold1) + (d - '1');
        const double a = std::atan2(static_cast<double>(position.y), static_cast<double>(position.x)) - kHoldStart;
        int i = static_cast<int>(std::lround(a / kSixth)) % 6;
        if (i < 0) i += 6;
        return static_cast<int>(HullZone::Hold1) + i;
    }
    // Sin nombre conocido: la zona que tenga mas cerca.
    return NearestZone(Vec3d{position.x, position.y, position.z});
}

int NearestZone(const Vec3d& p) {
    int best = static_cast<int>(HullZone::Spine);
    double nearest = 1e300;
    for (int z = 0; z < kHullZoneCount; ++z) {
        if (ZoneBound(z, p) >= nearest) continue;
        const double d = ZoneDistance(z, p);
        if (d < nearest) {
            nearest = d;
            best = z;
        }
    }
    return best;
}

// Avanza a saltos tan largos como la distancia a la pieza mas cercana: nunca se salta ninguna.
bool TraceZones(const Vec3d& origin, const Vec3d& direction, double length, int& zone, Vec3d& point) {
    double travelled = 0.0;
    for (int step = 0; step < 512 && travelled <= length; ++step) {
        const Vec3d p = origin + direction * travelled;
        double nearest = 1e300;
        int best = -1;
        for (int z = 0; z < kHullZoneCount; ++z) {
            if (ZoneBound(z, p) >= nearest) continue;
            const double d = ZoneDistance(z, p);
            if (d < nearest) {
                nearest = d;
                best = z;
            }
        }
        if (nearest < 0.5) {
            zone = best;
            point = p;
            return true;
        }
        travelled += std::max(nearest, 0.5);
    }
    return false;
}

ZoneImpact ProjectileImpact(const Vec3d& start, const Vec3d& end) {
    ZoneImpact impact;
    const Vec3d path = end - start;
    const double length = Length(path);
    const Vec3d direction = length > 1e-9 ? path * (1.0 / length) : Normalize(Vec3d{0, 0, 0} - start + Vec3d{0, 0, 1e-9});
    impact.from = direction * -1.0;
    // La recta entera, desde bien fuera de la nave: un paso del proyectil puede ser mas largo que ella.
    const double closest = Dot(Vec3d{0, 0, 0} - start, direction);
    const Vec3d nearPoint = start + direction * closest;
    const double reach = gLayout ? std::max(1500.0, Length(gLayout->extent) * 1.2) : 1500.0;
    const Vec3d entry = nearPoint - direction * reach;
    if (TraceZones(entry, direction, reach * 2.0, impact.zone, impact.point)) return impact;
    impact.zone = NearestZone(nearPoint);
    impact.point = nearPoint;
    return impact;
}

double EngineOutput(const HullZones& z) {
    // Una gondola danada empuja menos; sin las dos quedan los propulsores de maniobra.
    const auto nacelle = [](double integrity) { return integrity <= 0.0 ? 0.0 : 0.5 * std::sqrt(integrity); };
    return std::max(nacelle(Zone(z, HullZone::PortNacelle)) + nacelle(Zone(z, HullZone::StarboardNacelle)), 0.08);
}

double WarpIntegrity(const HullZones& z) { return Zone(z, HullZone::WarpRing); }

double RadiatorOutput(const HullZones& z) { return 0.25 + 0.375 * (Zone(z, HullZone::PortRadiator) + Zone(z, HullZone::StarboardRadiator)); }

double SteeringOutput(const HullZones& z) { return 0.4 + 0.6 * Zone(z, HullZone::Spine); }

double ReactorOutput(const HullZones& z) { return 0.25 + 0.75 * Zone(z, HullZone::Core); }

double AimSpread(const HullZones& z) { return 1.0 + 3.0 * (1.0 - Zone(z, HullZone::Bridge)); }

int HoldsLeft(const HullZones& z) {
    int left = 0;
    for (int h = static_cast<int>(HullZone::Hold1); h <= static_cast<int>(HullZone::Hold6); ++h) left += ZoneLost(z, h) ? 0 : 1;
    return left;
}

// El golpe entra por la envoltura de la nave en esa direccion. Las zonas mas cercanas a ese punto
// se lo reparten: la mas cercana casi siempre, alguna vecina a veces.
int ZoneHitFrom(const Vec3d& localDirection, double roll) {
    const double length = Length(localDirection);
    const Vec3d d = length > 1e-12 ? localDirection * (1.0 / length) : Vec3d{0, 0, 1};
    const Vec3d box = gLayout ? gLayout->extent : Vec3d{250.0, 250.0, 560.0};
    const Vec3d entry{d.x * box.x, d.y * box.y, d.z * box.z};
    std::array<double, kHullZoneCount> weight{};
    double total = 0.0;
    for (int z = 0; z < kHullZoneCount; ++z) {
        const double gap = ZoneDistance(z, entry) + 40.0;
        weight[static_cast<std::size_t>(z)] = 1.0 / (gap * gap * gap * gap);
        total += weight[static_cast<std::size_t>(z)];
    }
    double pick = std::clamp(roll, 0.0, 0.999999) * total;
    for (int z = 0; z < kHullZoneCount; ++z) {
        pick -= weight[static_cast<std::size_t>(z)];
        if (pick <= 0.0) return z;
    }
    return kHullZoneCount - 1;
}

}  // namespace game
