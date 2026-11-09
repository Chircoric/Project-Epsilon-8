// Tutorial.h - Dialogos con objetivos.
//
// Un paso son unas lineas que se leen y una condicion que el jugador debe cumplir. La escena no
// se detiene mientras tanto. Sirve para cualquier conversacion del juego: basta con otra lista.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace game {

class GameSession;

struct DialogueLine {
    std::string speaker;
    std::string text;
};

struct DialogueStep {
    std::vector<DialogueLine> lines;
    std::string objective;                          // vacio = solo conversacion
    std::function<bool(const GameSession&)> done;   // nulo = pasa al terminar de leer
};

class Dialogue {
public:
    void Start(std::vector<DialogueStep> steps);
    void Stop();
    void Resume(std::size_t step);  // sigue por este paso, tras cargar una partida

    // Escribe el texto poco a poco y pasa al siguiente paso cuando se cumple su condicion.
    void Update(const GameSession& session, double realSeconds);

    // Enter o clic: completa la linea o pasa a la siguiente.
    void Next();

    bool Active() const { return step_ < steps_.size(); }
    bool Reading() const;                 // hay texto en pantalla
    const DialogueLine* Line() const;
    std::string VisibleText() const;      // lo que ya se ha escrito
    bool LineComplete() const;
    const std::string& Objective() const; // lo que falta por hacer
    std::size_t Step() const { return step_; }
    std::size_t StepCount() const { return steps_.size(); }

private:
    std::vector<DialogueStep> steps_;
    std::size_t step_ = 0, line_ = 0;
    double typed_ = 0;
};

// El tutorial del primer sistema.
std::vector<DialogueStep> MakeTutorial();

}  // namespace game
