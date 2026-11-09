#include "game/GameHud.h"
#include "game/Units.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace game {

using namespace eps;
using Kind = TargetRef::Kind;

namespace {

constexpr float kTau = 6.2831853f;
constexpr float kLeftWidth = 372.0f;   // Audiowide es una letra ancha: los paneles dejan sitio
constexpr float kHeadingSpacing = 0.18f;  // separacion extra entre letras de una cabecera, en partes de su tamano
constexpr float kObjectiveTop = 112.0f;  // el objetivo del tutorial, bajo el nombre del sistema
constexpr float kCursorNotice = kObjectiveTop + 48.0f;  // y el aviso del cursor, bajo el objetivo
constexpr float kShipPanelHeight = 410.0f;  // con la defensa: escudo, conos y calor
constexpr float kRightWidth = 376.0f;

ImU32 Rgba(int r, int g, int b, float a = 1.0f) { return IM_COL32(r, g, b, static_cast<int>(a * 255.0f)); }

// Ambar sobre negro tostado, con amarillo para lo activo.
const ImU32 kAmber = Rgba(255, 170, 40), kAmberDim = Rgba(255, 150, 40, 0.34f), kAmberFaint = Rgba(255, 150, 40, 0.12f);
const ImU32 kYellow = Rgba(255, 228, 96), kOrange = Rgba(255, 110, 24);
const ImU32 kText = Rgba(255, 232, 188), kMuted = Rgba(206, 146, 74);
const ImU32 kPanel = Rgba(18, 10, 4, 0.82f), kPanelDeep = Rgba(8, 4, 2, 0.88f), kShadow = Rgba(0, 0, 0, 0.85f);
const ImU32 kHostile = Rgba(255, 58, 40), kNeutral = Rgba(208, 240, 120), kUnknown = Rgba(214, 196, 170), kVoid = Rgba(204, 132, 255), kOwn = Rgba(255, 228, 96);

std::string Format(const char* format, ...) {
    char text[220];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

std::string Distance(double km) {
    if (km >= 9.46e11) return Format("%.2f al", km / 9.4607304725808e12);
    if (km >= 7.5e6) return Format("%.2f UA", km / 1.495978707e8);
    if (km >= 1.0e6) return Format("%.2f M km", km / 1.0e6);
    return Format("%.0f km", km);
}

std::string Speed(double kmPerSecond) {
    if (kmPerSecond >= kLightSpeed * 1.0001) return Format("%.0f c", kmPerSecond / kLightSpeed);
    if (kmPerSecond >= kLightSpeed * 0.01) return Format("%.4f c", kmPerSecond / kLightSpeed);
    return Format("%.0f km/s", kmPerSecond);
}

// Lienzo con el estado del raton: un boton se dibuja y se comprueba en la misma llamada.
struct Ui {
    ImDrawList* d;
    float s;
    double t;
    ImFont* text;
    ImFont* title;
    ImFont* alien;
    ImFont* logo;
    const HudInput& in;
    std::vector<HudAction>& actions;

    void Label(float x, float y, float size, ImU32 color, const std::string& value) const { LabelIn(text, x, y, size, color, value); }
    void LabelIn(ImFont* font, float x, float y, float size, ImU32 color, const std::string& value) const {
        d->AddText(font, size * s, ImVec2(std::floor(x) + 1.0f, std::floor(y) + 1.0f), kShadow, value.c_str());
        d->AddText(font, size * s, ImVec2(std::floor(x), std::floor(y)), color, value.c_str());
    }
    float WidthIn(ImFont* font, float size, const std::string& value) const { return font->CalcTextSizeA(size * s, 1.0e6f, 0.0f, value.c_str()).x; }

    // Cabeceras con las letras separadas, como en una consola de a bordo. ImGui no sabe espaciar,
    // asi que se pinta letra a letra; en UTF-8 una letra empieza en un byte que no es 10xxxxxx.
    void Spaced(float x, float y, float size, ImU32 color, const std::string& value) const {
        const float gap = size * kHeadingSpacing * s;
        const char* p = value.c_str();
        const char* end = p + value.size();
        while (p < end) {
            const char* next = p + 1;
            while (next < end && (static_cast<unsigned char>(*next) & 0xC0) == 0x80) ++next;
            d->AddText(text, size * s, ImVec2(std::floor(x) + 1.0f, std::floor(y) + 1.0f), kShadow, p, next);
            d->AddText(text, size * s, ImVec2(std::floor(x), std::floor(y)), color, p, next);
            x += text->CalcTextSizeA(size * s, 1.0e6f, 0.0f, p, next).x + gap;
            p = next;
        }
    }
    float SpacedWidth(float size, const std::string& value) const {
        int letters = 0;
        for (const char c : value) letters += (static_cast<unsigned char>(c) & 0xC0) == 0x80 ? 0 : 1;
        return Width(size, value) + size * kHeadingSpacing * s * static_cast<float>(std::max(letters - 1, 0));
    }
    void Title(float x, float y, float size, ImU32 color, const std::string& value) const {
        d->AddText(title, size * s, ImVec2(std::floor(x) + 2.0f, std::floor(y) + 2.0f), kShadow, value.c_str());
        d->AddText(title, size * s, ImVec2(std::floor(x), std::floor(y)), color, value.c_str());
    }
    float Width(float size, const std::string& value) const { return text->CalcTextSizeA(size * s, 1.0e6f, 0.0f, value.c_str()).x; }
    float TitleWidth(float size, const std::string& value) const { return title->CalcTextSizeA(size * s, 1.0e6f, 0.0f, value.c_str()).x; }
    void Right(float x, float y, float size, ImU32 color, const std::string& value) const { Label(x - Width(size, value), y, size, color, value); }
    void Center(float x, float y, float size, ImU32 color, const std::string& value) const { Label(x - Width(size, value) * 0.5f, y, size, color, value); }

    bool Hover(float x, float y, float w, float h) const { return in.cursor && in.mouseX >= x && in.mouseX <= x + w && in.mouseY >= y && in.mouseY <= y + h; }

    // Panel con esquinas cortadas, cabecera rayada y un destello que recorre el borde.
    void Panel(float x, float y, float w, float h, const char* heading, const char* code) const {
        const float cut = 12.0f * s;
        const ImVec2 p[6] = {{x + cut, y}, {x + w, y}, {x + w, y + h - cut}, {x + w - cut, y + h}, {x, y + h}, {x, y + cut}};
        d->AddConvexPolyFilled(p, 6, kPanel);
        d->AddPolyline(p, 6, Rgba(255, 150, 40, 0.10f), 5.0f * s, ImDrawFlags_Closed);
        d->AddPolyline(p, 6, kAmberDim, 1.0f * s, ImDrawFlags_Closed);

        // Remates brillantes en las dos esquinas limpias.
        const float arm = 22.0f * s;
        d->AddLine(ImVec2(x + w - arm, y), ImVec2(x + w, y), kAmber, 2.0f * s);
        d->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + arm), kAmber, 2.0f * s);
        d->AddLine(ImVec2(x, y + h - arm), ImVec2(x, y + h), kAmber, 2.0f * s);
        d->AddLine(ImVec2(x, y + h), ImVec2(x + arm, y + h), kAmber, 2.0f * s);

        // Una lectura que recorre el borde superior.
        const float sweep = static_cast<float>(std::fmod(t * 0.22, 1.0));
        const float head = x + cut + (w - cut - 40.0f * s) * sweep;
        d->AddRectFilled(ImVec2(head, y), ImVec2(head + 40.0f * s, y + 1.5f * s), kYellow);

        if (!heading) return;
        const float hy = y + 7.0f * s;
        Spaced(x + 16.0f * s, hy, 14.0f, kYellow, heading);
        const float start = x + 22.0f * s + SpacedWidth(14.0f, heading);
        const float end = x + w - 12.0f * s - Width(10.5f, code);
        for (float k = start; k < end - 4.0f * s; k += 5.0f * s) d->AddLine(ImVec2(k, hy + 11.0f * s), ImVec2(k + 3.0f * s, hy + 4.0f * s), kAmberFaint, 1.0f);
        Right(x + w - 12.0f * s, hy + 3.0f * s, 10.5f, kMuted, code);
    }

    // Barra de segmentos inclinados.
    void Segments(float x, float y, float w, float h, double value, ImU32 color, int count = 26) const {
        const float slant = h * 0.7f, gap = 2.0f * s;
        const float step = (w - slant) / static_cast<float>(count);
        const int on = static_cast<int>(std::ceil(std::clamp(value, 0.0, 1.0) * count));
        for (int i = 0; i < count; ++i) {
            const float a = x + step * static_cast<float>(i);
            const ImU32 c = i < on ? color : kAmberFaint;
            d->AddQuadFilled(ImVec2(a + slant, y), ImVec2(a + slant + step - gap, y), ImVec2(a + step - gap, y + h), ImVec2(a, y + h), c);
        }
    }

    void Meter(float x, float y, float w, const char* name, double value, ImU32 color, const std::string& reading) const {
        Label(x, y, 12.5f, kMuted, name);
        Right(x + w, y, 12.5f, kText, reading);
        Segments(x, y + 16.0f * s, w, 7.0f * s, value, color);
    }

    bool Button(float x, float y, float w, float h, const std::string& caption, const char* key, HudAction action, bool enabled = true, bool active = false) const {
        const bool hover = enabled && Hover(x, y, w, h);
        const float cut = 6.0f * s;
        const ImVec2 p[6] = {{x + cut, y}, {x + w, y}, {x + w, y + h - cut}, {x + w - cut, y + h}, {x, y + h}, {x, y + cut}};
        const ImU32 fill = active ? Rgba(255, 170, 40, 0.34f) : hover ? Rgba(255, 170, 40, 0.24f) : Rgba(255, 150, 40, 0.07f);
        d->AddConvexPolyFilled(p, 6, fill);
        d->AddPolyline(p, 6, !enabled ? kAmberFaint : hover || active ? kYellow : kAmberDim, 1.0f * s, ImDrawFlags_Closed);
        const float size = 13.0f;
        const ImU32 color = !enabled ? kMuted : hover || active ? kYellow : kText;
        const float textY = y + (h - size * s * 1.15f) * 0.5f;
        if (key) {
            Label(x + 9.0f * s, textY, size, color, caption);
            Right(x + w - 8.0f * s, textY + 1.0f * s, 11.0f, kMuted, key);
        } else {
            Center(x + w * 0.5f, textY, size, color, caption);
        }
        if (hover && in.clicked) {
            actions.push_back(action);
            return true;
        }
        return false;
    }

    // Arco de radianes a a b, con grosor.
    void Arc(float cx, float cy, float r, float a, float b, ImU32 color, float thick) const {
        d->PathClear();
        d->PathArcTo(ImVec2(cx, cy), r, a, b, 48);
        d->PathStroke(color, 0, thick);
    }

    void Key(float x, float y, const char* key, const std::string& caption, float* width) const {
        const float keyW = std::max(Width(12.5f, key) + 12.0f * s, 22.0f * s);
        const float h = 20.0f * s;
        d->AddRectFilled(ImVec2(x, y), ImVec2(x + keyW, y + h), Rgba(255, 170, 40, 0.20f), 3.0f * s);
        d->AddRect(ImVec2(x, y), ImVec2(x + keyW, y + h), kAmber, 3.0f * s);
        d->AddLine(ImVec2(x + 3.0f * s, y + h - 3.0f * s), ImVec2(x + keyW - 3.0f * s, y + h - 3.0f * s), kAmberDim);
        Center(x + keyW * 0.5f, y + 2.5f * s, 12.5f, kYellow, key);
        Label(x + keyW + 6.0f * s, y + 2.5f * s, 12.5f, kText, caption);
        *width = keyW + 6.0f * s + Width(12.5f, caption) + 16.0f * s;
    }
};

// Lo alienigena se escribe con su propia letra, rota como una senal que llega mal.
bool Alien(const Contact& c) { return c.identified && c.target.kind == Kind::Anomaly; }
ImFont* NameFont(const Ui& u, const Contact& c) { return Alien(c) ? u.alien : u.text; }

ImU32 ContactColor(const Contact& c) {
    if (!c.identified) return kUnknown;
    if (c.target.kind == Kind::Station) return kOwn;
    if (c.stance == Stance::Hostile) return kHostile;
    if (c.stance == Stance::Neutral) return kNeutral;
    if (c.target.kind == Kind::Anomaly) return kVoid;
    return kText;
}

// Reloj de velocidad a escala logaritmica: 1 km/s, c y mas alla.
void SpeedGauge(const Ui& u, const GameSession& g, float cx, float cy, float r) {
    const ShipState& ship = g.Ship();
    const float s = u.s, start = kTau * 0.375f, sweep = kTau * 0.75f;
    const auto fraction = [](double v) { return static_cast<float>(std::clamp(std::log10(std::max(v, 1.0)) / 9.0, 0.0, 1.0)); };

    u.d->AddCircleFilled(ImVec2(cx, cy), r + 8.0f * s, kPanelDeep, 64);
    u.Arc(cx, cy, r, start, start + sweep, kAmberFaint, 7.0f * s);
    u.Arc(cx, cy, r, start, start + sweep * fraction(ship.speed), ship.speed >= kLightSpeed ? kVoid : kAmber, 7.0f * s);
    for (int i = 0; i <= 18; ++i) {
        const float a = start + sweep * static_cast<float>(i) / 18.0f;
        const float inner = r - (i % 2 == 0 ? 13.0f : 9.0f) * s;
        u.d->AddLine(ImVec2(cx + std::cos(a) * inner, cy + std::sin(a) * inner), ImVec2(cx + std::cos(a) * (r - 5.0f * s), cy + std::sin(a) * (r - 5.0f * s)), kAmberDim);
    }
    // Marca de la velocidad de la luz: el crucero se queda justo antes.
    const float c = start + sweep * fraction(kLightSpeed);
    u.d->AddLine(ImVec2(cx + std::cos(c) * (r - 12.0f * s), cy + std::sin(c) * (r - 12.0f * s)), ImVec2(cx + std::cos(c) * (r + 8.0f * s), cy + std::sin(c) * (r + 8.0f * s)), kYellow, 2.0f * s);
    u.Label(cx + std::cos(c) * (r + 10.0f * s) - 2.0f * s, cy + std::sin(c) * (r + 10.0f * s) - 9.0f * s, 11.0f, kYellow, "LUZ");

    // Mando por dentro.
    u.Arc(cx, cy, r - 18.0f * s, start, start + sweep, kAmberFaint, 3.0f * s);
    u.Arc(cx, cy, r - 18.0f * s, start, start + sweep * static_cast<float>(ship.throttle), kYellow, 3.0f * s);

    // Giro lento de decoracion: sensacion de instrumento vivo.
    const float spin = static_cast<float>(u.t * 0.4);
    u.Arc(cx, cy, r - 26.0f * s, spin, spin + 1.0f, kAmberDim, 1.0f);
    u.Arc(cx, cy, r - 26.0f * s, spin + 3.14f, spin + 4.14f, kAmberDim, 1.0f);

    u.Center(cx, cy - 26.0f * s, 12.5f, kMuted, Name(ship.mode));
    u.Center(cx, cy - 9.0f * s, 16.0f, kYellow, Speed(ship.speed));
    u.Center(cx, cy + 12.0f * s, 11.5f, kMuted, Format("MANDO %.0f %%", ship.throttle * 100.0));
    const double gamma = g.ExteriorFactor();
    if (gamma > 1.01) u.Center(cx, cy + 27.0f * s, 11.5f, kOrange, Format("EXTERIOR x%.1f", gamma));
    else if (ship.warpCharge > 0.0 && ship.warpCharge < 1.0) u.Center(cx, cy + 27.0f * s, 11.5f, kOrange, Format("BURBUJA %.0f %%", ship.warpCharge * 100.0));
}

void ShipPanel(const Ui& u, const GameSession& g, float x, float y) {
    const ShipState& ship = g.Ship();
    const Balance& rules = g.Rules();
    const float s = u.s, w = kLeftWidth * s, h = kShipPanelHeight * s;
    u.Panel(x, y, w, h, "NAVE", g.Creative() ? "CREATIVO  EPS-07" : "EPS-07");
    SpeedGauge(u, g, x + 92.0f * s, y + 104.0f * s, 68.0f * s);

    const float mx = x + 182.0f * s, mw = w - 198.0f * s;
    u.Meter(mx, y + 44.0f * s, mw, "ENERGÍA", ship.energy / rules.ship.energy, ship.energy < rules.ship.energy * 0.2 ? kHostile : kYellow, EnergyText(ship.energy));
    // Hibrida no existe hasta sacar el emisor de la estructura; Hiper, hasta tener el segundo.
    const bool curves = HybridUnlocked(ship), hyper = HyperUnlocked(ship, rules.drive);
    const DriveMode curve = hyper ? DriveMode::Hyper : DriveMode::Warp;
    u.Meter(mx, y + 86.0f * s, mw, "ANTIMATERIA", ship.antimatter / rules.ship.antimatterTank, !curves || CanCurve(ship, curve, rules.drive) ? kVoid : kHostile, MassText(ship.antimatter));
    u.Meter(mx, y + 128.0f * s, mw, "CASCO", ship.hull / rules.ship.hull, ship.hull < rules.ship.hull * 0.3 ? kHostile : kAmber, Format("%.0f", ship.hull));

    // Cada boton, a la medida de su nombre: la letra es ancha.
    const float by = y + 184.0f * s, gap = 4.0f * s, bh = 30.0f * s;
    const float wide = 80.0f * s, widest = 104.0f * s, narrow = w - 32.0f * s - gap * 3.0f - wide * 2.0f - widest;
    float bx = x + 16.0f * s;
    if (hyper) {
        u.Button(bx, by, wide, bh, "IMPULSO", nullptr, {HudActionType::Drive, 0}, true, ship.mode == DriveMode::Impulse);
        u.Button(bx += wide + gap, by, wide, bh, "CRUCERO", nullptr, {HudActionType::Drive, 1}, true, ship.mode == DriveMode::Cruise);
        u.Button(bx += wide + gap, by, widest, bh, "HÍBRIDA", nullptr, {HudActionType::Drive, 2}, CanCurve(ship, DriveMode::Warp, rules.drive), ship.mode == DriveMode::Warp);
        u.Button(bx += widest + gap, by, narrow, bh, "HIPER", nullptr, {HudActionType::Drive, 3}, true, ship.mode == DriveMode::Hyper);
    } else if (curves) {
        const float third = (w - 32.0f * s - gap * 2.0f) / 3.0f;
        u.Button(bx, by, third, bh, "IMPULSO", nullptr, {HudActionType::Drive, 0}, true, ship.mode == DriveMode::Impulse);
        u.Button(bx += third + gap, by, third, bh, "CRUCERO", nullptr, {HudActionType::Drive, 1}, true, ship.mode == DriveMode::Cruise);
        u.Button(bx += third + gap, by, third, bh, "HÍBRIDA", nullptr, {HudActionType::Drive, 2}, CanCurve(ship, DriveMode::Warp, rules.drive), ship.mode == DriveMode::Warp);
    } else {
        const float half = (w - 32.0f * s - gap) * 0.5f;
        u.Button(bx, by, half, bh, "IMPULSO", nullptr, {HudActionType::Drive, 0}, true, ship.mode == DriveMode::Impulse);
        u.Button(bx + half + gap, by, half, bh, "CRUCERO", nullptr, {HudActionType::Drive, 1}, true, ship.mode == DriveMode::Cruise);
    }

    // Lo caro es formar la burbuja; una vez estable, se ve lo poco que cuesta sostenerla.
    const float line = by + 38.0f * s;
    const bool stable = Curves(ship.mode) && ship.warpCharge >= 1.0 && ship.warpSpeed > rules.drive.impulseSpeed;
    if (curves) {
        const std::string bubble = stable ? "Mantener " + PowerText(MaintenancePower(ship, rules.drive) * 3600.0) + Format("  1:%.0f mil", MaintenanceRatio(ship, rules.drive) / 1000.0)
                                          : "Formar burbuja " + EnergyText(FormationEnergy(ship, curve, rules.drive));
        u.Label(x + 16.0f * s, line, 13.0f, stable ? kVoid : kMuted, bubble);
        u.Right(x + w - 16.0f * s, line, 13.0f, kVoid, hyper ? Format("Hiper %.0f c", HyperFactor(ship, rules.drive)) : Format("Híbrida %.0f c", rules.drive.warpFactor));
    }
    u.Label(x + 16.0f * s, line + 17.0f * s, 12.5f, kMuted, Format("Blindaje %d   Maniobra %d   Escáner %d", ship.heatShieldTier, ship.handlingTier, ship.scannerTier));
    u.Right(x + w - 16.0f * s, line + 17.0f * s, 12.5f, kMuted, ModeInfo(g.Mode()).name);
    u.Label(x + 16.0f * s, line + 34.0f * s, 12.5f, kMuted, Format("Vista %s", Distance(g.Visibility()).c_str()));
    u.Right(x + w - 16.0f * s, line + 34.0f * s, 12.5f, kMuted, Format("Día %.2f   nave %.2f", g.SimTime() / 86400.0, ship.shipHours / 24.0));
    u.Button(x + 16.0f * s, line + 55.0f * s, w - 32.0f * s, 26.0f * s, g.Scanning() ? "BARRIENDO..." : std::string("ESCÁNER  -") + EnergyText(rules.scanner.energy), nullptr, {HudActionType::Scan}, g.CanScan() && !g.Scanning());

    // Defensa: el campo encendido (K cambia), las placas de proa y el calor con sus refrigerantes.
    const DefenseState& d = ship.defense;
    const float dy = line + 90.0f * s, half = (w - 32.0f * s - 12.0f * s) * 0.5f;
    const std::size_t f = static_cast<std::size_t>(d.field);
    const double fieldCap = ShieldCapacity(rules.defense, d.field, d.shieldTier[f]);
    const bool active = g.FieldActive();
    const std::string fieldName = !d.fieldOn ? "ESCUDO" : d.field == ShieldKind::Dust ? "ARENA" : d.field == ShieldKind::Plasma ? "PLASMA" : d.field == ShieldKind::EM ? "EM" : "AMOR";
    const std::string fieldReading = !d.fieldOn ? std::string("APAGADO") : !active ? std::string(g.Overheated() ? "CALIENTE" : d.field == ShieldKind::Dust ? "SIN POLVO" : "SIN ENERGÍA") : d.field == ShieldKind::Love ? std::string("TODO") : Format("%.0f", d.charge[f]);
    u.Meter(x + 16.0f * s, dy, half, fieldName.c_str(), d.fieldOn && fieldCap > 0.0 ? d.charge[f] / fieldCap : 0.0, active ? kVoid : kHostile, fieldReading);
    const std::size_t a = static_cast<std::size_t>(ShieldKind::Ablative);
    const double plateCap = ShieldCapacity(rules.defense, ShieldKind::Ablative, d.shieldTier[a]);
    u.Meter(x + 28.0f * s + half, dy, half, "CONOS DE PROA", plateCap > 0.0 ? d.charge[a] / plateCap : 0.0, g.Ramming() && d.charge[a] <= 0.0 ? kHostile : kAmber, plateCap > 0.0 ? Format("%.0f", d.charge[a]) : std::string("—"));
    const double heat = d.heat / rules.thermal.limit;
    const ImU32 heatColor = heat > 1.0 ? kHostile : heat > rules.thermal.coolStart ? kOrange : kNeutral;
    u.Meter(x + 16.0f * s, dy + 40.0f * s, w - 32.0f * s, "CALOR", heat, heatColor,
            Format("%.0f %%   agua %.0f  N2 %.0f  rad %.0f %%", heat * 100.0, d.water, d.nitrogen, g.RadiatorEfficiency() * 100.0));
}

