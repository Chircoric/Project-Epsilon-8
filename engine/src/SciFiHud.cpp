#include "epsilon/SciFiHud.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>

#include "epsilon/BlackHole.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace eps {

namespace {

using namespace astro;

constexpr float kTau = 6.2831853f;

// Paleta: cian frio para la estructura y ambar para lo seleccionado.
ImU32 Rgba(int r, int g, int b, float a = 1.0f) { return IM_COL32(r, g, b, static_cast<int>(a * 255.0f)); }
const ImU32 kLine = Rgba(90, 210, 240, 0.85f);
const ImU32 kLineDim = Rgba(90, 210, 240, 0.28f);
const ImU32 kText = Rgba(205, 240, 250);
const ImU32 kTextDim = Rgba(120, 175, 195);
const ImU32 kAccent = Rgba(255, 185, 70);
const ImU32 kPanel = Rgba(5, 14, 24, 0.80f);
const ImU32 kShadow = Rgba(0, 0, 0, 0.75f);

ImU32 FromVec(const Vec3f& c, float alpha = 1.0f) {
    return Rgba(static_cast<int>(c.x * 255.0f), static_cast<int>(c.y * 255.0f), static_cast<int>(c.z * 255.0f), alpha);
}

std::string Format(const char* format, ...) {
    char text[160];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

std::string Distance(double km) {
    if (km >= 0.1 * kLightYear) return Format("%.2f al", km / kLightYear);
    if (km >= 0.05 * kAstronomicalUnit) return Format("%.3f UA", km / kAstronomicalUnit);
    if (km >= 1.0e6) return Format("%.2f M km", km / 1.0e6);
    return Format("%.0f km", km);
}

std::string Duration(double seconds) {
    const double abs = std::abs(seconds);
    if (abs >= 2.0 * kYear) return Format("%.2f anos", seconds / kYear);
    if (abs >= 2.0 * kDay) return Format("%.2f dias", seconds / kDay);
    if (abs >= kHour) return Format("%.2f h", seconds / kHour);
    return Format("%.1f s", seconds);
}

std::string TimeRate(double scale) {
    if (scale >= kYear) return Format("%.3g ANOS/S", scale / kYear);
    if (scale >= kDay) return Format("%.3g DIAS/S", scale / kDay);
    if (scale >= kHour) return Format("%.3g H/S", scale / kHour);
    return Format("%.3g S/S", scale);
}

class Canvas {
public:
    explicit Canvas(ImDrawList* list, float scale) : d(list), s(scale) {}

    ImDrawList* d;
    float s;

    void Text(float x, float y, float size, ImU32 color, const std::string& text) const {
        const ImVec2 at(std::floor(x), std::floor(y));
        d->AddText(nullptr, size * s, ImVec2(at.x + 1.0f, at.y + 1.0f), kShadow, text.c_str());
        d->AddText(nullptr, size * s, at, color, text.c_str());
    }
    float Width(float size, const std::string& text) const {
        return ImGui::GetFont()->CalcTextSizeA(size * s, 1.0e6f, 0.0f, text.c_str()).x;
    }
    void TextRight(float x, float y, float size, ImU32 color, const std::string& text) const { Text(x - Width(size, text), y, size, color, text); }

    // Rectangulo con dos esquinas cortadas.
    void Plate(float x, float y, float w, float h, ImU32 fill, ImU32 border, float cut) const {
        const ImVec2 p[6] = {{x + cut, y}, {x + w, y}, {x + w, y + h - cut}, {x + w - cut, y + h}, {x, y + h}, {x, y + cut}};
        d->AddConvexPolyFilled(p, 6, fill);
        d->AddPolyline(p, 6, Rgba(90, 210, 240, 0.10f), 4.0f * s, ImDrawFlags_Closed);  // brillo
        d->AddPolyline(p, 6, border, 1.0f * s, ImDrawFlags_Closed);
    }

    void Heading(float x, float y, float w, const std::string& title) const {
        Text(x, y, 12.5f, kLine, title);
        const float start = x + Width(12.5f, title) + 8.0f * s;
        d->AddLine(ImVec2(start, y + 8.0f * s), ImVec2(x + w, y + 8.0f * s), kLineDim, 1.0f);
        d->AddRectFilled(ImVec2(x + w - 4.0f * s, y + 6.0f * s), ImVec2(x + w, y + 10.0f * s), kLine);
    }

    void Row(float x, float y, float w, const char* name, const std::string& value) const {
        Text(x, y, 13.0f, kTextDim, name);
        TextRight(x + w, y, 13.0f, kText, value);
    }

    // Barra segmentada.
    void Meter(float x, float y, float w, const char* name, double value, ImU32 color, const std::string& text) const {
        Text(x, y, 12.0f, kTextDim, name);
        const float left = x + 86.0f * s, right = x + w - 58.0f * s;
        const int segments = 18;
        const float step = (right - left) / static_cast<float>(segments);
        const int on = static_cast<int>(std::ceil(std::clamp(value, 0.0, 1.0) * segments));
        for (int i = 0; i < segments; ++i) {
            const float a = left + step * static_cast<float>(i);
            d->AddRectFilled(ImVec2(a, y + 4.0f * s), ImVec2(a + step - 2.0f * s, y + 11.0f * s), i < on ? color : Rgba(90, 210, 240, 0.10f));
        }
        TextRight(x + w, y, 12.0f, kText, text);
    }

    void Diamond(float x, float y, float r, ImU32 color, bool filled) const {
        const ImVec2 a(x, y - r), b(x + r, y), c(x, y + r), e(x - r, y);
        if (filled) d->AddQuadFilled(a, b, c, e, color);
        else d->AddQuad(a, b, c, e, color, 1.2f);
    }

    // Cuatro esquinas alrededor de un punto.
    void Brackets(float x, float y, float r, float arm, ImU32 color) const {
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                const float cx = x + r * static_cast<float>(sx), cy = y + r * static_cast<float>(sy);
                d->AddLine(ImVec2(cx, cy), ImVec2(cx - arm * static_cast<float>(sx), cy), color, 1.5f);
                d->AddLine(ImVec2(cx, cy), ImVec2(cx, cy - arm * static_cast<float>(sy)), color, 1.5f);
            }
        }
    }

