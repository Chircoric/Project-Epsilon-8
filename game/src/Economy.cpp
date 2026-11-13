#include "game/Economy.h"

#include <algorithm>
#include <cmath>

namespace game {

using namespace eps::astro;

namespace {

void Normalize(Deposit& d) {
    double total = 0;
    for (const double s : d.share) total += s;
    if (total <= 0.0) return;
    for (double& s : d.share) s /= total;
}

void Set(Deposit& d, Resource r, double value) { d.share[static_cast<std::size_t>(r)] = value; }

}  // namespace

const char* Name(Resource r) {
    static const char* const names[] = {"Metal", "Silicatos", "Hielo", "Gas", "Materia estelar"};
    return names[static_cast<std::size_t>(r)];
}

double Inventory::Total() const {
    double total = 0;
    for (const double a : amounts_) total += a;
    return total;
}

bool Inventory::CanAfford(const Inventory& cost) const {
    for (std::size_t i = 0; i < amounts_.size(); ++i)
        if (amounts_[i] < cost.amounts_[i]) return false;
    return true;
}

bool Inventory::Spend(const Inventory& cost) {
    if (!CanAfford(cost)) return false;
    for (std::size_t i = 0; i < amounts_.size(); ++i) amounts_[i] -= cost.amounts_[i];
    return true;
}

bool DepositOf(const BodyData& body, Deposit& out) {
    out = {};
    const Materials& m = body.materials;
    Set(out, Resource::Metal, m.iron);
    Set(out, Resource::Silicate, m.silicate);
    Set(out, Resource::Ice, m.water + m.ices);
    Set(out, Resource::Gas, m.gas);

    double total = 0;
    for (const double s : out.share) total += s;
    if (total <= 0.0) return false;
    Normalize(out);
    // Un gigante se explota por encima de las nubes: mucho gas, pero sin suelo donde apoyarse.
    const bool gaseous = body.kind == PlanetClass::GasGiant || body.kind == PlanetClass::HotJupiter || body.kind == PlanetClass::IceGiant;
    out.richness = gaseous ? 0.6 : 1.0;
    return true;
}

Deposit DepositOf(BeltProfile profile) {
    Deposit d;
    Set(d, Resource::Metal, 0.45);
    Set(d, Resource::Silicate, 0.45);
    Set(d, Resource::Ice, 0.10);
    switch (profile) {
        case BeltProfile::Sparse: d.richness = 0.6; break;
        case BeltProfile::Dense: d.richness = 1.4; break;
        case BeltProfile::Rubble: d.richness = 1.1; break;
        case BeltProfile::Giants: d.richness = 1.2; break;
        default: d.richness = 1.0; break;
    }
    Normalize(d);
    return d;
}

StarYield YieldOf(const StarData& star) {
    StarYield y;
    const double light = std::max(star.luminosity, 1e-6);
    y.energyPerHour = kCollectorScale * std::clamp(90.0 * std::pow(light, 0.3), 15.0, 900.0);
    y.matterPerHour = 1.5 * std::pow(light, 0.2);
    y.orbitRadius = std::max(star.radius * 8.0, 2.0e6);

    switch (star.kind) {
        case StarClass::M: y.requiredTier = 0; break;
        case StarClass::K: y.requiredTier = 0; break;
        case StarClass::G: y.requiredTier = 1; break;
        case StarClass::F: y.requiredTier = 1; break;
        case StarClass::A: y.requiredTier = 2; break;
        case StarClass::B: y.requiredTier = 3; break;
        case StarClass::O: y.requiredTier = 4; break;
        case StarClass::WhiteDwarf: y.requiredTier = 2; break;
        case StarClass::RedGiant:
            y.requiredTier = 2;
            y.matterPerHour *= 4.0;  // el viento de una gigante arrastra mucha mas masa
            break;
        case StarClass::RedSupergiant:
            y.requiredTier = 3;
            y.matterPerHour *= 8.0;
            break;
        case StarClass::BlueSupergiant:
            y.requiredTier = 4;
            y.matterPerHour *= 6.0;
            break;
        case StarClass::NeutronStar:
            y.requiredTier = 4;
            y.energyPerHour = kCollectorScale * 500.0;
            y.orbitRadius = 2.0e5;
            break;
        case StarClass::BlackHole:
            // No da luz: la energia sale de lo que cae en el disco, y mas cuanto mas gira.
            y.requiredTier = star.blackHoleSpin > 0.0 ? 5 : 4;
            y.energyPerHour = kCollectorScale * 400.0 * (1.0 + 2.0 * star.blackHoleSpin) * std::pow(star.mass / (10.0 * kSolarMass), 0.15);
            y.matterPerHour = 20.0;
            y.orbitRadius = std::max(star.radius * 30.0, 2.0e5);
            break;
        default: break;
    }
    return y;
}

// Un campo de asteroides: su composicion sale de lo que daria un dron.
Composition BeltElements(BeltProfile profile) {
    const Deposit deposit = DepositOf(profile);
    const auto share = [&](Resource r) { return deposit.share[static_cast<std::size_t>(r)]; };
    Materials m;
    m.iron = share(Resource::Metal);
    m.silicate = share(Resource::Silicate);
    m.water = share(Resource::Ice) * 0.7;
    m.ices = share(Resource::Ice) * 0.3 + share(Resource::Gas);
    m.carbon = 0.3;
    return ElementsOf(m);
}

}  // namespace game
