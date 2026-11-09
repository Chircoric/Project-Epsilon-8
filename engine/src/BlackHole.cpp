#include "epsilon/BlackHole.h"

#include <algorithm>
#include <cmath>

#include "epsilon/Astro.h"

namespace eps::astro {

namespace {

// Arrastre del espacio, calibrado para que la anchura de la sombra coincida con la exacta. Por
// encima de 0.9 el metodo pierde estabilidad, asi que su efecto se limita ahi.
constexpr double kFrameDragging = 0.30;
constexpr double kMaxDragSpin = 0.9;

// Radio de la orbita circular de fotones, en masas M. Bardeen (1973).
double PhotonOrbit(double spin, double sign) {
    return 2.0 * (1.0 + std::cos(2.0 / 3.0 * std::acos(std::clamp(sign * spin, -1.0, 1.0))));
}

}  // namespace

double SchwarzschildRadius(double massKg) { return 2.0 * kGravity * massKg / (kLightSpeed * kLightSpeed); }

double KerrHorizon(double spin) { return 0.5 * (1.0 + std::sqrt(std::max(1.0 - spin * spin, 0.0))); }

double InnermostStableOrbit(double spin) {
    // Bardeen, Press y Teukolsky (1972), en masas M; se divide entre 2 para pasar a rs.
    const double a = std::clamp(spin, 0.0, 0.9999);
    const double z1 = 1.0 + std::cbrt(1.0 - a * a) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    const double z2 = std::sqrt(3.0 * a * a + z1 * z1);
    return 0.5 * (3.0 + z2 - std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2)));
}

double PhotonSphere(double spin, bool prograde) { return 0.5 * PhotonOrbit(spin, prograde ? -1.0 : 1.0) * 1.0; }

void KerrShadowEdges(double spin, double& left, double& right) {
    if (spin < 1e-6) {
        left = -std::sqrt(27.0) * 0.5;
        right = std::sqrt(27.0) * 0.5;
        return;
    }
    // Parametro de impacto de cada orbita de fotones ecuatorial.
    const auto impact = [spin](double r) {
        return -(r * r * r - 3.0 * r * r + spin * spin * r + spin * spin) / (spin * (r - 1.0));
    };
    left = -impact(PhotonOrbit(spin, 1.0)) * 0.5;
    right = -impact(PhotonOrbit(spin, -1.0)) * 0.5;
    if (left > right) std::swap(left, right);
}

LensResult TraceLensRay(const Vec3d& origin, const Vec3d& direction, double spin, int maxSteps, double reach, double frameDragging) {
    const double drag = frameDragging < 0.0 ? kFrameDragging : frameDragging;
    const Vec3d axis{0, 1, 0};
    const double horizon = KerrHorizon(spin);
    Vec3d x = origin, v = Normalize(direction);
    const double h2 = Dot(Cross(x, v), Cross(x, v));

    LensResult out;
    out.closest = Length(x);
    for (int i = 0; i < maxSteps; ++i) {
        const double r = Length(x);
        out.closest = std::min(out.closest, r);
        out.steps = i + 1;
        if (r < horizon) {
            out.captured = true;
            return out;
        }
        if (r > reach && Dot(x, v) > 0.0) break;

        const double dt = std::clamp(r * 0.06, 0.008, 1.5);
        const double r2 = r * r, r5 = r2 * r2 * r;
        Vec3d a = x * (-1.5 * h2 / r5);
        if (spin > 0.0) {
            const Vec3d n = x * (1.0 / r);
            const Vec3d field = (n * (3.0 * Dot(axis, n)) - axis) * (drag * std::min(spin, kMaxDragSpin) / (r2 * r));
            a += Cross(v, field);
        }
        v += a * dt;
        x += v * dt;
    }
    out.direction = Normalize(v);
    return out;
}

}  // namespace eps::astro
