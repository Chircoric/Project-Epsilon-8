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

// Lo que el jugador puede cambiar por separado en la pestaña de gráficos. Elegir una calidad lo
// rellena todo de golpe; tocar despues una opcion deja la calidad como "Personalizada".
struct GraphicsOptions {
    int renderScale = 100;  // % de la resolucion de la ventana a la que se pinta la escena
    bool bloom = true;
    bool film = true;       // grano y aberracion cromatica
    int surfaces = 2;       // 0..3: detalle del ruido de planetas y estrellas (el de cada calidad)
    int lighting = 2;       // 0..3: luz de los shaders y pasos de la nebulosa
    int hullDamage = 2;     // 0..3: como QualityProfile::hullDamage
    bool shadows = true;    // la nave se da sombra a si misma
    bool operator==(const GraphicsOptions&) const = default;
};

// Cada opcion de la pestaña, para HudActionType::Graphics.
enum class GraphicsOption : int { RenderScale, Bloom, Film, Surfaces, Lighting, HullDamage, Shadows };

inline constexpr int kRenderScales[] = {50, 67, 75, 100};

GraphicsOptions OptionsOf(GraphicsQuality quality);

// Dos formas de ver la nave. Minimalista: poco detalle, letra grande, porcentajes y unidades
// sencillas; las cifras reales quedan en la ventana de detalles. Experta: todos los paneles, con
// unidades reales y graficas. Unset = aun no se ha elegido: el juego pregunta al abrir el menu.
enum class UiStyle : int { Unset = -1, Minimal = 0, Expert = 1 };

// Tamaño de toda la interfaz (letra incluida), en %.
inline constexpr int kUiScales[] = {100, 115, 130, 150};
inline constexpr int kUiScaleCount = static_cast<int>(sizeof(kUiScales) / sizeof(kUiScales[0]));

// Ventanas que se pliegan pulsando su cabecera. Cada una es un bit de GameSettings::folded.
enum class HudPanel : int {
    Ship, Cargo, Sensors, Target, Radar, Structure, Suns,   // modo experto
    MiniShip, MiniSignals, MiniTarget, MiniDetails,          // modo minimalista
    Count
};

inline constexpr unsigned PanelBit(HudPanel p) { return 1u << static_cast<int>(p); }
// De serie, todo abierto menos la ventana de detalles del modo minimalista.
inline constexpr unsigned kDefaultFolded = PanelBit(HudPanel::MiniDetails);

struct GameSettings {
    GraphicsQuality quality = GraphicsQuality::High;
    GraphicsOptions graphics;  // de serie, las de Alta
    // true si alguna opcion ya no es la de la calidad elegida.
    bool Custom() const { return !(graphics == OptionsOf(quality)); }
    bool fullscreen = false;
    bool vsync = true;
    int uiVolume = 50;  // 0..100: volumen de los sonidos de la interfaz
    UiStyle uiStyle = UiStyle::Unset;
    int uiScale = 115;  // uno de kUiScales: la letra de serie ya es algo mas grande que antes
    unsigned folded = kDefaultFolded;
    // Identificador de este jugador en las partidas (su archivo en jugadores/). El juego lo crea al
    // azar la primera vez; vacio = aun no tiene.
    std::string playerId;
    // Sin elegir, la partida se ve como siempre: en modo experto.
    bool Minimal() const { return uiStyle == UiStyle::Minimal; }
    bool Folded(HudPanel p) const { return (folded & PanelBit(p)) != 0; }
};

// Una linea por dato, como la partida. Lo que no se entiende se ignora y queda por defecto.
std::string WriteSettings(const GameSettings& settings);
GameSettings ReadSettings(const std::string& text);

bool SaveSettings(const std::string& path, const GameSettings& settings);
GameSettings LoadSettings(const std::string& path);  // por defecto si no hay archivo

}  // namespace game
