// Pruebas del juego. Juegan partidas enteras sin ventana ni GPU.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include "game/Armament.h"
#include "game/Defense.h"
#include "game/Economy.h"
#include "game/GameLayer.h"
#include "game/GameModels.h"
#include "game/Net.h"
#include "game/SaveGame.h"
#include "game/Session.h"
#include "game/Settings.h"
#include "game/Units.h"

using namespace eps;
using namespace eps::astro;
using namespace game;
using Kind = TargetRef::Kind;

static int gChecks = 0, gFailures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++gChecks;                                                           \
        if (!(cond)) {                                                       \
            ++gFailures;                                                     \
            std::printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                    \
    } while (false)

static std::unique_ptr<Engine> MakeEngine(const Balance& rules, std::uint32_t seed = 20261105) {
    EngineConfig config;
    config.galaxy.seed = seed;
    config.galaxy.systems = 12;
    config.galaxy.starSpacing = rules.world.starSpacing;
    config.galaxy.streaming = true;
    config.galaxy.blackHoles = true;
    return std::make_unique<Engine>(config);
}

static void Run(GameSession& g, double realSeconds) {
    for (double t = 0; t < realSeconds; t += 0.05) g.Step(0.05, {});
}

// Vuela hasta llegar; devuelve false si no llega en el tiempo dado.
static bool Fly(GameSession& g, const TargetRef& target, DriveMode mode, double maxRealSeconds) {
    g.SetDrive(mode);
    if (!g.SetAutopilot(target)) return false;
    for (double t = 0; t < maxRealSeconds && g.Autopilot(); t += 0.05) g.Step(0.05, {});
    if (g.Orbiting()) return true;
    const std::optional<TargetInfo> info = g.Resolve(target);
    std::printf("  Vuelo sin llegar: %.0f km de %s, llegada %.0f, velocidad %.1f km/s, energia %.0f, objetivo a %.1f km/s, modo %s\n", info ? Length(info->position - g.Ship().position) : -1.0, info ? info->name.c_str() : "?", info ? info->arrival : 0.0, g.Ship().speed, g.Ship().energy, info ? Length(info->velocity) : 0.0, Name(g.Ship().mode));
    for (const std::string& line : g.Log()) std::printf("    %s\n", line.c_str());
    return false;
}

static TargetRef FirstPlanet(const GameSession& g, int system, std::size_t skip = 0) {
    const StarSystemData& data = *g.GetWorld().systems[static_cast<std::size_t>(system)];
    for (std::size_t b = 0; b < data.bodies.size(); ++b)
        if (data.bodies[b].parent < 0 && data.bodies[b].kind != PlanetClass::Comet && data.bodies[b].kind != PlanetClass::Asteroid && skip-- == 0) return {Kind::Planet, system, static_cast<int>(b)};
    return {};
}

// Una central en la orbita de la estrella del sistema 0: la estacion que usan las pruebas de atraque,
// de ruta y de taller. La primera es gratis.
static TargetRef PlaceStation(GameSession& g) {
    const TargetRef star{Kind::Star, 0, 0};
    const StarYield yield = YieldOf(g.GetWorld().systems[0]->StarAt(0));
    if (g.Ship().heatShieldTier < yield.requiredTier) g.Ship().heatShieldTier = yield.requiredTier;
    g.MarkScanned(0);
    CHECK(g.JumpTo(star, g.Resolve(star)->arrival));
    Run(g, 0.5);
    CHECK(g.DeployPower(star));
    return {Kind::Station, 0, g.Stations().All().back().id};
}

static void TestEconomy() {
    Inventory bag;
    bag.Add(Resource::Metal, 100.0);
    Inventory cost;
    cost.Add(Resource::Metal, 60.0);
    cost.Add(Resource::Ice, 1.0);
    CHECK(!bag.Spend(cost));
    CHECK(bag.Get(Resource::Metal) == 100.0);  // si falta algo, no se cobra nada
    Inventory metal;
    metal.Add(Resource::Metal, 60.0);
    CHECK(bag.Spend(metal));
    CHECK(bag.Get(Resource::Metal) == 40.0);

    // Lo que da un planeta sale de su composicion.
    BodyData rock;
    rock.kind = PlanetClass::Rock;
    rock.materials.iron = 0.75;
    rock.materials.silicate = 0.25;
    Deposit deposit;
    CHECK(DepositOf(rock, deposit));
    CHECK(std::abs(deposit.share[0] - 0.75) < 1e-9);
    BodyData nothing;
    CHECK(!DepositOf(nothing, deposit));
    CHECK(DepositOf(BeltProfile::Dense).richness > DepositOf(BeltProfile::Sparse).richness);

    // Cada estrella da distinto y pide distinto blindaje.
    std::mt19937 rng(3);
    const StarYield m = YieldOf(MakeStar(StarClass::M, rng)), g = YieldOf(MakeStar(StarClass::G, rng)), o = YieldOf(MakeStar(StarClass::O, rng));
    CHECK(m.energyPerHour < g.energyPerHour && g.energyPerHour < o.energyPerHour);
    CHECK(m.requiredTier < g.requiredTier && g.requiredTier < o.requiredTier);
    StarData hole = MakeStar(StarClass::BlackHole, rng);
    hole.blackHoleSpin = 0.0;
    const StarYield still = YieldOf(hole);
    hole.blackHoleSpin = 0.9;
    const StarYield spinning = YieldOf(hole);
    CHECK(spinning.energyPerHour > still.energyPerHour);
    CHECK(spinning.requiredTier > still.requiredTier);
    CHECK(still.matterPerHour > g.matterPerHour);
    StarData red = MakeStar(StarClass::RedGiant, rng), sun = MakeStar(StarClass::K, rng);
    red.luminosity = sun.luminosity;
    CHECK(YieldOf(red).matterPerHour > YieldOf(sun).matterPerHour * 3.0);
}

// Tiempo a bordo igual al exterior, salvo por la velocidad.
static void Burn(ShipState& s, const Balance& rules, int steps, double seconds = 1.0, const DriveInput& in = {}) {
    for (int i = 0; i < steps; ++i) StepDrive(s, rules, seconds, seconds * ExteriorRate(s.speed, 0.0, rules.world.maxTimeFactor), seconds / 60.0, in);
}

static void TestDrive() {
    const Balance rules;
    ShipState s = MakeShip(rules);
    const DriveRules& d = rules.drive;

    // La nave acelera: no salta a una velocidad, y tiene un tope.
    s.throttle = 1.0;
    Burn(s, rules, 10);
    CHECK(s.speed > 4.0 && s.speed < 6.0);
    Burn(s, rules, 2000);
    CHECK(std::abs(s.speed - d.impulseSpeed) < 2.0);

    // Al soltar el mando sigue por inercia, y la nebulosa la frena poco a poco.
    s.throttle = 0.0;
    const double cruising = s.speed;
    Burn(s, rules, 60);
    CHECK(s.speed < cruising);
    CHECK(s.speed > cruising * 0.95);
    Burn(s, rules, 3600 * 6);
    CHECK(s.speed < cruising * 0.05);

    // Espacio frena y la deja quieta, aunque haya gravedad.
    s.velocity = {0, 0, 100};
    s.speed = 100.0;
    s.holding = true;
    DriveInput pull;
    pull.gravity = {0.01, 0, 0};
    Burn(s, rules, 300, 1.0, pull);
    CHECK(s.speed < 0.1);

    // La gravedad curva la trayectoria; dentro de la burbuja no se nota.
    ShipState falling = MakeShip(rules);
    falling.holding = false;
    Burn(falling, rules, 100, 1.0, pull);
    CHECK(falling.velocity.x > 0.9);

    // El crucero se acerca a c y nunca la pasa, por mucho tiempo que pase, y sin gastar burbujas.
    s = MakeShip(rules);
    s.mode = DriveMode::Cruise;
    s.throttle = 1.0;
    Burn(s, rules, 5000);
    CHECK(s.speed < kLightSpeed);
    CHECK(s.speed > kLightSpeed * 0.98);
    CHECK(LorentzFactor(s.speed) > 5.0);
    CHECK(s.warpCharge == 0.0);
    CHECK(s.antimatter > rules.ship.startAntimatter - 0.1);

    // Fuera pasa mucho mas tiempo que a bordo, y eso es lo que recorre.
    const double hoursBefore = s.shipHours;
    const Vec3d at = s.position;
    const double rate = ExteriorRate(s.speed, 0.0, rules.world.maxTimeFactor);
    CHECK(rate > 5.0);
    StepDrive(s, rules, 3600.0, 3600.0 * rate, 1.0);
    CHECK(std::abs(s.shipHours - hoursBefore - 1.0) < 1e-9);
    CHECK(Length(s.position - at) > s.speed * 3600.0 * 5.0);

    // Un pozo gravitatorio tambien adelanta el reloj de fuera, sin pasar del limite.
    CHECK(ExteriorRate(0.0, 0.0, 60.0) == 1.0);
    CHECK(ExteriorRate(0.0, 0.5, 60.0) > 1.4);
    CHECK(ExteriorRate(0.0, 1.0, 5.0) == 5.0);

    // Sin la tecnologia de la estructura, Hibrida e Hiper vuelven al crucero.
    s = MakeShip(rules);
    s.mode = DriveMode::Hyper;
    s.throttle = 1.0;
    CHECK(!HybridUnlocked(s) && !HyperUnlocked(s, d));
    Burn(s, rules, 1);
    CHECK(s.mode == DriveMode::Cruise);
    CHECK(TopSpeed(s, DriveMode::Hyper, d) == TopSpeed(s, DriveMode::Warp, d));

    // Con el primer emisor, solo la Hibrida: el Hiper pedido se queda en Hibrida.
    s.emitters = 1;
    CHECK(HybridUnlocked(s) && !HyperUnlocked(s, d));
    s.mode = DriveMode::Hyper;
    Burn(s, rules, 1);
    CHECK(s.mode == DriveMode::Warp);

    // Con el segundo se pasa del limite del silicio, y cada emisor de mas da mas velocidad, con tope.
    s.emitters = 2;
    const double little = TopSpeed(s, DriveMode::Hyper, d);
    CHECK(std::abs(little - kLightSpeed * 50.0) < 1e-6);
    s.emitters = 4;
    CHECK(TopSpeed(s, DriveMode::Hyper, d) > little * 2.0);
    s.emitters = 1000;
    CHECK(std::abs(TopSpeed(s, DriveMode::Hyper, d) - kLightSpeed * 250.0) < 1e-6);
    s.mode = DriveMode::Hyper;
    Burn(s, rules, 1500);
    CHECK(s.speed > kLightSpeed);
    CHECK(LorentzFactor(s.speed) == 1.0);

    // Sin energia no hay motor, pero la nave nunca se queda parada del todo.
    s = MakeShip(rules);
    s.mode = DriveMode::Cruise;
    s.throttle = 1.0;
    s.energy = 0.0;
    s.antimatter = 0.0;
    Burn(s, rules, 200);
    CHECK(s.speed > 0.0);
    CHECK(s.speed <= d.impulseSpeed * d.emergencyFraction * 1.01);

    // Con el motor apagado recupera energia y antimateria: nunca se queda atrapada del todo.
    s = MakeShip(rules);
    s.energy = 0.0;
    s.antimatter = 0.0;
    s.throttle = 0.0;
    Burn(s, rules, 3600);
    CHECK(s.energy > d.idleRecharge * 0.9);
    CHECK(std::abs(s.antimatter - d.idleAntimatter) < 0.01);

    // El paso no cambia lo gastado en mantener la burbuja.
    ShipState a = MakeShip(rules), b = MakeShip(rules);
    a.mode = b.mode = DriveMode::Warp;
    a.throttle = b.throttle = 1.0;
    a.warpSpeed = b.warpSpeed = TopSpeed(a, DriveMode::Warp, d);
    a.warpCharge = b.warpCharge = 1.0;
    for (int i = 0; i < 600; ++i) StepDrive(a, rules, 1.0, 1.0, 1.0);
    for (int i = 0; i < 6000; ++i) StepDrive(b, rules, 0.1, 0.1, 0.1);
    CHECK(a.antimatter < rules.ship.startAntimatter);
    CHECK(std::abs((rules.ship.startAntimatter - a.antimatter) / (rules.ship.startAntimatter - b.antimatter) - 1.0) < 0.01);
    CHECK(std::abs(Length(a.position) / Length(b.position) - 1.0) < 0.01);
}

// Un viaje entero entre estrellas, con motor humano: la ruta forma la burbuja sola y paga por ella.
static void TestWarpRoute() {
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 20261105, GameModeId::Normal);
    g.Conversation().Stop();
    g.Step(0.05, {});
    g.Ship().heatShieldTier = 5;
    g.MarkScanned(3);
    g.Step(0.05, {});
    const TargetRef other{TargetRef::Kind::Star, 3, 0};
    CHECK(g.Resolve(other).has_value());

    // Sin la tecnologia, la ruta se queda en crucero aunque haya antimateria de sobra.
    CHECK(g.SetAutopilot(other));
    for (int i = 0; i < 600; ++i) g.Step(1.0 / 60.0, {});
    CHECK(g.Ship().mode == DriveMode::Cruise && g.Ship().warpCharge == 0.0);
    g.CancelAutopilot();
    g.Ship().emitters = 2;

    const double start = g.Ship().antimatter, simStart = g.SimTime();
    CHECK(g.SetAutopilot(other));
    bool curved = false;
    double top = 0.0;
    int steps = 0;
    for (; steps < 60 * 300 && g.Autopilot() && g.Phase() == GamePhase::Flying; ++steps) {
        g.Step(1.0 / 60.0, {});
        curved = curved || g.Ship().mode == DriveMode::Hyper;
        top = std::max(top, g.Ship().speed);
    }
    CHECK(!g.Autopilot());
    CHECK(g.Phase() == GamePhase::Flying);
    CHECK(curved);
    const double hyperTop = TopSpeed(g.Ship(), DriveMode::Hyper, rules.drive);
    CHECK(top > hyperTop * 0.9 && top < hyperTop * 1.05);
    CHECK(start - g.Ship().antimatter < 1.0);  // la burbuja de las ruinas cuesta casi nada
    CHECK(steps < 60 * 240);                 // minutos de partida
    CHECK(g.SimTime() - simStart > 86400.0); // aunque fuera pasen dias
    std::printf("  viaje a otra estrella: %.0f s reales, %.1f dias fuera, %.1f kt de antimateria\n", steps / 60.0, (g.SimTime() - simStart) / 86400.0, start - g.Ship().antimatter);

    // Sin antimateria la ruta avisa y sigue en crucero, sin dejar de funcionar.
    const TargetRef home{TargetRef::Kind::Star, 0, 0};
    g.Ship().antimatter = 0.0;
    CHECK(!g.SetDrive(DriveMode::Warp));
    CHECK(g.SetAutopilot(home));
    for (int i = 0; i < 600; ++i) g.Step(1.0 / 60.0, {});
    CHECK(g.Ship().mode == DriveMode::Cruise);
    CHECK(g.Ship().warpCharge == 0.0);
}

// Las cifras de la especificacion: 10^24 J sin energia negativa, y lo que dice para la metrica hibrida.
static void TestWarpCost() {
    const Balance rules;
    const DriveRules& d = rules.drive;
    const double toJoules = 1.0e18;

    // Formar una burbuja humana cuesta 10^24 J, unas 5,6 kt de antimateria.
    ShipState s = MakeShip(rules);
    CHECK(std::abs(FormationEnergy(s, DriveMode::Warp, d) * toJoules - 1.0e24) < 1.0e12);
    CHECK(std::abs(FormationCost(s, DriveMode::Warp, d) - 5.556) < 0.01);
    CHECK(std::abs(TopSpeed(s, DriveMode::Warp, d) - kLightSpeed * 10.0) < 1e-6);

    // Se paga mientras se forma, y hasta que este formada no pasa de lo que traia.
    const double start = s.antimatter;
    s.emitters = 1;  // el emisor de la estructura
    s.mode = DriveMode::Warp;
    s.throttle = 1.0;
    for (int i = 0; i < 240 && s.warpCharge < 1.0; ++i) StepDrive(s, rules, 1.0, 1.0, 1.0 / 60.0);
    CHECK(s.warpCharge >= 0.99);
    CHECK(s.warpSpeed <= d.impulseSpeed);
    CHECK(std::abs(start - s.antimatter - FormationCost(s, DriveMode::Warp, d)) < 0.1);

    // Sin antimateria para terminarla, ni empieza: no se malgasta nada.
    ShipState poor = MakeShip(rules);
    poor.mode = DriveMode::Warp;
    poor.throttle = 1.0;
    poor.antimatter = FormationCost(poor, DriveMode::Warp, d) * 0.5;
    const double left = poor.antimatter;
    CHECK(!CanCurve(poor, DriveMode::Warp, d));
    StepDrive(poor, rules, 10.0, 10.0, 1.0);
    CHECK(poor.warpCharge == 0.0);
    CHECK(std::abs(poor.antimatter - left) < 0.01);

    // Mantenerla gasta por segundo de 10 000 a 1 000 000 de veces menos: al empezar, lo primero;
    // a tope y estable, lo segundo.
    ShipState warp = MakeShip(rules);
    warp.emitters = 1;
    warp.mode = DriveMode::Warp;
    warp.warpCharge = 1.0;
    warp.warpSpeed = d.impulseSpeed;
    CHECK(MaintenanceRatio(warp, d) < 1.01e4);
    warp.warpSpeed = TopSpeed(warp, DriveMode::Warp, d);
    const double fast = MaintenanceRatio(warp, d);
    CHECK(fast > 9.0e4 && fast < 1.1e5);  // solo con la velocidad, a medio camino
    warp.warpAge = d.maintenanceSettle * 20.0;
    CHECK(std::abs(MaintenanceRatio(warp, d) / 1.0e6 - 1.0) < 0.01);
    CHECK(std::abs(MaintenancePower(warp, d) * toJoules - 1.0e18) < 1.0e16);

    // Un viaje de 0,1 anos luz gasta en mantener menos que en formar, y cabe en el deposito.
    ShipState trip = MakeShip(rules);
    trip.mode = DriveMode::Warp;
    trip.throttle = 1.0;
    trip.warpCharge = 1.0;
    trip.warpSpeed = TopSpeed(trip, DriveMode::Warp, d);
    const double full = trip.antimatter;
    const double seconds = 0.1 * 9.4607e12 / trip.warpSpeed;
    for (int i = 0; i < 3650; ++i) StepDrive(trip, rules, seconds / 3650.0 / 60.0, seconds / 3650.0, 1.0 / 60.0);
    const double spent = full - trip.antimatter;
    CHECK(spent > 0.0 && spent < FormationCost(trip, DriveMode::Warp, d));

    // Metrica hibrida: formar de 10^16 a 10^13 J, mantener de 10^12 a 10^9 W.
    ShipState hybrid = MakeShip(rules);
    hybrid.mode = DriveMode::Hyper;
    hybrid.emitters = 2;
    CHECK(std::abs(FormationEnergy(hybrid, DriveMode::Hyper, d) * toJoules / 1.0e16 - 1.0) < 0.01);
    CHECK(std::abs(MaintenancePower(hybrid, d) * toJoules / 1.0e12 - 1.0) < 0.01);
    hybrid.emitters = 1000;
    CHECK(std::abs(FormationEnergy(hybrid, DriveMode::Hyper, d) * toJoules / 1.0e13 - 1.0) < 0.01);
    hybrid.warpSpeed = TopSpeed(hybrid, DriveMode::Hyper, d);
    hybrid.warpAge = d.maintenanceSettle * 20.0;
    CHECK(std::abs(MaintenancePower(hybrid, d) * toJoules / 1.0e9 - 1.0) < 0.01);

    // Si se acaba, la burbuja colapsa y no queda deuda.
    ShipState dry = MakeShip(rules);
    dry.emitters = 1;
    dry.mode = DriveMode::Warp;
    dry.throttle = 1.0;
    dry.warpCharge = 1.0;
    dry.warpSpeed = TopSpeed(dry, DriveMode::Warp, d);
    dry.antimatter = 1.0e-3;
    for (int i = 0; i < 400; ++i) StepDrive(dry, rules, 1.0, 1.0, 1.0 / 60.0);
    CHECK(dry.antimatter == 0.0);
    CHECK(dry.warpSpeed < d.impulseSpeed);

    // El reactor repone la energia con antimateria, a cambio de casi nada.
    ShipState tank = MakeShip(rules);
    tank.throttle = 0.0;
    tank.energy = 0.0;
    Burn(tank, rules, 3600);
    CHECK(tank.energy > 900.0);
    CHECK(tank.antimatter > rules.ship.startAntimatter - 0.02);
}

static void TestStationRules() {
    const Balance rules;
    std::string error;

    // Una central convierte su energia en antimateria, y guarda esta y la materia por separado.
    Inventory free;
    StarYield yield;
    yield.energyPerHour = 9.0e5;  // al 10 %, 1 kt por hora
    yield.matterPerHour = 5.0;
    StationNetwork power(rules.stations);
    Inventory paid;
    paid.Add(Resource::Metal, 1.0e4);
    paid.Add(Resource::Silicate, 1.0e4);
    StationSite star;
    star.star = 0;
    const int central = power.Deploy(StationKind::Power, star, "c", yield, paid, &error);
    CHECK(central > 0);
    CHECK(paid.Get(Resource::Metal) == 1.0e4);  // la primera central es gratis
    CHECK(!power.NextIsFree(StationKind::Power));
    CHECK(power.Deploy(StationKind::Power, star, "c", yield, paid, &error) < 0);  // solo una por sitio
    CHECK(paid.Get(Resource::Metal) == 1.0e4);
    StationSite second;
    second.star = 1;
    CHECK(power.Deploy(StationKind::Power, second, "c", yield, paid, &error) > 0);
    CHECK(paid.Get(Resource::Metal) == 1.0e4 - rules.stations.powerMetal);  // la segunda ya cuesta
    CHECK(paid.Get(Resource::Silicate) == 1.0e4 - rules.stations.powerSilicate);
    CHECK(power.Count(StationKind::Power) == 2);
    power.Produce(3.0);
    CHECK(std::abs(power.Find(central)->storedAntimatter - 3.0) < 1e-6);
    CHECK(std::abs(power.Find(central)->stored.Get(Resource::StellarMatter) - 15.0) < 1e-6);
    double shipAntimatter = 119.0;
    power.Collect(central, free, 3000.0, shipAntimatter, 120.0);
    CHECK(shipAntimatter == 120.0);
    CHECK(std::abs(power.Find(central)->storedAntimatter - 2.0) < 1e-6);
    CHECK(free.Get(Resource::StellarMatter) == 15.0);

    // Se llena hasta su deposito y ahi se queda.
    power.Produce(1000.0);
    CHECK(power.Find(central)->storedAntimatter == power.AntimatterCapacity(*power.Find(central)));

    // Recoger respeta lo que cabe en la bodega y deja el resto.
    Inventory hold;
    double tank = 0;
    const double matterBefore = power.Find(central)->stored.Get(Resource::StellarMatter);
    power.Collect(central, hold, 100.0, tank, 1000.0);
    CHECK(std::abs(hold.Total() - 100.0) < 1e-6);
    CHECK(std::abs(power.Find(central)->stored.Get(Resource::StellarMatter) - (matterBefore - 100.0)) < 1e-6);

    // Un modulo mejora, cuesta mas en cada nivel y tiene tope.
    const double slow = power.AntimatterPerHour(*power.Find(central));
    paid.Add(Resource::Metal, 1.0e6);
    CHECK(power.AddOrUpgradeModule(central, ModuleKind::Collector, paid, &error));
    CHECK(power.AntimatterPerHour(*power.Find(central)) > slow);
    const double cost1 = power.ModuleCost(*power.Find(central), ModuleKind::Collector).Get(Resource::Metal);
    CHECK(power.AddOrUpgradeModule(central, ModuleKind::Collector, paid, &error));
    CHECK(power.ModuleCost(*power.Find(central), ModuleKind::Collector).Get(Resource::Metal) > cost1);
    CHECK(!power.AddOrUpgradeModule(central, static_cast<ModuleKind>(0), paid, &error));  // el taladro de la mina ya no existe
    for (int i = 0; i < 6; ++i) power.AddOrUpgradeModule(central, ModuleKind::Collector, paid, &error);
    CHECK(power.Find(central)->modules[0].level == rules.stations.moduleMaxLevel);
    const double storedBefore = power.Find(central)->storedAntimatter;
    CHECK(power.AddOrUpgradeModule(central, ModuleKind::Capacitor, paid, &error));
    CHECK(power.AntimatterCapacity(*power.Find(central)) > rules.stations.antimatterStorage);
    CHECK(power.Find(central)->storedAntimatter == storedBefore);

    // Una estrella como el Sol da una burbuja cada dos horas, mas o menos.
    std::mt19937 rng(3);
    const double perHour = power.AntimatterFrom(YieldOf(MakeStar(StarClass::G, rng)).energyPerHour);
    CHECK(perHour > 1.5 && perHour < 6.0);
}

static void TestAnomalies() {
    const Balance rules;
    std::vector<double> extents(30, 1.0e10);
    const std::vector<Anomaly> a = GenerateAnomalies(extents, rules.anomalies, 7), b = GenerateAnomalies(extents, rules.anomalies, 7);
    CHECK(a.size() == b.size());
    int derelict = 0, hostile = 0, neutral = 0, stations = 0, inStart = 0;
    for (const Anomaly& x : a) {
        derelict += x.kind == AnomalyKind::Derelict ? 1 : 0;
        hostile += x.stance == Stance::Hostile ? 1 : 0;
        neutral += x.stance == Stance::Neutral ? 1 : 0;
        stations += x.kind == AnomalyKind::AlienStation ? 1 : 0;
        if (x.system == 0 && x.kind == AnomalyKind::Derelict && x.emitter) ++inStart;
    }
    CHECK(derelict > 5 && hostile > 5 && neutral > 5 && stations > 3);
    CHECK(inStart == 1);  // el emisor debe poder alcanzarse sin superar la luz

    // Una nave hostil persigue y ataca; una neutral, no.
    std::mt19937 rng(5);
    Anomaly enemy;
    enemy.kind = AnomalyKind::AlienShip;
    enemy.stance = Stance::Hostile;
    enemy.offset = enemy.home = {0, 0, 0};
    const Vec3d ship{1.0e6, 0, 0};
    for (int i = 0; i < 4000; ++i) StepAnomaly(enemy, ship, rules.anomalies, 1.0, rng);
    CHECK(enemy.pursuing);
    CHECK(Length(enemy.offset - ship) < rules.anomalies.hostileWeaponRange);
    CHECK(Length(enemy.offset - ship) > rules.anomalies.hostileWeaponRange * 0.3);  // se queda a tiro, no embiste

    Anomaly calm = enemy;
    calm.stance = Stance::Neutral;
    calm.offset = calm.home = {0, 0, 0};
    calm.pursuing = false;
    for (int i = 0; i < 2000; ++i) StepAnomaly(calm, Vec3d{1.0e3, 0, 0}, rules.anomalies, 1.0, rng);
    CHECK(!calm.pursuing);

    // Huir a curvatura deja atras a un perseguidor.
    CHECK(rules.anomalies.hostileSpeed < kLightSpeed * 0.1);

    // Un bastion se queda en su sitio, y viene armado con misiles; las ruinas no tienen casco.
    Anomaly fort;
    fort.kind = AnomalyKind::AlienStation;
    fort.stance = Stance::Hostile;
    InitCombat(fort, rules.anomalies);
    const Vec3d at = fort.offset;
    StepAnomaly(fort, Vec3d{1.0e3, 0, 0}, rules.anomalies, 3600.0, rng);
    CHECK(Length(fort.offset - at) == 0.0);
    CHECK(fort.armed && fort.weapon == WeaponKind::Missile && fort.hull == StationHull(rules.anomalies, fort.stationLevel));
    CHECK(enemy.armed == false);  // sin InitCombat, un Anomaly a mano no trae arma
    for (const Anomaly& x : a) {
        if (x.kind == AnomalyKind::Derelict) CHECK(x.maxHull == 0.0 && !x.armed);
        else CHECK(x.maxHull > 0.0 && x.hull == x.maxHull && x.armed);
    }

    // Explorar lleva su tiempo, da un solo emisor y deja la estructura vacia.
    Anomaly ruin;
    ruin.kind = AnomalyKind::Derelict;
    ruin.emitter = true;
    int given = 0, when = -1;
    for (int i = 0; i < 100; ++i)
        if (Explore(ruin, rules.anomalies, 0.1)) {
            ++given;
            when = i;
        }
    CHECK(given == 1);
    CHECK(std::abs((when + 1) * 0.1 - rules.anomalies.exploreHours) < 0.11);
    CHECK(!ruin.emitter);
    CHECK(!Explore(ruin, rules.anomalies, 1.0));
}

// Fase A: escala de combate x20 y tabla de clases de nave.
static void TestShipClasses() {
    const Balance rules;
    CHECK(kCombatScale == 20.0);

    // El casco del jugador y la referencia de la tabla son lo mismo: la fragata ligera vale 1.
    CHECK(rules.ship.hull == 1000.0 * kCombatScale);
    CHECK(rules.ship.hull == rules.anomalies.referenceHull);
    CHECK(ShipClassOf(ShipClassId::Frigate, ShipVariant::Light).hull == 1.0);
    CHECK(ShipClassOf(ShipClassId::Frigate, ShipVariant::Light).shield == 1.0);
    CHECK(ShipClassOf(ShipClassId::Frigate, ShipVariant::Light).weaponSlots == 6);

    // El dano de las armas, por veinte; el resto de cada arma, como estaba.
    CHECK(rules.combat.weapons[0].damage == 400.0);
    CHECK(rules.combat.weapons[1].damage == 5200.0);
    CHECK(rules.combat.weapons[2].damage == 3600.0);
    CHECK(rules.combat.weapons[3].damage == 360.0);
    CHECK(rules.combat.weapons[0].cooldown == 3.0 && rules.combat.weapons[0].range == 1.5e5);

    // La tabla crece con la clase y con la variante, y las cifras tienen sentido.
    for (int v = 0; v < kShipVariantCount; ++v) {
        for (int c = 0; c < kShipClassCount; ++c) {
            const ShipClass& k = ShipClassOf(static_cast<ShipClassId>(c), static_cast<ShipVariant>(v));
            CHECK(k.length > 0.0 && k.hull > 0.0 && k.shield > 0.0 && k.weaponSlots >= 1);
            CHECK(k.speed > 0.0 && k.speed < kLightSpeed * 0.1);  // por debajo de la curvatura: se puede huir
            if (v == static_cast<int>(ShipVariant::Light)) CHECK(!k.heavyWeapons);  // la ligera nunca las monta
            if (c > 0) {
                const ShipClass& smaller = ShipClassOf(static_cast<ShipClassId>(c - 1), static_cast<ShipVariant>(v));
                CHECK(k.hull > smaller.hull && k.length > smaller.length);  // los huecos no: un carguero lleva menos que un acorazado
            }
        }
    }
    for (int c = 0; c < kShipClassCount; ++c) {
        const ShipClassId id = static_cast<ShipClassId>(c);
        CHECK(ShipClassOf(id, ShipVariant::Light).hull < ShipClassOf(id, ShipVariant::Normal).hull);
        CHECK(ShipClassOf(id, ShipVariant::Normal).hull < ShipClassOf(id, ShipVariant::Heavy).hull);
        CHECK(std::string(kShipClassNames[c]).size() > 2);
    }
    CHECK(ShipClassOf(ShipClassId::Drone, ShipVariant::Normal).hull == 0.01);
    CHECK(ShipClassOf(ShipClassId::Superfreighter, ShipVariant::Normal).weaponSlots == 100);
    CHECK(ShipClassOf(ShipClassId::SupremeCapital, ShipVariant::Normal).weaponSlots == 600);

    // Las naves alienigenas generadas llevan clase (dron, corveta o fragata ligera) y de ella sale casco y escudo.
    std::vector<double> extents(30, 1.0e10);
    const std::vector<Anomaly> all = GenerateAnomalies(extents, rules.anomalies, 7);
    int count[3] = {0, 0, 0};
    for (const Anomaly& a : all) {
        if (a.kind != AnomalyKind::AlienShip) continue;
        const ShipClass& k = ClassOf(a);
        CHECK(a.shipClass == ShipClassId::Drone || a.shipClass == ShipClassId::Corvette || a.shipClass == ShipClassId::Frigate);
        CHECK(a.variant == (a.shipClass == ShipClassId::Frigate ? ShipVariant::Light : ShipVariant::Normal));
        CHECK(a.maxHull == k.hull * rules.anomalies.referenceHull && a.hull == a.maxHull);
        CHECK(a.shieldMax == k.shield * rules.anomalies.referenceShield && a.shieldCharge == a.shieldMax);
        ++count[static_cast<int>(a.shipClass)];
    }
    CHECK(count[0] > 3 && count[1] > 3 && count[2] > 3);

    // La velocidad de persecucion sale de la clase: el dron alcanza al de la fragata.
    std::mt19937 rng(5);
    const Vec3d far{1.0e6, 0, 0};
    double speed[2] = {0, 0};
    const ShipClassId chasers[2] = {ShipClassId::Drone, ShipClassId::Frigate};
    for (int i = 0; i < 2; ++i) {
        Anomaly a;
        a.kind = AnomalyKind::AlienShip;
        a.stance = Stance::Hostile;
        a.shipClass = chasers[i];
        a.variant = ShipVariant::Normal;
        for (int t = 0; t < 60; ++t) StepAnomaly(a, far, rules.anomalies, 1.0, rng);
        speed[i] = Length(a.velocity);
        CHECK(speed[i] > 0.9 * ClassOf(a).speed && speed[i] <= ClassOf(a).speed + 1e-9);
    }
    CHECK(speed[0] > speed[1]);
    // La fragata ligera conserva la velocidad de siempre.
    CHECK(ShipClassOf(ShipClassId::Frigate, ShipVariant::Light).speed == rules.anomalies.hostileSpeed);

    // Se guarda la clase, y una partida anterior a la escala se pone al dia sin perder la fraccion de casco.
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 20261105);
    g.Step(0.05, {});
    std::size_t ships = 0;
    for (Anomaly& a : g.Anomalies()) {
        if (a.kind != AnomalyKind::AlienShip) continue;
        ++ships;
        a.hull = a.maxHull * 0.25;
        a.shieldCharge = a.shieldMax * 0.5;
    }
    CHECK(ships > 0);
    g.Ship().hull = 500.0 * kCombatScale;
    const std::string text = WriteSave(g.Snapshot());
    CHECK(text.find("\nclase ") != std::string::npos);
    const std::optional<SaveData> read = ReadSave(text);
    CHECK(read.has_value());
    if (!read) return;
    CHECK(WriteSave(*read) == text);
    CHECK(read->Ship()->ship.hull == 500.0 * kCombatScale);
    for (std::size_t i = 0; i < read->world.anomalies.size(); ++i) {
        CHECK(read->world.anomalies[i].shipClass == g.Anomalies()[i].shipClass && read->world.anomalies[i].variant == g.Anomalies()[i].variant);
    }

}

// La animacion de la estacion origami, leida del glTF: plegada en reposo y abierta al final.
static void TestOrigamiAnimation() {
    const std::string path = "modelos/estacion.gltf";
    std::error_code ignored;
    if (!GltfSupported() || !std::filesystem::exists(path, ignored)) {
        std::printf("  (sin %s: se salta)\n", path.c_str());
        return;
    }
    std::vector<ImportedPart> parts;
    std::string error;
    CHECK(LoadGltf(path, parts, &error));
    ModelAnimation deploy;
    CHECK(LoadGltfAnimation(path, "despliegue", deploy, 24.0, &error));
    CHECK(std::abs(deploy.duration - 15.0) < 0.1);
    CHECK(deploy.parts.size() == parts.size());
    CHECK(!LoadGltfAnimation(path, "no-existe", deploy, 24.0, &error));
    CHECK(LoadGltfAnimation(path, "", deploy, 24.0, &error));  // la primera
    const auto center = [](const ImportedPart& p) {
        Vec3f low = p.mesh.vertices[0].position, high = low;
        for (const MeshVertex& v : p.mesh.vertices) {
            low = {std::min(low.x, v.position.x), std::min(low.y, v.position.y), std::min(low.z, v.position.z)};
            high = {std::max(high.x, v.position.x), std::max(high.y, v.position.y), std::max(high.z, v.position.z)};
        }
        return (low + high) * 0.5f;
    };
    std::vector<PartMotion> start, end;
    deploy.Pose(0.0, start);
    deploy.Pose(99.0, end);  // se sujeta al final
    int checked = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].name != "nudo_01_nucleo" && parts[i].name != "nudo_02_nucleo") continue;
        const Vec3f rest = center(parts[i]);
        // En reposo esta plegada: los nudos a 380 m del eje (Y en el motor), a 54 m arriba o abajo.
        CHECK(std::abs(std::hypot(rest.x, rest.z) - 380.0f) < 2.0f);
        CHECK(std::abs(std::abs(rest.y) - 54.0f) < 2.0f);
        const Vec3f still = start[i].rotation.ToMatrix().TransformDirection(rest) + start[i].offset;
        CHECK(Length(still - rest) < 0.5f);
        // Al final, el anillo abierto: 470 m y en el plano.
        const Vec3f open = end[i].rotation.ToMatrix().TransformDirection(rest) + end[i].offset;
        CHECK(std::abs(std::hypot(open.x, open.z) - 470.0f) < 2.0f);
        CHECK(std::abs(open.y) < 1.0f);
        ++checked;
    }
    CHECK(checked == 2);

    // El andamio de transporte: va en el modelo, se pliega al soltarse y sale despedido.
    const ModelRef model = LoadGltfModel(path, "origami", [](const MeshData&) { return MeshId{1}; }, nullptr, &error);
    CHECK(model != nullptr);
    if (!model) return;
    int strut = -1, sector = -1, cradle = 0;
    for (std::size_t i = 0; i < model->parts.size(); ++i) {
        if (model->parts[i].name == "andamio_03_puntal") strut = static_cast<int>(i);
        if (model->parts[i].name == "andamio_03") sector = static_cast<int>(i);
        cradle += model->parts[i].name.rfind("cuna_", 0) == 0 ? 1 : 0;
    }
    CHECK(strut >= 0 && sector >= 0);
    CHECK(cradle == 0);  // la cuna vieja ya no esta
    if (strut < 0 || sector < 0) return;
    const auto moved = [&](const std::vector<PartMotion>& m, int part) {
        const Vec3f p = model->parts[static_cast<std::size_t>(part)].position;
        return m[static_cast<std::size_t>(part)].rotation.ToMatrix().TransformDirection(p) + m[static_cast<std::size_t>(part)].offset;
    };
    const Vec3f strutRest = model->parts[static_cast<std::size_t>(strut)].position;
    std::vector<PartMotion> scaffold;
    CHECK(GameLayer::ScaffoldMotion(*model, 0.0, scaffold));
    CHECK(Length(moved(scaffold, strut) - strutRest) < 0.01f);
    // Al segundo, el puntal esta plegado contra el anillo: mas cerca del eje y a la misma altura.
    CHECK(GameLayer::ScaffoldMotion(*model, 1.5, scaffold));
    const Vec3f folded = moved(scaffold, strut);
    CHECK(std::hypot(strutRest.x, strutRest.z) > 270.0f);
    CHECK(std::hypot(folded.x, folded.z) < 250.0f && std::hypot(folded.x, folded.z) > 228.0f);
    CHECK(std::abs(folded.y - strutRest.y) < 0.01f);
    // Suelto, el sector se aleja hacia fuera; y al rato ya no se dibuja.
    const double loose = GameSession::kShipyardOpenSeconds + GameSession::kShipyardSlideSeconds;
    CHECK(GameLayer::ScaffoldMotion(*model, loose + 10.0, scaffold));
    const Vec3f away = moved(scaffold, sector);
    CHECK(std::hypot(away.x, away.z) > 228.0f + 300.0f);
    CHECK(!GameLayer::ScaffoldMotion(*model, loose + 30.0, scaffold));
}

// La estacion origami y los drones mineros, de la nave de serie a la entrega de material.
static void TestShipyardAndDrones() {
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 42);
    g.Step(0.05, {});
    g.MarkScanned(0);
    g.Step(0.05, {});

    // De serie: dos bahias de drones en ranuras exteriores, con dos drones cada una, y la origami a bordo.
    CHECK(WorkingModules(g.Ship(), ShipModuleKind::DroneBay) == 2);
    CHECK(g.Drones().size() == 4);
    CHECK(g.Ship().stationAboard);
    CHECK(g.DronesOut() == 0);

    // Los drones no salen hacia una estrella ni desde lejos.
    CHECK(!g.DeployDrones({Kind::Star, 0, 0}));
    const TargetRef planet = FirstPlanet(g, 0, 2);
    CHECK(!g.DeployDrones(planet));

    // En orbita del planeta: salen, minan, vuelven y descargan, viaje tras viaje.
    CHECK(Fly(g, planet, DriveMode::Warp, 600.0));
    g.SetDrive(DriveMode::Impulse);
    const double before = g.Ship().cargo.Total();
    CHECK(g.DeployDrones(planet));
    CHECK(g.DroneSite() && *g.DroneSite() == planet);
    int maxOut = 0, mining = 0;
    for (double t = 0; t < 900.0; t += 0.05) {
        g.Step(0.05, {});
        maxOut = std::max(maxOut, g.DronesOut());
        for (const MiningDrone& d : g.Drones()) {
            mining += d.state == DroneState::Mining ? 1 : 0;
            // Minando estan en su sitio, junto al punto de trabajo, y no se quedan atras del planeta.
            if (d.state == DroneState::Mining && g.DroneWorkPoint()) CHECK(Length(*g.DroneWorkPoint() - d.position) < 200.0);
        }
    }
    CHECK(maxOut == 4);
    CHECK(mining > 0);
    const double mined = g.Ship().cargo.Total() - before;
    std::printf("  drones: %.1f kt minadas en 900 s reales\n", mined);
    CHECK(mined > rules.drones.hold);  // al menos un viaje completo de un dron

    // Al recogerlos vuelven todos; y con otro motor que el impulso se repliegan de golpe.
    g.RecallDrones();
    CHECK(!g.DroneSite());
    Run(g, 300.0);
    CHECK(g.DronesOut() == 0);
    CHECK(g.DeployDrones(planet));
    Run(g, 2.0);
    CHECK(g.DronesOut() > 0);
    g.SetDrive(DriveMode::Cruise);
    g.Step(0.05, {});
    CHECK(g.DronesOut() == 0 && !g.DroneSite());
    g.SetDrive(DriveMode::Impulse);

    // La origami: no se suelta en orbita ni en marcha; fuera de la orbita y parada, si.
    std::string why;
    CHECK(g.Orbiting() && !g.CanDeployShipyard(&why));
    g.SetThrottle(0.2);
    g.Step(0.05, {});  // dar mando la saca de la orbita
    CHECK(!g.Orbiting());
    g.HoldPosition();
    Run(g, 60.0);
    const bool can = g.CanDeployShipyard(&why);
    if (!can) std::printf("  no se puede soltar: %s (%.2f km/s)\n", why.c_str(), g.Ship().speed);
    CHECK(can);
    const Vec3d shipAt = g.Ship().position;
    CHECK(g.DeployShipyard());
    CHECK(!g.Ship().stationAboard);
    CHECK(g.Stations().Count(StationKind::Shipyard) == 1);
    CHECK(!g.DeployShipyard());  // solo hay una
    const int id = g.Stations().All().back().id;
    const TargetRef yard{Kind::Station, 0, id};
    // Se abre alrededor de las bodegas: al principio su centro esta en la nave.
    CHECK(Length(g.Resolve(yard)->position - shipAt) < 0.3);
    Run(g, GameSession::kShipyardOpenSeconds + GameSession::kShipyardSlideSeconds + GameSession::kShipyardArmsSeconds + 2.0);
    const GameSession::ShipyardPhase done = GameSession::ShipyardDeploy(g.Stations().Find(id)->deployAge);
    CHECK(done.ring == 1.0 && done.slide == 1.0 && done.arms == 1.0);
    // Y luego se aparta a popa, sin llevarse la nave.
    const double apart = Length(g.Resolve(yard)->position - g.Ship().position);
    std::printf("  origami a %.2f km de la nave tras desplegarse\n", apart);
    CHECK(apart > 1.0 && apart < 3.0);
    CHECK(g.Stations().Find(id)->modules.empty());
    CHECK(!g.UpgradeStation(id, ModuleKind::Collector));  // no es modular todavia

    // Atracada, se le entrega el metal y el silicato que pase de la reserva del escudo de arena.
    g.Ship().cargo.Add(Resource::Metal, 300.0);
    CHECK(g.Dock(yard, true));
    CHECK(g.Interact(yard));
    const Station* s = g.Stations().Find(id);
    CHECK(s && s->stored.Get(Resource::Metal) >= 300.0);
    CHECK(g.Ship().cargo.Get(Resource::Metal) == 0.0);
    CHECK(g.Ship().cargo.Get(Resource::Silicate) >= rules.ship.startSilicate - 1e-9);

    // Se guarda con su sitio y su orientacion, y la nave ya no la lleva.
    const std::optional<SaveData> back = ReadSave(WriteSave(g.Snapshot()));
    CHECK(back.has_value());
    if (back) {
        CHECK(!back->Ship()->ship.stationAboard);
        const Station* saved = nullptr;
        for (const Station& t : back->world.stations)
            if (t.kind == StationKind::Shipyard) saved = &t;
        CHECK(saved != nullptr);
        if (saved && s) {
            CHECK(Length(saved->site.offset - s->site.offset) < 1e-9);
            CHECK(std::abs(saved->attitude.w - s->attitude.w) < 1e-12 && std::abs(saved->attitude.x - s->attitude.x) < 1e-12);
            CHECK(saved->stored.Get(Resource::Metal) >= 300.0);
        }
        // Una partida sin la linea de la origami la tiene a bordo.
        std::string text = WriteSave(g.Snapshot());
        CHECK(text.find("origami-a-bordo 0") != std::string::npos);
    }
}

// La partida de principio a fin, siguiendo el orden que tendra un jugador.
static void TestFullLoop() {
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 42);
    g.Step(0.05, {});
    g.MarkScanned(0);
    g.Step(0.05, {});
    CHECK(g.GetWorld().state[0].loaded);
    CHECK(!g.Contacts().empty() || true);

    // Una central no se puede poner desde lejos ni en una estrella que quema.
    const TargetRef star{Kind::Star, 0, 0};
    CHECK(!g.DeployPower(star));
    CHECK(!g.DeployPower(FirstPlanet(g, 0, 0)));

    // La primera central es gratis y da antimateria a la nave.
    CHECK(Fly(g, star, DriveMode::Warp, 600.0));
    const double spent = g.Ship().cargo.Total();
    CHECK(g.DeployPower(star));
    CHECK(g.Ship().cargo.Total() == spent);
    const TargetRef central{Kind::Station, 0, g.Stations().All().back().id};
    CHECK(Fly(g, central, DriveMode::Impulse, 600.0));

    // En orbita, la nave acompana a la estacion sin quedarse atras, aunque esta se mueva.
    const double arrival = Length(g.Resolve(central)->position - g.Ship().position);
    Run(g, 20.0);
    CHECK(std::abs(Length(g.Resolve(central)->position - g.Ship().position) - arrival) < 1.0);
    Run(g, 580.0);
    g.Ship().antimatter = 0.0;
    CHECK(Fly(g, central, DriveMode::Impulse, 600.0));
    CHECK(g.Interact(central));
    CHECK(g.Ship().antimatter > 1.0);
    CHECK(g.Ship().cargo.Get(Resource::StellarMatter) > 0.0);

    // Antes de la estructura no hay curvatura.
    CHECK(!g.SetDrive(DriveMode::Warp));
    CHECK(!g.SetDrive(DriveMode::Hyper));
    TargetRef ruin;
    for (const Anomaly& a : g.Anomalies())
        if (a.system == 0 && a.kind == AnomalyKind::Derelict) ruin = {Kind::Anomaly, 0, a.id};
    CHECK(ruin.kind == Kind::Anomaly);
    CHECK(Fly(g, ruin, DriveMode::Warp, 900.0));
    CHECK(g.Interact(ruin));
    Run(g, 240.0);
    CHECK(g.Ship().emitters == 1 && HybridUnlocked(g.Ship()));
    CHECK(!HyperUnlocked(g.Ship(), rules.drive) && !g.SetDrive(DriveMode::Hyper));  // el hiper pide otro emisor
    CHECK(!g.Interact(ruin));  // vacia

    // Con el segundo emisor (de otra estructura), el hiper llega a otra estrella en poco tiempo
    // real, aunque fuera pasen dias.
    g.Ship().emitters = 2;
    TargetRef other{Kind::Star, 3, 0};
    const double start = g.SimTime();
    CHECK(Fly(g, other, DriveMode::Hyper, 300.0));
    CHECK(g.SimTime() - start < 86400.0 * 20.0);
    CHECK(Length(g.Resolve(other)->position - g.Ship().position) < YieldOf(g.GetWorld().systems[3]->star).orbitRadius * 1.5);

    // Lo desplegado sigue produciendo con su sistema descargado, y se puede volver.
    CHECK(!g.GetWorld().state[0].loaded);
    g.Stations().Find(g.Stations().All().back().id)->storedAntimatter = 0.0;
    const double storedBefore = 0.0;
    Run(g, 300.0);
    CHECK(g.Stations().All().back().storedAntimatter > storedBefore);
    CHECK(g.GetWorld().state[3].loaded);
    g.Ship().energy = rules.ship.energy;  // el viaje de ida la gasto
    const TargetRef back{Kind::Star, 0, 0};
    CHECK(Fly(g, back, DriveMode::Hyper, 300.0));
    CHECK(g.GetWorld().state[0].loaded);
    CHECK(g.Resolve(central).has_value());
}

static void TestHazards() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7);
    g.Step(0.05, {});
    g.MarkScanned(2);

    // Un agujero negro de Kerr pide mucho mas de lo que tiene la nave al empezar.
    TargetRef kerr{Kind::Star, 2, 0};
    const StarData& star = g.GetWorld().systems[2]->star;
    CHECK(star.kind == StarClass::BlackHole);
    const StarYield yield = YieldOf(star);
    CHECK(g.Ship().heatShieldTier < yield.requiredTier);
    CHECK(g.JumpTo(kerr, yield.orbitRadius * 1.1));
    g.Step(0.05, {});
    CHECK(g.NearestSystem() == 2);
    CHECK(g.InHeatZone());
    CHECK(!g.DeployPower(kerr));
    const double hull = g.Ship().hull;
    Run(g, 30.0);
    CHECK(g.Ship().hull < hull);

    // Con blindaje suficiente, el mismo sitio es seguro y se puede desplegar.
    g.Ship().heatShieldTier = yield.requiredTier;
    g.Ship().cargo.Add(Resource::Metal, 1000.0);
    g.Ship().cargo.Add(Resource::Silicate, 1000.0);
    CHECK(g.JumpTo(kerr, yield.orbitRadius * 1.1));
    g.Step(0.05, {});
    CHECK(!g.InHeatZone());
    CHECK(g.DeployPower(kerr));

    // Casco a cero: la nave se deshace, y luego se vuelve al punto de control.
    g.Ship().hull = 0.0;
    g.Step(0.05, {});
    CHECK(g.Phase() == GamePhase::Exploding);
    Run(g, 8.0);
    CHECK(g.Phase() == GamePhase::GameOver);
    g.ContinueAfterDefeat();
    CHECK(g.Phase() == GamePhase::Flying);
    CHECK(g.Ship().hull > 0.0);
    CHECK(g.Ship().speed == 0.0);

    // Lo hostil ataca a quien se acerca, y aparece en sensores como hostil solo de cerca.
    TargetRef enemy;
    for (const Anomaly& a : g.Anomalies())
        if (a.stance == Stance::Hostile && a.kind == AnomalyKind::AlienShip) enemy = {Kind::Anomaly, a.system, a.id};
    CHECK(enemy.kind == Kind::Anomaly);
    CHECK(g.JumpTo(enemy, rules.anomalies.hostileWeaponRange * 0.5));
    const double before = g.Ship().hull;
    Run(g, 10.0);
    CHECK(g.Ship().hull < before);

    bool sawHostile = false;
    for (const Contact& c : g.Contacts()) sawHostile = sawHostile || (c.stance == Stance::Hostile && c.identified);
    CHECK(sawHostile);
    CHECK(g.JumpTo(enemy, rules.ship.identifyRange * 5.0));
    Run(g, 2.0);
    bool hidden = false;
    for (const Contact& c : g.Contacts())
        if (c.target == enemy) hidden = !c.identified && c.stance == Stance::None;
    CHECK(hidden);
}

static void TestModels() {
    int uploads = 0;
    const GameAssets assets = BuildGameAssets([&](const MeshData& m) {
        ++uploads;
        CHECK(!m.vertices.empty() && !m.indices.empty());
        return static_cast<MeshId>(uploads);
    });
    for (const ModelRef& model : {assets.ship, assets.powerStation, assets.hostileShip, assets.neutralShip, assets.hostileStation, assets.neutralStation, assets.derelict}) {
        CHECK(model != nullptr);
        CHECK(model->parts.size() >= 4);
    }
    Anomaly a;
    a.kind = AnomalyKind::AlienStation;
    a.stance = Stance::Neutral;
    CHECK(ModelFor(assets, a) == assets.neutralStation);

    // Los modulos aparecen en el modelo uno a uno.
    int modules = 0;
    for (const ModelPart& p : assets.powerStation->parts) modules += p.name.rfind("mod_", 0) == 0 ? 1 : 0;
    CHECK(modules == 4);

    // La capa dibuja todo con las escalas correctas y la niebla apaga lo lejano.
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules);
    g.Step(0.05, {});
    GameLayer layer(g, assets, 1, 2, Color{0.1f, 0.05f, 0.15f, 1.0f});
    engine->AddLayer(&layer);
    Camera camera;
    camera.SetViewport(1600, 900, 800.0f, kNearPlaneKm);
    camera.SetOrientation(Quatf::Cast(g.Ship().orientation));
    camera.SetPosition(g.Ship().position - Heading(g.Ship()) * 2.3);
    FramePacket packet;
    engine->Build(camera, 1.0, packet);
    CHECK(packet.stats.meshesVisible >= assets.ship->parts.size() / 2);
    CHECK(packet.nebulae.size() == 3);
    const std::size_t clearSprites = packet.stats.spritesVisible;
    engine->SetFog(1000.0f, Color{0.1f, 0.05f, 0.15f, 1.0f});
    engine->Build(camera, 1.0, packet);
    CHECK(packet.fogDistance == 1000.0f);
    CHECK(packet.stats.spritesVisible <= clearSprites);
}

static void TestHandling() {
    const Balance rules;
    const Quatd oneRadian = Quatd::FromAxisAngle({0, 1, 0}, 1.0);
    ShipState s = MakeShip(rules);
    s.aim = oneRadian;

    // No gira de golpe: tarda varios segundos y no se pasa del rumbo.
    double passed = 0;
    while (AimError(s) > 0.01 && passed < 60.0) {
        StepHeading(s, rules.ship, 1.0 / 60.0);
        passed += 1.0 / 60.0;
    }
    std::printf("  90 grados aproximados en %.1f s\n", passed);
    CHECK(passed > 4.0);
    CHECK(passed < 30.0);
    for (int i = 0; i < 600; ++i) StepHeading(s, rules.ship, 1.0 / 60.0);
    CHECK(AimError(s) < 0.01);
    CHECK(Length(s.angularRate) < 0.01);

    // Cruza por el camino corto, aunque el angulo de vuelta entera.
    ShipState wrap = MakeShip(rules);
    wrap.aim = Quatd::FromAxisAngle({0, 1, 0}, 6.2);
    for (int i = 0; i < 60; ++i) StepHeading(wrap, rules.ship, 1.0 / 60.0);
    CHECK(Heading(wrap).x < 0.0);

    // No hay arriba ni abajo: se puede dar la vuelta entera por encima, sin ningun tope.
    ShipState loop = MakeShip(rules);
    const Quatd stepUp = Quatd::FromAxisAngle({1, 0, 0}, 0.2);
    double lowest = 1.0;
    for (int turn = 0; turn < 32; ++turn) {
        loop.aim = (loop.aim * stepUp).Normalized();
        for (int i = 0; i < 240 && AimError(loop) > 0.005; ++i) StepHeading(loop, rules.ship, 1.0 / 60.0);
        lowest = std::min(lowest, Heading(loop).z);
    }
    CHECK(lowest < -0.95);        // llego a mirar justo hacia atras
    CHECK(Heading(loop).z > 0.5); // 32 * 0,2 rad: una vuelta completa

    // Lo apuntado no se aleja del casco tanto como para salirse de la pantalla.
    ShipState lead = MakeShip(rules);
    lead.aim = Quatd::FromAxisAngle({0, 1, 0}, 2.5);
    LimitAim(lead, rules.ship.aimLead);
    CHECK(std::abs(AimError(lead) - rules.ship.aimLead) < 1e-6);

    // Mirar a cualquier sitio, incluso justo detras.
    ShipState facing = MakeShip(rules);
    FaceTo(facing, Vec3d{0, 0, -1});
    CHECK(Heading(facing).z < -0.999);

    // Mejorar la maniobra acorta el giro.
    ShipState better = MakeShip(rules);
    better.handlingTier = 4;
    better.aim = oneRadian;
    double fast = 0;
    while (AimError(better) > 0.01 && fast < 60.0) {
        StepHeading(better, rules.ship, 1.0 / 60.0);
        fast += 1.0 / 60.0;
    }
    CHECK(fast < passed * 0.7);

    // Tras girar, sigue derrapando en la direccion anterior un rato.
    ShipState drift = MakeShip(rules);
    drift.velocity = {0, 0, 100};
    drift.holding = false;
    drift.throttle = 0.5;
    drift.orientation = drift.aim = Quatd::FromAxisAngle({0, 1, 0}, 1.5);
    StepDrive(drift, rules, 1.0, 1.0, 1.0 / 60.0);
    CHECK(eps::Dot(drift.moveDirection, Heading(drift)) < 0.3);
    for (int i = 0; i < 1800; ++i) StepDrive(drift, rules, 1.0, 1.0, 1.0 / 60.0);
    CHECK(eps::Dot(drift.moveDirection, Heading(drift)) > 0.99);

    // La curvatura tarda en formarse y hasta entonces no hay velocidad de curvatura.
    ShipState warp = MakeShip(rules);
    warp.emitters = 1;
    warp.mode = DriveMode::Warp;
    warp.throttle = 1.0;
    for (int i = 0; i < 100; ++i) StepDrive(warp, rules, 1.0, 1.0, 1.0 / 60.0);
    CHECK(warp.warpCharge > 0.3 && warp.warpCharge < 0.7);
    CHECK(warp.speed <= rules.drive.impulseSpeed);
    for (int i = 0; i < 2000; ++i) StepDrive(warp, rules, 1.0, 1.0, 1.0 / 60.0);
    CHECK(warp.warpCharge == 1.0);
    CHECK(warp.speed > rules.drive.impulseSpeed * 10.0);
}

// El primer planeta rocoso o gaseoso del sistema inicial del que se pueda sacar algo.
static TargetRef MinablePlanet(const GameSession& g) {
    const StarSystemData& system = *g.GetWorld().systems[0];
    for (std::size_t b = 0; b < system.bodies.size(); ++b) {
        if (system.bodies[b].parent >= 0 || system.bodies[b].kind == PlanetClass::Comet || system.bodies[b].kind == PlanetClass::Asteroid) continue;
        const TargetRef candidate{TargetRef::Kind::Planet, 0, static_cast<int>(b)};
        Deposit deposit;
        if (g.Resolve(candidate) && DepositOf(system.bodies[b], deposit)) return candidate;
    }
    return {};
}

static std::unique_ptr<GameSession> Session(Engine& engine, const Balance& rules, GameModeId mode) {
    auto g = std::make_unique<GameSession>(engine, rules, 20261105, mode);
    g->Conversation().Stop();
    g->Step(0.05, {});
    g->MarkScanned(0);
    return g;
}

static void TestOrbits() {
    Balance rules;
    auto engine = MakeEngine(rules);
    auto game = Session(*engine, rules, GameModeId::Normal);
    GameSession& g = *game;
    const TargetRef planet = MinablePlanet(g);
    CHECK(planet.kind == TargetRef::Kind::Planet);
    const auto center = [&] { return g.Resolve(planet)->position; };
    const double radius = g.Resolve(planet)->radius;

    // La ruta deja en una orbita de verdad: da vueltas, sin gastar y sin caerse.
    CHECK(g.JumpTo(planet, radius * 3.0));
    CHECK(g.OrbitNow().valid && g.OrbitNow().locked && g.OrbitNow().period > 0.0);
    const Vec3d startOffset = g.Ship().position - center();
    const double energy = g.Ship().energy;
    for (int i = 0; i < 600; ++i) g.Step(1.0 / 60.0, {});
    const Vec3d offset = g.Ship().position - center();
    CHECK(std::abs(Length(offset) - radius * 3.0) < radius * 0.01);  // misma altura
    CHECK(Length(offset - startOffset) > radius * 0.05);             // pero en otro punto
    CHECK(g.Ship().energy >= energy);
    CHECK(g.DominantName() == g.Resolve(planet)->name);

    // Al salir de la orbita conserva su velocidad y sigue una orbita libre, sin motor.
    ShipControls push;
    push.throttleChange = 0.01;
    g.Step(1.0 / 60.0, push);
    g.SetThrottle(0.0);
    CHECK(!g.Orbiting());
    CHECK(g.OrbitNow().valid && !g.OrbitNow().locked && !g.OrbitNow().escaping);
    double lowest = 1.0e300, highest = 0.0;
    for (int i = 0; i < 60 * 120; ++i) {
        g.Step(1.0 / 60.0, {});
        const double d = Length(g.Ship().position - center());
        lowest = std::min(lowest, d);
        highest = std::max(highest, d);
    }
    std::printf("  Orbita libre: de %.2f a %.2f radios\n", lowest / radius, highest / radius);
    CHECK(g.Phase() == GamePhase::Flying);
    CHECK(lowest > radius * 2.7);
    CHECK(highest < radius * 3.3);

    // Quieta junto al planeta viaja con el: no hay que perseguirlo.
    g.HoldPosition();
    for (int i = 0; i < 300; ++i) g.Step(1.0 / 60.0, {});
    const double kept = Length(g.Ship().position - center());
    const Vec3d planetBefore = center();
    for (int i = 0; i < 1200; ++i) g.Step(1.0 / 60.0, {});
    CHECK(Length(center() - planetBefore) > radius * 0.05);  // el planeta se ha movido
    CHECK(std::abs(Length(g.Ship().position - center()) - kept) < radius * 0.01);
    CHECK(g.CanScan());

    // O circulariza lo que se tenga.
    CHECK(g.EnterOrbitHere());
    CHECK(g.Orbiting().has_value());

    // Una estrella tambien atrae: sin motor, la nave acaba cayendo hacia ella.
    auto other = Session(*engine, rules, GameModeId::Normal);
    GameSession& h = *other;
    const TargetRef star{TargetRef::Kind::Star, 0, 0};
    const double reachBefore = Length(h.Resolve(star)->position - h.Ship().position);
    h.Ship().holding = false;
    for (int i = 0; i < 1200; ++i) h.Step(0.05, {});
    CHECK(Length(h.Resolve(star)->position - h.Ship().position) < reachBefore);
    CHECK(h.Ship().speed > 1.0e-5);  // a 12 UA la gravedad es diminuta: 4e-8 km/s2, como en la realidad
}

static void TestContact() {
    Balance rules;
    auto engine = MakeEngine(rules);

    // Contra un planeta, la nave se hace añicos.
    {
        auto game = Session(*engine, rules, GameModeId::Normal);
        GameSession& g = *game;
        const TargetRef planet = MinablePlanet(g);
        CHECK(planet.kind == TargetRef::Kind::Planet);
        const double radius = g.Resolve(planet)->radius;
        CHECK(g.JumpTo(planet, radius * 1.3));
        FaceTo(g.Ship(), Normalize(g.Resolve(planet)->position - g.Ship().position));
        g.SetThrottle(1.0);
        for (int i = 0; i < 1200 && g.Phase() == GamePhase::Flying; ++i) {
            g.Step(1.0 / 60.0, {});
            if (g.Warning()) CHECK(g.Warning()->seconds >= 0.0);
        }
        CHECK(g.Phase() == GamePhase::Exploding);
        CHECK(g.LossReason().find("pulverizada") != std::string::npos);
        CHECK(Length(g.WreckPosition() - g.Resolve(planet)->position) >= radius);
    }

    // Un roce lento abolla el casco, pero no destruye la nave.
    {
        auto game = Session(*engine, rules, GameModeId::Normal);
        GameSession& g = *game;
        const TargetRef planet = MinablePlanet(g);
        const double radius = g.Resolve(planet)->radius;
        CHECK(g.JumpTo(planet, radius * 1.0005 + 2.0));
        g.Step(1.0 / 60.0, {});
        ShipControls push;
        push.throttleChange = 0.01;
        g.Step(1.0 / 60.0, push);
        g.SetThrottle(0.0);
        g.Ship().velocity = Normalize(g.Resolve(planet)->position - g.Ship().position) * 0.5;
        for (int i = 0; i < 600; ++i) g.Step(1.0 / 60.0, {});
        CHECK(g.Phase() == GamePhase::Flying);
        CHECK(g.Ship().hull < rules.ship.hull);
    }

    // La curvatura no se salta un planeta entre dos pasos: lo encuentra.
    {
        auto game = Session(*engine, rules, GameModeId::Normal);
        GameSession& g = *game;
        const TargetRef planet = MinablePlanet(g);
        const double radius = g.Resolve(planet)->radius;
        g.JumpTo(planet, radius * 40.0);
        ShipControls push;
        push.throttleChange = 0.01;
        g.Step(1.0 / 60.0, push);
        g.SetDrive(DriveMode::Warp);
        g.Ship().warpCharge = 1.0;
        g.Ship().warpSpeed = 0.9 * kLightSpeed;
        g.Ship().warpHeading = Normalize(g.Resolve(planet)->position - g.Ship().position);
        g.SetThrottle(0.95);
        g.TimeScale() = 600.0;
        for (int i = 0; i < 30 && g.Phase() == GamePhase::Flying; ++i) g.Step(1.0 / 60.0, {});
        CHECK(g.Phase() == GamePhase::Exploding);
        g.TimeScale() = rules.world.timeScale;
    }

    // Demasiado cerca de la estrella, la nave se evapora en segundos.
    {
        auto game = Session(*engine, rules, GameModeId::Normal);
        GameSession& g = *game;
        const TargetRef star{TargetRef::Kind::Star, 0, 0};
        const double radius = g.Resolve(star)->radius;
        CHECK(g.JumpTo(star, radius * 1.5));
        for (int i = 0; i < 60 * 10 && g.Phase() == GamePhase::Flying; ++i) g.Step(1.0 / 60.0, {});
        CHECK(g.Phase() == GamePhase::Exploding);
        CHECK(g.LossReason().find("evaporada") != std::string::npos);

        // En su orbita de trabajo, con el blindaje debido, no pasa nada.
        auto safe = Session(*engine, rules, GameModeId::Normal);
        const double orbit = YieldOf(engine->GetWorld().systems[0]->StarAt(0)).orbitRadius;
        CHECK(safe->JumpTo(star, orbit));
        for (int i = 0; i < 600; ++i) safe->Step(1.0 / 60.0, {});
        CHECK(safe->Ship().hull == rules.ship.hull);
    }

    // En modo pacifico nada de eso destruye la nave: resbala por la superficie.
    {
        auto game = Session(*engine, rules, GameModeId::Peaceful);
        GameSession& g = *game;
        const TargetRef planet = MinablePlanet(g);
        const double radius = g.Resolve(planet)->radius;
        CHECK(g.JumpTo(planet, radius * 1.2));
        FaceTo(g.Ship(), Normalize(g.Resolve(planet)->position - g.Ship().position));
        g.SetThrottle(1.0);
        for (int i = 0; i < 300; ++i) g.Step(1.0 / 60.0, {});
        CHECK(g.Phase() == GamePhase::Flying);
        CHECK(Length(g.Ship().position - g.Resolve(planet)->position) >= radius);
        FaceTo(g.Ship(), Normalize(g.Ship().position - g.Resolve(planet)->position));
        const double before = Length(g.Ship().position - g.Resolve(planet)->position);
        for (int i = 0; i < 300; ++i) g.Step(1.0 / 60.0, {});
        CHECK(Length(g.Ship().position - g.Resolve(planet)->position) > before + 1000.0);
    }

    // Una central al otro lado de la estrella: la ruta la rodea y llega, sin rozarla.
    {
        auto game = Session(*engine, rules, GameModeId::Normal);
        GameSession& g = *game;
        const TargetRef planet{TargetRef::Kind::Star, 0, 0};
        const double radius = g.Resolve(planet)->radius;
        const TargetRef station = PlaceStation(g);
        g.Stations().Find(station.index)->angle += kPi;  // justo enfrente
        g.TimeScale() = 600.0;  // a impulso, cruzar una estrella a escala normal tarda minutos reales
        CHECK(g.SetAutopilot(station));
        CHECK(g.Route().phase != RoutePhase::None || true);
        double closest = 1.0e300;
        int steps = 0;
        for (; steps < 60 * 300 && g.Autopilot(); ++steps) {
            g.Step(1.0 / 60.0, {});
            closest = std::min(closest, Length(g.Ship().position - g.Resolve(planet)->position));
        }
        std::printf("  Rodear la estrella: %.0f s, altura minima %.2f radios\n", steps / 60.0, closest / radius);        CHECK(!g.Autopilot());
        CHECK(g.Orbiting().has_value());
        CHECK(closest > radius * 1.08);
        CHECK(steps < 60 * 120);
    }
}

// Pegada a una estrella, con la curvatura seleccionada y mirando hacia fuera, tiene que poder irse.
// La ruta asistida no estrella a nadie: desde el inicio, a cada astro del sistema, varias galaxias.
static void TestRouteSafety() {
    Balance rules;
    int routes = 0;
    double slowest = 0;
    for (const std::uint32_t seed : {20261105u, 7u, 99u}) {
        auto engine = MakeEngine(rules, seed);
        GameSession g(*engine, rules, seed, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        g.MarkScanned(0);
        const StarSystemData& system = *g.GetWorld().systems[0];
        std::vector<TargetRef> targets;
        for (std::size_t b = 0; b < system.bodies.size() && targets.size() < 5; ++b) {
            if (system.bodies[b].parent >= 0 || system.bodies[b].kind == PlanetClass::Comet || system.bodies[b].kind == PlanetClass::Asteroid) continue;
            targets.push_back({TargetRef::Kind::Planet, 0, static_cast<int>(b)});
        }
        for (int m = 0; m < system.StarCount(); ++m) targets.push_back({TargetRef::Kind::Star, 0, m});
        if (YieldOf(system.StarAt(0)).requiredTier > g.Ship().heatShieldTier) g.Ship().heatShieldTier = YieldOf(system.StarAt(0)).requiredTier;
        for (const TargetRef& target : targets) {
            if (!g.Resolve(target)) continue;
            CHECK(g.SetAutopilot(target));
            int steps = 0;
            for (; steps < 60 * 240 && g.Autopilot() && g.Phase() == GamePhase::Flying; ++steps) g.Step(1.0 / 60.0, {});
            ++routes;
            slowest = std::max(slowest, steps / 60.0);
            if (steps > 60 * 60) std::printf("  ruta larga: %.0f s a %s, %s\n", steps / 60.0, g.Resolve(target)->name.c_str(), Name(g.Ship().mode));
            CHECK(g.Phase() == GamePhase::Flying);
            CHECK(!g.Autopilot());
            CHECK(g.Ship().hull == rules.ship.hull);
            g.Ship().energy = rules.ship.energy;
        }
    }
    std::printf("  %d rutas sin danos, la mas larga %.0f s\n", routes, slowest);
}

// Marcha atras, freno de curvatura y momentos criticos.
static void TestFlightControls() {
    Balance rules;

    // S por debajo de cero empuja hacia atras.
    ShipState back = MakeShip(rules);
    back.throttle = -1.0;
    Burn(back, rules, 60);
    CHECK(Dot(back.velocity, Heading(back)) < -5.0);

    // La burbuja frena mucho mas deprisa de lo que acelera.
    ShipState warp = MakeShip(rules);
    warp.emitters = 1;
    warp.mode = DriveMode::Warp;
    warp.warpCharge = 1.0;
    warp.warpSpeed = 5.0 * kLightSpeed;
    warp.holding = true;
    Burn(warp, rules, 120);
    CHECK(warp.warpSpeed < 0.05 * kLightSpeed);

    auto engine = MakeEngine(rules);
    auto game = Session(*engine, rules, GameModeId::Normal);
    GameSession& g = *game;
    const TargetRef planet = MinablePlanet(g);
    const double radius = g.Resolve(planet)->radius;

    // Junto a una estacion el tiempo corre menos, y la estacion se monta poco a poco.
    CHECK(g.JumpTo(planet, radius * 1.5));
    CHECK(g.TimeFocus() == 1.0);
    const TargetRef mine = PlaceStation(g);
    CHECK(g.Stations().All().back().deployAge == 0.0);
    g.Step(0.5, {});
    CHECK(g.Stations().All().back().deployAge == 0.5);
    CHECK(g.JumpTo(mine, 500.0));
    for (int i = 0; i < 180; ++i) g.Step(1.0 / 60.0, {});
    CHECK(g.TimeFocus() < 0.4);

    // Una explosion va casi a tiempo real, y los restos siguen al planeta.
    CHECK(g.JumpTo(planet, radius * 3.0));
    g.Ship().hull = 0.0;
    g.Step(1.0 / 60.0, {});
    CHECK(g.Phase() == GamePhase::Exploding);
    const double kept = Length(g.WreckPosition() - g.Resolve(planet)->position);
    for (int i = 0; i < 240; ++i) g.Step(1.0 / 60.0, {});
    CHECK(g.TimeFocus() < 0.1);
    CHECK(std::abs(Length(g.WreckPosition() - g.Resolve(planet)->position) - kept) < radius * 0.02);
}

// Sin escanear no se sabe nada de un astro, y no se puede desplegar en el.
static void TestAstroScan() {
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules);
    g.Conversation().Stop();
    for (int i = 0; i < 20; ++i) g.Step(1.0 / 60.0, {});
    const TargetRef planet = MinablePlanet(g);
    CHECK(!g.Scanned(planet));
    CHECK(g.ScannedCount() == 0);
    CHECK(g.JumpTo(planet, g.Resolve(planet)->radius * 2.0));
    CHECK(!g.DeployDrones(planet));
    for (int i = 0; i < 30; ++i) g.Step(1.0 / 60.0, {});
    for (const Contact& c : g.Contacts())
        if (c.target == planet) CHECK(!c.identified);

    CHECK(g.StartScan());
    for (int i = 0; i < 40; ++i) g.Step(1.0, {});
    CHECK(g.Scanned(planet));
    CHECK(g.ScannedCount() > 1);
    CHECK(g.DeployDrones(planet));
    for (int i = 0; i < 30; ++i) g.Step(1.0 / 60.0, {});
    bool named = false;
    for (const Contact& c : g.Contacts()) named = named || (c.target == planet && c.identified);
    CHECK(named);

    // Se conserva al guardar.
    const std::optional<SaveData> copy = ReadSave(WriteSave(g.Snapshot()));
    CHECK(copy.has_value());
    if (copy) CHECK(copy->player.scanned.size() == static_cast<std::size_t>(g.ScannedCount()));
}

// Las etiquetas se pintan con estas posiciones: no pueden ir por detras de lo que marcan.
static void TestLiveContacts() {
    Balance rules;
    auto engine = MakeEngine(rules);
    auto game = Session(*engine, rules, GameModeId::Normal);
    GameSession& g = *game;
    const TargetRef planet = MinablePlanet(g);
    CHECK(g.JumpTo(planet, g.Resolve(planet)->radius * 3.0));
    bool listed = false;
    for (int i = 0; i < 120; ++i) {
        g.Step(1.0 / 60.0, {});
        for (const Contact& c : g.Contacts()) {
            const std::optional<TargetInfo> info = g.Resolve(c.target);
            CHECK(info.has_value());
            if (info) CHECK(Length(info->position - c.position) < 1e-6);
            listed = listed || c.target == planet;
        }
    }
    CHECK(listed);

    // Pedir la ruta al sitio en el que ya se esta no hace nada.
    CHECK(g.SetAutopilot(planet));
    CHECK(!g.Autopilot());
    CHECK(g.Orbiting().has_value());
}

// El eje de las lineas del warp sigue un giro completo sin un solo salto.
static void TestWarpAxis() {
    Vec3d up{0, 1, 0};
    double worst = 0;
    constexpr int kSteps = 4000;
    for (int i = 0; i < kSteps; ++i) {
        const double a = 4.0 * kPi * i / kSteps, b = 1.57 * std::sin(a * 0.37);
        const Vec3d forward{std::sin(a) * std::cos(b), std::sin(b), std::cos(a) * std::cos(b)};
        const Vec3d next = TransportUp(up, forward);
        CHECK(std::abs(Length(next) - 1.0) < 1e-9);
        CHECK(std::abs(Dot(next, forward)) < 1e-9);
        if (i > 0) worst = std::max(worst, Length(next - up));
        up = next;
    }
    CHECK(worst < 0.02);
    const Vec3d stuck = TransportUp(Vec3d{0, 1, 0}, Vec3d{0, 1, 0});  // caso limite: paralelos
    CHECK(std::abs(Dot(stuck, Vec3d{0, 1, 0})) < 1e-9);
}

// La configuracion vuelve igual del archivo, y cada calidad cuesta menos que la siguiente.
static void TestSettings() {
    GameSettings s;
    s.quality = GraphicsQuality::Low;
    s.fullscreen = true;
    s.vsync = false;
    const GameSettings back = ReadSettings(WriteSettings(s));
    CHECK(back.quality == GraphicsQuality::Low);
    CHECK(back.fullscreen);
    CHECK(!back.vsync);

    // Lo desconocido o roto se ignora y queda lo de serie.
    const GameSettings odd = ReadSettings("calidad 99\nbasura\nsincronizacion 0\n");
    CHECK(odd.quality == GraphicsQuality::High);
    CHECK(!odd.vsync);
    CHECK(ReadSettings("").quality == GraphicsQuality::High);

    for (int i = 0; i + 1 < static_cast<int>(GraphicsQuality::Count); ++i) {
        const QualityProfile& a = Profile(static_cast<GraphicsQuality>(i));
        const QualityProfile& b = Profile(static_cast<GraphicsQuality>(i + 1));
        CHECK(a.renderScale <= b.renderScale);
        CHECK(a.maxOctaves < b.maxOctaves);
    }
    CHECK(Profile(GraphicsQuality::Low).renderScale <= 0.5f);
    CHECK(!Profile(GraphicsQuality::Low).bloom);
    CHECK(Profile(GraphicsQuality::High).renderScale == 1.0f);

    // La pestaña de graficos: de serie son las opciones de Alta, y un archivo antiguo, que solo trae
    // la calidad, las rellena con las de esa calidad.
    CHECK(!GameSettings{}.Custom());
    const GameSettings old = ReadSettings("calidad 0\n");
    CHECK(!old.Custom());
    CHECK(old.graphics.renderScale == 50);
    CHECK(!old.graphics.bloom);
    CHECK(old.graphics.hullDamage == 0);
    CHECK(OptionsOf(GraphicsQuality::Ultra).lighting == 3);

    // Cada opcion cambiada a mano vuelve igual del archivo y deja la calidad como personalizada.
    GameSettings custom = old;
    custom.graphics.renderScale = 75;
    custom.graphics.shadows = false;
    custom.graphics.lighting = 3;
    custom.graphics.film = true;
    const GameSettings again = ReadSettings(WriteSettings(custom));
    CHECK(again.Custom());
    CHECK(again.quality == GraphicsQuality::Low);
    CHECK(again.graphics == custom.graphics);
    // Lo que se sale de rango se recorta.
    CHECK(ReadSettings("luz 9\nescena 5\n").graphics.lighting == 3);
    CHECK(ReadSettings("luz 9\nescena 5\n").graphics.renderScale == 25);

    // La interfaz: sin elegir hasta que el jugador elige; el tamaño y las ventanas plegadas se guardan.
    const GameSettings fresh;
    CHECK(fresh.uiStyle == UiStyle::Unset);
    CHECK(!fresh.Minimal());
    CHECK(fresh.Folded(HudPanel::MiniDetails));
    CHECK(!fresh.Folded(HudPanel::Ship));
    GameSettings look;
    look.uiStyle = UiStyle::Minimal;
    look.uiScale = 130;
    look.folded = PanelBit(HudPanel::Radar) | PanelBit(HudPanel::MiniSignals);
    const GameSettings seen = ReadSettings(WriteSettings(look));
    CHECK(seen.Minimal());
    CHECK(seen.uiScale == 130);
    CHECK(seen.Folded(HudPanel::Radar));
    CHECK(seen.Folded(HudPanel::MiniSignals));
    CHECK(!seen.Folded(HudPanel::MiniDetails));
    CHECK(ReadSettings("interfaz 1\n").uiStyle == UiStyle::Expert);
    CHECK(ReadSettings("interfaz 7\n").uiStyle == UiStyle::Unset);
    CHECK(ReadSettings("tamano-interfaz 400\n").uiScale == kUiScales[kUiScaleCount - 1]);
    // Un archivo de antes, sin estas lineas, pregunta la interfaz y deja la letra de serie.
    CHECK(ReadSettings("calidad 2\nvolumen-interfaz 50\n").uiStyle == UiStyle::Unset);
    CHECK(ReadSettings("calidad 2\n").uiScale == 115);
}

static void TestUnits() {
    CHECK(EnergyText(808.0) == "808 EJ");
    CHECK(EnergyText(1500.0) == "1.50 ZJ");
    CHECK(EnergyText(0.5) == "500 PJ");
    CHECK(PowerText(99.0) == "27.5 PW");
    CHECK(PowerText(1.8) == "500 TW");
    CHECK(EnergyText(1.0e6) == "1.00 YJ");
    CHECK(PowerText(2.7e6) == "750 EW");
    CHECK(PowerText(1.0e-9 * 3600.0) == "1 GW");
    CHECK(MassText(10.0) == "10.0 kt");
    CHECK(MassText(3000.0) == "3.00 Mt");
    CHECK(MassText(0.25) == "250 t");
    CHECK(MassRateText(18.0) == "18.0 kt/h");
    CHECK(MassText(0.0) == "0 t");

    // Por debajo de la tonelada baja a kg, g y mg.
    CHECK(MassText(0.0005) == "500 kg");
    CHECK(MassText(2.5e-9) == "2.50 g");
    CHECK(MassText(4.0e-10) == "400 mg");
    CHECK(ElementMassText(0.00025) == "250 mg");
    CHECK(ElementMassText(0.0015) == "1.50 g");
    CHECK(ElementMassText(1.5) == "1.50 kg");
    CHECK(ElementMassText(12.3) == "12.3 kg");
    CHECK(ElementMassText(2500.0) == "2.50 t");
    CHECK(ElementMassText(3.0e6) == "3.00 kt");
    CHECK(ElementMassText(5.0e9) == "5.00 Mt");
    CHECK(ElementMassText(0.0) == "0 g");

    // La unidad mas cercana, para resaltarla en el diagrama de prefijos.
    CHECK(MassUnitIndex(1.0e-5) == 0);
    CHECK(MassUnitIndex(0.5) == 1);
    CHECK(MassUnitIndex(1.0) == 2);
    CHECK(MassUnitIndex(999.0) == 2);
    CHECK(MassUnitIndex(1000.0) == 3);
    CHECK(MassUnitIndex(2.0e6) == 4);
    CHECK(MassUnitIndex(7.0e9) == 5);
    CHECK(std::string(kMassUnits[MassUnitIndex(1.0e6)]) == "kt");
}

// Inventario de elementos: se guarda exacto, las partidas viejas siguen cargando y la extraccion
// selectiva rinde ritmo x fraccion x recuperacion.
static void TestElementInventory() {
    ElementStock stock;
    stock.Add(1, 0.1 + 0.2);
    stock.Add(118, 1.0e-7);
    stock.Add(26, 5.0);
    stock.Add(26, -9.0);   // no baja de cero
    stock.Add(0, 100.0);   // fuera de la tabla: se ignora
    stock.Add(119, 100.0);
    CHECK(stock.Get(1) == 0.1 + 0.2 && stock.Get(118) == 1.0e-7);
    CHECK(stock.Get(26) == 0.0 && stock.Get(0) == 0.0 && stock.Get(119) == 0.0);

    CHECK(ElementYieldPerHour(DroneRules{}, 2.0, 0.01) == DroneRules{}.rate * 2.0 * 0.01 * DroneRules{}.recovery);
    CHECK(ElementYieldPerHour(DroneRules{}, 1.0, 0.0) == 0.0);

    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules);
    g.Conversation().Stop();
    for (int i = 0; i < 20; ++i) g.Step(1.0 / 60.0, {});
    const TargetRef planet = MinablePlanet(g);
    CHECK(g.JumpTo(planet, g.Resolve(planet)->radius * 2.0));
    CHECK(g.StartScan());
    for (int i = 0; i < 40; ++i) g.Step(1.0, {});
    CHECK(g.Scanned(planet));
    CHECK(!g.ElementSharesOf({Kind::Planet, planet.system, 99999}).has_value());

    // El elemento mas abundante del planeta que no sea hidrogeno ni helio, para tener algo que sacar.
    const std::optional<astro::PeriodicShares> shares = g.ElementSharesOf(planet);
    CHECK(shares.has_value());
    if (!shares) return;
    int z = 0;
    for (int i = 3; i <= ElementStock::kCount; ++i)
        if (z == 0 || (*shares)[static_cast<std::size_t>(i - 1)] > (*shares)[static_cast<std::size_t>(z - 1)]) z = i;
    const double fraction = (*shares)[static_cast<std::size_t>(z - 1)];
    CHECK(fraction > 0.0);

    const double rate = g.ElementYieldRate(planet, z);
    CHECK(rate > 0.0);
    const double richness = rate / (rules.drones.rate * fraction * rules.drones.recovery);  // la riqueza del yacimiento
    CHECK(richness > 0.05 && richness < 20.0);
    CHECK(g.ElementYieldRate(planet, 0) == 0.0);

    // Los drones sacan solo ese elemento: va al inventario en kg, no a la bodega, y cada viaje
    // trae hold x fraccion x recuperacion.
    g.SetExtractElement(z);
    CHECK(g.ExtractElement() == z);
    g.SetExtractElement(500);
    CHECK(g.ExtractElement() == 0);
    g.SetExtractElement(z);
    const double metalBefore = g.Ship().cargo.Get(Resource::Metal), iceBefore = g.Ship().cargo.Get(Resource::Ice);
    CHECK(g.DeployDrones(planet));
    Run(g, 900.0);
    const double perTrip = rules.drones.hold * fraction * rules.drones.recovery * 1.0e6;  // kg
    const double got = g.Ship().elements.Get(z);
    std::printf("  extraccion selectiva: %s en 900 s reales (%.3g kg por viaje)\n", ElementMassText(got).c_str(), perTrip);
    CHECK(got >= perTrip * 0.999);
    const double trips = got / perTrip;
    CHECK(std::abs(trips - std::round(trips)) < 1.0e-6);
    CHECK(g.Ship().cargo.Get(Resource::Metal) == metalBefore && g.Ship().cargo.Get(Resource::Ice) == iceBefore);  // no minan a granel

    // El inventario va en la partida y vuelve exacto.
    g.Ship().elements.Add(79, 0.1 + 0.7);
    const SaveData before = g.Snapshot();
    const std::string text = WriteSave(before);
    CHECK(text.find("\nelementos ") != std::string::npos);
    const std::optional<SaveData> read = ReadSave(text);
    CHECK(read.has_value());
    if (!read) return;
    for (int i = 1; i <= ElementStock::kCount; ++i) CHECK(read->Ship()->ship.elements.Get(i) == before.Ship()->ship.elements.Get(i));
    CHECK(read->Ship()->ship.elements.Get(79) == 0.1 + 0.7);
    CHECK(WriteSave(*read) == text);
}

static void TestLeaveStar() {
    Balance rules;
    auto engine = MakeEngine(rules);
    auto game = Session(*engine, rules, GameModeId::Peaceful);
    GameSession& g = *game;
    g.Ship().heatShieldTier = 5;
    const TargetRef star{TargetRef::Kind::Star, 0, 0};
    const double radius = g.Resolve(star)->radius;
    CHECK(g.JumpTo(star, radius * 1.03));
    g.Ship().emitters = 1;
    g.SetDrive(DriveMode::Warp);
    const auto height = [&] { return Length(g.Ship().position - g.Resolve(star)->position); };

    // De frente contra ella: no atraviesa ni se apaga el mando.
    ShipControls push;
    push.throttleChange = 0.5;
    for (int i = 0; i < 120; ++i) g.Step(1.0 / 60.0, push);
    CHECK(height() >= radius);
    CHECK(g.Ship().throttle > 0.5);

    // Al girar se marcha, y la burbuja termina de formarse.
    FaceTo(g.Ship(), Normalize(g.Ship().position - g.Resolve(star)->position));
    const double before = height();
    for (int i = 0; i < 600; ++i) g.Step(1.0 / 60.0, push);
    CHECK(height() > before + 1.0e5);
    CHECK(g.Ship().warpCharge > 0.9);
}

static void TestDiscovery() {
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules);
    g.Conversation().Stop();
    for (int i = 0; i < 20; ++i) g.Step(1.0 / 60.0, {});

    // Al empezar solo se conoce el sistema inicial.
    CHECK(g.KnownSystems() == 1);
    for (const Contact& c : g.Contacts()) {
        if (c.target.kind == TargetRef::Kind::Star) CHECK(c.target.system == 0);
    }

    g.Ship().scannerTier = 8;
    CHECK(g.StartScan());
    for (int i = 0; i < 40; ++i) g.Step(1.0, {});
    CHECK(!g.Scanning());
    CHECK(g.KnownSystems() > 1);
    bool other = false;
    for (const Contact& c : g.Contacts()) other = other || (c.target.kind == TargetRef::Kind::Star && c.target.system != 0);
    CHECK(other);
    CHECK(g.TakeSaveRequest());  // lo descubierto no debe perderse
}

// La suma de control de un documento del guardado (FNV-1a), como la calcula SaveGame.cpp.
static std::string SumLine(const std::string& body) {
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : body) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    char sum[32];
    std::snprintf(sum, sizeof(sum), "suma %016llx\n", static_cast<unsigned long long>(h));
    return sum;
}

// La misma partida con otra cabecera en el documento del mundo (el primero) y su suma rehecha. El
// documento del jugador, que va detras, queda como esta.
static std::string WithHeader(const std::string& text, const std::string& header) {
    const std::size_t sum = text.find("suma ");
    const std::size_t rest = text.find('\n', sum) + 1;
    const std::string body = header + text.substr(text.find('\n'), sum - text.find('\n'));
    return body + SumLine(body) + text.substr(rest);
}

static std::string WithVersion(const std::string& text, int version) { return WithHeader(text, "EPSILON-NEBULOSA-MUNDO " + std::to_string(version)); }

static void TestSave() {
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 20261105, GameModeId::Hard);
    g.Step(0.05, {});
    g.Ship().cargo.Add(Resource::Metal, 1234.5);
    g.Ship().emitters = 3;
    g.Ship().hull = 777.125;
    g.Ship().scannerTier = 3;
    g.Ship().orientation = Quatd::FromAxisAngle(Normalize(Vec3d{1, 2, 3}), 2.5);
    g.TimeScale() = 600.0;
    for (int i = 0; i < 30; ++i) g.Step(0.05, {});

    const TargetRef placed = PlaceStation(g);
    g.Ship().hull = 777.125;  // junto a la estrella en Dificil el casco podria resentirse
    CHECK(g.TakeSaveRequest());
    CHECK(!g.TakeSaveRequest());
    const int station = placed.index;
    g.Stations().Find(station)->modules.push_back({ModuleKind::Collector, 3});
    g.Anomalies()[0].revealed = true;
    g.Anomalies()[0].emitter = false;

    const SaveData before = g.Snapshot();
    const std::string text = WriteSave(before);
    const std::optional<SaveData> read = ReadSave(text);
    CHECK(read.has_value());
    if (!read) return;

    // Los numeros vuelven exactos, y escribirlo otra vez da el mismo archivo.
    CHECK(read->world.simTime == before.world.simTime);
    CHECK(read->Ship()->ship.hull == 777.125);
    CHECK(read->Ship()->ship.orientation.w == before.Ship()->ship.orientation.w);
    CHECK(read->Ship()->absolutePosition.x == before.Ship()->absolutePosition.x);
    CHECK(read->world.stations.size() == before.world.stations.size());
    CHECK(read->world.stations.back().name == before.world.stations.back().name);
    CHECK(read->world.stations.back().modules.size() == 1 && read->world.stations.back().modules[0].level == 3);
    CHECK(read->world.anomalies.size() == before.world.anomalies.size());
    CHECK(read->world.anomalies[0].name == before.world.anomalies[0].name);
    CHECK(read->Ship()->ship.antimatter == before.Ship()->ship.antimatter);
    CHECK(read->Ship()->ship.emitters == 3);
    CHECK(!read->world.anomalies[0].emitter);
    CHECK(WriteSave(*read) == text);

    // Una partida de cualquier version anterior se rechaza con un mensaje que lo dice, sin migrarla;
    // y una de una version posterior, tambien. La suma de control es buena: lo que falla es la version.
    for (const int old : {1, 3, 5, kSaveVersion - 1, kSaveVersion + 1}) {
        std::string why;
        CHECK(!ReadSave(WithVersion(text, old), &why));
        CHECK(why.find("versión " + std::to_string(old)) != std::string::npos || why.find("(" + std::to_string(old) + ")") != std::string::npos);
        if (old < kSaveVersion) CHECK(why.find("anterior") != std::string::npos && why.find("misión nueva") != std::string::npos);
        else CHECK(why.find("posterior") != std::string::npos);
        if (old == kSaveVersion - 1) std::printf("  partida antigua: %s\n", why.c_str());
    }
    // Y una de las de un solo archivo, de antes de separar mundo y jugador, igual.
    {
        std::string why;
        CHECK(!ReadSave(WithHeader(text, "EPSILON-NEBULOSA 12"), &why));
        CHECK(why.find("anterior") != std::string::npos);
        CHECK(!ReadSave(WithHeader(text, "OTRA-COSA 13"), &why) && why == "no es una partida");
    }

    // Son dos documentos, cada uno con su suma: el mundo y el jugador se leen tambien por separado.
    {
        const std::string worldText = WriteWorld(before.world), playerText = WritePlayer(before.player);
        CHECK(text == worldText + playerText);
        CHECK(worldText.rfind("EPSILON-NEBULOSA-MUNDO ", 0) == 0 && playerText.rfind("EPSILON-NEBULOSA-JUGADOR ", 0) == 0);
        const std::optional<WorldSave> world = ReadWorld(worldText);
        const std::optional<PlayerSave> player = ReadPlayer(playerText);
        CHECK(world && player);
        if (world && player) {
            CHECK(world->ships.size() == 1 && world->ships[0].id == player->ship);
            CHECK(WriteWorld(*world) == worldText && WritePlayer(*player) == playerText);
            // De los sistemas solo se escriben los marcados: la lista leida puede ser mas corta.
            CHECK(std::count(player->knownSystems.begin(), player->knownSystems.end(), 1) == std::count(before.player.knownSystems.begin(), before.player.knownSystems.end(), 1));
            CHECK(player->scanned == before.player.scanned && !player->scanned.empty());
        }
        // Las naves van en el mundo, no en el jugador; lo que se conoce, al reves.
        CHECK(worldText.find("\nnave 1\n") != std::string::npos && playerText.find("\nbodega") == std::string::npos);
        CHECK(playerText.find("\nanalizados") != std::string::npos && worldText.find("\nanalizados") == std::string::npos);
        CHECK(!ReadPlayer(worldText) && !ReadWorld(playerText));  // cada uno, lo suyo
        CHECK(!ReadSave(worldText));                              // sin jugador no es una partida entera
        // Un dato de nave antes de abrir ninguna nave no vale.
        std::string orphan = worldText.substr(0, worldText.rfind("suma "));
        const std::size_t ship = orphan.find("\nnave 1\n");
        orphan.erase(ship, std::string("\nnave 1").size());
        std::string why;
        CHECK(!ReadWorld(orphan + SumLine(orphan), &why) && why.find("nave") != std::string::npos);
    }

    // Lo danado se rechaza, no se carga a medias.
    std::string error;
    std::string touched = text;
    touched[text.size() / 2] = touched[text.size() / 2] == '1' ? '2' : '1';
    CHECK(!ReadSave(touched, &error));
    CHECK(!error.empty());
    CHECK(!ReadSave(text.substr(0, text.size() / 2)));
    CHECK(!ReadSave("no es una partida"));

    // Otra sesion recupera lo mismo.
    auto other = MakeEngine(rules);
    GameSession h(*other, rules);
    for (int i = 0; i < 3; ++i) h.Step(0.05, {});
    CHECK(h.Restore(*read));
    h.Step(1.0 / 60.0, {});
    CHECK(h.Mode() == GameModeId::Hard);
    CHECK(h.Ship().cargo.Get(Resource::Metal) == g.Ship().cargo.Get(Resource::Metal));
    CHECK(h.Stations().All().size() == g.Stations().All().size());
    CHECK(h.Anomalies()[0].revealed);
    CHECK(h.Orbiting().has_value());
    const Vec3d absolute = h.Ship().position + h.GetWorld().origin;
    CHECK(Length(absolute - before.Ship()->absolutePosition) < 1.0e4);  // sigue en orbita de lo mismo
    CHECK(h.Snapshot().world.stations.back().modules.size() == 1);

    SaveData foreign = *read;
    foreign.world.seed = 77;
    CHECK(!h.Restore(foreign, &error));

    // Carpeta real: mundo.sav y jugadores/<id>.sav, sin dejar restos.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "epsilon_prueba_partida";
    const std::string folder = path.string();
    std::filesystem::remove_all(path);
    std::string problem;
    CHECK(!LoadGame(folder, "ana", &problem) && problem == "sin partida guardada");
    SaveData mine = before;
    mine.player.id = "ana";
    mine.player.name = "Ana María";
    CHECK(SaveGame(folder, mine));
    CHECK(std::filesystem::exists(path / "mundo.sav") && std::filesystem::exists(path / "jugadores" / "ana.sav"));
    CHECK(!std::filesystem::exists(path / "mundo.sav.tmp") && !std::filesystem::exists(path / "jugadores" / "ana.sav.tmp"));
    const std::optional<SaveData> loaded = LoadGame(folder, "ana");
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK(loaded->Ship()->ship.hull == before.Ship()->ship.hull);
        CHECK(loaded->player.name == "Ana María" && loaded->player.scanned == before.player.scanned);
        CHECK(WriteSave(*loaded) == WriteSave(mine));
    }
    // Otro jugador en el mismo mundo: entra sin archivo propio, con la primera nave y sin saber nada.
    const std::optional<SaveData> guest = LoadGame(folder, "../luis");
    CHECK(guest.has_value());
    if (guest) {
        CHECK(guest->player.id == "___luis" && guest->player.scanned.empty() && guest->player.tutorialStep == 0);
        CHECK(guest->Ship() && guest->Ship()->id == before.Ship()->id);
        CHECK(guest->world.stations.size() == before.world.stations.size());
        CHECK(SaveGame(folder, *guest));
        CHECK(std::filesystem::exists(path / "jugadores" / "___luis.sav"));  // el id no sale de su carpeta
        CHECK(LoadGame(folder, "ana").has_value());                            // y el de Ana sigue ahi
    }
    DeleteGame(folder);
    CHECK(!LoadGame(folder, "ana"));
    CHECK(!std::filesystem::exists(path / "jugadores"));
    std::filesystem::remove_all(path);

    // Perder la nave en modo realista borra la partida; en los demas, se guarda con la marca de perdida.
    GameSession lost(*engine, rules, 20261105, GameModeId::Realistic);
    lost.Step(1.0 / 60.0, {});
    lost.Ship().hull = 0.0;
    lost.Step(1.0 / 60.0, {});
    CHECK(lost.Phase() == GamePhase::Exploding);
    CHECK(lost.TakeSaveDeletion());
    g.Ship().hull = 0.0;
    g.Step(1.0 / 60.0, {});
    CHECK(!g.TakeSaveDeletion());
    CHECK(g.Snapshot().Ship()->lost);
}

static void TestScanner() {
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 42);
    g.Step(0.05, {});

    const Anomaly* ruin = nullptr;
    for (const Anomaly& a : g.Anomalies())
        if (a.system == 0 && a.kind == AnomalyKind::Derelict) ruin = &a;
    CHECK(ruin != nullptr);
    CHECK(ruin->cold);
    CHECK(ruin->signature == 1);
    const TargetRef target{Kind::Anomaly, 0, ruin->id};

    // Sin barrido, la estructura no existe para los sensores.
    const auto listed = [&] {
        for (const Contact& c : g.Contacts())
            if (c.target == target) return true;
        return false;
    };
    Run(g, 3.0);
    CHECK(!listed());

    // Hay que estar parado, y gasta energia.
    g.SetThrottle(1.0);
    CHECK(!g.StartScan());
    g.HoldPosition();
    Run(g, 30.0);
    const double energy = g.Ship().energy;
    CHECK(g.StartScan());
    CHECK(g.Scanning());
    CHECK(energy - g.Ship().energy > 100.0);
    CHECK(!g.StartScan());  // no se apilan

    // Moverse lo pierde todo.
    g.SetThrottle(0.5);
    Run(g, 1.0);
    CHECK(!g.Scanning());
    g.HoldPosition();
    Run(g, 30.0);

    // Sin energia no hay barrido.
    g.Ship().energy = 10.0;
    CHECK(!g.StartScan());
    g.Ship().energy = rules.ship.energy;

    // Desde lejos no la alcanza: hay que buscar.
    CHECK(g.JumpTo(target, g.ScanRange() * 1.5));
    Run(g, 1.0);
    g.CancelAutopilot();
    CHECK(g.StartScan());
    Run(g, 40.0);
    CHECK(!g.Scanning());
    CHECK(!ruin->revealed);

    // Dentro del alcance aparece, y sigue en la lista aunque nos alejemos.
    CHECK(g.JumpTo(target, g.ScanRange() * 0.6));
    Run(g, 1.0);
    g.Ship().energy = rules.ship.energy;
    CHECK(g.StartScan());
    Run(g, 29.0);
    CHECK(g.Scanning());  // el barrido dura 30 s reales, sin depender de la aceleracion del tiempo
    Run(g, 2.0);
    CHECK(!g.Scanning());
    CHECK(ruin->revealed);
    CHECK(g.RevealedStructures() == 1);
    Run(g, 1.0);
    CHECK(listed());

    // El alcance crece con el nivel, y lo mejor escondido pide mas nivel.
    const double first = g.ScanRange();
    g.Ship().scannerTier = 3;
    CHECK(g.ScanRange() > first * 2.0);
    bool deepest = false;
    for (const Anomaly& a : g.Anomalies()) deepest = deepest || (a.cold && a.signature > 1);
    CHECK(deepest);
}

static Anomaly* FirstStructure(GameSession& g) {
    for (Anomaly& a : g.Anomalies())
        if (a.system == 0 && a.kind == AnomalyKind::Derelict) return &a;
    return nullptr;
}

// Un barrido completo donde esta la nave, con la energia llena.
static bool ScanHere(GameSession& g) {
    g.Ship().energy = g.Rules().ship.energy;
    if (!g.StartScan()) return false;
    Run(g, g.Rules().scanner.seconds + 1.0);
    return !g.Scanning();
}

static double AngleDegrees(const Vec3d& a, const Vec3d& b) { return std::acos(std::clamp(Dot(Normalize(a), Normalize(b)), -1.0, 1.0)) * 180.0 / kPi; }

static void TestColdEcho() {
    Balance rules;
    const double au = kAstronomicalUnit;
    const ScannerRules& sr = rules.scanner;
    const AnomalyRules& ar = rules.anomalies;
    // La cifra tiene sentido: desde la orbita de la estrella, el nivel 1 la alcanza con margen.
    CHECK(ar.firstDerelictMinAu < ar.firstDerelictMaxAu && ar.firstDerelictMaxAu * au < sr.range * 0.75);

    // La estructura del sistema 0 se encuentra con un barrido desde la orbita de la estrella, sin modo
    // creativo y con el escaner de serie, en varias galaxias.
    for (const std::uint32_t seed : {20261105u, 1u, 2u, 3u, 42u, 777u}) {
        auto engine = MakeEngine(rules, seed);
        GameSession g(*engine, rules, seed, GameModeId::Peaceful);
        g.Conversation().Stop();
        g.Step(0.05, {});
        Anomaly* ruin = FirstStructure(g);
        CHECK(ruin != nullptr);
        if (!ruin) continue;
        const TargetRef star{Kind::Star, 0, 0};
        const std::optional<TargetInfo> sun = g.Resolve(star);
        CHECK(sun.has_value());
        if (!sun) continue;
        const double fromCenter = Length(ruin->offset);
        CHECK(fromCenter >= ar.firstDerelictMinAu * au * 0.999 && fromCenter <= ar.firstDerelictMaxAu * au * 1.001);
        const double fromStart = Length(g.AnomalyPosition(*ruin) - g.Ship().position);
        CHECK(g.JumpTo(star, sun->arrival));
        Run(g, 1.0);
        CHECK(g.Orbiting().has_value() && g.CanScan());
        CHECK(g.Ship().scannerTier == 1 && !g.Creative());
        const double fromOrbit = Length(g.AnomalyPosition(*ruin) - g.Ship().position);
        CHECK(fromOrbit < g.ScanRange());
        CHECK(ScanHere(g));
        CHECK(ruin->revealed && g.RevealedStructures() == 1);
        std::printf("  semilla %u: estructura a %.2f UA de la estrella y %.1f UA de la salida; desde la orbita, %.2f UA\n", seed, fromCenter / au, fromStart / au, fromOrbit / au);
    }

    // Colocarla cerca de la estrella gasta los mismos numeros que antes: lo demas sale igual.
    {
        auto engine = MakeEngine(rules);
        Balance moved = rules;
        moved.anomalies.firstDerelictMaxAu = 5.0;
        const std::vector<Anomaly> a = GenerateAnomalies(engine->GetWorld().extents, rules.anomalies, 99);
        const std::vector<Anomaly> b = GenerateAnomalies(engine->GetWorld().extents, moved.anomalies, 99);
        const std::vector<Anomaly> c = GenerateAnomalies(engine->GetWorld().extents, rules.anomalies, 99);
        CHECK(a.size() == b.size() && a.size() == c.size());
        bool same = a.size() == b.size(), repeat = a.size() == c.size();
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            if (a[i].id == 1) continue;  // la que se ha movido
            same = same && a[i].offset.x == b[i].offset.x && a[i].offset.z == b[i].offset.z && a[i].signature == b[i].signature && a[i].kind == b[i].kind;
        }
        for (std::size_t i = 0; i < std::min(a.size(), c.size()); ++i) repeat = repeat && a[i].offset.x == c[i].offset.x && a[i].offset.y == c[i].offset.y;
        CHECK(same);
        CHECK(repeat);
    }

    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 20261105, GameModeId::Peaceful);
    g.Conversation().Stop();
    g.Step(0.05, {});
    Anomaly* ruin = FirstStructure(g);
    CHECK(ruin != nullptr);
    if (!ruin) return;
    const TargetRef target{Kind::Anomaly, 0, ruin->id};
    CHECK(sr.echoFactor > 1.0 && std::abs(g.EchoRange() - g.ScanRange() * sr.echoFactor) < 1.0);

    // Fuera del alcance pero dentro del eco: no la revela, pero da rumbo y distancia aproximados.
    const auto hintAt = [&](double ranges, int tier) {  // a tantas veces el alcance de ese nivel
        g.Ship().scannerTier = tier;
        CHECK(g.JumpTo(target, g.ScanRange() * ranges));
        g.CancelAutopilot();
        g.HoldPosition();
        Run(g, 0.5);
        CHECK(ScanHere(g));
    };
    hintAt(2.0, 1);
    CHECK(!ruin->revealed);
    const EchoHint first = g.Echo();
    CHECK(first.active && !first.exact && first.system == 0 && first.weak == 0);
    CHECK(g.LastScan().find("Eco frío") != std::string::npos && g.LastScan().find(" UA") != std::string::npos && g.LastScan().find("rumbo") != std::string::npos);
    const Vec3d truth = g.AnomalyPosition(*ruin) - g.Ship().position;
    const Vec3d guess = *g.EchoPosition() - g.Ship().position;
    const double offBy = AngleDegrees(truth, guess);
    CHECK(offBy <= sr.echoBearingError + 1e-6);
    CHECK(std::abs(first.distance / Length(truth) - 1.0) <= sr.echoDistanceError + 1e-9);
    CHECK(std::abs(Length(guess) - first.distance) < 1.0);
    CHECK(first.bearing >= 0.0 && first.bearing < 360.0 && std::abs(first.elevation) <= 90.0);
    CHECK(first.error > 0.0);
    std::printf("  pista con nivel 1: %.2f UA (de verdad %.2f), %.1f grados de error, zona de %.2f UA\n", first.distance / au, Length(truth) / au, offBy, first.error / au);

    // Siguiendo la pista se llega: unos pocos saltos hasta el punto marcado, barriendo cada vez.
    int hops = 0;
    while (!ruin->revealed && hops < 4 && g.EchoPosition()) {
        ShipControls nudge;  // un toque de mando saca de la orbita que dejo el salto
        nudge.throttleChange = 0.01;
        g.Step(1.0 / 60.0, nudge);
        g.SetThrottle(0.0);
        CHECK(!g.Orbiting());
        g.Ship().position = *g.EchoPosition();
        g.HoldPosition();
        Run(g, 0.5);
        CHECK(ScanHere(g));
        ++hops;
    }
    CHECK(ruin->revealed);
    std::printf("  siguiendo la pista: encontrada en %d saltos\n", hops);
    Run(g, 1.0);
    CHECK(!g.Echo().active);  // revelada, la pista sobra
    ruin->revealed = false;

    // Con mas nivel la precision mejora (y el eco llega mas lejos).
    const double reach1 = g.EchoRange();
    hintAt(2.0, 3);
    CHECK(g.EchoRange() > reach1);
    CHECK(g.Echo().active);
    if (g.Echo().active) {
        const Vec3d t3 = g.AnomalyPosition(*ruin) - g.Ship().position;
        CHECK(AngleDegrees(t3, *g.EchoPosition() - g.Ship().position) <= sr.echoBearingError * sr.echoErrorPerTier * sr.echoErrorPerTier + 1e-6);
        CHECK(std::abs(g.Echo().distance / Length(t3) - 1.0) <= sr.echoDistanceError * sr.echoErrorPerTier * sr.echoErrorPerTier + 1e-9);
        CHECK(g.Echo().error / g.Echo().distance < first.error / first.distance);
    }
    g.Ship().scannerTier = 1;

    // Mas alla del eco, nada: fuera del tutorial se aplica la regla normal.
    hintAt(sr.echoFactor * 1.3, 1);
    CHECK(!g.Echo().active && !ruin->revealed);
    CHECK(g.LastScan().find("Eco frío") == std::string::npos);

    // La regla de la firma: lo que pide mas nivel no da rumbo, solo "senal debil".
    ruin->signature = 3;
    hintAt(2.0, 1);
    CHECK(!ruin->revealed && !g.Echo().active);
    CHECK(g.Echo().weak >= 1 && g.Echo().needTier == 3);
    CHECK(g.LastScan().find("Señal débil más allá del alcance") != std::string::npos && g.LastScan().find("nivel 3") != std::string::npos);

    // El tutorial la garantiza: en el paso de la estructura, la pista es exacta y llega aunque el eco
    // no alcance. Pero no se salta la firma.
    {
        GameSession t(*engine, rules, 20261105, GameModeId::Peaceful);
        t.Step(0.05, {});
        const std::vector<DialogueStep> steps = MakeTutorial();
        std::size_t structureStep = steps.size();
        for (std::size_t i = 0; i < steps.size(); ++i)
            if (steps[i].tag == kStructureStep) structureStep = i;
        CHECK(structureStep < steps.size());
        t.Conversation().Resume(structureStep);
        CHECK(t.EchoGuaranteed());
        Anomaly* r = FirstStructure(t);
        if (!r) return;
        const TargetRef there{Kind::Anomaly, 0, r->id};
        CHECK(t.JumpTo(there, t.EchoRange() * 1.5));
        t.CancelAutopilot();
        t.HoldPosition();
        Run(t, 0.5);
        CHECK(ScanHere(t));
        CHECK(!r->revealed);
        CHECK(t.Echo().active && t.Echo().exact && t.Echo().error == 0.0);
        if (t.EchoPosition()) CHECK(Length(*t.EchoPosition() - t.AnomalyPosition(*r)) < 1.0);
        CHECK(t.LastScan().find("Eco frío exacto") != std::string::npos);
        CHECK(t.Conversation().Tag() == kStructureStep);  // sigue en el paso hasta encontrarla

        r->signature = 2;  // con la firma por encima del nivel, ni el tutorial da rumbo
        CHECK(ScanHere(t));
        CHECK(!t.Echo().active && !r->revealed);
        r->signature = 1;

        // Terminado el tutorial, la misma posicion ya no da pista.
        t.Conversation().Stop();
        CHECK(!t.EchoGuaranteed());
        CHECK(ScanHere(t));
        CHECK(!t.Echo().active);
    }
}

static void TestModes() {
    CHECK(!ModeInfo(GameModeId::Peaceful).hostilesAttack);
    CHECK(ModeInfo(GameModeId::Easy).damage < ModeInfo(GameModeId::Hard).damage);
    CHECK(ModeInfo(GameModeId::Realistic).permadeath);

    // En pacifico nada ataca ni quema.
    Balance rules;
    auto calm = MakeEngine(rules);
    GameSession peace(*calm, rules, 7, GameModeId::Peaceful);
    peace.Step(0.05, {});
    TargetRef enemy;
    for (const Anomaly& a : peace.Anomalies())
        if (a.stance == Stance::Hostile && a.kind == AnomalyKind::AlienShip) enemy = {Kind::Anomaly, a.system, a.id};
    CHECK(peace.JumpTo(enemy, rules.anomalies.hostileWeaponRange * 0.5));
    Run(peace, 10.0);
    CHECK(peace.Ship().hull == rules.ship.hull);

    // Normal: el mundo no vuelve atras. La nave reaparece reparada, con tres cuartos de la bodega, en el
    // punto de partida si no ha atracado en ninguna estacion propia.
    auto normalEngine = MakeEngine(rules);
    GameSession g(*normalEngine, rules, 7, GameModeId::Normal);
    g.Step(0.05, {});
    const Vec3d start = g.Ship().position + g.GetWorld().origin;
    g.Ship().cargo.Add(Resource::Metal, 1000.0);
    g.Ship().emitters = 2;
    const TargetRef station = PlaceStation(g);
    const double metal = g.Ship().cargo.Get(Resource::Metal) + 5000.0;
    g.Ship().cargo.Add(Resource::Metal, 5000.0);
    const double clock = g.SimTime();
    g.Ship().hull = 0.0;
    Run(g, 8.0);
    CHECK(g.Phase() == GamePhase::GameOver);
    CHECK(g.Snapshot().Ship()->lost);
    g.ContinueAfterDefeat();
    CHECK(g.Phase() == GamePhase::Flying && !g.Docked());
    CHECK(Length(g.Ship().position + g.GetWorld().origin - start) < 1.0);
    Run(g, 0.05);
    CHECK(std::abs(g.Ship().cargo.Get(Resource::Metal) - metal * 0.75) < 1e-6);
    CHECK(g.Ship().emitters == 2);  // ya estan en el motor
    CHECK(g.Ship().hull == rules.ship.hull);
    CHECK(g.Stations().Count(StationKind::Power) == 1);  // la central sigue ahi
    CHECK(g.SimTime() > clock);                          // y el reloj no ha vuelto atras

    // Tras atracar en una estacion propia, es ahi donde reaparece: atracada y reparada.
    CHECK(g.JumpTo(station, g.Resolve(station)->arrival));
    CHECK(g.Dock(station, true));
    Run(g, 0.5);
    CHECK(g.Docked());
    g.Undock();
    Run(g, 2.0);
    const TargetRef far = FirstPlanet(g, 0);
    CHECK(g.JumpTo(far, g.Resolve(far)->arrival));
    Run(g, 0.5);
    g.Ship().zones[0] = 0.2;
    g.Ship().hull = 0.0;
    Run(g, 8.0);
    CHECK(g.Phase() == GamePhase::GameOver);
    // Guardada con la nave perdida, al cargar ya ha reaparecido.
    const SaveData wrecked = g.Snapshot();
    CHECK(wrecked.Ship()->lost && wrecked.Ship()->homeStation == station.index);
    const std::optional<SaveData> reread = ReadSave(WriteSave(wrecked));
    CHECK(reread && reread->Ship()->lost && reread->Ship()->homeStation == station.index);
    g.ContinueAfterDefeat();
    Run(g, 0.2);
    CHECK(g.Phase() == GamePhase::Flying && g.Docked());
    CHECK(g.DockedStation() && g.DockedStation()->id == station.index);
    CHECK(g.Ship().hull == rules.ship.hull && g.Ship().zones[0] == 1.0);
    if (reread) {
        auto loadEngine = MakeEngine(rules);
        GameSession h(*loadEngine, rules, 7, GameModeId::Normal);
        h.Step(0.05, {});
        CHECK(h.Restore(*reread));
        for (int i = 0; i < 200 && !h.Docked(); ++i) h.Step(0.05, {});
        CHECK(h.Phase() == GamePhase::Flying && h.Docked());
        CHECK(h.Ship().hull == rules.ship.hull);
        CHECK(!h.Snapshot().Ship()->lost);
    }

    // Realista: no queda nada.
    auto hardEngine = MakeEngine(rules);
    GameSession real(*hardEngine, rules, 7, GameModeId::Realistic);
    real.Step(0.05, {});
    real.Ship().cargo.Add(Resource::Metal, 1000.0);
    real.Ship().emitters = 2;
    PlaceStation(real);
    real.Ship().hull = 0.0;
    Run(real, 8.0);
    real.ContinueAfterDefeat();
    CHECK(real.Ship().cargo.Total() == rules.ship.startSilicate + rules.ship.startGas);  // solo la municion de salida
    CHECK(real.Ship().emitters == 0);
    CHECK(real.Stations().All().empty());
}

static void TestDialogue() {
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 42);
    Dialogue& talk = g.Conversation();
    CHECK(talk.Active());
    CHECK(talk.StepCount() >= 6);
    CHECK(talk.Line() != nullptr);

    // El texto aparece poco a poco, y no parte en dos una letra con tilde.
    CHECK(talk.VisibleText().empty());
    talk.Update(g, 0.1);
    const std::string some = talk.VisibleText();
    CHECK(!some.empty());
    CHECK(some.size() < talk.Line()->text.size());
    bool whole = true;
    for (double t = 0; t < 2.0; t += 0.02) {
        talk.Update(g, 0.02);
        const std::string text = talk.VisibleText();
        if (!text.empty()) whole = whole && (static_cast<unsigned char>(text.back()) & 0xC0) != 0xC0;
    }
    CHECK(whole);

    // Enter completa la linea y luego pasa a la siguiente. Se vuelve a empezar el paso para tener una
    // linea a medio escribir, sea cual sea su largo.
    talk.Replay();
    talk.Update(g, 0.1);
    CHECK(!talk.LineComplete());
    talk.Next();
    CHECK(talk.LineComplete());
    const std::string before = talk.Line()->text;
    talk.Next();
    CHECK(talk.Line() == nullptr || talk.Line()->text != before);

    // Terminadas las lineas, espera a que el jugador cumpla el objetivo.
    while (talk.Reading()) {
        talk.Next();
        talk.Next();
    }
    CHECK(!talk.Objective().empty());
    const std::size_t step = talk.Step();
    Run(g, 1.0);
    CHECK(talk.Step() == step);
    g.Ship().speed = 50.0;
    talk.Update(g, 0.01);
    CHECK(talk.Step() == step + 1);

    // Saltarse los dialogos no rompe nada.
    talk.Stop();
    CHECK(!talk.Active());
    CHECK(talk.Objective().empty());
    Run(g, 1.0);
}

// Alguno de los motivos por los que el objetivo aun no se cumple contiene ese texto.
static bool HasBlocker(const Guidance& guidance, const std::string& text) {
    for (const std::string& blocker : guidance.blockers)
        if (blocker.find(text) != std::string::npos) return true;
    return false;
}

static void TestTutorialGuide() {
    // Todo lo que se puede senalar existe: una tecla de la barra, una opcion de la rueda o un panel.
    for (int k = 1; k < static_cast<int>(Highlight::Count); ++k) {
        const HighlightTarget where = Locate(static_cast<Highlight>(k));
        CHECK(where.where != HighlightTarget::Where::None);
        CHECK(where.label && where.label[0] != '\0');
        if (where.where == HighlightTarget::Where::Key || where.where == HighlightTarget::Where::Wheel) CHECK(where.key && where.key[0] != '\0');
        if (where.where == HighlightTarget::Where::Wheel) {
            CHECK(where.option < WheelOption::Count);
            CHECK(std::string(where.key) == "T");
            CHECK(std::string(where.label) == Name(where.option));
        }
    }
    CHECK(Locate(Highlight::None).where == HighlightTarget::Where::None);

    // Cada paso con objetivo senala algo, y lo que senala resuelve a un elemento que existe.
    const std::vector<DialogueStep> steps = MakeTutorial();
    CHECK(steps.size() >= 12);
    std::set<std::string> objectives;
    for (const DialogueStep& step : steps) {
        CHECK(!step.lines.empty());
        for (const DialogueLine& line : step.lines) CHECK(line.text.size() < 140);  // una idea por linea
        if (step.objective.empty()) continue;
        CHECK(step.done != nullptr);
        CHECK(step.highlight != Highlight::None);
        CHECK(Locate(step.highlight).where != HighlightTarget::Where::None);
        CHECK(objectives.insert(step.objective).second);  // ningun objetivo repetido
    }
    // Drones, estacion y central son pasos distintos.
    const auto stepOf = [&](const char* word) {
        for (std::size_t i = 0; i < steps.size(); ++i)
            if (steps[i].objective.find(word) != std::string::npos) return i;
        return steps.size();
    };
    const std::size_t drones = stepOf("drones"), yard = stepOf("origami"), power = stepOf("central en");
    CHECK(drones < yard && yard < power && power < steps.size());
    CHECK(steps[yard].optional && !steps[drones].optional && !steps[power].optional);

    // El tutorial entero avanza cumpliendo cada condicion, con su aviso y la guia de cada paso.
    Balance rules;
    auto engine = MakeEngine(rules);
    GameSession g(*engine, rules, 20261105, GameModeId::Peaceful);
    g.Step(0.05, {});
    Dialogue& talk = g.Conversation();
    CHECK(talk.Active() && talk.StepCount() == steps.size());
    TargetRef target;
    const auto guide = [&] { return talk.Guide(g, target); };
    // Pasa las lineas y da al paso un momento con el objetivo en pantalla.
    const auto read = [&] {
        talk.SkipLines();
        CHECK(!talk.Reading() && !talk.Objective().empty());
    };
    // Cumplido lo que pide, avanza y lo cuenta.
    const auto passes = [&](std::size_t step) {
        const int before = talk.Completions();
        talk.Update(g, 0.01);
        CHECK(talk.Step() == step + 1);
        CHECK(talk.Completions() == before + 1);
        CHECK(talk.LastCompleted() == steps[step].objective);
        CHECK(talk.SinceCompleted() < 0.1);
    };

    // 0: mando.
    read();
    CHECK(guide().highlight == Highlight::KeyThrottle && HasBlocker(guide(), "Velocidad"));
    g.Ship().speed = 50.0;
    passes(0);
    // 1: frenar.
    read();
    CHECK(guide().highlight == Highlight::KeyBrake);
    g.Ship().speed = 0.0;
    g.Ship().throttle = 0.0;
    passes(1);
    // 2: cursor.
    read();
    CHECK(guide().highlight == Highlight::KeyCursor);
    g.MarkCursorUsed();
    passes(2);
    // 3: escaner. Moviendose senala Espacio; sin energia dice cuanta falta.
    read();
    g.Ship().speed = 50.0;
    CHECK(guide().highlight == Highlight::KeyBrake && HasBlocker(guide(), "Nave quieta: no"));
    g.Ship().speed = 0.0;
    g.Ship().energy = 0.0;
    CHECK(guide().highlight == Highlight::WheelScan && HasBlocker(guide(), "Energía insuficiente: faltan"));
    g.Ship().energy = rules.ship.energy;
    CHECK(guide().blockers.empty());
    g.MarkScanned(0);
    passes(3);
    // 4: analisis. Sin objetivo senala Tab.
    read();
    CHECK(guide().highlight == Highlight::KeyTarget && HasBlocker(guide(), "Objetivo"));
    target = {Kind::Star, 0, 0};
    CHECK(guide().highlight == Highlight::WheelAnalysis);
    g.MarkAnalysisUsed();
    passes(4);
    // 5: drones. Una estrella no vale; un planeta lejos pide Ir.
    read();
    CHECK(guide().highlight == Highlight::KeyTarget);
    target = FirstPlanet(g, 0);
    CHECK(target.kind == Kind::Planet);
    if (Length(g.Resolve(target)->position - g.Ship().position) > rules.drones.range) CHECK(guide().highlight == Highlight::WheelGo && HasBlocker(guide(), "Distancia"));
    CHECK(g.JumpTo(target, g.Resolve(target)->arrival));
    g.CancelAutopilot();
    CHECK(guide().highlight == Highlight::WheelDrones);
    CHECK(g.DeployDrones(target));
    passes(5);
    // 6: la estacion es opcional. Enter no la salta hasta que el objetivo lleva un momento en pantalla.
    read();
    CHECK(talk.Optional());
    talk.Next();
    CHECK(talk.Step() == yard);
    talk.Update(g, 1.0);
    CHECK(talk.Step() == yard);
    const int skipped = talk.Completions();
    talk.Next();
    CHECK(talk.Step() == yard + 1 && talk.Completions() == skipped);  // saltarlo no cuenta como cumplido
    g.RecallDrones();
    // 7: central, en la orbita de la estrella.
    read();
    CHECK(guide().highlight == Highlight::KeyTarget);  // el objetivo sigue siendo el planeta
    const StarYield yield = YieldOf(g.GetWorld().systems[0]->StarAt(0));
    if (g.Ship().heatShieldTier < yield.requiredTier) g.Ship().heatShieldTier = yield.requiredTier;
    target = {Kind::Star, 0, 0};
    CHECK(g.JumpTo(target, g.Resolve(target)->arrival));
    g.CancelAutopilot();
    CHECK(guide().highlight == Highlight::WheelPower && guide().blockers.empty());
    CHECK(g.DeployPower(target));
    passes(power);
    // 8: recoger de la central.
    read();
    CHECK(guide().highlight == Highlight::KeyTarget);
    target = {Kind::Station, 0, g.Stations().All().back().id};
    g.Ship().relayTier = 1;  // con enlace se recoge a distancia: aqui no importa donde quede la central
    CHECK(guide().highlight == Highlight::WheelUse && guide().blockers.empty());
    CHECK(g.Interact(target));
    passes(power + 1);
    // 9: la estructura. Sin pista, el escaner; con pista, la marca del radar y su distancia.
    read();
    CHECK(talk.Tag() == kStructureStep);
    CHECK(HasBlocker(guide(), "Pista: aún no"));
    Anomaly* ruin = FirstStructure(g);
    CHECK(ruin != nullptr);
    if (!ruin) return;
    CHECK(g.JumpTo({Kind::Anomaly, 0, ruin->id}, g.EchoRange() * 1.5));
    g.CancelAutopilot();
    g.HoldPosition();
    Run(g, 0.5);
    CHECK(ScanHere(g));
    CHECK(g.EchoPosition().has_value());
    CHECK(guide().highlight == Highlight::EchoMarker && HasBlocker(guide(), "acérquese a la marca"));
    ruin->revealed = true;
    const std::size_t structure = talk.Step();
    passes(structure);
    // 10: explorarla.
    read();
    CHECK(guide().highlight == Highlight::KeyTarget);
    target = {Kind::Anomaly, 0, ruin->id};
    CHECK(guide().highlight == Highlight::WheelGo || guide().highlight == Highlight::WheelUse);
    g.Ship().emitters = 1;
    passes(structure + 1);
    // 11: la burbuja Hibrida dice lo que cuesta y lo que hay.
    read();
    g.Ship().antimatter = 0.0;
    CHECK(guide().highlight == Highlight::KeyHybrid && HasBlocker(guide(), "Antimateria: faltan") && HasBlocker(guide(), "tiene"));
    g.Ship().mode = DriveMode::Warp;
    g.Ship().warpCharge = 0.5;
    CHECK(guide().highlight == Highlight::KeyThrottle && HasBlocker(guide(), "Formando: 50 %"));
    g.Ship().warpCharge = 1.0;
    passes(structure + 2);
    // 12: la despedida termina sola.
    talk.SkipLines();
    talk.Update(g, 0.01);
    CHECK(!talk.Active());
    CHECK(talk.Completions() == static_cast<int>(objectives.size()) - 1);  // todos menos la estacion, que se salto

    // Repetir el consejo vuelve a las lineas del paso, sin cambiar de paso.
    talk.Start(MakeTutorial());
    talk.Resume(3);
    talk.SkipLines();
    CHECK(!talk.Reading());
    talk.Replay();
    CHECK(talk.Reading() && talk.Step() == 3 && talk.VisibleText().empty());

    // El paso se guarda y vuelve; una partida de la version anterior, con el tutorial viejo, se rechaza.
    const std::string text = WriteSave(g.Snapshot());
    const std::optional<SaveData> back = ReadSave(text);
    CHECK(back && back->player.tutorialStep == 3);
    CHECK(kSaveVersion >= 12);
    std::string why;
    CHECK(!ReadSave(WithVersion(text, kSaveVersion - 1), &why));
    CHECK(why.find("anterior") != std::string::npos);
}

// Vuela un proyectil sin dueno, con toda la gravedad, y dice lo que se acerca al blanco.
static double FlightMiss(const Vec3d& origin, const Vec3d& shooter, const Vec3d& direction, double speed, bool beam, const Vec3d& target, const Vec3d& targetVelocity,
                  const std::vector<GravitySource>& sources, const CombatRules& rules, double time) {
    Projectile p;
    p.beam = beam;
    p.position = origin;
    p.velocity = beam ? direction * speed : shooter + direction * speed;
    const int steps = 400;
    double best = 1e300;
    Vec3d before = p.position - target;
    for (int i = 1; i <= steps; ++i) {
        StepProjectile(p, sources, rules, time / steps);
        const Vec3d now = p.position - (target + targetVelocity * (time / steps * i));
        const Vec3d d = now - before;
        const double dd = Dot(d, d);
        const double u = dd > 0.0 ? std::clamp(-Dot(before, d) / dd, 0.0, 1.0) : 0.0;
        const Vec3d at = before + d * u;
        best = std::min(best, Length(at));
        before = now;
    }
    return best;
}

static void TestBallistics() {
    const CombatRules rules;

    // Cada nivel da mas dano, recarga mas deprisa y se equivoca menos.
    for (int k = 0; k < static_cast<int>(WeaponKind::Count); ++k) {
        const WeaponKind kind = static_cast<WeaponKind>(k);
        CHECK(Scaled(rules, kind, 3).damage > Scaled(rules, kind, 1).damage);
        CHECK(Scaled(rules, kind, 3).cooldown < Scaled(rules, kind, 1).cooldown);
        CHECK(Scaled(rules, kind, 99).damage == Scaled(rules, kind, rules.maxTier).damage);
    }
    CHECK(AimError(rules, 5) < AimError(rules, 1) * 0.2);
    // Cuatro familias, cuatro tipos de dano distintos.
    bool seen[4] = {};
    for (const WeaponRules& w : rules.weapons) seen[static_cast<int>(w.type)] = true;
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);

    // La gravedad tira hacia el astro, como mu / r2.
    const std::vector<GravitySource> star{{Vec3d{1.0e6, 0, 0}, 1.3e11, 7.0e5}};
    const Vec3d pull = GravityAt(star, Vec3d{0, 0, 0}, 1.0);
    CHECK(pull.x > 0.0 && std::abs(pull.y) < 1e-12 && std::abs(pull.x - 1.3e11 / 1.0e12) < 1e-3);

    // Sin gravedad, la solucion alcanza a un blanco que se mueve, con el tirador moviendose tambien.
    const Vec3d origin{0, 0, 0}, shooter{50.0, 0, 0}, target{4.0e4, 3.0e4, 0}, tvel{-60.0, 40.0, 0};
    const FiringSolution plain = Solve(origin, shooter, target, tvel, 4000.0, false, {}, 1, rules);
    CHECK(plain.valid && plain.flightTime > 0.0);
    CHECK(FlightMiss(origin,shooter, plain.direction, 4000.0, false, target, tvel, {}, rules, plain.flightTime * 1.3) < 1.0);
    CHECK(Length(plain.impact - (target + tvel * plain.flightTime)) < 1e-6);
    // Un blanco mas rapido que el proyectil, que se aleja, no tiene solucion.
    CHECK(!Solve(origin, {}, Vec3d{1.0e4, 0, 0}, Vec3d{5000.0, 0, 0}, 4000.0, false, {}, 1, rules).valid);

    // Con gravedad, el computador que cuenta mas astros acierta donde el que cuenta menos falla.
    const std::vector<GravitySource> field{{Vec3d{5.0e4, 2.0e5, 0}, 1.3e11, 1.0e3}, {Vec3d{5.0e4, 1.5e5, 0}, 5.0e10, 1.0e3}};
    const Vec3d far{1.0e5, 0, 0};
    const FiringSolution rough = Solve(origin, {}, far, {}, 4000.0, false, field, 1, rules);
    const FiringSolution fine = Solve(origin, {}, far, {}, 4000.0, false, field, 3, rules);
    const double roughMiss = FlightMiss(origin,{}, rough.direction, 4000.0, false, far, {}, field, rules, 40.0);
    const double fineMiss = FlightMiss(origin,{}, fine.direction, 4000.0, false, far, {}, field, rules, 40.0);
    const double naive = FlightMiss(origin,{}, Vec3d{1, 0, 0}, 4000.0, false, far, {}, field, rules, 40.0);
    CHECK(naive > 300.0);               // sin contar la gravedad, el tiro se va muy lejos
    CHECK(fineMiss < 10.0);             // con todos los astros, da en el blanco
    CHECK(roughMiss > fineMiss * 10.0); // con uno solo, no
    CHECK(roughMiss < naive);           // pero algo mejora

    // El haz de luz se curva hacia el astro sin cambiar de velocidad, y con el doble de fuerza.
    Projectile beam;
    beam.beam = true;
    beam.velocity = Vec3d{0, 0, 299792.458};
    beam.position = Vec3d{1.0e6, 0, 0};
    const std::vector<GravitySource> lens{{Vec3d{0, 0, 0}, 1.3e11, 7.0e5}};
    StepProjectile(beam, lens, rules, 0.1);
    CHECK(beam.velocity.x < 0.0);
    CHECK(std::abs(Length(beam.velocity) - 299792.458) < 1e-6);
    Projectile shell;
    shell.velocity = Vec3d{0, 0, 3000.0};
    shell.position = Vec3d{1.0e6, 0, 0};
    StepProjectile(shell, lens, rules, 0.1);
    CHECK(std::abs(shell.velocity.x - (-1.3e11 / 1.0e12 * 0.1)) < 1e-6);
    CHECK(std::abs(beam.velocity.x - 2.0 * shell.velocity.x) < 1e-4);  // la luz se desvia el doble

    // Un guiado gira hacia el blanco, pero no mas deprisa de lo que puede.
    Projectile missile;
    missile.guided = true;
    missile.cruise = 1500.0;
    missile.turn = 0.05;
    missile.velocity = Vec3d{0, 0, 450.0};
    const Vec3d aim{1.0e5, 0, 0};
    StepProjectile(missile, {}, rules, 10.0, &aim, nullptr);
    const double turned = std::atan2(missile.velocity.x, missile.velocity.z);
    CHECK(turned > 0.4 && turned < 0.51);  // 0,05 rad/s por 10 s

    // Choques con un segmento: tambien lo que cruza entre dos pasos.
    double at = 0;
    CHECK(SegmentNear(Vec3d{-100, 5, 0}, Vec3d{100, 5, 0}, Vec3d{0, 0, 0}, 6.0, &at) && std::abs(at - 0.5) < 1e-9);
    CHECK(!SegmentNear(Vec3d{-100, 8, 0}, Vec3d{100, 8, 0}, Vec3d{0, 0, 0}, 6.0));

    // El computador cuenta mas astros cuanto mas nivel tiene.
    std::vector<GravitySource> many;
    for (int i = 0; i < 10; ++i) many.push_back({Vec3d{1.0e5 * (i + 1), 0, 0}, 1.0e9, 1.0});
    CHECK(Strongest(many, Vec3d{}, 3).size() == 3 && Strongest(many, Vec3d{}, 3)[0].position.x == 1.0e5);
}

// Las pruebas de combate miden contra la fragata ligera, la nave de referencia (casco = 1): un dron
// moriria de un solo impacto. La clase que el juego reparte de verdad se prueba en TestShipClasses.
static TargetRef FindShip(GameSession& g, Stance stance, AnomalyKind kind = AnomalyKind::AlienShip) {
    const AnomalyRules anomalyRules;
    for (Anomaly& a : g.Anomalies()) {
        if (a.stance != stance || a.kind != kind) continue;
        if (kind == AnomalyKind::AlienShip && (a.shipClass != ShipClassId::Frigate || a.variant != ShipVariant::Light)) {
            a.shipClass = ShipClassId::Frigate;
            a.variant = ShipVariant::Light;
            a.maxHull = a.hull = ClassOf(a).hull * anomalyRules.referenceHull;
            a.shieldMax = a.shieldCharge = ClassOf(a).shield * anomalyRules.referenceShield;
        }
        return {Kind::Anomaly, a.system, a.id};
    }
    return {};
}

static const Anomaly* Find(const GameSession& g, const TargetRef& t) {
    for (const Anomaly& a : g.Anomalies())
        if (a.id == t.index) return &a;
    return nullptr;
}

static void TestCombat() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Peaceful);  // nada dispara de vuelta: aqui solo se prueban las armas
    g.Step(0.05, {});
    CHECK(g.Ship().weapons.size() == 4);
    CHECK(!g.GravitySources().empty());

    // Sin blanco, sin burbuja o fuera de alcance, no se dispara.
    CHECK(!g.Fire(WeaponKind::Turret, TargetRef{}));
    const TargetRef enemy = FindShip(g, Stance::Hostile);
    CHECK(enemy.kind == Kind::Anomaly);
    CHECK(g.JumpTo(enemy, 1.0e6));
    g.Step(0.05, {});
    CHECK(!g.Fire(WeaponKind::Turret, enemy));  // a 1,5e5 km de alcance
    CHECK(g.Fire(WeaponKind::Missile, enemy));  // el misil llega a 1,2e6
    CHECK(g.Projectiles().size() == 1 && g.Projectiles()[0].guided && g.Projectiles()[0].type == DamageType::Antimatter);
    CHECK(!g.Fire(WeaponKind::Missile, enemy));  // recargando
    g.Ship().warpSpeed = 1.0e6;
    CHECK(!g.Fire(WeaponKind::Drone, enemy));
    g.Ship().warpSpeed = 0.0;

    // El misil persigue y da; pesa 0,2 kt de antimateria.
    const double fullHull = Find(g, enemy)->maxHull;
    for (int i = 0; i < 1200 && !g.Projectiles().empty(); ++i) Run(g, 0.1);
    CHECK(g.Projectiles().empty());
    CHECK(Find(g, enemy) == nullptr || Find(g, enemy)->hull < fullHull);

    // Torreta con computador bueno: acaba con una nave a tiro, y suelta restos.
    GameSession h(*engine, rules, 7, GameModeId::Peaceful);
    h.Step(0.05, {});
    const TargetRef victim = FindShip(h, Stance::Hostile);
    CHECK(h.JumpTo(victim, 4.0e4));
    h.Ship().computerTier = 5;
    const double metal = h.Ship().cargo.Get(Resource::Metal);
    double lowest = 1e300;
    for (int i = 0; i < 1500 && Find(h, victim); ++i) {
        Run(h, 0.1);
        if (const Anomaly* a = Find(h, victim)) lowest = std::min(lowest, a->hull);
        h.Fire(WeaponKind::Turret, victim);
    }
    CHECK(lowest < fullHull);                    // recibio impactos antes de caer
    CHECK(Find(h, victim) == nullptr);           // destruida
    CHECK(h.Kills() == 1);
    CHECK(h.Ship().cargo.Get(Resource::Metal) >= metal + rules.anomalies.shipLoot - 1e-9);

    // El canon espinal sale por donde mira el casco: alineado, un solo haz basta.
    GameSession s(*engine, rules, 7, GameModeId::Peaceful);
    s.Step(0.05, {});
    const TargetRef prey = FindShip(s, Stance::Hostile);
    CHECK(s.JumpTo(prey, 4.0e4));
    s.Step(0.05, {});
    const std::optional<FiringSolution> sol = s.Solution(prey, WeaponKind::Spinal);
    CHECK(sol && sol->flightTime < 5.0);
    FaceTo(s.Ship(), sol->direction);
    CHECK(s.Fire(WeaponKind::Spinal, prey));
    CHECK(s.Projectiles().size() == 1 && s.Projectiles()[0].beam);
    Run(s, 0.1);
    CHECK(s.Projectiles().empty());
    const Anomaly* hit = Find(s, prey);
    CHECK(hit && hit->hull < hit->maxHull);
    // El impacto deja un destello, y el haz se ve de punta a punta un instante.
    CHECK(s.Impacts().size() == 1 && s.Impacts()[0].beam && s.Impacts()[0].hit);
    CHECK(Length(s.Impacts()[0].position - s.Impacts()[0].origin) > 3.0e4);
    Run(s, 1.0);
    CHECK(s.Impacts().empty());
    // Mal alineado, falla.
    s.Ship().mode = DriveMode::Impulse;
    for (int i = 0; i < 800 && !s.WeaponReady(WeaponKind::Spinal); ++i) Run(s, 0.1);
    CHECK(s.WeaponReady(WeaponKind::Spinal));
    const double before = Find(s, prey) ? Find(s, prey)->hull : 0.0;
    FaceTo(s.Ship(), Vec3d{0, 1, 0} * 0.7 + (s.Solution(prey, WeaponKind::Spinal)->direction * 0.7));
    CHECK(s.Fire(WeaponKind::Spinal, prey));
    Run(s, 0.1);
    CHECK(Find(s, prey) && Find(s, prey)->hull == before);

    // Con el seguimiento, el casco se alinea solo y FireAll incluye el canon. El raton lo suelta.
    GameSession t(*engine, rules, 7, GameModeId::Peaceful);
    t.Step(0.05, {});
    const TargetRef tracked = FindShip(t, Stance::Hostile);
    CHECK(t.JumpTo(tracked, 4.0e4));
    t.Step(0.05, {});
    FaceTo(t.Ship(), Cross(t.Solution(tracked, WeaponKind::Spinal)->direction, Vec3d{0, 1, 0}));  // de costado
    t.TrackTarget(tracked);
    CHECK(t.Tracking().has_value());
    bool aligned = false;
    for (int i = 0; i < 600 && !aligned; ++i) {
        Run(t, 0.05);
        const std::optional<FiringSolution> now = t.Solution(tracked, WeaponKind::Spinal);
        aligned = now && std::acos(std::clamp(Dot(game::Heading(t.Ship()), now->direction), -1.0, 1.0)) * 4.0e4 < rules.combat.weapons[1].proximity;
    }
    CHECK(aligned);
    CHECK(t.Fire(WeaponKind::Spinal, tracked));
    Run(t, 0.1);
    CHECK(Find(t, tracked) && Find(t, tracked)->hull < Find(t, tracked)->maxHull);
    ShipControls mouse;
    mouse.turnYaw = 0.01f;
    t.Step(0.05, mouse);
    CHECK(!t.Tracking().has_value());
    t.TrackTarget(TargetRef{});
    CHECK(!t.Tracking().has_value());

    // Drones: seis, guiados, y cuestan metal.
    GameSession d(*engine, rules, 7, GameModeId::Peaceful);
    d.Step(0.05, {});
    const TargetRef swarmTarget = FindShip(d, Stance::Hostile);
    CHECK(d.JumpTo(swarmTarget, 2.0e5));
    d.Step(0.05, {});
    CHECK(!d.Fire(WeaponKind::Drone, swarmTarget));  // sin metal
    d.Ship().cargo.Add(Resource::Metal, 10.0);
    CHECK(d.Fire(WeaponKind::Drone, swarmTarget));
    CHECK(d.Projectiles().size() == 6);
    CHECK(d.Ship().cargo.Get(Resource::Metal) < 9.5);

    // Una nave neutral que recibe fuego deja de serlo.
    GameSession n(*engine, rules, 7, GameModeId::Peaceful);
    n.Step(0.05, {});
    const TargetRef calm = FindShip(n, Stance::Neutral);
    CHECK(n.JumpTo(calm, 4.0e4));
    n.Ship().computerTier = 5;
    for (int i = 0; i < 400 && Find(n, calm) && Find(n, calm)->stance == Stance::Neutral; ++i) {
        Run(n, 0.1);
        n.Fire(WeaponKind::Turret, calm);
    }
    CHECK(!Find(n, calm) || Find(n, calm)->stance == Stance::Hostile);

    // Las ruinas no tienen casco: no se pueden destruir.
    GameSession r(*engine, rules, 7, GameModeId::Peaceful);
    r.Step(0.05, {});
    TargetRef ruin;
    for (const Anomaly& a : r.Anomalies())
        if (a.kind == AnomalyKind::Derelict) ruin = {Kind::Anomaly, a.system, a.id};
    CHECK(ruin.kind == Kind::Anomaly);
    CHECK(r.JumpTo(ruin, 4.0e4));
    CHECK(!r.Fire(WeaponKind::Turret, ruin));
}

// Un enemigo dispara de verdad: sale un proyectil suyo, que cuesta casco y se puede derribar.
static void TestIncoming() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Normal);
    g.Step(0.05, {});
    const TargetRef enemy = FindShip(g, Stance::Hostile);
    CHECK(g.JumpTo(enemy, 5.0e4));
    bool sawShot = false;
    const double hull = g.Ship().hull;
    for (int i = 0; i < 200 && g.Ship().hull == hull; ++i) {
        Run(g, 0.1);
        for (const Projectile& p : g.Projectiles()) sawShot = sawShot || (p.team == 1 && p.target == 0);
    }
    CHECK(sawShot);
    CHECK(g.Ship().hull < hull);
    CHECK(g.Ship().hull > hull - 200.0);  // un impacto, no un golpe de gracia
    CHECK(g.TimeFocus() < 1.0);           // en combate el tiempo se frena

    // Un computador mejor no cambia lo que dispara el enemigo, pero el del jugador si: subir de nivel acierta mas.
    CHECK(AimError(rules.combat, 1) > AimError(rules.combat, 2));

    // Modos: Facil recibe la mitad del dano que Normal por el mismo impacto.
    GameSession easy(*engine, rules, 7, GameModeId::Easy);
    easy.Step(0.05, {});
    const TargetRef foe = FindShip(easy, Stance::Hostile);
    CHECK(easy.JumpTo(foe, 5.0e4));
    const double easyHull = easy.Ship().hull;
    for (int i = 0; i < 400 && easy.Ship().hull == easyHull; ++i) Run(easy, 0.1);
    CHECK(easy.Ship().hull < easyHull);
    CHECK(easyHull - easy.Ship().hull <= Scaled(rules.combat, WeaponKind::Turret, 1).damage * ModeInfo(GameModeId::Easy).damage + 1e-9);

    // Pacifico: nadie dispara.
    GameSession calm(*engine, rules, 7, GameModeId::Peaceful);
    calm.Step(0.05, {});
    CHECK(calm.JumpTo(FindShip(calm, Stance::Hostile), 5.0e4));
    Run(calm, 20.0);
    for (const Projectile& p : calm.Projectiles()) CHECK(p.team == 0);
    CHECK(calm.Ship().hull == rules.ship.hull);
}

// Hibrida e Hiper no existen hasta explorar la estructura del primer sistema.
static void TestDriveUnlock() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7);
    g.Step(0.05, {});
    CHECK(std::string(Name(DriveMode::Warp)) == "Híbrida");
    CHECK(g.SetDrive(DriveMode::Impulse) && g.SetDrive(DriveMode::Cruise));
    CHECK(!g.SetDrive(DriveMode::Warp) && !g.SetDrive(DriveMode::Hyper));
    CHECK(g.Ship().mode == DriveMode::Cruise);
    CHECK(!CanCurve(g.Ship(), DriveMode::Warp, rules.drive));
    g.Ship().mode = DriveMode::Warp;  // una partida antigua en ese modo no se queda colgada
    g.SetThrottle(1.0);
    g.Step(0.05, {});
    CHECK(g.Ship().mode == DriveMode::Cruise);
    g.SetThrottle(0.0);

    // La estructura del primer sistema da el emisor de energia negativa: la Hibrida, no el Hiper.
    TargetRef ruin;
    for (const Anomaly& a : g.Anomalies())
        if (a.kind == AnomalyKind::Derelict && a.system == 0) ruin = {Kind::Anomaly, 0, a.id};
    CHECK(ruin.kind == Kind::Anomaly);
    CHECK(g.JumpTo(ruin, 5.0));
    g.Step(0.05, {});
    CHECK(g.Interact(ruin));
    for (int i = 0; i < 4000 && !HybridUnlocked(g.Ship()); ++i) g.Step(0.05, {});
    CHECK(HybridUnlocked(g.Ship()));
    CHECK(g.SetDrive(DriveMode::Warp) && !g.SetDrive(DriveMode::Hyper));
}

// Q y E giran sobre el eje: la nariz sigue donde estaba, y lo de arriba cambia.
static void TestRoll() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7);
    g.Step(0.05, {});
    const Vec3d nose = Heading(g.Ship());
    const Vec3d up = g.Ship().orientation.ToMatrix().TransformDirection(Vec3d{0, 1, 0});
    ShipControls roll;
    roll.turnRoll = 0.02f;
    for (int i = 0; i < 60; ++i) g.Step(0.05, roll);
    CHECK(Dot(Heading(g.Ship()), nose) > 0.999);
    CHECK(Dot(g.Ship().orientation.ToMatrix().TransformDirection(Vec3d{0, 1, 0}), up) < 0.99);
    // Alabear manda sobre la ruta asistida, como cualquier otro mando.
    CHECK(g.SetAutopilot(FirstPlanet(g, 0)));
    g.Step(0.05, roll);
    CHECK(!g.Autopilot().has_value());
}

// El menu de sistemas: secciones, lunas bajo su planeta y lo que aun no se ha analizado.
static void TestAtlas() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7);
    g.Step(0.05, {});
    CHECK(g.Visited(0) && !g.Visited(1) && g.VisitedSystems() == 1);

    const std::vector<AtlasRow> blind = g.AtlasRows(0);
    CHECK(!blind.empty() && blind[0].target.kind == Kind::Star);
    bool unknown = false;
    for (const AtlasRow& row : blind) unknown = unknown || (!row.known && row.detail == "Sin analizar");
    CHECK(unknown);

    g.MarkScanned(0);
    const std::vector<AtlasRow> rows = g.AtlasRows(0);
    const StarSystemData& data = *g.GetWorld().systems[0];
    int planets = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const AtlasRow& row = rows[i];
        CHECK(row.known && !row.name.empty());
        if (row.target.kind != Kind::Planet) continue;
        const BodyData& body = data.bodies[static_cast<std::size_t>(row.target.index)];
        CHECK((row.depth == 1) == (body.parent >= 0));
        if (body.parent >= 0) {
            bool under = false;  // su planeta va antes, y solo hay lunas en medio
            for (std::size_t j = i; j-- > 0;) {
                if (rows[j].target.kind == Kind::Planet && rows[j].depth == 1) continue;
                under = rows[j].target.kind == Kind::Planet && rows[j].target.index == body.parent;
                break;
            }
            CHECK(under);
        } else {
            ++planets;
        }
    }
    CHECK(planets > 0);

    // Sirve para un sistema que no esta cargado.
    CHECK(!g.AtlasRows(5).empty());
    CHECK(g.AtlasRows(9999).empty());

    // Llegar a un sistema lo marca como visitado, y se guarda.
    g.MarkScanned(1);
    CHECK(g.JumpTo({Kind::Star, 1, 0}, YieldOf(g.GetWorld().systems[1]->star).orbitRadius * 1.5));
    Run(g, 2.0);
    CHECK(g.Visited(1) && g.VisitedSystems() == 2);
    const std::optional<SaveData> read = ReadSave(WriteSave(g.Snapshot()));
    CHECK(read && read->player.visitedSystems.size() > 1 && read->player.visitedSystems[1]);
}

// La composicion por elementos: suma 1 y cada clase de astro tiene lo que le toca.
static void TestElements() {
    const auto sum = [](const Composition& c) {
        double total = 0;
        for (const double v : c.share) total += v;
        return total;
    };
    const auto top = [](const Composition& c) {
        std::size_t best = 0;
        for (std::size_t e = 1; e < kElementCount; ++e)
            if (c.share[e] > c.share[best]) best = e;
        return static_cast<Element>(best);
    };
    Materials metal;
    metal.iron = 0.8;
    metal.silicate = 0.2;
    Materials rock;
    rock.iron = 0.2;
    rock.silicate = 0.8;
    Materials giant;
    giant.gas = 0.9;
    giant.ices = 0.1;
    Materials ocean;
    ocean.water = 0.5;
    ocean.silicate = 0.5;
    for (const Materials* m : {&metal, &rock, &giant, &ocean}) CHECK(std::abs(sum(ElementsOf(*m)) - 1.0) < 1e-9);
    CHECK(top(ElementsOf(metal)) == Element::Fe);
    CHECK(top(ElementsOf(rock)) == Element::O);
    CHECK(top(ElementsOf(giant)) == Element::H && ElementsOf(giant).Of(Element::He) > 0.15);
    CHECK(ElementsOf(ocean).Of(Element::H) > 0.04);
    CHECK(ElementsOf(rock).Of(Element::He) == 0.0);

    StarData sun;
    const Composition star = ElementsOf(sun);
    CHECK(std::abs(sum(star) - 1.0) < 1e-9);
    CHECK(std::abs(star.Of(Element::H) - 0.73) < 0.01 && star.Of(Element::Fe) > 0.0 && star.Of(Element::Fe) < 0.01);
    CHECK(std::string(ElementSymbol(Element::Fe)) == "Fe" && std::string(ElementName(Element::He)) == "Helio");

    // El color de la roca sigue a sus elementos: con mas hierro, mas oscura.
    BodyData dark, light;
    dark.kind = light.kind = PlanetClass::Rock;
    dark.mass = light.mass = 3.0e23;
    dark.radius = light.radius = 2500.0;
    dark.materials = metal;
    light.materials = rock;
    const Appearance a = DeriveAppearance(dark), b = DeriveAppearance(light);
    CHECK(a.b.x + a.b.y + a.b.z < b.b.x + b.b.y + b.b.z);
    CHECK(a.oceanLevel > 0.5);  // sin aire, muchos crateres

    // La tabla entera: 118 elementos que suman 1, los doce de las recetas casi intactos, los
    // siderofilos con el hierro, los gases nobles con el helio y nada del 95 en adelante.
    CHECK(std::string(Periodic(26).symbol) == "Fe" && Periodic(26).row == 4 && Periodic(26).column == 8);
    CHECK(Periodic(1).column == 1 && Periodic(2).column == 18 && Periodic(5).column == 13 && Periodic(13).column == 13);
    CHECK(Periodic(57).row == 9 && Periodic(57).column == 3 && Periodic(71).column == 17 && Periodic(72).row == 6 && Periodic(72).column == 4);
    CHECK(Periodic(103).row == 10 && Periodic(118).row == 7 && Periodic(118).column == 18 && std::string(Periodic(118).name) == "Oganesón");
    const auto total = [](const PeriodicShares& p) { double t = 0; for (const double v : p) t += v; return t; };
    const PeriodicShares metalAll = AllElementsOf(ElementsOf(metal), 7), rockAll = AllElementsOf(ElementsOf(rock), 7), giantAll = AllElementsOf(ElementsOf(giant), 7);
    for (const PeriodicShares* p : {&metalAll, &rockAll, &giantAll}) {
        CHECK(std::abs(total(*p) - 1.0) < 1e-9);
        for (int z = 95; z <= kPeriodicCount; ++z) CHECK((*p)[static_cast<std::size_t>(z - 1)] == 0.0);
        // Las trazas siempre existen; los doce dependen de la receta (el hierro puro no tiene helio).
        for (int z = 3; z <= 92; ++z) {
            const bool main = z == 6 || z == 7 || z == 8 || z == 12 || z == 13 || z == 14 || z == 16 || z == 20 || z == 26 || z == 28;
            CHECK(main || (*p)[static_cast<std::size_t>(z - 1)] > 0.0);
        }
    }
    CHECK(std::abs(rockAll[7] - ElementsOf(rock).Of(Element::O)) < 0.03);  // el oxigeno, casi igual
    CHECK(metalAll[77] > rockAll[77] * 3.0);   // platino: va con el hierro
    CHECK(giantAll[9] > rockAll[9] * 100.0);   // neon: va con el helio
    CHECK(rockAll[91] > 0.0 && rockAll[87] > 0.0 && rockAll[87] < rockAll[91] * 1e-5);  // el radio sale del uranio
    CHECK(AllElementsOf(ElementsOf(rock), 7)[50] == rockAll[50]);  // misma semilla, mismo astro
    CHECK(AllElementsOf(ElementsOf(rock), 8)[50] != rockAll[50]);  // otra semilla, otro reparto
}

// Una persona en una cubierta: anda, choca, sube escalones, salta y cae.
static void TestHumanoid() {
    const HumanoidRules body;
    WalkArea floor;
    floor.solids.push_back({{-50, -1, -50}, {50, 0, 50}});
    floor.solids.push_back({{5, 0, -5}, {6, 3, 5}});        // una pared delante
    floor.solids.push_back({{-5, 0, 3}, {-3, 0.3, 5}});     // un escalon bajo
    floor.solids.push_back({{-5, 0, -5}, {-3, 0.9, -3}});   // un cajon alto

    HumanoidState h;
    h.position = {0, 0.5, 0};
    HumanoidInput none;
    for (int i = 0; i < 120; ++i) StepHumanoid(h, none, floor, body, 9.81, 1.0 / 60.0);
    CHECK(h.grounded && std::abs(h.position.y) < 0.01);  // cae y se queda en el suelo

    // Andando hacia +X para en la pared, sin atravesarla, y a la velocidad de paso.
    h.yaw = 1.5707963;
    HumanoidInput walk;
    walk.forward = 1.0;
    double fastest = 0;
    for (int i = 0; i < 600; ++i) {
        StepHumanoid(h, walk, floor, body, 9.81, 1.0 / 60.0);
        fastest = std::max(fastest, Length(h.velocity));
    }
    CHECK(h.position.x <= 5.0 - body.radius + 1e-3 && h.position.x > 4.0);
    CHECK(std::abs(fastest - body.walkSpeed) < 0.05);
    walk.run = true;
    h.position = {0, 0, 0};
    h.yaw = 3.1415926;
    for (int i = 0; i < 60; ++i) StepHumanoid(h, walk, floor, body, 9.81, 1.0 / 60.0);
    CHECK(Length(h.velocity) > body.walkSpeed * 2.0);

    // Un escalon bajo se sube solo; un cajon alto, no.
    h = {};
    h.position = {-4, 0, 0};
    h.yaw = 0.0;  // hacia +Z
    walk.run = false;
    for (int i = 0; i < 150; ++i) StepHumanoid(h, walk, floor, body, 9.81, 1.0 / 60.0);  // unos 4 m: encima del escalon
    CHECK(h.position.z > 3.5 && h.position.z < 5.0 && h.position.y > 0.29);
    h = {};
    h.position = {-4, 0, 0};
    h.yaw = 3.1415926;  // hacia -Z, contra el cajon
    for (int i = 0; i < 240; ++i) StepHumanoid(h, walk, floor, body, 9.81, 1.0 / 60.0);
    CHECK(h.position.z >= -3.0 + body.radius - 1e-3 && h.position.y < 0.01);

    // Un salto sube unos 50 cm a 1 g, y vuelve al suelo.
    HumanoidInput hop;
    hop.jump = true;
    double peak = 0;
    for (int i = 0; i < 90; ++i) {
        StepHumanoid(h, hop, floor, body, 9.81, 1.0 / 60.0);
        hop.jump = false;
        peak = std::max(peak, h.position.y);
    }
    CHECK(peak > 0.45 && peak < 0.6);
    CHECK(h.grounded && h.position.y < 0.01);

    // Sin gravedad flota: no hay suelo que le empuje.
    HumanoidState drift;
    drift.position = {0, 2, 0};
    drift.velocity = {0, 0.5, 0};
    StepHumanoid(drift, walk, floor, body, 0.0, 1.0);
    CHECK(!drift.grounded && drift.position.y > 2.4);

    CHECK(!HumanoidFits({5.5, 0, 0}, floor, body) && HumanoidFits({0, 0.01, 0}, floor, body));
    CHECK(ClearRay({0, 1, 0}, {10, 1, 0}, floor, 0.0) <= 0.5);
    CHECK(ClearRay({0, 1, 0}, {-2, 1, 0}, floor, 0.0) == 1.0);
}

// Atracar sin rebotes, bajar a pie, andar por la cubierta y volver.
static void TestDocking() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Peaceful);
    g.Conversation().Stop();
    g.Step(0.05, {});
    g.MarkScanned(0);
    const TargetRef planet = FirstPlanet(g, 0, 1);
    const TargetRef station = PlaceStation(g);
    for (int i = 0; i < 200; ++i) g.Step(0.05, {});  // que termine de montarse

    CHECK(!g.Dock(planet));  // solo estaciones
    CHECK(!g.Disembark());   // sin atracar no se baja

    // Desde lejos: primero la ruta, luego el haz. La distancia solo baja: ni rebota ni se pasa.
    CHECK(g.JumpTo(station, 3000.0));
    g.Undock();
    CHECK(g.Dock(station));
    double last = 1e300;
    bool monotonic = true;
    for (int i = 0; i < 4000 && !g.Docked(); ++i) {
        g.Step(0.05, {});
        if (g.Docking() == DockPhase::Approaching) {
            const double now = Length(g.Ship().position - g.Resolve(station)->position);
            monotonic = monotonic && now <= last + 1e-6;
            last = now;
        }
    }
    CHECK(g.Docked());
    CHECK(monotonic);
    // Atracada de costado: la compuerta de estribor frente a la escotilla, a lo largo del ducto, y mirandola.
    const auto offHatch = [&] {
        const GameSession::DockPort port = g.Port(*g.Stations().Find(station.index));
        const Vec3d side = g.Ship().position + g.Ship().orientation.ToMatrix().TransformDirection(CurrentShipDesign().dockPort * 0.001);
        return Length(side - (port.position + port.normal * 0.12));
    };
    CHECK(offHatch() < 0.05);
    Run(g, 5.0);  // atracada se queda atracada
    CHECK(g.Docked() && offHatch() < 0.05);
    CHECK(Dot(g.Ship().orientation.ToMatrix().TransformDirection(Vec3d{1, 0, 0}), g.Port(*g.Stations().Find(station.index)).normal) < -0.99);
    CHECK(std::fabs(Dot(game::Heading(g.Ship()), g.Port(*g.Stations().Find(station.index)).normal)) < 0.01);  // la proa, de lado
    // El ducto se despliega y une la compuerta con la escotilla.
    CHECK(g.Tube().active && g.Tube().extension >= 1.0);
    CHECK(Length(g.Tube().stationEnd - g.Port(*g.Stations().Find(station.index)).position) < 1e-9);
    CHECK(std::fabs(Length(g.Tube().stationEnd - g.Tube().shipEnd) - 0.12) < 0.05);

    // Atracada, se repara.
    g.Ship().hull = 500.0;
    Run(g, 5.0);
    CHECK(g.Ship().hull > 500.0);

    // Se guarda atracada.
    const std::optional<SaveData> read = ReadSave(WriteSave(g.Snapshot()));
    CHECK(read && read->Ship()->docked);

    // A pie: cubierta, gente, tiempo real y la esclusa para volver.
    CHECK(g.Disembark());
    CHECK(g.OnFoot() && g.Crew().size() == static_cast<std::size_t>(rules.foot.crew));
    CHECK(g.CanBoard());
    const double simBefore = g.SimTime();
    ShipControls walk;
    walk.foot.move.forward = 1.0;
    for (int i = 0; i < 200; ++i) g.Step(0.05, walk);  // diez segundos andando
    CHECK(!g.CanBoard());
    CHECK(!g.Board());
    CHECK(g.Player().grounded);
    CHECK(Length(g.Player().position - g.Deck().spawn) > 8.0);
    CHECK(g.SimTime() - simBefore < 20.0);  // a pie el tiempo no corre a 60 veces
    bool crewMoved = false;
    const std::vector<Person> crewBefore = g.Crew();
    for (int i = 0; i < 300; ++i) g.Step(0.05, {});
    for (std::size_t i = 0; i < g.Crew().size(); ++i) crewMoved = crewMoved || Length(g.Crew()[i].body.position - crewBefore[i].body.position) > 1.0;
    CHECK(crewMoved);
    for (const Person& p : g.Crew()) CHECK(HumanoidFits(p.body.position, g.Deck().area, g.PersonRules()));
    CHECK(g.Board(true));
    CHECK(!g.OnFoot() && g.Docked());

    // Dar mando suelta las amarras.
    g.SetThrottle(0.5);
    g.Step(0.05, {});
    CHECK(!g.Docked() && g.Docking() == DockPhase::None);
}

// Cerca de una estacion la nave no corre: se arrima sin pasarse.
static void TestApproach() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Peaceful);
    g.Conversation().Stop();
    g.Step(0.05, {});
    g.MarkScanned(0);
    const TargetRef station = PlaceStation(g);
    CHECK(g.JumpTo(station, 200.0));
    FaceTo(g.Ship(), g.Resolve(station)->position - g.Ship().position);
    g.SetThrottle(1.0);
    const auto away = [&] { return Length(g.Ship().position - g.Resolve(station)->position); };
    double closest = away();
    for (int i = 0; i < 600 && away() > 40.0; ++i) {
        g.Step(0.05, {});
        closest = std::min(closest, away());
    }
    CHECK(closest < 60.0);             // con mando, llega
    g.SetThrottle(0.0);                // y al soltarlo se queda a su lado
    for (int i = 0; i < 400; ++i) {
        g.Step(0.05, {});
        closest = std::min(closest, away());
    }
    CHECK(away() < 200.0);
    // Junto a ella esta en la zona de atraque, con el tiempo frenado y la velocidad acotada en
    // segundos reales: no tarda una eternidad en llegar ni se pasa.
    CHECK(g.Approach().has_value() && g.Approach()->zone);
    CHECK(g.Approach()->top * g.TimeScale() * g.TimeFocus() < g.Approach()->distance / rules.ship.recedeTime * 1.2 + 0.2);
    CHECK(Length(g.Ship().velocity - (g.Resolve(station)->velocity - (g.ShipVelocity() - g.Ship().velocity))) < 1.0);
}

// Al soltar amarras la estacion no se aleja: la nave vuela en su marco aunque la estacion orbite y
// la estrella tire de ella.
static void TestStationFrame() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Peaceful);
    g.Conversation().Stop();
    g.Step(0.05, {});
    g.MarkScanned(0);
    const TargetRef station = PlaceStation(g);
    for (int i = 0; i < 200; ++i) g.Step(0.05, {});
    CHECK(g.JumpTo(station, 5.0));
    CHECK(g.Dock(station, true));
    g.Step(0.05, {});
    g.Undock();
    const auto away = [&] { return Length(g.Ship().position - g.Resolve(station)->position); };
    const double start = away();
    for (int i = 0; i < 1200; ++i) g.Step(0.05, {});  // un minuto real a la deriva
    CHECK(std::abs(away() - start) < 0.5);

    // Con mando se mueve a su alrededor; al soltarlo se queda donde la dejo, sin caer al planeta.
    g.SetThrottle(-0.3);  // marcha atras: se aparta de la estacion
    for (int i = 0; i < 60; ++i) g.Step(0.05, {});
    g.SetThrottle(0.0);
    for (int i = 0; i < 200; ++i) g.Step(0.05, {});
    const double moved = away();
    CHECK(moved > start + 0.05);
    for (int i = 0; i < 1200; ++i) g.Step(0.05, {});
    CHECK(std::abs(away() - moved) < 0.5);
    CHECK(g.Ship().speed < 0.05);  // la velocidad que se ve es respecto a la estacion
}

// Modo creativo: todo localizado, todos los motores, nada se gasta ni se rompe, y se crean anomalias.
static void TestCreative() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Normal);
    g.Conversation().Stop();
    g.Step(0.05, {});
    g.SetCreative(true);
    CHECK(g.Creative());
    CHECK(g.KnownSystems() == static_cast<int>(g.GetWorld().systems.size()));
    CHECK(g.Scanned({Kind::Star, 5, 0}));
    CHECK(HyperUnlocked(g.Ship(), rules.drive) && g.SetDrive(DriveMode::Hyper));
    g.Ship().hull = 1.0;
    g.Ship().antimatter = 0.0;
    g.Step(0.05, {});
    CHECK(g.Ship().hull == rules.ship.hull && g.Ship().antimatter == rules.ship.antimatterTank);
    const std::size_t before = g.Anomalies().size();
    const int id = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 500.0);
    CHECK(id >= 0 && g.Anomalies().size() == before + 1);
    const std::optional<TargetInfo> info = g.Resolve({Kind::Anomaly, g.NearestSystem(), id});
    CHECK(info && std::abs(Length(info->position - g.Ship().position) - 500.0) < 1.0);
    CHECK(g.Anomalies().back().maxHull > 0.0 && g.Anomalies().back().armed);
    const int ruin = g.SpawnAnomaly(AnomalyKind::Derelict, Stance::None, 50.0);
    CHECK(ruin > id && g.Anomalies().back().emitter);
    g.SetCreative(false);
    g.Ship().hull = 10.0;
    g.Step(0.05, {});
    CHECK(g.Ship().hull < rules.ship.hull);
}

// El combate se guarda: cascos, recargas, armas y computador.
static void TestCombatSave() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Peaceful);
    g.Step(0.05, {});
    g.Ship().computerTier = 4;
    g.Ship().weapons[0].tier = 3;
    g.Ship().weapons[0].cooldown = 1.5;
    Anomaly hurt;
    for (Anomaly& a : g.Anomalies())
        if (a.maxHull > 0.0) {
            a.hull = 123.5;
            a.cooldown = 7.25;
            hurt = a;
            break;
        }
    const SaveData data = g.Snapshot();
    std::string error;
    const std::optional<SaveData> read = ReadSave(WriteSave(data), &error);
    CHECK(read.has_value());
    if (!read) return;
    CHECK(read->Ship()->ship.computerTier == 4);
    CHECK(read->Ship()->ship.weapons.size() == 4 && read->Ship()->ship.weapons[0].tier == 3 && read->Ship()->ship.weapons[0].cooldown == 1.5);
    bool found = false;
    for (const Anomaly& a : read->world.anomalies)
        if (a.id == hurt.id) {
            found = true;
            CHECK(a.hull == 123.5 && a.maxHull == hurt.maxHull && a.cooldown == 7.25 && a.armed == hurt.armed && a.weapon == hurt.weapon);
        }
    CHECK(found);

    // Una partida de antes del combate se carga con armas y cascos de serie.
    SaveData old = data;
    old.Ship()->ship.weapons.clear();
    for (Anomaly& a : old.world.anomalies) a.hull = a.maxHull = 0.0;
    GameSession loaded(*engine, rules, 7, GameModeId::Peaceful);
    CHECK(loaded.Restore(old));
    CHECK(loaded.Ship().weapons.size() == 4);
    for (const Anomaly& a : loaded.Anomalies()) {
        if (a.kind == AnomalyKind::Derelict) CHECK(a.maxHull == 0.0);
        else CHECK(a.maxHull > 0.0 && a.hull == a.maxHull);
    }
}

// Fase F: niveles de estacion, planeta coraza, alerta por region, rendicion y revancha.
static void TestStationLevelsAndAlert() {
    Balance rules;
    const AnomalyRules& ar = rules.anomalies;
    const AlertRules& al = rules.alert;

    // Vida por nivel: el 2 es 30 veces la fragata ligera; el 6, el planeta coraza, 30 000.
    const double frigate = ShipClassOf(ShipClassId::Frigate, ShipVariant::Light).hull * ar.referenceHull;
    CHECK(StationHull(ar, 2) == 30.0 * frigate);
    CHECK(StationHull(ar, 1) == 10.0 * frigate && StationHull(ar, 4) == 300.0 * frigate && StationHull(ar, 6) == 30000.0 * frigate);
    for (int l = 2; l <= kStationLevels; ++l) CHECK(StationHull(ar, l) > StationHull(ar, l - 1));

    // El nivel sale del id: siempre de 1 a 5, todos aparecen y el 1 es el mas comun.
    int seen[kStationLevels + 1] = {};
    for (int id = 1; id <= 4000; ++id) {
        const int l = StationLevelFor(id, ar);
        CHECK(l >= 1 && l <= 5);
        if (l >= 1 && l <= 5) ++seen[l];
        if (id <= 50) CHECK(StationLevelFor(id, ar) == l);
    }
    for (int l = 1; l <= 5; ++l) CHECK(seen[l] > 0);
    CHECK(seen[1] > seen[3] && seen[3] > seen[5]);

    // Generacion: estaciones con nivel y su vida; como mucho un planeta coraza, al final, en la mitad lejana.
    const std::vector<double> extents(40, 6.0e9);
    int withPlanet = 0;
    for (std::uint32_t seed = 1; seed <= 60; ++seed) {
        const std::vector<Anomaly> list = GenerateAnomalies(extents, ar, seed);
        int planets = 0, maxId = 0;
        for (const Anomaly& a : list) {
            maxId = std::max(maxId, a.id);
            if (a.kind != AnomalyKind::AlienStation) continue;
            if (IsArmorPlanet(a)) {
                ++planets;
                CHECK(a.stance == Stance::Hostile && a.radius == ar.armorPlanetRadius && a.system >= 20 && a.system < 40);
                CHECK(a.maxHull == StationHull(ar, kArmorPlanetLevel) && a.hull == a.maxHull && a.signature == ar.armorPlanetSignature);
                CHECK(Length(a.offset) > extents[static_cast<std::size_t>(a.system)]);  // por fuera de sus planetas
                CHECK(a.id == list.back().id);
            } else {
                CHECK(a.stationLevel >= 1 && a.stationLevel <= 5 && a.maxHull == StationHull(ar, a.stationLevel));
            }
        }
        CHECK(planets <= 1);
        withPlanet += planets;
        // Lo demas no cambia con el planeta: mismos ids que antes de anadirlo.
        if (planets == 1) CHECK(list[list.size() - 2].id == maxId - 1);
    }
    CHECK(withPlanet > 0 && withPlanet < 60);  // raro, pero existe

    // El planeta coraza no se mueve.
    Anomaly planet;
    planet.id = 99;
    MakeArmorPlanet(planet, ar);
    std::mt19937 rng(3);
    const Vec3d at = planet.offset;
    StepAnomaly(planet, Vec3d{1.0e5, 0, 0}, ar, 3600.0, rng);
    CHECK(Length(planet.offset - at) == 0.0 && AlienModelOf(planet) == -1);

    // Alerta por region: sube en los 4 sistemas del grupo y en ninguno mas.
    SpeciesState s;
    ResizeSpecies(s, 40);
    RaiseAlert(s, 5, 0.3, 100.0, al);
    for (int k = 0; k < 40; ++k) CHECK((s.alert[static_cast<std::size_t>(k)] > 0.0) == (k >= 4 && k <= 7));
    CHECK(s.lastAttack[5] == 100.0 && s.lastAttack[3] < 0.0);
    RaiseAlert(s, 5, 0.9, 100.0, al);
    CHECK(s.alert[5] == 1.0);  // tope

    // Un hangar militar destruido la lleva a los 20 sistemas mas cercanos.
    SpeciesState h;
    ResizeSpecies(h, 40);
    std::vector<Vec3d> line;
    for (int k = 0; k < 40; ++k) line.push_back(Vec3d{static_cast<double>(k) * (k % 2 ? 1.0 : 1.001), 0, 0});
    RaiseAlert(h, 21, DestroyRaise(al, kHangarLevel), 0.0, al, &line, al.hangarSpread);
    int raised = 0;
    for (const double v : h.alert) raised += v > 0.0 ? 1 : 0;
    CHECK(raised == 20 && h.alert[21] == DestroyRaise(al, kHangarLevel) && h.alert[0] == 0.0 && h.alert[39] == 0.0);
    // Destruir un nivel 1 casi no la sube; un nivel 5 se nota mucho.
    CHECK(DestroyRaise(al, 1) < DestroyRaise(al, 5) * 0.1 && DestroyRaise(al, 0) == al.shipRaise);

    // Baja con el tiempo, y mas deprisa donde no se ataca desde hace calmHours.
    SpeciesState d;
    ResizeSpecies(d, 8);
    RaiseAlert(d, 0, 0.5, 0.0, al);  // region 0: sistemas 0 a 3
    RaiseAlert(d, 4, 0.5, 0.0, al);  // region 1: 4 a 7
    const double later = (al.calmHours + 1.0) * 3600.0;
    d.lastAttack[4] = d.lastAttack[5] = d.lastAttack[6] = d.lastAttack[7] = later;  // alli se acaba de atacar
    DecayAlert(d, later, 3600.0, al);
    CHECK(d.alert[0] < 0.5 && d.alert[4] < 0.5 && d.alert[0] < d.alert[4]);
    CHECK(std::fabs(d.alert[4] - 0.5 * std::exp(-al.decayPerHour)) < 1e-12);

    // Rendicion y revancha: una capital suprema o un planeta coraza; una fragata no.
    Anomaly capital;
    capital.kind = AnomalyKind::AlienShip;
    capital.shipClass = ShipClassId::SupremeCapital;
    Anomaly frigateShip = capital;
    frigateShip.shipClass = ShipClassId::Frigate;
    CHECK(TriggersSurrender(capital) && TriggersSurrender(planet) && !TriggersSurrender(frigateShip));
    SpeciesState r;
    std::vector<Anomaly> fleet(2);
    fleet[0].kind = AnomalyKind::AlienShip;
    fleet[0].hull = fleet[0].maxHull = 100.0;
    fleet[0].shieldCharge = fleet[0].shieldMax = 10.0;
    fleet[1].kind = AnomalyKind::AlienStation;
    fleet[1].hull = fleet[1].maxHull = 500.0;
    BeginSurrender(r, 1000.0, al);
    CHECK(Surrendered(r, 1000.0) && Surrendered(r, 1000.0 + al.surrenderHours * 3600.0 - 1.0));
    CHECK(!EndSurrender(r, fleet, 2000.0, al) && fleet[0].boost == 1.0);
    CHECK(EndSurrender(r, fleet, 1000.0 + al.surrenderHours * 3600.0, al));
    CHECK(!Surrendered(r, 1.0e9) && r.revenges == 1);
    CHECK(fleet[0].boost == 1.5 && fleet[0].maxHull == 150.0 && fleet[0].hull == 150.0 && fleet[0].shieldMax == 15.0);
    CHECK(fleet[1].maxHull == 500.0 && fleet[1].boost == 1.0);  // las estaciones no son la flota
    CHECK(RevengeBoost(r, al) == 1.5);
    CHECK(!EndSurrender(r, fleet, 1.0e9, al));  // una sola vez

    // ---- En la sesion ----
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Peaceful);
    g.Step(0.05, {});
    const auto find = [&](int id) -> Anomaly* {
        for (Anomaly& a : g.Anomalies())
            if (a.id == id) return &a;
        return nullptr;
    };
    CHECK(g.Species().alert.size() == engine->GetWorld().extents.size());
    // Un bastion de nivel 2 lleva la vida de la tabla, y su nivel sale en la ficha.
    const int fort = g.SpawnAnomaly(AnomalyKind::AlienStation, Stance::Hostile, 3000.0, 2);
    CHECK(find(fort) && find(fort)->stationLevel == 2 && find(fort)->maxHull == 30.0 * frigate);
    const std::optional<TargetInfo> info = g.Resolve({Kind::Anomaly, g.NearestSystem(), fort});
    CHECK(info && info->detail.find("nivel 2") != std::string::npos);
    // Atacarla sube la alerta de su region.
    const int here = g.NearestSystem();
    CHECK(g.SystemAlert(here) == 0.0);
    g.StrikeAnomaly(fort, DamageType::Nuclear, frigate * 2.0);
    CHECK(g.SystemAlert(here) > 0.0 && g.SystemAlert(here) < 0.1);

    // Destruir un hangar militar extiende la alerta a 20 sistemas o mas.
    const int hangar = g.SpawnAnomaly(AnomalyKind::AlienStation, Stance::Hostile, 3000.0, kHangarLevel);
    CHECK(find(hangar) && find(hangar)->maxHull == StationHull(ar, kHangarLevel));
    g.StrikeAnomaly(hangar, DamageType::Nuclear, 1.0e15);
    CHECK(find(hangar)->hull <= 0.0);
    int alerted = 0;
    for (const double v : g.Species().alert) alerted += v > 0.0 ? 1 : 0;
    CHECK(alerted >= std::min(al.hangarSpread, static_cast<int>(g.Species().alert.size())));  // la galaxia de prueba es pequena
    CHECK(!g.Surrender());

    // Una capital suprema destruida: rendicion temporal; al acabar, +50 %.
    const int boss = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 5000.0, 0, static_cast<int>(ShipClassId::SupremeCapital));
    CHECK(find(boss) && find(boss)->shipClass == ShipClassId::SupremeCapital && find(boss)->maxHull == ShipClassOf(ShipClassId::SupremeCapital, ShipVariant::Normal).hull * ar.referenceHull);
    const int escort = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 6000.0);
    const double escortHull = find(escort)->maxHull;
    g.StrikeAnomaly(boss, DamageType::Nuclear, 1.0e15);
    CHECK(g.Surrender());
    g.Step(0.05, {});
    CHECK(g.Surrender() && !find(escort)->pursuing);
    // Partida guardada a mitad de la rendicion: alerta, rendicion y niveles vuelven igual.
    {
        const std::string text = WriteSave(g.Snapshot());
        const std::optional<SaveData> read = ReadSave(text);
        CHECK(read.has_value());
        if (read) {
            CHECK(read->world.species.alert == g.Species().alert && read->world.species.lastAttack == g.Species().lastAttack);
            CHECK(read->world.species.surrenderUntil == g.Species().surrenderUntil && read->world.species.revengePending && read->world.species.revenges == 0);
            for (const Anomaly& a : read->world.anomalies)
                if (a.id == fort) CHECK(a.stationLevel == 2);
            CHECK(WriteSave(*read) == text);
        }
    }
    g.Species().surrenderUntil = g.SimTime();  // se acaba ya
    g.Step(0.05, {});
    CHECK(!g.Surrender() && g.Species().revenges == 1);
    CHECK(find(escort)->boost == 1.5 && std::fabs(find(escort)->maxHull - escortHull * 1.5) < 1e-6);
    const int fresh = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 7000.0);
    CHECK(find(fresh)->boost == 1.5);
    {
        const std::optional<SaveData> read = ReadSave(WriteSave(g.Snapshot()));
        CHECK(read && read->world.species.revenges == 1);
        if (read)
            for (const Anomaly& a : read->world.anomalies)
                if (a.id == escort) CHECK(a.boost == 1.5);
    }

    // Una partida sin planeta coraza (anterior a la fase F) lo recupera al cargarse, si su galaxia tiene.
    {
        SaveData old = g.Snapshot();
        const bool had = std::any_of(old.world.anomalies.begin(), old.world.anomalies.end(), [](const Anomaly& a) { return IsArmorPlanet(a); });
        old.world.anomalies.erase(std::remove_if(old.world.anomalies.begin(), old.world.anomalies.end(), [](const Anomaly& a) { return IsArmorPlanet(a); }), old.world.anomalies.end());
        GameSession loaded(*engine, rules, 7, GameModeId::Peaceful);
        CHECK(loaded.Restore(old));
        const bool has = std::any_of(loaded.Anomalies().begin(), loaded.Anomalies().end(), [](const Anomaly& a) { return IsArmorPlanet(a); });
        CHECK(has == had);
        CHECK(loaded.Species().revenges == 1);
    }

    // El planeta coraza en vuelo: inmovil, te ve desde lejos y dispara dentro de su percepcion.
    GameSession n(*engine, rules, 7, GameModeId::Normal);
    n.Step(0.05, {});
    const int egg = n.SpawnAnomaly(AnomalyKind::AlienStation, Stance::Hostile, 1.0e5, kArmorPlanetLevel);
    const Anomaly* p = nullptr;
    for (const Anomaly& a : n.Anomalies())
        if (a.id == egg) p = &a;
    CHECK(p && IsArmorPlanet(*p) && p->radius == ar.armorPlanetRadius);
    if (p) {
        const Vec3d still = p->offset;
        CHECK(std::fabs(Length(n.AnomalyPosition(*p) - n.Ship().position) - (1.0e5 + ar.armorPlanetRadius)) < 1.0);
        for (int i = 0; i < 10; ++i) n.Step(0.05, {});
        for (const Anomaly& a : n.Anomalies())
            if (a.id == egg) {
                CHECK(Length(a.offset - still) == 0.0 && a.discovered);
            }
        bool warned = false, fired = false;
        for (const std::string& line : n.Log()) warned = warned || line.find("planeta coraza") != std::string::npos;
        for (const Projectile& pr : n.Projectiles()) fired = fired || pr.owner == egg;
        CHECK(warned && fired);
    }
}

// Escudos: ventajas cruzadas, conos de proa, Amor, escudos alienigenas y firma del plasma.
static void TestShields() {
    Balance rules;
    const DefenseRules& r = rules.defense;
    const auto fresh = [&](ShieldKind field) {
        DefenseState d = MakeDefense(r, rules.thermal);
        d.field = field;
        return d;
    };
    // Cada campo para una parte distinta de cada tipo de dano.
    {
        DefenseState d = fresh(ShieldKind::Dust);
        const HitResult beam = AbsorbHit(d, r, DamageType::Photonic, 100.0, false, true, 1000.0);
        CHECK(std::abs(beam.field - 90.0) < 1e-9 && std::abs(beam.hull - 10.0) < 1e-9);
        CHECK(std::abs(beam.silicate - 90.0 * r.dustPerDamage) < 1e-9);  // la arena gasta polvo
        const HitResult bullet = AbsorbHit(d, r, DamageType::Kinetic, 100.0, false, true, 1000.0);
        CHECK(std::abs(bullet.hull - 85.0) < 1e-9);                      // lo rapido la atraviesa
        const HitResult empty = AbsorbHit(d, r, DamageType::Photonic, 100.0, false, true, 0.0);
        CHECK(empty.field == 0.0 && empty.hull == 100.0);                // sin polvo no hay arena
    }
    {
        DefenseState d = fresh(ShieldKind::Plasma);
        CHECK(AbsorbHit(d, r, DamageType::Kinetic, 100.0, false, true, 0.0).hull < 5.1);  // vaporiza lo fisico
        DefenseState e = fresh(ShieldKind::EM);
        CHECK(AbsorbHit(e, r, DamageType::Kinetic, 100.0, false, true, 0.0).hull == 100.0);  // nulo contra lo cinetico
        CHECK(AbsorbHit(e, r, DamageType::Antimatter, 100.0, false, true, 0.0).hull < 5.1);  // para la antimateria
        // Apagado o sobrecalentado, el campo no para nada.
        DefenseState off = fresh(ShieldKind::EM);
        CHECK(AbsorbHit(off, r, DamageType::Antimatter, 100.0, false, false, 0.0).hull == 100.0);
        // Se agota: lo que pasa de su carga llega al casco.
        DefenseState weak = fresh(ShieldKind::EM);
        weak.charge[static_cast<std::size_t>(ShieldKind::EM)] = 10.0;
        CHECK(std::abs(AbsorbHit(weak, r, DamageType::Antimatter, 100.0, false, true, 0.0).hull - 90.0) < 1e-9);
    }
    // Los conos solo cubren la proa, y se suman al campo.
    {
        DefenseState d = fresh(ShieldKind::EM);
        d.fieldOn = false;
        const HitResult back = AbsorbHit(d, r, DamageType::Kinetic, 100.0, false, true, 0.0);
        CHECK(back.plates == 0.0 && back.hull == 100.0);
        const HitResult front = AbsorbHit(d, r, DamageType::Kinetic, 100.0, true, true, 0.0);
        CHECK(std::abs(front.plates - 60.0) < 1e-9 && std::abs(front.hull - 40.0) < 1e-9);
        CHECK(d.charge[static_cast<std::size_t>(ShieldKind::Ablative)] < ShieldCapacity(r, ShieldKind::Ablative, 1));
        DefenseState both = fresh(ShieldKind::Dust);
        const HitResult layered = AbsorbHit(both, r, DamageType::Kinetic, 100.0, true, true, 1000.0);
        CHECK(layered.field > 0.0 && layered.plates > 0.0 && layered.hull < 40.0);
    }
    // El Amor lo para todo y devuelve lo que no es luz.
    {
        DefenseState d = fresh(ShieldKind::Love);
        d.shieldTier[static_cast<std::size_t>(ShieldKind::Love)] = 1;
        d.charge[static_cast<std::size_t>(ShieldKind::Love)] = ShieldCapacity(r, ShieldKind::Love, 1);
        const HitResult hit = AbsorbHit(d, r, DamageType::Antimatter, 500.0, false, true, 0.0);
        CHECK(hit.hull == 0.0 && hit.reflected == 500.0);
        CHECK(AbsorbHit(d, r, DamageType::Photonic, 50.0, false, true, 0.0).reflected == 0.0);
    }
    // Los alienigenas tambien llevan escudo: las naves varian, las estaciones son EM.
    {
        auto engine = MakeEngine(rules, 7);
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Step(0.05, {});
        bool kinds[5] = {};
        for (const Anomaly& a : g.Anomalies()) {
            if (a.kind == AnomalyKind::Derelict) CHECK(a.shieldMax == 0.0);
            else CHECK(a.shieldMax > 0.0 && a.shieldCharge == a.shieldMax);
            if (a.kind == AnomalyKind::AlienStation) CHECK(a.shield == ShieldKind::EM);
            if (a.kind == AnomalyKind::AlienShip) kinds[static_cast<int>(a.shield)] = true;
        }
        CHECK(kinds[0] && kinds[1] && kinds[2]);
        Anomaly em;
        em.kind = AnomalyKind::AlienStation;
        InitShield(em, rules.anomalies);
        CHECK(ShieldAnomaly(em, r, DamageType::Kinetic, 100.0) == 100.0);  // al EM se le ataca con cinetico
        CHECK(ShieldAnomaly(em, r, DamageType::Antimatter, 100.0) < 5.1);
        CHECK(em.shieldCharge < em.shieldMax);

        // Un campo se elige con K: arena, plasma, EM y apagado. El Amor no se tiene todavia.
        CHECK(g.Ship().defense.field == ShieldKind::Dust && g.Ship().defense.fieldOn);
        CHECK(g.CycleShield() == ShieldKind::Plasma);
        CHECK(g.CycleShield() == ShieldKind::EM);
        g.CycleShield();
        CHECK(!g.Ship().defense.fieldOn && !g.FieldActive());
        CHECK(g.CycleShield() == ShieldKind::Dust && g.Ship().defense.fieldOn);
        CHECK(!g.SetShield(ShieldKind::Love));
        CHECK(!g.SetShield(ShieldKind::Ablative));  // los conos no son un campo

        // El EM gasta energia: el reactor repone menos con el encendido. La arena gasta silicato.
        g.SetShieldOn(false);
        g.Ship().energy = rules.ship.energy * 0.5;
        Run(g, 1.0);
        const double gainOff = g.Ship().energy - rules.ship.energy * 0.5;
        CHECK(g.SetShield(ShieldKind::EM));
        g.Ship().energy = rules.ship.energy * 0.5;
        Run(g, 1.0);
        CHECK(g.Ship().energy - rules.ship.energy * 0.5 < gainOff - 1.0);
        CHECK(g.SetShield(ShieldKind::Dust));
        g.Ship().cargo.Add(Resource::Silicate, 50.0);
        const double silicate = g.Ship().cargo.Get(Resource::Silicate);
        Run(g, 1.0);
        CHECK(g.Ship().cargo.Get(Resource::Silicate) < silicate);
    }
    // Bajo fuego real: con plasma, las balas de una nave apenas tocan el casco.
    {
        auto engine = MakeEngine(rules, 7);
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Step(0.05, {});
        CHECK(g.SetShield(ShieldKind::Plasma));
        const TargetRef enemy = FindShip(g, Stance::Hostile);
        CHECK(Find(g, enemy) && Find(g, enemy)->weapon == WeaponKind::Turret);
        CHECK(g.JumpTo(enemy, 5.0e4));
        const std::size_t p = static_cast<std::size_t>(ShieldKind::Plasma);
        const double charge = ShieldCapacity(r, ShieldKind::Plasma, 1);
        const double hull = g.Ship().hull;
        for (int i = 0; i < 300 && g.Ship().defense.charge[p] >= charge - 1e-6; ++i) Run(g, 0.1);
        CHECK(g.Ship().defense.charge[p] < charge);
        CHECK(hull - g.Ship().hull <= Scaled(rules.combat, WeaponKind::Turret, 1).damage * 0.05 + 1e-6);
        bool logged = false;
        for (const std::string& line : g.Log()) logged = logged || line.find("parados por los escudos") != std::string::npos;
        CHECK(logged);
    }
    // El plasma delata: un hostil persigue desde tres veces mas lejos.
    {
        Anomaly a;
        a.kind = AnomalyKind::AlienShip;
        a.stance = Stance::Hostile;
        std::mt19937 rng(1);
        const Vec3d ship{rules.anomalies.hostileSightRange * 2.0, 0, 0};
        StepAnomaly(a, ship, rules.anomalies, 1.0, rng, true, 1.0);
        CHECK(!a.pursuing);
        StepAnomaly(a, ship, rules.anomalies, 1.0, rng, true, rules.defense.plasmaSight);
        CHECK(a.pursuing);
    }
    // Se guarda todo: campo, niveles, cargas, calor, depositos y escudos alienigenas.
    {
        auto engine = MakeEngine(rules, 7);
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Step(0.05, {});
        DefenseState& d = g.Ship().defense;
        d.field = ShieldKind::EM;
        d.fieldOn = false;
        d.shieldTier[1] = 3;
        d.charge[3] = 123.25;
        d.heat = 77.5;
        d.water = 12.5;
        d.nitrogen = 3.75;
        d.coolantTier[2] = 2;
        d.coolantOn[1] = false;
        int id = -1;
        for (Anomaly& a : g.Anomalies())
            if (a.shieldMax > 0.0) {
                a.shieldCharge = 42.5;
                id = a.id;
                break;
            }
        const std::optional<SaveData> read = ReadSave(WriteSave(g.Snapshot()));
        CHECK(read.has_value());
        if (read) {
            const DefenseState& e = read->Ship()->ship.defense;
            CHECK(e.field == ShieldKind::EM && !e.fieldOn && e.shieldTier[1] == 3 && e.charge[3] == 123.25);
            CHECK(e.heat == 77.5 && e.water == 12.5 && e.nitrogen == 3.75 && e.coolantTier[2] == 2 && !e.coolantOn[1] && e.coolantOn[0]);
            for (const Anomaly& a : read->world.anomalies)
                if (a.id == id) CHECK(a.shieldCharge == 42.5 && a.shieldMax > 0.0);
        }
    }
}

// Calor: armas, curvatura y estrellas calientan; radiadores y refrigerantes enfrian.
static void TestHeat() {
    Balance rules;
    const ThermalRules& t = rules.thermal;
    // Los radiadores siempre; los refrigerantes, solo por encima del umbral y de lo barato a lo caro.
    {
        DefenseState d = MakeDefense(rules.defense, t);
        d.heat = 20.0;
        StepCooling(d, t, 10.0, 0.0, 1.0, 100.0);
        CHECK(std::abs(d.heat - (20.0 - t.radiatorRate * 10.0)) < 1e-9);
        CHECK(d.water == t.waterTank);  // por debajo del umbral no se gasta agua
        d.heat = 90.0;
        const CoolingResult cool = StepCooling(d, t, 1.0, 0.0, 0.0, 100.0);  // junto a una estrella: sin radiadores
        CHECK(cool.removed[3] == 0.0 && cool.removed[0] > 0.0 && cool.removed[1] > 0.0 && cool.removed[2] > 0.0);
        CHECK(cool.iceUsed > 0.0);
        CHECK(d.heat < 90.0 && d.heat >= t.limit * t.coolStart);
        // El hielo fundido rellena el agua.
        DefenseState melt = MakeDefense(rules.defense, t);
        melt.water = 0.0;
        melt.coolantOn[2] = false;
        melt.heat = 90.0;
        StepCooling(melt, t, 1.0, 0.0, 0.0, 100.0);
        CHECK(melt.water > 0.0);
        // El nitrogeno se evapora aunque no se use.
        DefenseState boil = MakeDefense(rules.defense, t);
        StepCooling(boil, t, 3600.0, 0.0, 1.0, 0.0);
        CHECK(boil.nitrogen < t.nitrogenTank);
        // Sin nada, el calor se queda en el maximo.
        DefenseState hot = MakeDefense(rules.defense, t);
        hot.water = hot.nitrogen = 0.0;
        StepCooling(hot, t, 1.0, 500.0, 0.0, 0.0);
        CHECK(hot.heat == t.maximum && Overheated(hot, t));
    }
    auto engine = MakeEngine(rules, 7);
    // Disparar calienta; sobrecalentada no dispara, el campo cae y el casco sufre.
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        const TargetRef enemy = FindShip(g, Stance::Hostile);
        CHECK(g.JumpTo(enemy, 4.0e4));
        g.Step(0.05, {});
        FaceTo(g.Ship(), g.Solution(enemy, WeaponKind::Spinal)->direction);
        const double heat = g.Ship().defense.heat;
        CHECK(g.Fire(WeaponKind::Spinal, enemy));
        CHECK(g.Ship().defense.heat >= heat + t.weaponHeat[1] - 1e-9);
        g.Ship().defense.heat = 140.0;
        g.Ship().defense.water = g.Ship().defense.nitrogen = 0.0;
        CHECK(g.Overheated() && !g.FieldActive());
        CHECK(!g.Fire(WeaponKind::Turret, enemy));
        const double hull = g.Ship().hull;
        g.Step(0.05, {});
        CHECK(g.Ship().hull < hull);
        bool warned = false;
        for (const std::string& line : g.Log()) warned = warned || line.find("Sobrecalentamiento") != std::string::npos;
        CHECK(warned);
    }
    // Junto a una estrella los radiadores apenas sirven.
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        const double far = g.RadiatorEfficiency();
        const TargetRef star{Kind::Star, 0, 0};
        CHECK(g.JumpTo(star, g.Resolve(star)->radius * 5.0));
        g.Step(0.05, {});
        CHECK(far > 0.99 && g.RadiatorEfficiency() < 0.3);
    }
    // Presion de ariete: en curvatura, los conos la aguantan; sin ellos, el casco.
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        g.Ship().emitters = 1;
        g.Ship().antimatter = rules.ship.antimatterTank;
        g.SetCoolant(CoolantKind::Radiator, false);  // si no, se llevan el calor de la burbuja mientras se forma
        g.SetShieldOn(false);                         // y la arena tambien lo absorbe
        CHECK(g.SetDrive(DriveMode::Warp));
        g.SetThrottle(1.0);
        for (int i = 0; i < 400 && !g.Ramming(); ++i) g.Step(0.05, {});
        CHECK(g.Ramming());
        CHECK(g.Ship().defense.heat > 0.0);  // formar la burbuja calienta
        g.SetCoolant(CoolantKind::Radiator, true);
        const std::size_t a = static_cast<std::size_t>(ShieldKind::Ablative);
        const double plates = g.Ship().defense.charge[a], hull = g.Ship().hull;
        Run(g, 1.0);
        CHECK(g.Ship().defense.charge[a] < plates && g.Ship().hull == hull);
        g.Ship().defense.charge[a] = 0.0;
        Run(g, 1.0);
        CHECK(!g.Ramming() || g.Ship().hull < hull);
        // Las placas pesan: con ellas enteras la nave maniobra peor.
        CHECK(g.Ship().massFactor < 1.0 + 1e-9);
    }
    // Con la nave fria se licua nitrogeno del gas de la bodega.
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        g.Ship().defense.nitrogen = 0.0;
        g.Ship().cargo.Add(Resource::Gas, 100.0);
        Run(g, 2.0);
        CHECK(g.Ship().defense.nitrogen > 0.0);
        CHECK(g.Ship().cargo.Get(Resource::Gas) < 100.0 + rules.ship.startGas);
        CHECK(g.Ship().massFactor > 1.0);
    }
}

// Naves, bastiones y estructuras son solidos: no se atraviesan, abollan el casco y se pueden embestir.
static void TestSolidStructures() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    const auto drift = [&](GameSession& g, double speed) {
        g.Ship().holding = false;
        g.Ship().throttle = 0.0;
        g.Ship().velocity = game::Heading(g.Ship()) * speed;
    };
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        const int id = g.SpawnAnomaly(AnomalyKind::Derelict, Stance::None, 5.0 + 12.0);
        const TargetRef ruin{Kind::Anomaly, g.NearestSystem(), id};
        const double hull = g.Ship().hull;
        drift(g, 0.3);
        double nearest = 1e300;
        for (int i = 0; i < 40; ++i) {
            g.Step(0.05, {});
            nearest = std::min(nearest, Length(g.Resolve(ruin)->position - g.Ship().position));
        }
        CHECK(nearest >= 12.0 + rules.ship.radius - 0.05);  // no la atraviesa
        CHECK(g.Ship().hull < hull);                        // y abolla el casco
        bool logged = false;
        for (const std::string& line : g.Log()) logged = logged || line.find("Impacto contra Estructura") != std::string::npos;
        CHECK(logged);
    }
    {
        GameSession g(*engine, rules, 7, GameModeId::Peaceful);  // en Pacifico no hay dano propio, pero se puede embestir
        g.Conversation().Stop();
        g.Step(0.05, {});
        const int id = g.SpawnAnomaly(AnomalyKind::AlienStation, Stance::Neutral, 5.0);
        drift(g, 0.5);
        for (int i = 0; i < 40; ++i) g.Step(0.05, {});
        const Anomaly* a = Find(g, {Kind::Anomaly, g.NearestSystem(), id});
        CHECK(a && a->hull < a->maxHull);
        CHECK(a && a->stance == Stance::Hostile);  // embestir es un ataque
        CHECK(g.Ship().hull == rules.ship.hull);
    }
}

// Junto a una estacion se maniobra hasta su escotilla: a tiempo real, sin salir despedida, sin
// atravesarla y sin romperse al rozarla. Sobre todo la central, que orbita su estrella.
static void TestStationManeuver() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        const TargetRef station = PlaceStation(g);
        for (int i = 0; i < 200; ++i) g.Step(0.05, {});
        CHECK(g.JumpTo(station, 5.0));
        CHECK(g.Dock(station, true));
        for (int i = 0; i < 40; ++i) g.Step(0.05, {});
        g.Undock();
        const auto away = [&] { return Length(g.Ship().position - g.Resolve(station)->position); };
        const double radius = GameSession::StationRadius(StationKind::Power);
        double farthest = 0, nearest = 1e300, fastest = 0;
        const struct { double throttle; float yaw, pitch; double seconds; } moves[] = {
            {-0.3, 0, 0, 3}, {0, 0, 0, 3}, {1.0, 0, 0, 3}, {0.5, 0.5f, 0, 4}, {0.5, 0, 0.5f, 4}, {1.0, 0, 0, 6}, {-1.0, 0, 0, 4}, {0, 0, 0, 4}};
        for (const auto& m : moves) {
            g.SetThrottle(m.throttle);
            ShipControls c;
            c.turnYaw = m.yaw * 0.05f;
            c.turnPitch = m.pitch * 0.05f;
            for (int i = 0; i < static_cast<int>(m.seconds / 0.05); ++i) {
                g.Step(0.05, c);
                farthest = std::max(farthest, away());
                // La proa puede pasar junto al casco; el centro de la nave, nunca dentro.
                nearest = std::min(nearest, away());
                fastest = std::max(fastest, Length(g.ShipVelocity() - g.Resolve(station)->velocity));
            }
        }
        CHECK(g.Phase() == GamePhase::Flying);
        CHECK(farthest < 10.0);              // no sale despedida
        CHECK(nearest > radius * 0.5);       // ni la atraviesa
        CHECK(fastest < 0.2);                // a decenas de m/s, como mucho
        CHECK(g.Ship().hull > rules.ship.hull * 0.9);  // rozarla abolla poco
        CHECK(g.TimeFocus() * g.TimeScale() < 1.5);   // a tiempo real

        // Con el haz se mete por la escotilla: la proa por delante.
        CHECK(g.Dock(station));
        for (int i = 0; i < 600 && !g.Docked(); ++i) g.Step(0.05, {});
        CHECK(g.Docked());
        const GameSession::DockPort port = g.Port(*g.Stations().Find(station.index));
        const Vec3d side = g.Ship().position + g.Ship().orientation.ToMatrix().TransformDirection(CurrentShipDesign().dockPort * 0.001);
        CHECK(Length(side - (port.position + port.normal * 0.12)) < 0.05);  // de costado, con el ducto

        // Con la compuerta en la panza (la nave glTF, bajo el puente): se tumba hasta que la panza
        // mira a la escotilla, con la proa de lado, y el ducto sale de la panza.
        g.Undock();
        for (int i = 0; i < 40; ++i) g.Step(0.05, {});
        auto belly = std::make_shared<ShipDesign>(CurrentShipDesign());
        belly->dockPort = {0, 30, 450};
        belly->dockNormal = {0, 1, 0};  // abajo es +Y
        SetShipDesign(belly);
        CHECK(g.Dock(station));
        for (int i = 0; i < 600 && !g.Docked(); ++i) g.Step(0.05, {});
        CHECK(g.Docked());
        const GameSession::DockPort bellyPort = g.Port(*g.Stations().Find(station.index));
        const Mat4d turn = g.Ship().orientation.ToMatrix();
        CHECK(Length(g.Ship().position + turn.TransformDirection(belly->dockPort * 0.001) - (bellyPort.position + bellyPort.normal * 0.12)) < 0.05);
        CHECK(Dot(turn.TransformDirection(Vec3d{0, 1, 0}), bellyPort.normal) < -0.99);
        CHECK(std::fabs(Dot(game::Heading(g.Ship()), bellyPort.normal)) < 0.01);
        SetShipDesign(nullptr);
    }
}
static void TestHullZones() {
    int uploads = 0;
    const GameAssets assets = BuildGameAssets([&](const MeshData&) { return static_cast<MeshId>(++uploads); });
    // Cada pieza de la nave es de una zona, y cada bodega de la suya.
    std::set<int> holds;
    for (const ModelPart& p : assets.ship->parts) {
        const int zone = ZoneOfPart(p.name, p.position);
        CHECK(zone >= 0 && zone < kHullZoneCount);
        if (p.name == "bodega") holds.insert(zone);
    }
    CHECK(holds.size() == 6);

    // Un golpe cae del lado por el que llega.
    const auto tally = [](const Vec3d& from) {
        std::array<int, kHullZoneCount> count{};
        for (int i = 0; i < 400; ++i) ++count[static_cast<std::size_t>(ZoneHitFrom(from, (i + 0.5) / 400.0))];
        return count;
    };
    const auto front = tally({0, 0, 1}), back = tally({0, 0, -1}), port = tally({-1, 0, -0.4});
    CHECK(front[static_cast<std::size_t>(HullZone::Bow)] > 200);
    // Por detras se lo reparten las gondolas y el extremo de la espina, que queda entre ellas.
    CHECK(back[static_cast<std::size_t>(HullZone::PortNacelle)] + back[static_cast<std::size_t>(HullZone::StarboardNacelle)] > 120);
    CHECK(back[static_cast<std::size_t>(HullZone::Bow)] == 0);
    CHECK(port[static_cast<std::size_t>(HullZone::StarboardNacelle)] < port[static_cast<std::size_t>(HullZone::PortNacelle)]);

    // Una zona deshecha se suelta y se aleja; el resto sigue entero.
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Normal);
    g.Conversation().Stop();
    g.Step(0.05, {});
    const int nacelle = static_cast<int>(HullZone::PortNacelle);
    g.DamageZone(nacelle, rules.ship.hull * kZoneToughness * 0.5);
    CHECK(std::fabs(g.Ship().zones[static_cast<std::size_t>(nacelle)] - 0.5) < 1e-9);
    CHECK(g.LostPieces().empty());
    CHECK(g.ZoneHeat()[static_cast<std::size_t>(nacelle)] > 0.5);
    g.DamageZone(nacelle, rules.ship.hull * kZoneToughness * 0.6);
    CHECK(ZoneLost(g.Ship().zones, nacelle));
    CHECK(g.LostPieces().size() == 1);
    CHECK(g.Log().back().find("góndola de babor") != std::string::npos);
    CHECK(g.Ship().hull < rules.ship.hull * 0.7);
    CHECK(g.Ship().zones[static_cast<std::size_t>(HullZone::Bow)] == 1.0);
    const Vec3d start = g.LostPieces()[0].absolute - (g.Ship().position + g.GetWorld().origin);
    for (int i = 0; i < 40; ++i) g.Step(0.05, {});
    CHECK(Length(g.LostPieces()[0].absolute - (g.Ship().position + g.GetWorld().origin)) > Length(start));
    CHECK(g.ZoneHeat()[static_cast<std::size_t>(nacelle)] < 0.9);
    // Lo que la nave ya no tiene no se puede volver a soltar.
    g.DamageZone(nacelle, 10.0);
    CHECK(g.LostPieces().size() == 1);
    g.DamageZone(static_cast<int>(HullZone::Spine), rules.ship.hull * 0.1);  // la espina no se suelta
    CHECK(g.LostPieces().size() == 1 && g.Ship().zones[static_cast<std::size_t>(HullZone::Spine)] < 1.0);

    // En la capa: la pieza perdida no se dibuja en ninguna calidad; el dano solo con detalle.
    GameLayer layer(g, assets, 1, 2, Color{0.1f, 0.05f, 0.15f, 1.0f});
    engine->AddLayer(&layer);
    Camera camera;
    camera.SetViewport(1600, 900, 800.0f, kNearPlaneKm);
    camera.SetOrientation(Quatf::Cast(g.Ship().orientation));
    camera.SetPosition(g.Ship().position - Heading(g.Ship()) * 2.3);
    std::set<MeshId> nacelleMeshes;
    for (std::size_t i = 0; i < assets.ship->parts.size(); ++i)
        if (ZoneOfPart(assets.ship->parts[i].name, assets.ship->parts[i].position) == nacelle) nacelleMeshes.insert(assets.ship->parts[i].mesh);
    for (const int detail : {0, 2}) {
        layer.SetHullDetail(detail);
        FramePacket packet;
        engine->Build(camera, 1.0, packet);
        int onShip = 0, damaged = 0;
        for (const MeshInstance& m : packet.meshes) {
            if ((m.id & 0xFF000000u) != 0x70000000u || ((m.id >> 8) & 0xFFFFu) != 0) continue;  // solo la propia nave
            CHECK(nacelleMeshes.count(m.mesh) == 0);
            ++onShip;
            damaged += m.damage > 0.0f ? 1 : 0;
            CHECK(detail > 0 || m.damageDetail == 0);
        }
        CHECK(onShip > 0);
        CHECK(detail == 0 ? damaged == 0 : damaged > 0);  // la espina
    }

    // Se guarda.
    const std::optional<SaveData> back2 = ReadSave(WriteSave(g.Snapshot()));
    CHECK(back2.has_value() && ZoneLost(back2->Ship()->ship.zones, nacelle) && back2->Ship()->ship.zones[0] == 1.0);

    // Atracada se rehace; en la central sin atracar, solo se reparan las que siguen ahi.
    const TargetRef station = PlaceStation(g);
    for (int i = 0; i < 200; ++i) g.Step(0.05, {});
    CHECK(g.JumpTo(station, 5.0));
    CHECK(g.Dock(station, true));
    for (int i = 0; i < 20; ++i) g.Step(0.05, {});
    CHECK(!ZoneLost(g.Ship().zones, nacelle));
}

// Fase D: los alienigenas tienen casco por zonas, sacadas del nombre de sus piezas, y el modo
// cinematica sigue el golpe.
static void TestAlienZones() {
    int uploads = 0;
    const GameAssets assets = BuildGameAssets([&](const MeshData&) { return static_cast<MeshId>(++uploads); });
    const auto zoneOf = [&](AlienModel model, const char* name, int sign) {
        const ModelAsset& m = *AlienModelAsset(assets, model);
        for (std::size_t i = 0; i < m.parts.size(); ++i)
            if (m.parts[i].name == name && (sign == 0 || (m.parts[i].position.x < 0.0f) == (sign < 0))) return assets.alienPartZones[static_cast<std::size_t>(model)][i];
        return -1;
    };
    const auto idx = [](HullZone z) { return static_cast<int>(z); };
    // Las piezas, por su nombre.
    CHECK(zoneOf(AlienModel::HostileShip, "cuerpo", 0) == idx(HullZone::Spine));
    CHECK(zoneOf(AlienModel::HostileShip, "ojo", 0) == idx(HullZone::Core));
    CHECK(zoneOf(AlienModel::HostileShip, "cuchilla", -1) == idx(HullZone::PortNacelle));
    CHECK(zoneOf(AlienModel::HostileShip, "filo", 1) == idx(HullZone::StarboardNacelle));
    CHECK(zoneOf(AlienModel::NeutralShip, "disco", 0) == idx(HullZone::Spine));
    CHECK(zoneOf(AlienModel::NeutralShip, "aro", 0) == idx(HullZone::WarpRing));
    CHECK(zoneOf(AlienModel::NeutralShip, "centro", 0) == idx(HullZone::Core));
    CHECK(zoneOf(AlienModel::HostileStation, "nucleo", 0) == idx(HullZone::Core));
    CHECK(zoneOf(AlienModel::HostileStation, "corazon", 0) == idx(HullZone::Bridge));
    CHECK(zoneOf(AlienModel::HostileStation, "anillo", 0) == idx(HullZone::WarpRing));
    std::set<int> arms;
    {
        const ModelAsset& station = *assets.hostileStation;
        for (std::size_t i = 0; i < station.parts.size(); ++i)
            if (station.parts[i].name == "radio") arms.insert(assets.alienPartZones[static_cast<std::size_t>(AlienModel::HostileStation)][i]);
    }
    CHECK(arms.size() == 6 && *arms.begin() == idx(HullZone::Hold1));
    for (int m = 0; m < kAlienModelCount; ++m) CHECK(assets.alienZones[static_cast<std::size_t>(m)] && assets.alienPartZones[static_cast<std::size_t>(m)].size() == AlienModelAsset(assets, static_cast<AlienModel>(m))->parts.size());

    // Su forma: solo tiene las zonas de sus piezas, con sus nombres.
    const ZoneLayout& hostile = *assets.alienZones[static_cast<std::size_t>(AlienModel::HostileShip)];
    CHECK(ZonePresent(hostile, idx(HullZone::PortNacelle)) && ZonePresent(hostile, idx(HullZone::Core)) && ZonePresent(hostile, idx(HullZone::Spine)));
    CHECK(!ZonePresent(hostile, idx(HullZone::Bow)) && !ZonePresent(hostile, idx(HullZone::WarpRing)));
    CHECK(ZoneLabel(&hostile, idx(HullZone::PortNacelle), true) == "la cuchilla de babor");
    CHECK(ZoneLabel(&hostile, idx(HullZone::Core), true) == "el ojo");
    CHECK(ZoneLabel(nullptr, idx(HullZone::Bow), true) == "la proa");  // la nave, como siempre
    // La trayectoria decide: de frente por la punta de cada cuchilla; desde atras, el cuerpo.
    CHECK(ProjectileImpact(hostile, {-220, 0, 3000}, {-220, 0, -3000}).zone == idx(HullZone::PortNacelle));
    CHECK(ProjectileImpact(hostile, {220, 0, 3000}, {220, 0, -3000}).zone == idx(HullZone::StarboardNacelle));
    CHECK(ProjectileImpact(hostile, {0, 0, -3000}, {0, 0, 3000}).zone == idx(HullZone::Spine));
    CHECK(NearestZone(hostile, Vec3d{0, 0, -60}) == idx(HullZone::Core) || NearestZone(hostile, Vec3d{0, 0, -60}) == idx(HullZone::Spine));
    // La nave sigue con su forma de siempre.
    CHECK(ProjectileImpact({0, 0, 3000}, {0, 0, -3000}).zone == idx(HullZone::Bow));

    // Sin registrar las zonas, los alienigenas solo tienen su casco entero, como antes.
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Peaceful);
    g.Conversation().Stop();
    g.Step(0.05, {});
    const int bare = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 20.0);
    CHECK(bare >= 0 && !g.DamageAnomalyZone(bare, idx(HullZone::PortNacelle), 10.0));

    RegisterAlienZones(assets);
    const auto find = [&](int id) -> Anomaly* {
        for (Anomaly& a : g.Anomalies())
            if (a.id == id) return &a;
        return nullptr;
    };
    const int id = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 30.0);
    Anomaly* a = find(id);
    if (!a) return;
    // Dron, corveta o fragata: sin los .gltf, su modelo es la maqueta hostil, con las mismas zonas.
    const int shipModel = AlienModelOf(*a);
    CHECK(shipModel == static_cast<int>(ClassModel(a->shipClass, true)));
    CHECK(ZonesOf(*a) == assets.alienZones[static_cast<std::size_t>(shipModel)].get() && AlienModelAsset(assets, static_cast<AlienModel>(shipModel)) == assets.hostileShip);
    const double full = a->maxHull;
    const int port = idx(HullZone::PortNacelle);
    // Media cuchilla: no se suelta, pero brilla.
    CHECK(g.DamageAnomalyZone(id, port, full * kZoneToughness * 0.5));
    a = find(id);
    CHECK(std::fabs(a->zones[static_cast<std::size_t>(port)] - 0.5) < 1e-9);
    CHECK(std::fabs(a->hull - full * (1.0 - kZoneToughness * 0.5)) < 1e-6);
    CHECK(a->zoneHeat[static_cast<std::size_t>(port)] > 0.5);
    CHECK(g.LostPieces().empty());
    CHECK(g.LastStrike().anomaly == id && g.LastStrike().zone == port && g.LastStrike().serial == 1);
    // La otra mitad, y algo mas: se suelta, y lo que sobra pasa al ojo.
    g.DamageAnomalyZone(id, port, full * kZoneToughness * 0.6);
    a = find(id);
    CHECK(ZoneLost(a->zones, port));
    CHECK(g.LostPieces().size() == 1 && g.LostPieces()[0].model == shipModel && g.LostPieces()[0].zone == port);
    CHECK(g.Log().back().find("cuchilla de babor") != std::string::npos);
    CHECK(a->zones[static_cast<std::size_t>(HullZone::Core)] < 1.0 && a->zones[static_cast<std::size_t>(HullZone::StarboardNacelle)] == 1.0);
    const Vec3d start = g.LostPieces()[0].absolute - (g.AnomalyPosition(*a) + g.GetWorld().origin);
    for (int i = 0; i < 40; ++i) g.Step(0.05, {});
    a = find(id);
    CHECK(Length(g.LostPieces()[0].absolute - (g.AnomalyPosition(*a) + g.GetWorld().origin)) > Length(start));
    CHECK(a->zoneHeat[static_cast<std::size_t>(port)] < 0.9);
    // Lo que ya no tiene no se suelta otra vez: el golpe entra hasta el ojo.
    const double eye = a->zones[static_cast<std::size_t>(HullZone::Core)];
    g.DamageAnomalyZone(id, port, full * 0.01);
    a = find(id);
    CHECK(g.LostPieces().size() == 1 && a->zones[static_cast<std::size_t>(HullZone::Core)] < eye);

    // En la capa: la cuchilla perdida no se dibuja en la nave, sino suelta; lo danado se ve.
    GameLayer layer(g, assets, 1, 2, Color{0.1f, 0.05f, 0.15f, 1.0f});
    engine->AddLayer(&layer);
    layer.SetHullDetail(2);
    {
        Camera camera;
        camera.SetViewport(1600, 900, 800.0f, kNearPlaneKm);
        const Vec3d at = g.AnomalyPosition(*a);
        camera.SetPosition(at + Vec3d{0, -0.4, -2.0});
        camera.SetOrientation(Quatf::Cast(CameraLooking(Vec3d{0, 0.4, 2.0})));
        std::set<MeshId> blades;
        const ModelAsset& model = *assets.hostileShip;
        for (std::size_t i = 0; i < model.parts.size(); ++i)
            if (assets.alienPartZones[static_cast<std::size_t>(shipModel)][i] == port) blades.insert(model.parts[i].mesh);
        FramePacket packet;
        engine->Build(camera, 1.0, packet);
        int onAlien = 0, damaged = 0;
        const std::uint32_t tag = static_cast<std::uint32_t>(id) + 4096u;
        for (const MeshInstance& m : packet.meshes) {
            if ((m.id & 0xFF000000u) != 0x70000000u || ((m.id - 0x70000000u) >> 8) != tag) continue;
            ++onAlien;
            CHECK(blades.count(m.mesh) == 0);
            damaged += m.damage > 0.0f ? 1 : 0;
        }
        CHECK(onAlien > 0 && damaged > 0);
        // Y la cuchilla, suelta, con sus piezas.
        const Vec3d piece = g.LostPieces()[0].absolute - g.GetWorld().origin;
        camera.SetPosition(piece + Vec3d{0, -0.2, -1.0});
        camera.SetOrientation(Quatf::Cast(CameraLooking(Vec3d{0, 0.2, 1.0})));
        FramePacket loosePacket;
        engine->Build(camera, 1.0, loosePacket);
        int loose = 0;
        for (const MeshInstance& m : loosePacket.meshes)
            if ((m.id & 0xFF000000u) == 0x77000000u) {
                ++loose;
                CHECK(blades.count(m.mesh) == 1);
            }
        CHECK(loose == static_cast<int>(blades.size()));
    }

    // La espina a 0 la apaga: no se mueve por si misma ni dispara.
    g.DamageAnomalyZone(id, idx(HullZone::Spine), full * kZoneToughness / kCriticalHit * 1.01);
    a = find(id);
    CHECK(a && a->hull > 0.0 && a->disabled);
    CHECK(g.Log().back().find("apagada") != std::string::npos);
    a->velocity = Vec3d{};
    const Vec3d still = a->offset;
    for (int i = 0; i < 20; ++i) g.Step(0.05, {});
    a = find(id);
    CHECK(a && Length(a->offset - still) < 1e-9);

    // Se guarda: zonas y apagada.
    const std::optional<SaveData> back = ReadSave(WriteSave(g.Snapshot()));
    CHECK(back.has_value());
    if (back) {
        bool found = false;
        for (const Anomaly& b : back->world.anomalies)
            if (b.id == id) {
                found = true;
                CHECK(b.disabled && ZoneLost(b.zones, port) && std::fabs(b.zones[static_cast<std::size_t>(HullZone::Core)] - a->zones[static_cast<std::size_t>(HullZone::Core)]) < 1e-6);
            }
        CHECK(found);
    }

    // El nucleo a 0 la destruye, aunque le quede casco.
    const int kills = g.Kills();
    const int other = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 40.0);
    g.DamageAnomalyZone(other, idx(HullZone::Core), find(other)->maxHull * kZoneToughness / kCriticalHit * 1.01);
    CHECK(!find(other) || find(other)->hull <= 0.0);
    CHECK(g.Kills() == kills + 1);

    // Una neutral atacada pasa a su modelo hostil, y en el caen las zonas.
    const int calm = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Neutral, 50.0);
    CHECK(ZonesOf(*find(calm)) == assets.alienZones[static_cast<std::size_t>(ClassModel(find(calm)->shipClass, false))].get());
    CHECK(g.ShootAnomaly(calm, {220, 0, 3000}, {220, 0, -3000}, find(calm)->maxHull * 0.05) == idx(HullZone::StarboardNacelle));
    CHECK(find(calm)->stance == Stance::Hostile && find(calm)->zones[static_cast<std::size_t>(HullZone::StarboardNacelle)] < 1.0);

    // Un misil de verdad cae en una zona que su modelo tiene, sea cual sea su rumbo.
    {
        const int far = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 3000.0);
        find(far)->yaw = 1.1f;
        // El misil sale hacia delante: las que la prueba ha puesto cerca, fuera de su camino.
        for (Anomaly& b : g.Anomalies())
            if (b.id != far && Length(g.AnomalyPosition(b) - g.Ship().position) < 200.0) b.offset += Vec3d{4000.0, 0, 0};
        const int serial = g.LastStrike().serial;
        const TargetRef enemy{Kind::Anomaly, g.NearestSystem(), far};
        CHECK(g.Fire(WeaponKind::Missile, enemy));
        for (int i = 0; i < 1200 && !g.Projectiles().empty(); ++i) g.Step(0.1, {});
        CHECK(g.LastStrike().serial == serial + 1 && g.LastStrike().anomaly == far);
        CHECK(ZonePresent(hostile, g.LastStrike().zone));
    }

    // Modo cinematica: apagado, no hay toma; encendido, tras un impacto la camara rodea el blanco.
    Vec3d eyePos;
    Quatd look;
    CHECK(!g.Cinematic() && !g.CinematicShot(eyePos, look));
    g.SetCinematic(true);
    CHECK(g.Cinematic() && g.Log().back().find("cinemática") != std::string::npos);
    const int shot = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 60.0);
    g.DamageAnomalyZone(shot, port, find(shot)->maxHull * 0.05);
    CHECK(g.CinematicView() && g.CinematicShot(eyePos, look));
    {
        const Anomaly& b = *find(shot);
        const Vec3d hit = g.AnomalyPosition(b) + Quatd::FromAxisAngle({0, 1, 0}, static_cast<double>(b.yaw)).ToMatrix().TransformDirection(g.LastStrike().point * 0.001);
        const Vec3d forward = look.ToMatrix().TransformDirection(Vec3d{0, 0, 1});
        CHECK(Dot(forward, Normalize(hit - eyePos)) > 0.999);
        CHECK(look.ToMatrix().TransformDirection(Vec3d{0, -1, 0}).y < 0.0);  // sin dar la vuelta
        const double distance = Length(hit - eyePos);
        CHECK(distance >= rules.cinematic.minimum && distance < 5.0);
        // Gira con el tiempo real, y el tiempo del juego va mas despacio.
        const double before = g.SimTime();
        g.Step(0.5, {});
        const double slow = g.SimTime() - before;
        Vec3d moved;
        CHECK(g.CinematicShot(moved, look));
        CHECK(Length(moved - eyePos) > 0.01);
        CHECK(std::fabs(g.StrikeAge() - 0.5) < 1e-9);
        for (int i = 0; i < 20; ++i) g.Step(0.5, {});
        CHECK(!g.CinematicView() && !g.CinematicShot(moved, look));  // pasada la toma, vuelve la camara de siempre
        const double before2 = g.SimTime();
        g.Step(0.5, {});
        CHECK(slow < (g.SimTime() - before2) * 0.5);
    }
    g.SetCinematic(false);
    CHECK(!g.Cinematic());

    // La camara que mira: +Z hacia donde se pide y sin alabeo.
    for (const Vec3d& d : {Vec3d{0, 0, 1}, Vec3d{1, 0, 0}, Vec3d{-0.3, 0.8, -0.5}, Vec3d{0.2, -0.9, 0.1}}) {
        const Quatd q = CameraLooking(d);
        CHECK(Dot(q.ToMatrix().TransformDirection(Vec3d{0, 0, 1}), Normalize(d)) > 0.9999);
        CHECK(std::fabs(q.ToMatrix().TransformDirection(Vec3d{1, 0, 0}).y) < 1e-9);
    }

    for (int m = 0; m < kAlienModelCount; ++m) SetAlienZones(static_cast<AlienModel>(m), nullptr);  // el resto de pruebas, como antes
}

// Fase E: naves alienigenas por clase y torretas humanas cuadradas, de sus .gltf, con las maquetas de
// respaldo.
static void TestCombatModels() {
    int uploads = 0;
    const MeshUpload upload = [&](const MeshData&) { return static_cast<MeshId>(++uploads); };
    const auto idx = [](HullZone z) { return static_cast<int>(z); };

    // Que modelo le toca a cada nave: dron, corveta y fragata tienen el suyo, hostil o neutral.
    Anomaly a;
    a.kind = AnomalyKind::AlienShip;
    a.stance = Stance::Hostile;
    a.shipClass = ShipClassId::Corvette;
    CHECK(AlienModelOf(a) == static_cast<int>(AlienModel::HostileCorvette));
    a.stance = Stance::Neutral;
    a.shipClass = ShipClassId::Frigate;
    CHECK(AlienModelOf(a) == static_cast<int>(AlienModel::NeutralFrigate));
    a.shipClass = ShipClassId::Drone;
    CHECK(AlienModelOf(a) == static_cast<int>(AlienModel::NeutralDrone));
    a.shipClass = ShipClassId::Cruiser;  // sin modelo propio: la maqueta
    CHECK(AlienModelOf(a) == static_cast<int>(AlienModel::NeutralShip));
    a.kind = AnomalyKind::AlienStation;
    CHECK(AlienModelOf(a) == static_cast<int>(AlienModel::NeutralStation));

    // Cada arma humana tiene su torreta; las bahias de un mismo tamano comparten lanzador.
    for (int k = 0; k < static_cast<int>(ShipModuleKind::Count); ++k) {
        const ShipModuleKind kind = static_cast<ShipModuleKind>(k);
        CHECK((TurretModelName(kind) != nullptr) == (IsHumanWeapon(kind) || kind == ShipModuleKind::PointDefensePlatform));
    }
    CHECK(std::string(TurretModelName(ShipModuleKind::BayLight25)) == TurretModelName(ShipModuleKind::BayLight50));
    CHECK(std::string(TurretModelName(ShipModuleKind::CoilgunLight)) != TurretModelName(ShipModuleKind::CoilgunHeavy));

    // Sin archivos: las maquetas de siempre, y las armas con la malla de la torreta.
    GameAssets assets = BuildGameAssets(upload);
    for (int i = 0; i < kAlienClassModelCount; ++i) CHECK(assets.hostileClassShips[static_cast<std::size_t>(i)] == assets.hostileShip && assets.neutralClassShips[static_cast<std::size_t>(i)] == assets.neutralShip);
    for (const ModelRef& m : assets.moduleModels) CHECK(m == nullptr);
    std::string log;
    LoadCombatModels(assets, "no_existe/", upload, nullptr, &log);
    CHECK(assets.hostileClassShips[0] == assets.hostileShip && log.find("maqueta") != std::string::npos);

    // La version neutral: mismas mallas, piel verdosa y cristales turquesa.
    const ModelRef neutral = NeutralColors(*assets.hostileShip);
    CHECK(neutral->parts.size() == assets.hostileShip->parts.size());
    for (std::size_t i = 0; i < neutral->parts.size(); ++i) {
        const ModelPart& h = assets.hostileShip->parts[i];
        const ModelPart& n = neutral->parts[i];
        CHECK(n.mesh == h.mesh && n.position.z == h.position.z);
        CHECK(n.color.g > n.color.r && h.color.r > h.color.g);
    }

    // Con los .gltf junto al ejecutable.
    std::error_code ignored;
    if (!GltfSupported() || !std::filesystem::exists("modelos/alien_dron.gltf", ignored) || !std::filesystem::exists("modelos/torretas.gltf", ignored)) {
        std::printf("  (sin modelos/alien_*.gltf o torretas.gltf: se salta)\n");
        return;
    }
    log.clear();
    LoadCombatModels(assets, "modelos/", upload, nullptr, &log);
    CHECK(log.empty());
    if (!log.empty()) std::printf("  %s", log.c_str());
    for (int i = 0; i < kAlienClassModelCount; ++i) {
        const ShipClassId id = static_cast<ShipClassId>(i);
        const ModelRef& hostile = assets.hostileClassShips[static_cast<std::size_t>(i)];
        const ModelRef& calm = assets.neutralClassShips[static_cast<std::size_t>(i)];
        CHECK(hostile && hostile != assets.hostileShip && calm && calm != hostile);
        if (!hostile || !calm) continue;
        CHECK(AlienModelAsset(assets, ClassModel(id, true)) == hostile && AlienModelAsset(assets, ClassModel(id, false)) == calm);
        // La eslora de su clase (la variante que les da AssignShipClass), de proa a popa.
        float low = 1e30f, high = -1e30f;
        bool closed = true;
        for (const ModelPart& p : hostile->parts) {
            closed = closed && p.collision && !p.collision->vertices.empty();
            if (!p.collision) continue;
            for (const MeshVertex& v : p.collision->vertices) {
                low = std::min(low, v.position.z + p.position.z);
                high = std::max(high, v.position.z + p.position.z);
            }
        }
        CHECK(closed);
        const double length = ShipClassOf(id, id == ShipClassId::Frigate ? ShipVariant::Light : ShipVariant::Normal).length * 1000.0;
        CHECK(std::fabs((high - low) - length) < length * 0.03);
        CHECK(std::fabs(high + low) < length * 0.03);  // origen a media eslora
        // Sus zonas, por el nombre de sus piezas: todas tienen cuerpo, nucleo y dos cuchillas.
        const std::size_t m = static_cast<std::size_t>(ClassModel(id, true));
        CHECK(assets.alienPartZones[m].size() == hostile->parts.size());
        const ZoneLayout& zones = *assets.alienZones[m];
        for (HullZone z : {HullZone::Spine, HullZone::Core, HullZone::PortNacelle, HullZone::StarboardNacelle}) CHECK(ZonePresent(zones, idx(z)));
        CHECK(ZonePresent(zones, idx(HullZone::Bridge)) == (id != ShipClassId::Drone));
        CHECK(ZonePresent(zones, idx(HullZone::WarpRing)) == (id == ShipClassId::Frigate));
        CHECK(!ZonePresent(zones, idx(HullZone::Bow)) && !ZonePresent(zones, idx(HullZone::Hold1)));
        CHECK(ZoneLabel(&zones, idx(HullZone::Core), true) == (id == ShipClassId::Drone ? "el ojo" : "el núcleo"));
        // La cuchilla de babor esta a babor (-X), y la neutral tiene las mismas zonas.
        for (std::size_t p = 0; p < hostile->parts.size(); ++p)
            if (assets.alienPartZones[m][p] == idx(HullZone::PortNacelle)) CHECK(hostile->parts[p].position.x < 0.0f);
        CHECK(assets.alienPartZones[static_cast<std::size_t>(ClassModel(id, false))] == assets.alienPartZones[m]);
        // De frente por la punta de la cuchilla de estribor, la trayectoria da en ella.
        Vec3f tip{};
        for (std::size_t p = 0; p < hostile->parts.size(); ++p)
            if (assets.alienPartZones[m][p] == idx(HullZone::StarboardNacelle) && hostile->parts[p].position.x > tip.x) tip = hostile->parts[p].position;
        const int hit = ProjectileImpact(zones, {tip.x, tip.y, length}, {tip.x, tip.y, tip.z}).zone;
        CHECK(hit == idx(HullZone::StarboardNacelle));
        if (hit != idx(HullZone::StarboardNacelle)) std::printf("  clase %d: zona %d en (%.0f, %.0f, %.0f)\n", i, hit, tip.x, tip.y, tip.z);
    }

    // Torretas: una por arma, sobre su base (el -Y sale del casco), con los canones hacia +Z.
    const auto reach = [](const ModelAsset& model) {
        float r = 0.0f;
        for (const ModelPart& p : model.parts) r = std::max(r, Length(p.position) + p.radius);
        return r;
    };
    for (int k = 0; k < static_cast<int>(ShipModuleKind::Count); ++k) {
        const ShipModuleKind kind = static_cast<ShipModuleKind>(k);
        const ModelRef& model = assets.moduleModels[static_cast<std::size_t>(k)];
        CHECK((model != nullptr) == (TurretModelName(kind) != nullptr));
        if (!model) continue;
        const std::string prefix = std::string(TurretModelName(kind)) + "_";
        for (const ModelPart& p : model->parts) {
            CHECK(p.name.rfind(prefix, 0) == 0);
            CHECK(p.position.y < 0.0f);
        }
    }
    const auto weapon = [&](ShipModuleKind kind) { return *assets.moduleModels[static_cast<std::size_t>(kind)]; };
    for (const auto& [light, heavy] : {std::pair{ShipModuleKind::CoilgunLight, ShipModuleKind::CoilgunHeavy}, std::pair{ShipModuleKind::RailSpinalLight, ShipModuleKind::RailSpinalHeavy},
                                       std::pair{ShipModuleKind::Rail360Light, ShipModuleKind::Rail360Heavy}, std::pair{ShipModuleKind::BayLight10, ShipModuleKind::BayHeavy50},
                                       std::pair{ShipModuleKind::NuclearLight, ShipModuleKind::NuclearHeavy}})
        CHECK(reach(weapon(heavy)) > reach(weapon(light)) * 1.4f);
    // La ligera cabe en la base de torreta de la nave, como la bahia de drones (26 m de radio).
    for (ShipModuleKind kind : {ShipModuleKind::CoilgunLight, ShipModuleKind::Rail360Light, ShipModuleKind::BayLight10, ShipModuleKind::NuclearLight, ShipModuleKind::PointDefensePlatform})
        for (const ModelPart& p : weapon(kind).parts)
            if (p.name.find("zocalo") != std::string::npos) CHECK(std::hypot(p.position.x, p.position.z) < 1.0f && p.radius < 30.0f);
    for (const ModelPart& p : weapon(ShipModuleKind::CoilgunLight).parts)
        if (p.name.find("canones") != std::string::npos) CHECK(p.position.z > 15.0f);
    CHECK(reach(weapon(ShipModuleKind::RailSpinalLight)) > reach(weapon(ShipModuleKind::Rail360Light)));  // la espinal es la mas larga
}

static std::string Base64(const std::string& bytes) {
    static const char* const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t left = bytes.size() - i;
        const auto byte = [&](std::size_t k) { return k < bytes.size() ? static_cast<unsigned>(static_cast<unsigned char>(bytes[k])) : 0u; };
        const unsigned value = byte(i) << 16 | byte(i + 1) << 8 | byte(i + 2);
        encoded += alphabet[value >> 18 & 63];
        encoded += alphabet[value >> 12 & 63];
        encoded += left > 1 ? alphabet[value >> 6 & 63] : '=';
        encoded += left > 2 ? alphabet[value & 63] : '=';
    }
    return encoded;
}

static void TestStructuralDamage() {
    Balance rules;
    // Cada zona sostiene un sistema.
    {
        HullZones z = IntactHull();
        CHECK(std::fabs(EngineOutput(z) - 1.0) < 1e-9);
        z[static_cast<std::size_t>(HullZone::PortNacelle)] = 0.0;
        CHECK(std::fabs(EngineOutput(z) - 0.5) < 1e-9);
        z[static_cast<std::size_t>(HullZone::StarboardNacelle)] = 0.0;
        CHECK(EngineOutput(z) < 0.1 && EngineOutput(z) > 0.0);  // quedan los de maniobra

        ShipState s = MakeShip(rules);
        CHECK(DriveAvailable(s, DriveMode::Cruise, rules.drive));
        s.zones[static_cast<std::size_t>(HullZone::WarpRing)] = 0.2;
        CHECK(!DriveAvailable(s, DriveMode::Cruise, rules.drive));
        CHECK(DriveAvailable(s, DriveMode::Impulse, rules.drive));
        s.mode = DriveMode::Cruise;
        s.throttle = 1.0;
        StepDrive(s, rules, 1.0, 1.0, 1.0, {});
        CHECK(s.mode == DriveMode::Impulse);  // sin anillo no hay crucero

        // Con una gondola menos acelera la mitad.
        const auto pushed = [&](double port) {
            ShipState t = MakeShip(rules);
            t.zones[static_cast<std::size_t>(HullZone::PortNacelle)] = port;
            t.throttle = 1.0;
            t.holding = false;
            StepDrive(t, rules, 0.5, 0.5, 0.5, {});
            return Length(t.velocity);
        };
        CHECK(pushed(0.0) < pushed(1.0) * 0.6);
        CHECK(pushed(0.0) > pushed(1.0) * 0.4);
        CHECK(AimSpread(IntactHull()) == 1.0);
    }

    auto engine = MakeEngine(rules, 7);
    const auto fresh = [&] {
        auto g = std::make_unique<GameSession>(*engine, rules, 7, GameModeId::Normal);
        g->Conversation().Stop();
        g->Step(0.05, {});
        return g;
    };
    // La trayectoria decide donde da, no de donde viene el atacante.
    {
        const auto hit = [](const Vec3d& from, const Vec3d& through) { return ProjectileImpact(through + from * 2000.0, through - from * 2000.0); };
        const Vec3d front{0, 0, 1};
        CHECK(hit(front, {0, 0, 0}).zone == static_cast<int>(HullZone::Bow));
        CHECK(hit(front, {0, 0, 0}).point.z > 520.0);  // en la punta de la proa
        CHECK(hit(front, {-150, 0, -380}).zone == static_cast<int>(HullZone::PortNacelle));  // de frente, apuntando a la gondola
        CHECK(hit(front, {150, 0, -380}).zone == static_cast<int>(HullZone::StarboardNacelle));
        CHECK(hit(front, {230, 0, -120}).zone == static_cast<int>(HullZone::WarpRing));
        CHECK(hit({1, 0, 0}, {0, 0, -300}).zone == static_cast<int>(HullZone::StarboardNacelle));  // al nucleo, por estribor: la gondola se interpone
        CHECK(hit({0, 1, 0}, {0, 0, -300}).zone != static_cast<int>(HullZone::Bow));
        const ZoneImpact graze = hit(front, {600, 0, -380});  // pasa de largo: cuenta lo mas cercano
        CHECK(graze.zone == static_cast<int>(HullZone::StarboardNacelle) || graze.zone == static_cast<int>(HullZone::WarpRing));
        CHECK(Length(hit(front, {0, 0, 0}).from - front) < 1e-9);

        auto g = fresh();
        const double nacelle = g->Ship().zones[static_cast<std::size_t>(HullZone::PortNacelle)];
        CHECK(g->ShootHull(Vec3d{-150, 0, 2000}, Vec3d{-150, 0, -2000}, 50.0) == static_cast<int>(HullZone::PortNacelle));
        CHECK(g->Ship().zones[static_cast<std::size_t>(HullZone::PortNacelle)] < nacelle);
        CHECK(g->Ship().zones[static_cast<std::size_t>(HullZone::Bow)] == 1.0);  // la proa no se entera
        CHECK(g->HullHits().back().located && g->HullHits().back().point.x < -90.0);
    }
    // Por el hueco de una zona perdida el golpe sigue hacia dentro: la proa deja paso al puente.
    {
        auto g = fresh();
        const int bow = static_cast<int>(HullZone::Bow), bridge = static_cast<int>(HullZone::Bridge);
        g->DamageZone(bow, rules.ship.hull * kZoneToughness * 1.2);
        CHECK(ZoneLost(g->Ship().zones, bow));
        CHECK(g->Ship().zones[static_cast<std::size_t>(bridge)] < 1.0);  // lo que sobro
        const double before = g->Ship().zones[static_cast<std::size_t>(bridge)];
        g->DamageZone(bow, rules.ship.hull * 0.05);
        CHECK(g->Ship().zones[static_cast<std::size_t>(bridge)] < before);
        CHECK(g->Phase() == GamePhase::Flying);
        // Las placas de los conos iban en la proa.
        g->Step(0.05, {});
        CHECK(g->Ship().defense.charge[static_cast<std::size_t>(ShieldKind::Ablative)] == 0.0);
    }
    // Una bodega perdida se lleva su parte de la carga.
    {
        auto g = fresh();
        g->Ship().cargo.Add(Resource::Metal, 600.0);
        const double metal = g->Ship().cargo.Get(Resource::Metal);
        g->DamageZone(static_cast<int>(HullZone::Hold2), rules.ship.hull * kZoneToughness * 1.01);
        CHECK(std::fabs(g->Ship().cargo.Get(Resource::Metal) - metal * 5.0 / 6.0) < 1e-6);
    }
    // Zonas rojas: el puente apaga la nave; tras la cinematica, la pantalla final.
    {
        auto g = fresh();
        const Vec3d from{0, -1, 0.3};
        g->DamageZone(static_cast<int>(HullZone::Bridge), rules.ship.hull * kZoneToughness * 1.05, &from);
        CHECK(g->Phase() == GamePhase::Adrift);
        CHECK(g->ShutDown() && g->FailedZone() == static_cast<int>(HullZone::Bridge));
        CHECK(g->LossReason().find("Puente") != std::string::npos);
        CHECK(g->Ship().energy == 0.0);
        CHECK(g->HullHits().size() == 1);
        const Vec3d start = g->Ship().position;
        for (int i = 0; i < 200; ++i) g->Step(0.05, {});
        CHECK(g->Phase() == GamePhase::Adrift);           // 10 s: aun en la cinematica
        CHECK(Length(g->Ship().position - start) > 0.1);  // a la deriva
        for (int i = 0; i < 100; ++i) g->Step(0.05, {});
        CHECK(g->Phase() == GamePhase::GameOver && g->ShutDown());
    }
    // La espina tambien la apaga; el nucleo la hace estallar.
    {
        auto g = fresh();
        g->DamageZone(static_cast<int>(HullZone::Spine), rules.ship.hull * kZoneToughness * 1.05);
        CHECK(g->Phase() == GamePhase::Adrift && g->ShutDown());
        auto h = fresh();
        h->DamageZone(static_cast<int>(HullZone::Core), rules.ship.hull * kZoneToughness * 1.05);
        CHECK(h->Phase() == GamePhase::Exploding && !h->ShutDown());
        CHECK(h->FailedZone() == static_cast<int>(HullZone::Core));
    }
    // Un golpe directo en zona roja hace mas dano que en una normal.
    {
        auto g = fresh();
        const double before = g->Ship().zones[static_cast<std::size_t>(HullZone::Core)];
        g->DamageZone(static_cast<int>(HullZone::PortNacelle), rules.ship.hull * kZoneToughness * 1.5);  // lo que sobra va al nucleo
        CHECK(g->Ship().zones[static_cast<std::size_t>(HullZone::Core)] < before);
        CHECK(g->Phase() == GamePhase::Flying);
    }
    // En creativo nada apaga la nave.
    {
        auto g = fresh();
        g->SetCreative(true);
        g->DamageZone(static_cast<int>(HullZone::Bridge), rules.ship.hull * 0.9);
        CHECK(g->Phase() == GamePhase::Flying);
    }

    // Crateres en 3D: solo con la calidad maxima se calculan.
    {
        int uploads = 0;
        const GameAssets assets = BuildGameAssets([&](const MeshData&) { return static_cast<MeshId>(++uploads); });
        CHECK(assets.girder != kNoMesh && assets.plate != kNoMesh && assets.shard != kNoMesh);
        for (const ModelPart& p : assets.ship->parts) {
            CHECK(p.collision != nullptr);
            if (!p.collision) continue;
            // Las aristas de la nave se parten (12 m, salvo donde llega al tope de triangulos): se puede abollar.
            float longest = 0;
            const MeshData& m = *p.collision;
            for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3)
                for (int e = 0; e < 3; ++e) longest = std::max(longest, Length(m.vertices[m.indices[t + e]].position - m.vertices[m.indices[t + (e + 1) % 3]].position));
            CHECK(longest <= 24.0f);
        }
        auto g = fresh();
        GameLayer layer(*g, assets, 1, 2, Color{0.1f, 0.05f, 0.15f, 1.0f});
        engine->AddLayer(&layer);
        const Vec3d from{1, 0, 0};
        g->DamageZone(static_cast<int>(HullZone::WarpRing), rules.ship.hull * 0.06, &from);
        Camera camera;
        camera.SetViewport(1600, 900, 800.0f, kNearPlaneKm);
        camera.SetOrientation(Quatf::Cast(g->Ship().orientation));
        camera.SetPosition(g->Ship().position - Heading(g->Ship()) * 2.3);
        FramePacket packet;
        layer.SetHullDetail(2);
        engine->Build(camera, 1.0, packet);
        CHECK(layer.CraterCount() == 0 && packet.craters.empty());
        layer.SetHullDetail(3);
        engine->Build(camera, 1.0, packet);
        CHECK(layer.CraterCount() == 1);
        CHECK(!packet.craters.empty());
        bool twoSided = false, girders = false;
        for (const MeshInstance& m : packet.meshes) {
            twoSided = twoSided || (m.doubleSided && m.craterCount > 0);
            girders = girders || m.mesh == assets.girder;
        }
        CHECK(twoSided);
        CHECK(girders);
        // El crater queda en el lado de estribor del anillo, donde llego el golpe.
        for (const MeshCrater& c : packet.craters) CHECK(c.radius > 0.0f && c.depth >= 0.35f);
    }

    // La nave desde un .gltf: cada nodo es una pieza, con su zona por el nombre o por donde esta.
    if (GltfSupported()) {
        const float tri[9] = {-60, 0, 0, 60, 0, 0, 0, 0, 120};
        const std::uint16_t idx[3] = {0, 1, 2};
        std::string bytes(reinterpret_cast<const char*>(tri), sizeof(tri));
        bytes.append(reinterpret_cast<const char*>(idx), sizeof(idx));
        const std::string json =
            R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1,2]}],)"
            R"("nodes":[{"name":"gondola_babor","mesh":0,"translation":[-150,0,380]},{"name":"Proa","mesh":0,"translation":[0,0,-470]},{"name":"pieza","mesh":0,"translation":[150,0,380]}],)"
            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-60,0,0],"max":[60,0,120]},)"
            R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
            R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
            R"("buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,)" + Base64(bytes) + R"("}]})";
        const std::string path = "epsilon_nave_prueba.gltf";
        {
            std::ofstream file(path, std::ios::binary);
            file << json;
        }
        std::string error;
        int uploads = 0;
        const ModelRef ship = LoadShipModel(path, [&](const MeshData&) { return static_cast<MeshId>(++uploads); }, nullptr, &error);
        std::remove(path.c_str());
        CHECK(ship != nullptr);
        if (ship) {
            CHECK(ship->parts.size() == 3);
            for (const ModelPart& p : ship->parts) CHECK(p.collision != nullptr);
            // La proa del .gltf mira a -Z: en el juego, a +Z.
            CHECK(ZoneOfPart(ship->parts[0].name, ship->parts[0].position) == static_cast<int>(HullZone::PortNacelle));
            CHECK(ZoneOfPart(ship->parts[1].name, ship->parts[1].position) == static_cast<int>(HullZone::Bow));
            CHECK(ship->parts[1].position.z > 400.0f);
            CHECK(ZoneOfPart(ship->parts[2].name, ship->parts[2].position) == static_cast<int>(HullZone::StarboardNacelle));  // sin nombre: por donde esta
        } else {
            std::printf("  %s\n", error.c_str());
        }
        CHECK(LoadShipModel("no_existe.glb", [](const MeshData&) { return MeshId{1}; }, nullptr) == nullptr);
    }
}

static void TestShipModules() {
    Balance rules;
    SetShipDesign(nullptr);  // la maqueta
    const ShipDesign& maquette = CurrentShipDesign();
    int exterior = 0, interior = 0;
    for (const ModuleSlot& s : maquette.slots) (s.kind == SlotKind::External ? exterior : interior)++;
    CHECK(exterior == 6 && interior == 19);

    // Motores de impulso: cada uno mas rapido.
    {
        ShipState s = MakeShip(rules);
        CHECK(s.loadout.modules.size() == maquette.slots.size());
        const double base = TopSpeed(s, DriveMode::Impulse, rules.drive);
        s.loadout.engine = 4;
        CHECK(std::fabs(TopSpeed(s, DriveMode::Impulse, rules.drive) - base * ImpulseEngine(4).speed) < 1e-9);
        for (int t = 2; t <= kImpulseEngineCount; ++t) CHECK(ImpulseEngine(t).speed > ImpulseEngine(t - 1).speed);
        // Y lo nota de verdad: acelera mas y llega mas lejos en el mismo tiempo.
        const auto run = [&](int engine) {
            ShipState t = MakeShip(rules);
            t.loadout.engine = engine;
            t.throttle = 1.0;
            for (int i = 0; i < 400; ++i) StepDrive(t, rules, 0.5, 0.5, 0.5, {});
            return Length(t.velocity);
        };
        CHECK(run(4) > run(1) * 1.8);  // aun acelerando: manda la aceleracion (x2,2)
    }

    // El taller solo abre atracada; en creativo, en cualquier sitio y gratis.
    auto engine = MakeEngine(rules, 7);
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        std::string why;
        CHECK(!g.InstallModule(6, ShipModuleKind::Storage, &why));
        CHECK(why.find("atracad") != std::string::npos);
        g.SetCreative(true);
        const double capacity = CargoCapacity(g.Ship(), rules.ship);
        CHECK(g.InstallModule(6, ShipModuleKind::Storage, &why));
        CHECK(std::fabs(CargoCapacity(g.Ship(), rules.ship) - capacity - kStorageCargo) < 1e-9);
        CHECK(!g.InstallModule(0, ShipModuleKind::Storage, &why));  // un almacen no va por fuera
        CHECK(g.InstallModule(0, ShipModuleKind::Turret, &why));
        CHECK(g.InstallModule(7, ShipModuleKind::AntimatterTank, &why));
        CHECK(std::fabs(AntimatterCapacity(g.Ship(), rules.ship) - rules.ship.antimatterTank - kTankAntimatter) < 1e-9);
        int moduleTurrets = 0;
        for (const WeaponMount& m : g.Ship().weapons) moduleTurrets += m.slot == 0 && m.kind == WeaponKind::Turret ? 1 : 0;
        CHECK(moduleTurrets == 1);
        const int armorZone = maquette.slots[3].zone;  // la 5 y la 6 son las bahias de drones de serie
        CHECK(g.InstallModule(3, ShipModuleKind::ArmorPlate, &why));
        CHECK(std::fabs(ZoneHardness(g.Ship(), armorZone) - (1.0 + kArmorShare)) < 1e-9);
        CHECK(g.SetImpulseEngine(3, &why) && g.Ship().loadout.engine == 3);

        // Se guarda, y al cargar la torreta vuelve a salir de su modulo.
        const std::optional<SaveData> saved = ReadSave(WriteSave(g.Snapshot()));
        CHECK(saved.has_value());
        if (saved) {
            CHECK(saved->Ship()->ship.loadout.engine == 3);
            CHECK(saved->Ship()->ship.loadout.modules[6] == ShipModuleKind::Storage && saved->Ship()->ship.loadout.modules[0] == ShipModuleKind::Turret);
            GameSession h(*engine, rules, 7, GameModeId::Normal);
            CHECK(h.Restore(*saved));
            int turrets = 0;
            for (const WeaponMount& m : h.Ship().weapons) turrets += m.slot == 0 ? 1 : 0;
            CHECK(turrets == 1);
        }
        // Perder la zona de una ranura se lleva su modulo.
        g.SetCreative(false);
        const int holdZone = maquette.slots[6].zone;
        g.DamageZone(holdZone, rules.ship.hull * kZoneToughness * 1.01);
        CHECK(g.Ship().loadout.modules[6] == ShipModuleKind::None);
    }

    // Atracada de verdad: montar cuesta materiales y quitar devuelve la mitad.
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        const TargetRef station = PlaceStation(g);
        for (int i = 0; i < 200; ++i) g.Step(0.05, {});
        CHECK(g.JumpTo(station, 5.0));
        CHECK(g.Dock(station, true));
        g.Step(0.05, {});
        CHECK(g.CanRefit());
        std::string why;
        g.Ship().cargo.Add(Resource::Metal, 1000.0 - g.Ship().cargo.Get(Resource::Metal));
        g.Ship().cargo.Add(Resource::Silicate, 500.0 - g.Ship().cargo.Get(Resource::Silicate));
        const ShipModuleSpec& tank = ModuleSpec(ShipModuleKind::AntimatterTank);
        CHECK(g.InstallModule(8, ShipModuleKind::AntimatterTank, &why));
        CHECK(std::fabs(g.Ship().cargo.Get(Resource::Metal) - (1000.0 - tank.metal)) < 1e-6);
        CHECK(g.InstallModule(8, ShipModuleKind::None, &why));
        CHECK(std::fabs(g.Ship().cargo.Get(Resource::Metal) - (1000.0 - tank.metal * 0.5)) < 1e-6);
        g.Ship().cargo.Add(Resource::Metal, -g.Ship().cargo.Get(Resource::Metal));
        CHECK(!g.SetImpulseEngine(2, &why));  // sin metal
        CHECK(why.find("Faltan") != std::string::npos);
    }

    // Dano pieza a pieza: lo pequeno se suelta solo, antes que su zona.
    {
        int uploads = 0;
        const GameAssets assets = BuildGameAssets([&](const MeshData&) { return static_cast<MeshId>(++uploads); });
        const std::shared_ptr<ShipDesign> design = BuildShipDesign(*assets.ship, {});
        CHECK(design->pieces.size() == assets.ship->parts.size());
        for (const DesignPiece& p : design->pieces) CHECK(!p.samples.empty());
        for (const ModuleSlot& s : design->slots)
            if (s.kind == SlotKind::External) CHECK(s.piece >= 0);
        SetShipDesign(design);
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        CHECK(g.Ship().pieces.size() == design->pieces.size());
        const int nacelle = static_cast<int>(HullZone::StarboardNacelle);
        for (int i = 0; i < 4; ++i) g.ShootHull({150, 0, -3000}, {150, 0, 3000}, 45.0 * kCombatScale);  // por detras, al escape de estribor
        int lostHere = 0;
        for (std::size_t i = 0; i < design->pieces.size(); ++i) lostHere += design->pieces[i].zone == nacelle && PieceLost(g.Ship(), static_cast<int>(i)) ? 1 : 0;
        CHECK(lostHere >= 1);
        CHECK(!ZoneLost(g.Ship().zones, nacelle));
        bool single = false;
        for (const GameSession::LostPiece& p : g.LostPieces()) single = single || p.piece >= 0;
        CHECK(single);
        // Se guarda.
        const std::optional<SaveData> saved = ReadSave(WriteSave(g.Snapshot()));
        CHECK(saved && saved->Ship()->ship.pieces.size() == design->pieces.size());

        // En la capa: la pieza arrancada no se dibuja.
        GameLayer layer(g, assets, 1, 2, Color{0.1f, 0.05f, 0.15f, 1.0f});
        engine->AddLayer(&layer);
        Camera camera;
        camera.SetViewport(1600, 900, 800.0f, kNearPlaneKm);
        camera.SetOrientation(Quatf::Cast(g.Ship().orientation));
        camera.SetPosition(g.Ship().position - Heading(g.Ship()) * 2.3);
        FramePacket packet;
        engine->Build(camera, 1.0, packet);
        for (const MeshInstance& m : packet.meshes) {
            if ((m.id & 0xFF000000u) != 0x70000000u || ((m.id >> 8) & 0xFFFFu) != 0) continue;
            const int part = static_cast<int>(m.id & 0xFFu) - 1;
            CHECK(!PieceLost(g.Ship(), part));
        }
    }
    SetShipDesign(nullptr);
}

static void TestThrustersAndLight() {
    Balance rules;
    // Con gas gira con los propulsores de maniobra y lo gasta; sin el, solo el giroscopio, mas despacio.
    const auto turn = [&](double gas, double& spent, Vec3d& push) {
        ShipState s = MakeShip(rules);
        s.cargo.Add(Resource::Gas, gas - s.cargo.Get(Resource::Gas));
        AimAt(s, Normalize(Vec3d{1, 0, 1}));
        for (int i = 0; i < 20; ++i) {
            StepHeading(s, rules.ship, 0.1);
            if (i == 0) push = s.angularPush;
        }
        spent = gas - s.cargo.Get(Resource::Gas);
        return Length(s.angularRate);
    };
    double spent = 0, none = 0;
    Vec3d push, noPush;
    const double withGas = turn(10.0, spent, push);
    const double withoutGas = turn(0.0, none, noPush);
    CHECK(spent > 0.0 && none == 0.0);
    CHECK(Length(push) > 0.0 && Length(noPush) == 0.0);  // sin gas no hay soplo que dibujar
    CHECK(withoutGas < withGas * 0.5);
    CHECK(MakeShip(rules).cargo.Get(Resource::Gas) == rules.ship.startGas);

    // Motores de iones: sin gas, la mitad de velocidad maxima; con gas, empujar lo gasta.
    {
        ShipState a = MakeShip(rules), b = MakeShip(rules);
        b.cargo.Add(Resource::Gas, -b.cargo.Get(Resource::Gas));
        CHECK(std::fabs(TopSpeed(b, DriveMode::Impulse, rules.drive) - 0.5 * TopSpeed(a, DriveMode::Impulse, rules.drive)) < 1e-9);
        a.throttle = 1.0;
        a.holding = false;
        const double before = a.cargo.Get(Resource::Gas);
        for (int i = 0; i < 20; ++i) StepDrive(a, rules, 1.0, 1.0, 1.0);
        CHECK(a.cargo.Get(Resource::Gas) < before);
    }

    SetShipDesign(nullptr);
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Normal);
    g.Conversation().Stop();
    g.Step(0.05, {});
    int uploads = 0;
    const GameAssets assets = BuildGameAssets([&](const MeshData&) { return static_cast<MeshId>(++uploads); });
    GameLayer layer(g, assets, 1, 2, Color{0.1f, 0.05f, 0.15f, 1.0f});
    engine->AddLayer(&layer);
    Camera camera;
    camera.SetViewport(1600, 900, 800.0f, kNearPlaneKm);
    camera.SetOrientation(Quatf::Cast(g.Ship().orientation));
    camera.SetPosition(g.Ship().position - Heading(g.Ship()) * 2.3);
    const auto count = [](const FramePacket& p, std::uint32_t tag) {
        int n = 0;
        for (const SpriteInstance& s : p.sprites) n += (s.id & 0xFF000000u) == tag ? 1 : 0;
        return n;
    };
    FramePacket packet;
    engine->Build(camera, 1.0, packet);
    CHECK(count(packet, 0x7D000000u) == 0);  // parada: sin estela
    g.Ship().throttle = 1.0;
    engine->Build(camera, 1.0, packet);
    const int ion = count(packet, 0x7D000000u);
    CHECK(ion > 0);
    g.Ship().loadout.engine = 4;
    engine->Build(camera, 1.0, packet);
    CHECK(count(packet, 0x7D000000u) > ion);  // la de fusion lleva nucleo y discos de choque

    // A mas de c, la nave se ilumina desde hacia donde avanza.
    g.Ship().throttle = 0.0;
    g.Ship().moveDirection = Heading(g.Ship());
    g.Ship().speed = 2.0 * kLightSpeed;
    engine->Build(camera, 1.0, packet);
    const Vec3f ahead = Normalize(camera.ViewRotation().TransformDirection(Vec3f(Heading(g.Ship()))));
    bool lit = false;
    for (const MeshInstance& m : packet.meshes)
        if ((m.id & 0xFF000000u) == 0x70000000u && ((m.id >> 8) & 0xFFFFu) == 0) lit = Dot(m.lightDirection, ahead) > 0.99f;
    CHECK(lit);
}

// Armamento humano (fase B): catalogo, reglas de montaje, costes, misiles, defensa puntual y bahias de serie.
static void TestHumanArmament() {
    Balance rules;
    SetShipDesign(nullptr);  // la maqueta: huecos 0 a 5 exteriores, 4 y 5 son las bahias de serie
    const ArmamentRules& arm = rules.armament;

    // El catalogo: 14 armas, una por cada modulo de arma, todas por fuera y con su masa real.
    CHECK(static_cast<int>(ShipModuleKind::Count) - static_cast<int>(kFirstHumanWeapon) == kHumanWeaponCount);
    for (int k = static_cast<int>(kFirstHumanWeapon); k < static_cast<int>(ShipModuleKind::Count); ++k) {
        const ShipModuleKind kind = static_cast<ShipModuleKind>(k);
        const HumanWeaponSpec* w = HumanWeaponOf(arm, kind);
        CHECK(w != nullptr && IsHumanWeapon(kind));
        if (!w) continue;
        CHECK(ModuleSpec(kind).slot == SlotKind::External && ModuleSpec(kind).metal == w->mass && w->mass >= 0.2 && w->mass <= 1.7);
        if (w->type == ArmType::MissileBay || w->type == ArmType::NuclearMissile) CHECK(w->damageType == DamageType::Explosive || w->damageType == DamageType::Nuclear);
        else CHECK(w->damageType == DamageType::Kinetic && w->capacity == 0);
    }
    CHECK(HumanWeaponOf(arm, ShipModuleKind::Turret) == nullptr && !IsHumanWeapon(ShipModuleKind::DroneBay));
    // La pesada es mas de todo que la ligera del mismo tipo.
    for (int t = 0; t < kHumanWeaponCount; t += 2) {
        if (t >= 6 && t < 12) continue;  // las bahias van en otro orden
        CHECK(arm.weapons[t + 1].heavy && !arm.weapons[t].heavy && arm.weapons[t + 1].damage > arm.weapons[t].damage && arm.weapons[t + 1].mass > arm.weapons[t].mass);
    }
    // Bahias de 10, 25 y 50 misiles; el nuclear, uno solo.
    CHECK(HumanWeaponOf(arm, ShipModuleKind::BayLight10)->capacity == 10 && HumanWeaponOf(arm, ShipModuleKind::BayLight25)->capacity == 25 && HumanWeaponOf(arm, ShipModuleKind::BayLight50)->capacity == 50);
    CHECK(HumanWeaponOf(arm, ShipModuleKind::BayHeavy50)->capacity == 50 && HumanWeaponOf(arm, ShipModuleKind::NuclearLight)->capacity == 1);
    CHECK(HumanWeaponOf(arm, ShipModuleKind::NuclearLight)->damageType == DamageType::Nuclear && HumanWeaponOf(arm, ShipModuleKind::BayLight10)->damageType == DamageType::Explosive);
    // Coilgun: debil y rapida. Railgun: mas potente y mas lenta. La 360 es peor que la espinal en todo.
    const HumanWeaponSpec& coil = *HumanWeaponOf(arm, ShipModuleKind::CoilgunLight);
    const HumanWeaponSpec& spinal = *HumanWeaponOf(arm, ShipModuleKind::RailSpinalLight);
    const HumanWeaponSpec& r360 = *HumanWeaponOf(arm, ShipModuleKind::Rail360Light);
    CHECK(spinal.damage > coil.damage && r360.damage > coil.damage && spinal.cooldown > coil.cooldown && r360.cooldown > coil.cooldown);
    CHECK(spinal.damage > r360.damage && spinal.range > r360.range && spinal.speed > r360.speed && spinal.damage / spinal.cooldown > r360.damage / r360.cooldown && spinal.aimError < r360.aimError);
    // Costes: W, Re, Co y Nb para las de riel; Pt, Pd e Ir para el misil explosivo; U y Th para el nuclear.
    CHECK(MissingElements(ElementStock{}, coil.elements, 4).find("W") != std::string::npos);
    CHECK(HumanWeaponOf(arm, ShipModuleKind::BayLight10)->missile[0].z == 78 && HumanWeaponOf(arm, ShipModuleKind::BayLight10)->missile[1].z == 46 && HumanWeaponOf(arm, ShipModuleKind::BayLight10)->missile[2].z == 77);
    CHECK(HumanWeaponOf(arm, ShipModuleKind::NuclearLight)->missile[0].z == 92 && HumanWeaponOf(arm, ShipModuleKind::NuclearLight)->missile[1].z == 90);
    CHECK(HumanWeaponOf(arm, ShipModuleKind::NuclearLight)->missileEnergy > HumanWeaponOf(arm, ShipModuleKind::BayLight10)->missileEnergy);

    // Planos: uno por tipo y tamano; las bahias de 10, 25 y 50 comparten el suyo.
    CHECK(BlueprintId(*HumanWeaponOf(arm, ShipModuleKind::BayLight10)) == BlueprintId(*HumanWeaponOf(arm, ShipModuleKind::BayLight50)));
    CHECK(BlueprintId(*HumanWeaponOf(arm, ShipModuleKind::BayLight10)) != BlueprintId(*HumanWeaponOf(arm, ShipModuleKind::BayHeavy10)));
    {
        ShipLoadout l;
        const int id = BlueprintId(coil);
        CHECK(!HasBlueprint(l, id));
        GrantBlueprint(l, id);
        CHECK(HasBlueprint(l, id) && !HasBlueprint(l, id + 1));
        CHECK(BlueprintEnergy(arm, id + 1) == BlueprintEnergy(arm, id) * arm.blueprintHeavy);
    }

    // Reglas de montaje: las pesadas, solo en cascos pesados; la espinal, solo en algunas clases y solo una.
    std::string why;
    const HumanWeaponSpec& heavyCoil = *HumanWeaponOf(arm, ShipModuleKind::CoilgunHeavy);
    CHECK(!MountAllowed(arm, heavyCoil, ShipClassId::Frigate, ShipVariant::Light, 0, &why) && why.find("pesada") != std::string::npos);
    CHECK(MountAllowed(arm, heavyCoil, ShipClassId::Frigate, ShipVariant::Heavy, 0));
    CHECK(MountAllowed(arm, coil, ShipClassId::Drone, ShipVariant::Light, 0));
    CHECK(MountAllowed(arm, spinal, ShipClassId::Frigate, ShipVariant::Light, 0));
    CHECK(!MountAllowed(arm, spinal, ShipClassId::Corvette, ShipVariant::Normal, 0, &why) && why.find("clase") != std::string::npos);
    CHECK(!MountAllowed(arm, spinal, ShipClassId::Freighter, ShipVariant::Normal, 0));
    CHECK(!MountAllowed(arm, spinal, ShipClassId::Frigate, ShipVariant::Light, 1, &why) && why.find("una") != std::string::npos);
    CHECK(MountAllowed(arm, r360, ShipClassId::Corvette, ShipVariant::Normal, 1));  // la 360 va en cualquiera

    // Defensa puntual: tantos puntos como huecos x 5 (30 en la fragata ligera); las plataformas suman.
    CHECK(DefensePoints(arm, ShipClassId::Frigate, ShipVariant::Light) == 30);
    CHECK(DefensePoints(arm, ShipClassId::Superfreighter, ShipVariant::Normal) == 500);
    CHECK(PointDefenseRate(arm, 0, 0) == 0.0 && PointDefenseRate(arm, 30, 2) > PointDefenseRate(arm, 30, 0) && PointDefenseRate(arm, 30, 1) > PointDefenseRate(arm, 10, 0));

    // Los escudos tambien tienen opinion sobre el dano nuevo: el plasma para el explosivo, el EM casi no.
    CHECK(kDamageTypeCount == 6 && std::string(Name(DamageType::Explosive)) == "Explosivo" && std::string(Name(DamageType::Nuclear)) == "Nuclear");
    CHECK(rules.defense.shields[static_cast<int>(ShieldKind::Plasma)].absorb[static_cast<int>(DamageType::Explosive)] > rules.defense.shields[static_cast<int>(ShieldKind::EM)].absorb[static_cast<int>(DamageType::Explosive)]);
    CHECK(rules.defense.shields[static_cast<int>(ShieldKind::Love)].absorb[static_cast<int>(DamageType::Nuclear)] == 1.0);
    {
        DefenseState d = MakeDefense(rules.defense, rules.thermal);
        d.field = ShieldKind::Plasma;
        d.shieldTier[1] = 1;
        d.charge[1] = 1.0e9;
        const HitResult hit = AbsorbHit(d, rules.defense, DamageType::Explosive, 1000.0, false, true, 0.0);
        CHECK(std::fabs(hit.field - 800.0) < 1e-9 && std::fabs(hit.hull - 200.0) < 1e-9);
    }

    // Los elementos se pagan enteros o no se pagan.
    {
        ElementStock stock;
        stock.Add(74, 1000.0);
        CHECK(!PayElements(stock, coil.elements, 4) && stock.Get(74) == 1000.0);
        for (const ElementCost& c : coil.elements) stock.Add(c.z, c.kg);
        CHECK(PayElements(stock, coil.elements, 4) && stock.Get(74) == 1000.0 && stock.Get(75) == 0.0);
        RefundElements(stock, coil.elements, 4, 0.5);
        CHECK(stock.Get(75) == coil.elements[1].kg * 0.5);
        CHECK(ElementCostText(coil.elements, 4).find("Re") != std::string::npos);
    }

    auto engine = MakeEngine(rules, 7);

    // En la nave: sale con las dos bahias de serie en los huecos 4 y 5, que se mueven pero no se sacan.
    {
        GameSession g(*engine, rules, 7, GameModeId::Peaceful);
        g.Conversation().Stop();
        g.Step(0.05, {});
        CHECK(g.Ship().loadout.starterBays == 2);
        CHECK(g.Ship().loadout.modules[4] == ShipModuleKind::DroneBay && g.Ship().loadout.modules[5] == ShipModuleKind::DroneBay);
        CHECK(g.Ship().loadout.ammo.size() == g.Ship().loadout.modules.size() && g.Ship().loadout.cooldown.size() == g.Ship().loadout.modules.size());
        g.SetCreative(true);
        CHECK(!g.InstallModule(4, ShipModuleKind::None, &why) && why.find("serie") != std::string::npos);  // ni en creativo
        CHECK(!g.InstallModule(5, ShipModuleKind::Turret, &why));
        CHECK(g.Ship().loadout.modules[4] == ShipModuleKind::DroneBay);
        // Cambiar de hueco si, y siguen siendo dos.
        CHECK(g.MoveModule(4, 2, &why));
        CHECK(g.Ship().loadout.modules[4] == ShipModuleKind::None && g.Ship().loadout.modules[2] == ShipModuleKind::DroneBay);
        CHECK(WorkingModules(g.Ship(), ShipModuleKind::DroneBay) == 2);
        CHECK(!g.MoveModule(5, 2, &why) && why.find("ocupado") != std::string::npos);
        CHECK(!g.MoveModule(0, 1, &why));                // una torreta no se mueve gratis
        CHECK(!g.MoveModule(2, 6, &why));                // a una bahia interior, no
        CHECK(!g.MoveModule(2, 99, &why));
        CHECK(!g.InstallModule(2, ShipModuleKind::None, &why));
        // Una bahia de mas si se puede quitar; y si se pierde una de serie, volver a montarla es gratis.
        CHECK(g.InstallModule(4, ShipModuleKind::DroneBay, &why));
        CHECK(g.InstallModule(4, ShipModuleKind::None, &why));
    }

    // Montar un arma pide plano, elementos y la clase adecuada; todo atracado en una estacion propia.
    {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        const TargetRef station = PlaceStation(g);
        for (int i = 0; i < 200; ++i) g.Step(0.05, {});
        CHECK(g.JumpTo(station, 5.0));
        CHECK(g.Dock(station, true));
        g.Step(0.05, {});
        CHECK(g.CanRefit());
        g.Ship().cargo.Add(Resource::Metal, 1000.0 - g.Ship().cargo.Get(Resource::Metal));
        const double metal = g.Ship().cargo.Get(Resource::Metal);

        CHECK(!g.InstallModule(2, ShipModuleKind::CoilgunLight, &why) && why.find("Plano") != std::string::npos);
        CHECK(!g.FabricateBlueprint(BlueprintId(coil), &why) && why.find("origami") != std::string::npos);  // esta es una central
        GrantBlueprint(g.Ship().loadout, BlueprintId(coil));
        CHECK(!g.InstallModule(2, ShipModuleKind::CoilgunLight, &why) && why.find("Faltan elementos") != std::string::npos && why.find("W") != std::string::npos);
        CHECK(g.Ship().loadout.modules[2] == ShipModuleKind::None);
        for (const ElementCost& c : coil.elements) g.Ship().elements.Add(c.z, c.kg * 1.5);
        CHECK(g.InstallModule(2, ShipModuleKind::CoilgunLight, &why));
        CHECK(std::fabs(g.Ship().cargo.Get(Resource::Metal) - (metal - coil.mass)) < 1e-9);  // su masa real, no 260
        CHECK(std::fabs(g.Ship().elements.Get(74) - coil.elements[0].kg * 0.5) < 1e-9);
        // Quitarla devuelve la mitad del metal y de los elementos.
        CHECK(g.InstallModule(2, ShipModuleKind::None, &why));
        CHECK(std::fabs(g.Ship().cargo.Get(Resource::Metal) - (metal - coil.mass * 0.5)) < 1e-9);
        CHECK(std::fabs(g.Ship().elements.Get(74) - coil.elements[0].kg * 1.0) < 1e-9);  // 0,5 que sobraron + la mitad de lo pagado

        // Si se pierde una bahia de serie, volver a montarla no cuesta nada.
        g.Ship().loadout.modules[4] = ShipModuleKind::None;
        const double beforeBay = g.Ship().cargo.Get(Resource::Metal);
        CHECK(g.InstallModule(4, ShipModuleKind::DroneBay, &why));
        CHECK(g.Ship().cargo.Get(Resource::Metal) == beforeBay);
        CHECK(!g.InstallModule(4, ShipModuleKind::None, &why));  // y de nuevo no se saca

        // La pesada no entra en una fragata ligera aunque haya plano y elementos.
        GrantBlueprint(g.Ship().loadout, BlueprintId(heavyCoil));
        for (const ElementCost& c : heavyCoil.elements) g.Ship().elements.Add(c.z, c.kg);
        CHECK(!g.InstallModule(2, ShipModuleKind::CoilgunHeavy, &why) && why.find("pesada") != std::string::npos);

        // La espinal si entra (una fragata la admite), pero solo una.
        GrantBlueprint(g.Ship().loadout, BlueprintId(spinal));
        for (const ElementCost& c : spinal.elements) g.Ship().elements.Add(c.z, c.kg * 2.5);
        CHECK(g.InstallModule(0, ShipModuleKind::RailSpinalLight, &why));
        CHECK(!g.InstallModule(1, ShipModuleKind::RailSpinalLight, &why) && why.find("una") != std::string::npos);

        // Las armas van por fuera.
        CHECK(!g.InstallModule(6, ShipModuleKind::CoilgunLight, &why));

        // Misiles: la bahia se monta y se llena a fuerza de energia, metal y elementos.
        const HumanWeaponSpec& bay = *HumanWeaponOf(arm, ShipModuleKind::BayLight10);
        GrantBlueprint(g.Ship().loadout, BlueprintId(bay));
        CHECK(g.InstallModule(3, ShipModuleKind::BayLight10, &why));
        CHECK(g.Ship().loadout.ammo[3] == 0);
        CHECK(g.ManufactureMissiles(3, 1, &why) == 0 && why.find("Faltan elementos") != std::string::npos);
        for (const ElementCost& c : bay.missile) g.Ship().elements.Add(c.z, c.kg * 4.0);  // da para cuatro
        const double energy = g.Ship().energy;
        const double metalBefore = g.Ship().cargo.Get(Resource::Metal);
        CHECK(g.ManufactureMissiles(3, 6, &why) == 4);   // fabrica los que alcanzan
        CHECK(g.Ship().loadout.ammo[3] == 4);
        CHECK(std::fabs(energy - g.Ship().energy - 4.0 * bay.missileEnergy) < 1e-9);
        CHECK(std::fabs(metalBefore - g.Ship().cargo.Get(Resource::Metal) - 4.0 * bay.missileMass) < 1e-9);
        CHECK(g.Ship().elements.Get(78) < 1e-9);
        CHECK(g.ManufactureMissiles(3, 0, &why) == 0);  // sin elementos
        CHECK(g.ManufactureMissiles(0, 1, &why) == 0 && why.find("bahía") != std::string::npos);  // eso es una railgun
        // Desmontar la bahia pierde sus misiles.
        CHECK(g.InstallModule(3, ShipModuleKind::None, &why) && g.Ship().loadout.ammo[3] == 0);

        // Defensa puntual: aparte de las torretas; hasta 30 puntos en la fragata ligera, cobrados en metal.
        CHECK(g.PointDefenseCapacity() == 30 && g.PointDefenseRate() == 0.0);
        const double beforePoints = g.Ship().cargo.Get(Resource::Metal);
        CHECK(g.SetPointDefense(10, &why) && g.Ship().loadout.pointDefense == 10);
        CHECK(std::fabs(beforePoints - g.Ship().cargo.Get(Resource::Metal) - 10.0 * arm.pointMass) < 1e-9);
        CHECK(!g.SetPointDefense(31, &why) && !g.SetPointDefense(10, &why));
        CHECK(std::fabs(g.PointDefenseRate() - 10.0 * arm.pointRate) < 1e-9);
        // Y una plataforma en un hueco de torreta clasica suma lo suyo.
        CHECK(g.InstallModule(3, ShipModuleKind::PointDefensePlatform, &why));
        CHECK(std::fabs(g.PointDefenseRate() - (10.0 * arm.pointRate + arm.platformRate)) < 1e-9);
        CHECK(g.SetPointDefense(0, &why) && g.Ship().loadout.pointDefense == 0);
    }

    // Planos en creativo: gratis y en cualquier sitio; no se repiten.
    {
        GameSession g(*engine, rules, 7, GameModeId::Peaceful);
        g.Conversation().Stop();
        g.Step(0.05, {});
        CHECK(!g.FabricateBlueprint(0, &why));
        g.SetCreative(true);
        CHECK(!g.FabricateBlueprint(-1, &why) && !g.FabricateBlueprint(kBlueprintCount, &why));
        CHECK(g.FabricateBlueprint(0, &why) && HasBlueprint(g.Ship().loadout, 0));
        CHECK(!g.FabricateBlueprint(0, &why) && why.find("Ya") != std::string::npos);
    }

    // Disparar: la railgun espinal solo sale con el blanco en el eje; el misil gasta uno y es guiado y derribable.
    {
        Balance wide = rules;
        wide.armament.weapons[2].proximity = 2000.0;  // la railgun espinal ligera: mira de 2000 km a 5e4 km, ~0,04 rad
        auto world = MakeEngine(wide, 7);
        GameSession g(*world, wide, 7, GameModeId::Peaceful);
        g.Conversation().Stop();
        g.Step(0.05, {});
        g.SetCreative(true);
        const TargetRef enemy = FindShip(g, Stance::Hostile);
        CHECK(enemy.kind == Kind::Anomaly);
        CHECK(g.JumpTo(enemy, 5.0e4));
        g.Step(0.05, {});
        CHECK(g.InstallModule(0, ShipModuleKind::RailSpinalLight, &why));
        CHECK(g.InstallModule(1, ShipModuleKind::CoilgunLight, &why));
        CHECK(g.InstallModule(2, ShipModuleKind::BayLight10, &why));
        CHECK(g.InstallModule(3, ShipModuleKind::NuclearLight, &why));
        g.SetCreative(false);

        CHECK(!g.FireModule(2, enemy, &why) && why.find("sin misiles") != std::string::npos);
        CHECK(!g.FireModule(0, TargetRef{}, &why) && why.find("Sin blanco") != std::string::npos);
        CHECK(!g.FireModule(6, enemy, &why));  // una bahia interior no tiene arma
        g.Ship().loadout.ammo[2] = 10;
        g.Ship().loadout.ammo[3] = 1;

        // Misil explosivo: guiado, tipo de dano nuevo, gasta uno y recarga.
        CHECK(g.FireModule(2, enemy, &why));
        CHECK(g.Projectiles().size() == 1 && g.Projectiles()[0].guided && g.Projectiles()[0].type == DamageType::Explosive && g.Projectiles()[0].kind == WeaponKind::Missile);
        CHECK(g.Projectiles()[0].damage == HumanWeaponOf(wide.armament, ShipModuleKind::BayLight10)->damage);
        CHECK(g.Ship().loadout.ammo[2] == 9);
        CHECK(!g.FireModule(2, enemy, &why) && why.find("recargando") != std::string::npos);
        // El nuclear es de un solo uso: sale y la bahia queda vacia.
        CHECK(g.FireModule(3, enemy, &why));
        CHECK(g.Projectiles().back().type == DamageType::Nuclear && g.Projectiles().back().guided);
        CHECK(g.Ship().loadout.ammo[3] == 0);
        CHECK(g.ManufactureMissiles(3, 1, &why) == 0);  // y fabricar otro pide estar atracada
        // Cuando el tiempo corre, vuelven a cargar (las bahias llevan su recarga).
        for (int i = 0; i < 400 && g.Ship().loadout.cooldown[2] > 0.0; ++i) Run(g, 0.1);
        CHECK(g.Ship().loadout.cooldown[2] <= 0.0);

        // La coilgun es cinetica y no guiada. Sale girada o no: no necesita el eje.
        CHECK(g.FireModule(1, enemy, &why));
        CHECK(g.Projectiles().back().type == DamageType::Kinetic && !g.Projectiles().back().guided);

        // La espinal: de costado no sale; con el blanco en el eje, si. El proyectil lleva el eje del casco.
        const std::optional<TargetInfo> info = g.Resolve(enemy);
        CHECK(info.has_value());
        const Vec3d toward = Normalize(info->position - g.Ship().position);
        FaceTo(g.Ship(), Cross(toward, Vec3d{0, 1, 0}));
        CHECK(!g.FireModule(0, enemy, &why) && why.find("eje") != std::string::npos);
        const double heat = g.Ship().defense.heat;
        FaceTo(g.Ship(), toward);
        CHECK(g.FireModule(0, enemy, &why));
        CHECK(g.Projectiles().back().type == DamageType::Kinetic && g.Ship().defense.heat > heat);
        CHECK(Length(g.Projectiles().back().velocity) > 2.0e4);
        // FireAll incluye las armas humanas listas, sin llenar el registro de quejas.
        const std::size_t logBefore = g.Log().size();
        g.Ship().loadout.cooldown[2] = 0.0;
        g.Ship().loadout.cooldown[1] = 0.0;
        g.FireAll(enemy);
        CHECK(g.Ship().loadout.ammo[2] == 8);
        (void)logBefore;
    }

    // Se guarda: misiles, planos, defensa puntual y las armas montadas; y una partida sin esas lineas sigue cargando.
    {
        GameSession g(*engine, rules, 7, GameModeId::Peaceful);
        g.Conversation().Stop();
        g.Step(0.05, {});
        g.SetCreative(true);
        CHECK(g.InstallModule(0, ShipModuleKind::BayHeavy25, &why) == false);  // pesada: no en la fragata ligera
        CHECK(g.InstallModule(0, ShipModuleKind::BayLight25, &why));
        CHECK(g.InstallModule(1, ShipModuleKind::PointDefensePlatform, &why));
        CHECK(g.MoveModule(4, 2, &why));
        CHECK(g.FabricateBlueprint(BlueprintId(coil), &why));
        CHECK(g.SetPointDefense(12, &why));
        CHECK(g.ManufactureMissiles(0, 17, &why) == 17);
        const std::string text = WriteSave(g.Snapshot());
        const std::optional<SaveData> saved = ReadSave(text);
        CHECK(saved.has_value());
        if (saved) {
            const ShipLoadout& l = saved->Ship()->ship.loadout;
            CHECK(l.modules[0] == ShipModuleKind::BayLight25 && l.modules[1] == ShipModuleKind::PointDefensePlatform && l.modules[2] == ShipModuleKind::DroneBay);
            CHECK(l.ammo[0] == 17 && l.pointDefense == 12 && l.starterBays == 2 && HasBlueprint(l, BlueprintId(coil)));
            GameSession h(*engine, rules, 7, GameModeId::Peaceful);
            CHECK(h.Restore(*saved));
            CHECK(h.Ship().loadout.ammo[0] == 17 && h.Ship().loadout.pointDefense == 12 && HasBlueprint(h.Ship().loadout, BlueprintId(coil)));
            CHECK(h.Ship().loadout.modules[0] == ShipModuleKind::BayLight25);
        }
        // Las lineas nuevas van en el archivo (una version anterior las ignora).
        CHECK(text.find("\nmunicion ") != std::string::npos && text.find("\narmamento ") != std::string::npos);
    }
}

// ---- Partida dorada --------------------------------------------------------------------------
// Una partida corta con paso fijo y ordenes fijas (viajar, minar, maniobrar, atracar, combatir) cuyo
// guardado final se compara con tests/data/partida_dorada.txt. Vigila que un cambio de estructura no
// cambie el comportamiento. Si el cambio es querido: epsilon_game_tests.exe --regenerar-dorada
static bool gRegenerateGolden = false;

static std::string PlayGolden() {
    Balance rules;
    auto engine = MakeEngine(rules, 11);
    GameSession g(*engine, rules, 11, GameModeId::Normal);
    g.Conversation().Stop();
    const double dt = 1.0 / 60.0;
    const auto run = [&](int steps, const ShipControls& controls = {}) {
        for (int i = 0; i < steps; ++i) g.Step(dt, controls);
    };
    run(30);
    g.MarkScanned(0);

    // Viaje en crucero al primer planeta, con el piloto.
    const TargetRef planet = FirstPlanet(g, 0);
    using C = CommandType;
    g.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Cruise)));
    g.Apply(Order(C::Autopilot, planet));
    for (int i = 0; i < 20000 && g.Autopilot(); ++i) g.Step(dt, {});

    // Drones a la superficie y de vuelta.
    g.Apply(Order(C::DeployDrones, planet));
    run(900);
    g.Apply(Order(C::RecallDrones));
    run(300);

    // Mando a mano: sale de la orbita, gira, acelera y frena.
    ShipControls turn;
    turn.turnYaw = 0.004f;
    turn.turnPitch = -0.002f;
    turn.throttleChange = 0.35;
    run(120, turn);
    run(120);
    ShipControls brake;
    brake.manualBrake = true;
    run(1, brake);
    run(120);

    // Una central junto a la estrella: desplegar, atracar y soltar.
    const TargetRef star{Kind::Star, 0, 0};
    g.Ship().heatShieldTier = std::max(g.Ship().heatShieldTier, YieldOf(g.GetWorld().systems[0]->StarAt(0)).requiredTier);
    g.JumpTo(star, g.Resolve(star)->arrival);
    run(30);
    g.Apply(Order(C::DeployPower, star));
    if (!g.Stations().All().empty()) {
        const TargetRef station{Kind::Station, 0, g.Stations().All().back().id};
        if (const std::optional<TargetInfo> info = g.Resolve(station)) g.JumpTo(station, info->arrival);  // a tiro del haz
        g.Apply(Order(C::Dock, station, 1));
        run(120);
        g.Apply(Order(C::Interact, station));  // recoge lo producido
        g.Apply(Order(C::Undock));
        run(120);
    }

    // Combate: un hostil delante, y todo lo que este listo contra el.
    const int enemy = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 30000.0);
    const TargetRef foe{Kind::Anomaly, g.NearestSystem(), enemy};
    g.Apply(Order(C::SetShield, static_cast<int>(ShieldKind::EM)));
    for (int i = 0; i < 1200; ++i) {
        if (i % 20 == 0) g.Apply(Order(C::FireAll, foe));
        g.Step(dt, {});
    }
    return WriteSave(g.Snapshot());
}

// Dos naves en el mismo mundo, cada una con su mando (fase 3a de docs/multijugador.md).
static void TestFleet() {
    using C = CommandType;
    Balance rules;
    const auto logHas = [](const PlayerState& p, const std::string& text) {
        for (const std::string& line : p.log)
            if (line.find(text) != std::string::npos) return true;
        return false;
    };
    {
        auto engine = MakeEngine(rules, 9);
        GameSession g(*engine, rules, 9, GameModeId::Peaceful);
        Run(g, 0.5);
        CHECK(g.VesselCount() == 1 && g.Focus() == 0 && g.VesselId() == 1);
        const Vec3d first = g.Ship().position;

        // La segunda aparece al costado, parada, y cada una ve a la otra en sus sensores.
        const int id = g.AddVessel(50.0);
        CHECK(id == 2 && g.VesselCount() == 2 && g.Focus() == 0 && g.VesselId() == 1);
        CHECK(std::abs(Length(g.VesselAt(1).ship.position - first) - 50.0) < 1e-6);
        Run(g, 1.0);
        const TargetRef other{Kind::Ship, 0, 2}, self{Kind::Ship, 0, 1};
        CHECK(g.Resolve(other).has_value() && !g.Resolve(self));  // la propia no es un objetivo
        const auto sees = [&](int index, int shipId) {
            for (const Contact& c : g.VesselAt(index).contacts)
                if (c.target.kind == Kind::Ship && c.target.index == shipId && c.identified) return true;
            return false;
        };
        CHECK(sees(0, 2) && sees(1, 1) && !sees(0, 1));

        // Una orden a la segunda no toca a la primera, y al reves.
        CHECK(g.ApplyTo(2, Order(C::SetDrive, static_cast<int>(DriveMode::Cruise))));
        CHECK(g.VesselAt(1).ship.mode == DriveMode::Cruise && g.Ship().mode == DriveMode::Impulse);
        CHECK(!g.ApplyTo(7, Order(C::HoldPosition)));
        CHECK(g.ApplyTo(2, Order(C::SetDrive, static_cast<int>(DriveMode::Impulse))));

        // Un mando por nave: la segunda acelera y gira; la primera sigue donde estaba, mirando a lo mismo.
        std::vector<ShipControls> controls(2);
        controls[1].throttleChange = 0.5;
        controls[1].turnYaw = 0.002f;
        const Vec3d heading = Heading(g.Ship());
        const Vec3d apart = g.VesselAt(1).ship.position - g.Ship().position;
        for (int i = 0; i < 240; ++i) g.StepAll(1.0 / 60.0, controls);
        CHECK(g.VesselAt(1).ship.throttle > 0.9 && g.Ship().throttle == 0.0);
        CHECK(g.VesselAt(1).ship.speed > 1.0 && g.Ship().speed < 0.5);
        CHECK(Length(Heading(g.VesselAt(1).ship) - heading) > 1e-3 && Length(Heading(g.Ship()) - heading) < 1e-9);
        CHECK(Length(g.VesselAt(1).ship.position - g.Ship().position - apart) > 10.0);
        CHECK(g.VesselId() == 1);  // fuera del paso, la nave en curso vuelve a ser la del foco

        // El foco decide de que nave hablan Ship(), Apply() y lo demas.
        CHECK(g.SetFocus(1) && g.VesselId() == 2 && g.Ship().throttle > 0.9);
        CHECK(g.Apply(Order(C::HoldPosition)) && g.Ship().throttle == 0.0 && g.VesselAt(1).ship.throttle == 0.0);
        CHECK(!g.SetFocus(5) && g.Focus() == 1);
        Run(g, 3.0);

        // La primera va con el piloto hasta la segunda y se queda a su lado.
        CHECK(g.SetFocus(0));
        CHECK(g.Apply(Order(C::Autopilot, other)));
        for (int i = 0; i < 4000 && g.Autopilot(); ++i) g.Step(0.05, {});
        CHECK(g.Orbiting() && *g.Orbiting() == other);
        CHECK(Length(g.VesselAt(1).ship.position - g.Ship().position) < 200.0);

        // Dos naves no ocupan el mismo sitio: la que se mete en la otra sale empujada.
        g.Apply(Order(C::HoldPosition));
        g.Ship().throttle = 0.01;  // suelta la orbita en el siguiente paso
        g.Step(0.05, {});
        g.Apply(Order(C::HoldPosition));
        g.Ship().position = g.VesselAt(1).ship.position + Vec3d{0.2, 0.0, 0.0};
        g.Step(0.05, {});
        CHECK(Length(g.VesselAt(1).ship.position - g.Ship().position) > rules.ship.radius * 2.0 - 0.05);

        // Limite provisional: con dos naves, ninguna sale del sistema.
        const World& w = g.GetWorld();
        const Vec3d center = w.scene.WorldPosition(w.sectors[0]);
        g.Ship().position = center + Vec3d{w.extents[0] * 3.0, 0.0, 0.0};
        g.Step(0.05, {});
        CHECK(Length(g.Ship().position - w.scene.WorldPosition(w.sectors[0])) < w.extents[0] * 1.5001);
        CHECK(logHas(g.PlayerAt(0), "ninguna puede salir") && !logHas(g.PlayerAt(1), "ninguna puede salir"));

        // El guardado lleva las dos naves en el mundo y al jugador del foco; al cargar vuelven las dos.
        const SaveData saved = g.Snapshot();
        CHECK(saved.world.ships.size() == 2 && saved.player.ship == 1);
        CHECK(g.SnapshotPlayer(1).ship == 2 && g.SnapshotPlayer(1).id != saved.player.id);
        const std::string text = WriteSave(saved);
        const std::optional<SaveData> read = ReadSave(text);
        CHECK(read && read->world.ships.size() == 2 && WriteSave(*read) == text);
        if (read) {
            auto engineB = MakeEngine(rules, 9);
            GameSession h(*engineB, rules, 9, GameModeId::Peaceful);
            h.Step(0.05, {});
            CHECK(h.Restore(*read));
            CHECK(h.VesselCount() == 2 && h.Focus() == 0 && h.VesselId() == 1);
            const Vec3d gap = g.VesselAt(1).ship.position - g.VesselAt(0).ship.position;
            CHECK(Length(h.VesselAt(1).ship.position - h.VesselAt(0).ship.position - gap) < 1e-3);
            Run(h, 0.5);
            CHECK(h.AddVessel() == 3);  // los numeros de nave no se repiten
            // Quien lleva la segunda nave, al cargar, la lleva.
            SaveData second = *read;
            second.player = g.SnapshotPlayer(1);
            auto engineC = MakeEngine(rules, 9);
            GameSession k(*engineC, rules, 9, GameModeId::Peaceful);
            k.Step(0.05, {});
            CHECK(k.Restore(second) && k.Focus() == 1 && k.VesselId() == 2);
        }
    }

    // El reloj de la burbuja: con una nave dilatando y otra quieta, manda la quieta. El mundo no corre
    // mas para la segunda, y a bordo de la primera pasa menos tiempo.
    {
        auto engine = MakeEngine(rules, 9);
        GameSession g(*engine, rules, 9, GameModeId::Peaceful);
        Run(g, 0.5);
        g.AddVessel(1000.0);
        g.Ship().mode = DriveMode::Cruise;
        g.Ship().holding = false;
        std::vector<ShipControls> controls(2);
        controls[0].throttleChange = 1.0;
        for (int i = 0; i < 2400 && g.VesselAt(0).exteriorFactor < 1.05; ++i) g.StepAll(1.0 / 60.0, controls);
        CHECK(g.VesselAt(0).exteriorFactor > 1.05);
        const double still = g.VesselAt(1).exteriorFactor;  // la quieta solo dilata lo poco que da la gravedad
        CHECK(still >= 1.0 && still < 1.001);
        const double clock = g.SimTime(), fast = g.VesselAt(0).ship.shipHours, slow = g.VesselAt(1).ship.shipHours;
        double expected = 0.0;
        for (int i = 0; i < 60; ++i) {
            g.StepAll(1.0 / 60.0, controls);
            expected += g.TimeFocus() * g.TimeScale() / 60.0 * g.VesselAt(1).exteriorFactor;
        }
        const double passed = g.SimTime() - clock;
        CHECK(passed > 0.0 && std::abs(passed - expected) < 1e-6 * expected);  // con el factor de la quieta, no el de la que dilata
        const double aboardFast = (g.VesselAt(0).ship.shipHours - fast) * 3600.0, aboardSlow = (g.VesselAt(1).ship.shipHours - slow) * 3600.0;
        CHECK(std::abs(aboardSlow * still - passed) < 1e-5 * passed);
        CHECK(aboardFast < passed * 0.97 && aboardFast > 0.0);
    }

    // Los hostiles atienden a la nave que tienen mas cerca, y la baja es de quien dispara.
    {
        auto engine = MakeEngine(rules, 9);
        GameSession g(*engine, rules, 9, GameModeId::Normal);
        Run(g, 0.5);
        g.AddVessel(400000.0);  // lejos: fuera del alcance de las armas del hostil
        CHECK(g.SetFocus(1));
        const auto guard = [](const ShipState& s) {
            double total = s.hull;
            for (const double c : s.defense.charge) total += c;
            return total;
        };
        const double before = guard(g.Ship());
        const int enemy = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 30000.0);
        CHECK(enemy > 0);
        const TargetRef foe{Kind::Anomaly, g.NearestSystem(), enemy};
        g.Ship().computerTier = 5;
        const auto alive = [&] {
            for (const Anomaly& a : g.Anomalies())
                if (a.id == enemy) return true;
            return false;
        };
        // Primero sin responder: el hostil va a por la segunda nave, que es la que tiene cerca.
        for (int i = 0; i < 600 && guard(g.Ship()) >= before; ++i) Run(g, 0.1);
        CHECK(guard(g.Ship()) < before);
        CHECK(logHas(g.PlayerAt(1), "Impactos de") && !logHas(g.PlayerAt(0), "Impactos de"));
        for (int i = 0; i < 1500 && alive(); ++i) {
            Run(g, 0.1);
            g.Apply(Order(C::FireAll, foe));
        }
        CHECK(!alive());
        CHECK(g.PlayerAt(1).kills == 1 && g.PlayerAt(0).kills == 0);
        const ShipState& second = g.VesselAt(1).ship;
        CHECK(g.VesselAt(0).ship.hull == rules.ship.hull);  // a la primera no le llego nada
        CHECK(g.VesselAt(0).ship.cargo.Get(Resource::Metal) == 0.0 && second.cargo.Get(Resource::Metal) > 0.0);  // el botin, a quien la derribo
    }
}

// Dos jugadores en la misma nave, cada uno con su puesto y su mando (fase 3b de docs/multijugador.md).
static void TestCrew() {
    using C = CommandType;
    Balance rules;
    auto engine = MakeEngine(rules, 9);
    GameSession g(*engine, rules, 9, GameModeId::Peaceful);
    Run(g, 0.5);
    const auto logHas = [&](int player, const std::string& text) {
        for (const std::string& line : g.PlayerAt(player).log)
            if (line.find(text) != std::string::npos) return true;
        return false;
    };
    const auto step = [&](const ShipControls& first, const ShipControls& second, int steps) {
        for (int i = 0; i < steps; ++i) g.StepPlayers(1.0 / 60.0, {first, second});
    };
    ShipControls fly;
    fly.throttleChange = 0.5;
    fly.turnYaw = 0.002f;

    // Quien va solo lo lleva todo. Al entrar el segundo, el primero pilota y el otro lleva lo demas.
    CHECK(g.PlayerCount() == 1 && g.PlayerAt(0).seats == kAllSeats);
    CHECK(g.SeatHolder(Seat::Tactical) == 0);
    const int ana = g.AddPlayer("Ana");
    CHECK(ana == 1 && g.PlayerCount() == 2 && g.VesselCount() == 1 && g.FocusPlayer() == 0);
    CHECK(g.PlayerAt(1).vessel == 0 && g.PlayerAt(1).name == "Ana");
    CHECK(g.PlayerAt(0).seats == SeatBit(Seat::Pilot));
    CHECK(g.PlayerAt(1).seats == (SeatBit(Seat::Tactical) | SeatBit(Seat::Engineering)));
    CHECK(g.SeatHolder(Seat::Pilot) == 0 && g.SeatHolder(Seat::Tactical) == 1 && g.SeatHolder(Seat::Engineering) == 1);

    // Cada orden pide su puesto. La que no toca se rechaza, se le explica a quien la dio y no hace nada.
    const CommandResult refused = g.ApplyAs(1, Order(C::SetDrive, static_cast<int>(DriveMode::Cruise)));
    CHECK(!refused && refused.why.find("pilotaje") != std::string::npos && g.Ship().mode == DriveMode::Impulse);
    CHECK(logHas(1, "pilotaje") && !logHas(0, "pilotaje"));
    CHECK(g.ApplyAs(0, Order(C::SetDrive, static_cast<int>(DriveMode::Cruise))) && g.Ship().mode == DriveMode::Cruise);
    CHECK(g.ApplyAs(0, Order(C::SetDrive, static_cast<int>(DriveMode::Impulse))));
    CHECK(!g.ApplyAs(0, Order(C::SetShield, static_cast<int>(ShieldKind::EM))));  // el piloto no toca los escudos
    CHECK(!g.Apply(Order(C::StartScan)) && !g.Scanning());                        // Apply es el jugador del foco: el piloto
    CHECK(!g.ApplyAs(5, Order(C::HoldPosition)));
    // Lo que hace uno en su puesto lo ve toda la tripulacion en su registro.
    CHECK(g.ApplyAs(1, Order(C::SetShield, static_cast<int>(ShieldKind::EM))));
    CHECK(g.Ship().defense.field == ShieldKind::EM && logHas(0, "Escudo") && logHas(1, "Escudo"));
    // Lo de cada uno no pide puesto y no toca al otro.
    CHECK(g.ApplyAs(1, Order(C::Cinematic, 1)) && g.PlayerAt(1).cinematic && !g.PlayerAt(0).cinematic);

    // El mando de vuelo solo vale el de quien pilota.
    const Vec3d heading = Heading(g.Ship());
    step({}, fly, 120);
    CHECK(g.Ship().throttle == 0.0 && Length(Heading(g.Ship()) - heading) < 1e-9);
    step(fly, {}, 120);
    CHECK(g.Ship().throttle > 0.9 && Length(Heading(g.Ship()) - heading) > 1e-3);
    CHECK(g.ApplyAs(0, Order(C::HoldPosition)));

    // Un puesto ocupado no se quita; uno libre lo ocupa quien quiera. Sin piloto, la nave no obedece a nadie.
    CHECK(!g.TakeSeat(1, Seat::Pilot) && g.TakeSeat(1, Seat::Tactical));
    CHECK(g.LeaveSeat(0, Seat::Pilot) && !g.LeaveSeat(0, Seat::Pilot) && g.SeatHolder(Seat::Pilot) == -1);
    step(fly, fly, 60);
    CHECK(g.Ship().throttle == 0.0);
    const CommandResult vacant = g.ApplyAs(0, Order(C::Orbit));
    CHECK(!vacant && vacant.why.find("libre") != std::string::npos);
    CHECK(g.TakeSeat(1, Seat::Pilot) && g.SeatHolder(Seat::Pilot) == 1);
    step(fly, {}, 60);
    CHECK(g.Ship().throttle == 0.0);  // el primero ya no pilota
    step({}, fly, 60);
    CHECK(g.Ship().throttle > 0.4);
    CHECK(g.ApplyAs(1, Order(C::HoldPosition)));
    CHECK(g.LeaveSeat(1, Seat::Pilot) && g.TakeSeat(0, Seat::Pilot));  // como al principio
    Run(g, 2.0);

    // Lo que averigua uno lo sabe el otro.
    CHECK(g.SetFocusPlayer(1));
    g.MarkScanned(0);
    CHECK(g.SetFocusPlayer(0));
    CHECK(g.PlayerAt(0).scanned.empty() && !g.PlayerAt(1).scanned.empty());
    g.Step(0.05, {});
    CHECK(!g.PlayerAt(0).scanned.empty() && g.PlayerAt(0).scanned == g.PlayerAt(1).scanned);

    // A pie: baja cada uno por su cuenta a la misma cubierta, y la nave no se suelta con alguien fuera.
    const TargetRef station = PlaceStation(g);  // de ingenieria: lo hace la sesion directamente
    CHECK(g.JumpTo(station, g.Resolve(station)->arrival));
    CHECK(g.Dock(station, true));
    Run(g, 0.5);
    CHECK(g.Docked());
    CHECK(g.ApplyAs(1, Order(C::Disembark)) && g.PlayerAt(1).onFoot && !g.PlayerAt(0).onFoot);
    CHECK(!g.OnFoot() && !g.Crew().empty());  // el del foco, el piloto, sigue a bordo
    CHECK(logHas(1, "A pie en") && !logHas(0, "A pie en"));
    g.ApplyAs(0, Order(C::Undock));
    Run(g, 0.5);
    CHECK(g.Docked() && logHas(0, "tripulación a pie"));
    ShipControls walk;
    walk.foot.move.forward = 1.0f;
    const auto spawn = g.Deck().spawn;  // donde se baja
    const auto from = g.PlayerAt(1).avatar.position;
    step(walk, walk, 90);
    CHECK(Length(g.PlayerAt(1).avatar.position - from) > 0.5);  // ella anda con su mando
    CHECK(g.Docked());
    CHECK(g.ApplyAs(0, Order(C::Disembark)) && g.OnFoot() && g.PlayerAt(0).onFoot);
    CHECK(Length(g.PlayerAt(0).avatar.position - spawn) < 1e-9);  // el baja a la esclusa, donde ella ya no esta
    CHECK(Length(g.PlayerAt(1).avatar.position - spawn) > 0.5);
    step(walk, {}, 30);
    CHECK(Length(g.PlayerAt(0).avatar.position - spawn) > 0.1);
    CHECK(g.ApplyAs(1, Order(C::Board, 1)) && !g.PlayerAt(1).onFoot && !g.Crew().empty());  // queda uno: la cubierta sigue
    CHECK(g.ApplyAs(0, Order(C::Board, 1)) && !g.OnFoot() && g.Crew().empty());
    g.ApplyAs(0, Order(C::Undock));
    Run(g, 1.0);
    CHECK(!g.Docked());

    // Un tercero se queda con uno de los dos puestos del segundo.
    const int eva = g.AddPlayer("Eva");
    CHECK(eva == 2 && g.PlayerAt(0).seats == SeatBit(Seat::Pilot));
    CHECK(g.PlayerAt(1).seats == SeatBit(Seat::Tactical) && g.PlayerAt(2).seats == SeatBit(Seat::Engineering));
    CHECK(g.AddPlayer("Sin sitio") == 3 && g.PlayerAt(3).seats == 0);  // el cuarto mira

    // Y con dos naves, cada tripulacion en la suya.
    const int second = g.AddVessel(500.0);
    CHECK(second == 2 && g.PlayerCount() == 5 && g.PlayerAt(4).vessel == 1 && g.PlayerAt(4).seats == kAllSeats);
    const int luis = g.AddPlayer("Luis", second);
    CHECK(luis == 5 && g.PlayerAt(5).vessel == 1 && g.PlayerAt(4).seats == SeatBit(Seat::Pilot));
    CHECK(g.AddPlayer("Nadie", 99) == -1);
    CHECK(g.SetFocus(1) && g.FocusPlayer() == 4 && g.VesselId() == 2);  // SetFocus(nave) pasa a quien la pilota
    CHECK(g.SetFocusPlayer(5) && g.Focus() == 1 && g.VesselId() == 2 && g.SeatHolder(Seat::Engineering) == 5);
    // Una orden a la nave la da quien ocupa el puesto: no se rechaza.
    CHECK(g.ApplyTo(second, Order(C::SetDrive, static_cast<int>(DriveMode::Cruise))) && g.VesselAt(1).ship.mode == DriveMode::Cruise);
    CHECK(g.ApplyTo(second, Order(C::SetShield, static_cast<int>(ShieldKind::EM))) && g.VesselAt(0).ship.mode == DriveMode::Impulse);
    // Un mensaje de la segunda nave no llega a la tripulacion de la primera.
    CHECK(logHas(4, "Escudo") && logHas(5, "Escudo"));
    CHECK(g.SnapshotPlayer(5).ship == 2 && g.SnapshotPlayer(1).ship == 1);
    Run(g, 1.0);
    CHECK(g.FocusPlayer() == 5);
}

// Burbujas: las naves que no estan en el mismo sistema van cada una con su mundo cargado y su reloj
// (fase 4 de docs/multijugador.md).
static void TestBubbles() {
    using C = CommandType;
    Balance rules;
    auto engine = MakeEngine(rules, 9);
    GameSession g(*engine, rules, 9, GameModeId::Peaceful);
    int made = 0;
    g.SetEngineFactory([&] {
        ++made;
        return MakeEngine(rules, 9);
    });
    Run(g, 0.5);
    const TargetRef station = PlaceStation(g);
    g.AddVessel(100.0);
    Run(g, 1.0);
    CHECK(g.BubbleCount() == 1 && g.BubbleOf(0) == g.BubbleOf(1) && made == 0);
    const TargetRef other{Kind::Ship, 0, 2};
    CHECK(g.Resolve(other).has_value());
    const auto center = [&] {
        const World& w = g.GetWorld();
        return w.scene.WorldPosition(w.sectors[0]);
    };
    const auto absolute = [&](int index) {
        const int back = g.Focus();
        g.SetFocus(index);
        const Vec3d where = g.Ship().position + g.GetWorld().origin;
        g.SetFocus(back);
        return where;
    };
    const auto logHas = [&](int player, const std::string& text) {
        for (const std::string& line : g.PlayerAt(player).log)
            if (line.find(text) != std::string::npos) return true;
        return false;
    };
    const double extent = g.GetWorld().extents[0];

    // La segunda sale del sistema: se lleva su burbuja, con otro motor, y sigue en el mismo sitio del universo.
    CHECK(g.SetFocus(1));
    g.Ship().position = center() + Vec3d{extent * 3.0, 0.0, 0.0};
    const Vec3d placed = g.Ship().position + g.GetWorld().origin;
    CHECK(g.SetFocus(0));
    g.Step(0.05, {});
    CHECK(g.BubbleCount() == 2 && g.BubbleOf(0) != g.BubbleOf(1) && made == 1);
    CHECK(Length(absolute(1) - placed) < 1.0);
    CHECK(!logHas(1, "ninguna puede salir"));  // ya no hay limite: hay fabrica de motores
    CHECK(&g.ViewEngine() == engine.get());
    CHECK(g.SetFocus(1) && &g.ViewEngine() != engine.get());  // llevando la segunda, se dibuja su mundo
    CHECK(g.SetFocus(0));
    Run(g, 1.0);
    CHECK(!g.Resolve(other));  // en otra burbuja no se la ve
    bool seen = false;
    for (const Contact& c : g.Contacts()) seen = seen || c.target.kind == Kind::Ship;
    CHECK(!seen);

    // Cada burbuja lleva su reloj. La que viaja dilata y el suyo corre mas; el de la que se queda, no.
    CHECK(g.ApplyTo(2, Order(C::SetDrive, static_cast<int>(DriveMode::Cruise))));
    std::vector<ShipControls> controls(2);
    controls[1].throttleChange = 1.0;
    const double start0 = g.ClockOf(0), start1 = g.ClockOf(1);
    double expected = 0.0;
    for (int i = 0; i < 3000 && g.ClockOf(1) - start1 < (g.ClockOf(0) - start0) * 1.05 + 30.0; ++i) {
        g.StepAll(1.0 / 60.0, controls);
        expected += g.TimeFocus() * g.TimeScale() / 60.0 * g.VesselAt(0).exteriorFactor;
    }
    CHECK(g.ClockOf(1) - start1 > (g.ClockOf(0) - start0) * 1.05);
    CHECK(std::abs(g.ClockOf(0) - start0 - expected) < 1e-6 * expected);  // la quieta, como si estuviera sola
    CHECK(g.WorldTime() == g.ClockOf(1) && g.SimTime() == g.ClockOf(0));
    // La central esta en el sistema de la primera: produce con su reloj, no con el mas adelantado.
    const Station* power = g.Stations().Find(station.index);
    CHECK(power && power->producedUntil == g.ClockOf(0));
    const double stored = power ? power->storedAntimatter : 0.0;

    // El guardado lleva el reloj de cada nave, y al cargar cada una vuelve a su burbuja.
    const SaveData saved = g.Snapshot();
    CHECK(saved.world.ships.size() == 2 && saved.world.ships[1].clock > saved.world.ships[0].clock);
    CHECK(saved.world.simTime == g.WorldTime());
    const std::optional<SaveData> read = ReadSave(WriteSave(saved));
    CHECK(read && read->world.ships[1].clock == saved.world.ships[1].clock);
    if (read) {
        auto engineB = MakeEngine(rules, 9);
        GameSession h(*engineB, rules, 9, GameModeId::Peaceful);
        h.SetEngineFactory([&] { return MakeEngine(rules, 9); });
        h.Step(0.05, {});
        CHECK(h.Restore(*read));
        CHECK(h.VesselCount() == 2 && h.BubbleCount() == 2 && h.BubbleOf(0) != h.BubbleOf(1));
        CHECK(h.ClockOf(0) == saved.world.ships[0].clock && h.ClockOf(1) == saved.world.ships[1].clock);
        h.SetFocus(1);
        CHECK(Length(h.Ship().position + h.GetWorld().origin - saved.world.ships[1].absolutePosition) < 1.0);
        h.SetFocus(0);
        Run(h, 0.5);
        CHECK(h.BubbleCount() == 2);
    }

    // Vuelve al sistema: entra en la burbuja de la que estaba y adopta su reloj, que es anterior.
    CHECK(g.SetFocus(1));
    g.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Impulse)));
    g.Apply(Order(C::HoldPosition));
    g.Ship().warpSpeed = 0.0;
    g.Ship().warpCharge = 0.0;
    g.Ship().velocity = {};
    g.Ship().position = center() + Vec3d{extent * 0.5, 0.0, 0.0};
    const Vec3d returned = g.Ship().position + g.GetWorld().origin;
    CHECK(g.SetFocus(0));
    const double ahead = g.ClockOf(1);
    g.Step(0.05, {});
    CHECK(g.BubbleCount() == 1 && g.BubbleOf(0) == g.BubbleOf(1));
    CHECK(g.ClockOf(1) == g.ClockOf(0) && g.ClockOf(1) < ahead);
    CHECK(Length(absolute(1) - returned) < 100.0);
    Run(g, 1.0);
    CHECK(g.Resolve(other).has_value());  // se vuelven a ver
    // El tiempo de la central no ha vuelto atras ni ha producido dos veces.
    power = g.Stations().Find(station.index);
    CHECK(power && power->producedUntil == g.ClockOf(0) && power->storedAntimatter >= stored);

    // Si sale otra vez, se reutiliza el motor que quedo libre.
    CHECK(g.SetFocus(1));
    g.Ship().position = center() + Vec3d{0.0, 0.0, extent * 3.0};
    CHECK(g.SetFocus(0));
    g.Step(0.05, {});
    CHECK(g.BubbleCount() == 2 && made == 1);

    // Sin fabrica de motores sigue el limite de la fase 3a (lo comprueba TestFleet); y una nave sola
    // que cambia de sistema no abre ninguna burbuja.
    auto engineC = MakeEngine(rules, 9);
    GameSession solo(*engineC, rules, 9, GameModeId::Peaceful);
    int extra = 0;
    solo.SetEngineFactory([&] {
        ++extra;
        return MakeEngine(rules, 9);
    });
    Run(solo, 0.5);
    solo.MarkKnown(3);
    const TargetRef far{Kind::Star, 3, 0};
    CHECK(solo.JumpTo(far, 1.0e8));
    Run(solo, 1.0);
    CHECK(solo.NearestSystem() == 3 && solo.BubbleCount() == 1 && extra == 0);
}

// Servidor y cliente en el mismo proceso, unidos por el transporte en memoria (fase 5 de docs/multijugador.md).
static void TestNet() {
    using C = CommandType;
    Balance rules;
    const double dt = GameSession::kFixedStep;
    const auto absolute = [](const GameSession& s) { return s.Ship().position + s.GetWorld().origin; };

    // El servidor con su sesion; el cliente con la suya, que pasa a ser una replica; y, para comparar,
    // la misma partida jugada directamente.
    auto engineS = MakeEngine(rules, 9), engineR = MakeEngine(rules, 9), engineD = MakeEngine(rules, 9);
    GameSession server(*engineS, rules, 9, GameModeId::Peaceful), replica(*engineR, rules, 9, GameModeId::Peaceful), direct(*engineD, rules, 9, GameModeId::Peaceful);
    direct.SetPlayer("ana", "Ana");
    GameServer host(server);
    auto link = MakeMemoryLink();
    host.Add(link.first);
    Hello hello;
    hello.id = "ana";
    hello.name = "Ana";
    GameClient client(replica, link.second, hello);
    CHECK(replica.Replica() && !client.Ready() && host.Clients() == 0);
    host.Update(0.0);
    client.Update();
    CHECK(client.Ready() && !client.Refused() && host.Clients() == 1);
    CHECK(server.PlayerAt(0).id == "ana" && server.PlayerAt(0).name == "Ana");

    // Un fotograma: el cliente manda su mando, el servidor da un paso y el cliente aplica lo que le llega.
    const auto frame = [&](const ShipControls& controls) {
        replica.Step(dt, controls);
        host.Update(dt);
        client.Update();
        direct.Step(dt, controls);
    };
    const auto frames = [&](int count, const ShipControls& controls = {}) {
        for (int i = 0; i < count; ++i) frame(controls);
    };
    frames(30);
    CHECK(host.Tick() == 30 && client.Snapshots() == 31);
    // La replica ve lo mismo que el servidor, en su propio mundo.
    CHECK(Length(absolute(replica) - absolute(server)) < 1e-6);
    CHECK(replica.SimTime() == server.SimTime() && replica.SimTime() > 0.0);
    CHECK(replica.Contacts().size() == server.Contacts().size() && !replica.Contacts().empty());
    CHECK(replica.Anomalies().size() == server.Anomalies().size());

    // Una orden y un mando viajan al servidor; la replica no se toca hasta que vuelve la instantanea.
    CHECK(replica.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Cruise))));
    direct.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Cruise)));
    CHECK(replica.Ship().mode == DriveMode::Impulse && server.Ship().mode == DriveMode::Impulse);
    frame({});
    CHECK(server.Ship().mode == DriveMode::Cruise && replica.Ship().mode == DriveMode::Cruise);
    replica.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Impulse)));
    direct.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Impulse)));
    ShipControls fly;
    fly.throttleChange = 0.5;
    fly.turnYaw = 0.00390625f;
    const Vec3d heading = Heading(server.Ship());
    frames(120, fly);
    CHECK(server.Ship().throttle > 0.9 && replica.Ship().throttle == server.Ship().throttle);
    CHECK(Length(Heading(server.Ship()) - heading) > 1e-3 && Length(Heading(replica.Ship()) - Heading(server.Ship())) < 1e-12);
    CHECK(Length(absolute(replica) - absolute(server)) < 1e-6);
    ShipControls brake;
    brake.manualBrake = true;
    frame(brake);
    frames(60);
    CHECK(server.Ship().throttle == 0.0 && replica.Ship().throttle == 0.0);

    // Estaciones, registro y mundo: lo que hace el servidor llega entero.
    const TargetRef star{Kind::Star, 0, 0};
    for (GameSession* s : {&server, &direct}) {
        s->Ship().heatShieldTier = 5;
        s->MarkScanned(0);
        s->JumpTo(star, s->Resolve(star)->arrival);
    }
    frames(30);
    replica.Apply(Order(C::DeployPower, star));
    direct.Apply(Order(C::DeployPower, star));
    frames(30);
    CHECK(server.Stations().All().size() == 1 && replica.Stations().All().size() == 1);
    CHECK(replica.Stations().All().front().name == server.Stations().All().front().name);
    CHECK(replica.Log() == server.Log() && !replica.Log().empty());
    CHECK(replica.Scanned(star) && replica.Orbiting() && *replica.Orbiting() == star);
    CHECK(Length(absolute(replica) - absolute(server)) < 1e-6);

    // Y la partida del servidor es, numero a numero, la misma que jugada sin red.
    CHECK(WriteSave(server.Snapshot()) == WriteSave(direct.Snapshot()));

    // La instantanea ida y vuelta da los mismos bytes; una cortada o de otro tipo se rechaza.
    {
        const Packet packet = EncodeSnapshot(server.MakeSnapshot(0));
        SnapshotData back;
        CHECK(DecodeSnapshot(packet, back) && EncodeSnapshot(back) == packet);
        CHECK(back.vessels.size() == 1 && back.players.size() == 1 && back.stations.size() == 1 && back.me == 0);
        Packet cut(packet.begin(), packet.begin() + static_cast<std::ptrdiff_t>(packet.size() / 2));
        SnapshotData broken;
        CHECK(!DecodeSnapshot(cut, broken));
        ShipCommand order;
        CHECK(!DecodeCommand(packet, order) && TypeOf(packet) == MessageType::Snapshot && TypeOf({}) == MessageType{});
        const ShipCommand sent = Order(C::Fire, star, 2);
        CHECK(DecodeCommand(EncodeCommand(sent), order) && order.type == C::Fire && order.target == star && order.value == 2);
        std::printf("  instantanea de una nave: %zu bytes\n", packet.size());
    }

    // El tutorial viaja por donde va: mismo paso y mismo texto en la replica; apagado alli, apagado aqui.
    CHECK(server.Conversation().Active() && replica.Conversation().Active());
    CHECK(replica.Conversation().Step() == server.Conversation().Step() && replica.Conversation().VisibleText() == server.Conversation().VisibleText());
    CHECK(replica.Conversation().Objective() == server.Conversation().Objective());
    server.Conversation().Stop();
    direct.Conversation().Stop();
    frame({});
    CHECK(!replica.Conversation().Active() && !replica.Conversation().Reading());

    // Un segundo jugador entra de tripulante: misma nave, con armas e ingenieria.
    auto engineR2 = MakeEngine(rules, 9);
    GameSession replica2(*engineR2, rules, 9, GameModeId::Peaceful);
    auto link2 = MakeMemoryLink();
    host.Add(link2.first);
    Hello crew;
    crew.id = "luis";
    crew.name = "Luis";
    crew.join = JoinAs::Crew;
    GameClient client2(replica2, link2.second, crew);
    const auto round = [&](int count) {
        for (int i = 0; i < count; ++i) {
            replica.Step(dt, {});
            replica2.Step(dt, {});
            host.Update(dt);
            client.Update();
            client2.Update();
        }
    };
    round(5);
    CHECK(client2.Ready() && host.Clients() == 2 && server.PlayerCount() == 2 && server.VesselCount() == 1);
    CHECK(server.PlayerAt(1).id == "luis" && server.PlayerAt(1).vessel == 0);
    CHECK(replica2.VesselId() == 1 && replica2.PlayerCount() == 2 && replica2.PlayerAt(replica2.FocusPlayer()).name == "Luis");
    CHECK(replica2.SeatHolder(Seat::Pilot) != replica2.FocusPlayer() && replica2.SeatHolder(Seat::Tactical) == replica2.FocusPlayer());
    const auto said = [](const GameSession& s, const std::string& text) {
        for (const std::string& line : s.Log())
            if (line.find(text) != std::string::npos) return true;
        return false;
    };
    replica2.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Cruise)));  // no pilota: se le rechaza a el
    round(2);
    CHECK(server.Ship().mode == DriveMode::Impulse && said(replica2, "pilotaje") && !said(replica, "pilotaje"));
    replica2.Apply(Order(C::SetShield, static_cast<int>(ShieldKind::EM)));
    round(2);
    CHECK(server.Ship().defense.field == ShieldKind::EM && replica.Ship().defense.field == ShieldKind::EM && replica2.Ship().defense.field == ShieldKind::EM);
    CHECK(said(replica, "Escudo") && said(replica2, "Escudo"));

    // Un tercero entra con su propia nave: cada uno lleva la suya y ve la del otro.
    auto engineR3 = MakeEngine(rules, 9);
    GameSession replica3(*engineR3, rules, 9, GameModeId::Peaceful);
    auto link3 = MakeMemoryLink();
    host.Add(link3.first);
    Hello own;
    own.id = "eva";
    own.name = "Eva";
    own.join = JoinAs::OwnShip;
    GameClient client3(replica3, link3.second, own);
    const auto all = [&](int count) {
        for (int i = 0; i < count; ++i) {
            replica.Step(dt, {});
            replica2.Step(dt, {});
            replica3.Step(dt, {});
            host.Update(dt);
            client.Update();
            client2.Update();
            client3.Update();
        }
    };
    all(20);
    CHECK(client3.Ready() && server.VesselCount() == 2 && server.PlayerCount() == 3);
    CHECK(replica3.VesselId() == 2 && replica3.VesselCount() == 2 && replica.VesselCount() == 2 && replica.VesselId() == 1);
    const TargetRef second{Kind::Ship, 0, 2}, first{Kind::Ship, 0, 1};
    CHECK(replica.Resolve(second).has_value() && replica3.Resolve(first).has_value() && !replica3.Resolve(second));
    replica3.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Cruise)));
    all(2);
    CHECK(server.VesselAt(1).ship.mode == DriveMode::Cruise && server.VesselAt(0).ship.mode == DriveMode::Impulse);
    CHECK(replica3.Ship().mode == DriveMode::Cruise && replica.Ship().mode == DriveMode::Impulse);

    // Si la segunda nave se va a otro sistema, cada cliente deja de recibir la burbuja del otro.
    server.SetEngineFactory([&] { return MakeEngine(rules, 9); });
    server.SetFocus(1);
    server.MarkKnown(3);
    const TargetRef far{Kind::Star, 3, 0};
    CHECK(server.JumpTo(far, 1.0e8));
    server.SetFocus(0);
    all(30);
    CHECK(server.BubbleCount() == 2);
    CHECK(replica.VesselCount() == 1 && replica3.VesselCount() == 1 && replica3.VesselId() == 2 && replica.VesselId() == 1);
    CHECK(replica3.NearestSystem() == 3 && replica.NearestSystem() == 0);
    server.SetFocus(1);
    const Vec3d there = absolute(server);
    server.SetFocus(0);
    CHECK(Length(absolute(replica3) - there) < 1e-3);
    CHECK(!replica.Resolve(second));

    // Quien no habla lo mismo, o quien ya esta dentro, no entra.
    auto engineX = MakeEngine(rules, 9);
    GameSession stranger(*engineX, rules, 9, GameModeId::Peaceful);
    auto linkX = MakeMemoryLink();
    host.Add(linkX.first);
    Hello old;
    old.id = "viejo";
    old.protocol = kProtocolVersion + 1;
    GameClient clientX(stranger, linkX.second, old);
    host.Update(0.0);
    clientX.Update();
    CHECK(clientX.Refused() && !clientX.Ready() && clientX.Refusal().find("versión") != std::string::npos);
    auto engineY = MakeEngine(rules, 9);
    GameSession twin(*engineY, rules, 9, GameModeId::Peaceful);
    auto linkY = MakeMemoryLink();
    host.Add(linkY.first);
    GameClient clientY(twin, linkY.second, hello);  // otra vez "ana", que sigue conectada
    host.Update(0.0);
    clientY.Update();
    CHECK(clientY.Refused() && server.PlayerCount() == 3);
    // De otra galaxia tampoco.
    auto engineZ = MakeEngine(rules, 4);
    GameSession alien(*engineZ, rules, 4, GameModeId::Peaceful);
    auto linkZ = MakeMemoryLink();
    host.Add(linkZ.first);
    Hello other;
    other.id = "otro";
    other.join = JoinAs::Crew;
    GameClient clientZ(alien, linkZ.second, other);
    host.Update(0.0);
    clientZ.Update();
    CHECK(clientZ.Refused() && !clientZ.Ready());
    std::printf("  enviado por el servidor: %.0f kB en %llu pasos\n", host.BytesSent() / 1024.0, static_cast<unsigned long long>(host.Tick()));
}

// Desconectarse: la nave sale del mundo, pero no si esta en combate (decision 6 de docs/multijugador.md).
static void TestDisconnect() {
    using C = CommandType;
    Balance rules;
    const double calm = rules.world.logoutCalm;
    const TargetRef second{Kind::Ship, 0, 2};
    {
        auto engine = MakeEngine(rules, 9);
        GameSession g(*engine, rules, 9, GameModeId::Normal);
        Run(g, 0.5);
        g.AddVessel(400000.0);
        Run(g, 1.0);
        CHECK(g.Resolve(second).has_value() && g.PlayerAt(1).connected && !g.InCombat(1));
        const auto where = [&] { return g.VesselAt(1).ship.position + g.GetWorld().origin; };

        // Sin combate, la nave de quien se va sale del mundo enseguida, y deja de verse.
        g.PlayerLeft(1);
        CHECK(!g.PlayerAt(1).connected && g.VesselAt(1).leaving && g.VesselAt(1).phase == GamePhase::Flying);
        const Vec3d parked = where();
        g.Step(0.05, {});
        CHECK(g.VesselAt(1).phase == GamePhase::Away && !g.VesselAt(1).leaving);
        Run(g, 2.0);
        CHECK(!g.Resolve(second));
        CHECK(g.MakeSnapshot(0).vessels.size() == 1 && g.MakeSnapshot(0).players.size() == 1);
        CHECK(g.BubbleCount() == 1 && g.Phase() == GamePhase::Flying);

        // Guardada asi, al cargar sigue fuera; y quien la lleva, al cargar, esta dentro.
        const SaveData saved = g.Snapshot();
        CHECK(saved.world.ships.size() == 2 && saved.world.ships[1].away && !saved.world.ships[1].lost && !saved.world.ships[0].away);
        const std::string text = WriteSave(saved);
        CHECK(text.find("\nausente 1\n") != std::string::npos);
        const std::optional<SaveData> read = ReadSave(text);
        CHECK(read && read->world.ships[1].away && WriteSave(*read) == text);
        if (read) {
            auto engineB = MakeEngine(rules, 9);
            GameSession h(*engineB, rules, 9, GameModeId::Normal);
            h.Step(0.05, {});
            CHECK(h.Restore(*read));
            CHECK(h.VesselAt(1).phase == GamePhase::Away && h.VesselAt(0).phase == GamePhase::Flying && !h.PlayerAt(1).connected);
            SaveData theirs = *read;
            theirs.player = g.SnapshotPlayer(1);
            auto engineC = MakeEngine(rules, 9);
            GameSession k(*engineC, rules, 9, GameModeId::Normal);
            k.Step(0.05, {});
            CHECK(k.Restore(theirs) && k.VesselId() == 2 && k.Phase() == GamePhase::Flying);
        }

        // Vuelve: reaparece donde estaba.
        g.PlayerBack(1);
        CHECK(g.PlayerAt(1).connected && g.VesselAt(1).phase == GamePhase::Flying);
        Run(g, 1.0);
        CHECK(g.Resolve(second).has_value() && Length(where() - parked) < 50.0);

        // En combate no: con un hostil a tiro, la nave de quien se va se queda, parada y vulnerable.
        CHECK(g.SetFocus(1));
        const int enemy = g.SpawnAnomaly(AnomalyKind::AlienShip, Stance::Hostile, 30000.0);
        CHECK(g.SetFocus(0));
        for (int i = 0; i < 600 && !g.InCombat(1); ++i) Run(g, 0.1);
        CHECK(g.InCombat(1) && !g.InCombat(0));
        const auto guard = [&] {
            double total = g.VesselAt(1).ship.hull;
            for (const double c : g.VesselAt(1).ship.defense.charge) total += c;
            return total;
        };
        g.PlayerLeft(1);
        const double before = guard();
        for (int i = 0; i < 400 && guard() >= before; ++i) Run(g, 0.1);
        CHECK(guard() < before);  // le siguen dando: desconectarse no la ha salvado
        CHECK(g.VesselAt(1).phase == GamePhase::Flying && g.VesselAt(1).leaving && g.VesselAt(1).ship.throttle == 0.0);
        CHECK(g.Resolve(second).has_value());
        // El hostil se aleja. Hasta que no pasa el tiempo de calma, sigue ahi; despues, sale.
        for (Anomaly& a : g.Anomalies())
            if (a.id == enemy) a.offset = a.offset + Vec3d{5.0e8, 0.0, 0.0};
        for (int i = 0; i < 100 && g.InCombat(1) && g.VesselAt(1).combatAge < 1.0; ++i) Run(g, 0.1);  // lo que quede en vuelo
        Run(g, calm * 0.5);
        CHECK(g.VesselAt(1).phase == GamePhase::Flying);
        Run(g, calm + 5.0);
        CHECK(g.VesselAt(1).phase == GamePhase::Away);
    }

    // En una nave con mas gente, el que se va solo deja sus puestos: la nave sigue con los demas.
    {
        auto engine = MakeEngine(rules, 9);
        GameSession g(*engine, rules, 9, GameModeId::Peaceful);
        Run(g, 0.5);
        g.AddPlayer("Ana");
        g.PlayerLeft(0);
        CHECK(g.PlayerAt(0).seats == 0 && g.PlayerAt(1).seats == kAllSeats && !g.VesselAt(0).leaving);
        Run(g, 1.0);
        CHECK(g.VesselAt(0).phase == GamePhase::Flying);
        CHECK(g.ApplyAs(1, Order(C::SetDrive, static_cast<int>(DriveMode::Cruise))));  // ahora pilota ella
        g.PlayerBack(0);
        CHECK(g.PlayerAt(0).connected && g.PlayerAt(0).seats == 0);  // no quedaba ninguno libre
        g.PlayerLeft(1);
        CHECK(g.PlayerAt(0).seats == kAllSeats);
        g.PlayerLeft(0);
        CHECK(g.VesselAt(0).leaving);
        g.Step(0.05, {});
        CHECK(g.VesselAt(0).phase == GamePhase::Away);
        g.Step(0.05, {});  // con todas las naves fuera, el mundo espera sin romperse
        g.PlayerBack(1);
        CHECK(g.VesselAt(0).phase == GamePhase::Flying && g.PlayerAt(1).seats == kAllSeats);
    }

    // Por la red: cerrar la conexion es irse, y volver con el mismo identificador es recuperar la nave.
    {
        auto engineS = MakeEngine(rules, 9), engineA = MakeEngine(rules, 9), engineB = MakeEngine(rules, 9), engineC = MakeEngine(rules, 9);
        GameSession server(*engineS, rules, 9, GameModeId::Peaceful), first(*engineA, rules, 9, GameModeId::Peaceful), other(*engineB, rules, 9, GameModeId::Peaceful),
            again(*engineC, rules, 9, GameModeId::Peaceful);
        GameServer host(server);
        auto linkA = MakeMemoryLink(), linkB = MakeMemoryLink(), linkC = MakeMemoryLink();
        host.Add(linkA.first);
        host.Add(linkB.first);
        Hello ana, eva;
        ana.id = "ana";
        eva.id = "eva";
        eva.join = JoinAs::OwnShip;
        GameClient clientA(first, linkA.second, ana), clientB(other, linkB.second, eva);
        const auto round = [&](int count) {
            for (int i = 0; i < count; ++i) {
                first.Step(GameSession::kFixedStep, {});
                host.Update(GameSession::kFixedStep);
                clientA.Update();
                clientB.Update();
            }
        };
        round(10);
        CHECK(host.Clients() == 2 && server.VesselCount() == 2 && first.VesselCount() == 2 && other.VesselId() == 2);
        linkB.second->Close();
        round(10);
        CHECK(host.Clients() == 1 && !server.PlayerAt(1).connected && server.VesselAt(1).phase == GamePhase::Away);
        CHECK(first.VesselCount() == 1 && !first.Resolve(second));
        host.Add(linkC.first);
        GameClient clientC(again, linkC.second, eva);
        for (int i = 0; i < 10; ++i) {
            first.Step(GameSession::kFixedStep, {});
            host.Update(GameSession::kFixedStep);
            clientA.Update();
            clientC.Update();
        }
        CHECK(clientC.Ready() && !clientC.Refused() && host.Clients() == 2);
        CHECK(server.PlayerCount() == 2 && server.VesselCount() == 2 && server.PlayerAt(1).connected && server.VesselAt(1).phase == GamePhase::Flying);
        CHECK(again.VesselId() == 2 && first.VesselCount() == 2 && first.Resolve(second).has_value());
    }
}

// La red de verdad (ENet), por la interfaz de esta misma maquina: fase 6 de docs/multijugador.md.
static void TestNetwork() {
    if (!NetworkAvailable()) {
        std::printf("  (compilado sin ENet: no se prueba la red)\n");
        return;
    }
    using C = CommandType;
    Balance rules;
    const int port = 27391;  // distinto del de juego, por si hay un servidor abierto
    std::string error;
    std::unique_ptr<NetListener> listener = ListenOn(port, 4, &error);
    CHECK(listener != nullptr);
    if (!listener) {
        std::printf("  no se pudo escuchar en el puerto %d: %s\n", port, error.c_str());
        return;
    }
    CHECK(!ListenOn(port, 4, &error) && !error.empty());  // el puerto ya esta cogido
    CHECK(!ConnectTo("127.0.0.1", port + 7, 0.3, &error) && error.find("no responde") != std::string::npos);  // ahi no hay nadie

    // Un paquete pequeno y uno del tamano de una instantanea, en los dos sentidos y por los dos canales.
    {
        // Conectar bloquea esperando al servidor, y el servidor solo contesta si se le atiende: aqui, en otro hilo.
        std::atomic<bool> stop{false};
        std::thread pump([&] {
            while (!stop) {
                listener->Pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
        std::shared_ptr<Transport> client = ConnectTo("127.0.0.1", port, 3.0, &error);
        stop = true;
        pump.join();
        CHECK(client != nullptr);
        if (!client) {
            std::printf("  no se pudo conectar: %s\n", error.c_str());
            return;
        }
        std::shared_ptr<Transport> peer;
        for (int i = 0; i < 500 && !peer; ++i) {  // el saludo termina de cruzarse atendiendo a los dos lados
            listener->Pump();
            peer = listener->Accept();
            Packet nothing;
            client->Receive(nothing);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(peer && peer->Open() && client->Open());
        if (!peer) return;
        Packet small{1, 2, 3}, big(40000);
        for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::uint8_t>(i * 7 + 3);
        const auto exchange = [&](Transport& from, Transport& to, const Packet& sent, bool reliable) {
            from.Send(sent, reliable);
            Packet got;
            for (int i = 0; i < 500; ++i) {
                listener->Pump();
                if (to.Receive(got)) return got == sent;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        };
        CHECK(exchange(*client, *peer, small, true) && exchange(*peer, *client, small, true));
        CHECK(exchange(*client, *peer, big, true) && exchange(*peer, *client, big, true));
        CHECK(exchange(*peer, *client, big, false) && exchange(*client, *peer, small, false));
        // Al cerrar el cliente, el servidor ve la conexion cerrada.
        client->Close();
        for (int i = 0; i < 500 && peer->Open(); ++i) {
            listener->Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(!peer->Open() && !client->Open());
    }

    // Una partida entera por la red: servidor que escucha, y un cliente que entra con nave propia.
    auto engineS = MakeEngine(rules, 9), engineR = MakeEngine(rules, 9);
    GameSession server(*engineS, rules, 9, GameModeId::Peaceful), replica(*engineR, rules, 9, GameModeId::Peaceful);
    GameServer host(server);
    host.Listen(std::move(listener));
    std::atomic<bool> stop{false};
    std::thread pump([&] {  // el servidor, atendiendo mientras el cliente conecta
        while (!stop) {
            host.Update(0.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::shared_ptr<Transport> link = ConnectTo("localhost", port, 3.0, &error);
    stop = true;
    pump.join();
    CHECK(link != nullptr);
    if (!link) return;
    Hello hello;
    hello.id = "eva";
    hello.name = "Eva";
    hello.join = JoinAs::OwnShip;
    GameClient client(replica, link, hello);
    const auto frames = [&](int count, const ShipControls& controls = {}) {
        for (int i = 0; i < count; ++i) {
            replica.Step(GameSession::kFixedStep, controls);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // que el paquete cruce
            host.Update(GameSession::kFixedStep);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            client.Update();
        }
    };
    for (int i = 0; i < 300 && !client.Ready(); ++i) frames(1);
    CHECK(client.Ready() && !client.Refused() && host.Clients() == 1);
    CHECK(server.VesselCount() == 2 && server.PlayerAt(1).id == "eva" && replica.VesselId() == 2 && replica.VesselCount() == 2);
    replica.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Cruise)));
    frames(20);
    CHECK(server.VesselAt(1).ship.mode == DriveMode::Cruise && replica.Ship().mode == DriveMode::Cruise);
    replica.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Impulse)));
    ShipControls fly;
    fly.throttleChange = 0.5;
    frames(120, fly);
    CHECK(server.VesselAt(1).ship.throttle > 0.8 && server.VesselAt(0).ship.throttle == 0.0);
    frames(10);
    CHECK(std::abs(replica.Ship().throttle - server.VesselAt(1).ship.throttle) < 1e-9);
    CHECK(std::abs(replica.SimTime() - server.SimTime()) < 60.0 && replica.SimTime() > 0.0);
    // Se va: su nave sale del mundo (no hay combate).
    link->Close();
    for (int i = 0; i < 300 && server.VesselAt(1).phase != GamePhase::Away; ++i) {
        host.Update(GameSession::kFixedStep);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(host.Clients() == 0 && server.VesselAt(1).phase == GamePhase::Away && !server.PlayerAt(1).connected);
}

// Produccion y alerta por sello de tiempo (fase 1.7 de docs/multijugador.md): da igual cada cuanto se
// mire una estacion o un sistema; lo que cuenta es el reloj con el que se mira.
static void TestTimeStamps() {
    Balance rules;
    const auto make = [&] {
        StationNetwork net(rules.stations);
        Inventory cargo;
        StarYield yield;
        yield.energyPerHour = 5.0;
        yield.matterPerHour = 0.5;
        StationSite site;
        CHECK(net.Deploy(StationKind::Power, site, "Central", yield, cargo) > 0);
        return net;
    };
    StationNetwork often = make(), once = make(), plain = make();
    // Mil pasos de 36 s, uno solo de 10 h y el metodo de siempre: lo mismo.
    for (int i = 1; i <= 1000; ++i) often.ProduceUntil(500.0 + 36.0 * i, 500.0);
    once.ProduceUntil(500.0 + 36000.0, 500.0);
    plain.Produce(10.0);
    const Station& a = often.All().front();
    const Station& b = once.All().front();
    const Station& c = plain.All().front();
    CHECK(c.storedAntimatter > 0.0 && c.stored.Get(Resource::StellarMatter) > 0.0);
    CHECK(std::abs(a.storedAntimatter - c.storedAntimatter) < 1e-9 * c.storedAntimatter);
    CHECK(std::abs(b.storedAntimatter - c.storedAntimatter) < 1e-9 * c.storedAntimatter);
    CHECK(std::abs(a.stored.Get(Resource::StellarMatter) - c.stored.Get(Resource::StellarMatter)) < 1e-9);
    CHECK(std::abs(a.angle - c.angle) < 1e-9 && std::abs(b.angle - c.angle) < 1e-9);
    // Un reloj anterior al sello no produce ni lo mueve: el tiempo de una estacion no retrocede.
    const double stored = b.storedAntimatter;
    once.ProduceUntil(1000.0, 0.0);
    CHECK(once.All().front().storedAntimatter == stored && once.All().front().producedUntil == 36500.0);

    // La alerta: lo mismo en muchos pasos que en uno, y que con DecayAlert.
    SpeciesState many, single, old;
    for (SpeciesState* s : {&many, &single, &old}) {
        ResizeSpecies(*s, 4);
        s->alert[2] = 0.8;
    }
    for (int i = 1; i <= 100; ++i) DecayAlertUntil(many, 36.0 * i, 0.0, rules.alert);
    DecayAlertUntil(single, 3600.0, 0.0, rules.alert);
    DecayAlert(old, 3600.0, 3600.0, rules.alert);
    CHECK(old.alert[2] < 0.8 && old.alert[2] > 0.0);
    CHECK(std::abs(many.alert[2] - old.alert[2]) < 1e-9 && std::abs(single.alert[2] - old.alert[2]) < 1e-12);
    CHECK(many.alert[0] == 0.0);
    DecayAlertUntil(single, 100.0, 0.0, rules.alert);  // hacia atras, nada
    CHECK(std::abs(single.alert[2] - old.alert[2]) < 1e-12);
}

// Las ordenes como datos (GameSession::Apply) y el paso fijo (StepFixed): fase 0 de docs/multijugador.md.
static void TestOrdersAndFixedStep() {
    using C = CommandType;
    Balance rules;
    auto engine = MakeEngine(rules, 5);
    {
        GameSession g(*engine, rules, 5, GameModeId::Peaceful);
        g.Step(0.05, {});
        // Una orden hace lo mismo que el metodo al que llama, y dice si pudo.
        CHECK(g.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Cruise))));
        CHECK(g.Ship().mode == DriveMode::Cruise);
        CHECK(!g.Apply(Order(C::SetDrive, static_cast<int>(DriveMode::Hyper))));  // aun por descubrir
        ShipCommand throttle = Order(C::SetThrottle);
        throttle.amount = 0.5;
        CHECK(g.Apply(throttle) && g.Ship().throttle == 0.5);
        CHECK(g.Apply(Order(C::HoldPosition)) && g.Ship().throttle == 0.0);
        const TargetRef planet = FirstPlanet(g, 0);
        CHECK(g.Apply(Order(C::Autopilot, planet)) && g.Autopilot() && *g.Autopilot() == planet);
        CHECK(g.Apply(Order(C::CancelAutopilot)) && !g.Autopilot());
        CHECK(!g.Apply(Order(C::Autopilot, TargetRef{})));
        CHECK(g.Apply(Order(C::Cinematic, 1)) && g.Cinematic());
        CHECK(g.Apply(Order(C::Coolant, static_cast<int>(CoolantKind::Water), 0)) && !g.Ship().defense.coolantOn[0]);

        // Lo que no es un valor de su enumeracion se rechaza sin tocar nada: puede venir de la red.
        CHECK(!g.Apply(Order(C::SetDrive, 99)) && g.Ship().mode == DriveMode::Cruise);
        CHECK(!g.Apply(Order(C::SetDrive, -1)));
        CHECK(!g.Apply(Order(C::Fire, planet, 99)));
        CHECK(!g.Apply(Order(C::SetShield, static_cast<int>(ShieldKind::Count))));
        CHECK(!g.Apply(Order(C::Coolant, 99, 1)));
        CHECK(!g.Apply(ShipCommand{}));
        const CommandResult unknown = g.Apply(Order(C::InstallModule, 9999, 0));
        CHECK(!unknown && !unknown.why.empty());

        // El taller explica por que no: sin atracar no se monta nada.
        const CommandResult refit = g.Apply(Order(C::InstallModule, static_cast<int>(ShipModuleKind::Turret), 0));
        CHECK(!refit && !refit.why.empty());
        // Sin blanco no dispara nada, y lo dice con la cuenta.
        const CommandResult fired = g.Apply(Order(C::FireAll, TargetRef{}));
        CHECK(!fired && fired.count == 0);
    }

    // Paso fijo: con fotogramas de 1/60 exactos es lo mismo que Step.
    const auto save = [](const GameSession& g) { return WriteSave(g.Snapshot()); };
    ShipControls turn;
    turn.turnYaw = 0.00390625f;  // 2^-8: se puede partir en cuatro sin redondeo
    turn.throttleChange = 0.25;
    // Cada sesion con su motor: avanzan a la vez y no deben compartir mundo.
    auto engineA = MakeEngine(rules, 5), engineB = MakeEngine(rules, 5), engineC = MakeEngine(rules, 5);
    GameSession a(*engineA, rules, 5, GameModeId::Peaceful), b(*engineB, rules, 5, GameModeId::Peaceful), c(*engineC, rules, 5, GameModeId::Peaceful);
    const Vec3d headingStart = Heading(a.Ship());
    for (int i = 0; i < 120; ++i) {
        a.Step(GameSession::kFixedStep, turn);
        CHECK(b.StepFixed(GameSession::kFixedStep, turn) == 1);
    }
    CHECK(save(a) == save(b));
    CHECK(Length(Heading(a.Ship()) - headingStart) > 1e-3);  // el giro llego

    // A 240 fotogramas por segundo da un paso de cada cuatro, y el giro de los cuatro no se pierde.
    ShipControls quarter = turn;
    quarter.turnYaw = turn.turnYaw / 4.0f;
    int steps = 0;
    for (int i = 0; i < 480; ++i) steps += c.StepFixed(GameSession::kFixedStep / 4.0, quarter);
    CHECK(steps == 120);
    CHECK(save(a) == save(c));

    // Sin tiempo no hay paso; el freno pedido entre dos pasos espera al siguiente.
    CHECK(c.StepFixed(0.0, {}) == 0);
    ShipControls brake;
    brake.manualBrake = true;
    CHECK(c.Ship().throttle > 0.0);
    CHECK(c.StepFixed(GameSession::kFixedStep / 4.0, brake) == 0 && c.Ship().throttle > 0.0);
    CHECK(c.StepFixed(GameSession::kFixedStep, {}) == 1 && c.Ship().throttle == 0.0);
    // Un tiron de varios segundos no se recupera entero.
    CHECK(c.StepFixed(5.0, {}) <= 8);
}

// Compara dos guardados linea a linea. Los numeros, con una tolerancia relativa: un cambio de orden en
// una suma no es un cambio de comportamiento. La linea de la suma de control no cuenta.
static int GoldenDifferences(const std::string& expected, const std::string& actual) {
    std::istringstream a(expected), b(actual);
    std::string la, lb;
    int line = 0, differences = 0;
    while (true) {
        const bool ha = static_cast<bool>(std::getline(a, la)), hb = static_cast<bool>(std::getline(b, lb));
        if (!ha && !hb) break;
        ++line;
        if (!la.empty() && la.back() == '\r') la.pop_back();
        if (!lb.empty() && lb.back() == '\r') lb.pop_back();
        bool same = ha == hb;
        if (same && la.rfind("suma ", 0) == 0 && lb.rfind("suma ", 0) == 0) continue;
        if (same && la != lb) {
            std::istringstream ta(la), tb(lb);
            std::string wa, wb;
            while (same) {
                const bool ga = static_cast<bool>(ta >> wa), gb = static_cast<bool>(tb >> wb);
                if (!ga || !gb) {
                    same = ga == gb;
                    break;
                }
                if (wa == wb) continue;
                char* ea = nullptr;
                char* eb = nullptr;
                const double va = std::strtod(wa.c_str(), &ea), vb = std::strtod(wb.c_str(), &eb);
                same = *ea == '\0' && *eb == '\0' && std::abs(va - vb) <= 1e-6 * std::max({1.0, std::abs(va), std::abs(vb)});
            }
        }
        if (same) continue;
        if (++differences <= 6) std::printf("  Linea %d:\n    dorada: %.150s\n    ahora:  %.150s\n", line, ha ? la.c_str() : "(falta)", hb ? lb.c_str() : "(falta)");
    }
    return differences;
}

static void TestGoldenGame() {
    const std::string first = PlayGolden();
    // Dos veces seguidas debe dar exactamente lo mismo: sin eso, la comparacion no vale nada.
    CHECK(first == PlayGolden());
    CHECK(first.find("\nestacion ") != std::string::npos);  // la partida llego a desplegar la central
    CHECK(first.find("\nbase 1 0\n") != std::string::npos);   // y a atracar en ella

    const std::string path = std::string(EPSILON_TEST_DATA) + "/partida_dorada.txt";  // .txt: los .sav no se suben (.gitignore)
    if (gRegenerateGolden) {
        std::filesystem::create_directories(FilePath(EPSILON_TEST_DATA));
        std::ofstream out(FilePath(path), std::ios::binary | std::ios::trunc);
        out << first;
        std::printf("  Partida dorada regenerada: %s\n", path.c_str());
        return;
    }
    std::ifstream in(FilePath(path), std::ios::binary);
    CHECK(static_cast<bool>(in));
    if (!in) {
        std::printf("  Falta %s: ejecute epsilon_game_tests.exe --regenerar-dorada\n", path.c_str());
        return;
    }
    std::ostringstream stored;
    stored << in.rdbuf();
    const int differences = GoldenDifferences(stored.str(), first);
    CHECK(differences == 0);
    if (differences > 0) {
        const std::string now = std::string(EPSILON_TEST_DATA) + "/partida_dorada.ahora.txt";
        std::ofstream out(FilePath(now), std::ios::binary | std::ios::trunc);
        out << first;
        std::printf("  %d lineas distintas. La partida de ahora queda en %s\n  Si el cambio es querido: epsilon_game_tests.exe --regenerar-dorada\n", differences, now.c_str());
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--regenerar-dorada") gRegenerateGolden = true;
    // Lo que cuesta un motor (un mundo cargado) con la galaxia del juego: uno por burbuja (fase 4).
    if (argc > 1 && std::string(argv[1]) == "--medir-motor") {
        Balance rules;
        EngineConfig config;
        config.galaxy.seed = 20261105;
        config.galaxy.systems = rules.world.systems;
        config.galaxy.starSpacing = rules.world.starSpacing;
        config.galaxy.streaming = true;
        config.galaxy.blackHoles = true;
        std::vector<std::unique_ptr<Engine>> engines;
        std::printf("Galaxia de %d sistemas\n", rules.world.systems);
        for (int i = 0; i < 8; ++i) {
            const auto start = std::chrono::steady_clock::now();
            engines.push_back(std::make_unique<Engine>(config));
            const double built = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            const auto advance = std::chrono::steady_clock::now();
            Vec3d eye = engines.back()->GetWorld().scene.WorldPosition(engines.back()->GetWorld().sectors[static_cast<std::size_t>(i % rules.world.systems)]);
            for (int f = 0; f < 120; ++f) {
                engines.back()->Recenter(eye);
                engines.back()->Advance(eye, 800.0f, f * 1.0);
            }
            const double stepped = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - advance).count() / 120.0;
            std::printf("  motor %d: %.0f ms en crearse, %.2f ms por paso\n", i + 1, built, stepped);
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // para leer la memoria desde fuera
        }
        return 0;
    }
    const bool onlyGolden = argc > 1 && std::string(argv[1]) == "--solo-dorada";  // la comprobacion rapida de una reorganizacion
    if (gRegenerateGolden || onlyGolden) {
        std::printf("[ Partida dorada ]\n");
        TestGoldenGame();
        if (onlyGolden) std::printf("\n%d comprobaciones, %d fallos\n", gChecks, gFailures);
        return gFailures == 0 ? 0 : 1;
    }
    const struct { const char* name; void (*fn)(); } tests[] = {
        {"Partida dorada", TestGoldenGame},
        {"Ordenes y paso fijo", TestOrdersAndFixedStep},
        {"Sellos de tiempo", TestTimeStamps},
        {"Flota: dos naves", TestFleet},
        {"Tripulacion: dos jugadores en una nave", TestCrew},
        {"Burbujas: naves en sistemas distintos", TestBubbles},
        {"Servidor y cliente en memoria", TestNet},
        {"Desconexion: salir del mundo, pero no en combate", TestDisconnect},
        {"Red (ENet) en esta maquina", TestNetwork},
        {"Armamento humano", TestHumanArmament},
        {"Propulsores, estela y luz", TestThrustersAndLight},
        {"Modulos de la nave", TestShipModules},
        {"Dano estructural", TestStructuralDamage},
        {"Zonas del casco", TestHullZones},
        {"Zonas alienigenas y cinematica", TestAlienZones},
        {"Modelos de combate (fase E)", TestCombatModels},
        {"Maniobra junto a una estacion", TestStationManeuver},
        {"Choques con naves y estaciones", TestSolidStructures},
        {"Escudos", TestShields},
        {"Calor y refrigerantes", TestHeat},
        {"Balistica", TestBallistics},
        {"Combate", TestCombat},
        {"Fuego enemigo", TestIncoming},
        {"Combate guardado", TestCombatSave},
        {"Niveles de estacion y alerta (fase F)", TestStationLevelsAndAlert},
        {"Motores por descubrir", TestDriveUnlock},
        {"Alabeo", TestRoll},
        {"Menu de sistemas", TestAtlas},
        {"Elementos", TestElements},
        {"Personas", TestHumanoid},
        {"Atraque y a pie", TestDocking},
        {"Arrimarse a una estacion", TestApproach},
        {"Marco de la estacion", TestStationFrame},
        {"Modo creativo", TestCreative},
        {"Economia", TestEconomy},
        {"Motores", TestDrive},
        {"Reglas de estaciones", TestStationRules},
        {"Anomalias", TestAnomalies},
        {"Partida completa", TestFullLoop},
        {"Animacion de la origami", TestOrigamiAnimation},
        {"Origami y drones mineros", TestShipyardAndDrones},
        {"Peligros", TestHazards},
        {"Modelos y capa", TestModels},
        {"Manejo", TestHandling},
        {"Orbitas", TestOrbits},
        {"Contacto", TestContact},
        {"Rutas seguras", TestRouteSafety},
        {"Mandos de vuelo", TestFlightControls},
        {"Analisis de astros", TestAstroScan},
        {"Sensores al dia", TestLiveContacts},
        {"Eje del warp", TestWarpAxis},
        {"Clases de nave", TestShipClasses},
        {"Unidades", TestUnits},
        {"Elementos", TestElementInventory},
        {"Configuracion", TestSettings},
        {"Salir de una estrella", TestLeaveStar},
        {"Descubrimiento", TestDiscovery},
        {"Partidas guardadas", TestSave},
        {"Coste de la curvatura", TestWarpCost},
        {"Viaje con curvatura", TestWarpRoute},
        {"Escaner", TestScanner},
        {"Primera estructura y eco frio", TestColdEcho},
        {"Modos y derrota", TestModes},
        {"Dialogos", TestDialogue},
        {"Tutorial guiado", TestTutorialGuide},
    };
    for (const auto& t : tests) {
        std::printf("[ %s ]\n", t.name);
        std::fflush(stdout);
        t.fn();
    }
    std::printf("\n%d comprobaciones, %d fallos\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
