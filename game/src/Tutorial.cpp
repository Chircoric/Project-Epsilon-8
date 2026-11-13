#include "game/Tutorial.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/Economy.h"
#include "game/Session.h"
#include "game/Units.h"

namespace game {

namespace {

using Kind = TargetRef::Kind;
using eps::Length;

constexpr double kCharactersPerSecond = 48.0;
constexpr double kSkipDelay = 0.8;  // segundos con el objetivo en pantalla antes de que Enter salte un paso opcional

const char* const kAi = "IA DE A BORDO";

// Cuenta caracteres, no bytes: la ñ y las tildes ocupan dos.
std::size_t ByteOffset(const std::string& text, std::size_t characters) {
    std::size_t i = 0;
    while (i < text.size() && characters > 0) {
        ++i;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) ++i;
        --characters;
    }
    return i;
}

std::size_t Characters(const std::string& text) {
    std::size_t count = 0;
    for (const char c : text) count += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    return count;
}

const std::string kNoObjective;

// --- Lo que falta para cumplir un objetivo -------------------------------------------------------

std::string Km(double km) {
    char text[48];
    if (km >= 7.5e6) std::snprintf(text, sizeof(text), "%.2f UA", km / 1.495978707e8);
    else if (km >= 1.0e6) std::snprintf(text, sizeof(text), "%.2f M km", km / 1.0e6);
    else std::snprintf(text, sizeof(text), "%.0f km", km);
    return text;
}

std::string Speed(double kmPerSecond) {
    char text[48];
    std::snprintf(text, sizeof(text), kmPerSecond >= 10.0 ? "%.0f km/s" : "%.1f km/s", kmPerSecond);
    return text;
}

double DistanceTo(const GameSession& g, const TargetRef& t) {
    const std::optional<TargetInfo> info = g.Resolve(t);
    return info ? Length(info->position - g.Ship().position) : 1.0e300;
}

bool Astro(const TargetRef& t) { return t.kind == Kind::Star || t.kind == Kind::Planet || t.kind == Kind::Belt; }

// La nave quieta: lo que piden el escaner y la estacion. Senala Espacio si no lo esta.
bool NeedStill(const GameSession& g, Guidance& out) {
    const ShipState& ship = g.Ship();
    const bool orbit = g.Orbiting().has_value();
    bool still = true;
    if (g.Autopilot()) {
        out.blockers.push_back("Ruta asistida en curso: Espacio la para");
        still = false;
    }
    if (ship.throttle != 0.0) {
        out.blockers.push_back("Mando a cero: no (Espacio)");
        still = false;
    }
    if (!orbit && ship.speed > 1.0) {
        out.blockers.push_back("Nave quieta: no, va a " + Speed(ship.speed) + " (Espacio)");
        still = false;
    }
    if (!still) out.highlight = Highlight::KeyBrake;
    return still;
}

// El barrido: quieta y con energia. Si se puede, senala el escaner de la rueda.
void ScanGuide(const GameSession& g, Guidance& out) {
    if (g.Scanning()) {
        out.blockers.push_back("Barrido en curso: espere a que termine");
        out.highlight = Highlight::None;
        return;
    }
    const double need = g.Rules().scanner.energy, have = g.Ship().energy;
    const bool still = NeedStill(g, out);
    if (have < need) out.blockers.push_back("Energía insuficiente: faltan " + EnergyText(need - have) + " (tiene " + EnergyText(have) + " de " + EnergyText(need) + ")");
    if (still) out.highlight = Highlight::WheelScan;
}

// El objetivo fijado debe ser de una clase: si no, senala Tab.
bool NeedTarget(const TargetRef& t, bool ok, const char* what, Guidance& out) {
    if (ok) return true;
    out.blockers.push_back(std::string(t.kind == Kind::None ? "Objetivo: ninguno fijado" : "Objetivo: no es ") + (t.kind == Kind::None ? "" : what) + " (Tab elige otro)");
    out.highlight = Highlight::KeyTarget;
    return false;
}

// Un astro sin analizar: senala el escaner.
bool NeedScanned(const GameSession& g, const TargetRef& t, Guidance& out) {
    if (g.Scanned(t)) return true;
    out.blockers.push_back("Datos del astro: no (escáner)");
    ScanGuide(g, out);
    return false;
}

// Hay que estar a menos de range de su llegada, como mide GameSession::InRange: si no, senala Ir.
bool NeedRange(const GameSession& g, const TargetRef& t, double range, Guidance& out) {
    if (g.InRange(t, range)) return true;
    const std::optional<TargetInfo> info = g.Resolve(t);
    const double distance = DistanceTo(g, t), limit = range + (info ? info->arrival : 0.0);
    out.blockers.push_back("Distancia: " + Km(distance) + ", hace falta menos de " + Km(limit) + (g.Autopilot() ? " (en ruta)" : " (Ir)"));
    out.highlight = g.Autopilot() ? Highlight::None : Highlight::WheelGo;
    return false;
}

}  // namespace

