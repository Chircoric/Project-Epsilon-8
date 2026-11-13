// Stations.h - Estaciones automaticas que despliega la nave.
//
//   (La estacion de mineria ya no existe: la mineria la hacen los drones, GameSession::DeployDrones.
//   Las partidas viejas que la tenian la pierden al cargarse.)
//   Central: se coloca en la orbita de una estrella o un agujero negro. Con su energia mueve
//            aceleradores de particulas que fabrican antimateria, y acumula esta y la materia
//            que suelta la estrella.
//   Origami: la estacion de nivel 1. Viaja plegada alrededor de las bodegas de la nave y se despliega
//            una sola vez, en cualquier punto de un sistema: un anillo de 1 km con esclusas y brazos
//            que construiran naves con el material que se le entregue. No es modular todavia.
//
// La central guarda lo producido hasta que la nave lo recoge. Produce por tiempo, asi que sigue
// trabajando aunque su sistema este descargado o el jugador este lejos. Tiene huecos para modulos, y
// un modulo tiene nivel: asi se mejora sin cambiar de estacion.
#pragma once

#include <string>
#include <vector>

#include "epsilon/Math3D.h"
#include "game/Balance.h"
#include "game/Economy.h"

namespace game {

// Los numeros son los de las partidas guardadas: 0 era la mina, y los modulos 0 y 1 el taladro y la tolva.
enum class StationKind : int { Power = 1, Shipyard = 2 };
constexpr int kRetiredMiningKind = 0;

enum class ModuleKind : int {
    Collector = 2,  // central: capta mas energia, y por tanto hace mas antimateria
    Capacitor,      // central: guarda mas
    Scoop,          // central: recoge mas materia
    Count
};

const char* Name(ModuleKind kind);
bool Fits(ModuleKind module, StationKind station);

struct Module {
    ModuleKind kind = ModuleKind::Collector;
    int level = 1;
};

// Donde esta. Se guarda con indices estables del generador: sobreviven a descargar el sistema.
struct StationSite {
    int system = 0;
    int body = -1;    // planeta: indice en StarSystemData::bodies
    int belt = -1;    // campo: indice en StarSystemData::belts
    int family = 0;   // cumulo del campo
    int star = -1;    // estrella: indice para StarAt
    // Origami: donde quedo, en km, respecto al planeta (body) o, sin el, al centro del sistema. No orbita.
    eps::Vec3d offset;
};

struct Station {
    int id = 0;
    StationKind kind = StationKind::Power;
    StationSite site;
    std::string name;
    std::vector<Module> modules;
    StarYield yield;        // energia
    Inventory stored;       // recursos en espera
    double storedAntimatter = 0;  // kt
    double angle = 0;       // posicion en su orbita
    double deployAge = 1.0e9;  // segundos reales desde que salio de la nave; no se guarda
    eps::Quatd attitude;    // origami: la de la nave al soltarla; su eje es el de la nave (+Z del modelo girado)
    eps::Vec3d launch;      // origami: km, como site.offset: donde se abrio, antes de apartarse; no se guarda
    double producedUntil = -1.0;  // reloj (s) hasta el que ha producido; < 0: aun sin sello. No se guarda
};

class StationNetwork {
public:
    explicit StationNetwork(const StationRules& rules) : rules_(rules) {}

    int Count(StationKind kind) const;
    bool NextIsFree(StationKind kind) const;
    Inventory DeployCost(StationKind kind) const;  // vacio si es gratis

    // Comprueba que hay sitio y cobra. Devuelve el id o -1, con el motivo en error.
    int Deploy(StationKind kind, const StationSite& site, const std::string& name, const StarYield& yield, Inventory& cargo, std::string* error = nullptr);

    // Avanza la produccion de todas.
    void Produce(double hours);
    // Lo mismo, por sello de tiempo: cada estacion produce lo que le falte hasta el instante time (s) y
    // lo anota, asi que da igual cada cuanto se la mire o con que reloj (docs/multijugador.md, 2.3). Una
    // que aun no tiene sello empieza a contar en from. El tiempo de una estacion nunca retrocede.
    void ProduceUntil(double time, double from);
    void Age(double realSeconds);

    // Vacia una estacion en la nave, hasta donde quepa.
    void Collect(int id, Inventory& cargo, double cargoCapacity, double& antimatter, double antimatterTank);

    Inventory ModuleCost(const Station& station, ModuleKind kind) const;
    bool AddOrUpgradeModule(int id, ModuleKind kind, Inventory& cargo, std::string* error = nullptr);

    double ModuleBonus(const Station& s, ModuleKind kind, double perLevel) const;
    double EnergyPerHour(const Station& s) const;
    double AntimatterFrom(double energyPerHour) const;  // kt por hora que dan los aceleradores
    double AntimatterPerHour(const Station& s) const;
    double MatterPerHour(const Station& s) const;
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
    void ProduceOne(Station& s, double hours);
    StationRules rules_;
    std::vector<Station> stations_;
    int nextId_ = 1;
};

}  // namespace game