void CargoPanel(const Ui& u, const GameSession& g, float x, float y) {
    const float s = u.s, w = kLeftWidth * s;
    const int shown = std::min(static_cast<int>(g.Stations().All().size()), 5);
    const float h = (146.0f + 18.0f * static_cast<float>(shown)) * s;
    u.Panel(x, y, w, h, "BODEGA", (MassText(g.Ship().cargo.Total()) + " / " + MassText(CargoCapacity(g.Ship(), g.Rules().ship))).c_str());
    const float ix = x + 16.0f * s, iw = w - 32.0f * s;
    float line = y + 32.0f * s;
    const float half = iw * 0.5f;
    for (int r = 0; r < static_cast<int>(Resource::Count); ++r) {
        const Resource resource = static_cast<Resource>(r);
        const float rx = ix + half * static_cast<float>(r % 2);
        u.Label(rx, line, 13.0f, kMuted, Name(resource));
        u.Right(rx + half - 10.0f * s, line, 13.0f, kText, MassText(g.Ship().cargo.Get(resource)));
        if (r % 2 == 1) line += 18.0f * s;
    }
    // Los emisores de energia negativa van montados en el motor, no en la bodega.
    {
        const float rx = ix + half;
        u.Label(rx, line, 13.0f, kVoid, "Emisores E<0");
        u.Right(rx + half - 10.0f * s, line, 13.0f, kText, Format("%d", g.Ship().emitters));
        line += 18.0f * s;
    }
    line += 8.0f * s;
    const bool freeMine = g.Stations().NextIsFree(StationKind::Mining), freePower = g.Stations().NextIsFree(StationKind::Power);
    u.Label(ix, line, 13.0f, kYellow, Format("ESTACIONES %d", static_cast<int>(g.Stations().All().size())));
    u.Right(ix + iw, line + 1.0f * s, 11.5f, freeMine || freePower ? kNeutral : kMuted, freeMine && freePower ? "Mina y central gratis" : freeMine ? "Mina gratis" : freePower ? "Central gratis" : "Con coste");
    line += 18.0f * s;
    int count = 0;
    for (const Station& st : g.Stations().All()) {
        if (++count > 5) break;
        const bool mining = st.kind == StationKind::Mining;
        const double fill = mining ? st.stored.Total() / g.Stations().OreCapacity(st) : st.storedAntimatter / g.Stations().AntimatterCapacity(st);
        u.Label(ix, line, 12.0f, kMuted, mining ? "MINA" : "CENTRAL");
        u.Segments(ix + 66.0f * s, line + 4.0f * s, iw - 120.0f * s, 6.0f * s, fill, mining ? kAmber : kYellow, 18);
        u.Right(ix + iw, line, 12.0f, kText, MassText(mining ? st.stored.Total() : st.storedAntimatter));
        line += 17.0f * s;
    }
}

// Radar en 3D: un disco visto en perspectiva con cinco capas de altura, en los ejes de la nave
// (delante es el fondo del disco, arriba es lo que la nave tiene encima). Cada senal va en su capa
// con un tallo hasta el plano de la nave. Distancia en escala logaritmica: cuatro decadas hasta el
// alcance elegido; mas lejos queda en el borde, hueca. Con el cursor se elige una senal pinchandola.
constexpr float kRadarWidth = 420.0f, kRadarHeight = 214.0f;

void Radar3D(const Ui& u, const GameSession& g, const TargetRef* target, float x, float y) {
    const float s = u.s, w = kRadarWidth * s, h = kRadarHeight * s;
    const int index = std::clamp(u.in.radarRange, 0, kRadarRangeCount - 1);
    const double top = kRadarRanges[index], low = top * 1.0e-4;
    u.Panel(x, y, w, h, "RADAR 3D", ("ALCANCE " + Distance(top)).c_str());

    // Alcance: menos y mas.
    const float bw = 24.0f * s, bh = 20.0f * s, by = y + 30.0f * s;
    u.Button(x + w - 14.0f * s - bw * 2.0f - 4.0f * s, by, bw, bh, "-", nullptr, {HudActionType::RadarRange, -1}, index > 0);
    u.Button(x + w - 14.0f * s - bw, by, bw, bh, "+", nullptr, {HudActionType::RadarRange, 1}, index < kRadarRangeCount - 1);

    const float cx = x + w * 0.46f, cy = y + h * 0.60f;
    const float rx = w * 0.36f, ry = rx * 0.30f, gap = h * 0.14f;
    const auto ellipse = [&](float ey, float scale, ImU32 color, float thick) {
        u.d->AddEllipse(ImVec2(cx, ey), ImVec2(rx * scale, ry * scale), color, 0.0f, 64, thick);
    };
    // Las capas, de abajo arriba. La del medio es el plano de la nave, con su reticula.
    static const char* const kLayers[] = {"-2", "-1", "0", "+1", "+2"};
    for (int k = -2; k <= 2; ++k) {
        const float ey = cy - static_cast<float>(k) * gap;
        if (k == 0) {
            u.d->AddEllipseFilled(ImVec2(cx, ey), ImVec2(rx, ry), kPanelDeep, 0.0f, 64);
            for (int i = 1; i <= 3; ++i) ellipse(ey, static_cast<float>(i) / 3.0f, kAmberDim, 1.0f);
            u.d->AddLine(ImVec2(cx - rx, ey), ImVec2(cx + rx, ey), kAmberFaint);
            u.d->AddLine(ImVec2(cx, ey - ry), ImVec2(cx, ey + ry), kAmberFaint);
            // Lo que tiene delante: una cuna abierta hacia el fondo.
            u.d->AddLine(ImVec2(cx, ey), ImVec2(cx - rx * 0.5f, ey - ry * 0.87f), kAmberDim);
            u.d->AddLine(ImVec2(cx, ey), ImVec2(cx + rx * 0.5f, ey - ry * 0.87f), kAmberDim);
        } else {
            ellipse(ey, 1.0f, Rgba(255, 150, 40, 0.16f + 0.04f * static_cast<float>(2 - std::abs(k))), 1.0f);
        }
        u.Label(cx + rx + 10.0f * s, ey - 7.0f * s, 11.0f, k == 0 ? kYellow : kMuted, kLayers[k + 2]);
    }
    u.d->AddLine(ImVec2(cx - rx, cy - gap * 2.0f), ImVec2(cx - rx, cy + gap * 2.0f), kAmberFaint);
    u.d->AddLine(ImVec2(cx + rx, cy - gap * 2.0f), ImVec2(cx + rx, cy + gap * 2.0f), kAmberFaint);
    u.Label(cx + rx + 36.0f * s, cy - gap * 2.0f - 6.0f * s, 10.0f, kMuted, "ENCIMA");
    u.Label(cx + rx + 36.0f * s, cy + gap * 2.0f - 6.0f * s, 10.0f, kMuted, "DEBAJO");

    // Barrido por el plano.
    const float sweep = static_cast<float>(std::fmod(u.t * (g.Scanning() ? 4.0 : 1.0), static_cast<double>(kTau)));
    for (int i = 0; i < 16; ++i) {
        const float a = sweep - static_cast<float>(i) * 0.05f;
        u.d->AddLine(ImVec2(cx, cy), ImVec2(cx + std::cos(a) * rx, cy + std::sin(a) * ry), Rgba(255, 170, 40, 0.16f * (1.0f - static_cast<float>(i) / 16.0f)));
    }

    const ShipState& ship = g.Ship();
    const Mat4d toShip = ship.orientation.Conjugate().ToMatrix();
    int hovered = -1;
    float hoverX = 0, hoverY = 0, best = 9.0f * s;
    struct Blip { float x, y, foot; int layer; bool beyond; ImU32 color; bool selected; };
    std::vector<Blip> blips;
    for (std::size_t i = 0; i < g.Contacts().size(); ++i) {
        const Contact& k = g.Contacts()[i];
        // En la nave arriba es -Y: el puente esta encima.
        const Vec3d rel = toShip.TransformDirection(k.position - ship.position);
        const double flat = std::sqrt(rel.x * rel.x + rel.z * rel.z), distance = std::max(Length(rel), 1e-3);
        const double elevation = std::atan2(-rel.y, flat) * 180.0 / 3.14159265358979;
        const int layer = std::clamp(static_cast<int>(std::lround(elevation / 36.0)), -2, 2);
        const bool beyond = distance > top;
        const float reach = static_cast<float>(std::clamp(std::log10(distance / low) / 4.0, 0.04, 1.0));
        const double nx = flat > 1e-9 ? rel.x / flat : 0.0, nz = flat > 1e-9 ? rel.z / flat : 0.0;
        const float px = cx + static_cast<float>(nx) * reach * rx, foot = cy - static_cast<float>(nz) * reach * ry;
        const float py = foot - static_cast<float>(layer) * gap;
        const bool selected = target && k.target == *target;
        blips.push_back({px, py, foot, layer, beyond, selected ? kYellow : ContactColor(k), selected});
        const float dx = u.in.mouseX - px, dy = u.in.mouseY - py, near = std::sqrt(dx * dx + dy * dy);
        if (u.in.cursor && near < best) {
            best = near;
            hovered = static_cast<int>(i);
            hoverX = px;
            hoverY = py;
        }
    }
    for (const Blip& b : blips) {
        if (b.layer != 0) u.d->AddLine(ImVec2(b.x, b.foot), ImVec2(b.x, b.y), (b.color & 0x00FFFFFFu) | 0x90000000u, 1.0f * s);
        u.d->AddCircleFilled(ImVec2(b.x, b.foot), 1.4f * s, (b.color & 0x00FFFFFFu) | 0x70000000u);
        const float r = (b.selected ? 4.2f : 3.0f) * s;
        // Encima, un triangulo hacia arriba; debajo, hacia abajo; en el plano, un rombo.
        if (b.layer > 0) {
            if (b.beyond) u.d->AddTriangle(ImVec2(b.x, b.y - r), ImVec2(b.x - r, b.y + r * 0.7f), ImVec2(b.x + r, b.y + r * 0.7f), b.color, 1.2f * s);
            else u.d->AddTriangleFilled(ImVec2(b.x, b.y - r), ImVec2(b.x - r, b.y + r * 0.7f), ImVec2(b.x + r, b.y + r * 0.7f), b.color);
        } else if (b.layer < 0) {
            if (b.beyond) u.d->AddTriangle(ImVec2(b.x, b.y + r), ImVec2(b.x - r, b.y - r * 0.7f), ImVec2(b.x + r, b.y - r * 0.7f), b.color, 1.2f * s);
            else u.d->AddTriangleFilled(ImVec2(b.x, b.y + r), ImVec2(b.x - r, b.y - r * 0.7f), ImVec2(b.x + r, b.y - r * 0.7f), b.color);
        } else {
            const ImVec2 q[4] = {{b.x, b.y - r}, {b.x + r, b.y}, {b.x, b.y + r}, {b.x - r, b.y}};
            if (b.beyond) u.d->AddPolyline(q, 4, b.color, ImDrawFlags_Closed, 1.2f * s);
            else u.d->AddConvexPolyFilled(q, 4, b.color);
        }
        if (b.selected) u.d->AddCircle(ImVec2(b.x, b.y), 8.0f * s, kYellow, 24, 1.2f * s);
    }
    // La nave, en el centro, apuntando al fondo.
    u.d->AddTriangleFilled(ImVec2(cx, cy - 7.0f * s), ImVec2(cx - 5.0f * s, cy + 4.0f * s), ImVec2(cx + 5.0f * s, cy + 4.0f * s), kYellow);

    if (hovered >= 0) {
        const Contact& k = g.Contacts()[static_cast<std::size_t>(hovered)];
        u.d->AddCircle(ImVec2(hoverX, hoverY), 9.0f * s, kText, 24, 1.0f * s);
        const std::string text = k.name + "  " + Distance(k.distance);
        const float tw = u.WidthIn(NameFont(u, k), 12.0f, text) + 12.0f * s;
        const float tx = std::min(hoverX + 12.0f * s, x + w - tw - 6.0f * s), ty = hoverY - 24.0f * s;
        u.d->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + 18.0f * s), kPanelDeep);
        u.d->AddRect(ImVec2(tx, ty), ImVec2(tx + tw, ty + 18.0f * s), kAmberDim);
        u.LabelIn(NameFont(u, k), tx + 6.0f * s, ty + 2.0f * s, 12.0f, ContactColor(k), text);
        if (u.in.clicked) u.actions.push_back({HudActionType::SelectContact, hovered});
    }
    u.Label(x + 16.0f * s, y + h - 22.0f * s, 10.5f, kMuted, u.in.cursor ? "Pincha una señal para fijarla" : "C: cursor para elegir señales");
}

void ContactList(const Ui& u, const GameSession& g, const TargetRef* target, float x, float y) {
    const float s = u.s, w = kRightWidth * s;
    const std::size_t rows = std::min<std::size_t>(g.Contacts().size(), 9);
    const float h = 40.0f * s + 18.0f * s * static_cast<float>(rows);
    u.Panel(x, y, w, h, "SENSORES", Format("%zu SEÑALES", g.Contacts().size()).c_str());

    const float ix = x + 14.0f * s, iw = w - 28.0f * s;
    float line = y + 36.0f * s;
    for (std::size_t i = 0; i < rows; ++i) {
        const Contact& k = g.Contacts()[i];
        const bool selected = target && k.target == *target;
        const float rowH = 18.0f * s;
        const bool hover = u.Hover(x + 4.0f * s, line - 1.0f * s, w - 8.0f * s, rowH);
        if (selected || hover) u.d->AddRectFilled(ImVec2(x + 4.0f * s, line - 1.0f * s), ImVec2(x + w - 4.0f * s, line + rowH - 2.0f * s), Rgba(255, 170, 40, selected ? 0.22f : 0.12f));
        if (hover && u.in.clicked) u.actions.push_back({HudActionType::SelectContact, static_cast<int>(i)});
        u.d->AddQuadFilled(ImVec2(ix + 4.0f * s, line + 3.0f * s), ImVec2(ix + 8.0f * s, line + 7.0f * s), ImVec2(ix + 4.0f * s, line + 11.0f * s), ImVec2(ix, line + 7.0f * s), ContactColor(k));
        u.LabelIn(NameFont(u, k), ix + 14.0f * s, line, 13.0f, selected ? kYellow : ContactColor(k), k.name);
        u.Right(ix + iw, line + 1.0f * s, 12.0f, kMuted, Distance(k.distance));
        line += rowH;
    }
}

// Lo que se puede destruir: una nave o un bastion. Las estructuras no tienen casco.
const Anomaly* Attackable(const GameSession& g, const TargetRef* target) {
    if (!target || target->kind != Kind::Anomaly) return nullptr;
    for (const Anomaly& a : g.Anomalies())
        if (a.id == target->index && a.maxHull > 0.0) return &a;
    return nullptr;
}

// Lo que le falta a un arma para disparar a ese blanco. Vacio = esta lista.
std::string WeaponState(const GameSession& g, WeaponKind kind, double distance, double aimError) {
    const WeaponRules spec = Scaled(g.Rules().combat, kind, g.WeaponTier(kind));
    const ShipState& ship = g.Ship();
    if (!g.WeaponReady(kind)) return Format("%.0f s", std::ceil(g.WeaponCooldown(kind)));
    if (distance > spec.range) return "LEJOS";
    if (ship.energy < spec.energy) return "SIN ENERGÍA";
    if (ship.antimatter < spec.antimatter) return "SIN ANTIMATERIA";
    if (ship.cargo.Get(Resource::Metal) < spec.metal) return "SIN METAL";
    if (kind == WeaponKind::Spinal && aimError * distance > spec.proximity) return Format("GIRA %.1f°", aimError * 180.0 / 3.14159265358979);
    return "";
}

void TargetPanel(const Ui& u, const GameSession& g, const TargetRef& target, float x, float y) {
    const std::optional<TargetInfo> info = g.Resolve(target);
    if (!info) return;
    const float s = u.s, w = kRightWidth * s;
    const Anomaly* enemy = Attackable(g, &target);
    std::vector<std::string> lines;
    const Station* station = target.kind == Kind::Station ? g.Stations().Find(target.index) : nullptr;
    if (station) {
        if (station->kind == StationKind::Mining) lines.push_back("Extrae " + MassRateText(g.Stations().MiningPerHour(*station)) + "   guarda " + MassText(g.Stations().OreCapacity(*station)));
        else lines.push_back("Capta " + PowerText(g.Stations().EnergyPerHour(*station)) + "   antimateria " + MassRateText(g.Stations().AntimatterPerHour(*station)));
        if (station->kind == StationKind::Power) lines.push_back("Guarda " + MassText(g.Stations().AntimatterCapacity(*station)) + "   materia " + MassRateText(g.Stations().MatterPerHour(*station)));
        std::string modules = "Módulos:";
        for (const Module& m : station->modules) modules += Format(" %s %d", Name(m.kind), m.level);
        if (station->modules.empty()) modules += " ninguno";
        lines.push_back(modules);
    }
    const bool astroTarget = target.kind == Kind::Star || target.kind == Kind::Planet || target.kind == Kind::Belt;
    const float h = (118.0f + 16.0f * static_cast<float>(lines.size()) + (astroTarget ? 36.0f : 0.0f) + (enemy ? 168.0f : 0.0f)) * s;
    u.Panel(x, y, w, h, "OBJETIVO", Distance(Length(info->position - g.Ship().position)).c_str());
    const float ix = x + 16.0f * s;
    const bool known = g.Scanned(target);
    u.LabelIn(target.kind == Kind::Anomaly ? u.alien : u.text, ix, y + 30.0f * s, 16.0f, kYellow, known || target.kind == Kind::Star ? info->name : std::string("Señal sin identificar"));
    u.Label(ix, y + 50.0f * s, 12.5f, known ? kText : kOrange, known ? info->detail : std::string("Sin datos: usa el escáner"));
    float line = y + 67.0f * s;
    for (const std::string& text : lines) {
        u.Label(ix, line, 12.0f, kMuted, text);
        line += 16.0f * s;
    }
    if (enemy) {
        const float iw = w - 32.0f * s;
        // Su escudo, y debajo su casco. Cada boton dice que parte de esa arma lo atraviesa.
        const bool shielded = enemy->shieldMax > 0.0 && enemy->shieldCharge > 0.5;
        u.Label(ix, line, 12.0f, kMuted, enemy->shieldMax > 0.0 ? Format("Escudo %s", Name(enemy->shield)) : std::string("Sin escudo"));
        if (enemy->shieldMax > 0.0) u.Right(ix + iw, line, 12.0f, kText, Format("%.0f / %.0f", enemy->shieldCharge, enemy->shieldMax));
        line += 16.0f * s;
        if (enemy->shieldMax > 0.0) u.Segments(ix, line + 2.0f * s, iw, 5.0f * s, enemy->shieldCharge / enemy->shieldMax, kVoid);
        line += 12.0f * s;
        u.Segments(ix, line + 2.0f * s, iw, 7.0f * s, enemy->hull / enemy->maxHull, enemy->stance == Stance::Hostile ? kHostile : kNeutral);
        line += 16.0f * s;
        const double distance = Length(info->position - g.Ship().position);
        const std::optional<FiringSolution> solution = g.Solution(target, WeaponKind::Spinal);
        const double aimError = solution ? std::acos(std::clamp(Dot(game::Heading(g.Ship()), solution->direction), -1.0, 1.0)) : 3.14159265358979;
        for (int k = 0; k < static_cast<int>(WeaponKind::Count); ++k) {
            const WeaponKind kind = static_cast<WeaponKind>(k);
            const std::string state = WeaponState(g, kind, distance, aimError);
            const DamageType type = g.Rules().combat.weapons[k].type;
            const double pass = 1.0 - g.Rules().defense.shields[static_cast<int>(enemy->shield)].absorb[static_cast<int>(type)];
            const std::string caption = shielded ? Format("%s  N%d   pasa %.0f %%", Name(kind), g.WeaponTier(kind), pass * 100.0) : Format("%s  N%d", Name(kind), g.WeaponTier(kind));
            u.Button(ix, line, iw, 24.0f * s, caption, state.empty() ? "LISTA" : state.c_str(), {HudActionType::Fire, k}, state.empty());
            line += 28.0f * s;
        }
    }

    if (astroTarget) u.Button(ix, y + h - 76.0f * s, w - 32.0f * s, 30.0f * s, "ANÁLISIS", nullptr, {HudActionType::Analyze});
    const float by = y + h - 40.0f * s, bh = 30.0f * s, bw = (w - 32.0f * s - 8.0f * s) / 2.0f;
    u.Button(ix, by, bw, bh, g.Autopilot() ? "PARAR" : "IR", nullptr, {g.Autopilot() ? HudActionType::Stop : HudActionType::Go});
    if (target.kind == Kind::Planet || target.kind == Kind::Belt) u.Button(ix + bw + 8.0f * s, by, bw, bh, "MINA", nullptr, {HudActionType::Mine});
    else if (target.kind == Kind::Star) u.Button(ix + bw + 8.0f * s, by, bw, bh, "CENTRAL", nullptr, {HudActionType::Power});
    else if (station) {
        u.Button(ix + bw + 8.0f * s, by, bw * 0.5f - 2.0f * s, bh, "RECOGER", nullptr, {HudActionType::Use});
        u.Button(ix + bw * 1.5f + 10.0f * s, by, bw * 0.5f - 2.0f * s, bh, "MEJORA", nullptr, {HudActionType::Upgrade});
    } else if (enemy) {
        const bool tracking = g.Tracking() && *g.Tracking() == target;
        u.Button(ix + bw + 8.0f * s, by, bw, bh, "ALINEAR", "B", {HudActionType::Track}, true, tracking);
    } else if (target.kind == Kind::Anomaly) u.Button(ix + bw + 8.0f * s, by, bw, bh, "EXPLORAR", nullptr, {HudActionType::Use});
}

// Cinta de rumbo, arriba, con la marca del objetivo.
void Compass(const Ui& u, const GameSession& g, const TargetRef* target, float width) {
    const float s = u.s, cx = width * 0.5f, y = 14.0f * s, half = 280.0f * s, h = 30.0f * s;
    const ShipState& ship = g.Ship();
    const Vec3d forward = Heading(ship);
    const float heading = static_cast<float>(std::atan2(forward.x, forward.z)) * 360.0f / kTau;
    const float degree = half / 60.0f;

    u.d->AddRectFilledMultiColor(ImVec2(cx - half, y), ImVec2(cx, y + h), Rgba(18, 10, 4, 0.0f), kPanel, kPanel, Rgba(18, 10, 4, 0.0f));
    u.d->AddRectFilledMultiColor(ImVec2(cx, y), ImVec2(cx + half, y + h), kPanel, Rgba(18, 10, 4, 0.0f), Rgba(18, 10, 4, 0.0f), kPanel);
    const int first = static_cast<int>(std::floor(heading / 5.0f)) * 5 - 60;
    for (int d = first; d <= first + 125; d += 5) {
        const float at = cx + (static_cast<float>(d) - heading) * degree;
        if (std::abs(at - cx) > half) continue;
        const float fade = 1.0f - std::abs(at - cx) / half;
        const bool major = d % 15 == 0;
        u.d->AddLine(ImVec2(at, y + h - (major ? 12.0f : 6.0f) * s), ImVec2(at, y + h), Rgba(255, 170, 40, fade * (major ? 0.9f : 0.45f)));
        if (d % 30 == 0) {
            int shown = d % 360;
            if (shown < 0) shown += 360;
            u.Center(at, y + 1.0f * s, 11.5f, Rgba(255, 215, 150, fade), Format("%03d", shown));
        }
    }
    u.d->AddTriangleFilled(ImVec2(cx, y + h - 1.0f * s), ImVec2(cx - 6.0f * s, y + h + 8.0f * s), ImVec2(cx + 6.0f * s, y + h + 8.0f * s), kYellow);

    if (target && target->kind != Kind::None) {
        if (const std::optional<TargetInfo> info = g.Resolve(*target)) {
            const Vec3d rel = ship.orientation.Conjugate().ToMatrix().TransformDirection(info->position - ship.position);
            const float delta = static_cast<float>(std::atan2(rel.x, rel.z)) * 360.0f / kTau;
            const float at = cx + std::clamp(delta, -58.0f, 58.0f) * degree;
            u.d->AddQuadFilled(ImVec2(at, y + 12.0f * s), ImVec2(at + 5.0f * s, y + 18.0f * s), ImVec2(at, y + 24.0f * s), ImVec2(at - 5.0f * s, y + 18.0f * s), kOrange);
        }
    }

    const World& w = g.GetWorld();
    const astro::StarSystemData& system = *w.systems[static_cast<std::size_t>(g.NearestSystem())];
    const std::string title = Format("SYS-%d", g.NearestSystem());
    const float size = 26.0f;
    u.Title(cx - u.TitleWidth(size, title) * 0.5f, y + h + 14.0f * s, size, kAmber, title);
    u.Center(cx, y + h + 46.0f * s, 12.0f, kMuted, astro::LayoutName(system));
}

