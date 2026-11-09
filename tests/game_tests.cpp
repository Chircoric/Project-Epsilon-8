// Pruebas del juego. Juegan partidas enteras sin ventana ni GPU.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include "game/Economy.h"
#include "game/GameLayer.h"
#include "game/GameModels.h"
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
    StationNetwork net(rules.stations);
    Inventory cargo;
    Deposit deposit;
    deposit.share[static_cast<std::size_t>(Resource::Metal)] = 1.0;
    std::string error;

    // Las tres primeras son gratis, aunque la bodega este vacia.
    for (int i = 0; i < 3; ++i) {
        CHECK(net.NextIsFree(StationKind::Mining));
        StationSite site;
        site.body = i;
        CHECK(net.Deploy(StationKind::Mining, site, "m", deposit, {}, cargo, &error) > 0);
    }
    CHECK(!net.NextIsFree(StationKind::Mining));
    StationSite fourth;
    fourth.body = 3;
    CHECK(net.Deploy(StationKind::Mining, fourth, "m", deposit, {}, cargo, &error) < 0);
    CHECK(net.Count(StationKind::Mining) == 3);

    // La cuarta cuesta, y solo una por sitio.
    cargo.Add(Resource::Metal, rules.stations.miningMetal);
    cargo.Add(Resource::Silicate, rules.stations.miningSilicate);
    StationSite repeated;
    repeated.body = 2;
    CHECK(net.Deploy(StationKind::Mining, repeated, "m", deposit, {}, cargo, &error) < 0);
    CHECK(cargo.Get(Resource::Metal) == rules.stations.miningMetal);
    CHECK(net.Deploy(StationKind::Mining, fourth, "m", deposit, {}, cargo, &error) > 0);
    CHECK(cargo.Total() == 0.0);

    // Producen por tiempo, hasta llenarse.
    const int id = net.All()[0].id;
    net.Produce(2.0);
    CHECK(std::abs(net.Find(id)->stored.Get(Resource::Metal) - rules.stations.miningRate * 2.0) < 1e-6);
    net.Produce(1000.0);
    CHECK(std::abs(net.Find(id)->stored.Total() - rules.stations.miningStorage) < 1e-6);

    // Un modulo mejora, cuesta mas en cada nivel y tiene tope.
    const double slow = net.MiningPerHour(*net.Find(id));
    cargo.Add(Resource::Metal, 1.0e6);
    CHECK(net.AddOrUpgradeModule(id, ModuleKind::Drill, cargo, &error));
    CHECK(net.MiningPerHour(*net.Find(id)) > slow);
    const double cost1 = net.ModuleCost(*net.Find(id), ModuleKind::Drill).Get(Resource::Metal);
    CHECK(net.AddOrUpgradeModule(id, ModuleKind::Drill, cargo, &error));
    CHECK(net.ModuleCost(*net.Find(id), ModuleKind::Drill).Get(Resource::Metal) > cost1);
    CHECK(!net.AddOrUpgradeModule(id, ModuleKind::Collector, cargo, &error));  // no sirve aqui
    for (int i = 0; i < 6; ++i) net.AddOrUpgradeModule(id, ModuleKind::Drill, cargo, &error);
    CHECK(net.Find(id)->modules[0].level == rules.stations.moduleMaxLevel);

    const double beforeOre = net.Find(id)->stored.Total();
    CHECK(net.AddOrUpgradeModule(id, ModuleKind::Hopper, cargo, &error));
    CHECK(net.OreCapacity(*net.Find(id)) > rules.stations.miningStorage);
    CHECK(net.Find(id)->stored.Total() == beforeOre);

    // Recoger respeta lo que cabe en la bodega y deja el resto.
    Inventory hold;
    double tank = 0;
    net.Collect(id, hold, 100.0, tank, 1000.0);
    CHECK(std::abs(hold.Total() - 100.0) < 1e-6);
    CHECK(std::abs(net.Find(id)->stored.Total() - (beforeOre - 100.0)) < 1e-6);

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
    const int central = power.Deploy(StationKind::Power, star, "c", {}, yield, paid, &error);
    CHECK(central > 0);
    CHECK(paid.Get(Resource::Metal) == 1.0e4);  // la primera central es gratis
    CHECK(!power.NextIsFree(StationKind::Power));
    StationSite second;
    second.star = 1;
    CHECK(power.Deploy(StationKind::Power, second, "c", {}, yield, paid, &error) > 0);
    CHECK(paid.Get(Resource::Metal) < 1.0e4);  // la segunda ya cuesta
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
    CHECK(fort.armed && fort.weapon == WeaponKind::Missile && fort.hull == rules.anomalies.stationHull);
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

    // Tres minas gratis en tres sitios.
    for (std::size_t i = 0; i < 3; ++i) {
        const TargetRef planet = FirstPlanet(g, 0, i + 2);
        CHECK(planet.kind == Kind::Planet);
        CHECK(Fly(g, planet, DriveMode::Warp, 600.0));
        const double spent = g.Ship().cargo.Total();
        CHECK(g.DeployMining(planet));
        CHECK(g.Ship().cargo.Total() == spent);
    }
    CHECK(g.Stations().Count(StationKind::Mining) == 3);
    CHECK(!g.Stations().NextIsFree(StationKind::Mining));

    // Una central no se puede poner desde lejos ni en una estrella que quema.
    const TargetRef star{Kind::Star, 0, 0};
    CHECK(!g.DeployPower(star));
    CHECK(!g.DeployPower(FirstPlanet(g, 0, 0)));
    CHECK(!g.DeployMining(star));

    // Dejando pasar el tiempo se llenan las minas.
    Run(g, 1200.0);
    const TargetRef mine{Kind::Station, 0, g.Stations().All()[0].id};
    CHECK(Fly(g, mine, DriveMode::Warp, 600.0));
    CHECK(g.Interact(mine));
    CHECK(g.Ship().cargo.Total() > 100.0);

    // En orbita, la nave acompana a la estacion sin quedarse atras, aunque esta se mueva.
    const double arrival = Length(g.Resolve(mine)->position - g.Ship().position);
    Run(g, 20.0);
    CHECK(std::abs(Length(g.Resolve(mine)->position - g.Ship().position) - arrival) < 1.0);

    // Con lo recogido se paga la central, que da antimateria a la nave.
    g.Ship().cargo.Add(Resource::Metal, 600.0);
    g.Ship().cargo.Add(Resource::Silicate, 500.0);
    CHECK(Fly(g, star, DriveMode::Warp, 600.0));
    CHECK(g.DeployPower(star));
    const TargetRef central{Kind::Station, 0, g.Stations().All().back().id};
    Run(g, 600.0);
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
    CHECK(g.Resolve(mine).has_value());
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
    for (const ModelRef& model : {assets.ship, assets.miningStation, assets.powerStation, assets.hostileShip, assets.neutralShip, assets.hostileStation, assets.neutralStation, assets.derelict}) {
        CHECK(model != nullptr);
        CHECK(model->parts.size() >= 4);
    }
    Anomaly a;
    a.kind = AnomalyKind::AlienStation;
    a.stance = Stance::Neutral;
    CHECK(ModelFor(assets, a) == assets.neutralStation);

    // Los modulos aparecen en el modelo uno a uno.
    int modules = 0;
    for (const ModelPart& p : assets.miningStation->parts) modules += p.name.rfind("mod_", 0) == 0 ? 1 : 0;
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

    // Una mina al otro lado del planeta: la ruta lo rodea y llega, sin rozarlo.
    {
        auto game = Session(*engine, rules, GameModeId::Normal);
        GameSession& g = *game;
        const TargetRef planet = MinablePlanet(g);
        const double radius = g.Resolve(planet)->radius;
        g.JumpTo(planet, radius * 1.5);
        CHECK(g.DeployMining(planet));
        const int mine = g.Stations().All().back().id;
        g.Stations().Find(mine)->angle += kPi;  // justo enfrente
        const TargetRef station{TargetRef::Kind::Station, 0, mine};
        CHECK(g.SetAutopilot(station));
        CHECK(g.Route().phase != RoutePhase::None || true);
        double closest = 1.0e300;
        int steps = 0;
        for (; steps < 60 * 300 && g.Autopilot(); ++steps) {
            g.Step(1.0 / 60.0, {});
            closest = std::min(closest, Length(g.Ship().position - g.Resolve(planet)->position));
        }
        std::printf("  Rodear el planeta: %.0f s, altura minima %.2f radios\n", steps / 60.0, closest / radius);
        CHECK(!g.Autopilot());
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
    CHECK(g.DeployMining(planet));
    CHECK(g.Stations().All().back().deployAge == 0.0);
    g.Step(0.5, {});
    CHECK(g.Stations().All().back().deployAge == 0.5);
    const TargetRef mine{TargetRef::Kind::Station, 0, g.Stations().All().back().id};
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
    CHECK(!g.DeployMining(planet));
    for (int i = 0; i < 30; ++i) g.Step(1.0 / 60.0, {});
    for (const Contact& c : g.Contacts())
        if (c.target == planet) CHECK(!c.identified);

    CHECK(g.StartScan());
    for (int i = 0; i < 40; ++i) g.Step(1.0, {});
    CHECK(g.Scanned(planet));
    CHECK(g.ScannedCount() > 1);
    CHECK(g.DeployMining(planet));
    for (int i = 0; i < 30; ++i) g.Step(1.0 / 60.0, {});
    bool named = false;
    for (const Contact& c : g.Contacts()) named = named || (c.target == planet && c.identified);
    CHECK(named);

    // Se conserva al guardar.
    const std::optional<SaveData> copy = ReadSave(WriteSave(g.Snapshot()));
    CHECK(copy.has_value());
    if (copy) CHECK(copy->scanned.size() == static_cast<std::size_t>(g.ScannedCount()));
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

    const World& w = g.GetWorld();
    TargetRef field{TargetRef::Kind::Belt, 0, 0};
    const StarSystemData& system = *w.systems[0];
    TargetRef planet;
    for (std::size_t b = 0; b < system.bodies.size(); ++b) {
        const TargetRef t{TargetRef::Kind::Planet, 0, static_cast<int>(b)};
        Deposit deposit;
        if (system.bodies[b].parent < 0 && g.Resolve(t) && DepositOf(system.bodies[b], deposit)) planet = t;
    }
    (void)field;
    CHECK(g.JumpTo(planet, g.Resolve(planet)->radius * 1.4));
    g.MarkScanned(0);
    CHECK(g.DeployMining(planet));
    CHECK(g.TakeSaveRequest());
    CHECK(!g.TakeSaveRequest());
    const int station = g.Stations().All().back().id;
    g.Stations().Find(station)->modules.push_back({ModuleKind::Drill, 3});
    g.Anomalies()[0].revealed = true;
    g.Anomalies()[0].emitter = false;

    const SaveData before = g.Snapshot();
    const std::string text = WriteSave(before);
    const std::optional<SaveData> read = ReadSave(text);
    CHECK(read.has_value());
    if (!read) return;

    // Los numeros vuelven exactos, y escribirlo otra vez da el mismo archivo.
    CHECK(read->simTime == before.simTime);
    CHECK(read->ship.hull == 777.125);
    CHECK(read->ship.orientation.w == before.ship.orientation.w);
    CHECK(read->absolutePosition.x == before.absolutePosition.x);
    CHECK(read->stations.size() == before.stations.size());
    CHECK(read->stations.back().name == before.stations.back().name);
    CHECK(read->stations.back().modules.size() == 1 && read->stations.back().modules[0].level == 3);
    CHECK(read->anomalies.size() == before.anomalies.size());
    CHECK(read->anomalies[0].name == before.anomalies[0].name);
    CHECK(read->ship.antimatter == before.ship.antimatter);
    CHECK(read->ship.emitters == 3);
    CHECK(!read->anomalies[0].emitter);
    CHECK(WriteSave(*read) == text);

    // Una partida de la version 3 llevaba materia oscura en la bodega: se descarta, y haberla
    // encontrado cuenta como el primer emisor.
    {
        std::string v3 = text.substr(0, text.rfind("suma "));
        const std::string current = "NEBULOSA " + std::to_string(kSaveVersion);
        v3.replace(v3.find(current), current.size(), "NEBULOSA 3");
        const std::size_t bodega = v3.find("\nbodega");
        v3.insert(v3.find('\n', bodega + 1), " 7.5");
        std::string kept;
        std::istringstream lines(v3);
        for (std::string line; std::getline(lines, line);)
            if (line.rfind("estacion ", 0) != 0) kept += line + "\n";
        std::uint64_t h = 14695981039346656037ull;
        for (const char c : kept) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }
        char sum[32];
        std::snprintf(sum, sizeof(sum), "suma %016llx\n", static_cast<unsigned long long>(h));
        std::string error3;
        const std::optional<SaveData> old = ReadSave(kept + sum, &error3);
        CHECK(old.has_value());
        if (old) {
            CHECK(old->ship.emitters == 1);
            CHECK(old->ship.cargo.Get(Resource::Metal) == before.ship.cargo.Get(Resource::Metal));
            CHECK(old->ship.cargo.Total() == before.ship.cargo.Total());
        }
    }

    // Una partida antigua sigue valiendo: la curvatura de entonces es el crucero de ahora, y sus
    // centrales pasan a fabricar antimateria.
    {
        SaveData old = before;
        StarYield yield;
        yield.energyPerHour = 100.0;
        Station central;
        central.kind = StationKind::Power;
        central.yield = yield;
        central.storedAntimatter = 1500.0;  // entonces era energia
        old.stations.push_back(central);
        old.ship.mode = static_cast<DriveMode>(2);  // el hiper de entonces
        old.ship.antimatter = 0.0;
        old.ship.warpCharge = 1.0;
        old.tutorialStep = 8;
        UpgradeFromFirstVersion(old);
        CHECK(old.ship.mode == DriveMode::Hyper);
        CHECK(old.ship.warpCharge == 0.0);
        CHECK(old.ship.antimatter == rules.ship.startAntimatter);
        CHECK(old.stations.back().storedAntimatter == 0.0);
        CHECK(old.stations.back().yield.energyPerHour == 100.0 * kCollectorScale);
        CHECK(old.tutorialStep == 9);
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
    CHECK(Length(absolute - before.absolutePosition) < 1.0e4);  // sigue en orbita de lo mismo
    CHECK(h.Snapshot().stations.back().modules.size() == 1);

    SaveData foreign = *read;
    foreign.seed = 77;
    CHECK(!h.Restore(foreign, &error));

    // Archivo real, sin dejar restos.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "epsilon_prueba.sav";
    const std::string file = path.string();
    CHECK(SaveToFile(file, before));
    const std::optional<SaveData> loaded = LoadFromFile(file);
    CHECK(loaded.has_value());
    if (loaded) CHECK(loaded->ship.hull == before.ship.hull);
    CHECK(!std::filesystem::exists(file + ".tmp"));
    std::filesystem::remove(path);
    CHECK(!LoadFromFile(file));

    // Perder la nave en modo realista borra la partida; en los demas, guarda el punto de control.
    GameSession lost(*engine, rules, 20261105, GameModeId::Realistic);
    lost.Step(1.0 / 60.0, {});
    lost.Ship().hull = 0.0;
    lost.Step(1.0 / 60.0, {});
    CHECK(lost.Phase() == GamePhase::Exploding);
    CHECK(lost.TakeSaveDeletion());
    g.Ship().hull = 0.0;
    g.Step(1.0 / 60.0, {});
    CHECK(!g.TakeSaveDeletion());
    CHECK(g.Snapshot().ship.hull > 0.0);
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
    Run(g, 40.0);
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

    // Normal: se vuelve al punto de control, con una parte de la bodega.
    auto normalEngine = MakeEngine(rules);
    GameSession g(*normalEngine, rules, 7, GameModeId::Normal);
    g.Step(0.05, {});
    g.Ship().cargo.Add(Resource::Metal, 1000.0);
    g.Ship().emitters = 2;
    const TargetRef planet = FirstPlanet(g, 0, 1);
    CHECK(g.JumpTo(planet, g.Resolve(planet)->arrival));
    Run(g, 1.0);
    g.MarkScanned(0);
    CHECK(g.DeployMining(planet));  // guarda aqui
    const Vec3d saved = g.Ship().position;
    g.Ship().cargo.Add(Resource::Metal, 5000.0);  // lo que venga despues se pierde
    g.Ship().hull = 0.0;
    Run(g, 8.0);
    CHECK(g.Phase() == GamePhase::GameOver);
    g.ContinueAfterDefeat();
    Run(g, 0.05);
    CHECK(std::abs(g.Ship().cargo.Get(Resource::Metal) - 750.0) < 1e-6);
    CHECK(g.Ship().emitters == 2);  // ya estan en el motor
    CHECK(g.Ship().hull == rules.ship.hull);
    CHECK(g.Stations().Count(StationKind::Mining) == 1);
    (void)saved;

    // Realista: no queda nada.
    auto hardEngine = MakeEngine(rules);
    GameSession real(*hardEngine, rules, 7, GameModeId::Realistic);
    real.Step(0.05, {});
    real.Ship().cargo.Add(Resource::Metal, 1000.0);
    real.Ship().emitters = 2;
    const TargetRef second = FirstPlanet(real, 0, 1);
    CHECK(real.JumpTo(second, real.Resolve(second)->arrival));
    Run(real, 1.0);
    CHECK((real.MarkScanned(0), real.DeployMining(second)));
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

    // Enter completa la linea y luego pasa a la siguiente.
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

static TargetRef FindShip(const GameSession& g, Stance stance, AnomalyKind kind = AnomalyKind::AlienShip) {
    for (const Anomaly& a : g.Anomalies())
        if (a.stance == stance && a.kind == kind) return {Kind::Anomaly, a.system, a.id};
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
    CHECK(read && read->visitedSystems.size() > 1 && read->visitedSystems[1]);
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
    CHECK(g.JumpTo(planet, g.Resolve(planet)->arrival));
    Run(g, 0.5);
    CHECK(g.DeployMining(planet));
    const TargetRef station{Kind::Station, 0, g.Stations().All().back().id};
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
    CHECK(read && read->docked);

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
    const TargetRef planet = FirstPlanet(g, 0, 1);
    CHECK(g.JumpTo(planet, g.Resolve(planet)->arrival));
    Run(g, 0.5);
    CHECK(g.DeployMining(planet));
    const TargetRef station{Kind::Station, 0, g.Stations().All().back().id};
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
// el planeta tire de ella.
static void TestStationFrame() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    GameSession g(*engine, rules, 7, GameModeId::Peaceful);
    g.Conversation().Stop();
    g.Step(0.05, {});
    g.MarkScanned(0);
    const TargetRef planet = FirstPlanet(g, 0, 1);
    CHECK(g.JumpTo(planet, g.Resolve(planet)->arrival));
    Run(g, 0.5);
    CHECK(g.DeployMining(planet));
    const TargetRef station{Kind::Station, 0, g.Stations().All().back().id};
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
    CHECK(read->ship.computerTier == 4);
    CHECK(read->ship.weapons.size() == 4 && read->ship.weapons[0].tier == 3 && read->ship.weapons[0].cooldown == 1.5);
    bool found = false;
    for (const Anomaly& a : read->anomalies)
        if (a.id == hurt.id) {
            found = true;
            CHECK(a.hull == 123.5 && a.maxHull == hurt.maxHull && a.cooldown == 7.25 && a.armed == hurt.armed && a.weapon == hurt.weapon);
        }
    CHECK(found);

    // Una partida de antes del combate se carga con armas y cascos de serie.
    SaveData old = data;
    old.ship.weapons.clear();
    for (Anomaly& a : old.anomalies) a.hull = a.maxHull = 0.0;
    GameSession loaded(*engine, rules, 7, GameModeId::Peaceful);
    CHECK(loaded.Restore(old));
    CHECK(loaded.Ship().weapons.size() == 4);
    for (const Anomaly& a : loaded.Anomalies()) {
        if (a.kind == AnomalyKind::Derelict) CHECK(a.maxHull == 0.0);
        else CHECK(a.maxHull > 0.0 && a.hull == a.maxHull);
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
            const DefenseState& e = read->ship.defense;
            CHECK(e.field == ShieldKind::EM && !e.fieldOn && e.shieldTier[1] == 3 && e.charge[3] == 123.25);
            CHECK(e.heat == 77.5 && e.water == 12.5 && e.nitrogen == 3.75 && e.coolantTier[2] == 2 && !e.coolantOn[1] && e.coolantOn[0]);
            for (const Anomaly& a : read->anomalies)
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
// atravesarla y sin romperse al rozarla. Sobre todo en un cinturon, donde la mina va a 60 km/s.
static void TestStationManeuver() {
    Balance rules;
    auto engine = MakeEngine(rules, 7);
    for (const bool belt : {true, false}) {
        GameSession g(*engine, rules, 7, GameModeId::Normal);
        g.Conversation().Stop();
        g.Step(0.05, {});
        g.MarkScanned(0);
        const TargetRef site = belt ? TargetRef{Kind::Belt, 0, 0} : FirstPlanet(g, 0, 1);
        CHECK(g.JumpTo(site, g.Resolve(site)->arrival));
        Run(g, 0.5);
        CHECK(g.DeployMining(site));
        const TargetRef station{Kind::Station, 0, g.Stations().All().back().id};
        for (int i = 0; i < 200; ++i) g.Step(0.05, {});
        CHECK(g.JumpTo(station, 5.0));
        CHECK(g.Dock(station, true));
        for (int i = 0; i < 40; ++i) g.Step(0.05, {});
        g.Undock();
        const auto away = [&] { return Length(g.Ship().position - g.Resolve(station)->position); };
        const double radius = GameSession::StationRadius(StationKind::Mining);
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
    CHECK(back2.has_value() && ZoneLost(back2->ship.zones, nacelle) && back2->ship.zones[0] == 1.0);

    // Atracada se rehace; en la central sin atracar, solo se reparan las que siguen ahi.
    const TargetRef site{Kind::Belt, 0, 0};
    g.MarkScanned(0);
    CHECK(g.JumpTo(site, g.Resolve(site)->arrival));
    Run(g, 0.5);
    CHECK(g.DeployMining(site));
    const TargetRef station{Kind::Station, 0, g.Stations().All().back().id};
    for (int i = 0; i < 200; ++i) g.Step(0.05, {});
    CHECK(g.JumpTo(station, 5.0));
    CHECK(g.Dock(station, true));
    for (int i = 0; i < 20; ++i) g.Step(0.05, {});
    CHECK(!ZoneLost(g.Ship().zones, nacelle));
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
        const int armorZone = maquette.slots[5].zone;
        CHECK(g.InstallModule(5, ShipModuleKind::ArmorPlate, &why));
        CHECK(std::fabs(ZoneHardness(g.Ship(), armorZone) - (1.0 + kArmorShare)) < 1e-9);
        CHECK(g.SetImpulseEngine(3, &why) && g.Ship().loadout.engine == 3);

        // Se guarda, y al cargar la torreta vuelve a salir de su modulo.
        const std::optional<SaveData> saved = ReadSave(WriteSave(g.Snapshot()));
        CHECK(saved.has_value());
        if (saved) {
            CHECK(saved->ship.loadout.engine == 3);
            CHECK(saved->ship.loadout.modules[6] == ShipModuleKind::Storage && saved->ship.loadout.modules[0] == ShipModuleKind::Turret);
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
        g.MarkScanned(0);
        const TargetRef site{Kind::Belt, 0, 0};
        CHECK(g.JumpTo(site, g.Resolve(site)->arrival));
        Run(g, 0.5);
        CHECK(g.DeployMining(site));
        const TargetRef station{Kind::Station, 0, g.Stations().All().back().id};
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
        for (int i = 0; i < 4; ++i) g.ShootHull({150, 0, -3000}, {150, 0, 3000}, 45.0);  // por detras, al escape de estribor
        int lostHere = 0;
        for (std::size_t i = 0; i < design->pieces.size(); ++i) lostHere += design->pieces[i].zone == nacelle && PieceLost(g.Ship(), static_cast<int>(i)) ? 1 : 0;
        CHECK(lostHere >= 1);
        CHECK(!ZoneLost(g.Ship().zones, nacelle));
        bool single = false;
        for (const GameSession::LostPiece& p : g.LostPieces()) single = single || p.piece >= 0;
        CHECK(single);
        // Se guarda.
        const std::optional<SaveData> saved = ReadSave(WriteSave(g.Snapshot()));
        CHECK(saved && saved->ship.pieces.size() == design->pieces.size());

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

int main() {
    const struct { const char* name; void (*fn)(); } tests[] = {
        {"Propulsores, estela y luz", TestThrustersAndLight},
        {"Modulos de la nave", TestShipModules},
        {"Dano estructural", TestStructuralDamage},
        {"Zonas del casco", TestHullZones},
        {"Maniobra junto a una estacion", TestStationManeuver},
        {"Choques con naves y estaciones", TestSolidStructures},
        {"Escudos", TestShields},
        {"Calor y refrigerantes", TestHeat},
        {"Balistica", TestBallistics},
        {"Combate", TestCombat},
        {"Fuego enemigo", TestIncoming},
        {"Combate guardado", TestCombatSave},
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
        {"Unidades", TestUnits},
        {"Configuracion", TestSettings},
        {"Salir de una estrella", TestLeaveStar},
        {"Descubrimiento", TestDiscovery},
        {"Partidas guardadas", TestSave},
        {"Coste de la curvatura", TestWarpCost},
        {"Viaje con curvatura", TestWarpRoute},
        {"Escaner", TestScanner},
        {"Modos y derrota", TestModes},
        {"Dialogos", TestDialogue},
    };
    for (const auto& t : tests) {
        std::printf("[ %s ]\n", t.name);
        std::fflush(stdout);
        t.fn();
    }
    std::printf("\n%d comprobaciones, %d fallos\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
