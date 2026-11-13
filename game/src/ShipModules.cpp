#include "game/ShipModules.h"

#include <algorithm>
#include <array>

#include "game/Balance.h"

namespace game {

using namespace eps;

const ShipModuleSpec& ModuleSpec(ShipModuleKind kind) {
    static const ShipModuleSpec specs[] = {
        {"Vacía", "Nada montado.", SlotKind::External, 0, 0},
        {"Torreta", "Una torreta más, con su propia recarga. Dispara desde donde está montada.", SlotKind::External, 260, 60},
        {"Aleta de radiador", "Suelta calor al vacío: un 25 % más de refrigeración.", SlotKind::External, 180, 40},
        {"Antena de sensores", "Un 30 % más de alcance del escáner.", SlotKind::External, 150, 90},
        {"Placa de blindaje", "Su zona aguanta un 40 % más de golpes.", SlotKind::External, 320, 0},
        {"Almacén", "1 Mt más de bodega.", SlotKind::Internal, 200, 20},
        {"Tanque de antimateria", "40 kt más de antimateria: unas siete burbujas.", SlotKind::Internal, 240, 120},
        {"Refuerzo estructural", "Cuadernas extra: su zona aguanta un 30 % más.", SlotKind::Internal, 280, 30},
        {"Bahía de drones", "Dos drones mineros: van a un planeta o un campo, minan y descargan en la bodega.", SlotKind::External, 300, 120},
        {"Plataforma de defensa puntual", "Derriba misiles y drones que se acercan. Va en un hueco de torreta clásica.", SlotKind::External, 0.15, 0},
    };
    // Las armas humanas salen de ArmamentRules: cuestan su masa real en metal (los elementos y el plano, aparte).
    static const std::array<ShipModuleSpec, kHumanWeaponCount> weaponSpecs = [] {
        const ArmamentRules rules;
        std::array<ShipModuleSpec, kHumanWeaponCount> out{};
        for (int w = 0; w < kHumanWeaponCount; ++w) out[static_cast<std::size_t>(w)] = {rules.weapons[w].name, rules.weapons[w].description, SlotKind::External, rules.weapons[w].mass, 0.0};
        return out;
    }();
    if (IsHumanWeapon(kind)) return weaponSpecs[static_cast<std::size_t>(static_cast<int>(kind) - static_cast<int>(kFirstHumanWeapon))];
    const int i = static_cast<int>(kind);
    return specs[std::clamp(i, 0, static_cast<int>(ShipModuleKind::PointDefensePlatform))];
}

const ImpulseEngineSpec& ImpulseEngine(int tier) {
    static const ImpulseEngineSpec engines[kImpulseEngineCount] = {
        {"Iónico de serie", "El motor con el que sale la nave.", 1.0, 1.0, 1.0, 0, 0, {0.55f, 0.85f, 1.0f}},
        {"Iónico de alto flujo", "Más gas por la tobera: un 50 % más rápido.", 1.5, 1.3, 1.4, 400, 120, {0.35f, 1.0f, 0.95f}},
        {"Plasma pulsado", "Pulsos de plasma confinado: más del doble de rápido.", 2.2, 1.7, 2.0, 900, 300, {0.75f, 0.45f, 1.0f}},
        {"Fusión magnética", "Escape de fusión: el triple de rápido, a costa de energía.", 3.0, 2.2, 3.0, 1800, 700, {1.0f, 0.95f, 0.85f}},
    };
    return engines[std::clamp(tier, 1, kImpulseEngineCount) - 1];
}

namespace {

std::shared_ptr<const ShipDesign>& Current() {
    static std::shared_ptr<const ShipDesign> design;
    return design;
}

Vec3d ZoneCenter(int zone) {
    if (const ZoneLayout* layout = CustomZoneLayout()) return layout->centers[static_cast<std::size_t>(zone)];
    return ZoneInfo(zone).center;
}

}  // namespace

void SetShipDesign(std::shared_ptr<const ShipDesign> design) { Current() = std::move(design); }

const ShipDesign& CurrentShipDesign() {
    if (!Current()) {
        auto maquette = std::make_shared<ShipDesign>();
        maquette->slots = MaquetteHardpoints();
        for (const ModuleSlot& s : InteriorBays()) maquette->slots.push_back(s);
        Current() = maquette;
    }
    return *Current();
}

std::vector<ModuleSlot> InteriorBays() {
    std::vector<ModuleSlot> bays;
    const auto add = [&](HullZone zone, int count) {
        for (int i = 0; i < count; ++i) {
            ModuleSlot s;
            const int z = static_cast<int>(zone);
            s.name = std::string("bahía ") + std::to_string(i + 1) + (ZoneInfo(z).masculine ? " del " : " de la ") + ZoneInfo(z).name;
            s.kind = SlotKind::Internal;
            s.zone = z;
            s.position = ZoneCenter(z);
            bays.push_back(s);
        }
    };
    for (int h = static_cast<int>(HullZone::Hold1); h <= static_cast<int>(HullZone::Hold6); ++h) add(static_cast<HullZone>(h), 2);
    add(HullZone::Spine, 3);
    add(HullZone::Core, 2);
    add(HullZone::Bridge, 1);
    add(HullZone::Bow, 1);
    return bays;
}

std::vector<ModuleSlot> MaquetteHardpoints() {
    // Encima del puente (que en el mundo es -Y), a los lados, y arriba y abajo del nucleo.
    const struct { const char* name; Vec3d at, up; } points[] = {
        {"hp_torreta_1", {-30, -100, 300}, {0, -1, 0}},
        {"hp_torreta_2", {30, -100, 300}, {0, -1, 0}},
        {"hp_torreta_3", {-45, -80, 380}, {-1, 0, 0}},
        {"hp_torreta_4", {45, -80, 380}, {1, 0, 0}},
        {"hp_torreta_5", {0, -40, -300}, {0, -1, 0}},
        {"hp_torreta_6", {0, 40, -300}, {0, 1, 0}},
    };
    std::vector<ModuleSlot> slots;
    for (const auto& p : points) {
        ModuleSlot s;
        s.name = p.name;
        s.kind = SlotKind::External;
        s.position = p.at;
        s.normal = p.up;
        s.zone = NearestZone(p.at);
        slots.push_back(s);
    }
    return slots;
}

double PieceDistance(const DesignPiece& piece, const Vec3d& point) {
    if (piece.samples.empty()) return Length(piece.center - point) - piece.radius;
    double nearest = 1e300;
    for (const Vec3d& s : piece.samples) nearest = std::min(nearest, Length(s - point));
    return nearest;
}

std::vector<int> StarterSlots(const ShipDesign& design) {
    std::vector<int> out;
    for (const char* wanted : {"hp_torreta_5", "hp_torreta_6"})
        for (std::size_t i = 0; i < design.slots.size(); ++i)
            if (design.slots[i].kind == SlotKind::External && design.slots[i].name == wanted) out.push_back(static_cast<int>(i));
    if (out.size() == 2) return out;
    out.clear();
    for (int i = static_cast<int>(design.slots.size()) - 1; i >= 0 && out.size() < 2; --i)
        if (design.slots[static_cast<std::size_t>(i)].kind == SlotKind::External) out.insert(out.begin(), i);
    return out;
}

std::string PrettyName(const std::string& name) {
    std::string out = name.rfind("hp_", 0) == 0 ? name.substr(3) : name;
    std::replace(out.begin(), out.end(), '_', ' ');
    return out;
}

}  // namespace game