const char* Name(WheelOption option) {
    static const char* const kNames[] = {"Escáner", "Ir", "Órbita", "Atracar", "A pie", "Usar", "Drones", "Central", "Estación", "Mejorar", "Análisis", "Mapa", "Disparar", "Trayectoria", "Taller"};
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<std::size_t>(WheelOption::Count));
    const int i = static_cast<int>(option);
    return i >= 0 && i < static_cast<int>(WheelOption::Count) ? kNames[i] : "";
}

HighlightTarget Locate(Highlight h) {
    using W = HighlightTarget::Where;
    const auto key = [](const char* cap, const char* label) { return HighlightTarget{W::Key, cap, WheelOption::Count, label}; };
    const auto wheel = [](WheelOption o) { return HighlightTarget{W::Wheel, "T", o, Name(o)}; };
    const auto panel = [](const char* label) { return HighlightTarget{W::Panel, nullptr, WheelOption::Count, label}; };
    switch (h) {
        case Highlight::None: case Highlight::Count: return {};
        case Highlight::KeyThrottle: return key("R", "Sujete R: mando");
        case Highlight::KeyBrake: return key("Espacio", "Espacio: frenar");
        case Highlight::KeyWheel: return key("T", "T: opciones");
        case Highlight::KeyCursor: return key("C", "C: cursor");
        case Highlight::KeyTarget: return key("Tab", "Tab: objetivo");
        case Highlight::KeyMap: return key("M", "M: mapa");
        case Highlight::KeyHybrid: return key("3", "3: Híbrida");
        case Highlight::WheelScan: return wheel(WheelOption::Scan);
        case Highlight::WheelGo: return wheel(WheelOption::Go);
        case Highlight::WheelUse: return wheel(WheelOption::Use);
        case Highlight::WheelDrones: return wheel(WheelOption::Drones);
        case Highlight::WheelPower: return wheel(WheelOption::Power);
        case Highlight::WheelShipyard: return wheel(WheelOption::Shipyard);
        case Highlight::WheelAnalysis: return wheel(WheelOption::Analysis);
        case Highlight::ShipPanel: return panel("Panel de la nave");
        case Highlight::SensorsPanel: return panel("Sensores");
        case Highlight::TargetPanel: return panel("Objetivo");
        case Highlight::RadarPanel: return panel("Radar");
        case Highlight::ScanButton: return panel("Escáner");
        case Highlight::PinnedContact: return panel("Objetivo fijado");
        case Highlight::HybridButton: return panel("Híbrida");
        case Highlight::EchoMarker: return {W::Marker, nullptr, WheelOption::Count, "Pista del escáner"};
    }
    return {};
}

void Dialogue::Start(std::vector<DialogueStep> steps) {
    steps_ = std::move(steps);
    step_ = line_ = 0;
    typed_ = 0;
}

void Dialogue::Resume(std::size_t step) {
    step_ = std::min(step, steps_.size());
    line_ = 0;
    typed_ = 0;
}

void Dialogue::SkipLines() {
    if (Active()) line_ = steps_[step_].lines.size();
}

void Dialogue::Replay() {
    if (!Active()) return;
    line_ = 0;
    typed_ = 0;
}

void Dialogue::Stop() {
    steps_.clear();
    step_ = line_ = 0;
}

bool Dialogue::Reading() const { return Active() && line_ < steps_[step_].lines.size(); }

const DialogueLine* Dialogue::Line() const { return Reading() ? &steps_[step_].lines[line_] : nullptr; }

bool Dialogue::LineComplete() const {
    const DialogueLine* line = Line();
    return !line || static_cast<std::size_t>(typed_) >= Characters(line->text);
}