std::string Duration(double seconds) {
    const double abs = std::abs(seconds);
    if (abs >= 86400.0 * 365.25) return Format("%.2f años", seconds / (86400.0 * 365.25));
    if (abs >= 86400.0) return Format("%.1f días", seconds / 86400.0);
    if (abs >= 3600.0) return Format("%.1f h", seconds / 3600.0);
    return Format("%.0f s", seconds);
}

void Row(const Ui& u, float x, float y, float w, const std::string& name, const std::string& value) {
    u.Label(x, y, 12.5f, kMuted, name);
    u.Right(x + w, y, 12.5f, kText, value);
}

void Heading(const Ui& u, float x, float y, float w, const std::string& text) {
    u.Label(x, y, 13.5f, kYellow, text);
    u.d->AddLine(ImVec2(x, y + 18.0f * u.s), ImVec2(x + w, y + 18.0f * u.s), kAmberDim);
}

// Holograma del sistema: orbitas en escala logaritmica, girando despacio, con el astro resaltado.
void Hologram(const Ui& u, const GameSession& g, const TargetRef& target, float cx, float cy, float radius) {
    const World& w = g.GetWorld();
    const astro::StarSystemData& system = *w.systems[static_cast<std::size_t>(target.system)];
    const astro::StarData& star = system.StarAt(0);
    const auto skip = [](const astro::BodyData& p) { return p.parent >= 0 || p.kind == astro::PlanetClass::Comet || p.kind == astro::PlanetClass::Asteroid || p.destroyed; };
    double closest = star.habitableInner, outer = star.snowLine * 1.2;
    for (const astro::BodyData& p : system.bodies) {
        if (skip(p)) continue;
        closest = std::min(closest, Periapsis(p.orbit));
        outer = std::max(outer, Periapsis(p.orbit) * 1.6);
    }
    const double inner = std::max(star.radius, closest * 0.4);
    const double spin = u.t * 0.14, cs = std::cos(spin), sn = std::sin(spin);
    const float tilt = 0.40f, lift = 0.9f;
    const auto project = [&](const Vec3d& p) {
        const double r = std::max(std::sqrt(p.x * p.x + p.z * p.z), 1.0);
        const double scaled = std::log(std::max(r, inner) / inner) / std::log(outer / inner) * radius;
        const double x = p.x / r * scaled, z = p.z / r * scaled;
        return ImVec2(cx + static_cast<float>(x * cs - z * sn), cy + static_cast<float>(x * sn + z * cs) * tilt - static_cast<float>(p.y / r * scaled) * lift);
    };

    // Rejilla del plano.
    for (int ring = 1; ring <= 4; ++ring) u.d->AddEllipse(ImVec2(cx, cy), ImVec2(radius * static_cast<float>(ring) / 4.0f, radius * tilt * static_cast<float>(ring) / 4.0f), kAmberFaint, 0.0f, 64);
    for (int i = 0; i < 12; ++i) {
        const float a = kTau * static_cast<float>(i) / 12.0f + static_cast<float>(spin);
        u.d->AddLine(ImVec2(cx, cy), ImVec2(cx + std::cos(a) * radius, cy + std::sin(a) * radius * tilt), kAmberFaint);
    }
    // Barrido de luz, como un proyector.
    const float sweep = static_cast<float>(std::fmod(u.t * 0.35, 1.0));
    u.d->AddEllipse(ImVec2(cx, cy), ImVec2(radius * sweep, radius * tilt * sweep), Rgba(255, 228, 96, 0.28f * (1.0f - sweep)), 0.0f, 64, 2.0f * u.s);

    constexpr int kPoints = 72;
    for (std::size_t b = 0; b < system.bodies.size(); ++b) {
        const astro::BodyData& p = system.bodies[b];
        if (skip(p)) continue;
        const bool chosen = target.kind == Kind::Planet && static_cast<std::size_t>(target.index) == b;
        const KeplerOrbit orbit = MakeOrbit(kNoNode, p.orbit);
        ImVec2 line[kPoints];
        for (int i = 0; i < kPoints; ++i) line[i] = project(OrbitPositionFromEccentric(orbit, kPi * 2.0 * i / kPoints));
        u.d->AddPolyline(line, kPoints, chosen ? kYellow : Rgba(255, 170, 40, 0.42f), (chosen ? 1.8f : 1.0f) * u.s, ImDrawFlags_Closed);
        const ImVec2 now = project(OrbitPosition(orbit, g.SimTime()));
        const Vec3d flat = OrbitPosition(orbit, g.SimTime());
        const ImVec2 base = project(Vec3d{flat.x, 0.0, flat.z});
        u.d->AddLine(base, now, kAmberDim);
        u.d->AddCircleFilled(now, (chosen ? 4.5f : 2.5f) * u.s, chosen ? kYellow : kAmber);
        if (chosen) u.d->AddCircle(now, 9.0f * u.s, kYellow, 0, 1.2f * u.s);
    }
    const Vec3f color = astro::BlackbodyColor(star.temperature);
    u.d->AddCircleFilled(ImVec2(cx, cy), 7.0f * u.s, Rgba(static_cast<int>(color.x * 255.0f), static_cast<int>(color.y * 255.0f), static_cast<int>(color.z * 255.0f), target.kind == Kind::Star ? 1.0f : 0.8f));
    u.d->AddCircle(ImVec2(cx, cy), 11.0f * u.s, target.kind == Kind::Star ? kYellow : kAmberDim);

    // Lineas de barrido sobre todo el holograma.
    const float top = cy - radius * 0.75f;
    for (float line = 0; line < radius * 1.5f; line += 4.0f * u.s) u.d->AddLine(ImVec2(cx - radius, top + line), ImVec2(cx + radius, top + line), Rgba(18, 10, 4, 0.16f));
}

float ElementGrid(const Ui& u, const astro::Composition& c, float x, float y, float w);

void StarStats(const Ui& u, const GameSession& g, const astro::StarData& star, const astro::StarSystemData& system, float x, float y, float w) {
    const float row = 19.0f * u.s;
    const bool hole = star.kind == astro::StarClass::BlackHole;
    const StarYield yield = YieldOf(star);
    Heading(u, x, y, w, hole ? "AGUJERO NEGRO" : "ESTRELLA");
    y += 26.0f * u.s;
    Row(u, x, y, w, "Clase", astro::Info(star.kind).name); y += row;
    Row(u, x, y, w, "Sistema", Format("%s, %d estrellas", astro::LayoutName(system), system.StarCount())); y += row;
    Row(u, x, y, w, "Masa", Format("%.3g masas solares", star.mass / astro::kSolarMass)); y += row;
    if (hole) {
        Row(u, x, y, w, "Radio de Schwarzschild", Distance(star.radius)); y += row;
        Row(u, x, y, w, "Giro a/M", star.blackHoleSpin > 0.0 ? Format("%.2f", star.blackHoleSpin) : std::string("sin rotación")); y += row;
    } else {
        Row(u, x, y, w, "Radio", Format("%.3g radios solares", star.radius / astro::kSolarRadius)); y += row;
        Row(u, x, y, w, "Temperatura", Format("%.0f K", star.temperature)); y += row;
        Row(u, x, y, w, "Giro", Duration(2.0 * kPi / star.spinRate)); y += row;
        Row(u, x, y, w, "Zona habitable", Format("%.2f a %.2f UA", star.habitableInner / astro::kAstronomicalUnit, star.habitableOuter / astro::kAstronomicalUnit)); y += row;
        Row(u, x, y, w, "Línea de hielo", Format("%.2f UA", star.snowLine / astro::kAstronomicalUnit)); y += row;
    }
    Row(u, x, y, w, "Luminosidad", Format("%.3g soles", star.luminosity)); y += row + 8.0f * u.s;

    if (!hole) {
        Heading(u, x, y, w, "COMPOSICIÓN");
        y += 26.0f * u.s;
        u.Meter(x, y, w, "Hidrógeno", star.hydrogen, Rgba(150, 200, 255), Format("%.0f %%", star.hydrogen * 100.0)); y += 29.0f * u.s;
        u.Meter(x, y, w, "Helio", star.helium, Rgba(255, 220, 140), Format("%.0f %%", star.helium * 100.0)); y += 29.0f * u.s;
        u.Meter(x, y, w, "Metales", star.metals * 20.0, kOrange, Format("%.1f %%", star.metals * 100.0)); y += 30.0f * u.s;
        y = ElementGrid(u, astro::ElementsOf(star), x, y, w);
    }
    Heading(u, x, y, w, "PARA UNA CENTRAL");
    y += 26.0f * u.s;
    Row(u, x, y, w, "Potencia", PowerText(yield.energyPerHour)); y += row;
    Row(u, x, y, w, "Antimateria", MassRateText(g.Stations().AntimatterFrom(yield.energyPerHour))); y += row;
    Row(u, x, y, w, "Materia estelar", MassRateText(yield.matterPerHour)); y += row;
    Row(u, x, y, w, "Blindaje necesario", Format("nivel %d (tienes %d)", yield.requiredTier, g.Ship().heatShieldTier)); y += row;
    Row(u, x, y, w, "Órbita de trabajo", Distance(yield.orbitRadius));
}

// Todos los elementos, en tres columnas: simbolo y porcentaje en masa. Lo que es traza se marca asi.
float ElementGrid(const Ui& u, const astro::Composition& c, float x, float y, float w) {
    const float s = u.s, row = 17.0f * s, column = w / 3.0f;
    Heading(u, x, y, w, "ELEMENTOS");
    y += 24.0f * s;
    for (std::size_t e = 0; e < astro::kElementCount; ++e) {
        const float cx = x + column * static_cast<float>(e % 3), cy = y + row * static_cast<float>(e / 3);
        const double share = c.share[e] * 100.0;
        const std::string value = share >= 0.1 ? Format("%.1f %%", share) : share > 0.0 ? std::string("traza") : std::string("-");
        u.Label(cx, cy, 12.0f, share >= 1.0 ? kText : kMuted, astro::ElementSymbol(static_cast<astro::Element>(e)));
        u.Right(cx + column - 10.0f * s, cy, 12.0f, share >= 1.0 ? kText : kMuted, value);
    }
    return y + row * 4.0f + 6.0f * s;
}

// Un campo de asteroides: su composicion sale de lo que daria una mina.
astro::Composition BeltElements(astro::BeltProfile profile) {
    const Deposit deposit = DepositOf(profile);
    const auto share = [&](Resource r) { return deposit.share[static_cast<std::size_t>(r)]; };
    astro::Materials m;
    m.iron = share(Resource::Metal);
    m.silicate = share(Resource::Silicate);
    m.water = share(Resource::Ice) * 0.7;
    m.ices = share(Resource::Ice) * 0.3 + share(Resource::Gas);
    m.carbon = 0.3;
    return astro::ElementsOf(m);
}

// De que esta hecho el aire, deducido de la clase del astro y sus materiales.
std::string AirText(const astro::BodyData& d) {
    const astro::Materials& m = d.materials;
    if (m.pressure <= 1.0e-6) return "Sin atmósfera";
    const char* gases = "dióxido de carbono";
    switch (d.kind) {
        case astro::PlanetClass::GasGiant: case astro::PlanetClass::HotJupiter: return "Envolvente: hidrógeno y helio";
        case astro::PlanetClass::IceGiant: return "Envolvente: hidrógeno, helio y metano";
        case astro::PlanetClass::Terran: gases = m.life ? "nitrógeno y oxígeno" : "nitrógeno y CO2"; break;
        case astro::PlanetClass::Lava: gases = m.sulfur > 0.2 ? "vapor de roca y azufre" : "vapor de roca"; break;
        case astro::PlanetClass::Ice: gases = m.methane > 0.25 ? "nitrógeno y metano" : "nitrógeno"; break;
        default: break;
    }
    const char* thickness = m.pressure < 0.01 ? "Tenue" : m.pressure < 0.5 ? "Fina" : m.pressure < 5.0 ? "Densa" : "Muy densa";
    return Format("%s: %s", thickness, gases);
}

void BodyStats(const Ui& u, const GameSession& g, const astro::BodyData& data, const astro::StarSystemData& system, int index, float x, float y, float w) {
    const float s = u.s, row = 18.0f * s, half = (w - 16.0f * s) * 0.5f, x2 = x + half + 16.0f * s;
    const astro::Materials& m = data.materials;
    Heading(u, x, y, w, "DATOS");
    y += 25.0f * s;
    const double moons = static_cast<double>(std::count_if(system.bodies.begin(), system.bodies.end(), [&](const astro::BodyData& o) { return o.parent == index; }));
    Row(u, x, y, half, "Masa", Format("%.3g Tierras", data.mass / astro::kEarthMass));
    Row(u, x2, y, half, "Radio", Distance(data.radius)); y += row;
    Row(u, x, y, half, "Densidad", Format("%.2f g/cm3", data.mass / (4.0 / 3.0 * kPi * std::pow(data.radius, 3.0)) / 1.0e12));
    Row(u, x2, y, half, "Gravedad", Format("%.2f g", (data.mass / astro::kEarthMass) / std::pow(data.radius / astro::kEarthRadius, 2.0))); y += row;
    Row(u, x, y, half, "Temperatura", Format("%.0f K", m.surfaceTemperature > 0.0 ? m.surfaceTemperature : data.temperature));
    Row(u, x2, y, half, "Presión", m.gas > 0.05 || m.ices > 0.3 ? std::string("sin suelo") : m.pressure <= 1.0e-6 ? std::string("vacío") : Format("%.3g bar", m.pressure)); y += row;
    Row(u, x, y, w, "Atmósfera", AirText(data)); y += row;
    Row(u, x, y, half, "Giro", data.tidallyLocked ? std::string("acoplado") : Duration(2.0 * kPi / data.spin.rate));
    Row(u, x2, y, half, "Lunas", Format("%.0f", moons)); y += row + 8.0f * s;

    Heading(u, x, y, w, "MATERIALES");
    y += 25.0f * s;
    const struct { const char* name; double value; ImU32 color; } rows[] = {
        {"Hierro", m.iron, Rgba(190, 190, 205)}, {"Silicatos", m.silicate, Rgba(210, 165, 120)}, {"Agua", m.water, Rgba(90, 160, 255)},
        {"Hielos", m.ices, Rgba(170, 235, 255)}, {"Hidrógeno y helio", m.gas, kYellow}};
    for (const auto& r : rows) {
        u.Meter(x, y, w, r.name, r.value, r.color, Format("%.1f %%", r.value * 100.0));
        y += 24.0f * s;
    }
    y += 4.0f * s;
    y = ElementGrid(u, astro::ElementsOf(m), x, y, w);

    Heading(u, x, y, w, "PARA UNA MINA");
    y += 25.0f * s;
    Deposit deposit;
    if (DepositOf(data, deposit)) {
        Row(u, x, y, half, "Riqueza", Format("x%.2f", deposit.richness));
        Row(u, x2, y, half, "Extrae", MassRateText(g.Rules().stations.miningRate * deposit.richness)); y += row;
        std::string yields;
        for (int r = 0; r < static_cast<int>(Resource::Count); ++r) {
            const double share = deposit.share[static_cast<std::size_t>(r)];
            if (share > 0.01) yields += Format("%s%s %.0f %%", yields.empty() ? "" : "   ", Name(static_cast<Resource>(r)), share * 100.0);
        }
        u.Label(x, y, 12.0f, kNeutral, yields);
    } else {
        u.Label(x, y, 12.5f, kMuted, "Aquí no hay nada que extraer.");
    }
    y += row + 6.0f * s;

    Heading(u, x, y, w, "ÓRBITA");
    y += 25.0f * s;
    const OrbitalElements& e = data.orbit;
    Row(u, x, y, half, "Semieje", Distance(e.semiMajorAxis));
    Row(u, x2, y, half, "Excentricidad", Format("%.3f", e.eccentricity)); y += row;
    Row(u, x, y, half, "Periodo", Duration(OrbitPeriod(e)));
    Row(u, x2, y, half, "Inclinación", Format("%.1f°", e.inclination * 180.0 / kPi));
}

void AnalysisWindow(const Ui& u, const GameSession& g, const TargetRef& target, const AnalysisLayout& l) {
    const float s = u.s;
    const World& w = g.GetWorld();
    const std::optional<TargetInfo> info = g.Resolve(target);
    const bool astroTarget = target.kind == Kind::Star || target.kind == Kind::Planet || target.kind == Kind::Belt;
    u.d->AddRectFilled(ImVec2(l.x, l.y), ImVec2(l.x + l.w, l.y + l.h), Rgba(8, 4, 2, 0.72f));
    u.Panel(l.x, l.y, l.w, l.h, "ANÁLISIS", "ESC  CERRAR");
    if (!info || !astroTarget) {
        u.Center(l.x + l.w * 0.5f, l.y + l.h * 0.45f, 16.0f, kText, "Elige una estrella, un planeta o un campo.");
        return;
    }
    u.Title(l.x + 18.0f * s, l.y + 32.0f * s, 22.0f, kAmber, info->name);
    if (!g.Scanned(target)) {
        u.Center(l.x + l.w * 0.5f, l.y + l.h * 0.42f, 18.0f, kOrange, "SIN DATOS");
        u.Center(l.x + l.w * 0.5f, l.y + l.h * 0.42f + 30.0f * s, 14.0f, kText, "Frena con Espacio y usa Escáner, en la rueda (T), para conocer este astro.");
        return;
    }

    // Esfera real a la izquierda; holograma debajo; datos a la derecha.
    const float size = static_cast<float>(l.miniSize);
    const ImVec2 center(static_cast<float>(l.miniX) + size * 0.5f, static_cast<float>(l.miniY) + size * 0.5f);
    if (l.miniSize > 0 && target.kind != Kind::Belt) {
        u.d->AddCircleFilled(center, size * 0.5f, kPanelDeep, 64);
        u.d->AddCircle(center, size * 0.5f, kAmberDim, 64);
        const float spin = static_cast<float>(u.t * 0.4);
        for (int i = 0; i < 3; ++i) u.Arc(center.x, center.y, size * 0.54f, spin + static_cast<float>(i) * 2.09f, spin + static_cast<float>(i) * 2.09f + 1.0f, kAmber, 1.5f * s);
    }
    const float left = l.w * 0.46f;
    const float holoY = static_cast<float>(l.miniY) + size + (l.y + l.h - static_cast<float>(l.miniY) - size) * 0.5f;
    Hologram(u, g, target, l.x + left * 0.5f, holoY, left * 0.46f);
    u.Label(l.x + 18.0f * s, l.y + l.h - 26.0f * s, 11.5f, kMuted, "Órbitas en escala logarítmica");

    const float x = l.x + left + 12.0f * s, y = l.y + 64.0f * s, width = l.w - left - 30.0f * s;
    const astro::StarSystemData& system = *w.systems[static_cast<std::size_t>(target.system)];
    u.Label(l.x + 18.0f * s, l.y + 62.0f * s, 12.5f, kText, info->detail);
    if (target.kind == Kind::Star) {
        StarStats(u, g, system.StarAt(target.index), system, x, y, width);
    } else if (target.kind == Kind::Planet) {
        BodyStats(u, g, system.bodies[static_cast<std::size_t>(target.index)], system, target.index, x, y, width);
    } else {
        const AsteroidBelt* belt = nullptr;
        const SystemState& state = w.state[static_cast<std::size_t>(target.system)];
        if (static_cast<std::size_t>(target.index) < state.beltBodies.size() && state.beltBodies[static_cast<std::size_t>(target.index)] >= 0) belt = &w.belts[static_cast<std::size_t>(w.bodies[static_cast<std::size_t>(state.beltBodies[static_cast<std::size_t>(target.index)])].belt)];
        float line = y;
        const float row = 19.0f * s;
        Heading(u, x, line, width, "CAMPO DE ASTEROIDES");
        line += 26.0f * s;
        if (belt) {
            Row(u, x, line, width, "Tipo", ProfileInfo(belt->profile).name); line += row;
            Row(u, x, line, width, "Radio interior", Distance(belt->inner)); line += row;
            Row(u, x, line, width, "Radio exterior", Distance(belt->outer)); line += row;
            Row(u, x, line, width, "Cúmulos", Format("%zu", belt->families.size())); line += row + 8.0f * s;
            line = ElementGrid(u, BeltElements(belt->profile), x, line, width);
            const Deposit deposit = DepositOf(belt->profile);
            Heading(u, x, line, width, "PARA UNA MINA");
            line += 26.0f * s;
            Row(u, x, line, width, "Riqueza", Format("x%.2f", deposit.richness)); line += row;
            for (int r = 0; r < static_cast<int>(Resource::Count); ++r) {
                const double share = deposit.share[static_cast<std::size_t>(r)];
                if (share <= 0.01) continue;
                u.Meter(x, line, width, Name(static_cast<Resource>(r)), share, kAmber, Format("%.0f %%", share * 100.0));
                line += 27.0f * s;
            }
        }
    }
}

// Lo que va a recorrer: la orbita, la inercia o la ruta. Se dibuja sobre el espacio.
void FlightPath(const Ui& u, const GameSession& g, const Camera& camera) {
    const std::vector<Vec3d> path = g.PredictedPath();
    const bool route = g.Autopilot().has_value();
    const float width = static_cast<float>(camera.Width()), height = static_cast<float>(camera.Height());
    ScreenPoint last;
    for (const Vec3d& point : path) {
        const ScreenPoint p = camera.WorldToScreen(point);
        if (p.visible && last.visible && std::abs(p.x - last.x) < width && std::abs(p.y - last.y) < height) u.d->AddLine(ImVec2(last.x, last.y), ImVec2(p.x, p.y), route ? Rgba(255, 215, 90, 0.75f) : Rgba(120, 220, 255, 0.55f), 1.4f * u.s);
        last = p;
    }
}

// Lo que va a recorrer la nave con el mando y el giro que lleva: se enciende en la rueda. Una marca
// cada cinco segundos reales, y el final dice cuanto falta para llegar ahi.
void Trajectory(const Ui& u, const GameSession& g, const Camera& camera) {
    constexpr double kSeconds = 30.0;
    const std::vector<Vec3d> path = g.ManeuverPath(kSeconds);
    if (path.size() < 2) return;
    const float s = u.s, width = static_cast<float>(camera.Width()), height = static_cast<float>(camera.Height());
    const ImU32 color = Rgba(120, 255, 170, 0.85f), faint = Rgba(120, 255, 170, 0.35f);
    const std::size_t every = std::max<std::size_t>((path.size() - 1) / 6, 1);
    ScreenPoint last = camera.WorldToScreen(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) {
        const ScreenPoint p = camera.WorldToScreen(path[i]);
        if (p.visible && last.visible && std::abs(p.x - last.x) < width && std::abs(p.y - last.y) < height) {
            u.d->AddLine(ImVec2(last.x, last.y), ImVec2(p.x, p.y), i % 2 == 0 ? color : faint, 2.0f * s);
            if (i % every == 0) u.d->AddCircleFilled(ImVec2(p.x, p.y), 2.6f * s, color);
        }
        last = p;
    }
    if (last.visible) u.Label(last.x + 8.0f * s, last.y - 8.0f * s, 11.5f, color, Format("+%.0f s", kSeconds));
}

