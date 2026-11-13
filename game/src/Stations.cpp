#include "game/Stations.h"

#include <algorithm>
#include <cmath>

namespace game {

namespace {

constexpr double kOrePerHull = 1.0;

bool SameSite(const StationSite& a, const StationSite& b) {
    return a.system == b.system && a.body == b.body && a.belt == b.belt && a.family == b.family && a.star == b.star;
}

}  // namespace

const char* Name(ModuleKind kind) {
    static const char* const names[] = {"Colector", "Condensador", "Cuchara"};
    return names[static_cast<int>(kind) - static_cast<int>(ModuleKind::Collector)];
}

bool Fits(ModuleKind module, StationKind station) {
    return station == StationKind::Power && module >= ModuleKind::Collector && module < ModuleKind::Count;  // la origami no es modular todavia
}

int StationNetwork::Count(StationKind kind) const {
    int count = 0;
    for (const Station& s : stations_) count += s.kind == kind ? 1 : 0;
    return count;
}

bool StationNetwork::NextIsFree(StationKind kind) const {
    if (kind == StationKind::Shipyard) return true;  // va a bordo desde el principio
    return Count(kind) < rules_.freePower;
}

Inventory StationNetwork::DeployCost(StationKind kind) const {
    Inventory cost;
    if (NextIsFree(kind)) return cost;
    cost.Add(Resource::Metal, rules_.powerMetal);
    cost.Add(Resource::Silicate, rules_.powerSilicate);
    return cost;
}

int StationNetwork::Deploy(StationKind kind, const StationSite& site, const std::string& name, const StarYield& yield, Inventory& cargo, std::string* error) {
    for (const Station& s : stations_) {
        // La origami se queda donde se suelta, sin ocupar el sitio de una central.
        if (kind != StationKind::Shipyard && s.kind != StationKind::Shipyard && SameSite(s.site, site)) {
            if (error) *error = "Ya hay una estacion aqui";
            return -1;
        }
    }
    const Inventory cost = DeployCost(kind);
    if (!cargo.Spend(cost)) {
        if (error) *error = "Faltan recursos para desplegarla";
        return -1;
    }
    Station s;
    s.id = nextId_++;
    s.kind = kind;
    s.site = site;
    s.name = name;
    s.yield = yield;
    s.angle = static_cast<double>(s.id) * 2.399963;  // angulo aureo: no se amontonan
    s.deployAge = 0.0;
    stations_.push_back(std::move(s));
    return stations_.back().id;
}

void StationNetwork::Age(double realSeconds) {
    for (Station& s : stations_) s.deployAge += realSeconds;
}

double StationNetwork::ModuleBonus(const Station& s, ModuleKind kind, double perLevel) const {
    double bonus = 1.0;
    for (const Module& m : s.modules)
        if (m.kind == kind) bonus += perLevel * m.level;
    return bonus;
}

double StationNetwork::EnergyPerHour(const Station& s) const { return s.yield.energyPerHour * ModuleBonus(s, ModuleKind::Collector, rules_.collectorBonus); }
// La antimateria vale su masa por c^2, la mitad de lo que da al aniquilarse con materia corriente.
double StationNetwork::AntimatterFrom(double energyPerHour) const { return energyPerHour * rules_.acceleratorEfficiency / (kAnnihilationEnergy * 0.5); }
double StationNetwork::AntimatterPerHour(const Station& s) const { return AntimatterFrom(EnergyPerHour(s)); }
double StationNetwork::MatterPerHour(const Station& s) const { return s.yield.matterPerHour * ModuleBonus(s, ModuleKind::Scoop, rules_.scoopBonus); }
double StationNetwork::AntimatterCapacity(const Station& s) const { return rules_.antimatterStorage * ModuleBonus(s, ModuleKind::Capacitor, rules_.capacitorBonus); }
double StationNetwork::MatterCapacity(const Station& s) const { return rules_.matterStorage * ModuleBonus(s, ModuleKind::Capacitor, rules_.capacitorBonus); }

void StationNetwork::ProduceOne(Station& s, double hours) {
    if (s.kind == StationKind::Shipyard) return;  // no produce: guarda el material que se le entrega
    s.angle += hours * rules_.orbitRate;
    s.storedAntimatter = std::min(s.storedAntimatter + AntimatterPerHour(s) * hours, AntimatterCapacity(s));
    const double room = std::max(MatterCapacity(s) - s.stored.Get(Resource::StellarMatter), 0.0);
    s.stored.Add(Resource::StellarMatter, std::min(MatterPerHour(s) * hours, room));
}

void StationNetwork::Produce(double hours) {
    for (Station& s : stations_) ProduceOne(s, hours);
}

void StationNetwork::ProduceUntil(double time, double from) {
    ProduceUntil(time, from, [](const Station&) { return true; });
}

void StationNetwork::ProduceUntil(double time, double from, const std::function<bool(const Station&)>& accept) {
    for (Station& s : stations_) {
        if (!accept(s)) continue;
        if (s.producedUntil < 0.0) s.producedUntil = std::min(from, time);
        if (time <= s.producedUntil) continue;
        ProduceOne(s, (time - s.producedUntil) / 3600.0);
        s.producedUntil = time;
    }
}

void StationNetwork::Collect(int id, Inventory& cargo, double cargoCapacity, double& antimatter, double antimatterTank) {
    Station* s = Find(id);
    if (!s) return;

    const double take = std::min(antimatterTank - antimatter, s->storedAntimatter);
    if (take > 0.0) {
        antimatter += take;
        s->storedAntimatter -= take;
    }
    for (int r = 0; r < static_cast<int>(Resource::Count); ++r) {
        const Resource resource = static_cast<Resource>(r);
        const double amount = std::min(s->stored.Get(resource), std::max(cargoCapacity - cargo.Total(), 0.0) / kOrePerHull);
        if (amount <= 0.0) continue;
        cargo.Add(resource, amount);
        s->stored.Add(resource, -amount);
    }
}

Inventory StationNetwork::ModuleCost(const Station& station, ModuleKind kind) const {
    int level = 0;
    for (const Module& m : station.modules)
        if (m.kind == kind) level = m.level;
    Inventory cost;
    cost.Add(Resource::Metal, rules_.moduleBaseCost * std::pow(rules_.moduleCostGrowth, level));
    return cost;
}

bool StationNetwork::AddOrUpgradeModule(int id, ModuleKind kind, Inventory& cargo, std::string* error) {
    const auto fail = [&](const char* text) {
        if (error) *error = text;
        return false;
    };
    Station* s = Find(id);
    if (!s) return fail("Esa estacion no existe");
    if (!Fits(kind, s->kind)) return fail("Ese modulo no sirve en esta estacion");

    Module* existing = nullptr;
    for (Module& m : s->modules)
        if (m.kind == kind) existing = &m;
    if (existing && existing->level >= rules_.moduleMaxLevel) return fail("Ese modulo ya esta al maximo");
    if (!existing && static_cast<int>(s->modules.size()) >= rules_.moduleSlots) return fail("No quedan huecos");
    if (!cargo.Spend(ModuleCost(*s, kind))) return fail("Faltan recursos");

    if (existing) ++existing->level;
    else s->modules.push_back({kind, 1});
    return true;
}

Station* StationNetwork::Find(int id) {
    for (Station& s : stations_)
        if (s.id == id) return &s;
    return nullptr;
}

const Station* StationNetwork::Find(int id) const {
    for (const Station& s : stations_)
        if (s.id == id) return &s;
    return nullptr;
}

}  // namespace game
