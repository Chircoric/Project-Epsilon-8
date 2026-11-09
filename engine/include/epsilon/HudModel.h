// HudModel.h - Que muestra la interfaz en cada momento. No dibuja ni conoce ninguna libreria.
//
// La simulacion rellena un HudFrame y lo entrega junto al FramePacket. Asi el hilo principal
// nunca lee posiciones que otro hilo esta cambiando. Los nombres, materiales y elementos
// orbitales vienen del WorldCatalog, una copia inmutable: cargar un sistema publica otra.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <memory>
#include <string>

#include "epsilon/Universe.h"

namespace eps {

struct HudLabel {
    int body = -1;
    float x = 0, y = 0;    // pixeles
    float pixelRadius = 0;
    double distance = 0;   // km desde la camara
};

struct HudListEntry {
    int body = -1;
    double distance = 0;   // km; a la camara en el espacio, a su estrella dentro de un sistema
};

struct HudFrame {
    std::shared_ptr<const WorldCatalog> catalog;
    std::string console;              // texto que se esta escribiendo
    std::vector<std::string> consoleLog;  // ultimas respuestas
    bool consoleOpen = false;
    std::string objective;            // mensaje de la tarea actual
    bool visible = true;
    bool inside = false;     // la camara esta dentro de un sistema
    int system = 0;          // el mas cercano
    double starDistance = 0;
    int width = 1600, height = 900;
    double simTime = 0, timeScale = 1;
    double simClock = 0;  // segundos reales; hace parpadear el cursor
    bool paused = false;
    int followed = -1;

    std::vector<HudLabel> labels;      // solo lo que esta en pantalla
    std::vector<HudListEntry> list;    // panel izquierdo

