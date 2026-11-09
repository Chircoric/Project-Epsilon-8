// GameHud.h - Interfaz de la partida.
//
// Dear ImGui solo se usa como lienzo y para las fuentes: los paneles y botones son propios. Con
// el cursor activo, cada panel responde al raton y lo que se pulsa llega como una HudAction.
#pragma once

#include <string>
#include <vector>

#include "epsilon/Camera.h"
#include "epsilon/SdlGpuBackend.h"
#include "game/Session.h"
#include "game/Settings.h"

namespace game {

enum class HudActionType : int {
    Drive,          // value: DriveMode
    Go, Mine, Power, Use, Scan, Upgrade, Stop,
    SelectContact,  // value: indice en los sensores
    NextDialogue,
    ToggleCursor, ToggleKeys,
    StartMode,      // value: GameModeId
    Continue,       // tras perder la nave
    Resume, Quit,
    Analyze,        // abre o cierra el analisis del objetivo
    LoadGame,       // sigue la partida guardada
    SaveGame,
    Fire,           // value: WeaponKind; -1 = todo lo que este listo
    Track,          // el rumbo sigue la solucion de tiro del canon espinal
    Orbit,          // circulariza alrededor de lo que domina aqui
    CloseWheel,     // cierra la rueda de opciones
    ToggleAtlas,    // abre o cierra el menu de sistemas
    AtlasSystem,    // value: sistema
    AtlasSelect,    // value: TargetRef::Kind; system e index dicen cual
    AtlasMode,      // value: 0 = sistema, 1 = astro, 2 = tabla periodica
    AtlasTarget,    // fija como objetivo lo elegido en el menu
    Dock,           // atraca en la estacion objetivo
    Undock,
    Disembark,      // baja a pie a la cubierta
    Board,          // vuelve a la nave
    Shield,         // value: ShieldKind; -1 = el siguiente; -2 = apagar
    Coolant,        // value: CoolantKind; cambia entre encendido y apagado
    RadarRange,     // value: +1 aleja el alcance del radar, -1 lo acerca
    ToggleTrajectory,  // muestra u oculta la linea de lo que va a recorrer la nave
    ToggleWorkshop,    // abre o cierra el taller de modulos (atracada)
    WorkshopSlot,      // value: ranura elegida en el taller
    InstallModule,     // value: ShipModuleKind; index: ranura
    ImpulseEngine,     // value: motor de impulso, 1..kImpulseEngineCount
    MenuPage,       // value: MenuPage
    Quality,        // value: GraphicsQuality
    ToggleFullscreen,
    ToggleVsync,
};

// Paginas del menu de inicio. La configuracion tambien se abre desde la pausa.
enum class MenuPage : int { Main, NewGame, Settings, Credits };

// Alcances del radar, en km: de la escotilla de una estacion a otro sistema.
inline constexpr double kRadarRanges[] = {10.0, 100.0, 1.0e3, 1.0e4, 1.0e5, 1.0e6, 1.0e7, 1.0e8, 1.0e9, 1.0e11, 1.0e13};
inline constexpr int kRadarRangeCount = static_cast<int>(sizeof(kRadarRanges) / sizeof(kRadarRanges[0]));

struct HudAction {
    HudActionType type;
    int value = 0;
    int system = 0, index = 0;
};

// Estado del menu de sistemas. El arrastre y la rueda del raton los lleva main.cpp.
struct AtlasView {
    bool open = false;
    int system = 0;
    TargetRef selected;
    bool astro = false;      // false = el sistema entero; true = el astro elegido
    bool table = false;      // la tabla periodica de lo elegido, en lugar de la vista en 3D
    double yaw = 0.7, pitch = 0.55;
    double zoom = 1.0;
    int scroll = 0;          // filas de la lista
    bool realSphere = false; // el astro tiene su superficie real en memoria
};

enum class HudScreen : int { Menu, Playing, Paused };

struct HudInput {
    float mouseX = 0, mouseY = 0;
    bool clicked = false;
    bool cursor = false;      // el raton maneja la interfaz, no la nave
    bool showKeys = true;
    int radarRange = 7;       // indice en kRadarRanges
    bool trajectory = false;  // linea de la ruta que lleva la nave, desde la rueda
    bool workshop = false;    // taller de modulos abierto
    int workshopSlot = 0;     // ranura elegida en el taller
    HudScreen screen = HudScreen::Playing;
    MenuPage page = MenuPage::Main;  // en el menu de inicio; en la pausa, Settings abre la configuracion
    double pageClock = 0;     // segundos reales desde que se abrio la pagina: anima su entrada
    GameSettings settings;
    double clock = 0;         // segundos reales: anima lo que se mueve
    bool consoleOpen = false;
    const std::string* console = nullptr;
    std::string savedGame;    // resumen de la partida guardada; vacio = no hay
    bool analysis = false;    // ventana de analisis del objetivo
    bool wheel = false;       // rueda de opciones abierta
    AtlasView atlas;
};

// Donde va la ventana de analisis y la esfera real que muestra.
struct AnalysisLayout {
    float x = 0, y = 0, w = 0, h = 0, scale = 1;
    int miniX = 0, miniY = 0, miniSize = 0;
};

AnalysisLayout ComputeAnalysis(int width, int height);

// El menu de sistemas: lista, vista en 3D y datos. La esfera crece con el zoom.
struct AtlasLayout {
    float x = 0, y = 0, w = 0, h = 0, scale = 1;
    float listX = 0, listW = 0, listY = 0, listH = 0;
    float viewX = 0, viewY = 0, viewW = 0, viewH = 0;
    float infoX = 0, infoW = 0;
    int miniX = 0, miniY = 0, miniSize = 0;
};

AtlasLayout ComputeAtlas(int width, int height, double zoom);

class GameHud final : public eps::IGpuOverlay {
public:
    ~GameHud() override;

    bool Initialize(eps::SdlGpuBackend& backend, const std::string& fontFolder);
    void Shutdown();

    void Set(const GameSession* session, const eps::Camera* camera, const TargetRef* target, const HudInput& input) {
        session_ = session;
        camera_ = camera;
        target_ = target;
        input_ = input;
    }

    // Lo pulsado desde la ultima llamada.
    std::vector<HudAction> TakeActions();

    void Prepare(SDL_GPUCommandBuffer* cmd) override;
    void Draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) override;

private:
    const GameSession* session_ = nullptr;
    const eps::Camera* camera_ = nullptr;
    const TargetRef* target_ = nullptr;
    HudInput input_;
    std::vector<HudAction> actions_;
    void* textFont_ = nullptr;
    void* titleFont_ = nullptr;
    void* alienFont_ = nullptr;  // lo que viene de fuera: anomalias y alienigenas
    void* logoFont_ = nullptr;   // el EPSILON del menu, en Hangul
    bool initialized_ = false, hasDrawData_ = false;
};

}  // namespace game