std::string Dialogue::VisibleText() const {
    const DialogueLine* line = Line();
    return line ? line->text.substr(0, ByteOffset(line->text, static_cast<std::size_t>(typed_))) : std::string();
}

const std::string& Dialogue::Tag() const { return Active() ? steps_[step_].tag : kNoObjective; }

const std::string& Dialogue::Objective() const {
    return Active() && !Reading() ? steps_[step_].objective : kNoObjective;
}

Guidance Dialogue::Guide(const GameSession& session, const TargetRef& target) const {
    Guidance out;
    if (Objective().empty()) return out;
    const DialogueStep& step = steps_[step_];
    out.highlight = step.highlight;
    if (step.guide) step.guide(session, target, out);
    return out;
}

void Dialogue::Next() {
    // Quien pasa las lineas con Enter no se salta el paso sin haber visto su objetivo.
    if (Optional()) {
        if (shown_ >= kSkipDelay) Advance(false);
        return;
    }
    if (!Reading()) return;
    if (!LineComplete()) {
        typed_ = static_cast<double>(Characters(Line()->text));
        return;
    }
    ++line_;
    typed_ = 0;
}

void Dialogue::Advance(bool completed) {
    if (completed) {
        ++completions_;
        lastCompleted_ = steps_[step_].objective;
        sinceCompleted_ = 0.0;
    }
    ++step_;
    line_ = 0;
    typed_ = 0;
    shown_ = 0;
}

void Dialogue::Update(const GameSession& session, double realSeconds) {
    sinceCompleted_ += realSeconds;
    if (!Active()) return;
    if (Reading()) {
        typed_ += realSeconds * kCharactersPerSecond;
        shown_ = 0;
        return;
    }
    shown_ += realSeconds;
    const DialogueStep& step = steps_[step_];
    if (!step.done) Advance(false);
    else if (step.done(session)) Advance(!step.objective.empty());
}

