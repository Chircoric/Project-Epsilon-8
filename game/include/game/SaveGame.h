// SaveGame.h - Partidas guardadas: el mundo en un archivo y cada jugador en el suyo.
//
// Una partida es una carpeta:
//
//   <partida>/mundo.sav             lo que existe aunque no haya nadie: reloj, estaciones, anomalias,
//                                   la especie y las naves (con su dueño)
//   <partida>/jugadores/<id>.sav    lo de cada jugador: que nave lleva, el tutorial y lo que conoce
//
// Las naves van en el mundo y no en el jugador: ocupan un sitio, pueden estar atracadas en una
// estacion y, en cooperativo, son de varios (docs/multijugador.md, apartado 5).
//
// Los dos son archivos de texto: una linea por dato, con su nombre delante. Se pueden abrir y leer, y
// lo que se guarda con numeros exactos vuelve a ser exactamente lo mismo. La ultima linea de cada uno
// es una suma de control: un archivo cortado o tocado a mano se rechaza en lugar de cargarse a medias.
//
// Cada archivo se escribe primero en uno temporal y luego sustituye al bueno, asi que un corte de luz
// a mitad de guardar no destruye la partida anterior.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "game/Anomalies.h"
#include "game/Balance.h"
#include "game/Ship.h"
#include "game/Stations.h"

namespace game {

// 2: antimateria, modo crucero y centrales a otra escala. 3: armas, computador y cascos.
// 4: sin materia oscura; emisores de energia negativa. 5: escudos, calor y refrigerantes.
// 6: escala de combate x20 y clases de nave (lineas "clase"); 7: inventario de elementos (linea "elementos").
// 8: armamento humano (lineas "municion" y "armamento"; tipos de dano explosivo y nuclear; modulos de arma).
// 9: zonas en los alienigenas. 10: niveles de estacion (lineas "nivel"), alerta por sistema ("alerta")
// y rendicion y revancha ("rendicion"). 11: la primera estructura, junto a la estrella inicial.
// 12: el tutorial reorganizado, con otros pasos (la linea "tutorial" cuenta los nuevos).
// 13: mundo y jugador en archivos distintos; las naves, en bloques "nave" dentro del mundo; sellos de
// tiempo en estaciones y alerta; sin los huecos de las minas retiradas.
// Solo se carga la version actual: una partida de otra version se rechaza con un mensaje, sin migrarla.
inline constexpr int kSaveVersion = 13;

// Una nave, dentro del mundo.
struct ShipSave {
    int id = 1;
    ShipState ship;
    eps::Vec3d absolutePosition;
    int orbitKind = 0, orbitSystem = -1, orbitIndex = -1;  // TargetRef; kind 0 = sin orbita
    eps::Vec3d orbitOffset;
    eps::Vec3d orbitNormal{0, -1, 0};
    double orbitRate = 0;
    bool docked = false;  // atracada en la estacion que dice la orbita
    int homeStation = -1; // la ultima estacion propia donde atraco: ahi reaparece si se pierde
    bool lost = false;    // estaba perdida al guardar: al cargar, reaparece
    double clock = 0;     // el reloj de su burbuja (s): las naves que no estan juntas pueden llevar fechas distintas
    bool away = false;    // fuera del mundo: su jugador no esta conectado (GamePhase::Away)
};

struct WorldSave {
    std::uint32_t seed = 0;
    GameModeId mode = GameModeId::Normal;
    double simTime = 0;
    std::vector<Station> stations;
    int nextStation = 1;
    std::vector<Anomaly> anomalies;
    SpeciesState species;  // alerta por sistema, rendicion y revancha
    std::vector<ShipSave> ships;
};

struct PlayerSave {
    std::string id = "local";   // lo crea el juego una vez y lo guarda en ajustes.txt
    std::string name = "Piloto";
    int ship = 1;               // la nave que lleva (ShipSave::id)
    std::size_t tutorialStep = 0;
    bool cursorUsed = false, analysisUsed = false;
    int collected = 0;
    int kills = 0;
    std::vector<char> knownSystems;
    std::vector<char> visitedSystems;
    std::vector<std::uint64_t> scanned;  // astros analizados
};

// Una partida para un jugador: el mundo y lo suyo.
struct SaveData {
    WorldSave world;
    PlayerSave player;
    // La nave de este jugador, o la primera si la suya no esta; nullptr si el mundo no tiene ninguna.
    const ShipSave* Ship() const;
    ShipSave* Ship();
};

std::string WriteWorld(const WorldSave& world);
std::optional<WorldSave> ReadWorld(const std::string& text, std::string* error = nullptr);
std::string WritePlayer(const PlayerSave& player);
std::optional<PlayerSave> ReadPlayer(const std::string& text, std::string* error = nullptr);

// Los dos documentos seguidos, el mundo primero: para las pruebas y, en red, para mandarle a un
// jugador que entra todo lo que necesita de una vez.
std::string WriteSave(const SaveData& data);
std::optional<SaveData> ReadSave(const std::string& text, std::string* error = nullptr);

std::filesystem::path FilePath(const std::string& utf8);  // las rutas van en UTF-8, como las da SDL

// La carpeta de una partida. Cargar sin archivo de ese jugador no es un error: es un jugador nuevo en
// un mundo que ya existe, y se le da la primera nave. Sin mundo, el error es "sin partida guardada".
bool SaveGame(const std::string& folder, const SaveData& data, std::string* error = nullptr);
std::optional<SaveData> LoadGame(const std::string& folder, const std::string& playerId, std::string* error = nullptr);
void DeleteGame(const std::string& folder);  // el mundo y todos sus jugadores
// El identificador de un jugador, limpio para usarlo como nombre de archivo.
std::string SafePlayerId(const std::string& id);

}  // namespace game