    void Frame(float x, float y, float w, float h) const {
        d->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), Rgba(2, 8, 14, 0.75f));
        d->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), kLineDim);
        for (int i = 1; i < 4; ++i) {
            const float gx = x + w * static_cast<float>(i) / 4.0f, gy = y + h * static_cast<float>(i) / 4.0f;
            d->AddLine(ImVec2(gx, y), ImVec2(gx, y + h), Rgba(90, 210, 240, 0.07f));
            d->AddLine(ImVec2(x, gy), ImVec2(x + w, gy), Rgba(90, 210, 240, 0.07f));
        }
    }
};

const char* StarTitle(StarClass kind) {
    switch (kind) {
        case StarClass::O: return "Azul, clase O";
        case StarClass::B: return "Azul blanca, clase B";
        case StarClass::A: return "Blanca, clase A";
        case StarClass::F: return "Blanco amarilla, clase F";
        case StarClass::G: return "Amarilla, clase G";
        case StarClass::K: return "Naranja, clase K";
        case StarClass::M: return "Enana roja, clase M";
        default: return Info(kind).name;
    }
}

const char* KindName(const Body& b, const StarSystemData& system) {
    const StarData& star = system.StarAt(b.member);
    if (b.isStar && star.kind == StarClass::BlackHole) return star.blackHoleSpin > 0.0 ? "Agujero negro de Kerr" : "Agujero negro de Schwarzschild";
    return b.isStar ? StarTitle(star.kind) : Name(b.kind);
}

Vec3f BodyColor(const Body& b, const StarSystemData& system) {
    if (b.isStar) return system.StarAt(b.member).kind == StarClass::BlackHole ? Vec3f{1.0f, 0.62f, 0.32f} : BlackbodyColor(system.StarAt(b.member).temperature);
    switch (b.kind) {
        case PlanetClass::Asteroid: return {0.82f, 0.72f, 0.60f};
        case PlanetClass::Belt: return {0.95f, 0.80f, 0.55f};
        case PlanetClass::Lava: return {1.0f, 0.5f, 0.2f};
        case PlanetClass::Terran: return {0.45f, 0.95f, 0.6f};
        case PlanetClass::Ice: return {0.75f, 0.9f, 1.0f};
        case PlanetClass::GasGiant: case PlanetClass::HotJupiter: return {1.0f, 0.82f, 0.55f};
        case PlanetClass::IceGiant: return {0.5f, 0.85f, 1.0f};
        case PlanetClass::Desert: return {0.95f, 0.7f, 0.45f};
        case PlanetClass::Comet: return {0.6f, 0.9f, 0.9f};
        case PlanetClass::Moon: return {0.72f, 0.74f, 0.8f};
        default: return {0.8f, 0.76f, 0.72f};
    }
}

std::string ZoneText(const WorldCatalog& w, const Body& b, const BodyData& data) {
    const StarSystemData& system = *w.systems[static_cast<std::size_t>(b.system)];
    const bool moon = b.kind == PlanetClass::Moon;
    const BodyData& anchor = moon ? system.bodies[static_cast<std::size_t>(data.parent)] : data;
    const double a = anchor.orbit.semiMajorAxis;
    const StarData& s = system.HostOf(anchor);
    if (a < s.habitableInner) return "ZONA CALIENTE";
    if (a <= s.habitableOuter) return "ZONA HABITABLE";
    if (a < s.snowLine) return "ZONA FRIA";
    return "MAS ALLA DEL HIELO";
}