// Un anillo fijo en el centro de la pantalla: la flecha dice hacia donde gira la nave ahora mismo,
// aunque el punto de mira se haya salido de la pantalla. La marca hueca, hacia donde va a girar
// (lo apuntado); el arco de arriba, el alabeo.
void TurnRing(const Ui& u, const GameSession& g, const Camera& camera, float width, float height) {
    const ShipState& ship = g.Ship();
    const float s = u.s, cx = width * 0.5f, cy = height * 0.5f, r = 70.0f * s;
    u.d->AddCircle(ImVec2(cx, cy), r, Rgba(255, 150, 40, 0.18f), 72, 1.2f * s);
    for (int i = 0; i < 4; ++i) {
        const float a = kTau * 0.25f * static_cast<float>(i);
        u.d->AddLine(ImVec2(cx + std::cos(a) * (r - 4.0f * s), cy + std::sin(a) * (r - 4.0f * s)), ImVec2(cx + std::cos(a) * (r + 4.0f * s), cy + std::sin(a) * (r + 4.0f * s)), kAmberDim, 1.0f * s);
    }
    const Mat4f view = camera.ViewRotation();
    const auto screen = [&](const Vec3d& world) {
        const Vec3f v = view.TransformDirection(Vec3f(world));
        return ImVec2(v.x, v.y);  // en pantalla, x e y van como en la vista (WorldToScreen)
    };
    const Vec3d heading = Heading(ship);

    // Lo apuntado: hacia alli girara.
    const double error = AimError(ship);
    if (error > 0.01) {
        const ImVec2 d = screen(AimHeading(ship) - heading);
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1e-6f) {
            const float a = std::atan2(d.y, d.x), px = cx + std::cos(a) * r, py = cy + std::sin(a) * r;
            const float tx = std::cos(a), ty = std::sin(a);
            u.d->AddTriangle(ImVec2(px + tx * 7.0f * s, py + ty * 7.0f * s), ImVec2(px - ty * 5.0f * s, py + tx * 5.0f * s), ImVec2(px + ty * 5.0f * s, py - tx * 5.0f * s), kNeutral, 1.2f * s);
        }
    }

    // El giro de verdad: la proa se mueve hacia la flecha. Cuanto mas deprisa, mas larga y opaca.
    const Vec3d spin = ship.angularRate;
    const double fraction = std::clamp(Length(spin) / std::max(g.Rules().ship.turnRate, 1e-6), 0.0, 1.0);
    const ImVec2 nose = screen(Cross(spin, heading));
    const float noseLen = std::sqrt(nose.x * nose.x + nose.y * nose.y);
    if (noseLen > 1e-5f && fraction > 0.01) {
        const float a = std::atan2(nose.y, nose.x), tx = std::cos(a), ty = std::sin(a);
        const float alpha = 0.45f + 0.55f * static_cast<float>(fraction);
        const ImU32 color = Rgba(255, 228, 96, alpha);
        u.Arc(cx, cy, r, a - 0.35f, a + 0.35f, color, 3.0f * s);
        const float px = cx + tx * r, py = cy + ty * r, head = (10.0f + 10.0f * static_cast<float>(fraction)) * s;
        u.d->AddTriangleFilled(ImVec2(px + tx * head, py + ty * head), ImVec2(px - ty * 6.0f * s, py + tx * 6.0f * s), ImVec2(px + ty * 6.0f * s, py - tx * 6.0f * s), color);
        u.Center(px + tx * (head + 14.0f * s), py + ty * (head + 14.0f * s) - 7.0f * s, 11.0f, color, Format("%.1f°/s", Length(spin) * 180.0 / 3.14159265358979));
    }
    // Alabeo: un arco que sale de donde queda el techo de la nave y va hacia donde se mueve al rodar.
    const double roll = Dot(spin, heading);
    const Vec3d up = ship.orientation.ToMatrix().TransformDirection(Vec3d{0, -1, 0});
    const ImVec2 roof = screen(up), moving = screen(Cross(heading * roll, up));
    if (std::abs(roll) > 0.004 && roof.x * roof.x + roof.y * roof.y > 1e-6f) {
        const float sweep = static_cast<float>(std::clamp(std::abs(roll) / g.Rules().ship.turnRate, 0.15, 1.0)) * 0.9f;
        const float side = roof.x * moving.y - roof.y * moving.x > 0.0f ? 1.0f : -1.0f;
        const float from = std::atan2(roof.y, roof.x), to = from + side * sweep;
        u.Arc(cx, cy, r + 9.0f * s, std::min(from, to), std::max(from, to), kVoid, 2.0f * s);
        const float tx = -std::sin(to) * side, ty = std::cos(to) * side;
        const float px = cx + std::cos(to) * (r + 9.0f * s), py = cy + std::sin(to) * (r + 9.0f * s);
        u.d->AddTriangleFilled(ImVec2(px + tx * 8.0f * s, py + ty * 8.0f * s), ImVec2(px - ty * 4.5f * s, py + tx * 4.5f * s), ImVec2(px + ty * 4.5f * s, py - tx * 4.5f * s), kVoid);
    }
}

// Ruta y orbita, en el lateral; la alarma, en el centro, por debajo del punto de mira.
void FlightStatus(const Ui& u, const GameSession& g, float width, float x, float top) {
    const float s = u.s;
    float y = top;
    const ShipState& ship = g.Ship();
    const RouteReport route = g.Route();
    if (route.phase != RoutePhase::None) {
        static const char* const names[] = {"", "Orientando", "En crucero", "Rodeando", "Frenando"};
        u.Label(x, y, 14.0f, kYellow, Format("RUTA A %s", route.target.c_str()));
        y += 18.0f * s;
        const std::string left = route.seconds > 0.0 && route.seconds < 3600.0 ? Format("   %.0f s", route.seconds) : std::string();
        u.Label(x, y, 12.5f, kText, Format("%s   %s%s", names[static_cast<int>(route.phase)], Distance(route.distance).c_str(), left.c_str()));
        y += 20.0f * s;
    }
    const OrbitReport orbit = g.OrbitNow();
    if (orbit.valid && ship.warpSpeed <= 0.0 && ship.speed >= 1.0) {
        if (orbit.locked) {
            u.Label(x, y, 14.0f, kNeutral, Format("ÓRBITA DE %s", orbit.body.c_str()));
            y += 18.0f * s;
            if (orbit.period > 0.0) u.Label(x, y, 12.5f, kText, Format("Altura %s   vuelta %.1f h", Distance(orbit.periapsis).c_str(), orbit.period / 3600.0));
        } else if (orbit.escaping) {
            u.Label(x, y, 13.0f, kMuted, Format("Saliendo de %s", orbit.body.c_str()));
        } else {
            u.Label(x, y, 13.0f, kText, Format("Trayectoria en %s", orbit.body.c_str()));
            y += 18.0f * s;
            u.Label(x, y, 12.5f, orbit.periapsis < 0.0 ? kOrange : kText, orbit.periapsis < 0.0 ? Format("Cae a la superficie   máx %s", Distance(orbit.apoapsis).c_str()) : Format("Mín %s   máx %s", Distance(orbit.periapsis).c_str(), Distance(orbit.apoapsis).c_str()));
        }
        y += 20.0f * s;
    }
    if (ship.holding && ship.speed < 1.0 && !g.Orbiting() && !g.Autopilot()) {
        u.Label(x, y, 12.5f, kMuted, "Manteniendo posición");
        y += 18.0f * s;
    }
    if (g.TimeFocus() < 0.9) u.Label(x, y, 12.5f, kOrange, Format("Tiempo reducido: x%.0f", g.TimeFocus() * 60.0));

    if (const std::optional<CollisionWarning> warning = g.Warning()) {
        const float blink = 0.6f + 0.4f * static_cast<float>(std::sin(u.t * 9.0));
        u.Center(width * 0.5f, 212.0f * s, 17.0f, Rgba(255, 58, 40, blink), Format("%s: %s EN %.0f s", warning->heat ? "CALOR LETAL" : "RUMBO DE COLISIÓN", warning->name.c_str(), warning->seconds));
    }
}

void Markers(const Ui& u, const GameSession& g, const Camera& camera, const TargetRef* target) {
    const float s = u.s;
    for (const Contact& k : g.Contacts()) {
        const ScreenPoint p = camera.WorldToScreen(k.position);
        if (!p.visible) continue;
        const bool selected = target && k.target == *target;
        const ImU32 color = selected ? kYellow : ContactColor(k);
        const float r = (selected ? 14.0f : 6.0f) * s;
        if (selected) {
            const float arm = 6.0f * s;
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {
                    const float cx = p.x + r * static_cast<float>(sx), cy = p.y + r * static_cast<float>(sy);
                    u.d->AddLine(ImVec2(cx, cy), ImVec2(cx - arm * static_cast<float>(sx), cy), color, 1.6f * s);
                    u.d->AddLine(ImVec2(cx, cy), ImVec2(cx, cy - arm * static_cast<float>(sy)), color, 1.6f * s);
                }
        } else {
            u.d->AddQuad(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y), color, 1.2f);
        }
        if (selected || k.distance < 2.0e7) u.LabelIn(NameFont(u, k), p.x + r + 6.0f * s, p.y - 8.0f * s, 12.5f, color, Format("%s  %s", k.name.c_str(), Distance(k.distance).c_str()));
    }

    // Cada proyectil cercano lleva su marca: rojo si viene a por ti, amarillo si es tuyo.
    for (const Projectile& shot : g.Projectiles()) {
        const double away = Length(shot.position - g.Ship().position);
        if (away > 1.0e6) continue;
        const ScreenPoint p = camera.WorldToScreen(shot.position);
        if (!p.visible) continue;
        const ImU32 color = shot.team != 0 ? kHostile : kYellow;
        const float r = (shot.guided ? 4.0f : 2.5f) * s;
        u.d->AddQuad(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y), color, 1.2f);
    }

    // Adonde debe mirar el casco para que el canon espinal acierte.
    if (Attackable(g, target)) {
        if (const std::optional<FiringSolution> solution = g.Solution(*target, WeaponKind::Spinal)) {
            const ScreenPoint p = camera.WorldToScreen(g.Ship().position + solution->direction * Length(solution->impact - g.Ship().position));
            if (p.visible) {
                const float r = 5.0f * s;
                u.d->AddQuad(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y), kNeutral, 1.4f);
                u.d->AddLine(ImVec2(p.x - r * 2.0f, p.y), ImVec2(p.x - r, p.y), kNeutral, 1.2f);
                u.d->AddLine(ImVec2(p.x + r, p.y), ImVec2(p.x + r * 2.0f, p.y), kNeutral, 1.2f);
            }
        }
    }
}

// ---- Menu de sistemas ------------------------------------------------------------------------


ImU32 BodyColor(const astro::BodyData& b) {
    const astro::Materials& m = b.materials;
    if (m.gas > 0.3) return Rgba(222, 190, 140);
    if (m.ices > 0.3) return Rgba(170, 220, 250);
    if (m.water > 0.3) return Rgba(70, 130, 220);
    if (m.iron > 0.4) return Rgba(180, 130, 100);
    return Rgba(170, 150, 130);
}

// Respecto a la estrella. Una luna va con su planeta.
Vec3d BodyPoint(const astro::StarSystemData& system, int index, double time) {
    const astro::BodyData& b = system.bodies[static_cast<std::size_t>(index)];
    Vec3d at = OrbitPosition(MakeOrbit(kNoNode, b.orbit), time);
    if (b.parent >= 0) at += BodyPoint(system, b.parent, time);
    return at;
}

// Una camara simple sobre el lienzo: gira alrededor del pivote y proyecta en perspectiva real.
struct AtlasCamera {
    Vec3d pivot;
    double distance, focal, cy, sy, cp, sp;
    float x, y;

    struct Point {
        ImVec2 at;
        float scale = 0;    // pixeles por km aqui
        double depth = 0;
        bool front = false;
    };

    Point Project(const Vec3d& world) const {
        const Vec3d r = world - pivot;
        const double x1 = r.x * cy - r.z * sy, z1 = r.x * sy + r.z * cy;
        const double y1 = r.y * cp - z1 * sp, z2 = r.y * sp + z1 * cp;
        const double depth = distance + z2;
        Point p;
        p.depth = depth;
        p.front = depth > distance * 0.02;
        const double scale = focal / std::max(depth, distance * 0.02);
        p.scale = static_cast<float>(scale);
        p.at = ImVec2(x + static_cast<float>(x1 * scale), y - static_cast<float>(y1 * scale));
        return p;
    }
};

void DrawRing(const Ui& u, const AtlasCamera& camera, const KeplerOrbit& orbit, const Vec3d& center, ImU32 color, float thick) {
    constexpr int kPoints = 180;
    AtlasCamera::Point last = camera.Project(center + OrbitPositionFromEccentric(orbit, 0.0));
    for (int i = 1; i <= kPoints; ++i) {
        const AtlasCamera::Point now = camera.Project(center + OrbitPositionFromEccentric(orbit, kPi * 2.0 * i / kPoints));
        if (last.front && now.front) u.d->AddLine(last.at, now.at, color, thick);
        last = now;
    }
}

void DrawGlobe(const Ui& u, const ImVec2& at, float radius, ImU32 color, const ImVec2& light) {
    u.d->AddCircleFilled(at, radius, IM_COL32_BLACK);
    if (radius < 4.0f) {
        u.d->AddCircleFilled(at, radius, color);
        return;
    }
    // Del lado de la sombra al de la luz, en capas.
    constexpr int kLayers = 10;
    for (int i = 0; i < kLayers; ++i) {
        const float t = static_cast<float>(i) / kLayers;
        const ImU32 layer = (color & 0x00FFFFFFu) | (static_cast<ImU32>(90.0f + 165.0f * t) << 24);
        u.d->AddCircleFilled(ImVec2(at.x + light.x * radius * t * 0.45f, at.y + light.y * radius * t * 0.45f), radius * (1.0f - t * 0.55f), layer, 48);
    }
}

const AtlasRow* FindRow(const std::vector<AtlasRow>& rows, const TargetRef& target) {
    for (const AtlasRow& row : rows)
        if (row.target == target) return &row;
    return nullptr;
}

// El sistema entero a escala real, con sus orbitas inclinadas y excentricas. Lo que se ve minusculo
// se ve minusculo: la rueda acerca hacia lo elegido.
void AtlasSystemView(const Ui& u, const GameSession& g, const AtlasView& a, const AtlasLayout& l, const std::vector<AtlasRow>& rows) {
    const float s = u.s;
    const astro::StarSystemData& system = *g.GetWorld().systems[static_cast<std::size_t>(a.system)];
    const double time = g.SimTime();
    const auto skip = [](const astro::BodyData& p) { return p.destroyed || p.kind == astro::PlanetClass::Comet || p.kind == astro::PlanetClass::Asteroid; };

    double outer = system.star.snowLine;
    for (const astro::BodyData& p : system.bodies)
        if (p.parent < 0 && !skip(p)) outer = std::max(outer, p.orbit.semiMajorAxis * (1.0 + p.orbit.eccentricity));
    const bool planetChosen = a.selected.kind == Kind::Planet && a.selected.system == a.system && static_cast<std::size_t>(a.selected.index) < system.bodies.size();

    AtlasCamera camera;
    camera.pivot = planetChosen ? BodyPoint(system, a.selected.index, time) : Vec3d{};
    // La rueda cambia el enfoque, no la distancia: acercar la camara deformaba las orbitas al pasar entre ellas.
    camera.distance = outer * 2.8;
    camera.focal = static_cast<double>(std::min(l.viewW, l.viewH)) * 1.1 * a.zoom;
    camera.cy = std::cos(a.yaw);
    camera.sy = std::sin(a.yaw);
    camera.cp = std::cos(a.pitch);
    camera.sp = std::sin(a.pitch);
    camera.x = l.viewX + l.viewW * 0.5f;
    camera.y = l.viewY + l.viewH * 0.5f;

    u.d->PushClipRect(ImVec2(l.viewX, l.viewY), ImVec2(l.viewX + l.viewW, l.viewY + l.viewH), true);
    for (const astro::BeltData& belt : system.belts) {
        for (const double radius : {belt.inner, belt.outer}) {
            OrbitalElements e;
            e.semiMajorAxis = radius;
            DrawRing(u, camera, MakeOrbit(kNoNode, e), Vec3d{}, Rgba(255, 170, 40, 0.14f), 1.0f);
        }
    }

    struct Dot {
        int index;       // -1 = la estrella
        AtlasCamera::Point p;
        float radius;
    };
    std::vector<Dot> dots;
    const AtlasCamera::Point sun = camera.Project(Vec3d{});
    dots.push_back({-1, sun, std::max(5.0f * s, static_cast<float>(system.star.radius) * sun.scale)});
    for (int b = 0; b < static_cast<int>(system.bodies.size()); ++b) {
        const astro::BodyData& p = system.bodies[static_cast<std::size_t>(b)];
        if (skip(p)) continue;
        const Vec3d center = p.parent >= 0 ? BodyPoint(system, p.parent, time) : Vec3d{};
        const AtlasCamera::Point host = camera.Project(center);
        // Una luna solo aparece cuando su orbita se distingue de su planeta.
        if (p.parent >= 0 && static_cast<float>(p.orbit.semiMajorAxis) * host.scale < 7.0f) continue;
        const bool chosen = planetChosen && a.selected.index == b;
        DrawRing(u, camera, MakeOrbit(kNoNode, p.orbit), center, chosen ? kYellow : Rgba(255, 170, 40, p.parent >= 0 ? 0.24f : 0.42f), (chosen ? 1.6f : 1.0f) * s);
        const AtlasCamera::Point at = camera.Project(BodyPoint(system, b, time));
        if (at.front) dots.push_back({b, at, std::max((p.parent >= 0 ? 2.0f : 3.0f) * s, static_cast<float>(p.radius) * at.scale)});
    }
    std::sort(dots.begin(), dots.end(), [](const Dot& x, const Dot& y) { return x.p.depth > y.p.depth; });

    const bool inside = u.Hover(l.viewX, l.viewY, l.viewW, l.viewH);
    const Dot* hovered = nullptr;
    float closest = 1.0e9f;
    for (const Dot& d : dots) {
        const ImVec2 toSun(sun.at.x - d.p.at.x, sun.at.y - d.p.at.y);
        const float reach = std::max(std::sqrt(toSun.x * toSun.x + toSun.y * toSun.y), 1.0f);
        if (d.index < 0) {
            const Vec3f c = astro::BlackbodyColor(system.star.temperature);
            const ImU32 color = Rgba(static_cast<int>(c.x * 255.0f), static_cast<int>(c.y * 255.0f), static_cast<int>(c.z * 255.0f));
            u.d->AddCircleFilled(d.p.at, std::min(d.radius * 1.5f, l.viewW), (color & 0x00FFFFFFu) | 0x30000000u, 48);
            u.d->AddCircleFilled(d.p.at, std::min(d.radius, l.viewW), color, 48);
        } else {
            DrawGlobe(u, d.p.at, std::min(d.radius, l.viewW), BodyColor(system.bodies[static_cast<std::size_t>(d.index)]), ImVec2(toSun.x / reach, toSun.y / reach));
        }
        const float dx = u.in.mouseX - d.p.at.x, dy = u.in.mouseY - d.p.at.y, away = std::sqrt(dx * dx + dy * dy) - d.radius;
        if (inside && away < 9.0f * s && away < closest) {
            closest = away;
            hovered = &d;
        }
    }
    const auto label = [&](const Dot& d, ImU32 color) {
        const TargetRef t = d.index < 0 ? TargetRef{Kind::Star, a.system, 0} : TargetRef{Kind::Planet, a.system, d.index};
        const AtlasRow* row = FindRow(rows, t);
        if (row) u.Label(d.p.at.x + d.radius + 8.0f * s, d.p.at.y - 8.0f * s, 12.5f, color, row->name);
    };
    for (const Dot& d : dots) {
        const bool chosen = d.index < 0 ? a.selected.kind == Kind::Star && a.selected.system == a.system : planetChosen && a.selected.index == d.index;
        if (chosen) {
            u.d->AddCircle(d.p.at, d.radius + 6.0f * s, kYellow, 32, 1.4f * s);
            label(d, kYellow);
        }
    }
    // Lo que hay en el sistema aparte de los astros: lo visto, y la propia nave si esta aqui.
    const World& world = g.GetWorld();
    const Vec3d center = world.scene.WorldPosition(world.sectors[static_cast<std::size_t>(a.system)]);
    for (const Anomaly& other : g.Anomalies()) {
        if (other.system != a.system || !(other.discovered || other.revealed)) continue;
        const AtlasCamera::Point p = camera.Project(other.offset);
        if (!p.front) continue;
        const float r = 3.5f * s;
        const ImU32 color = other.kind == AnomalyKind::Derelict ? kVoid : other.stance == Stance::Hostile ? kHostile : kNeutral;
        u.d->AddQuad(ImVec2(p.at.x, p.at.y - r), ImVec2(p.at.x + r, p.at.y), ImVec2(p.at.x, p.at.y + r), ImVec2(p.at.x - r, p.at.y), color, 1.2f);
    }
    if (g.NearestSystem() == a.system) {
        const AtlasCamera::Point p = camera.Project(g.Ship().position - center);
        if (p.front) {
            const float r = 5.0f * s;
            u.d->AddTriangleFilled(ImVec2(p.at.x, p.at.y - r), ImVec2(p.at.x - r * 0.8f, p.at.y + r * 0.7f), ImVec2(p.at.x + r * 0.8f, p.at.y + r * 0.7f), kYellow);
            u.Label(p.at.x + r + 4.0f * s, p.at.y - 7.0f * s, 11.5f, kYellow, "TÚ");
        }
    }
    if (hovered) {
        label(*hovered, kText);
        if (u.in.clicked) {
            if (hovered->index < 0) u.actions.push_back({HudActionType::AtlasSelect, static_cast<int>(Kind::Star), a.system, 0});
            else u.actions.push_back({HudActionType::AtlasSelect, static_cast<int>(Kind::Planet), a.system, hovered->index});
        }
    }
    u.d->PopClipRect();
    u.Label(l.viewX + 8.0f * s, l.viewY + l.viewH - 20.0f * s, 11.5f, kMuted, Format("Escala real   %s de lado", Distance(camera.distance * l.viewW / camera.focal).c_str()));
}

