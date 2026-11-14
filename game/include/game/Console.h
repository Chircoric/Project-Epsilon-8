// Console.h - Los comandos de consola del juego y quien puede usarlos.
//
// Antes vivian en main.cpp, atados a la ventana. Ahora estan en la biblioteca comun: los usan el juego
// (F1, --cmd) y el servidor sin ventana (entrada estandar), y las pruebas los ejecutan sin GPU.
//
// Los comandos son trucos y herramientas de administracion (saltar, creativo, nivel, aparecer, tiempo...):
// solo los puede usar quien tenga una categoria de personal. Un jugador corriente no (docs/multijugador.md,
// decision 8 y R10). La categoria la da el servidor: no la dice el cliente.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "epsilon/Features.h"
#include "game/Session.h"

namespace game {

// De menos a mas. Las categorias de personal son las que no son Player.
enum class Rank : std::uint8_t {
    Player,         // jugador: sin comandos
    Moderator,      // moderador
    Admin,          // administrador
    Owner,          // dueño del servidor
    Developer,      // desarrollador
};

const char* Name(Rank rank);                              // "jugador", "moderador", "administrador", "dueño", "desarrollador"
bool ParseRank(const std::string& word, Rank& out);       // acepta tambien "dueno", "admin" y "dev"; no distingue mayusculas
// Solo el personal puede usar comandos.
constexpr bool CanUseCommands(Rank rank) { return rank >= Rank::Moderator; }

// Quien tiene que categoria, por el id del jugador (PlayerState::id, el que manda en el saludo). Quien
// no esta en la lista es un jugador. Se guarda en un archivo de texto, una linea por persona:
//
//   # comentario
//   <id> <categoria>
class RankBook {
public:
    Rank Of(const std::string& playerId) const;
    void Set(const std::string& playerId, Rank rank);  // Player lo quita de la lista
    const std::map<std::string, Rank>& All() const { return ranks_; }

    // false si no se pudo leer o escribir; en Load, un archivo que no existe no es un error (lista vacia).
    bool Load(const std::string& path, std::string* error = nullptr);
    bool Save(const std::string& path, std::string* error = nullptr) const;

private:
    std::map<std::string, Rank> ranks_;
};

// Registra todos los comandos del juego en el registro del motor. target es el objetivo que comparten
// los comandos (objetivo, ir, disparar...); tiene que sobrevivir al registro.
void RegisterGameCommands(eps::CommandRegistry& commands, GameSession& session, TargetRef& target);

// Ejecuta una linea como alguien de esa categoria. Si no puede, no se ejecuta nada: devuelve el motivo y
// allowed queda a false. Es el unico camino por el que una peticion de un jugador debe llegar al registro.
std::string RunCommand(eps::CommandRegistry& commands, Rank rank, const std::string& line, bool* allowed = nullptr);

}  // namespace game
