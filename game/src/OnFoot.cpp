#include "game/OnFoot.h"

#include <algorithm>
#include <cmath>

#include "epsilon/Astro.h"

namespace game {

using eps::astro::Uniform;
using eps::Color;
using eps::Vec3d;

namespace {

constexpr double kHalfX = 30.0, kHalfZ = 20.0, kHigh = 9.0, kWall = 0.5;  // m: un hangar de 60 por 40

const Color kFloor{0.30f, 0.32f, 0.36f, 1.0f}, kPlate{0.46f, 0.48f, 0.52f, 1.0f}, kRail{0.62f, 0.50f, 0.26f, 1.0f};
const Color kLamp{1.0f, 0.92f, 0.72f, 1.0f}, kDoor{0.35f, 0.85f, 1.0f, 1.0f};

void Add(DeckPlan& d, const Vec3d& center, const Vec3d& size, const Color& color, float emissive = 0.0f, bool solid = true) {
    d.boxes.push_back({center, size, color, emissive, solid});
    if (solid) d.area.solids.push_back({center - size * 0.5, center + size * 0.5});
}

void AddCrate(DeckPlan& d, double x, double z, double side, const Color& color) { Add(d, {x, side * 0.5, z}, {side, side, side}, color, 0.05f); }

}  // namespace

DeckPlan MakeDeck(StationKind kind) {
    DeckPlan d;
    (void)kind;  // por ahora solo hay la cubierta de la central
    const Color wall{0.30f, 0.36f, 0.44f, 1.0f};

    // La caja del hangar. Es opaca y cerrada: dentro no se ve la estacion ni el espacio.
    Add(d, {0, -kWall * 0.5, 0}, {kHalfX * 2.0 + kWall * 2.0, kWall, kHalfZ * 2.0 + kWall * 2.0}, kFloor, 0.08f);
    Add(d, {0, kHigh + kWall * 0.5, 0}, {kHalfX * 2.0 + kWall * 2.0, kWall, kHalfZ * 2.0 + kWall * 2.0}, wall, 0.08f);
    Add(d, {-kHalfX - kWall * 0.5, kHigh * 0.5, 0}, {kWall, kHigh, kHalfZ * 2.0}, wall, 0.1f);
    Add(d, {kHalfX + kWall * 0.5, kHigh * 0.5, 0}, {kWall, kHigh, kHalfZ * 2.0}, wall, 0.1f);
    Add(d, {0, kHigh * 0.5, -kHalfZ - kWall * 0.5}, {kHalfX * 2.0, kHigh, kWall}, wall, 0.1f);
    Add(d, {0, kHigh * 0.5, kHalfZ + kWall * 0.5}, {kHalfX * 2.0, kHigh, kWall}, wall, 0.1f);

    // Luces del techo y marcas del suelo.
    for (int i = -2; i <= 2; ++i) Add(d, {i * 11.0, kHigh - 0.1, 0}, {7.0, 0.1, 1.0}, kLamp, 1.1f, false);
    for (int i = -2; i <= 2; ++i) Add(d, {i * 11.0, 0.01, 8.0}, {6.0, 0.02, 0.2}, kRail, 0.4f, false);
    Add(d, {-10.0, 0.01, 0}, {0.3, 0.02, 12.0}, kRail, 0.4f, false);

    // Columnas: dan algo que rodear y sentido de la escala.
    for (const double x : {-16.0, 0.0, 16.0}) {
        Add(d, {x, kHigh * 0.5, -11.0}, {1.2, kHigh, 1.2}, kPlate, 0.1f);
        Add(d, {x, kHigh * 0.5, 7.0}, {1.2, kHigh, 1.2}, kPlate, 0.1f);
    }

    // Una galeria a tres metros, con su escalera de peldanos bajos.
    constexpr double kGallery = 3.0, kStep = 0.3, kRun = 0.45;
    const int steps = static_cast<int>(kGallery / kStep);
    Add(d, {6.0, kGallery - 0.15, 16.0}, {48.0, 0.3, 8.0}, kPlate, 0.1f);
    for (int i = 0; i < steps - 1; ++i) {
        const double top = kStep * (i + 1);
        Add(d, {-18.0 - kRun * (steps - 1 - i), top * 0.5, 15.0}, {kRun, top, 3.0}, kPlate, 0.1f);
    }
    Add(d, {6.0, kGallery + 0.55, 12.0}, {48.0, 1.1, 0.1}, kRail, 0.2f, true);

    // La esclusa de la nave.
    Add(d, {-kHalfX + 0.1, 1.5, 0}, {0.2, 3.0, 3.0}, kDoor, 2.0f, false);
    Add(d, {-kHalfX + 0.15, 3.3, 0}, {0.3, 0.3, 3.8}, kRail, 0.5f, false);
    d.spawn = {-kHalfX + 3.0, 0.01, 0};
    d.airlock = {-kHalfX + 0.5, 0, 0};

    // El nucleo de la central: una columna de luz antimateria.
    Add(d, {14.0, kHigh * 0.5, -3.0}, {4.0, kHigh, 4.0}, Color{0.55f, 0.35f, 1.0f, 1.0f}, 2.2f);
    Add(d, {14.0, 0.6, -3.0}, {6.0, 1.2, 6.0}, kPlate, 0.1f);
    AddCrate(d, -6.0, -9.0, 1.6, kPlate);
    AddCrate(d, -3.8, -9.0, 1.6, kPlate);
    return d;
}

std::vector<Person> MakeCrew(const DeckPlan& deck, const eps::HumanoidRules& rules, int count, std::mt19937& rng) {
    static const char* const names[] = {"Técnica Ibarra", "Operario Sol", "Ingeniera Vega", "Capataz Ruiz", "Mecánico Paz"};
    std::vector<Person> crew;
    for (int i = 0; i < count; ++i) {
        Person p;
        p.name = names[i % 5];
        for (int attempt = 0; attempt < 40; ++attempt) {
            const Vec3d at{Uniform(rng, -kHalfX + 4.0, kHalfX - 2.0), 0.02, Uniform(rng, -kHalfZ + 2.0, 10.0)};
            p.body.position = at;
            if (eps::HumanoidFits(at, deck.area, rules)) break;
        }
        p.goal = p.body.position;
        p.body.yaw = Uniform(rng, 0.0, 6.28);
        p.wait = Uniform(rng, 0.0, 4.0);
        crew.push_back(p);
    }
    return crew;
}

void StepPerson(Person& p, const DeckPlan& deck, const eps::HumanoidRules& rules, double gravity, double dt, std::mt19937& rng) {
    eps::HumanoidInput input;
    if (p.wait > 0.0) {
        p.wait -= dt;
    } else {
        Vec3d to = p.goal - p.body.position;
        to.y = 0.0;
        if (eps::Length(to) < 0.6 || p.walked > 25.0) {
            // Busca otro sitio libre, y si lleva mucho sin llegar se rinde: algo le estorba.
            for (int attempt = 0; attempt < 12; ++attempt) {
                const Vec3d at{Uniform(rng, -kHalfX + 4.0, kHalfX - 2.0), 0.02, Uniform(rng, -kHalfZ + 2.0, 10.0)};
                if (!eps::HumanoidFits(at, deck.area, rules)) continue;
                p.goal = at;
                break;
            }
            p.wait = Uniform(rng, 1.0, 6.0);
            p.walked = 0.0;
        } else {
            p.walked += dt;
            p.body.yaw = std::atan2(to.x, to.z);
            input.forward = 1.0;
        }
    }
    eps::StepHumanoid(p.body, input, deck.area, rules, gravity, dt);
}

}  // namespace game