// El astro elegido, con su superficie real si esta en memoria, y sus lunas en su orbita.
void AtlasBodyView(const Ui& u, const GameSession& g, const AtlasView& a, const AtlasLayout& l, const std::vector<AtlasRow>& rows) {
    const float s = u.s, cx = l.viewX + l.viewW * 0.5f, cy = l.viewY + l.viewH * 0.5f;
    const astro::StarSystemData& system = *g.GetWorld().systems[static_cast<std::size_t>(a.system)];
    const bool planet = a.selected.kind == Kind::Planet && a.selected.system == a.system && static_cast<std::size_t>(a.selected.index) < system.bodies.size();
    const bool star = a.selected.kind == Kind::Star && a.selected.system == a.system;
    if (!planet && !star) {
        u.Center(cx, cy, 14.0f, kMuted, "Elige una estrella o un planeta en la lista.");
        return;
    }
    if (!g.Scanned(a.selected)) {
        u.Center(cx, cy - 10.0f * s, 16.0f, kOrange, "SIN DATOS");
        u.Center(cx, cy + 14.0f * s, 12.5f, kText, "Hay que analizar este astro con el escáner.");
        return;
    }
    const float radius = static_cast<float>(l.miniSize) * 0.41f;
    const double realRadius = planet ? system.bodies[static_cast<std::size_t>(a.selected.index)].radius : system.StarAt(a.selected.index).radius;

    // La camara queda fuera de todas las orbitas: desde dentro, las cercanas se deforman de golpe.
    double outermost = realRadius;
    if (planet) {
        for (const astro::BodyData& moon : system.bodies)
            if (moon.parent == a.selected.index && !moon.destroyed) outermost = std::max(outermost, moon.orbit.semiMajorAxis * (1.0 + moon.orbit.eccentricity));
    }
    AtlasCamera camera;
    camera.pivot = {};
    camera.distance = std::max(realRadius * 3.2, outermost * 2.8);
    camera.focal = static_cast<double>(radius) * camera.distance / realRadius;
    camera.cy = std::cos(a.yaw);
    camera.sy = std::sin(a.yaw);
    camera.cp = std::cos(a.pitch);
    camera.sp = std::sin(a.pitch);
    camera.x = cx;
    camera.y = cy;

    u.d->PushClipRect(ImVec2(l.viewX, l.viewY), ImVec2(l.viewX + l.viewW, l.viewY + l.viewH), true);
    // La corona de una estrella la pinta la GPU con el mismo calculo que en el juego (main.cpp).
    int moons = 0;
    if (planet) {
        for (int b = 0; b < static_cast<int>(system.bodies.size()); ++b) {
            const astro::BodyData& moon = system.bodies[static_cast<std::size_t>(b)];
            if (moon.parent != a.selected.index || moon.destroyed) continue;
            ++moons;
            const KeplerOrbit orbit = MakeOrbit(kNoNode, moon.orbit);
            DrawRing(u, camera, orbit, Vec3d{}, Rgba(255, 170, 40, 0.5f), 1.0f * s);
            const AtlasCamera::Point p = camera.Project(OrbitPosition(orbit, g.SimTime()));
            if (!p.front) continue;
            DrawGlobe(u, p.at, std::max(3.0f * s, static_cast<float>(moon.radius) * p.scale), BodyColor(moon), ImVec2(-0.7f, -0.7f));
            if (const AtlasRow* row = FindRow(rows, {Kind::Planet, a.system, b})) u.Label(p.at.x + 8.0f * s, p.at.y - 8.0f * s, 11.5f, kText, row->name);
        }
    }
    u.d->PopClipRect();
    if (!a.realSphere) {
        // Sin la superficie en memoria, un globo con su color: el sistema queda lejos.
        if (planet) {
            DrawGlobe(u, ImVec2(cx, cy), radius, BodyColor(system.bodies[static_cast<std::size_t>(a.selected.index)]), ImVec2(-0.7f, -0.7f));
        } else {
            const Vec3f c = astro::BlackbodyColor(system.StarAt(a.selected.index).temperature);
            u.d->AddCircleFilled(ImVec2(cx, cy), radius, Rgba(static_cast<int>(c.x * 255.0f), static_cast<int>(c.y * 255.0f), static_cast<int>(c.z * 255.0f)), 64);
        }
        u.Label(l.viewX + 8.0f * s, l.viewY + 8.0f * s, 11.5f, kMuted, "Superficie real solo en el sistema donde estás");
    }
    u.Label(l.viewX + 8.0f * s, l.viewY + l.viewH - 20.0f * s, 11.5f, kMuted, moons > 0 ? Format("%d lunas: aleja con la rueda para ver sus órbitas", moons) : std::string("Sin lunas"));
}

void AtlasInfo(const Ui& u, const GameSession& g, const AtlasView& a, const AtlasLayout& l, const std::vector<AtlasRow>& rows) {
    const float s = u.s, x = l.infoX, width = l.infoW;
    float y = l.listY;
    const astro::StarSystemData& system = *g.GetWorld().systems[static_cast<std::size_t>(a.system)];
    const AtlasRow* row = FindRow(rows, a.selected);
    if (!row) {
        u.Label(x, y, 13.0f, kMuted, "Elige algo de la lista.");
        return;
    }
    u.Label(x, y, 15.0f, kYellow, row->name);
    y += 24.0f * s;
    if (!row->known) {
        u.Label(x, y, 13.0f, kOrange, "Sin analizar: usa el escáner cerca de aquí.");
    } else if (a.selected.kind == Kind::Star) {
        StarStats(u, g, system.StarAt(a.selected.index), system, x, y, width);
    } else if (a.selected.kind == Kind::Planet) {
        BodyStats(u, g, system.bodies[static_cast<std::size_t>(a.selected.index)], system, a.selected.index, x, y, width);
    } else if (a.selected.kind == Kind::Belt) {
        const astro::BeltData& belt = system.belts[static_cast<std::size_t>(a.selected.index)];
        const float line = 19.0f * s;
        Heading(u, x, y, width, "CAMPO DE ASTEROIDES");
        y += 26.0f * s;
        Row(u, x, y, width, "Tipo", row->detail); y += line;
        Row(u, x, y, width, "Radio interior", Distance(belt.inner)); y += line;
        Row(u, x, y, width, "Radio exterior", Distance(belt.outer)); y += line + 8.0f * s;
        ElementGrid(u, BeltElements(belt.profile), x, y, width);
    } else {
        u.Label(x, y, 13.0f, kText, row->detail);
    }
    const bool composition = row->known && (a.selected.kind == Kind::Star || a.selected.kind == Kind::Planet || a.selected.kind == Kind::Belt);
    if (composition && !a.table) u.Button(x, l.y + l.h - 78.0f * s, width, 30.0f * s, "VER LOS 118 ELEMENTOS", nullptr, {HudActionType::AtlasMode, 2}, true);
    u.Button(x, l.y + l.h - 42.0f * s, width, 30.0f * s, "FIJAR COMO OBJETIVO", nullptr, {HudActionType::AtlasTarget}, g.Resolve(a.selected).has_value());
}

// Lista por secciones: lo visitado, y lo localizado pero aun sin visitar. El sistema elegido se abre.
void AtlasList(const Ui& u, const GameSession& g, const AtlasView& a, const AtlasLayout& l, const std::vector<AtlasRow>& rows) {
    const float s = u.s, rowH = 20.0f * s;
    struct Item {
        int kind;        // 0 titulo, 1 sistema, 2 fila
        int system = 0;
        const AtlasRow* row = nullptr;
        std::string text;
    };
    std::vector<Item> items;
    const int total = static_cast<int>(g.GetWorld().systems.size());
    const auto section = [&](const char* title, bool visited, int count) {
        items.push_back({0, 0, nullptr, Format("%s  %d", title, count)});
        for (int i = 0; i < total; ++i) {
            if (!g.KnownSystem(i) || g.Visited(i) != visited) continue;
            items.push_back({1, i, nullptr, g.SystemName(i)});
            if (i != a.system) continue;
            for (const AtlasRow& row : rows) items.push_back({2, i, &row, row.name});
        }
    };
    section("VISITADOS", true, g.VisitedSystems());
    section("LOCALIZADOS", false, g.KnownSystems() - g.VisitedSystems());

    const int capacity = std::max(static_cast<int>(l.listH / rowH), 1);
    const int first = std::clamp(a.scroll, 0, std::max(static_cast<int>(items.size()) - capacity, 0));
    float y = l.listY;
    for (int i = first; i < static_cast<int>(items.size()) && i < first + capacity; ++i) {
        const Item& item = items[static_cast<std::size_t>(i)];
        if (item.kind == 0) {
            u.Label(l.listX, y + 2.0f * s, 12.0f, kAmber, item.text);
            u.d->AddLine(ImVec2(l.listX, y + rowH - 3.0f * s), ImVec2(l.listX + l.listW, y + rowH - 3.0f * s), kAmberDim);
            y += rowH;
            continue;
        }
        const bool chosen = item.kind == 1 ? item.system == a.system : item.row->target == a.selected;
        const bool hover = u.Hover(l.listX, y, l.listW, rowH);
        if (chosen || hover) u.d->AddRectFilled(ImVec2(l.listX, y), ImVec2(l.listX + l.listW, y + rowH - 2.0f * s), Rgba(255, 170, 40, chosen ? 0.22f : 0.12f));
        if (item.kind == 1) {
            const bool here = g.NearestSystem() == item.system;
            u.Label(l.listX + 6.0f * s, y + 2.0f * s, 13.0f, chosen ? kYellow : kText, (item.system == a.system ? "- " : "+ ") + item.text);
            if (here) u.Right(l.listX + l.listW - 6.0f * s, y + 3.0f * s, 11.0f, kNeutral, "AQUÍ");
            if (hover && u.in.clicked) u.actions.push_back({HudActionType::AtlasSystem, item.system});
        } else {
            const float indent = (16.0f + 14.0f * static_cast<float>(item.row->depth)) * s;
            u.Label(l.listX + indent, y + 3.0f * s, 12.0f, chosen ? kYellow : item.row->known ? kText : kMuted, item.text);
            u.Right(l.listX + l.listW - 6.0f * s, y + 4.0f * s, 10.5f, kMuted, item.row->detail.size() > 15 ? item.row->detail.substr(0, 14) + "." : item.row->detail);
            if (hover && u.in.clicked) u.actions.push_back({HudActionType::AtlasSelect, static_cast<int>(item.row->target.kind), item.row->target.system, item.row->target.index});
        }
        y += rowH;
    }
    if (static_cast<int>(items.size()) > capacity) u.Right(l.listX + l.listW, l.y + l.h - 24.0f * s, 11.0f, kMuted, Format("%d/%zu", first + 1, items.size()));
}

// Los 118 elementos de lo elegido en el mapa, o false si no hay datos.
bool SelectedElements(const GameSession& g, const AtlasView& a, astro::PeriodicShares& out) {
    const astro::StarSystemData& system = *g.GetWorld().systems[static_cast<std::size_t>(a.system)];
    if (a.selected.system != a.system || !g.Scanned(a.selected)) return false;
    const std::uint32_t base = static_cast<std::uint32_t>(a.system) * 1000u;
    const std::uint32_t index = static_cast<std::uint32_t>(std::max(a.selected.index, 0));
    if (a.selected.kind == Kind::Planet && static_cast<std::size_t>(a.selected.index) < system.bodies.size()) {
        out = astro::AllElementsOf(astro::ElementsOf(system.bodies[index].materials), base + index + 1u);
        return true;
    }
    if (a.selected.kind == Kind::Star && a.selected.index < system.StarCount()) {
        const astro::StarData& star = system.StarAt(a.selected.index);
        if (star.kind == astro::StarClass::BlackHole) return false;
        out = astro::AllElementsOf(astro::ElementsOf(star), base + 900u + index);
        return true;
    }
    if (a.selected.kind == Kind::Belt && static_cast<std::size_t>(a.selected.index) < system.belts.size()) {
        out = astro::AllElementsOf(BeltElements(system.belts[index].profile), base + 800u + index);
        return true;
    }
    return false;
}

// Porcentaje en masa en poco sitio: 31.2, 0.45, 3e-5. Cero es que no existe en la naturaleza.
std::string ShortPercent(double share) {
    const double p = share * 100.0;
    if (p <= 0.0) return "-";
    if (p >= 9.95) return Format("%.0f", p);
    if (p >= 0.995) return Format("%.1f", p);
    if (p >= 0.00995) return Format("%.2f", p);
    const int exponent = static_cast<int>(std::floor(std::log10(p)));
    if (exponent <= -10) return Format("e%d", exponent);  // en una casilla no cabe mas: el detalle, al pasar el raton
    return Format("%.0fe%d", p / std::pow(10.0, exponent), exponent);
}

// La tabla periodica entera de lo elegido: cada casilla con su porcentaje en masa, y el color dice
// cuanto hay en escala logaritmica. Sirve para disenar piezas: lo que existe y lo que escasea.
void PeriodicView(const Ui& u, const GameSession& g, const AtlasView& a, const AtlasLayout& l, const std::vector<AtlasRow>& rows) {
    const float s = u.s;
    astro::PeriodicShares shares{};
    if (!SelectedElements(g, a, shares)) {
        const bool known = FindRow(rows, a.selected) != nullptr;
        u.Center(l.viewX + l.viewW * 0.5f, l.viewY + l.viewH * 0.5f, 14.0f, known ? kOrange : kMuted,
                 known && (a.selected.kind == Kind::Planet || a.selected.kind == Kind::Star || a.selected.kind == Kind::Belt) ? "Sin analizar: usa el escáner cerca de aquí." : "Elige una estrella, un planeta o un campo en la lista.");
        return;
    }
    const AtlasRow* row = FindRow(rows, a.selected);
    const float pad = 10.0f * s, top = l.viewY + 30.0f * s, footer = 46.0f * s;
    const float cellW = (l.viewW - pad * 2.0f) / 18.0f;
    const float cellH = std::min(cellW * 1.45f, (l.viewH - (top - l.viewY) - footer - pad) / 9.5f);
    const float left = l.viewX + pad;
    u.Label(left, l.viewY + 8.0f * s, 13.0f, kYellow, row ? row->name : std::string("Composición"));
    u.Right(l.viewX + l.viewW - pad, l.viewY + 9.0f * s, 11.5f, kMuted, "% en masa de los 118 elementos   pasa el ratón por una casilla");

    int hovered = 0;
    const auto cellAt = [&](int r, int c, float& x, float& y) {
        x = left + cellW * static_cast<float>(c - 1);
        y = top + cellH * static_cast<float>(r <= 7 ? r - 1 : r - 1.5f);  // los lantanidos y actinidos, algo separados
    };
    for (int z = 1; z <= astro::kPeriodicCount; ++z) {
        const astro::PeriodicInfo p = astro::Periodic(z);
        float x = 0, y = 0;
        cellAt(p.row, p.column, x, y);
        const double share = shares[static_cast<std::size_t>(z - 1)];
        // De 1e-12 a 1, de apagado a encendido.
        const float level = share > 0.0 ? std::clamp(static_cast<float>((std::log10(share) + 12.0) / 12.0), 0.0f, 1.0f) : 0.0f;
        const ImU32 fill = share > 0.0 ? Rgba(static_cast<int>(40.0f + 215.0f * level), static_cast<int>(18.0f + 150.0f * level * level), static_cast<int>(8.0f + 30.0f * level), 0.35f + 0.55f * level)
                                       : Rgba(40, 40, 46, 0.5f);
        const ImVec2 a0(x + 1.0f * s, y + 1.0f * s), a1(x + cellW - 1.0f * s, y + cellH - 1.0f * s);
        u.d->AddRectFilled(a0, a1, fill);
        const bool main = z == 1 || z == 2 || z == 6 || z == 7 || z == 8 || z == 12 || z == 13 || z == 14 || z == 16 || z == 20 || z == 26 || z == 28;
        const bool over = u.Hover(x, y, cellW, cellH);
        if (over) hovered = z;
        u.d->AddRect(a0, a1, over ? kYellow : main ? kAmber : kAmberDim, 0.0f, 0, over ? 2.0f : 1.0f);
        const ImU32 ink = level > 0.55f ? kText : share > 0.0 ? kNeutral : kMuted;
        u.Label(x + 3.0f * s, y + 2.0f * s, 9.0f, kMuted, Format("%d", z));
        u.Center(x + cellW * 0.5f, y + cellH * 0.44f, cellH > 50.0f * s ? 17.0f : cellH > 34.0f * s ? 14.0f : 12.0f, ink, p.symbol);
        u.Center(x + cellW * 0.5f, y + cellH * 0.80f, cellH > 50.0f * s ? 11.0f : 9.5f, ink, ShortPercent(share));
    }
    // Donde irian los lantanidos y actinidos dentro de la tabla.
    for (int r = 6; r <= 7; ++r) {
        float x = 0, y = 0;
        cellAt(r, 3, x, y);
        u.d->AddRect(ImVec2(x + 1.0f * s, y + 1.0f * s), ImVec2(x + cellW - 1.0f * s, y + cellH - 1.0f * s), kAmberDim);
        u.Center(x + cellW * 0.5f, y + cellH * 0.5f, 9.5f, kMuted, r == 6 ? "57-71" : "89-103");
    }

    // La casilla bajo el raton, con todo el detalle.
    const float fy = l.viewY + l.viewH - footer + 8.0f * s;
    if (hovered > 0) {
        const astro::PeriodicInfo p = astro::Periodic(hovered);
        const double share = shares[static_cast<std::size_t>(hovered - 1)];
        std::string amount;
        if (share <= 0.0) amount = "no existe en la naturaleza: solo se fabrica";
        else if (share >= 1e-4) amount = Format("%.4g %% en masa", share * 100.0);
        else if (share >= 1e-7) amount = Format("%.4g %% en masa   (%.3g ppm)", share * 100.0, share * 1e6);
        else amount = Format("%.3g %% en masa   (%.3g ppb)", share * 100.0, share * 1e9);
        u.Label(left, fy, 14.0f, kYellow, Format("%s  %s", p.symbol, p.name));
        u.Label(left + 190.0f * s, fy + 1.0f * s, 13.0f, kText, Format("Z %d", hovered));
        u.Label(left + 250.0f * s, fy + 1.0f * s, 13.0f, kText, amount);
    } else {
        double main = 0;
        for (const int z : {1, 2, 6, 7, 8, 12, 13, 14, 16, 20, 26, 28}) main += shares[static_cast<std::size_t>(z - 1)];
        u.Label(left, fy, 12.5f, kMuted, Format("Los doce principales suman %.2f %%; el resto, trazas. Del 95 en adelante, solo sintéticos.", main * 100.0));
    }
}

void AtlasWindow(const Ui& u, const GameSession& g, const AtlasView& a, const AtlasLayout& l) {
    const float s = u.s;
    u.d->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(l.x * 2.0f + l.w, l.y * 2.0f + l.h), Rgba(8, 4, 2, 0.55f));
    u.d->AddRectFilled(ImVec2(l.x, l.y), ImVec2(l.x + l.w, l.y + l.h), Rgba(8, 4, 2, 0.9f));
    u.Panel(l.x, l.y, l.w, l.h, "MAPA", "M  CERRAR");
    const std::vector<AtlasRow> rows = g.AtlasRows(a.system);
    AtlasList(u, g, a, l, rows);

    // La vista en 3D y sus dos modos.
    const float bw = 130.0f * s, by = l.listY;
    u.Button(l.viewX, by, bw, 26.0f * s, "SISTEMA", nullptr, {HudActionType::AtlasMode, 0}, true, !a.astro && !a.table);
    u.Button(l.viewX + bw + 6.0f * s, by, bw, 26.0f * s, "ASTRO", nullptr, {HudActionType::AtlasMode, 1}, true, a.astro && !a.table);
    u.Button(l.viewX + (bw + 6.0f * s) * 2.0f, by, bw * 1.35f, 26.0f * s, "TABLA PERIÓDICA", nullptr, {HudActionType::AtlasMode, 2}, true, a.table);
    if (!a.table) u.Right(l.viewX + l.viewW, by + 7.0f * s, 11.5f, kMuted, "Arrastra: girar   Rueda: acercar");
    u.d->AddRectFilled(ImVec2(l.viewX, l.viewY), ImVec2(l.viewX + l.viewW, l.viewY + l.viewH), kPanelDeep);
    u.d->AddRect(ImVec2(l.viewX, l.viewY), ImVec2(l.viewX + l.viewW, l.viewY + l.viewH), kAmberDim);
    if (a.table) PeriodicView(u, g, a, l, rows);
    else if (a.astro) AtlasBodyView(u, g, a, l, rows);
    else AtlasSystemView(u, g, a, l, rows);
    AtlasInfo(u, g, a, l, rows);
}

// Rueda de opciones: todo lo que antes iba en teclas. Las diez estan siempre en su sitio, asi la mano
// se las aprende; lo que no sirve ahora se ve apagado.
void OptionWheel(const Ui& u, const GameSession& g, const TargetRef* target, float width, float height) {
    const float s = u.s, cx = width * 0.5f, cy = height * 0.5f, outer = 210.0f * s, inner = 78.0f * s;
    const Kind kind = target ? target->kind : Kind::None;
    const bool astroTarget = kind == Kind::Star || kind == Kind::Planet || kind == Kind::Belt;
    const bool ruin = kind == Kind::Anomaly && !Attackable(g, target);
    struct Option {
        const char* name;
        HudAction action;
        bool enabled;
    };
    const bool attached = g.Docking() != DockPhase::None;
    const Option options[] = {
        {"Escáner", {HudActionType::Scan}, g.CanScan() && !g.Scanning()},
        {g.Autopilot() ? "Parar" : "Ir", {g.Autopilot() ? HudActionType::Stop : HudActionType::Go}, g.Autopilot().has_value() || kind != Kind::None},
        {"Órbita", {HudActionType::Orbit}, !g.Orbiting() && !g.DominantName().empty()},
        {attached ? "Soltar" : "Atracar", {attached ? HudActionType::Undock : HudActionType::Dock}, attached || kind == Kind::Station},
        {"A pie", {HudActionType::Disembark}, g.Docked()},
        {"Usar", {HudActionType::Use}, kind == Kind::Station || ruin},
        {"Mina", {HudActionType::Mine}, kind == Kind::Planet || kind == Kind::Belt},
        {"Central", {HudActionType::Power}, kind == Kind::Star},
        {"Mejorar", {HudActionType::Upgrade}, kind == Kind::Station},
        {"Análisis", {HudActionType::Analyze}, astroTarget},
        {"Mapa", {HudActionType::ToggleAtlas}, true},
        {"Disparar", {HudActionType::Fire, -1}, Attackable(g, target) != nullptr},
        {u.in.trajectory ? "Quitar ruta" : "Trayectoria", {HudActionType::ToggleTrajectory}, true},
        {"Taller", {HudActionType::ToggleWorkshop}, g.CanRefit()},
    };
    constexpr int kCount = static_cast<int>(sizeof(options) / sizeof(options[0]));
    const float step = kTau / static_cast<float>(kCount);

    u.d->AddCircleFilled(ImVec2(cx, cy), outer + 8.0f * s, kPanel, 96);
    const float dx = u.in.mouseX - cx, dy = u.in.mouseY - cy, reach = std::sqrt(dx * dx + dy * dy);
    int hovered = -1;
    if (reach > inner) {
        // El primer sector va centrado arriba.
        float angle = std::atan2(dy, dx) + kTau * 0.25f + step * 0.5f;
        while (angle < 0.0f) angle += kTau;
        hovered = static_cast<int>(angle / step) % kCount;
    }
    for (int i = 0; i < kCount; ++i) {
        const Option& o = options[i];
        const float a = -kTau * 0.25f + step * static_cast<float>(i);
        const float from = a - step * 0.5f + 0.012f, to = a + step * 0.5f - 0.012f;
        const bool lit = i == hovered && o.enabled;
        // Un sector de corona no es convexo: se rellena a tiras.
        const ImU32 fill = lit ? Rgba(255, 170, 40, 0.42f) : Rgba(255, 150, 40, o.enabled ? 0.12f : 0.04f);
        constexpr int kStrips = 10;
        for (int k = 0; k < kStrips; ++k) {
            const float t0 = from + (to - from) * static_cast<float>(k) / kStrips, t1 = from + (to - from) * static_cast<float>(k + 1) / kStrips;
            u.d->AddQuadFilled(ImVec2(cx + std::cos(t0) * inner, cy + std::sin(t0) * inner), ImVec2(cx + std::cos(t0) * outer, cy + std::sin(t0) * outer),
                               ImVec2(cx + std::cos(t1) * outer, cy + std::sin(t1) * outer), ImVec2(cx + std::cos(t1) * inner, cy + std::sin(t1) * inner), fill);
        }
        u.d->PathClear();
        u.d->PathArcTo(ImVec2(cx, cy), outer, from, to, 18);
        u.d->PathArcTo(ImVec2(cx, cy), inner, to, from, 12);
        u.d->PathStroke(lit ? kYellow : kAmberDim, ImDrawFlags_Closed, 1.0f * s);
        const float at = (outer + inner) * 0.5f;
        u.Center(cx + std::cos(a) * at, cy + std::sin(a) * at - 8.0f * s, 14.0f, lit ? kYellow : o.enabled ? kText : kMuted, o.name);
    }
    u.d->AddCircle(ImVec2(cx, cy), inner - 6.0f * s, kAmberDim, 64);
    u.Center(cx, cy - 18.0f * s, 12.5f, kYellow, "OPCIONES");
    const std::string name = target && g.Resolve(*target) ? g.Resolve(*target)->name : std::string("Sin objetivo");
    u.Center(cx, cy + 2.0f * s, 11.5f, kMuted, name.size() > 16 ? name.substr(0, 15) + "." : name);

    if (!u.in.clicked) return;
    if (hovered >= 0 && options[hovered].enabled) u.actions.push_back(options[hovered].action);
    u.actions.push_back({HudActionType::CloseWheel});
}

