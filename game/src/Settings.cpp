#include "game/Settings.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "game/SaveGame.h"

namespace game {

const QualityProfile& Profile(GraphicsQuality quality) {
    static const QualityProfile profiles[] = {
        {"Baja", "La mitad de resolución en la escena, sin bloom y con menos detalle. El casco no enseña el daño, pero las piezas rotas sí se pierden. Para equipos modestos.", 0.5f, false, false, 5.0f, -1.0f, 10.0f, 0},
        {"Media", "Tres cuartos de resolución y bloom. El casco dañado se quema. Un buen punto para portátiles.", 0.75f, true, false, 7.0f, -0.5f, 8.0f, 1},
        {"Alta", "Resolución completa con todos los efectos. El casco dañado se quema y se agrieta al rojo.", 1.0f, true, true, 10.0f, 0.0f, 6.0f, 2},
        {"Ultra", "Como Alta, con más detalle en las superficies de cerca y agujeros en el casco destrozado.", 1.0f, true, true, 12.0f, 0.5f, 5.0f, 3},
    };
    const int i = static_cast<int>(quality);
    return profiles[i >= 0 && i < static_cast<int>(GraphicsQuality::Count) ? i : static_cast<int>(GraphicsQuality::High)];
}

GraphicsOptions OptionsOf(GraphicsQuality quality) {
    const QualityProfile& p = Profile(quality);
    const int level = std::clamp(static_cast<int>(quality), 0, static_cast<int>(GraphicsQuality::Count) - 1);
    GraphicsOptions o;
    o.renderScale = static_cast<int>(p.renderScale * 100.0f + 0.5f);
    o.bloom = p.bloom;
    o.film = p.film;
    o.surfaces = level;
    o.lighting = level;
    o.hullDamage = p.hullDamage;
    o.shadows = true;
    return o;
}

std::string WriteSettings(const GameSettings& s) {
    std::ostringstream out;
    // La calidad va primero: al leer, rellena todas las opciones y las lineas siguientes las afinan.
    out << "calidad " << static_cast<int>(s.quality) << '\n';
    out << "escena " << s.graphics.renderScale << '\n';
    out << "bloom " << (s.graphics.bloom ? 1 : 0) << '\n';
    out << "pelicula " << (s.graphics.film ? 1 : 0) << '\n';
    out << "superficies " << s.graphics.surfaces << '\n';
    out << "luz " << s.graphics.lighting << '\n';
    out << "casco " << s.graphics.hullDamage << '\n';
    out << "sombras " << (s.graphics.shadows ? 1 : 0) << '\n';
    out << "pantalla-completa " << (s.fullscreen ? 1 : 0) << '\n';
    out << "sincronizacion " << (s.vsync ? 1 : 0) << '\n';
    out << "volumen-interfaz " << s.uiVolume << '\n';
    out << "interfaz " << static_cast<int>(s.uiStyle) << '\n';
    out << "tamano-interfaz " << s.uiScale << '\n';
    out << "plegados " << s.folded << '\n';
    if (!s.playerId.empty()) out << "jugador " << s.playerId << '\n';
    return out.str();
}

GameSettings ReadSettings(const std::string& text) {
    GameSettings s;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        std::string key;
        int value = 0;
        if (!(in >> key)) continue;
        if (key == "jugador") {  // el unico dato que no es un numero
            std::string id;
            if (in >> id) s.playerId = SafePlayerId(id);
            continue;
        }
        if (!(in >> value)) continue;
        if (key == "calidad") {
            if (value < 0 || value >= static_cast<int>(GraphicsQuality::Count)) continue;
            s.quality = static_cast<GraphicsQuality>(value);
            s.graphics = OptionsOf(s.quality);  // un archivo antiguo solo trae esta linea
        }
        else if (key == "escena") s.graphics.renderScale = std::clamp(value, 25, 100);
        else if (key == "bloom") s.graphics.bloom = value != 0;
        else if (key == "pelicula") s.graphics.film = value != 0;
        else if (key == "superficies") s.graphics.surfaces = std::clamp(value, 0, 3);
        else if (key == "luz") s.graphics.lighting = std::clamp(value, 0, 3);
        else if (key == "casco") s.graphics.hullDamage = std::clamp(value, 0, 3);
        else if (key == "sombras") s.graphics.shadows = value != 0;
        else if (key == "pantalla-completa") s.fullscreen = value != 0;
        else if (key == "sincronizacion") s.vsync = value != 0;
        else if (key == "volumen-interfaz") s.uiVolume = std::clamp(value, 0, 100);
        else if (key == "interfaz") s.uiStyle = value == 0 ? UiStyle::Minimal : value == 1 ? UiStyle::Expert : UiStyle::Unset;
        else if (key == "tamano-interfaz") s.uiScale = std::clamp(value, kUiScales[0], kUiScales[kUiScaleCount - 1]);
        else if (key == "plegados") s.folded = static_cast<unsigned>(value) & ((1u << static_cast<int>(HudPanel::Count)) - 1u);
    }
    return s;
}

bool SaveSettings(const std::string& path, const GameSettings& settings) {
    std::ofstream out(FilePath(path), std::ios::binary | std::ios::trunc);
    out << WriteSettings(settings);
    return static_cast<bool>(out.flush());
}

GameSettings LoadSettings(const std::string& path) {
    std::ifstream in(FilePath(path), std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return ReadSettings(text.str());
}

}  // namespace game