// Dos vistas de la orbita: desde arriba y de canto.
void OrbitPlanes(const Canvas& c, const WorldCatalog& w, const HudFrame& f, const Body& b, float x, float y, float width, float height) {
    const KeplerOrbit& orbit = w.orbits[static_cast<std::size_t>(b.orbit)];
    constexpr int kPoints = 96;
    std::vector<Vec3d> points(kPoints);
    double reach = 1.0, reachH = 1.0, reachV = 1.0;
    const double node = orbit.elements.ascendingNode;
    const Vec3d nodeAxis{std::cos(node), 0.0, std::sin(node)};
    for (int i = 0; i < kPoints; ++i) {
        points[static_cast<std::size_t>(i)] = OrbitPositionFromEccentric(orbit, 2.0 * kPi * i / kPoints);
        const Vec3d& p = points[static_cast<std::size_t>(i)];
        reach = std::max({reach, std::abs(p.x), std::abs(p.z)});
        reachH = std::max(reachH, std::abs(Dot(p, nodeAxis)));
        reachV = std::max(reachV, std::abs(p.y));
    }

    const float gap = 8.0f * c.s;
    const float w1 = (width - gap) * 0.5f;
    const float x2 = x + w1 + gap;
    const float title = 16.0f * c.s;
    c.Text(x, y, 11.5f, kTextDim, "PLANO SUPERIOR  X-Z");
    c.Text(x2, y, 11.5f, kTextDim, "PLANO LATERAL");
    const float top = y + title;
    const float h = height - title;
    c.Frame(x, top, w1, h);
    c.Frame(x2, top, w1, h);

    const Vec3d now = OrbitPositionFromEccentric(orbit, f.selectedAnomaly);
    const float half = std::min(w1, h) * 0.44f;

    // Vista superior: mirando desde el norte del plano de referencia.
    const ImVec2 c1(x + w1 * 0.5f, top + h * 0.5f);
    const auto topView = [&](const Vec3d& p) { return ImVec2(c1.x + static_cast<float>(p.x / reach) * half, c1.y - static_cast<float>(p.z / reach) * half); };

    // Vista lateral: el eje horizontal es la linea de nodos. La altura se exagera si hace falta.
    const double exaggeration = std::clamp(0.7 * reachH / reachV, 1.0, 40.0);
    const float halfH = w1 * 0.44f, halfV = h * 0.40f;
    const ImVec2 c2(x2 + w1 * 0.5f, top + h * 0.5f);
    const auto sideView = [&](const Vec3d& p) {
        return ImVec2(c2.x + static_cast<float>(Dot(p, nodeAxis) / reachH) * halfH,
                      c2.y + static_cast<float>(p.y * exaggeration / reachH) * halfV * static_cast<float>(halfH / halfV));
    };

    std::vector<ImVec2> line(kPoints);
    for (int i = 0; i < kPoints; ++i) line[static_cast<std::size_t>(i)] = topView(points[static_cast<std::size_t>(i)]);
    c.d->AddPolyline(line.data(), kPoints, kLine, 1.2f, ImDrawFlags_Closed);
    for (int i = 0; i < kPoints; ++i) line[static_cast<std::size_t>(i)] = sideView(points[static_cast<std::size_t>(i)]);
    c.d->AddLine(ImVec2(x2 + 2.0f, c2.y), ImVec2(x2 + w1 - 2.0f, c2.y), kLineDim);  // plano de referencia
    c.d->AddPolyline(line.data(), kPoints, kLine, 1.2f, ImDrawFlags_Closed);

    const Vec3f centerColor = {1.0f, 0.9f, 0.6f};
    const ImU32 sun = FromVec(centerColor);
    c.d->AddCircleFilled(c1, 2.8f * c.s, sun);
    c.d->AddCircleFilled(c2, 2.8f * c.s, sun);

    // Periastro y apoastro.
    const Vec3d peri = OrbitPositionFromEccentric(orbit, 0.0), apo = OrbitPositionFromEccentric(orbit, kPi);
    for (const ImVec2& p : {topView(peri), sideView(peri)}) c.d->AddCircle(p, 2.5f * c.s, Rgba(120, 255, 180));
    for (const ImVec2& p : {topView(apo), sideView(apo)}) c.d->AddCircle(p, 2.5f * c.s, Rgba(255, 120, 120));

    for (const ImVec2& p : {topView(now), sideView(now)}) {
        c.d->AddCircleFilled(p, 3.4f * c.s, kAccent);
        c.d->AddCircle(p, 6.0f * c.s, Rgba(255, 185, 70, 0.5f));
    }
    if (exaggeration > 1.05) c.TextRight(x2 + w1 - 4.0f * c.s, top + h - 15.0f * c.s, 11.0f, kTextDim, Format("altura x%.0f", exaggeration));
}

// Una estrella no orbita: se muestra su sistema, visto desde arriba y en corte radial.
// Lo que orbitan los planetas de una estrella: ella misma o la pareja de la que forma parte.
int HostOfStar(const StarSystemData& system, int member) {
    for (std::size_t n = 0; n < system.nodes.size(); ++n) {
        const StellarNode& node = system.nodes[n];
        if (node.first >= 0 || node.star != member) continue;
        return node.planetOuter > 0.0 || node.parent < 0 ? static_cast<int>(n) : node.parent;
    }
    return 0;
}

void SystemPlanes(const Canvas& c, const WorldCatalog& w, const Body& b, float x, float y, float width, float height) {
    const StarSystemData& system = *w.systems[static_cast<std::size_t>(b.system)];
    const int host = HostOfStar(system, b.member);
    const StarData& star = system.nodes.empty() ? system.star : system.nodes[static_cast<std::size_t>(host)].effective;
    const auto skip = [&](const BodyData& p) { return p.parent >= 0 || p.kind == PlanetClass::Comet || p.kind == PlanetClass::Asteroid || p.destroyed || p.host != host; };
    double closest = star.habitableInner, outer = star.snowLine * 1.2;
    for (const BodyData& p : system.bodies) {
        if (skip(p)) continue;
        closest = std::min(closest, Periapsis(p.orbit));
        outer = std::max(outer, Apoapsis(p.orbit));
    }
    const double inner = std::max(star.radius, closest * 0.3);  // la escala empieza cerca del primer planeta

    c.Text(x, y, 11.5f, kTextDim, "PLANO SUPERIOR (LOG)");
    const float top = y + 16.0f * c.s;
    const float h = height - 16.0f * c.s;
    const float side = std::min(h, width * 0.5f - 4.0f * c.s);
    c.Frame(x, top, side, h);
    const ImVec2 center(x + side * 0.5f, top + h * 0.5f);
    const float maxRadius = side * 0.46f;
    const auto scale = [&](double r) { return static_cast<float>(std::log(std::max(r, inner) / inner) / std::log(outer / inner)) * maxRadius; };

    c.d->AddCircleFilled(center, std::max(scale(star.habitableOuter), 1.0f), Rgba(70, 255, 150, 0.12f));
    c.d->AddCircleFilled(center, std::max(scale(star.habitableInner), 1.0f), Rgba(2, 8, 14, 0.9f));
    c.d->AddCircle(center, scale(star.snowLine), Rgba(150, 210, 255, 0.45f));
    c.d->AddCircleFilled(center, 3.0f * c.s, FromVec(BlackbodyColor(star.temperature)));

    const float x2 = x + side + 8.0f * c.s, w2 = width - side - 8.0f * c.s;
    c.Text(x2, y, 11.5f, kTextDim, "CORTE RADIAL");
    c.Frame(x2, top, w2, h);
    const float axis = top + h * 0.5f;
    const auto radial = [&](double r) { return x2 + 6.0f * c.s + scale(r) / maxRadius * (w2 - 12.0f * c.s); };
    c.d->AddRectFilled(ImVec2(radial(star.habitableInner), top + 1.0f), ImVec2(std::max(radial(star.habitableOuter), radial(star.habitableInner) + 1.0f), top + h - 1.0f), Rgba(70, 255, 150, 0.14f));
    c.d->AddLine(ImVec2(radial(star.snowLine), top + 1.0f), ImVec2(radial(star.snowLine), top + h - 1.0f), Rgba(150, 210, 255, 0.55f));
    c.d->AddLine(ImVec2(x2 + 2.0f, axis), ImVec2(x2 + w2 - 2.0f, axis), kLineDim);

    for (const BodyData& p : system.bodies) {
        if (skip(p)) continue;
        c.d->AddCircle(center, scale(p.orbit.semiMajorAxis), kLineDim);
        Body shown;
        shown.kind = p.kind;
        c.d->AddCircleFilled(ImVec2(radial(p.orbit.semiMajorAxis), axis), 2.6f * c.s, FromVec(BodyColor(shown, system)));
    }
    c.Text(x2 + 4.0f * c.s, top + h - 15.0f * c.s, 11.0f, Rgba(90, 240, 150), "habitable");
    c.TextRight(x2 + w2 - 4.0f * c.s, top + 3.0f * c.s, 11.0f, Rgba(150, 210, 255), "hielo");
}