// A pie: sin paneles de vuelo. Lo que importa es donde se esta, como se mueve y como volver.
void FootHud(const Ui& u, const GameSession& g, const Camera& camera, float width, float height) {
    const float s = u.s;
    const HumanoidState& me = g.Player();
    const Station* station = g.DockedStation();
    const float w = 300.0f * s, h = 118.0f * s, x = 14.0f * s, y = 14.0f * s;
    u.Panel(x, y, w, h, "A PIE", station ? station->name.c_str() : "");
    const double speed = std::sqrt(me.velocity.x * me.velocity.x + me.velocity.z * me.velocity.z);
    const float ix = x + 16.0f * s, iw = w - 32.0f * s;
    Row(u, ix, y + 32.0f * s, iw, "Velocidad", Format("%.1f m/s", speed));
    Row(u, ix, y + 51.0f * s, iw, "Gravedad", Format("%.2f g", g.Rules().foot.gravity / 9.81));
    Row(u, ix, y + 70.0f * s, iw, "Altura", Format("%.2f m  %s", g.PersonRules().height, me.grounded ? "en el suelo" : "en el aire"));
    Row(u, ix, y + 89.0f * s, iw, "Vista", g.FirstPerson() ? "primera persona" : "tercera persona");

    // La gente lleva su nombre encima, de cerca.
    const Vec3d origin = g.DeckOrigin();
    for (const Person& p : g.Crew()) {
        Vec3d head = p.body.position;
        head.y += g.PersonRules().height + 0.35;
        const ScreenPoint at = camera.WorldToScreen(origin + DeckToWorld(head));
        Vec3d gap = p.body.position - me.position;
        if (!at.visible || Length(gap) > 18.0) continue;
        u.Center(at.x, at.y - 8.0f * s, 12.0f, kNeutral, p.name);
    }

    if (g.FirstPerson()) u.d->AddCircleFilled(ImVec2(width * 0.5f, height * 0.5f), 2.2f * s, kYellow);
    if (g.CanBoard()) u.Center(width * 0.5f, height * 0.62f, 15.0f, kYellow, "E   VOLVER A LA NAVE");
    else {
        const ScreenPoint door = camera.WorldToScreen(origin + DeckToWorld(g.Deck().airlock + Vec3d{0, 1.5, 0}));
        if (door.visible) u.Center(door.x, door.y - 20.0f * s, 12.0f, kOwn, "ESCLUSA");
    }

    struct Hint { const char* key; const char* text; };
    const Hint hints[] = {{"WASD", "Andar"}, {"Ratón", "Mirar"}, {"Mayús", "Correr"}, {"Espacio", "Saltar"}, {"V", "Vista"}, {"E", "A bordo"}, {"M", "Mapa"}};
    float total = 0, step = 0;
    for (const Hint& hint : hints) {
        u.Key(-1000.0f, 0.0f, hint.key, hint.text, &step);
        total += step;
    }
    float hx = (width - total) * 0.5f;
    for (const Hint& hint : hints) {
        u.Key(hx, height - 32.0f * s, hint.key, hint.text, &step);
        hx += step;
    }
}

// Las teclas que sirven ahora mismo, segun lo que este haciendo el jugador.
void KeyBar(const Ui& u, const GameSession& g, const TargetRef* target, float width, float height) {
    const float s = u.s;
    struct Hint { const char* key; const char* text; };
    std::vector<Hint> hints;
    const Kind kind = target ? target->kind : Kind::None;
    if (u.in.cursor) hints.push_back({"C", "Volver a los mandos"});
    else {
        hints.push_back({"WASD", "Rumbo"});
        hints.push_back({"Q E", "Girar"});
        hints.push_back({"R F", "Mando"});
    }
    hints.push_back({"Tab", "Objetivo"});
    hints.push_back({"T", "Opciones"});
    hints.push_back({"M", "Mapa"});
    if (Attackable(g, target)) {
        hints.push_back({"G", "Disparar"});
        hints.push_back({"B", g.Tracking() ? "Soltar rumbo" : "Alinear cañón"});
    }
    hints.push_back({"K", "Escudo"});
    hints.push_back({"Espacio", "Frenar"});
    hints.push_back({HyperUnlocked(g.Ship(), g.Rules().drive) ? "1 2 3 4" : HybridUnlocked(g.Ship()) ? "1 2 3" : "1 2", "Motor"});
    (void)kind;
    if (!u.in.cursor) hints.push_back({"C", "Cursor"});
    hints.push_back({"V", "Centrar vista"});
    hints.push_back({"H", "Ocultar"});

    float total = 0, step = 0;
    for (const Hint& h : hints) {
        u.Key(-1000.0f, 0.0f, h.key, h.text, &step);
        total += step;
    }
    const float y = height - 32.0f * s;
    u.d->AddRectFilledMultiColor(ImVec2((width - total) * 0.5f - 60.0f * s, y - 6.0f * s), ImVec2(width * 0.5f, y + 26.0f * s), Rgba(18, 10, 4, 0.0f), kPanel, kPanel, Rgba(18, 10, 4, 0.0f));
    u.d->AddRectFilledMultiColor(ImVec2(width * 0.5f, y - 6.0f * s), ImVec2((width + total) * 0.5f + 60.0f * s, y + 26.0f * s), kPanel, Rgba(18, 10, 4, 0.0f), Rgba(18, 10, 4, 0.0f), kPanel);
    float x = (width - total) * 0.5f;
    for (const Hint& h : hints) {
        u.Key(x, y, h.key, h.text, &step);
        x += step;
    }
}

// bottom: hasta donde puede bajar (encima del radar).
void DialogueBox(const Ui& u, const GameSession& g, float width, float bottom) {
    const Dialogue& talk = g.Conversation();
    const float s = u.s;
    const float w = std::clamp(width - (kLeftWidth + kRightWidth + 60.0f) * s, 520.0f * s, 820.0f * s);
    const float x = (width - w) * 0.5f;

    if (const DialogueLine* line = talk.Line()) {
        const float h = 128.0f * s, y = bottom - h - 8.0f * s;
        u.Panel(x, y, w, h, line->speaker.c_str(), "ENLACE // CANAL 1");

        // Retrato: un nucleo que late mientras habla.
        const float cx = x + 52.0f * s, cy = y + 66.0f * s, pulse = 0.5f + 0.5f * static_cast<float>(std::sin(u.t * (talk.LineComplete() ? 1.5 : 9.0)));
        u.d->AddCircleFilled(ImVec2(cx, cy), 30.0f * s, kPanelDeep, 48);
        u.d->AddCircle(ImVec2(cx, cy), 30.0f * s, kAmber, 48, 1.5f * s);
        u.d->AddCircleFilled(ImVec2(cx, cy), (8.0f + pulse * 7.0f) * s, Rgba(255, 170, 40, 0.65f), 32);
        u.d->AddCircleFilled(ImVec2(cx, cy), 4.0f * s, kYellow, 16);
        const float spin = static_cast<float>(u.t * 1.2);
        u.Arc(cx, cy, 23.0f * s, spin, spin + 2.0f, kAmberDim, 1.5f * s);
        u.Arc(cx, cy, 23.0f * s, spin + 3.14f, spin + 5.14f, kAmberDim, 1.5f * s);

        const std::string shown = talk.VisibleText();
        const float tx = x + 96.0f * s, wrap = w - 112.0f * s;
        const ImVec2 at(tx, y + 34.0f * s);
        const ImVec4 clip(x, y, x + w, y + h);
        u.d->AddText(u.text, 17.0f * s, ImVec2(at.x + 1.0f, at.y + 1.0f), kShadow, shown.c_str(), nullptr, wrap, &clip);
        u.d->AddText(u.text, 17.0f * s, at, kText, shown.c_str(), nullptr, wrap, &clip);

        const bool blink = std::fmod(u.t, 1.0) < 0.6;
        if (talk.LineComplete() && blink) u.Right(x + w - 16.0f * s, y + h - 22.0f * s, 12.0f, kYellow, "ENTER  continuar");
        if (u.Hover(x, y, w, h) && u.in.clicked) u.actions.push_back({HudActionType::NextDialogue});
    }

    // Debajo del nombre del sistema, que acaba en y = 104, y con la etiqueta en su propia linea.
    if (!talk.Objective().empty()) {
        const std::string text = talk.Objective();
        const float ow = std::max(u.Width(14.0f, text), u.Width(11.0f, "OBJETIVO")) + 40.0f * s, oh = 40.0f * s;
        const float ox = (width - ow) * 0.5f, oy = kObjectiveTop * s;
        u.d->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + ow, oy + oh), kPanel);
        u.d->AddRect(ImVec2(ox, oy), ImVec2(ox + ow, oy + oh), kAmberDim);
        u.d->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + 4.0f * s, oy + oh), kYellow);
        u.Spaced(ox + 16.0f * s, oy + 4.0f * s, 11.0f, kMuted, "OBJETIVO");
        u.Label(ox + 16.0f * s, oy + 19.0f * s, 14.0f, kYellow, text);
    }
}

void ScanOverlay(const Ui& u, const GameSession& g, float width, float height) {
    if (!g.Scanning()) return;
    const float s = u.s, cx = width * 0.5f, cy = height * 0.5f, r = 120.0f * s;
    const float progress = static_cast<float>(g.ScanProgress());
    u.Arc(cx, cy, r, -1.5708f, -1.5708f + kTau, kAmberFaint, 6.0f * s);
    u.Arc(cx, cy, r, -1.5708f, -1.5708f + kTau * progress, kYellow, 6.0f * s);
    for (int i = 0; i < 60; ++i) {
        const float a = kTau * static_cast<float>(i) / 60.0f + static_cast<float>(u.t * 0.3);
        const float wave = 8.0f + 12.0f * std::abs(std::sin(a * 7.0f + static_cast<float>(u.t * 6.0)));
        u.d->AddLine(ImVec2(cx + std::cos(a) * (r + 8.0f * s), cy + std::sin(a) * (r + 8.0f * s)), ImVec2(cx + std::cos(a) * (r + (8.0f + wave) * s), cy + std::sin(a) * (r + (8.0f + wave) * s)), i % 2 == 0 ? kAmberDim : Rgba(204, 132, 255, 0.45f));
    }
    u.Center(cx, cy + r + 34.0f * s, 14.0f, kYellow, Format("BARRIDO MICROONDAS / UV   %.0f %%", progress * 100.0f));
    u.Center(cx, cy + r + 54.0f * s, 12.0f, kMuted, Format("Alcance %s   nivel %d", Distance(g.ScanRange()).c_str(), g.Ship().scannerTier));
}

void Log(const Ui& u, const GameSession& g, float x, float y) {
    float line = y;
    for (const std::string& text : g.Log()) {
        u.Label(x, line, 12.5f, kText, text);
        line += 16.0f * u.s;
    }
}

// ---------------------------------------------------------------------------------------------
// Menu de inicio: una consola de navegacion, como la de Epsilon 0 pero mas cargada. A la izquierda
// la carta del sector y el archivo que se descifra; a la derecha, los botones; todo enmarcado.
// ---------------------------------------------------------------------------------------------

const ImU32 kHot = Rgba(255, 84, 24), kHotDim = Rgba(255, 84, 24, 0.38f), kCyan = Rgba(90, 225, 255), kCyanDim = Rgba(90, 225, 255, 0.32f);

// Numero entre 0 y 1 que sale siempre igual para el mismo n: el mapa no tiembla.
float Hash(int n) {
    unsigned x = static_cast<unsigned>(n) * 2654435761u + 0x9E3779B9u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return static_cast<float>(x & 0xFFFFFFu) / 16777215.0f;
}

// Los primeros n caracteres de un texto en UTF-8, sin partir una letra con tilde.
std::string FirstLetters(const std::string& text, int n) {
    std::size_t i = 0;
    for (int letters = 0; i < text.size() && letters < n; ++letters) {
        ++i;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) ++i;
    }
    return text.substr(0, i);
}

// Mayusculas en UTF-8, tildes y enes incluidas: en Latin-1, la minuscula es la mayuscula mas 0x20.
std::string Upper(const std::string& text) {
    std::string out = text;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(out[i]);
        if (c >= 'a' && c <= 'z') out[i] = static_cast<char>(c - 0x20);
        else if (c == 0xC3 && i + 1 < out.size()) {
            const unsigned char next = static_cast<unsigned char>(out[i + 1]);
            if (next >= 0xA0 && next <= 0xBE && next != 0xB7) out[i + 1] = static_cast<char>(next - 0x20);
            ++i;
        }
    }
    return out;
}

// Texto que salta de linea al llegar al ancho dado.
void Wrapped(const Ui& u, float x, float y, float size, float width, ImU32 color, const std::string& text) {
    u.d->AddText(u.text, size * u.s, ImVec2(std::floor(x) + 1.0f, std::floor(y) + 1.0f), kShadow, text.c_str(), nullptr, width);
    u.d->AddText(u.text, size * u.s, ImVec2(std::floor(x), std::floor(y)), color, text.c_str(), nullptr, width);
}

// El logotipo de Epsilon-Z, con su propia letra. Las letras mezcladas son a proposito: en Hangul
// asi se ve "3psilon-z". Se estira a lo ancho y se inclina hacia delante, como si fuera rapido.
// Se estira desde x, que es su borde izquierdo o, con centered, su centro.
void Logo(const Ui& u, float x, float y, float size, bool centered) {
    const std::string title = "3PSiLgn-Z";
    const float stretch = 1.35f, slant = 0.28f;
    const float width = u.WidthIn(u.logo, size, title);
    const float left = std::floor(centered ? x - width * 0.5f : x), top = std::floor(y);
    const float baseline = top + size * u.s * 0.8f;
    const auto draw = [&](float dx, float dy, ImU32 color) {
        const int first = u.d->VtxBuffer.Size;
        u.d->AddText(u.logo, size * u.s, ImVec2(left + dx, top + dy), color, title.c_str());
        for (int i = first; i < u.d->VtxBuffer.Size; ++i) {
            ImVec2& p = u.d->VtxBuffer[i].pos;
            p.x = x + (p.x - x) * stretch + (baseline - p.y) * slant;
        }
    };
    draw(4.0f, 4.0f, kShadow);
    draw(-2.0f, 0.0f, kHotDim);  // un eco rojo detras: la imagen vibra como en una pantalla vieja
    draw(0.0f, 0.0f, kAmber);
}

// Paralelogramo inclinado hacia delante, como el logotipo: la forma de los botones del menu.
void Slanted(const Ui& u, float x, float y, float w, float h, ImU32 fill, ImU32 edge, float thick) {
    const float lean = h * 0.35f;
    const ImVec2 p[4] = {{x + lean, y}, {x + w, y}, {x + w - lean, y + h}, {x, y + h}};
    u.d->AddConvexPolyFilled(p, 4, fill);
    u.d->AddPolyline(p, 4, edge, ImDrawFlags_Closed, thick);
}

// Boton grande del menu. Entra deslizandose, por orden, al abrir la pagina.
void MenuButton(const Ui& u, float x, float y, float w, float h, const std::string& caption, const std::string& detail, const char* key, HudAction action, int order, bool enabled = true) {
    const float s = u.s;
    const float arrive = std::clamp(static_cast<float>((u.in.pageClock - 0.05 * order) / 0.28), 0.0f, 1.0f);
    const float ease = 1.0f - (1.0f - arrive) * (1.0f - arrive);
    x += (1.0f - ease) * 90.0f * s;
    if (arrive <= 0.0f) return;
    const bool hover = enabled && u.Hover(x, y, w, h);
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(u.t * 5.0));
    Slanted(u, x, y, w, h, hover ? Rgba(255, 110, 24, 0.30f + 0.08f * pulse) : Rgba(40, 14, 4, 0.78f), !enabled ? kAmberFaint : hover ? kYellow : kAmberDim, (hover ? 2.0f : 1.2f) * s);
    // Una franja caliente en el borde de entrada, y un marcador delante del elegido.
    const float lean = h * 0.35f;
    u.d->AddQuadFilled(ImVec2(x + lean, y), ImVec2(x + lean + 6.0f * s, y), ImVec2(x + 6.0f * s, y + h), ImVec2(x, y + h), hover ? kYellow : kHot);
    if (hover) u.d->AddRectFilled(ImVec2(x - 18.0f * s, y + h * 0.5f - 5.0f * s), ImVec2(x - 8.0f * s, y + h * 0.5f + 5.0f * s), kYellow);
    const ImU32 color = !enabled ? kMuted : hover ? kYellow : kText;
    const bool twoLines = !detail.empty();
    u.Spaced(x + lean + 18.0f * s, y + (twoLines ? 8.0f : (h / s - 17.0f) * 0.5f) * s, 17.0f, color, caption);
    if (twoLines) u.Label(x + lean + 10.0f * s, y + h - 21.0f * s, 11.5f, hover ? kText : kMuted, detail);
    if (key) u.Right(x + w - lean - 6.0f * s, y + 9.0f * s, 11.5f, hover ? kYellow : kMuted, key);
    if (hover && u.in.clicked) u.actions.push_back(action);
}

// Fondo: un velo caliente, lineas de barrido y el marco holografico de Epsilon 0.
void MenuBackdrop(const Ui& u, float width, float height) {
    const float s = u.s;
    u.d->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, height), Rgba(46, 10, 2, 0.80f), Rgba(14, 4, 2, 0.50f), Rgba(10, 3, 2, 0.62f), Rgba(52, 12, 2, 0.86f));
    for (float y = 0.0f; y < height; y += 3.0f * s) u.d->AddLine(ImVec2(0, y), ImVec2(width, y), Rgba(0, 0, 0, 0.16f));
    const float band = static_cast<float>(std::fmod(u.t * 70.0, static_cast<double>(height) + 200.0)) - 100.0f;
    u.d->AddRectFilledMultiColor(ImVec2(0, band), ImVec2(width, band + 90.0f * s), Rgba(255, 120, 30, 0.0f), Rgba(255, 120, 30, 0.0f), Rgba(255, 120, 30, 0.07f), Rgba(255, 120, 30, 0.07f));

    const float x1 = 22.0f * s, y1 = 22.0f * s, x2 = width - 22.0f * s, y2 = height - 22.0f * s;
    const float arm = 200.0f * s, cut = 26.0f * s, thick = 3.0f * s;
    const auto corner = [&](float cx, float cy, float dx, float dy) {
        u.d->AddTriangleFilled(ImVec2(cx, cy), ImVec2(cx + cut * dx, cy), ImVec2(cx, cy + cut * dy), kHot);
        u.d->AddLine(ImVec2(cx + cut * dx, cy), ImVec2(cx + arm * dx, cy), kAmber, thick);
        u.d->AddLine(ImVec2(cx, cy + cut * dy), ImVec2(cx, cy + arm * dy), kAmber, thick);
    };
    corner(x1, y1, 1, 1);
    corner(x2, y1, -1, 1);
    corner(x1, y2, 1, -1);
    corner(x2, y2, -1, -1);
    u.d->AddLine(ImVec2(x1 + arm + 20.0f * s, y1 + 2.0f * s), ImVec2(x2 - arm - 20.0f * s, y1 + 2.0f * s), kAmberFaint);
    u.d->AddLine(ImVec2(x1 + arm + 20.0f * s, y2 - 2.0f * s), ImVec2(x2 - arm - 20.0f * s, y2 - 2.0f * s), kAmberFaint);
    for (float x = x1 + arm + 40.0f * s; x < x2 - arm - 20.0f * s; x += 32.0f * s) u.d->AddLine(ImVec2(x, y1 + 2.0f * s), ImVec2(x, y1 + 8.0f * s), kAmberDim);
}

// Carta del sector: anillos, un barrido de radar y los sistemas con su ruta. Es decorativa: los
// puntos salen de un numero fijo, no de la partida, para que el menu no revele nada.
void SectorMap(const Ui& u, float cx, float cy, float r) {
    const float s = u.s;
    const float t = static_cast<float>(u.t);
    u.d->AddCircleFilled(ImVec2(cx, cy), r, Rgba(20, 6, 2, 0.55f), 96);
    for (int ring = 1; ring <= 4; ++ring) u.d->AddCircle(ImVec2(cx, cy), r * static_cast<float>(ring) / 4.0f, ring == 4 ? kAmberDim : kAmberFaint, 96, ring == 4 ? 1.6f * s : 1.0f);
    u.d->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), kAmberFaint);
    u.d->AddLine(ImVec2(cx, cy - r), ImVec2(cx, cy + r), kAmberFaint);
    for (int i = 0; i < 72; ++i) {
        const float a = kTau * static_cast<float>(i) / 72.0f;
        const float inner = r - (i % 6 == 0 ? 12.0f : 5.0f) * s;
        u.d->AddLine(ImVec2(cx + std::cos(a) * inner, cy + std::sin(a) * inner), ImVec2(cx + std::cos(a) * r, cy + std::sin(a) * r), i % 6 == 0 ? kAmber : kAmberDim);
    }

    // El barrido deja una estela que se apaga.
    const float sweep = t * 0.7f;
    for (int k = 0; k < 28; ++k) {
        const float a0 = sweep - static_cast<float>(k) * 0.035f, a1 = a0 - 0.035f;
        u.d->AddTriangleFilled(ImVec2(cx, cy), ImVec2(cx + std::cos(a0) * r, cy + std::sin(a0) * r), ImVec2(cx + std::cos(a1) * r, cy + std::sin(a1) * r), Rgba(255, 96, 24, 0.20f * (1.0f - static_cast<float>(k) / 28.0f)));
    }
    u.d->AddLine(ImVec2(cx, cy), ImVec2(cx + std::cos(sweep) * r, cy + std::sin(sweep) * r), kYellow, 1.6f * s);

    constexpr int kNodes = 18;
    ImVec2 at[kNodes];
    for (int i = 0; i < kNodes; ++i) {
        const float a = Hash(i) * kTau + t * 0.015f;
        const float d = r * (0.12f + 0.82f * Hash(i + 50));
        at[i] = ImVec2(cx + std::cos(a) * d, cy + std::sin(a) * d);
    }
    // La ruta conocida: un tramo de sistemas unidos, con el destino al final.
    constexpr int route[] = {0, 3, 7, 11, 5, 14};
    for (int k = 0; k + 1 < static_cast<int>(sizeof(route) / sizeof(route[0])); ++k) {
        const ImVec2 a = at[route[k]], b = at[route[k + 1]];
        for (float f = 0.0f; f < 1.0f; f += 0.08f) {
            const float g = std::min(f + 0.045f, 1.0f);
            u.d->AddLine(ImVec2(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f), ImVec2(a.x + (b.x - a.x) * g, a.y + (b.y - a.y) * g), kCyanDim, 1.4f * s);
        }
    }
    for (int i = 0; i < kNodes; ++i) {
        const float a = std::atan2(at[i].y - cy, at[i].x - cx);
        const float behind = std::fmod(sweep - a + kTau * 8.0f, kTau);  // cuanto hace que paso el barrido
        const float lit = std::max(1.0f - behind / 1.2f, 0.0f);
        const float type = Hash(i + 100);
        const ImU32 base = type < 0.25f ? kCyan : type < 0.55f ? kAmber : type < 0.8f ? kHot : kVoid;
        const float size = (2.0f + 2.5f * Hash(i + 150) + 3.0f * lit) * s;
        u.d->AddCircleFilled(at[i], size, base, 12);
        if (lit > 0.05f) u.d->AddCircle(at[i], size + 6.0f * s * lit, Rgba(255, 228, 96, 0.6f * lit), 16, 1.0f);
        if (Hash(i + 200) > 0.45f) u.Label(at[i].x + 7.0f * s, at[i].y - 6.0f * s, 10.0f, kMuted, Format("SYS-%02d", static_cast<int>(Hash(i + 300) * 90.0f) + 1));
    }
    // El destino parpadea; la nave esta al principio de la ruta.
    const ImVec2 goal = at[route[5]], ship = at[route[0]];
    if (std::fmod(u.t, 1.0) < 0.6) {
        const float g = 10.0f * s;
        const ImVec2 q[4] = {{goal.x, goal.y - g}, {goal.x + g, goal.y}, {goal.x, goal.y + g}, {goal.x - g, goal.y}};
        u.d->AddPolyline(q, 4, kYellow, ImDrawFlags_Closed, 1.6f * s);
    }
    u.Label(goal.x - 26.0f * s, goal.y + 12.0f * s, 11.0f, kYellow, "DESTINO");
    u.d->AddTriangleFilled(ImVec2(ship.x, ship.y - 8.0f * s), ImVec2(ship.x - 6.0f * s, ship.y + 5.0f * s), ImVec2(ship.x + 6.0f * s, ship.y + 5.0f * s), kNeutral);
    u.Label(ship.x - 28.0f * s, ship.y + 10.0f * s, 11.0f, kNeutral, "TU NAVE");

    u.Spaced(cx - r, cy - r - 26.0f * s, 12.0f, kAmber, "CARTA DE SECTOR  N-42");
    u.Right(cx + r, cy - r - 24.0f * s, 11.0f, kMuted, Format("BARRIDO %03.0f°", std::fmod(sweep * 57.2958f, 360.0f)));
    u.Label(cx - r, cy + r + 10.0f * s, 11.0f, kMuted, Format("AR 14h %02dm   DEC -08° %02d'   ZOOM x%.1f", static_cast<int>(std::fmod(t * 0.7f, 60.0f)), static_cast<int>(std::fmod(t * 1.3f, 60.0f)), 1.0f + 0.5f * std::sin(t * 0.3f) + 0.5f));
}

