// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Chircoric
#include "game/ShipWreck.h"

#include <algorithm>
#include <cmath>

namespace game {

using eps::Quatd;
using eps::Vec3d;

double WreckRandom(std::uint32_t seed, std::uint32_t salt) {
    std::uint32_t h = seed * 0x9E3779B1u + salt * 0x85EBCA77u + 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return static_cast<double>(h >> 8) / 16777216.0;
}

namespace {

struct Group {
    std::vector<int> parts;
    Vec3d center, half;
    double radius = 0;
};

// Centro (pesando cada pieza por su volumen) y radio que las envuelve.
void Measure(Group& g, const std::vector<WreckPart>& parts) {
    Vec3d sum;
    double weight = 0;
    for (const int i : g.parts) {
        const WreckPart& p = parts[static_cast<std::size_t>(i)];
        const double w = std::max(p.radius * p.radius * p.radius, 1e-9);
        sum += p.position * w;
        weight += w;
    }
    g.center = weight > 0.0 ? sum * (1.0 / weight) : Vec3d{};
    g.radius = 0;
    g.half = {};
    for (const int i : g.parts) {
        const WreckPart& p = parts[static_cast<std::size_t>(i)];
        const Vec3d d = p.position - g.center;
        g.radius = std::max(g.radius, Length(d) + p.radius);
        // Una pieza no es una bola: de su radio solo cuenta una parte en cada eje.
        g.half = {std::max(g.half.x, std::fabs(d.x) + p.radius * 0.6), std::max(g.half.y, std::fabs(d.y) + p.radius * 0.6), std::max(g.half.z, std::fabs(d.z) + p.radius * 0.6)};
    }
}

double Axis(const Vec3d& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

// Parte un grupo en dos por el eje en el que mas se extiende. false si no se puede.
bool Split(Group& g, Group& other, const std::vector<WreckPart>& parts) {
    if (g.parts.size() < 2) return false;
    Vec3d low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
    for (const int i : g.parts) {
        const Vec3d& p = parts[static_cast<std::size_t>(i)].position;
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    const Vec3d span = high - low;
    const int axis = span.x >= span.y && span.x >= span.z ? 0 : span.y >= span.z ? 1 : 2;
    std::stable_sort(g.parts.begin(), g.parts.end(), [&](int a, int b) { return Axis(parts[static_cast<std::size_t>(a)].position, axis) < Axis(parts[static_cast<std::size_t>(b)].position, axis); });
    // Por el centro del tramo; si todo cae a un lado (piezas en el mismo sitio), por la mitad de la lista.
    const double middle = (Axis(low, axis) + Axis(high, axis)) * 0.5;
    std::size_t cut = 0;
    while (cut < g.parts.size() && Axis(parts[static_cast<std::size_t>(g.parts[cut])].position, axis) < middle) ++cut;
    if (cut == 0 || cut == g.parts.size()) cut = g.parts.size() / 2;
    other.parts.assign(g.parts.begin() + static_cast<std::ptrdiff_t>(cut), g.parts.end());
    g.parts.resize(cut);
    Measure(g, parts);
    Measure(other, parts);
    return true;
}

}  // namespace

WreckPlan PlanWreck(const std::vector<WreckPart>& parts, const Wreck& wreck, int maxBodies, const DestructionRules& rules) {
    WreckPlan plan;
    plan.bodyOfPart.assign(parts.size(), -1);
    if (maxBodies <= 0) return plan;

    // Por zonas: lo que se rompe junto es lo que el casco ya tenia junto.
    std::vector<Group> groups;
    std::vector<int> groupOfZone(static_cast<std::size_t>(kHullZoneCount) + 1, -1);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const WreckPart& p = parts[i];
        if (p.gone || (p.zone >= 0 && p.zone < kHullZoneCount && ZoneLost(wreck.zones, p.zone))) continue;
        const std::size_t slot = p.zone >= 0 && p.zone < kHullZoneCount ? static_cast<std::size_t>(p.zone) : static_cast<std::size_t>(kHullZoneCount);
        if (groupOfZone[slot] < 0) {
            groupOfZone[slot] = static_cast<int>(groups.size());
            groups.emplace_back();
        }
        groups[static_cast<std::size_t>(groupOfZone[slot])].parts.push_back(static_cast<int>(i));
    }
    for (Group& g : groups) Measure(g, parts);

    // Si sobran, el mas pequeno se une al que tiene mas cerca.
    while (static_cast<int>(groups.size()) > maxBodies) {
        std::size_t small = 0;
        for (std::size_t i = 1; i < groups.size(); ++i)
            if (groups[i].radius < groups[small].radius) small = i;
        std::size_t near = small == 0 ? 1 : 0;
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (i != small && Length(groups[i].center - groups[small].center) < Length(groups[near].center - groups[small].center)) near = i;
        groups[near].parts.insert(groups[near].parts.end(), groups[small].parts.begin(), groups[small].parts.end());
        Measure(groups[near], parts);
        groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(small));
    }
    // Si faltan, se parte el mas grande que aun tenga dos piezas.
    while (static_cast<int>(groups.size()) < maxBodies) {
        int big = -1;
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (groups[i].parts.size() >= 2 && (big < 0 || groups[i].radius > groups[static_cast<std::size_t>(big)].radius)) big = static_cast<int>(i);
        if (big < 0) break;
        Group other;
        if (!Split(groups[static_cast<std::size_t>(big)], other, parts)) break;
        groups.push_back(std::move(other));
    }

