#include "game/Ship.h"

#include <algorithm>

#include "game/Armament.h"
#include "game/Defense.h"
#include <cmath>

namespace game {

namespace {

constexpr double kHour = 3600.0;

double EmergencySpeed(const DriveRules& rules) { return rules.impulseSpeed * rules.emergencyFraction; }

double Handling(const ShipState& s, const ShipRules& r) { return (1.0 + r.handlingBonus * s.handlingTier) / std::max(s.massFactor, 1.0) * SteeringOutput(s.zones); }

// Giro que lleva de "from" a "to", en el mundo, por el camino corto.
void TurnBetween(const eps::Quatd& from, const eps::Quatd& to, eps::Vec3d& axis, double& angle) {
    eps::Quatd e = to * from.Conjugate();
    if (e.w < 0.0) e = eps::Quatd{-e.x, -e.y, -e.z, -e.w};
    const eps::Vec3d v{e.x, e.y, e.z};
    const double length = eps::Length(v);
    angle = 2.0 * std::atan2(length, e.w);
    axis = length > 1e-12 ? v * (1.0 / length) : eps::Vec3d{0, 1, 0};
}

}  // namespace

const char* Name(DriveMode mode) {
    static const char* const names[] = {"Impulso", "Crucero", "Híbrida", "Hiper"};
    return names[static_cast<int>(mode)];
}

ShipState MakeShip(const Balance& rules) {
    ShipState s;
    s.energy = rules.ship.energy;
    s.antimatter = rules.ship.startAntimatter;
    s.hull = rules.ship.hull;
    s.heatShieldTier = rules.ship.heatShieldTier;
    s.scannerTier = rules.scanner.startTier;
    s.computerTier = rules.ship.computerTier;
    s.weapons = DefaultWeapons(rules.ship);
    s.defense = MakeDefense(rules.defense, rules.thermal);
    s.cargo.Add(Resource::Silicate, rules.ship.startSilicate);
    s.cargo.Add(Resource::Gas, rules.ship.startGas);
    s.loadout.starter = true;  // sale con dos bahias de drones mineros
    s.loadout.starterBays = rules.armament.starterBays;
    FitToDesign(s);
    return s;
}

void FitToDesign(ShipState& s) {
    const ShipDesign& design = CurrentShipDesign();
    s.loadout.modules.resize(design.slots.size(), ShipModuleKind::None);
    s.loadout.ammo.resize(design.slots.size(), 0);
    s.loadout.cooldown.resize(design.slots.size(), 0.0);
    // Un modulo en una ranura del tipo equivocado (otra nave) se quita.
    for (std::size_t i = 0; i < design.slots.size(); ++i)
        if (s.loadout.modules[i] != ShipModuleKind::None && ModuleSpec(s.loadout.modules[i]).slot != design.slots[i].kind) s.loadout.modules[i] = ShipModuleKind::None;
    // Nave de serie: las bahias de drones van donde diga la nave que se use (puede cambiar al cargar el .glb).
    if (s.loadout.starter) {
        for (ShipModuleKind& m : s.loadout.modules)
            if (m == ShipModuleKind::DroneBay) m = ShipModuleKind::None;
        for (const int slot : StarterSlots(design)) s.loadout.modules[static_cast<std::size_t>(slot)] = ShipModuleKind::DroneBay;
    }
    s.loadout.engine = std::clamp(s.loadout.engine, 1, kImpulseEngineCount);
    // Los misiles solo van en bahias, y no caben mas que su capacidad.
    static const ArmamentRules armament;
    for (std::size_t i = 0; i < s.loadout.modules.size(); ++i) {
        const HumanWeaponSpec* w = HumanWeaponOf(armament, s.loadout.modules[i]);
        s.loadout.ammo[i] = w ? std::clamp(s.loadout.ammo[i], 0, w->capacity) : 0;
    }
    s.pieces.resize(design.pieces.size(), 1.0);
}

bool PieceLost(const ShipState& s, int piece) { return piece >= 0 && piece < static_cast<int>(s.pieces.size()) && s.pieces[static_cast<std::size_t>(piece)] <= 0.0; }

bool SlotWorking(const ShipState& s, int slot) {
    const ShipDesign& design = CurrentShipDesign();
    if (slot < 0 || slot >= static_cast<int>(design.slots.size())) return false;
    const ModuleSlot& m = design.slots[static_cast<std::size_t>(slot)];
    return !ZoneLost(s.zones, m.zone) && !PieceLost(s, m.piece);
}

int WorkingModules(const ShipState& s, ShipModuleKind kind, int zone) {
    const ShipDesign& design = CurrentShipDesign();
    int count = 0;
    for (std::size_t i = 0; i < s.loadout.modules.size() && i < design.slots.size(); ++i)
        if (s.loadout.modules[i] == kind && (zone < 0 || design.slots[i].zone == zone) && SlotWorking(s, static_cast<int>(i))) ++count;
    return count;
}

double CargoCapacity(const ShipState& s, const ShipRules& rules) {
    return rules.cargo * static_cast<double>(HoldsLeft(s.zones)) / 6.0 + kStorageCargo * WorkingModules(s, ShipModuleKind::Storage);
}

double AntimatterCapacity(const ShipState& s, const ShipRules& rules) { return rules.antimatterTank + kTankAntimatter * WorkingModules(s, ShipModuleKind::AntimatterTank); }

double ZoneHardness(const ShipState& s, int zone) {
    return 1.0 + kArmorShare * WorkingModules(s, ShipModuleKind::ArmorPlate, zone) + kReinforcementShare * WorkingModules(s, ShipModuleKind::Reinforcement, zone);
}

std::vector<WeaponMount> DefaultWeapons(const ShipRules& rules) {
    std::vector<WeaponMount> out;
    for (int k = 0; k < static_cast<int>(WeaponKind::Count); ++k) out.push_back({static_cast<WeaponKind>(k), rules.weaponTier, 0.0});
    return out;
}

eps::Vec3d Forward(const eps::Quatd& rotation) { return rotation.ToMatrix().TransformDirection(eps::Vec3d{0, 0, 1}); }

eps::Vec3d Heading(const ShipState& s) { return Forward(s.orientation); }

eps::Vec3d AimHeading(const ShipState& s) { return Forward(s.aim); }

eps::Quatd TurnedToward(const eps::Quatd& rotation, const eps::Vec3d& direction) {
    const eps::Vec3d from = Forward(rotation), to = eps::Normalize(direction);
    const double cosine = std::clamp(eps::Dot(from, to), -1.0, 1.0);
    eps::Vec3d axis = eps::Cross(from, to);
    if (eps::Length(axis) < 1e-9) {
        if (cosine > 0.0) return rotation;
        // Justo al reves: cualquier eje perpendicular sirve.
        axis = eps::Cross(from, std::abs(from.y) < 0.9 ? eps::Vec3d{0, 1, 0} : eps::Vec3d{1, 0, 0});
    }
    return (eps::Quatd::FromAxisAngle(axis, std::acos(cosine)) * rotation).Normalized();
}

void AimAt(ShipState& s, const eps::Vec3d& direction) { s.aim = TurnedToward(s.aim, direction); }

void FaceTo(ShipState& s, const eps::Vec3d& direction) {
    s.orientation = TurnedToward(s.orientation, direction);
    s.aim = s.orientation;
    s.angularRate = {};
}

void LimitAim(ShipState& s, double maxAngle) {
    eps::Vec3d axis;
    double angle = 0;
    TurnBetween(s.orientation, s.aim, axis, angle);
    if (angle > maxAngle) s.aim = (eps::Quatd::FromAxisAngle(axis, maxAngle) * s.orientation).Normalized();
}

bool HybridUnlocked(const ShipState& s) { return s.emitters > 0; }

bool HyperUnlocked(const ShipState& s, const DriveRules& r) { return s.emitters >= r.hyperEmitters; }

bool DriveAvailable(const ShipState& s, DriveMode mode, const DriveRules& r) {
    // Todo lo que no es impulso curva el espacio: sin anillo no hay crucero ni burbuja.
    if (mode != DriveMode::Impulse && WarpIntegrity(s.zones) < kWarpRingMinimum) return false;
    if (mode == DriveMode::Warp) return HybridUnlocked(s);
    if (mode == DriveMode::Hyper) return HyperUnlocked(s, r);
    return true;
}

double HyperFactor(const ShipState& s, const DriveRules& r) {
    const double extra = std::max(s.emitters - r.hyperEmitters, 0);
    return std::min(r.hyperBase * (1.0 + r.hyperGrowth * std::log2(1.0 + extra)), r.hyperCap);
}

double IonGasFactor(const ShipState& s, const DriveRules& r) { return s.cargo.Get(Resource::Gas) > 0.0 ? 1.0 : r.ionNoGas; }

double TopSpeed(const ShipState& s, DriveMode mode, const DriveRules& r) {
    // Un anillo danado deforma peor el espacio: llega menos lejos.
    const double ring = 0.4 + 0.6 * std::clamp(WarpIntegrity(s.zones), 0.0, 1.0);
    switch (mode) {
        case DriveMode::Cruise: return kLightSpeed * r.cruiseFraction * ring;
        case DriveMode::Warp: return kLightSpeed * r.warpFactor * ring;
        case DriveMode::Hyper: return kLightSpeed * (HyperUnlocked(s, r) ? HyperFactor(s, r) : r.warpFactor) * ring;
        default: return r.impulseSpeed * ImpulseEngine(s.loadout.engine).speed * IonGasFactor(s, r);
    }
}

bool Curves(DriveMode mode) { return mode == DriveMode::Warp || mode == DriveMode::Hyper; }

double FormationEnergy(const ShipState& s, DriveMode mode, const DriveRules& r) {
    if (mode == DriveMode::Warp || !HyperUnlocked(s, r)) return r.formationEnergy;
    // Cuanto mas se acerca al tope, mas fina es la geometria.
    const double progress = std::clamp((HyperFactor(s, r) - r.hyperBase) / (r.hyperCap - r.hyperBase), 0.0, 1.0);
    return r.hybridFormationHigh * std::pow(r.hybridFormationLow / r.hybridFormationHigh, progress);
}

double FormationCost(const ShipState& s, DriveMode mode, const DriveRules& r) { return FormationEnergy(s, mode, r) / kAnnihilationEnergy; }

bool CanCurve(const ShipState& s, DriveMode mode, const DriveRules& r) {
    return DriveAvailable(s, mode, r) && s.antimatter > 0.0 &&s.antimatter >= (1.0 - s.warpCharge) * FormationCost(s, mode, r);
}

double MaintenanceRatio(const ShipState& s, const DriveRules& r) {
    const double speed = std::clamp(s.warpSpeed / TopSpeed(s, s.mode, r), 0.0, 1.0);
    const double time = 1.0 - std::exp(-s.warpAge / r.maintenanceSettle);
    return r.maintenanceRatioLow * std::pow(r.maintenanceRatioHigh / r.maintenanceRatioLow, 0.5 * (speed + time));
}

double MaintenancePower(const ShipState& s, const DriveRules& r) {
    return std::max(FormationEnergy(s, s.mode, r) / MaintenanceRatio(s, r), r.maintenanceFloor);
}

double RequestedSpeed(const ShipState& s, const DriveRules& r) {
    const double t = std::clamp(s.throttle, 0.0, 1.0);
    if (s.mode == DriveMode::Impulse) return r.impulseSpeed * ImpulseEngine(s.loadout.engine).speed * IonGasFactor(s, r) * t;
    // Con un cuadrado, el mando sirve tanto para ir a 1000 km/s como para llegar al tope.
    return std::max(TopSpeed(s, s.mode, r) * t * t, r.impulseSpeed * t);
}

double LorentzFactor(double speed) {
    const double beta = speed / kLightSpeed;
    return beta < 1.0 ? 1.0 / std::sqrt(1.0 - beta * beta) : 1.0;
}

double AimError(const ShipState& s) {
    return std::acos(std::clamp(eps::Dot(Heading(s), AimHeading(s)), -1.0, 1.0));
}

// Acelera el giro hacia lo apuntado y frena a tiempo para no pasarse.
void StepHeading(ShipState& s, const ShipRules& r, double realSeconds) {
    // Los propulsores de maniobra gastan gas; sin el solo queda el giroscopio, que gira mas despacio.
    const bool gas = s.cargo.Get(Resource::Gas) > 0.0;
    const double handling = Handling(s, r) * (gas ? 1.0 : r.gyroOnly);
    const double top = r.turnRate * handling, accel = r.turnAccel * handling;
    eps::Vec3d axis;
    double angle = 0;
    TurnBetween(s.orientation, s.aim, axis, angle);

    // Velocidad con la que aun se puede frenar en lo que queda: v^2 = 2 a d.
    const eps::Vec3d wanted = axis * std::min(top, std::sqrt(2.0 * accel * angle));
    const eps::Vec3d change = wanted - s.angularRate;
    const double most = accel * realSeconds, size = eps::Length(change);
    const eps::Vec3d push = size > most ? change * (most / size) : change;
    s.angularRate += push;
    s.angularPush = realSeconds > 0.0 && gas ? push * (1.0 / realSeconds) : eps::Vec3d{};
    if (gas) s.cargo.Add(Resource::Gas, -std::min(eps::Length(push) * r.rcsGas, s.cargo.Get(Resource::Gas)));

    const double rate = eps::Length(s.angularRate);
    if (angle < 1e-3 && rate < most) {
        s.orientation = s.aim;
        s.angularRate = {};
    } else if (rate > 0.0) {
        s.orientation = (eps::Quatd::FromAxisAngle(s.angularRate, rate * realSeconds) * s.orientation).Normalized();
    }
}

eps::Vec3d TransportUp(const eps::Vec3d& up, const eps::Vec3d& forward) {
    const eps::Vec3d flat = up - forward * eps::Dot(up, forward);
    if (eps::Length(flat) > 1e-6) return eps::Normalize(flat);
    return eps::Normalize(eps::Cross(forward, std::abs(forward.y) < 0.9 ? eps::Vec3d{0, 1, 0} : eps::Vec3d{1, 0, 0}));
}

double ExteriorRate(double speed, double gravity, double limit) {
    if (speed >= kLightSpeed) return limit;
    const double well = 1.0 / std::sqrt(1.0 - std::clamp(gravity, 0.0, 0.998));
    return std::min(LorentzFactor(speed) * well, limit);
}

void StepReactor(ShipState& s, const Balance& rules, double seconds) {
    const DriveRules& r = rules.drive;
    const double given = std::min({rules.ship.energy - s.energy, r.reactorRate * ReactorOutput(s.zones) * seconds / kHour, s.antimatter * kAnnihilationEnergy});
    if (given > 0.0) {
        s.energy += given;
        s.antimatter -= given / kAnnihilationEnergy;
    }
    // Esto solo aporta con el motor apagado: esperar siempre es una salida.
    if (s.throttle == 0.0 && s.warpSpeed < 1.0) {
        s.energy += r.idleRecharge * seconds / kHour;
        s.antimatter = std::min(s.antimatter + r.idleAntimatter * seconds / kHour, std::max(s.antimatter, AntimatterCapacity(s, rules.ship)));
    }
}

double StepDrive(ShipState& s, const Balance& rules, double seconds, double exterior, double realSeconds, const DriveInput& in) {
    const DriveRules& r = rules.drive;
    if (!DriveAvailable(s, s.mode, r)) {
        if (DriveAvailable(s, DriveMode::Warp, r)) s.mode = DriveMode::Warp;
        else if (DriveAvailable(s, DriveMode::Cruise, r)) s.mode = DriveMode::Cruise;
        else s.mode = DriveMode::Impulse;
    }
    if (s.throttle != 0.0) s.holding = false;
    const double handling = Handling(s, rules.ship);
    const eps::Vec3d heading = Heading(s);

    // La burbuja tarda en formarse y se paga mientras se forma; hasta entonces solo empuja el motor
    // normal y se conserva lo que traiga del crucero. El crucero no curva nada: ni espera ni paga.
    const bool curves = Curves(s.mode);
    const bool running = s.mode != DriveMode::Impulse && s.throttle > 0.0 && (curves ? CanCurve(s, s.mode, r) : s.energy > 0.0);
    const double charge = seconds / r.warpCharge * std::clamp(WarpIntegrity(s.zones), 0.1, 1.0);
    if (curves && running) {
        const double gained = std::min(charge, 1.0 - s.warpCharge);
        s.antimatter = std::max(s.antimatter - gained * FormationCost(s, s.mode, r), 0.0);
        s.warpCharge = std::min(s.warpCharge + charge, 1.0);
    } else {
        s.warpCharge = std::max(s.warpCharge - charge * 2.0, 0.0);
    }
    double wantedWarp = 0.0;
    if (running) wantedWarp = !curves || s.warpCharge >= 1.0 ? RequestedSpeed(s, r) : std::min(s.warpSpeed, TopSpeed(s, DriveMode::Cruise, r));
    if (in.warpLimit > 0.0) wantedWarp = std::min(wantedWarp, in.warpLimit);
    const double change = wantedWarp < s.warpSpeed ? r.warpBrake : r.response;  // frena mas que acelera
    s.warpSpeed += (wantedWarp - s.warpSpeed) * (1.0 - std::exp(-seconds * handling / change));
    if (s.warpSpeed < 1e-3) s.warpSpeed = 0.0;
    const bool assisted = in.wanted != nullptr;
    if (assisted || s.warpSpeed <= 0.0) s.warpHeading = heading;
    else s.warpHeading = eps::Normalize(s.warpHeading + (heading - s.warpHeading) * (1.0 - std::exp(-realSeconds * handling / rules.ship.driftTime)));

    // Las gondolas empujan: con una menos se acelera la mitad, y sin ninguna apenas se mueve.
    const double engines = EngineOutput(s.zones);
    // El motor de impulso montado (ShipModules.h) sube la velocidad y la aceleracion, y gasta mas.
    const ImpulseEngineSpec& impulse = ImpulseEngine(s.loadout.engine);
    const double accel = r.impulseAccel * impulse.accel * handling * engines * (s.energy > 0.0 ? 1.0 : r.emergencyFraction);
    const double top = (s.energy > 0.0 ? r.impulseSpeed * impulse.speed : EmergencySpeed(r)) * std::min(1.0, 0.3 + engines) * IonGasFactor(s, r);
    const bool bubble = s.warpSpeed > r.impulseSpeed;

    // Dentro de la burbuja el espacio se mueve con ella: ni gravedad ni nebulosa.
    if (!bubble) s.velocity += in.gravity * exterior;

    double burn = 0;
    if (assisted || s.holding) {
        // Los propulsores llevan la velocidad a la pedida y compensan la gravedad.
        const eps::Vec3d goal = assisted ? *in.wanted : eps::Vec3d{};
        const eps::Vec3d needed = goal - s.velocity;
        const double size = eps::Length(needed), most = accel * seconds * 2.0;
        s.velocity += size > most ? needed * (most / size) : needed;
        burn = r.impulseBurn * impulse.burn * std::min(size / std::max(most, 1e-9), 1.0) * seconds / kHour;
    } else {
        const bool cruising = s.mode == DriveMode::Cruise && s.throttle > 0.0 && s.warpSpeed > r.impulseSpeed;
        const bool pushing = s.throttle != 0.0 && s.warpCharge < 1.0 && !cruising;
        if (pushing) {
            const double before = eps::Length(s.velocity);
            const double push = s.throttle > 0.0 ? s.throttle : s.throttle * r.reverseThrust;  // marcha atras
            s.velocity += heading * (accel * push * seconds);
            const double after = eps::Length(s.velocity), limit = std::max(top, before);
            if (after > limit) s.velocity = s.velocity * (limit / after);
            burn = r.impulseBurn * impulse.burn * std::abs(s.throttle) * seconds / kHour;
        }
        if (!bubble) s.velocity = s.velocity * std::exp(-r.drag * in.dragScale * exterior);
    }

    // El propelente de los iones sale de la bodega, en proporcion al empuje.
    if (burn > 0.0 && r.impulseBurn > 0.0) {
        const double full = r.impulseBurn * impulse.burn * seconds / kHour;
        const double use = r.ionGas * seconds * std::min(burn / std::max(full, 1e-12), 1.0);
        s.cargo.Add(Resource::Gas, -std::min(use, s.cargo.Get(Resource::Gas)));
    }

    const eps::Vec3d total = s.velocity + s.warpHeading * s.warpSpeed;
    s.speed = eps::Length(total);
    s.moveDirection = s.speed > 1e-6 ? total * (1.0 / s.speed) : heading;
    const double distance = s.speed * exterior;
    s.position += total * exterior;
    s.shipHours += seconds / kHour;

    // Mantener la burbuja se paga en tiempo de fuera, que es el que tarda en cruzar el espacio.
    if (curves && s.warpCharge >= 1.0 && s.warpSpeed > r.impulseSpeed) {
        s.warpAge += exterior;
        s.antimatter = std::max(s.antimatter - MaintenancePower(s, r) * exterior / kAnnihilationEnergy, 0.0);
    } else {
        s.warpAge = 0.0;
        if (s.warpSpeed > r.impulseSpeed) burn += r.cruiseBurn * std::min(s.warpSpeed / kLightSpeed, 1.0) * seconds / kHour;
    }
    s.energy = std::max(s.energy - burn, 0.0);
    StepReactor(s, rules, seconds);
    return distance;
}

}  // namespace game