// El archivo de Epsilon 0, que se descifra linea a linea al abrir el menu.
void ArchivePanel(const Ui& u, float x, float y, float w, float h) {
    const float s = u.s;
    u.Panel(x, y, w, h, "ARCHIVO", "N-42 // CLASIFICADO");
    static const char* const lines[] = {
        ">> ANÁLISIS SECTOR N-42",
        ">> CLASE: REMANENTE DE SUPERNOVA",
        ">> ESTADO: EXPANSIÓN ACTIVA",
        ">> COMPONENTES: H 70 %   He 28 %   Au",
        ">> EPSILON-0 ... EPSILON-Y: CERRADOS",
        ">> EPSILON-Z: ABIERTO",
    };
    constexpr int kLines = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
    const double typing = std::max(u.in.pageClock - 0.25, 0.0) * 46.0;  // letras por segundo
    int budget = static_cast<int>(typing);
    float line = y + 34.0f * s;
    for (int i = 0; i < kLines && budget > 0; ++i) {
        const std::string text = lines[i];
        const std::string shown = FirstLetters(text, budget);
        budget -= static_cast<int>(shown.size());
        const ImU32 color = i == kLines - 1 ? kYellow : i >= 3 ? kText : kAmber;
        u.Label(x + 16.0f * s, line, 12.5f, color, shown);
        if (shown.size() < text.size() && std::fmod(u.t, 0.5) < 0.25) u.Label(x + 16.0f * s + u.Width(12.5f, shown), line, 12.5f, kYellow, "_");
        line += 17.0f * s;
    }
    const float progress = std::clamp(static_cast<float>(typing / 190.0), 0.0f, 1.0f);
    u.Label(x + 16.0f * s, y + h - 30.0f * s, 11.0f, kMuted, progress < 1.0f ? "DESCIFRANDO" : "DESCIFRADO");
    u.Segments(x + 110.0f * s, y + h - 27.0f * s, w - 180.0f * s, 6.0f * s, progress, progress < 1.0f ? kHot : kNeutral, 24);
    u.Right(x + w - 16.0f * s, y + h - 30.0f * s, 11.0f, kText, Format("%.0f %%", progress * 100.0f));
}

// Columna de datos que corre: codigos del sector, como en un terminal.
void DataColumn(const Ui& u, float x, float y, int rows) {
    const float s = u.s;
    const bool blink = std::fmod(u.t, 1.2) < 0.8;
    u.Label(x, y, 11.5f, blink ? kAmber : kAmberDim, "SEC-09 [ESPERA]");
    const int tick = static_cast<int>(u.t * 3.0);
    for (int i = 0; i < rows; ++i) {
        const int n = tick + i;
        const ImU32 color = i == rows - 1 ? kYellow : Hash(n * 7) > 0.85f ? kHot : kMuted;
        u.Label(x, y + (16.0f + 14.0f * static_cast<float>(i)) * s, 10.5f, color,
                Format("0x%04X  %02X %02X %02X %02X  %s", static_cast<int>(Hash(n) * 65535.0f), static_cast<int>(Hash(n + 1) * 255.0f), static_cast<int>(Hash(n + 2) * 255.0f), static_cast<int>(Hash(n + 3) * 255.0f), static_cast<int>(Hash(n + 4) * 255.0f), Hash(n + 5) > 0.5f ? "OK" : "--"));
    }
}

// La configuracion: en el menu de inicio y en la pausa. Devuelve su alto.
float SettingsPanel(const Ui& u, float x, float y, float w, bool fromPause) {
    const float s = u.s;
    const GameSettings& settings = u.in.settings;
    const QualityProfile& profile = Profile(settings.quality);
    const float h = 336.0f * s;
    u.Panel(x, y, w, h, "AJUSTES DEL SISTEMA", "SE GUARDAN PARA TODAS LAS PARTIDAS");
    const float ix = x + 18.0f * s, iw = w - 36.0f * s;
    float line = y + 40.0f * s;

    u.Spaced(ix, line, 13.0f, kAmber, "CALIDAD GRÁFICA");
    line += 24.0f * s;
    const int count = static_cast<int>(GraphicsQuality::Count);
    const float gap = 8.0f * s, bw = (iw - gap * static_cast<float>(count - 1)) / static_cast<float>(count), bh = 40.0f * s;
    for (int i = 0; i < count; ++i) {
        const GraphicsQuality q = static_cast<GraphicsQuality>(i);
        const float bx = ix + (bw + gap) * static_cast<float>(i);
        const bool active = q == settings.quality;
        const bool hover = u.Hover(bx, line, bw, bh);
        Slanted(u, bx, line, bw, bh, active ? Rgba(255, 110, 24, 0.42f) : hover ? Rgba(255, 150, 40, 0.20f) : Rgba(40, 14, 4, 0.78f), active ? kYellow : hover ? kAmber : kAmberDim, (active ? 2.0f : 1.2f) * s);
        u.Center(bx + bw * 0.5f, line + 11.0f * s, 14.0f, active ? kYellow : hover ? kText : kMuted, Profile(q).name);
        if (hover && u.in.clicked) u.actions.push_back({HudActionType::Quality, i});
    }
    line += bh + 12.0f * s;
    Wrapped(u, ix, line, 12.5f, iw, kText, profile.description);
    line += 38.0f * s;
    u.Label(ix, line, 11.5f, kMuted, Format("Escena al %.0f %%   Bloom %s   Grano %s   Detalle %.0f", profile.renderScale * 100.0f, profile.bloom ? "sí" : "no", profile.film ? "sí" : "no", profile.maxOctaves));
    line += 30.0f * s;

    const auto toggle = [&](const char* name, bool on, HudActionType action) {
        const bool hover = u.Hover(ix, line, iw, 34.0f * s);
        u.d->AddRectFilled(ImVec2(ix, line), ImVec2(ix + iw, line + 34.0f * s), hover ? Rgba(255, 150, 40, 0.16f) : Rgba(255, 150, 40, 0.05f));
        u.d->AddRect(ImVec2(ix, line), ImVec2(ix + iw, line + 34.0f * s), hover ? kAmber : kAmberFaint);
        u.Spaced(ix + 12.0f * s, line + 9.0f * s, 13.0f, hover ? kYellow : kText, name);
        const float sw = 60.0f * s, sx = ix + iw - sw - 10.0f * s;
        Slanted(u, sx, line + 6.0f * s, sw, 22.0f * s, on ? Rgba(255, 110, 24, 0.55f) : Rgba(40, 14, 4, 0.9f), on ? kYellow : kAmberDim, 1.2f * s);
        u.Center(sx + sw * 0.5f, line + 9.0f * s, 12.5f, on ? kYellow : kMuted, on ? "SÍ" : "NO");
        if (hover && u.in.clicked) u.actions.push_back({action});
        line += 42.0f * s;
    };
    toggle("PANTALLA COMPLETA", settings.fullscreen, HudActionType::ToggleFullscreen);
    toggle("SINCRONIZACIÓN VERTICAL", settings.vsync, HudActionType::ToggleVsync);

    const float back = 220.0f * s;
    MenuButton(u, x + w - back - 18.0f * s, y + h - 52.0f * s, back, 38.0f * s, "VOLVER", "", "ESC", {HudActionType::MenuPage, static_cast<int>(MenuPage::Main)}, 0);
    if (!fromPause) u.Label(ix, y + h - 40.0f * s, 11.0f, kMuted, "Si va lento, prueba Baja o Media.");
    return h;
}

void CreditsPanel(const Ui& u, float x, float y, float w) {
    const float s = u.s, h = 330.0f * s;
    u.Panel(x, y, w, h, "CRÉDITOS", "EPSILON-Z");
    const float ix = x + 18.0f * s;
    float line = y + 40.0f * s;
    const auto row = [&](const char* role, const char* who) {
        u.Spaced(ix, line, 11.0f, kMuted, role);
        u.Label(ix, line + 16.0f * s, 15.0f, kText, who);
        line += 44.0f * s;
    };
    row("PROGRAMACIÓN Y DISEÑO", "Alumno UNAQ");
    row("LÍNEA DE JUEGOS", "Epsilon-0 a Epsilon-Z, sobre la Nebulosa Naranja");
    row("LETRAS", "Audiowide (Astigmatic), Rubik Glitch (NaN, Luke Prowse)");
    row("LOGOTIPO", "Hangul (Sébastien Hayez)");
    u.Label(ix, line, 11.0f, kMuted, "Las tres letras tienen licencia SIL Open Font License 1.1.");
    MenuButton(u, x + w - 238.0f * s, y + h - 52.0f * s, 220.0f * s, 38.0f * s, "VOLVER", "", "ESC", {HudActionType::MenuPage, static_cast<int>(MenuPage::Main)}, 0);
}

void Menu(const Ui& u, float width, float height) {
    const float s = u.s;
    MenuBackdrop(u, width, height);

    // Cabecera, como en Epsilon 0: el nombre arriba a la izquierda y una linea de estado.
    const float left = 70.0f * s;
    Logo(u, left, 44.0f * s, 64.0f, false);
    const float lineY = 132.0f * s;
    u.d->AddLine(ImVec2(left, lineY), ImVec2(left + 520.0f * s, lineY), kAmber, 3.0f * s);
    u.d->AddRectFilled(ImVec2(left + 520.0f * s, lineY - 5.0f * s), ImVec2(left + 530.0f * s, lineY + 5.0f * s), kHot);
    const std::string status = "SISTEMA: EN LÍNEA  //  NEBULOSA NARANJA  //  EPSILON-Z";
    u.Label(left, lineY + 12.0f * s, 12.5f, kMuted, status + (std::fmod(u.t, 1.0) < 0.5 ? "_" : ""));

    // Carta y archivo a la izquierda; datos arriba a la derecha. Ceden sitio en ventanas pequenas.
    const float columnX = width - 500.0f * s;
    const float archiveTop = height - 260.0f * s, mapTop = lineY + 82.0f * s;
    // La carta cabe entre la cabecera y el archivo, con sitio para su linea de coordenadas.
    const float mapR = std::min({height * 0.25f, (columnX - left) * 0.36f, 230.0f * s, (archiveTop - mapTop - 40.0f * s) * 0.5f});
    if (mapR > 90.0f * s) SectorMap(u, left + 30.0f * s + mapR, mapTop + mapR, mapR);
    const float archiveW = std::min(520.0f * s, columnX - left - 40.0f * s);
    if (archiveW > 300.0f * s) ArchivePanel(u, left, archiveTop, archiveW, 186.0f * s);
    DataColumn(u, width - 300.0f * s, 52.0f * s, 6);

    const float bx = columnX, bw = 430.0f * s;
    float by = height * 0.30f;
    switch (u.in.page) {
        case MenuPage::Main: {
            u.Spaced(bx, by - 30.0f * s, 12.0f, kAmber, "CONSOLA DE MANDO");
            int order = 0;
            const float bh = 62.0f * s, step = 76.0f * s;
            if (!u.in.savedGame.empty()) {
                MenuButton(u, bx, by, bw, bh, "CONTINUAR MISIÓN", u.in.savedGame, "ENTER", {HudActionType::LoadGame}, order++);
                by += step;
            }
            MenuButton(u, bx, by, bw, bh, "NUEVA MISIÓN", "Cinco modos, de Pacífico a Realista", "1-5", {HudActionType::MenuPage, static_cast<int>(MenuPage::NewGame)}, order++);
            by += step;
            MenuButton(u, bx, by, bw, bh, "CONFIGURACIÓN", std::string("Calidad gráfica: ") + Profile(u.in.settings.quality).name, nullptr, {HudActionType::MenuPage, static_cast<int>(MenuPage::Settings)}, order++);
            by += step;
            MenuButton(u, bx, by, bw, bh, "CRÉDITOS", "Quién hizo Epsilon-Z", nullptr, {HudActionType::MenuPage, static_cast<int>(MenuPage::Credits)}, order++);
            by += step;
            MenuButton(u, bx, by, bw, bh, "ABORTAR", "Salir del juego", "ESC", {HudActionType::Quit}, order++);
            break;
        }
        case MenuPage::NewGame: {
            u.Spaced(bx, by - 30.0f * s, 12.0f, kAmber, "NUEVA MISIÓN");
            if (!u.in.savedGame.empty()) u.Right(bx + bw, by - 29.0f * s, 11.0f, kHot, "SUSTITUYE A LA GUARDADA");
            const float bh = 58.0f * s, step = 66.0f * s;
            for (int i = 0; i < static_cast<int>(GameModeId::Count); ++i) {
                const GameMode& mode = ModeInfo(static_cast<GameModeId>(i));
                MenuButton(u, bx, by, bw, bh, Upper(mode.name), mode.description, nullptr, {HudActionType::StartMode, i}, i);
                u.Right(bx + bw - 26.0f * s, by + 9.0f * s, 12.0f, kMuted, Format("%d", i + 1));
                by += step;
            }
            MenuButton(u, bx + bw - 220.0f * s, by + 6.0f * s, 220.0f * s, 38.0f * s, "VOLVER", "", "ESC", {HudActionType::MenuPage, static_cast<int>(MenuPage::Main)}, 6);
            break;
        }
        case MenuPage::Settings:
            SettingsPanel(u, bx - 40.0f * s, by - 30.0f * s, bw + 50.0f * s, false);
            break;
        case MenuPage::Credits:
            CreditsPanel(u, bx - 40.0f * s, by - 30.0f * s, bw + 50.0f * s);
            break;
    }

    u.Label(left, height - 44.0f * s, 11.0f, kMuted, "EPSILON-Z  //  CONSTRUCCIÓN 0.Z  //  ENLACE ESTABLE");
    u.Right(width - 70.0f * s, height - 44.0f * s, 11.0f, kMuted, "Ratón o teclado  ·  ENTER continuar  ·  ESC atrás");
}

void Paused(const Ui& u, float width, float height) {
    const float s = u.s, cx = width * 0.5f, cy = height * 0.5f;
    u.d->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), Rgba(8, 4, 2, 0.6f));
    if (u.in.page == MenuPage::Settings) {
        const float w = 560.0f * s;
        SettingsPanel(u, cx - w * 0.5f, cy - 190.0f * s, w, true);
        return;
    }
    u.Title(cx - u.TitleWidth(44.0f, "PAUSA") * 0.5f, cy - 130.0f * s, 44.0f, kAmber, "PAUSA");
    u.Button(cx - 130.0f * s, cy - 44.0f * s, 260.0f * s, 38.0f * s, "CONTINUAR", nullptr, {HudActionType::Resume});
    u.Button(cx - 130.0f * s, cy + 4.0f * s, 260.0f * s, 38.0f * s, "GUARDAR", nullptr, {HudActionType::SaveGame});
    u.Button(cx - 130.0f * s, cy + 52.0f * s, 260.0f * s, 38.0f * s, "CONFIGURACIÓN", nullptr, {HudActionType::MenuPage, static_cast<int>(MenuPage::Settings)});
    u.Button(cx - 130.0f * s, cy + 100.0f * s, 260.0f * s, 38.0f * s, "GUARDAR Y SALIR", nullptr, {HudActionType::Quit});
}

// --- Taller: modulos y motor, atracada en una estacion propia ---------------------------------

std::string CostText(double metal, double silicate) {
    if (metal <= 0.0 && silicate <= 0.0) return "gratis";
    std::string text = Format("%.0f metal", std::max(metal, 0.0));
    if (silicate > 0.0) text += Format(" + %.0f silicato", silicate);
    return text;
}

void Workshop(const Ui& u, const GameSession& g, float width, float height) {
    const float s = u.s, w = 1100.0f * s, h = 640.0f * s;
    const float x = (width - w) * 0.5f, y = (height - h) * 0.5f;
    const ShipState& ship = g.Ship();
    const ShipDesign& design = CurrentShipDesign();
    u.d->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), Rgba(0, 0, 0, 0.45f));
    u.Panel(x, y, w, h, "TALLER", "MÓDULOS Y MOTOR");
    u.Label(x + 16.0f * s, y + 32.0f * s, 12.0f, kMuted, Format("Bodega: %.0f kt de metal, %.0f kt de silicato. Quitar un módulo devuelve la mitad de lo que costó.", ship.cargo.Get(Resource::Metal), ship.cargo.Get(Resource::Silicate)));

    // Motores de impulso: una tarjeta por modelo.
    u.Spaced(x + 16.0f * s, y + 56.0f * s, 12.0f, kYellow, "MOTOR DE IMPULSO");
    const float cardW = (w - 32.0f * s - 3.0f * 10.0f * s) / static_cast<float>(kImpulseEngineCount), cardH = 64.0f * s;
    const ImpulseEngineSpec& fitted = ImpulseEngine(ship.loadout.engine);
    for (int t = 1; t <= kImpulseEngineCount; ++t) {
        const ImpulseEngineSpec& e = ImpulseEngine(t);
        const float cx = x + 16.0f * s + static_cast<float>(t - 1) * (cardW + 10.0f * s), cy = y + 76.0f * s;
        const bool active = t == ship.loadout.engine;
        u.Button(cx, cy, cardW, cardH, "", nullptr, {HudActionType::ImpulseEngine, t}, !active, active);
        u.Label(cx + 10.0f * s, cy + 6.0f * s, 13.0f, active ? kYellow : kText, e.name);
        u.Label(cx + 10.0f * s, cy + 25.0f * s, 11.0f, kMuted, Format("x%.1f velocidad, x%.1f aceleración, x%.1f gasto", e.speed, e.accel, e.burn));
        u.Label(cx + 10.0f * s, cy + 42.0f * s, 11.0f, active ? kNeutral : kMuted, active ? std::string("Montado") : CostText(e.metal - fitted.metal * 0.5, e.silicate - fitted.silicate * 0.5));
    }

    // Ranuras: exteriores a la izquierda, interiores a la derecha de la lista.
    const float listY = y + 160.0f * s, rowH = 15.5f * s, colW = 330.0f * s;
    u.Spaced(x + 16.0f * s, listY - 18.0f * s, 12.0f, kYellow, "EXTERIORES");
    u.Spaced(x + 16.0f * s + colW + 12.0f * s, listY - 18.0f * s, 12.0f, kYellow, "INTERIORES");
    int rowExt = 0, rowInt = 0;
    for (std::size_t i = 0; i < design.slots.size(); ++i) {
        const ModuleSlot& slot = design.slots[i];
        const bool external = slot.kind == SlotKind::External;
        const float rx = x + 16.0f * s + (external ? 0.0f : colW + 12.0f * s);
        const float ry = listY + static_cast<float>(external ? rowExt++ : rowInt++) * rowH;
        const ShipModuleKind kind = i < ship.loadout.modules.size() ? ship.loadout.modules[i] : ShipModuleKind::None;
        const bool selected = static_cast<int>(i) == u.in.workshopSlot;
        const bool broken = !SlotWorking(ship, static_cast<int>(i));
        if (selected) u.d->AddRectFilled(ImVec2(rx - 4.0f * s, ry - 1.0f * s), ImVec2(rx + colW, ry + rowH - 1.0f * s), kAmberFaint);
        if (u.Hover(rx - 4.0f * s, ry - 1.0f * s, colW + 4.0f * s, rowH) && u.in.clicked) u.actions.push_back({HudActionType::WorkshopSlot, static_cast<int>(i)});
        u.Label(rx, ry, 11.0f, broken ? kHostile : selected ? kYellow : kText, PrettyName(slot.name));
        u.Right(rx + colW - 6.0f * s, ry, 11.0f, kind == ShipModuleKind::None ? kMuted : kNeutral, broken ? std::string("dañada") : ModuleSpec(kind).name);
    }

    // La ranura elegida: lo que se puede montar en ella.
    const int chosen = std::clamp(u.in.workshopSlot, 0, std::max(static_cast<int>(design.slots.size()) - 1, 0));
    if (design.slots.empty()) return;
    const ModuleSlot& slot = design.slots[static_cast<std::size_t>(chosen)];
    const float px = x + 16.0f * s + 2.0f * (colW + 12.0f * s), pw = x + w - 16.0f * s - px;
    float py = listY - 18.0f * s;
    u.Spaced(px, py, 12.0f, kYellow, "RANURA");
    py += 20.0f * s;
    u.Label(px, py, 14.0f, kText, PrettyName(slot.name));
    py += 20.0f * s;
    u.Label(px, py, 11.5f, kMuted, std::string(slot.kind == SlotKind::External ? "Exterior, " : "Interior, ") + "en " + (ZoneInfo(slot.zone).masculine ? "el " : "la ") + ZoneInfo(slot.zone).name + Format(" (%.0f %%)", Zone(ship.zones, static_cast<HullZone>(slot.zone)) * 100.0));
    py += 26.0f * s;
    const ShipModuleKind current = static_cast<std::size_t>(chosen) < ship.loadout.modules.size() ? ship.loadout.modules[static_cast<std::size_t>(chosen)] : ShipModuleKind::None;
    for (int k = 1; k < static_cast<int>(ShipModuleKind::Count); ++k) {
        const ShipModuleKind kind = static_cast<ShipModuleKind>(k);
        const ShipModuleSpec& spec = ModuleSpec(kind);
        if (spec.slot != slot.kind) continue;
        const bool active = kind == current;
        u.Button(px, py, pw, 52.0f * s, "", nullptr, {HudActionType::InstallModule, k, 0, chosen}, !active, active);
        u.Label(px + 10.0f * s, py + 5.0f * s, 13.0f, active ? kYellow : kText, spec.name);
        u.Right(px + pw - 10.0f * s, py + 6.0f * s, 11.0f, active ? kNeutral : kMuted, active ? std::string("Montado") : CostText(spec.metal, spec.silicate));
        u.Label(px + 10.0f * s, py + 26.0f * s, 11.0f, kMuted, spec.description);
        py += 58.0f * s;
    }
    if (current != ShipModuleKind::None) u.Button(px, py, pw, 30.0f * s, "QUITAR", nullptr, {HudActionType::InstallModule, 0, 0, chosen});

    // Lo que dice la nave al montar, y lo que cambia.
    const std::string last = g.Log().empty() ? std::string() : g.Log().back();
    u.Label(x + 16.0f * s, y + h - 46.0f * s, 12.0f, kText, last);
    u.Label(x + 16.0f * s, y + h - 26.0f * s, 11.0f, kMuted, Format("Bodega %.2f Mt   Antimateria %.0f kt   Impulso %.0f km/s   Torretas %d   Escáner x%.1f", CargoCapacity(ship, g.Rules().ship) / 1000.0, AntimatterCapacity(ship, g.Rules().ship), g.Rules().drive.impulseSpeed * fitted.speed, WorkingModules(ship, ShipModuleKind::Turret), 1.0 + kSensorBoost * WorkingModules(ship, ShipModuleKind::SensorArray)));
    u.Right(x + w - 16.0f * s, y + h - 26.0f * s, 11.0f, kMuted, "Rueda > Taller para cerrar");
}

