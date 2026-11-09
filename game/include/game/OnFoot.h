// OnFoot.h - A pie: las cubiertas de las estaciones y la gente que las pisa.
//
// Todo va en metros, con Y hacia arriba, como en el motor de personas (Humanoid.h). Una cubierta
// sale de una lista de cajas: las mismas sirven para chocar y para dibujarse, asi lo que se ve es
// lo que se pisa. Este modo es la base: mas adelante tendra sus propias mecanicas.
#pragma once

#include <random>
#include <string>
#include <vector>

#include "epsilon/Humanoid.h"
#include "epsilon/RenderTypes.h"
#include "game/Stations.h"

namespace game {

struct DeckBox {
    eps::Vec3d center, size;   // m
    eps::Color color;
    float emissive = 0.0f;
    bool solid = true;         // las luces y las marcas no estorban
};

struct DeckPlan {
    std::vector<DeckBox> boxes;
    eps::WalkArea area;
    eps::Vec3d spawn;          // junto a la esclusa
    eps::Vec3d airlock;        // por donde se vuelve a la nave
};

DeckPlan MakeDeck(StationKind kind);

// Alguien que no es el jugador: anda hasta un punto, espera y busca otro.
struct Person {
    eps::HumanoidState body;
    std::string name;
    eps::Vec3d goal;
    double wait = 0;           // segundos que le quedan parado
    double walked = 0;         // segundos andando hacia este punto
};

std::vector<Person> MakeCrew(const DeckPlan& deck, const eps::HumanoidRules& rules, int count, std::mt19937& rng);
void StepPerson(Person& p, const DeckPlan& deck, const eps::HumanoidRules& rules, double gravity, double dt, std::mt19937& rng);

// De metros de cubierta a km del mundo, respecto al origen de la cubierta. En el mundo, arriba es -Y.
inline eps::Vec3d DeckToWorld(const eps::Vec3d& metres) { return eps::Vec3d{metres.x, -metres.y, metres.z} * 0.001; }

}  // namespace game