std::vector<DialogueStep> MakeTutorial() {
    using S = const GameSession&;
    using T = const TargetRef&;
    using G = Guidance&;
    std::vector<DialogueStep> t;

    // Volar: mando y frenada.
    {
        DialogueStep s;
        s.lines = {{kAi, "Reactivación completa. Hemos caído dentro de la Nebulosa, capitán."},
                   {kAi, "No veo más allá de unos cientos de miles de kilómetros."},
                   {kAi, "Esta nave pesa como una ciudad: gira despacio y sigue por inercia."},
                   {kAi, "Mueva el ratón o use W, A, S y D para apuntar. Q y E la giran sobre su eje."},
                   {kAi, "Sujete R para dar mando. F lo baja y da marcha atrás."}};
        s.objective = "Da mando con R y gana velocidad";
        s.highlight = Highlight::KeyThrottle;
        s.done = [](S g) { return g.Ship().speed > 20.0; };
        s.guide = [](S g, T, G out) { out.blockers.push_back("Velocidad: " + Speed(g.Ship().speed) + " de 20 km/s"); };
        t.push_back(std::move(s));
    }
    {
        DialogueStep s;
        s.lines = {{kAi, "Al soltar el mando no se detiene: el motor empuja, y la nave sigue por inercia."},
                   {kAi, "Espacio frena y la mantiene quieta. Muchas órdenes piden la nave parada."}};
        s.objective = "Frena con Espacio hasta quedar quieta";
        s.highlight = Highlight::KeyBrake;
        s.done = [](S g) { return g.Ship().speed <= 1.0 && g.Ship().throttle == 0.0; };
        s.guide = [](S g, T, G out) {
            out.blockers.push_back("Velocidad: " + Speed(g.Ship().speed) + ", quieta por debajo de 1 km/s");
            if (g.Ship().throttle != 0.0) out.blockers.push_back("Mando a cero: no (Espacio)");
        };
        t.push_back(std::move(s));
    }

    // El cursor.
    {
        DialogueStep s;
        s.lines = {{kAi, "La interfaz también responde al cursor. C lo activa; C otra vez vuelve a los mandos."},
                   {kAi, "Con el cursor se eligen objetivos y se pliegan o despliegan los paneles."}};
        s.objective = "Pulsa C para probar el cursor";
        s.highlight = Highlight::KeyCursor;
        s.done = [](S g) { return g.CursorUsed(); };
        t.push_back(std::move(s));
    }

    // El escaner.
    {
        DialogueStep s;
        s.lines = {{kAi, "Aún no sabemos qué hay aquí: los sensores solo dan señales sin identificar."},
                   {kAi, "Para conocer un astro, sus recursos y sus datos, hay que escanearlo."},
                   {kAi, "Sin esos datos tampoco podremos desplegar nada en él."},
                   {kAi, "El escáner pide la nave quieta y mucha energía, pero alcanza todo el sistema."},
                   {kAi, "Pulse T para abrir la rueda de opciones y elija Escáner."}};
        s.objective = "Escanea el sistema: T y Escáner";
        s.highlight = Highlight::WheelScan;
        s.done = [](S g) { return g.ScannedCount() > 1; };
        s.guide = [](S g, T, G out) { ScanGuide(g, out); };
        t.push_back(std::move(s));
    }

    // El analisis.
    {
        DialogueStep s;
        s.lines = {{kAi, "Ya tenemos datos. Tab fija como objetivo la siguiente señal."},
                   {kAi, "Con un astro fijado, Análisis en la rueda enseña su composición, sus recursos y su órbita."}};
        s.objective = "Fija un astro y abre su análisis";
        s.highlight = Highlight::WheelAnalysis;
        s.done = [](S g) { return g.AnalysisUsed(); };
        s.guide = [](S, T target, G out) { NeedTarget(target, Astro(target), "un astro", out); };
        t.push_back(std::move(s));
    }

    // Los drones mineros.
    {
        DialogueStep s;
        s.lines = {{kAi, "Necesitamos materiales. En la popa llevamos dos bahías con cuatro drones mineros."},
                   {kAi, "Van, minan y vuelven solos, y descargan en nuestra bodega."},
                   {kAi, "Fije un planeta o un campo de asteroides con Tab."},
                   {kAi, "Ir, en la rueda, nos lleva en ruta asistida hasta su órbita."},
                   {kAi, "Allí, elija Drones en la rueda. Cuidado: un choque contra un planeta nos haría añicos."}};
        s.objective = "Manda los drones a un planeta o un campo";
        s.highlight = Highlight::WheelDrones;
        s.done = [](S g) { return g.DroneSite().has_value() || g.DronesOut() > 0; };
        s.guide = [](S g, T target, G out) {
            if (!NeedTarget(target, target.kind == Kind::Planet || target.kind == Kind::Belt, "un planeta ni un campo", out)) return;
            if (!NeedScanned(g, target, out)) return;
            if (!NeedRange(g, target, g.Rules().drones.range, out)) return;
            if (g.Ship().mode != DriveMode::Impulse) out.blockers.push_back("Motor: impulso, no (tecla 1)");
        };
        t.push_back(std::move(s));
    }

    // La estacion origami: se puede guardar para otro sitio.
    {
        DialogueStep s;
        s.lines = {{kAi, "Drones fuera. Repiten viajes hasta que los recoja: Drones otra vez en la rueda."},
                   {kAi, "Si encendemos otro motor que el impulso, vuelven solos."},
                   {kAi, "Alrededor de las bodegas viaja plegada la estación origami."},
                   {kAi, "Se abre en un anillo de un kilómetro que construye naves con el material que le entreguemos."},
                   {kAi, "Para soltarla: fuera de órbita, quieta con Espacio y con el impulso. Luego, Estación en la rueda."},
                   {kAi, "Si prefiere guardarla para un sitio mejor, pulse Enter y seguimos."}};
        s.objective = "Suelta la estación origami (Enter: guardarla)";
        s.highlight = Highlight::WheelShipyard;
        s.optional = true;
        s.done = [](S g) { return !g.Ship().stationAboard; };
        s.guide = [](S g, T, G out) {
            std::string why;
            if (g.CanDeployShipyard(&why)) return;
            out.blockers.push_back(why);
            if (g.Orbiting() || g.Ship().speed > g.Rules().stations.shipyardDeploySpeed) out.highlight = Highlight::KeyBrake;
        };
        t.push_back(std::move(s));
    }

    // La central.
    {
        DialogueStep s;
        s.lines = {{kAi, "Para salir de este sistema hace falta antimateria, y solo la fabrica una central."},
                   {kAi, "Tenemos materiales para una. Se despliega en la órbita de una estrella."},
                   {kAi, "Fije la estrella con Tab, vaya con Ir y elija Central en la rueda."}};
        s.objective = "Despliega una central en la estrella";
        s.highlight = Highlight::WheelPower;
        s.done = [](S g) { return g.Stations().Count(StationKind::Power) > 0; };
        s.guide = [](S g, T target, G out) {
            if (!NeedTarget(target, target.kind == Kind::Star, "una estrella", out)) return;
            if (!NeedScanned(g, target, out)) return;
            const eps::World& w = g.GetWorld();
            if (target.system >= 0 && static_cast<std::size_t>(target.system) < w.systems.size()) {
                const int need = YieldOf(w.systems[static_cast<std::size_t>(target.system)]->StarAt(target.index)).requiredTier;
                if (g.Ship().heatShieldTier < need)
                    out.blockers.push_back("Blindaje: nivel " + std::to_string(g.Ship().heatShieldTier) + ", esta estrella pide " + std::to_string(need));
            }
            NeedRange(g, target, g.Rules().stations.deployRange, out);
        };
        t.push_back(std::move(s));
    }

    // Recoger de la central.
    {
        DialogueStep s;
        s.lines = {{kAi, "Cada estrella da una potencia distinta, y las más calientes piden más blindaje."},
                   {kAi, "La central usa esa luz para fabricar antimateria y guarda la materia que suelta la estrella."},
                   {kAi, "Para cargar, fije la central, acérquese y elija Usar en la rueda."}};
        s.objective = "Recoge de la central: Usar, en la rueda";
        s.highlight = Highlight::WheelUse;
        s.done = [](S g) { return g.Collected() > 0; };
        s.guide = [](S g, T target, G out) {
            const Station* station = target.kind == Kind::Station ? g.Stations().Find(target.index) : nullptr;
            if (!NeedTarget(target, station && station->kind == StationKind::Power, "la central", out)) return;
            if (g.Ship().relayTier <= 0) NeedRange(g, target, g.Rules().ship.interactRange, out);
        };
        t.push_back(std::move(s));
    }

    // La estructura: la pista del escaner, en el radar y en pantalla.
    {
        DialogueStep s;
        s.lines = {{kAi, "Ahora lo importante. El crucero, la tecla 2, llega a 0,99 veces la luz."},
                   {kAi, "Sirve dentro de un sistema, pero no para llegar a otro. No tenemos nada más rápido."},
                   {kAi, "Detecto rastros de algo muy antiguo en este sistema, pero está frío: los sensores normales no lo ven."},
                   {kAi, "El escáner lo oye como un eco, aunque no alcance a resolverlo."},
                   {kAi, "Escanee quieta o en órbita. El eco dará su rumbo y su distancia: lo marcaré en el radar y en pantalla."},
                   {kAi, "Acérquese a la marca y repita el barrido hasta encontrarlo."}};
        s.objective = "Encuentra la estructura con el escáner";
        s.highlight = Highlight::EchoMarker;
        s.tag = kStructureStep;
        s.done = [](S g) { return g.RevealedStructures() > 0; };
        s.guide = [](S g, T, G out) {
            const std::optional<eps::Vec3d> echo = g.EchoPosition();
            if (!echo) {
                out.blockers.push_back("Pista: aún no hay (escáner)");
                ScanGuide(g, out);
                return;
            }
            const double distance = Length(*echo - g.Ship().position);
            const bool near = distance <= g.ScanRange();
            out.blockers.push_back("Pista: a " + Km(distance) + (near ? ", al alcance: repita el barrido" : ", acérquese a la marca y repita"));
            if (near) ScanGuide(g, out);
        };
        t.push_back(std::move(s));
    }

    // Explorarla.
    {
        DialogueStep s;
        s.lines = {{kAi, "Ahí está. No conozco a quien construyó eso, y no me gusta."},
                   {kAi, "Fíjela, acérquese y elija Usar en la rueda para explorarla."},
                   {kAi, "Desmontar lo que haya dentro llevará un par de horas: quédese quieta a su lado."}};
        s.objective = "Explora la estructura abandonada";
        s.highlight = Highlight::WheelUse;
        s.done = [](S g) { return HybridUnlocked(g.Ship()); };
        s.guide = [](S g, T target, G out) {
            if (g.Exploring()) {
                out.blockers.push_back("Explorando: quédese quieta a su lado");
                out.highlight = Highlight::None;
                return;
            }
            bool ruin = false;
            if (target.kind == Kind::Anomaly)
                for (const Anomaly& a : g.Anomalies()) ruin = ruin || (a.id == target.index && a.kind == AnomalyKind::Derelict);
            if (!NeedTarget(target, ruin, "la estructura", out)) return;
            NeedRange(g, target, g.Rules().ship.interactRange, out);
        };
        t.push_back(std::move(s));
    }

    // La burbuja Hibrida: lo que cuesta y lo que hay.
    {
        DialogueStep s;
        s.lines = {{kAi, "Un emisor de energía negativa. Con él, la burbuja se cierra sin colapsar."},
                   {kAi, "La curvatura Híbrida ya está en el panel de la nave: la tecla 3, hasta 10 veces la luz."},
                   {kAi, "Formar la burbuja cuesta 10 elevado a 24 julios: unas 5,6 kilotoneladas de antimateria."},
                   {kAi, "Mantenerla gasta cada segundo de diez mil a un millón de veces menos."},
                   {kAi, "Si la apaga, tendrá que pagarla otra vez."},
                   {kAi, "Antes, apunte la proa a espacio libre: a esa velocidad, chocar con la estructura sería el fin."},
                   {kAi, "Pulse 3 y dé mando con R hasta que se forme la burbuja."}};
        s.objective = "Forma una burbuja Híbrida: 3 y R";
        s.highlight = Highlight::KeyHybrid;
        s.done = [](S g) { return g.Ship().mode == DriveMode::Warp && g.Ship().warpCharge >= 1.0; };
        s.guide = [](S g, T, G out) {
            const ShipState& ship = g.Ship();
            const DriveRules& drive = g.Rules().drive;
            // Tras explorar, la proa sigue hacia la estructura: con mando, la nave se estrella contra ella.
            const eps::Vec3d heading = Heading(ship);
            for (const Anomaly& a : g.Anomalies()) {
                const eps::Vec3d to = g.AnomalyPosition(a) - ship.position;
                const double distance = Length(to);
                if (distance > 1.0e5 || distance < 1.0 || eps::Dot(heading, to / distance) < 0.85) continue;  // a menos de 100 000 km y a menos de unos 30 grados
                out.blockers.push_back("Rumbo: la proa apunta a " + a.name + ", a " + Km(distance) + ": gire antes de dar mando");
                out.highlight = Highlight::None;
                break;
            }
            const double cost = FormationCost(ship, DriveMode::Warp, drive);
            out.blockers.push_back("Burbuja: " + EnergyText(FormationEnergy(ship, DriveMode::Warp, drive)) + " = " + MassText(cost) + " de antimateria; tiene " + MassText(ship.antimatter));
            if (ship.mode != DriveMode::Warp) {
                if (!CanCurve(ship, DriveMode::Warp, drive)) out.blockers.push_back("Antimateria: faltan " + MassText(std::max(cost - ship.antimatter, 0.0)) + " (recoja de la central)");
                return;
            }
            char line[64];
            std::snprintf(line, sizeof(line), "Formando: %.0f %% (sujete R)", std::clamp(ship.warpCharge, 0.0, 1.0) * 100.0);
            out.blockers.push_back(line);
            out.highlight = Highlight::KeyThrottle;
        };
        t.push_back(std::move(s));
    }

    // Despedida: el Hiper y el mapa.
    {
        DialogueStep s;
        s.lines = {{kAi, "Hay más estructuras como esa en otros sistemas."},
                   {kAi, "Con un segundo emisor podríamos montar el Hiper: desde 50 veces la luz, y la burbuja costaría casi nada."},
                   {kAi, "Cada emisor de más lo afina un poco, hasta 250 veces la luz."},
                   {kAi, "La tecla M abre el mapa: los sistemas, sus astros y su modelo en 3D."},
                   {kAi, "Fin de los procedimientos, capitán. El resto de la Nebulosa es suyo."}};
        s.highlight = Highlight::KeyMap;
        t.push_back(std::move(s));
    }
    return t;
}

}  // namespace game