void DetailPanel(const Canvas& c, const WorldCatalog& w, const HudFrame& f, const HudLayout& l) {
    const Body& b = w.bodies[static_cast<std::size_t>(f.selected)];
    const StarSystemData& system = *w.systems[static_cast<std::size_t>(b.system)];
    const float s = c.s;
    c.Plate(l.panelX, l.panelY, l.panelW, l.panelH, kPanel, kLine, 16.0f * s);

    const float x = l.panelX + 16.0f * s, w2 = l.panelW - 32.0f * s;
    float y = l.panelY + 10.0f * s;
    c.Text(x, y, 20.0f, kAccent, b.name);
    y += 25.0f * s;
    c.Text(x, y, 12.5f, FromVec(BodyColor(b, system)), KindName(b, system));
    c.TextRight(x + w2, y, 12.5f, kTextDim, f.selected == f.followed ? "SIGUIENDO" : Distance(f.selectedDistance));

    // Reticula de la miniatura; la esfera se dibuja despues, encima de esto.
    const float size = static_cast<float>(l.miniSize);
    const ImVec2 center(static_cast<float>(l.miniX) + size * 0.5f, static_cast<float>(l.miniY) + size * 0.5f);
    c.d->AddCircleFilled(center, size * 0.52f, Rgba(2, 10, 18, 0.9f), 64);
    c.d->AddCircle(center, size * 0.52f, kLineDim, 64);
    c.Brackets(center.x, center.y, size * 0.54f, 12.0f * s, kLine);

    y = static_cast<float>(l.miniY) + size + 12.0f * s;

    if (b.kind == PlanetClass::Belt && !b.isStar) {
        const BeltInfo& info = w.belts[static_cast<std::size_t>(b.belt)];
        const auto length = [](double km) { return Format("%.2f UA", km / kAstronomicalUnit); };
        c.Heading(x, y, w2, "CAMPO DE ASTEROIDES");
        y += 20.0f * s;
        const float row = 17.0f * s;
        c.Row(x, y, w2, "Tipo", ProfileInfo(info.profile).name); y += row;
        c.Row(x, y, w2, "Origen", info.debris ? "restos de un choque" : "primordial"); y += row;
        c.Row(x, y, w2, "Radio interior", length(info.inner)); y += row;
        c.Row(x, y, w2, "Radio exterior", length(info.outer)); y += row;
        c.Row(x, y, w2, "Anchura", length(info.outer - info.inner)); y += row + 6.0f * s;
        c.Heading(x, y, w2, "CONTENIDO");
        y += 20.0f * s;
        c.Row(x, y, w2, "Densidad", Format("%.2f rocas / millon km3", info.density)); y += row;
        c.Row(x, y, w2, "Tamano de las rocas", Format("%.2f - %.0f km", info.minRadius, info.maxRadius)); y += row;
        c.Row(x, y, w2, "Rocas", Format("%.3g en %zu cumulos", static_cast<double>(info.rocks), info.families)); y += row;
        c.Row(x, y, w2, "Masa", Format("%.3g masas terrestres", info.mass / kEarthMass)); y += row;
        c.Row(x, y, w2, "Asteroides con nombre", Format("%d", info.notable)); y += row + 6.0f * s;
        c.Text(x, y, 12.0f, kTextDim, "F viaja al cumulo mas cercano");
        return;
    }

    if (b.isStar && system.StarAt(b.member).kind == StarClass::BlackHole) {
        const StarData& star = system.StarAt(b.member);
        const double rs = star.radius, spin = star.blackHoleSpin;
        double left = 0, right = 0;
        KerrShadowEdges(spin, left, right);
        const auto length = [](double km) { return km >= 0.01 * kAstronomicalUnit ? Format("%.3f UA", km / kAstronomicalUnit) : Format("%.0f km", km); };
        c.Heading(x, y, w2, "AGUJERO NEGRO");
        y += 20.0f * s;
        const float row = 17.0f * s;
        c.Row(x, y, w2, "Masa", Format("%.4g masas solares", star.mass / kSolarMass)); y += row;
        c.Row(x, y, w2, "Giro a/M", spin > 0.0 ? Format("%.2f", spin) : std::string("sin rotacion")); y += row;
        c.Row(x, y, w2, "Radio de Schwarzschild", length(rs)); y += row;
        c.Row(x, y, w2, "Horizonte", length(rs * KerrHorizon(spin))); y += row;
        c.Row(x, y, w2, "Anchura de la sombra", length(rs * (right - left))); y += row;
        c.Row(x, y, w2, "Ultima orbita estable", length(rs * InnermostStableOrbit(spin))); y += row;
        c.Row(x, y, w2, "Luminosidad del disco", Format("%.3g soles", star.luminosity)); y += row + 6.0f * s;
        return;
    }

    if (b.isStar) {
        const StarData& star = system.StarAt(b.member);
        c.Heading(x, y, w2, "ESTRELLA");
        y += 20.0f * s;
        const float row = 17.0f * s;
        if (system.StarCount() > 1) {
            c.Row(x, y, w2, "Sistema", Format("%s, %d estrellas", LayoutName(system), system.StarCount())); y += row;
            if (b.orbit >= 0) {
                const OrbitalElements& e = w.orbits[static_cast<std::size_t>(b.orbit)].elements;
                c.Row(x, y, w2, "Vuelta a su pareja", Duration(OrbitPeriod(e))); y += row;
                c.Row(x, y, w2, "Distancia al baricentro", Distance(f.selectedRange)); y += row;
            }
        }
        c.Row(x, y, w2, "Masa", Format("%.3f masas solares", star.mass / kSolarMass)); y += row;
        c.Row(x, y, w2, "Radio", star.radius < 0.01 * kSolarRadius ? Format("%.0f km", star.radius) : Format("%.3g radios solares", star.radius / kSolarRadius)); y += row;
        c.Row(x, y, w2, "Temperatura", Format("%.0f K", star.temperature)); y += row;
        c.Row(x, y, w2, "Luminosidad", Format("%.3g soles", star.luminosity)); y += row;
        c.Row(x, y, w2, "Giro", Duration(2.0 * kPi / star.spinRate)); y += row;
        c.Row(x, y, w2, "Zona habitable", Format("%.2f - %.2f UA", star.habitableInner / kAstronomicalUnit, star.habitableOuter / kAstronomicalUnit)); y += row;
        c.Row(x, y, w2, "Linea de hielo", Format("%.2f UA", star.snowLine / kAstronomicalUnit)); y += row + 6.0f * s;
        c.Heading(x, y, w2, "COMPOSICION");
        y += 20.0f * s;
        c.Meter(x, y, w2, "Hidrogeno", star.hydrogen, Rgba(150, 200, 255), Format("%.0f %%", star.hydrogen * 100)); y += 17.0f * s;
        c.Meter(x, y, w2, "Helio", star.helium, Rgba(255, 220, 140), Format("%.0f %%", star.helium * 100)); y += 17.0f * s;
        c.Meter(x, y, w2, "Metales", star.metals * 20.0, Rgba(255, 150, 90), Format("%.1f %%", star.metals * 100)); y += 24.0f * s;
        c.Heading(x, y, w2, "SISTEMA");
        y += 20.0f * s;
        SystemPlanes(c, w, b, x, y, w2, l.panelY + l.panelH - y - 12.0f * s);
        return;
    }

    const BodyData& data = b.extra >= 0 ? w.rockData[static_cast<std::size_t>(b.extra)] : system.bodies[static_cast<std::size_t>(b.data)];
    const Materials& m = data.materials;
    const OrbitalElements& e = data.orbit;

    c.Heading(x, y, w2, "DATOS");
    y += 19.0f * s;
    const float row = 16.0f * s;
    const float half = (w2 - 14.0f * s) * 0.5f, x2 = x + half + 14.0f * s;
    const double moons = static_cast<double>(std::count_if(system.bodies.begin(), system.bodies.end(), [&](const BodyData& o) { return b.data >= 0 && o.parent == b.data; }));
    const double gravity = (data.mass / kEarthMass) / std::pow(data.radius / kEarthRadius, 2.0);
    c.Row(x, y, half, "Masa", Format("%.3g M.T.", data.mass / kEarthMass));
    c.Row(x2, y, half, "Radio", data.radius < 10.0 ? Format("%.2f km", data.radius) : Format("%.0f km", data.radius)); y += row;
    c.Row(x, y, half, "Densidad", Format("%.2f g/cm3", data.mass / (4.0 / 3.0 * kPi * std::pow(data.radius, 3.0)) / 1.0e12));
    c.Row(x2, y, half, "Gravedad", Format("%.2f g", gravity)); y += row;
    c.Row(x, y, half, "Temp.", Format("%.0f K", m.surfaceTemperature > 0 ? m.surfaceTemperature : data.temperature));
    c.Row(x2, y, half, "Presion", m.gas > 0.05 || m.ices > 0.3 ? "sin suelo" : Format("%.3g bar", m.pressure)); y += row;
    c.Row(x, y, half, "Giro", data.tidallyLocked ? "acoplado" : Duration(2.0 * kPi / data.spin.rate));
    c.Row(x2, y, half, "Achatado", Format("%.2f %%", DeriveAppearance(data).flattening * 100.0f)); y += row;
    c.Row(x, y, half, "Eje", Format("%.1f grados", std::acos(std::clamp(Dot(data.spin.axis, Normalize(Cross(MakeOrbit(kNoNode, e).p, MakeOrbit(kNoNode, e).q))), -1.0, 1.0)) * 180.0 / kPi));
    c.Row(x2, y, half, "Lunas", Format("%.0f", moons)); y += row + 6.0f * s;

    c.Heading(x, y, w2, "MATERIALES");
    y += 18.0f * s;
    const float bar = 15.5f * s;
    c.Meter(x, y, w2, "Hierro", m.iron, Rgba(170, 170, 190), Format("%.0f %%", m.iron * 100)); y += bar;
    c.Meter(x, y, w2, "Silicatos", m.silicate, Rgba(200, 160, 120), Format("%.0f %%", m.silicate * 100)); y += bar;
    c.Meter(x, y, w2, "Agua", m.water, Rgba(90, 160, 255), Format("%.2g %%", m.water * 100)); y += bar;
    c.Meter(x, y, w2, "Hielos", m.ices, Rgba(170, 235, 255), Format("%.0f %%", m.ices * 100)); y += bar;
    c.Meter(x, y, w2, "H y He", m.gas, Rgba(255, 215, 150), Format("%.0f %%", m.gas * 100)); y += bar;

    // Lo que colorea la superficie, en una linea de muestras.
    const struct { const char* name; double value; ImU32 color; } chips[] = {
        {"Oxidos", m.rust, Rgba(200, 80, 35)}, {"Azufre", m.sulfur, Rgba(235, 205, 60)},
        {"Carbono", m.carbon, Rgba(95, 95, 105)}, {"Metano", m.methane, Rgba(70, 130, 240)},
        {"Oceano", m.ocean, Rgba(40, 110, 220)}, {"Hielo", m.iceCap, Rgba(225, 240, 255)}};
    const float chip = w2 / 6.0f;
    for (int i = 0; i < 6; ++i) {
        const float cx = x + chip * static_cast<float>(i);
        c.d->AddRectFilled(ImVec2(cx, y + 3.0f * s), ImVec2(cx + (chip - 4.0f * s) * static_cast<float>(std::clamp(chips[i].value, 0.0, 1.0)), y + 8.0f * s), chips[i].color);
        c.d->AddRect(ImVec2(cx, y + 3.0f * s), ImVec2(cx + chip - 4.0f * s, y + 8.0f * s), kLineDim);
        c.Text(cx, y + 9.0f * s, 10.5f, kTextDim, chips[i].name);
    }
    y += 28.0f * s;

    c.Heading(x, y, w2, Format("ORBITA  /  %s", ZoneText(w, b, data).c_str()));
    y += 19.0f * s;
    c.Row(x, y, half, "Semieje", Distance(e.semiMajorAxis));
    c.Row(x2, y, half, "Excentr.", Format("%.4f", e.eccentricity)); y += row;
    c.Row(x, y, half, "Periodo", Duration(OrbitPeriod(e)));
    c.Row(x2, y, half, "Inclin.", Format("%.2f grados", e.inclination * 180.0 / kPi)); y += row;
    c.Row(x, y, half, "Distancia", Distance(f.selectedRange));
    c.Row(x2, y, half, "Velocidad", Format("%.2f km/s", f.selectedSpeed)); y += row + 5.0f * s;

    OrbitPlanes(c, w, f, b, x, y, w2, l.panelY + l.panelH - y - 12.0f * s);
}

