#include "game/Combat.h"

#include <algorithm>
#include <cmath>

namespace game {

using eps::Vec3d;

namespace {

constexpr double kLight = 299792.458;  // km/s

}  // namespace

const char* Name(DamageType type) {
    static const char* const names[] = {"Cinético", "Fotónico", "Antimateria", "Térmico"};
    return names[static_cast<int>(type)];
}

const char* Name(WeaponKind kind) {
    static const char* const names[] = {"Torreta", "Canón espinal", "Misil", "Drones"};
    return names[static_cast<int>(kind)];
}

WeaponRules Scaled(const CombatRules& rules, WeaponKind kind, int tier) {
    WeaponRules w = rules.weapons[static_cast<int>(kind)];
    const int level = std::clamp(tier, 1, rules.maxTier) - 1;
    w.damage *= 1.0 + rules.tierDamage * level;
    w.cooldown /= 1.0 + rules.tierCooldown * level;
    return w;
}

Vec3d GravityAt(const std::vector<GravitySource>& sources, const Vec3d& at, double scale) {
    Vec3d a;
    for (const GravitySource& s : sources) {
        const Vec3d r = s.position - at;
        const double d2 = eps::Dot(r, r) + 1.0;
        a += r * (s.mu * scale / (d2 * std::sqrt(d2)));
    }
    return a;
}

std::vector<GravitySource> Strongest(const std::vector<GravitySource>& sources, const Vec3d& at, std::size_t count) {
    std::vector<GravitySource> out = sources;
    const auto pull = [&](const GravitySource& s) {
        const Vec3d r = s.position - at;
        return s.mu / (eps::Dot(r, r) + 1.0);
    };
    std::sort(out.begin(), out.end(), [&](const GravitySource& a, const GravitySource& b) { return pull(a) > pull(b); });
    if (out.size() > count) out.resize(count);
    return out;
}

double AimError(const CombatRules& rules, int tier) {
    return rules.computerError / std::pow(static_cast<double>(std::clamp(tier, 1, rules.maxTier)), rules.computerErrorPower);
}

Vec3d Scatter(const Vec3d& direction, double error, std::mt19937& rng) {
    if (error <= 0.0) return direction;
    std::normal_distribution<double> noise(0.0, error);
    const Vec3d d = eps::Normalize(direction);
    const Vec3d u = eps::Normalize(eps::Cross(d, std::abs(d.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0}));
    const Vec3d v = eps::Cross(d, u);
    return eps::Normalize(d + u * noise(rng) + v * noise(rng));
}

FiringSolution Solve(const Vec3d& origin, const Vec3d& shooterVelocity, const Vec3d& targetPosition, const Vec3d& targetVelocity, double speed,
                     bool beam, const std::vector<GravitySource>& sources, int tier, const CombatRules& rules) {
    FiringSolution out;
    const double s = beam ? kLight : speed;
    const Vec3d rel = targetPosition - origin;
    const Vec3d w = beam ? targetVelocity : targetVelocity - shooterVelocity;

    // Sin gravedad: (w.w - s2) t2 + 2 rel.w t + rel.rel = 0. De las dos raices, la primera positiva.
    const double a = eps::Dot(w, w) - s * s, b = 2.0 * eps::Dot(rel, w), c = eps::Dot(rel, rel);
    double t = -1.0;
    if (std::abs(a) < 1e-9) {
        if (b < 0.0) t = -c / b;
    } else {
        const double disc = b * b - 4.0 * a * c;
        if (disc >= 0.0) {
            const double root = std::sqrt(disc);
            const double t1 = (-b - root) / (2.0 * a), t2 = (-b + root) / (2.0 * a);
            for (const double candidate : {t1, t2}) {
                if (candidate > 0.0 && (t < 0.0 || candidate < t)) t = candidate;
            }
        }
    }
    if (t <= 0.0 || !std::isfinite(t)) return out;

    Vec3d dir = eps::Normalize(rel + w * t);
    const int level = std::clamp(tier, 1, rules.maxTier);
    const std::vector<GravitySource> known = Strongest(sources, origin, static_cast<std::size_t>(1 + rules.bodiesPerTier * (level - 1)));
    constexpr int kSteps = 96;
    const int corrections = 1 + level;
    double flight = t, miss = 0.0;

    if (!known.empty() && rules.gravityScale > 0.0) {
        for (int k = 0; k < corrections; ++k) {
            Projectile p;
            p.beam = beam;
            p.position = origin;
            p.velocity = beam ? dir * s : shooterVelocity + dir * s;
            const double total = flight * 1.25 + 1e-3, dt = total / kSteps;
            double bestSq = 1e300, bestTau = flight;
            Vec3d bestVec;
            Vec3d r0 = p.position - targetPosition;
            for (int i = 1; i <= kSteps; ++i) {
                StepProjectile(p, known, rules, dt);
                const double tau = i * dt;
                const Vec3d r1 = p.position - (targetPosition + targetVelocity * tau);
                // El punto de mayor acercamiento dentro de este tramo, que es casi una recta.
                const Vec3d d = r1 - r0;
                const double dd = eps::Dot(d, d);
                const double u = dd > 0.0 ? std::clamp(-eps::Dot(r0, d) / dd, 0.0, 1.0) : 0.0;
                const Vec3d at = r0 + d * u;
                const double sq = eps::Dot(at, at);
                if (sq < bestSq) {
                    bestSq = sq;
                    bestVec = at;
                    bestTau = tau - dt + u * dt;
                }
                r0 = r1;
            }
            miss = std::sqrt(bestSq);
            flight = std::max(bestTau, 1e-3);
            const Vec3d across = bestVec - dir * eps::Dot(bestVec, dir);
            if (miss < 1e-6 * std::max(eps::Length(rel), 1.0)) break;
            dir = eps::Normalize(dir - across * (1.0 / (s * flight)));
        }
    }
    out.valid = true;
    out.direction = dir;
    out.flightTime = flight;
    out.impact = targetPosition + targetVelocity * flight;
    out.miss = miss;
    return out;
}

void StepProjectile(Projectile& p, const std::vector<GravitySource>& sources, const CombatRules& rules, double dt, const Vec3d* targetPosition,
                    const Vec3d* targetVelocity) {
    const Vec3d g = GravityAt(sources, p.position, rules.gravityScale);
    if (p.beam) {
        // La luz se curva, pero no cambia de velocidad.
        const double speed = std::max(eps::Length(p.velocity), 1.0);
        const Vec3d dir = p.velocity * (1.0 / speed);
        const Vec3d across = g - dir * eps::Dot(g, dir);
        const Vec3d before = p.velocity;
        p.velocity = eps::Normalize(p.velocity + across * (2.0 * dt)) * speed;
        p.position += (before + p.velocity) * (0.5 * dt);
        p.life -= dt;
        return;
    }
    if (!p.guided) {
        // Salto de rana: de segundo orden, para que el computador y el vuelo real coincidan aunque
        // no usen el mismo paso.
        p.position += p.velocity * dt + g * (0.5 * dt * dt);
        p.velocity += (g + GravityAt(sources, p.position, rules.gravityScale)) * (0.5 * dt);
        p.life -= dt;
        return;
    }
    {
        if (p.guided && targetPosition && p.turn > 0.0) {
            const double speed = std::max(eps::Length(p.velocity), 1.0);
            const Vec3d dir = p.velocity * (1.0 / speed);
            const Vec3d to = *targetPosition - p.position;
            const double distance = std::max(eps::Length(to), 1.0);
            const Vec3d lead = *targetPosition + (targetVelocity ? *targetVelocity : Vec3d{}) * (distance / std::max(p.cruise, 1.0)) - p.position;
            const Vec3d want = eps::Normalize(lead);
            const double angle = std::acos(std::clamp(eps::Dot(dir, want), -1.0, 1.0));
            const double step = std::min(angle, p.turn * dt);
            Vec3d side = want - dir * eps::Dot(want, dir);
            Vec3d turned = dir;
            if (eps::Length(side) > 1e-12) turned = eps::Normalize(dir * std::cos(step) + eps::Normalize(side) * std::sin(step));
            const double faster = speed + (p.cruise - speed) * std::min(dt / 10.0, 1.0);
            p.velocity = turned * faster;
        }
        p.velocity += g * dt;
    }
    p.position += p.velocity * dt;
    p.life -= dt;
}

bool SegmentNear(const Vec3d& a, const Vec3d& b, const Vec3d& center, double radius, double* t) {
    const Vec3d d = b - a, f = a - center;
    const double dd = eps::Dot(d, d);
    const double u = dd > 0.0 ? std::clamp(-eps::Dot(f, d) / dd, 0.0, 1.0) : 0.0;
    const Vec3d at = f + d * u;
    if (t) *t = u;
    return eps::Dot(at, at) <= radius * radius;
}

}  // namespace game
