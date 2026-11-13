// Economy.h - Recursos, yacimientos y lo que aporta cada estrella.
//
// Lo que produce un sitio sale de los datos que el motor ya genera: un planeta rico en hierro da
// metal, y una estrella da energia segun su luminosidad. Asi la economia no necesita tablas
// por planeta y cada sistema es distinto.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "epsilon/Asteroids.h"
#include "epsilon/Astro.h"
#include "game/Balance.h"

namespace game {

enum class Resource : int {
    Metal,
    Silicate,
    Ice,
    Gas,
    StellarMatter,  // lo que sueltan las estrellas; solo lo recogen las estaciones de energia
    Count
};

const char* Name(Resource resource);

class Inventory {
public:
    double Get(Resource r) const { return amounts_[static_cast<std::size_t>(r)]; }
    void Add(Resource r, double amount) { amounts_[static_cast<std::size_t>(r)] += amount; }
    double Total() const;

    // Paga la cantidad si la tiene. Si no, no toca nada y devuelve false.
    bool Spend(const Inventory& cost);
    bool CanAfford(const Inventory& cost) const;

private:
    std::array<double, static_cast<std::size_t>(Resource::Count)> amounts_{};
};

// Los elementos de la tabla periodica que se tienen, aparte de la bodega: en kg, por numero atomico
// (1 a 118). No cuentan para la capacidad de la bodega.
class ElementStock {
public:
    static constexpr int kCount = eps::astro::kPeriodicCount;
    static bool Valid(int z) { return z >= 1 && z <= kCount; }
    double Get(int z) const { return Valid(z) ? amounts_[static_cast<std::size_t>(z - 1)] : 0.0; }
    void Add(int z, double kilograms) {
        if (!Valid(z)) return;
        double& a = amounts_[static_cast<std::size_t>(z - 1)];
        a = std::max(a + kilograms, 0.0);
    }
    double Total() const {
        double sum = 0;
        for (const double a : amounts_) sum += a;
        return sum;
    }
    bool Empty() const { return Total() <= 0.0; }

private:
    std::array<double, static_cast<std::size_t>(kCount)> amounts_{};
};

// La composicion de un campo de asteroides, deducida de lo que daria un dron.
eps::astro::Composition BeltElements(eps::astro::BeltProfile profile);

// kt de un elemento por hora y por dron: ritmo x riqueza x fraccion x recuperacion.
inline double ElementYieldPerHour(const DroneRules& r, double richness, double fraction) { return r.rate * richness * fraction * r.recovery; }

// Reparto de lo que se extrae de un sitio, en fracciones que suman 1.
struct Deposit {
    std::array<double, static_cast<std::size_t>(Resource::Count)> share{};
    double richness = 1;  // multiplica el ritmo
};

// false si no se puede minar: una estrella, una luna sin materiales...
bool DepositOf(const eps::astro::BodyData& body, Deposit& out);
Deposit DepositOf(eps::astro::BeltProfile profile);

// Lo que capta el enjambre de espejos de una central respecto a un colector de 170 km de lado.
inline constexpr double kCollectorScale = 3.0e4;

// Lo que da una estrella a una central.
struct StarYield {
    double energyPerHour = 0;
    double matterPerHour = 0;
    int requiredTier = 0;   // blindaje termico minimo para entrar en su orbita
    double orbitRadius = 0;  // km, donde se coloca una estacion
};

StarYield YieldOf(const eps::astro::StarData& star);

}  // namespace game