void ListPanel(const Canvas& c, const WorldCatalog& w, const HudFrame& f, const HudLayout& l) {
    const float s = c.s;
    const float rowH = 19.0f * s;
    const float height = 40.0f * s + rowH * static_cast<float>(f.list.size());
    c.Plate(l.listX, l.listY, l.listW, height, kPanel, kLineDim, 12.0f * s);
    const float x = l.listX + 12.0f * s, inner = l.listW - 24.0f * s;
    float y = l.listY + 8.0f * s;
    c.Heading(x, y, inner, f.inside ? "CUERPOS DEL SISTEMA" : "ESTRELLAS CERCANAS");
    y += 24.0f * s;
    for (const HudListEntry& e : f.list) {
        const Body& b = w.bodies[static_cast<std::size_t>(e.body)];
        const StarSystemData& system = *w.systems[static_cast<std::size_t>(b.system)];
        const bool selected = e.body == f.selected;
        if (selected) c.d->AddRectFilled(ImVec2(l.listX + 3.0f * s, y - 2.0f * s), ImVec2(l.listX + l.listW - 3.0f * s, y + rowH - 3.0f * s), Rgba(255, 185, 70, 0.14f));
        const bool moon = !b.isStar && b.kind == PlanetClass::Moon;
        const float indent = moon ? 12.0f * s : 0.0f;
        c.Diamond(x + indent + 4.0f * s, y + 7.0f * s, 3.5f * s, FromVec(BodyColor(b, system)), true);
        // Fuera de un sistema, el nombre lleva la clase; dentro, basta con la letra del planeta.
        const std::string name = f.inside ? (b.isStar ? b.name : b.name.substr(b.name.find(' ') + 1)) : b.name + "  " + Info(system.star.kind).name;
        c.Text(x + indent + 13.0f * s, y, 13.0f, selected ? kAccent : kText, name);
        if (!(f.inside && b.isStar)) c.TextRight(x + inner, y, 12.0f, kTextDim, Distance(e.distance));
        y += rowH;
    }
}

