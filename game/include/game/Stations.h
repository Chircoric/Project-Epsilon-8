// Stations.h - Estaciones automaticas que despliega la nave.
//
//   Mineria: se coloca en un planeta o un campo de asteroides y acumula lo que extrae.
//   Central: se coloca en la orbita de una estrella o un agujero negro. Con su energia mueve
//            aceleradores de particulas que fabrican antimateria, y acumula esta y la materia
//            que suelta la estrella.
//
// Las dos guardan lo producido hasta que la nave lo recoge. Producen por tiempo, asi que siguen
// trabajando aunque su sistema este descargado o el jugador este lejos. Cada una tiene huecos
// para modulos, y un modulo tiene nivel: asi se mejoran sin cambiar de estacion.
#pragma once

#include <string>
#include <vector>

#include "game/Balance.h"
#include "game/Economy.h"

namespace game {

enum class StationKind : int { Mining, Power };

enum class ModuleKind : int {
    Drill,      // mineria: extrae mas
    Hopper,     // mineria: guarda mas
    Collector,  // central: capta mas energia, y por tanto hace mas antimateria
    Capacitor,  // central: guarda mas
    Scoop,      // central: recoge mas materia
    Count
};

const char* Name(ModuleKind kind);
bool Fits(ModuleKind module, StationKind station);

struct Module {
    ModuleKind kind = ModuleKind::Drill;
    int level = 1;
};

// Donde esta. Se guarda con indices estables del generador: sobreviven a descargar el sistema.
struct StationSite {
    int system = 0;
    int body = -1;    // planeta: indice en StarSystemData::bodies
    int belt = -1;    // campo: indice en StarSystemData::belts
    int family = 0;   // cumulo del campo
    int star = -1;    // estrella: indice para StarAt
};

struct Station {
    int id = 0;
    StationKind kind = StationKind::Mining;
    StationSite site;
    std::string name;
    std::vector<Module> modules;
    Deposit deposit;        // mineria
    StarYield yield;        // energia
    Inventory stored;       // recursos en espera
    double storedAntimatter = 0;  // kt
    double angle = 0;       // posicion en su orbita
    double deployAge = 1.0e9;  // segundos reales desde que salio de la nave; no se guarda
};

class StationNetwork {
public:
    explicit StationNetwork(const StationRules& rules) : rules_(rules) {}

    int Count(StationKind kind) const;
    bool NextIsFree(StationKind kind) const;
    Inventory DeployCost(StationKind kind) const;  // vacio si es gratis

    // Comprueba que hay sitio y cobra. Devuelve el id o -1, con el motivo en error.
    int Deploy(StationKind kind, const StationSite& site, const std::string& name, const Deposit& deposit, const StarYield& yield, Inventory& cargo, std::string* error = nullptr);

    // Avanza la produccion de todas.
    void Produce(double hours);
    void Age(double realSeconds);

    // Vacia una estacion en la nave, hasta donde quepa.
    void Collect(int id, Inventory& cargo, double cargoCapacity, double& antimatter, double antimatterTank);

    Inventory ModuleCost(const Station& station, ModuleKind kind) const;
    bool AddOrUpgradeModule(int id, ModuleKind kind, Inventory& cargo, std::string* error = nullptr);

    double ModuleBonus(const Station& s, ModuleKind kind, double perLevel) const;
    double MiningPerHour(const Station& s) const;
    double EnergyPerHour(const Station& s) const;
    double AntimatterFrom(double energyPerHour) const;  // kt por hora que dan los aceleradores
    double AntimatterPerHour(const Station& s) const;
    double MatterPerHour(const Station& s) const;
    double OreCapacity(const Station& s) const;
    double AntimatterCapacity(const Station& s) const;
    double MatterCapacity(const Station& s) const;

    const std::vector<Station>& All() const { return stations_; }
    int NextId() const { return nextId_; }
    void Restore(std::vector<Station> stations, int nextId) {
        stations_ = std::move(stations);
        nextId_ = nextId;
    }
    Station* Find(int id);
    const Station* Find(int id) const;

private:
    StationRules rules_;
    std::vector<Station> stations_;
    int nextId_ = 1;
};

}  // namespace game
