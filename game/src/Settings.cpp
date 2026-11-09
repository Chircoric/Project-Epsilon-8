#include "game/Settings.h"

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

std::string WriteSettings(const GameSettings& s) {
    std::ostringstream out;
    out << "calidad " << static_cast<int>(s.quality) << '\n';
    out << "pantalla-completa " << (s.fullscreen ? 1 : 0) << '\n';
    out << "sincronizacion " << (s.vsync ? 1 : 0) << '\n';
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
        if (!(in >> key >> value)) continue;
        if (key == "calidad" && value >= 0 && value < static_cast<int>(GraphicsQuality::Count)) s.quality = static_cast<GraphicsQuality>(value);
        else if (key == "pantalla-completa") s.fullscreen = value != 0;
        else if (key == "sincronizacion") s.vsync = value != 0;
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