struct Box { float x0, y0, x1, y1; };

void Labels(const Canvas& c, const WorldCatalog& w, const HudFrame& f) {
    const float s = c.s;
    // Primero lo importante: si dos etiquetas chocan, se queda la de mayor prioridad.
    std::vector<const HudLabel*> order;
    for (const HudLabel& l : f.labels) order.push_back(&l);
    const auto rank = [&](const HudLabel* l) {
        const Body& b = w.bodies[static_cast<std::size_t>(l->body)];
        if (l->body == f.selected) return 0;
        if (b.isStar) return 1;
        return b.kind == PlanetClass::Moon || b.kind == PlanetClass::Comet ? 3 : 2;
    };
    std::sort(order.begin(), order.end(), [&](const HudLabel* a, const HudLabel* b) {
        const int ra = rank(a), rb = rank(b);
        return ra != rb ? ra < rb : a->body < b->body;
    });

    std::vector<Box> used;
    for (const HudLabel* l : order) {
        const Body& b = w.bodies[static_cast<std::size_t>(l->body)];
        const StarSystemData& system = *w.systems[static_cast<std::size_t>(b.system)];
        const bool selected = l->body == f.selected;
        const ImU32 color = selected ? kAccent : FromVec(BodyColor(b, system), 0.95f);
        const float ring = std::max(l->pixelRadius + 6.0f * s, 8.0f * s);
        const float clipped = std::min(ring, 180.0f * s);

        std::string title = b.name;
        std::string sub = f.inside ? Name(b.kind) : Info(system.star.kind).name;
        if (b.isStar) sub = Info(system.star.kind).name;
        sub += "  " + Distance(l->distance);

        const float tx = l->x + clipped + 14.0f * s, ty = l->y - clipped - 14.0f * s;
        const float tw = std::max(c.Width(14.0f, title), c.Width(11.5f, sub));
        const Box box{tx, ty - 2.0f * s, tx + tw, ty + 31.0f * s};
        bool clear = true;
        for (const Box& u : used) clear = clear && (box.x1 < u.x0 || box.x0 > u.x1 || box.y1 < u.y0 || box.y0 > u.y1);

        c.Diamond(l->x, l->y, 3.0f * s, color, false);
        if (selected) c.Brackets(l->x, l->y, clipped, 10.0f * s, kAccent);
        if (!clear && !selected) continue;
        used.push_back(box);

        // Fondo oscuro: la etiqueta debe leerse tambien sobre un planeta o una estrella.
        c.d->AddRectFilled(ImVec2(tx - 6.0f * s, ty - 1.0f * s), ImVec2(tx + tw + 6.0f * s, ty + 31.0f * s), Rgba(3, 12, 22, 0.62f));
        c.d->AddLine(ImVec2(l->x + 3.0f * s, l->y - 3.0f * s), ImVec2(tx - 4.0f * s, ty + 15.0f * s), selected ? kAccent : Rgba(90, 210, 240, 0.45f), 1.0f);
        c.d->AddLine(ImVec2(tx - 4.0f * s, ty + 15.0f * s), ImVec2(tx + tw, ty + 15.0f * s), selected ? kAccent : Rgba(90, 210, 240, 0.45f), 1.0f);
        c.Text(tx, ty, 14.0f, selected ? kAccent : kText, title);
        c.Text(tx, ty + 16.0f * s, 11.5f, kTextDim, sub);
    }
}