// --- Estructura: el casco por zonas, visto desde arriba --------------------------------------

constexpr float kStructureWidth = 250.0f, kStructureHeight = 214.0f;

ImU32 IntegrityColor(double integrity, float alpha = 1.0f) {
    if (integrity <= 0.0) return Rgba(90, 70, 60, alpha * 0.6f);
    if (integrity > 0.66) return Rgba(120, 230, 110, alpha);
    if (integrity > 0.33) return Rgba(255, 210, 70, alpha);
    return Rgba(255, 70, 40, alpha);
}

// Silueta de la nave con cada zona de su color: verde entera, amarilla tocada, roja a punto de caer,
// gris perdida. Las zonas rojas (apagan o hacen estallar la nave) llevan borde rojo.
void StructurePanel(const Ui& u, const GameSession& g, float x, float y) {
    const float s = u.s, w = kStructureWidth * s, h = kStructureHeight * s;
    u.Panel(x, y, w, h, "ESTRUCTURA", "ZONAS");
    const HullZones& z = g.Ship().zones;
    const HullZones& heat = g.ZoneHeat();
    const float cx = x + w * 0.36f, cy = y + 30.0f * s + 76.0f * s;
    // px por metro: la maqueta cabe con 0,128; una nave cargada se ajusta al mismo alto.
    const ZoneLayout* layout = CustomZoneLayout();
    const float k = layout ? 70.0f * s / static_cast<float>(std::max({layout->extent.z, layout->extent.x * 2.2, 1.0})) : 0.128f * s;
    const auto at = [&](double mx, double mz) { return ImVec2(cx + static_cast<float>(mx) * k, cy - static_cast<float>(mz) * k); };
    const auto shape = [&](HullZone zone, double x0, double z0, double x1, double z1, bool round) {
        const int i = static_cast<int>(zone);
        const double integrity = z[static_cast<std::size_t>(i)];
        const bool lost = ZoneLost(z, i);
        const float flash = static_cast<float>(heat[static_cast<std::size_t>(i)]) * (0.5f + 0.5f * static_cast<float>(std::sin(u.t * 18.0)));
        const ImVec2 a = at(x0, z1), b = at(x1, z0);
        const float r = round ? std::min(b.x - a.x, b.y - a.y) * 0.5f : 1.5f * s;
        if (!lost) u.d->AddRectFilled(a, b, IntegrityColor(integrity, 0.55f + 0.45f * flash), r);
        const bool red = ZoneInfo(i).red != RedZone::None;
        u.d->AddRect(a, b, red ? Rgba(255, 40, 30, 0.95f) : lost ? Rgba(150, 120, 100, 0.5f) : Rgba(255, 220, 160, 0.35f), r, 0, (red ? 2.0f : 1.0f) * s);
    };
    if (layout) {
        // La planta de cada pieza, con el color de su zona; las grandes primero y el puente encima.
        for (const HullZone zone : {HullZone::WarpRing, HullZone::PortNacelle, HullZone::StarboardNacelle, HullZone::PortRadiator, HullZone::StarboardRadiator,
                                    HullZone::Hold1, HullZone::Hold2, HullZone::Hold3, HullZone::Hold4, HullZone::Hold5, HullZone::Hold6,
                                    HullZone::Spine, HullZone::Core, HullZone::Bridge, HullZone::Bow})
            for (const ZoneRect& r : layout->plan[static_cast<std::size_t>(zone)]) shape(zone, r.x0, r.z0, r.x1, r.z1, false);
    } else {
    shape(HullZone::WarpRing, -248, -140, 248, -100, true);
    shape(HullZone::PortNacelle, -205, -545, -95, -230, false);
    shape(HullZone::StarboardNacelle, 95, -545, 205, -230, false);
    shape(HullZone::PortRadiator, -84, -400, -56, -160, false);
    shape(HullZone::StarboardRadiator, 56, -400, 84, -160, false);
    for (int i = 0; i < 6; ++i) {
        const double side = i < 3 ? 1.0 : -1.0, row = static_cast<double>(i % 3);
        shape(static_cast<HullZone>(static_cast<int>(HullZone::Hold1) + i), side * 22.0, 175.0 - row * 70.0 - 60.0, side * 92.0, 175.0 - row * 70.0, false);
    }
    shape(HullZone::Spine, -14, -450, 14, 450, false);
    shape(HullZone::Core, -40, -340, 40, -260, true);
    shape(HullZone::Bridge, -45, 260, 45, 420, false);
    shape(HullZone::Bow, -60, 420, 60, 540, true);
    }

    // A la derecha, lo que queda de cada sistema.
    const float tx = x + w * 0.66f;
    float ty = y + 36.0f * s;
    const auto line = [&](const char* name, double value) {
        u.Label(tx, ty, 10.5f, kMuted, name);
        u.Label(tx, ty + 12.0f * s, 12.5f, IntegrityColor(value), Format("%.0f %%", std::clamp(value, 0.0, 1.0) * 100.0));
        ty += 30.0f * s;
    };
    line("EMPUJE", EngineOutput(z));
    line("CURVATURA", WarpIntegrity(z) < kWarpRingMinimum ? 0.0 : WarpIntegrity(z));
    line("RADIADORES", (RadiatorOutput(z) - 0.25) / 0.75);
    line("MANIOBRA", (SteeringOutput(z) - 0.4) / 0.6);
    line("PUNTERÍA", 1.0 / AimSpread(z));
    u.d->AddRect(ImVec2(x + 14.0f * s, y + h - 22.0f * s), ImVec2(x + 24.0f * s, y + h - 12.0f * s), Rgba(255, 40, 30), 0.0f, 0, 2.0f * s);
    u.Label(x + 30.0f * s, y + h - 25.0f * s, 10.5f, kMuted, "Zona roja: apaga o hace estallar");
}

// Cinematica de la nave apagada: franjas negras, la alarma, el parte de lo que se va apagando y
// la baliza que nadie contesta. Acaba en negro, justo antes de la pantalla final.
void ShutdownScene(const Ui& u, const GameSession& g, float width, float height) {
    const float s = u.s, age = static_cast<float>(g.WreckAge());
    const float bar = std::min(age / 1.6f, 1.0f) * height * 0.12f;
    u.d->AddRectFilled(ImVec2(0, 0), ImVec2(width, bar), Rgba(0, 0, 0));
    u.d->AddRectFilled(ImVec2(0, height - bar), ImVec2(width, height), Rgba(0, 0, 0));

    // Los primeros segundos la imagen tiembla: lineas de interferencia.
    if (age < 3.0f) {
        for (int i = 0; i < 9; ++i) {
            const float r = std::fmod(std::sin(static_cast<float>(i) * 12.9898f + std::floor(age * 20.0f) * 78.233f) * 43758.5453f, 1.0f);
            const float yy = std::fabs(r) * height;
            u.d->AddRectFilled(ImVec2(0, yy), ImVec2(width, yy + (1.0f + 3.0f * std::fabs(r)) * s), Rgba(255, 120, 60, 0.10f * (1.0f - age / 3.0f)));
        }
    }

    const bool bridge = g.FailedZone() == static_cast<int>(HullZone::Bridge);
    const std::string title = bridge ? "PUENTE DESTRUIDO" : "ESPINA PARTIDA";
    const float blink = age < 3.0f ? (std::fmod(age * 4.0f, 1.0f) < 0.6f ? 1.0f : 0.25f) : 1.0f;
    if (age > 0.3f) u.Title((width - u.TitleWidth(40.0f, title)) * 0.5f, bar + 30.0f * s, 40.0f, Rgba(255, 58, 40, blink), title);

    // El parte, letra a letra.
    struct Line {
        float start;
        const char* text;
        ImU32 color;
    };
    const Line lines[] = {
        {2.0f, bridge ? "Sin mando: el puente no responde." : "Los conductos de energía están cortados.", kOrange},
        {3.8f, "Pérdida de potencia principal. Motores sin respuesta.", kText},
        {5.6f, "Escudos caídos. Luces de emergencia.", kText},
        {7.4f, "Soporte vital: reserva para seis horas.", kText},
        {9.4f, "Baliza de socorro activada. Nadie responde.", Rgba(255, 90, 70)},
    };
    float ly = height - bar - 150.0f * s;
    for (const Line& l : lines) {
        if (age < l.start) break;
        const std::string full = l.text;
        std::size_t letters = static_cast<std::size_t>((age - l.start) * 38.0f), bytes = 0;
        while (bytes < full.size() && letters > 0) {
            ++bytes;
            while (bytes < full.size() && (static_cast<unsigned char>(full[bytes]) & 0xC0) == 0x80) ++bytes;
            --letters;
        }
        const std::string shown = full.substr(0, bytes) + (bytes < full.size() && std::fmod(age * 3.0f, 1.0f) < 0.5f ? "_" : "");
        u.Label(width * 0.5f - u.Width(15.0f, full) * 0.5f, ly, 15.0f, l.color, shown);
        ly += 24.0f * s;
    }
    // A negro.
    const float fade = std::clamp((age - 11.5f) / 2.5f, 0.0f, 1.0f);
    if (fade > 0.0f) u.d->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), Rgba(0, 0, 0, fade));
}

void GameOver(const Ui& u, const GameSession& g, float width, float height) {
    const float s = u.s, cx = width * 0.5f, cy = height * 0.5f;
    const GameMode& mode = ModeInfo(g.Mode());
    u.d->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), Rgba(8, 2, 1, g.ShutDown() ? 0.88f : 0.72f));
    const std::string title = g.ShutDown() ? "NAVE APAGADA" : "NAVE PERDIDA";
    u.Title(cx - u.TitleWidth(52.0f, title) * 0.5f, cy - 130.0f * s, 52.0f, kHostile, title);
    if (!g.LossReason().empty()) u.Center(cx, cy - 80.0f * s, 15.0f, kOrange, g.LossReason());
    u.Center(cx, cy - 56.0f * s, 16.0f, kText, mode.permadeath ? "No queda nada. Una nueva tripulación empezará de cero." : mode.cargoLoss > 0.0 ? Format("Vuelves al último punto de control y pierdes el %.0f %% de la bodega.", mode.cargoLoss * 100.0) : std::string("Vuelves al último punto de control sin perder la carga."));
    u.Button(cx - 150.0f * s, cy, 300.0f * s, 42.0f * s, mode.permadeath ? "EMPEZAR DE NUEVO" : "CONTINUAR", nullptr, {HudActionType::Continue});
    u.Center(cx, cy + 52.0f * s, 12.5f, kMuted, "Pulsa ENTER");
}

}  // namespace

AtlasLayout ComputeAtlas(int width, int height, double zoom) {
    AtlasLayout l;
    l.scale = std::clamp(static_cast<float>(height) / 900.0f, 0.7f, 2.0f);
    const float s = l.scale;
    l.w = std::min(1560.0f * s, static_cast<float>(width) - 28.0f * s);
    l.h = std::min(840.0f * s, static_cast<float>(height) - 28.0f * s);
    l.x = (static_cast<float>(width) - l.w) * 0.5f;
    l.y = (static_cast<float>(height) - l.h) * 0.5f;
    l.listX = l.x + 16.0f * s;
    l.listW = 330.0f * s;
    l.listY = l.y + 36.0f * s;
    l.listH = l.h - 66.0f * s;
    l.infoW = 410.0f * s;
    l.infoX = l.x + l.w - l.infoW - 16.0f * s;
    l.viewX = l.listX + l.listW + 14.0f * s;
    l.viewW = l.infoX - 14.0f * s - l.viewX;
    l.viewY = l.listY + 34.0f * s;
    l.viewH = l.y + l.h - 16.0f * s - l.viewY;
    const float room = std::min(l.viewW, l.viewH);
    l.miniSize = static_cast<int>(std::clamp(room * 0.86f * static_cast<float>(zoom), 12.0f, room));
    l.miniX = static_cast<int>(l.viewX + (l.viewW - static_cast<float>(l.miniSize)) * 0.5f);
    l.miniY = static_cast<int>(l.viewY + (l.viewH - static_cast<float>(l.miniSize)) * 0.5f);
    return l;
}

AnalysisLayout ComputeAnalysis(int width, int height) {
    AnalysisLayout l;
    l.scale = std::clamp(static_cast<float>(height) / 900.0f, 0.7f, 2.0f);
    l.w = std::min(1120.0f * l.scale, static_cast<float>(width) - 40.0f * l.scale);
    l.h = std::min(760.0f * l.scale, static_cast<float>(height) - 60.0f * l.scale);
    l.x = (static_cast<float>(width) - l.w) * 0.5f;
    l.y = (static_cast<float>(height) - l.h) * 0.5f;
    l.miniSize = static_cast<int>(230.0f * l.scale);
    l.miniX = static_cast<int>(l.x + l.w * 0.23f - static_cast<float>(l.miniSize) * 0.5f);
    l.miniY = static_cast<int>(l.y + 88.0f * l.scale);
    return l;
}

GameHud::~GameHud() { Shutdown(); }

bool GameHud::Initialize(SdlGpuBackend& backend, const std::string& fontFolder) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Fuentes con licencia OFL, que permite incluirlas en el juego: Audiowide para leer y para los
    // titulos, Rubik Glitch para lo alienigena y Hangul para el logotipo. Sus licencias van en la
    // misma carpeta.
    const auto load = [&](const char* file, float size) -> ImFont* {
        const std::string path = fontFolder + "/" + file;
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path.c_str(), &info)) {
            std::printf("Aviso: no se encontro %s\n", path.c_str());
            return nullptr;
        }
        return io.Fonts->AddFontFromFileTTF(path.c_str(), size);
    };
    ImFont* text = load("Audiowide-Regular.ttf", 24.0f);
    ImFont* title = load("Audiowide-Regular.ttf", 64.0f);
    ImFont* alien = load("RubikGlitch-Regular.ttf", 32.0f);
    ImFont* logo = load("Hangul.otf", 96.0f);  // solo el EPSILON del menu
    if (!text) text = io.Fonts->AddFontDefault();
    if (!title) title = text;
    if (!alien) alien = text;
    if (!logo) logo = title;
    textFont_ = text;
    titleFont_ = title;
    alienFont_ = alien;
    logoFont_ = logo;

    ImGui_ImplSDLGPU3_InitInfo init{};
    init.Device = backend.Device();
    init.ColorTargetFormat = static_cast<SDL_GPUTextureFormat>(backend.TargetFormat());
    initialized_ = ImGui_ImplSDLGPU3_Init(&init);
    if (!initialized_) ImGui::DestroyContext();
    return initialized_;
}

void GameHud::Shutdown() {
    if (!initialized_) return;
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

std::vector<HudAction> GameHud::TakeActions() {
    std::vector<HudAction> out;
    out.swap(actions_);
    return out;
}

void GameHud::Prepare(SDL_GPUCommandBuffer* cmd) {
    hasDrawData_ = false;
    if (!initialized_ || !session_ || !camera_) return;
    const int width = camera_->Width(), height = camera_->Height();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();

    const float s = std::clamp(static_cast<float>(height) / 900.0f, 0.7f, 2.0f);
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    // Con la rueda o el mapa abiertos, los paneles de debajo no responden al raton.
    HudInput panels = input_;
    if (input_.wheel || input_.atlas.open) {
        panels.clicked = false;
        panels.cursor = false;
    }
    ImFont* const textFont = static_cast<ImFont*>(textFont_);
    ImFont* const titleFont = static_cast<ImFont*>(titleFont_);
    ImFont* const alienFont = static_cast<ImFont*>(alienFont_);
    ImFont* const logoFont = static_cast<ImFont*>(logoFont_);
    const Ui u{ImGui::GetBackgroundDrawList(), s, input_.clock, textFont, titleFont, alienFont, logoFont, panels, actions_};
    const Ui top{ImGui::GetBackgroundDrawList(), s, input_.clock, textFont, titleFont, alienFont, logoFont, input_, actions_};
    const GameSession& g = *session_;

    if (input_.screen == HudScreen::Menu) {
        Menu(u, w, h);
    } else if (g.Phase() == GamePhase::Adrift) {
        ShutdownScene(u, g, w, h);
    } else if (g.Phase() == GamePhase::Exploding) {
        const float alarm = 0.5f + 0.5f * static_cast<float>(std::sin(input_.clock * 8.0));
        const std::string text = g.FailedZone() == static_cast<int>(HullZone::Core) ? "FALLO DE CONTENCIÓN" : "RUPTURA DE CASCO";
        u.Title((w - u.TitleWidth(40.0f, text)) * 0.5f, 90.0f * s, 40.0f, Rgba(255, 58, 40, 0.5f + alarm * 0.5f), text);
    } else if (g.Phase() == GamePhase::GameOver) {
        GameOver(u, g, w, h);
    } else if (g.OnFoot()) {
        FootHud(u, g, *camera_, w, h);
        Log(u, g, 16.0f * s, h - 44.0f * s - 16.0f * s * static_cast<float>(g.Log().size()));
        if (input_.atlas.open) AtlasWindow(top, g, input_.atlas, ComputeAtlas(width, height, input_.atlas.zoom));
        if (input_.screen == HudScreen::Paused) Paused(u, w, h);
    } else {
        FlightPath(u, g, *camera_);
        if (input_.trajectory) Trajectory(u, g, *camera_);
        Markers(u, g, *camera_, target_);
        TurnRing(u, g, *camera_, w, h);

        ScanOverlay(u, g, w, h);
        Compass(u, g, target_, w);
        ShipPanel(u, g, 14.0f * s, 14.0f * s);
        CargoPanel(u, g, 14.0f * s, (kShipPanelHeight + 24.0f) * s);
        FlightStatus(u, g, w, 18.0f * s, (kShipPanelHeight + 24.0f + 146.0f + 14.0f + 18.0f * static_cast<float>(std::min(static_cast<int>(g.Stations().All().size()), 5))) * s);
        ContactList(u, g, target_, w - (kRightWidth + 14.0f) * s, 14.0f * s);
        const float listH = (40.0f + 18.0f * static_cast<float>(std::min<std::size_t>(g.Contacts().size(), 9))) * s;
        if (target_ && target_->kind != Kind::None) TargetPanel(u, g, *target_, w - (kRightWidth + 14.0f) * s, 24.0f * s + listH);
        Log(u, g, 16.0f * s, h - 44.0f * s - 16.0f * s * static_cast<float>(g.Log().size()));
        // El radar, siempre abajo en el centro, encima de las teclas.
        const float radarY = h - (input_.showKeys ? 44.0f : 30.0f) * s - kRadarHeight * s;
        Radar3D(u, g, target_, (w - kRadarWidth * s) * 0.5f, radarY);
        StructurePanel(u, g, w - (kStructureWidth + 14.0f) * s, radarY + (kRadarHeight - kStructureHeight) * s);
        DialogueBox(u, g, w, radarY);
        if (g.InHeatZone()) {
            const std::string text = "CALOR: EL BLINDAJE NO AGUANTA ESTA ÓRBITA";
            u.Center(w * 0.5f, 190.0f * s, 15.0f, kHostile, text);
        }
        if (g.Overheated()) {
            const float blink = 0.55f + 0.45f * static_cast<float>(std::sin(input_.clock * 6.0));
            u.Center(w * 0.5f, 284.0f * s, 15.0f, Rgba(255, 90, 40, blink), "SOBRECALENTAMIENTO: ESCUDOS Y ARMAS FUERA DE SERVICIO");
        } else if (g.Ramming() && g.Ship().defense.charge[static_cast<std::size_t>(ShieldKind::Ablative)] <= 0.0) {
            u.Center(w * 0.5f, 284.0f * s, 14.0f, kOrange, "PRESIÓN DE ARIETE: SIN CONOS, EL CASCO SUFRE");
        }
        if (g.Docking() == DockPhase::Approaching) u.Center(w * 0.5f, 236.0f * s, 14.0f, kNeutral, "HAZ DE ATRAQUE");
        else if (g.Docked()) {
            const Station* station = g.DockedStation();
            u.Center(w * 0.5f, 236.0f * s, 14.0f, kNeutral, "ATRACADA EN " + (station ? station->name : std::string()));
            u.Center(w * 0.5f, 258.0f * s, 13.5f, kText, "R o F sueltan las amarras.  En la rueda: A pie");
        } else if (const auto& near = g.Approach()) {
            // Cerca de una estacion la velocidad esta limitada: se dice cuanto, y como atracar.
            const std::string limit = near->top < 1.0 ? Format("%.0f m/s", near->top * 1000.0) : Format("%.1f km/s", near->top);
            if (near->zone) {
                u.Center(w * 0.5f, 236.0f * s, 14.0f, kNeutral, "ZONA DE ATRAQUE   máx " + limit);
                u.Center(w * 0.5f, 258.0f * s, 13.5f, kText, "Atracar, en la rueda (T).  Sin mando, la nave se queda a su lado");
            } else {
                u.Center(w * 0.5f, 236.0f * s, 12.5f, kMuted, Format("Aproximación a %s   %s   máx %s", near->station.c_str(), Distance(near->distance).c_str(), limit.c_str()));
            }
        }
        int incoming = 0;
        double nearest = 1.0e300;
        for (const Projectile& p : g.Projectiles()) {
            const double away = Length(p.position - g.Ship().position);
            if (p.team == 0 || away >= 1.0e6) continue;
            ++incoming;
            nearest = std::min(nearest, away);
        }
        if (incoming > 0) {
            const std::string fire = Format("FUEGO ENTRANTE: %d  A %s", incoming, Distance(nearest).c_str());
            u.LabelIn(u.alien, w * 0.5f - u.WidthIn(u.alien, 16.0f, fire) * 0.5f, 212.0f * s, 16.0f, kHostile, fire);
        }
        if (input_.showKeys) KeyBar(u, g, target_, w, h);
        else u.Label(16.0f * s, h - 24.0f * s, 12.0f, kMuted, "H  mostrar teclas");
        if (panels.cursor) {
            u.Center(w * 0.5f, kCursorNotice * s, 12.5f, kYellow, "CURSOR ACTIVO");
        }
        if (input_.analysis && target_) AnalysisWindow(u, g, *target_, ComputeAnalysis(width, height));
        if (input_.atlas.open) AtlasWindow(top, g, input_.atlas, ComputeAtlas(width, height, input_.atlas.zoom));
        if (input_.workshop && g.CanRefit()) Workshop(top, g, w, h);
        if (input_.wheel) OptionWheel(top, g, target_, w, h);
        if (input_.screen == HudScreen::Paused) Paused(u, w, h);
    }

    if (input_.consoleOpen && input_.console) {
        u.d->AddRectFilled(ImVec2(14.0f * s, h - 74.0f * s), ImVec2(600.0f * s, h - 46.0f * s), kPanel);
        u.d->AddRect(ImVec2(14.0f * s, h - 74.0f * s), ImVec2(600.0f * s, h - 46.0f * s), kAmber);
        u.Label(24.0f * s, h - 68.0f * s, 14.0f, kYellow, "> " + *input_.console + "_");
    }

    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
    hasDrawData_ = true;
}

void GameHud::Draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) {
    if (hasDrawData_) ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass);
}

}  // namespace game
