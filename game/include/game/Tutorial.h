// Tutorial.h - Dialogos con objetivos.
//
// Un paso son unas lineas que se leen y una condicion que el jugador debe cumplir. La escena no
// se detiene mientras tanto. Sirve para cualquier conversacion del juego: basta con otra lista.
//
// Mientras el objetivo esta activo, la interfaz senala donde se hace (Highlight) y dice que falta
// para cumplirlo (Guidance): la nave no esta quieta, falta energia, el objetivo no es una estrella...
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace game {

class GameSession;
struct TargetRef;

struct DialogueLine {
    std::string speaker;
    std::string text;
};

// Las opciones de la rueda (T), en el orden en que GameHud las dibuja: la primera arriba y las
// demas en el sentido del reloj.
enum class WheelOption : int { Scan, Go, Orbit, Dock, OnFoot, Use, Drones, Power, Shipyard, Upgrade, Analysis, Map, Fire, Trajectory, Workshop, Count };
const char* Name(WheelOption option);

// Lo que el tutorial puede senalar en la interfaz.
enum class Highlight : int {
    None,
    // Teclas de la barra de abajo
    KeyThrottle,  // R (y F)
    KeyBrake,     // Espacio
    KeyWheel,     // T
    KeyCursor,    // C
    KeyTarget,    // Tab
    KeyMap,       // M
    KeyHybrid,    // 3
    // Opciones de la rueda: con ella cerrada se senala la T
    WheelScan, WheelGo, WheelUse, WheelDrones, WheelPower, WheelShipyard, WheelAnalysis,
    // Paneles y lo que hay en ellos
    ShipPanel,      // la nave (en el minimalista, su ventana)
    SensorsPanel,   // la lista de senales
    TargetPanel,    // el objetivo fijado
    RadarPanel,     // el radar 3D (solo en el experto)
    ScanButton,     // el boton del escaner del panel de la nave
    PinnedContact,  // la fila del objetivo fijado en los sensores
    HybridButton,   // el boton de la Hibrida del panel de la nave
    EchoMarker,     // la marca de la pista del escaner, en el radar y en pantalla
    Count
};

// Donde esta cada cosa que se senala: una tecla, una opcion de la rueda o algo de un panel.
struct HighlightTarget {
    enum class Where { None, Key, Wheel, Panel, Marker } where = Where::None;
    const char* key = nullptr;             // la tecla que lo hace o lo abre, como sale en la barra
    WheelOption option = WheelOption::Count;
    const char* label = "";                // lo que se escribe junto a la flecha
};
HighlightTarget Locate(Highlight h);

// Lo que hay que senalar ahora y por que el objetivo aun no se cumple.
struct Guidance {
    Highlight highlight = Highlight::None;
    std::vector<std::string> blockers;  // "Nave quieta: no", "Energía: faltan 40 EJ"...
};

struct DialogueStep {
    std::vector<DialogueLine> lines;
    std::string objective;                          // vacio = solo conversacion
    std::function<bool(const GameSession&)> done;   // nulo = pasa al terminar de leer
    std::string tag;                                // para que el juego sepa en que paso esta
    Highlight highlight = Highlight::None;          // lo que se senala mientras el objetivo esta activo
    // Afina lo que se senala y dice que falta, segun la partida y el objetivo fijado. Nulo = solo highlight.
    std::function<void(const GameSession&, const TargetRef&, Guidance&)> guide;
    bool optional = false;                          // Enter lo salta, con el objetivo en pantalla
};

// El paso del tutorial en que hay que encontrar la estructura: en el, el eco del barrido es exacto.
inline constexpr const char* kStructureStep = "estructura";

class Dialogue {
public:
    void Start(std::vector<DialogueStep> steps);
    void Stop();
    void Resume(std::size_t step);  // sigue por este paso, tras cargar una partida
    void SkipLines();               // da por leidas las lineas del paso: queda su objetivo
    void Replay();                  // vuelve a decir las lineas del paso actual ("repetir consejo")

    // Escribe el texto poco a poco y pasa al siguiente paso cuando se cumple su condicion.
    void Update(const GameSession& session, double realSeconds);

    // Enter o clic: completa la linea o pasa a la siguiente; en un paso opcional, lo salta.
    void Next();

    bool Active() const { return step_ < steps_.size(); }
    bool Reading() const;                 // hay texto en pantalla
    const DialogueLine* Line() const;
    std::string VisibleText() const;      // lo que ya se ha escrito
    bool LineComplete() const;
    const std::string& Objective() const; // lo que falta por hacer
    std::size_t Step() const { return step_; }
    std::size_t StepCount() const { return steps_.size(); }
    const std::string& Tag() const;       // la marca del paso actual; vacia si no hay o ha terminado
    const DialogueStep* Current() const { return Active() ? &steps_[step_] : nullptr; }
    bool Optional() const { return Active() && !Reading() && steps_[step_].optional; }

    // Lo que hay que senalar y lo que falta, mientras el objetivo esta en pantalla.
    Guidance Guide(const GameSession& session, const TargetRef& target) const;

    // Cada objetivo cumplido suma uno: asi el juego sabe cuando sonar y destellar.
    int Completions() const { return completions_; }
    const std::string& LastCompleted() const { return lastCompleted_; }
    double SinceCompleted() const { return sinceCompleted_; }  // segundos reales

private:
    void Advance(bool completed);

    std::vector<DialogueStep> steps_;
    std::size_t step_ = 0, line_ = 0;
    double typed_ = 0;
    double shown_ = 0;  // segundos reales con el objetivo del paso en pantalla
    int completions_ = 0;
    std::string lastCompleted_;
    double sinceCompleted_ = 1.0e9;
};

// El tutorial del primer sistema.
std::vector<DialogueStep> MakeTutorial();

}  // namespace game