void OffscreenArrow(const Canvas& c, const WorldCatalog& w, const HudFrame& f) {
    if (f.selected < 0 || f.selectedOnScreen) return;
    const float s = c.s;
    const float a = f.selectedArrow;
    const ImVec2 tip(f.selectedX + std::cos(a) * 14.0f * s, f.selectedY + std::sin(a) * 14.0f * s);
    const ImVec2 left(f.selectedX + std::cos(a + 2.5f) * 12.0f * s, f.selectedY + std::sin(a + 2.5f) * 12.0f * s);
    const ImVec2 right(f.selectedX + std::cos(a - 2.5f) * 12.0f * s, f.selectedY + std::sin(a - 2.5f) * 12.0f * s);
    c.d->AddTriangleFilled(tip, left, right, kAccent);
    const std::string text = w.bodies[static_cast<std::size_t>(f.selected)].name + "  " + Distance(f.selectedDistance);
    // El texto se coloca hacia el centro de la pantalla para no tapar la flecha.
    const float width = c.Width(12.5f, text);
    const float tx = std::clamp(f.selectedX - std::cos(a) * (width * 0.5f + 26.0f * s) - width * 0.5f, 8.0f * s, static_cast<float>(f.width) - width - 8.0f * s);
    const float ty = std::clamp(f.selectedY - std::sin(a) * 26.0f * s - 8.0f * s, 44.0f * s, static_cast<float>(f.height) - 44.0f * s);
    c.Text(tx, ty, 12.5f, kAccent, text);
}

void TopBar(const Canvas& c, const WorldCatalog& w, const HudFrame& f) {
    const float s = c.s, width = static_cast<float>(f.width);
    const StarSystemData& system = *w.systems[static_cast<std::size_t>(f.system)];
    c.d->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, 44.0f * s), Rgba(3, 12, 22, 0.85f), Rgba(3, 12, 22, 0.85f), Rgba(3, 12, 22, 0), Rgba(3, 12, 22, 0));
    c.d->AddLine(ImVec2(16.0f * s, 36.0f * s), ImVec2(width - 16.0f * s, 36.0f * s), kLineDim);

    const std::string where = f.inside ? Format("SISTEMA SYS-%d  /  %s", f.system, system.StarCount() > 1 ? LayoutName(system) : Info(system.star.kind).name) : "ESPACIO INTERESTELAR";
    c.Text(18.0f * s, 11.0f * s, 16.0f, kLine, where);
    // Dentro de un sistema, la clase completa ayuda mas que la letra sola.
    const std::string range = f.inside ? Distance(f.starDistance) + (system.StarCount() > 1 ? " del centro" : " de la estrella") : Format("SYS-%d a ", f.system) + Distance(f.starDistance);
    c.Text(18.0f * s + c.Width(16.0f, where) + 18.0f * s, 14.0f * s, 12.5f, kTextDim, range);

    const std::string date = Format("DIA %.1f", f.simTime / kDay);
    const std::string rate = f.paused ? "PAUSA" : TimeRate(f.timeScale);
    c.TextRight(width - 18.0f * s, 11.0f * s, 16.0f, f.paused ? kAccent : kLine, rate);
    c.TextRight(width - 34.0f * s - c.Width(16.0f, rate), 14.0f * s, 12.5f, kTextDim, date);
}