    const double energy = std::max(wreck.energy, 0.05);
    plan.bodies.reserve(groups.size());
    for (std::size_t b = 0; b < groups.size(); ++b) {
        const Group& g = groups[b];
        for (const int i : g.parts) plan.bodyOfPart[static_cast<std::size_t>(i)] = static_cast<int>(b);
        const std::uint32_t salt = static_cast<std::uint32_t>(b) * 16u;
        const auto unit = [&](std::uint32_t k) { return WreckRandom(wreck.seed, salt + k); };
        WreckBody body;
        body.center = g.center;
        body.radius = g.radius;
        body.half = g.half;
        body.zone = g.parts.empty() ? -1 : parts[static_cast<std::size_t>(g.parts.front())].zone;
        body.parts = static_cast<int>(g.parts.size());
        // Hacia fuera desde el origen de la explosion, algo desviado. Lo de lejos sale mas rapido que lo
        // de cerca (asi los trozos no se cruzan) y lo grande, mas despacio que lo pequeno.
        const Vec3d away = g.center - wreck.origin;
        const double distance = Length(away);
        const Vec3d jitter{unit(1) - 0.5, unit(2) - 0.5, unit(3) - 0.5};
        const Vec3d direction = Normalize(away + jitter * (0.5 * distance + 20.0) + Vec3d{0, 0, 1e-6});
        const double reach = std::min(distance / std::max(rules.blastReach, 1.0), 1.5);
        const double heavy = 1.0 + g.radius / std::max(rules.heavyRadius, 1.0);
        body.velocity = direction * (rules.blastSpeed * energy * (0.35 + 0.65 * reach) * (0.7 + 0.6 * unit(4)) / heavy);
        body.spinAxis = Normalize(Vec3d{unit(5) - 0.5, unit(6) - 0.5, unit(7) - 0.5} + Vec3d{0, 0, 1e-6});
        body.spin = rules.tumble * std::sqrt(energy) * (0.25 + unit(8)) / heavy;
        body.release = distance / std::max(rules.blastFront, 1.0) / std::sqrt(energy) * (0.8 + 0.4 * unit(9));
        plan.bodies.push_back(body);
    }
    return plan;
}

WreckPose PoseOf(const Wreck& wreck, const WreckBody& body, double age) {
    const double rate = Length(wreck.angular);
    const Vec3d axis = rate > 1e-12 ? wreck.angular * (1.0 / rate) : Vec3d{0, 1, 0};
    const double held = std::clamp(age, 0.0, body.release);
    const double free = std::max(age - body.release, 0.0);
    // Hasta soltarse va con la nave, que sigue girando.
    const Quatd atRelease = (Quatd::FromAxisAngle(axis, rate * held) * wreck.orientation).Normalized();
    const auto turn = atRelease.ToMatrix();
    const Vec3d arm = turn.TransformDirection(body.center);
    WreckPose pose;
    pose.free = free;
    if (free <= 0.0) {
        pose.offset = arm;
        pose.rotation = atRelease;
        return pose;
    }
    // Suelto: la velocidad de ese punto de la nave (giro heredado) mas el impulso, en linea recta.
    const Vec3d inherited = Cross(wreck.angular, arm);
    pose.offset = arm + (inherited + turn.TransformDirection(body.velocity)) * free;
    const Quatd carried = Quatd::FromAxisAngle(axis, rate * free);
    const Quatd tumble = Quatd::FromAxisAngle(turn.TransformDirection(body.spinAxis), body.spin * free);
    pose.rotation = (tumble * carried * atRelease).Normalized();
    return pose;
}

}  // namespace game
