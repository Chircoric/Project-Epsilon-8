// Settings.h - Configuracion del jugador: calidad grafica, pantalla completa y sincronizacion.
//
// Se guarda aparte de la partida, en texto, para que valga en todas. Cada nivel de calidad es un
// perfil: lo que cuesta mas en una tarjeta modesta es la resolucion de la escena, despues el
// detalle del ruido de los planetas y el bloom.
#pragma once

#include <string>

namespace game {

enum class GraphicsQuality : int { Low, Medium, High, Ultra, Count };

struct QualityProfile {
    const char* name;
    const char* description;
    float renderScale;      // parte de la resolucion de la ventana a la que se pinta la escena
    bool bloom;             // el brillo que desborda de lo luminoso
    bool film;              // grano y aberracion cromatica
    float maxOctaves;       // capas de ruido por pixel en las superficies
    float detailBias;       // positivo, mas nitido; negativo, mas difuso
    float sphereMinPixels;  // por debajo, un planeta es un punto
    // Lo que ensena el casco danado: 0 nada (las piezas perdidas si desaparecen), 1 quemaduras,
    // 2 ademas grietas al rojo, 3 ademas agujeros que dejan ver a traves.
    int hullDamage;
};

const QualityProfile& Profile(GraphicsQuality quality);

struct GameSettings {
    GraphicsQuality quality = GraphicsQuality::High;
    bool fullscreen = false;
    bool vsync = true;
};

// Una linea por dato, como la partida. Lo que no se entiende se ignora y queda por defecto.
std::string WriteSettings(const GameSettings& settings);
GameSettings ReadSettings(const std::string& text);

bool SaveSettings(const std::string& path, const GameSettings& settings);
GameSettings LoadSettings(const std::string& path);  // por defecto si no hay archivo

}  // namespace game
