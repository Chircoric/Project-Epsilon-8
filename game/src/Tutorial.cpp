#include "game/Tutorial.h"

#include <algorithm>

#include "game/Session.h"

namespace game {

namespace {

constexpr double kCharactersPerSecond = 48.0;

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

}  // namespace

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

const std::string& Dialogue::Objective() const {
    return Active() && !Reading() ? steps_[step_].objective : kNoObjective;
}

void Dialogue::Next() {
    if (!Reading()) return;
    if (!LineComplete()) {
        typed_ = static_cast<double>(Characters(Line()->text));
        return;
    }
    ++line_;
    typed_ = 0;
}

void Dialogue::Update(const GameSession& session, double realSeconds) {
    if (!Active()) return;
    if (Reading()) {
        typed_ += realSeconds * kCharactersPerSecond;
        return;
    }
    const DialogueStep& step = steps_[step_];
    if (!step.done || step.done(session)) {
        ++step_;
        line_ = 0;
        typed_ = 0;
    }
}

std::vector<DialogueStep> MakeTutorial() {
    using S = const GameSession&;
    std::vector<DialogueStep> t;

    t.push_back({{{kAi, "Reactivación completa. Hemos caído dentro de la Nebulosa, capitán, y no veo más allá de unos cientos de miles de kilómetros."},
                  {kAi, "Esta nave pesa como una ciudad. Gira despacio y sigue por inercia: el motor empuja, y al soltarlo no se detiene. Espacio frena y la mantiene quieta."},
                  {kAi, "Mueva el ratón o use W, A, S, D para apuntar, y Q y E para girar sobre el eje. Sujete R para dar mando; F lo baja y da marcha atrás."}},
                 "Da mando con R y gana velocidad",
                 [](S g) { return g.Ship().speed > 20.0; }});

    t.push_back({{{kAi, "La interfaz también responde al cursor. Pulse C para activarlo y C otra vez para volver a los mandos."},
                  {kAi, "Con el cursor puede elegir objetivos, cambiar de motor y dar órdenes desde los paneles."}},
                 "Pulsa C para probar el cursor",
                 [](S g) { return g.CursorUsed(); }});

    t.push_back({{{kAi, "Atención a esto, capitán: aún no sabemos qué hay aquí. Los sensores solo dan señales sin identificar."},
                  {kAi, "Para conocer un astro, sus recursos y sus estadísticas hay que escanearlo. Sin esos datos tampoco podremos desplegar nada en él."},
                  {kAi, "Frene con Espacio y espere a quedar quieta. Luego pulse T para abrir la rueda de opciones y elija Escáner. Gasta mucha energía, pero alcanza todo el sistema."}},
                 "Escanea el sistema: Espacio, T y Escáner",
                 [](S g) { return g.ScannedCount() > 1; }});

    t.push_back({{{kAi, "Ya tenemos datos. Elija un astro con Tab y elija Análisis en la rueda: composición, recursos, órbita y su holograma."}},
                 "Abre el análisis de un astro, en la rueda",
                 [](S g) { return g.AnalysisUsed(); }});

    t.push_back({{{kAi, "Necesitamos materiales. Sin reservas, los almacenes pueden fabricar tres estaciones de minería sin coste."},
                  {kAi, "Elija un planeta o un campo de asteroides con Tab, abra la rueda con T y elija Ir: la ruta asistida nos dejará en órbita. Luego, Mina en la rueda. Cuidado: un choque contra un planeta nos haría añicos."}},
                 "Despliega una mina en un planeta o un campo",
                 [](S g) { return g.Stations().Count(StationKind::Mining) > 0; }});

    t.push_back({{{kAi, "Mina en marcha. Trabajará sola, pero no puede enviarnos nada: habrá que venir a recoger en persona."},
                  {kAi, "Para salir de este sistema hace falta antimateria, y eso solo lo fabrica una central. Tenemos materiales para una: vaya a la órbita de la estrella y elija Central en la rueda."}},
                 "Despliega una central en la estrella",
                 [](S g) { return g.Stations().Count(StationKind::Power) > 0; }});

    t.push_back({{{kAi, "Cada estrella da una potencia distinta, y las más calientes exigen más blindaje. Vigile la zona térmica."},
                  {kAi, "La central usa esa luz para mover aceleradores de partículas que fabrican antimateria, y guarda también la materia que suelta la estrella. Cuando quiera cargar, venga a ella y elija Usar en la rueda."}},
                 "Recoge de una estación: Usar, en la rueda",
                 [](S g) { return g.Collected() > 0; }});

    t.push_back({{{kAi, "Ahora lo importante. El crucero, la tecla 2, llega a 0,99 veces la luz: sirve dentro de un sistema, pero no para llegar a otro. No tenemos nada más rápido."},
                  {kAi, "Detecto rastros de algo muy antiguo en este sistema, pero está frío: los sensores normales no lo ven. Podría ser lo que nos falta."},
                  {kAi, "El mismo barrido sirve, y también localiza otros sistemas. Pero para algo así no llega lejos: hay que acercarse."},
                  {kAi, "Abra la rueda con T y elija Escáner, en órbita o quieto. Si no aparece nada, muévase y repita."}},
                 "Encuentra la estructura con el escáner",
                 [](S g) { return g.RevealedStructures() > 0; }});

    t.push_back({{{kAi, "Ahí está. No conozco a quien construyó eso, y no me gusta. Acérquese y elija Usar en la rueda para explorarla."},
                  {kAi, "Desmontar lo que haya dentro llevará un par de horas. Quédese quieta a su lado."}},
                 "Explora la estructura abandonada",
                 [](S g) { return HybridUnlocked(g.Ship()); }});

    t.push_back({{{kAi, "Un emisor de energía negativa. Con él, la burbuja se cierra sin colapsar: la curvatura Híbrida ya está en el panel."},
                  {kAi, "La tecla 3 es la Híbrida: hasta 10 veces la luz. Formar la burbuja cuesta 10 elevado a 24 julios, unas 5,6 kilotoneladas de antimateria."},
                  {kAi, "Lo caro es formarla. Mantenerla gasta cada segundo de diez mil a un millón de veces menos, cuanto más rápido vaya y más tiempo lleve estable. Si la apaga, tendrá que pagarla otra vez."},
                  {kAi, "Pulse 3 y dé mando con R hasta que se forme la burbuja."}},
                 "Forma una burbuja Híbrida: 3 y R",
                 [](S g) { return g.Ship().mode == DriveMode::Warp && g.Ship().warpCharge >= 1.0; }});

    t.push_back({{{kAi, "Hay más estructuras como esa en otros sistemas. Con un segundo emisor podríamos montar el Hiper: la misma geometría, más fina, desde 50 veces la luz, y la burbuja costaría casi nada."},
                  {kAi, "Cada emisor de más lo afina un poco, hasta 250 veces la luz."},
                  {kAi, "La tecla M abre el mapa: los sistemas, sus astros y su modelo en 3D. Fin de los procedimientos, capitán: el resto de la Nebulosa es suyo."}},
                 "", {}});
    return t;
}

}  // namespace game