    int selected = -1;
    bool selectedOnScreen = false;
    float selectedX = 0, selectedY = 0;   // pixeles; si esta fuera, punto en el borde
    float selectedArrow = 0;              // direccion hacia el cuerpo, en radianes
    double selectedDistance = 0;          // km desde la camara
    double selectedRange = 0;             // km a su cuerpo central
    double selectedSpeed = 0;             // km/s respecto a su cuerpo central
    double selectedAnomaly = 0;           // anomalia excentrica actual
};

struct HudLayout {
    float scale = 1;
    float panelX = 0, panelY = 0, panelW = 0, panelH = 0;  // panel derecho
    float listX = 0, listY = 0, listW = 0;                 // panel izquierdo
    int miniX = 0, miniY = 0, miniSize = 0;                // miniatura 3D
};

inline HudLayout ComputeHudLayout(int width, int height) {
    HudLayout l;
    l.scale = std::clamp(static_cast<float>(height) / 900.0f, 0.7f, 2.0f);
    l.panelW = 350.0f * l.scale;
    l.panelH = std::min(806.0f * l.scale, static_cast<float>(height) - 76.0f * l.scale);
    l.panelX = static_cast<float>(width) - l.panelW - 16.0f * l.scale;
    l.panelY = 54.0f * l.scale;
    l.listX = 16.0f * l.scale;
    l.listY = 54.0f * l.scale;
    l.listW = 250.0f * l.scale;
    l.miniSize = static_cast<int>(176.0f * l.scale);
    l.miniX = static_cast<int>(l.panelX + (l.panelW - static_cast<float>(l.miniSize)) * 0.5f);
    l.miniY = static_cast<int>(l.panelY + 58.0f * l.scale);
    return l;
}

// Dentro de un sistema se muestran sus planetas; fuera, solo estrellas.
inline bool IsInsideSystem(const World& w, int system, double starDistance) {
    return starDistance < w.extents[static_cast<std::size_t>(system)] * 2.5;
}

// Rellena lo que depende de la camara. Llamar tras actualizar orbitas y camara.
inline void BuildHud(const World& w, const Camera& cam, int selected, HudFrame& out) {
    using astro::PlanetClass;
    const Vec3d camera = cam.Position();
    const float focal = cam.FocalPx();
    const float width = static_cast<float>(cam.Width()), height = static_cast<float>(cam.Height());

    out.width = cam.Width();
    out.height = cam.Height();
    out.selected = selected;
    out.catalog = w.catalog;
    out.labels.clear();
    out.list.clear();

    out.system = NearestSystem(w, camera, &out.starDistance);

    out.inside = IsInsideSystem(w, out.system, out.starDistance);

    const auto addLabel = [&](int index, const Body& b, const Vec3d& position) {
        const ScreenPoint p = cam.WorldToScreen(position);
        if (!p.visible || p.x < 0.0f || p.y < 0.0f || p.x > width || p.y > height) return;
        const double distance = Length(position - camera);
        out.labels.push_back({index, p.x, p.y, SilhouettePixels(static_cast<float>(b.radius), static_cast<float>(std::max(distance, b.radius * 1.001)), focal), distance});
    };

    const int selectedParent = selected >= 0 ? w.bodies[static_cast<std::size_t>(selected)].parent : -1;

    // Fuera de un sistema solo importan las estrellas.
    if (!out.inside) {
        for (const SystemState& state : w.state) {
            const Body& b = w.bodies[static_cast<std::size_t>(state.starBody)];
            const Vec3d position = w.scene.WorldPosition(b.node);
            addLabel(state.starBody, b, position);
            out.list.push_back({state.starBody, Length(position - camera)});
        }
    } else {
        const SystemState& state = w.state[static_cast<std::size_t>(out.system)];
        const Vec3d center = w.scene.WorldPosition(w.sectors[static_cast<std::size_t>(out.system)]);
        for (const int index : state.starBodies) {
            const Body& star = w.bodies[static_cast<std::size_t>(index)];
            const Vec3d position = w.scene.WorldPosition(star.node);
            addLabel(index, star, position);
            out.list.push_back({index, Length(position - center)});
        }
        const Vec3d starPosition = center;
        for (const int index : state.bodies) {
            const Body& b = w.bodies[static_cast<std::size_t>(index)];
            const Vec3d position = w.scene.WorldPosition(b.node);
            const bool isMoon = b.kind == PlanetClass::Moon;
            const bool isAsteroid = b.kind == PlanetClass::Asteroid;
            bool show = b.renderable < 0 || !w.renderables[static_cast<std::size_t>(b.renderable)].hidden;
            // Un asteroide se etiqueta de cerca o si esta seleccionado su campo.
            if (isAsteroid && show) show = b.parent == selected || index == selected || selectedParent == b.parent || Length(position - camera) < b.radius * 3.0e4;
            if (isMoon && show) {
                // Una luna se etiqueta al acercarse a su planeta o si su planeta esta seleccionado.
                const double orbit = w.orbits[static_cast<std::size_t>(b.orbit)].elements.semiMajorAxis;
                const Vec3d host = w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(b.parent)].node);
                show = Length(host - camera) < orbit * 80.0 || b.parent == selected || index == selected || selectedParent == b.parent;
            }
            if (show) addLabel(index, b, position);
            if ((!isMoon && !isAsteroid) || b.parent == selected || selectedParent == b.parent) out.list.push_back({index, Length(position - starPosition)});
        }
    }
    // Lo seleccionado se ve desde cualquier sitio, aunque sea de otro sistema.
    if (selected >= 0 && (w.bodies[static_cast<std::size_t>(selected)].system != out.system || w.bodies[static_cast<std::size_t>(selected)].extra >= 0)) {
        const Body& b = w.bodies[static_cast<std::size_t>(selected)];
        addLabel(selected, b, w.scene.WorldPosition(b.node));
    } else if (selected >= 0 && !out.inside && !w.bodies[static_cast<std::size_t>(selected)].isStar) {
        const Body& b = w.bodies[static_cast<std::size_t>(selected)];
        addLabel(selected, b, w.scene.WorldPosition(b.node));
    }

    if (!out.inside) {
        // Las estrellas mas cercanas: el espacio tiene decenas y el panel solo muestra unas pocas.
        std::sort(out.list.begin(), out.list.end(), [](const HudListEntry& a, const HudListEntry& b) { return a.distance != b.distance ? a.distance < b.distance : a.body < b.body; });
        if (out.list.size() > 10) out.list.resize(10);
    }

    out.selectedOnScreen = false;
    if (selected < 0) return;

    const Body& body = w.bodies[static_cast<std::size_t>(selected)];
    const Vec3d position = w.scene.WorldPosition(body.node);
    out.selectedDistance = Length(position - camera);
    if (body.orbit >= 0) {
        const KeplerOrbit& orbit = w.orbits[static_cast<std::size_t>(body.orbit)];
        out.selectedAnomaly = EccentricAnomalyAt(orbit, out.simTime);
        out.selectedRange = Length(OrbitPosition(orbit, out.simTime));
        out.selectedSpeed = Length(OrbitVelocity(orbit, out.simTime));
    }

    const Vec3f view = cam.ToViewSpace(position);
    const float cx = width * 0.5f, cy = height * 0.5f;
    if (view.z > cam.NearZ()) {
        const float x = view.x / view.z * focal + cx, y = view.y / view.z * focal + cy;
        if (x >= 0.0f && y >= 0.0f && x <= width && y <= height) {
            out.selectedOnScreen = true;
            out.selectedX = x;
            out.selectedY = y;
            return;
        }
    }
    // Fuera de pantalla: una flecha en el borde. Si esta detras, el signo de la proyeccion
    // se invierte, por eso se usa la direccion en la vista y no el punto proyectado.
    const float angle = std::atan2(view.y, view.x);
    const float margin = 40.0f;
    const float rx = cx - margin, ry = cy - margin;
    const float reach = std::min(rx / std::max(std::abs(std::cos(angle)), 1e-4f), ry / std::max(std::abs(std::sin(angle)), 1e-4f));
    out.selectedArrow = angle;
    out.selectedX = cx + std::cos(angle) * reach;
    out.selectedY = cy + std::sin(angle) * reach;
}

// Cuerpo con la etiqueta mas cercana al punto de mira, o -1.
inline int PickHudBody(const HudFrame& hud, float maxPixels = 150.0f) {
    const float cx = static_cast<float>(hud.width) * 0.5f, cy = static_cast<float>(hud.height) * 0.5f;
    int best = -1;
    float bestDistance = maxPixels * maxPixels;
    for (const HudLabel& l : hud.labels) {
        const float dx = l.x - cx, dy = l.y - cy;
        const float d = dx * dx + dy * dy;
        if (d < bestDistance) {
            bestDistance = d;
            best = l.body;
        }
    }
    return best;
}

// Siguiente o anterior cuerpo de la lista del panel.
inline int StepHudSelection(const HudFrame& hud, int step) {
    if (hud.list.empty()) return hud.selected;
    const int count = static_cast<int>(hud.list.size());
    int position = step > 0 ? -1 : 0;
    for (int i = 0; i < count; ++i)
        if (hud.list[static_cast<std::size_t>(i)].body == hud.selected) position = i;
    return hud.list[static_cast<std::size_t>((position + step % count + count) % count)].body;
}

}  // namespace eps