void BottomBar(const Canvas& c, const HudFrame& f) {
    const float s = c.s, height = static_cast<float>(f.height);
    c.Text(18.0f * s, height - 22.0f * s, 11.5f, kTextDim, "CLIC seleccionar   N/P lista   F seguir   TAB saltar   RETROCESO soltar   ,/. tiempo   ESPACIO pausa   O orbitas   H interfaz   F1 consola");
}

// Consola de comandos, abajo a la izquierda. Se ve aunque la interfaz este oculta.
void Console(const Canvas& c, const HudFrame& f) {
    const float s = c.s;
    const float lineHeight = 17.0f * s;
    const float width = std::min(760.0f * s, static_cast<float>(f.width) - 32.0f * s);
    const float height = lineHeight * static_cast<float>(f.consoleLog.size() + 1) + 20.0f * s;
    const float x = 16.0f * s, y = static_cast<float>(f.height) - height - 40.0f * s;
    c.Plate(x, y, width, height, kPanel, kLine, 10.0f * s);
    float line = y + 8.0f * s;
    for (const std::string& text : f.consoleLog) {
        c.Text(x + 12.0f * s, line, 13.0f, kTextDim, text);
        line += lineHeight;
    }
    const bool blink = static_cast<int>(f.simClock * 2.0) % 2 == 0;
    c.Text(x + 12.0f * s, line, 13.0f, kAccent, "> " + f.console + (blink ? "_" : ""));
}

void Objective(const Canvas& c, const HudFrame& f) {
    const float s = c.s;
    const float width = c.Width(14.0f, f.objective) + 32.0f * s;
    const float x = (static_cast<float>(f.width) - width) * 0.5f, y = 54.0f * s;
    c.Plate(x, y, width, 28.0f * s, kPanel, kLine, 8.0f * s);
    c.Text(x + 16.0f * s, y + 6.0f * s, 14.0f, kAccent, f.objective);
}

void Crosshair(const Canvas& c, const HudFrame& f) {
    const float s = c.s, x = static_cast<float>(f.width) * 0.5f, y = static_cast<float>(f.height) * 0.5f;
    const ImU32 color = Rgba(90, 210, 240, 0.55f);
    for (int i = 0; i < 4; ++i) {
        const float a = kTau * 0.25f * static_cast<float>(i);
        const float dx = std::cos(a), dy = std::sin(a);
        c.d->AddLine(ImVec2(x + dx * 9.0f * s, y + dy * 9.0f * s), ImVec2(x + dx * 18.0f * s, y + dy * 18.0f * s), color, 1.0f);
    }
}

std::string FindFont() {
    const char* folder = SDL_getenv("windir");
    const std::string fonts = std::string(folder ? folder : "C:/Windows") + "/Fonts/";
    for (const char* name : {"bahnschrift.ttf", "segoeui.ttf", "consola.ttf"}) {
        SDL_PathInfo info;
        if (SDL_GetPathInfo((fonts + name).c_str(), &info)) return fonts + name;
    }
    return {};
}

}  // namespace

SciFiHud::~SciFiHud() { Shutdown(); }

bool SciFiHud::Initialize(SdlGpuBackend& backend) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no hay ventanas que recordar
    io.LogFilename = nullptr;

    const std::string font = FindFont();
    if (!font.empty()) io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f);

    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = backend.Device();
    info.ColorTargetFormat = static_cast<SDL_GPUTextureFormat>(backend.TargetFormat());
    initialized_ = ImGui_ImplSDLGPU3_Init(&info);
    if (!initialized_) ImGui::DestroyContext();
    return initialized_;
}

void SciFiHud::Shutdown() {
    if (!initialized_) return;
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void SciFiHud::BuildFrame() {
    const HudFrame& f = *frame_;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(f.width), static_cast<float>(f.height));
    io.DeltaTime = 1.0f / 60.0f;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();

    const HudLayout layout = ComputeHudLayout(f.width, f.height);
    const Canvas c(ImGui::GetBackgroundDrawList(), layout.scale);

    if (f.visible) {
        Crosshair(c, f);
        Labels(c, *f.catalog, f);
        OffscreenArrow(c, *f.catalog, f);
        TopBar(c, *f.catalog, f);
        ListPanel(c, *f.catalog, f, layout);
        if (f.selected >= 0) DetailPanel(c, *f.catalog, f, layout);
        if (!f.consoleOpen) BottomBar(c, f);
        if (!f.objective.empty()) Objective(c, f);
    }
    if (f.consoleOpen) Console(c, f);

    ImGui::Render();
}

void SciFiHud::Prepare(SDL_GPUCommandBuffer* cmd) {
    hasDrawData_ = false;
    if (!initialized_ || !frame_ || !frame_->catalog || (!frame_->visible && !frame_->consoleOpen) || frame_->width <= 0 || frame_->height <= 0) return;
    BuildFrame();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
    hasDrawData_ = true;
}

void SciFiHud::Draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) {
    if (hasDrawData_) ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass);
}

}  // namespace eps
