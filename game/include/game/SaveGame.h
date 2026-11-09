// SaveGame.h - Partidas guardadas en archivo.
//
// Es un archivo de texto: una linea por dato, con su nombre delante. Se puede abrir y leer, una
// version nueva del juego puede anadir lineas sin romper las antiguas, y lo que se guarda con
// numeros exactos vuelve a ser exactamente lo mismo. La ultima linea es una suma de control:
// un archivo cortado o tocado a mano se rechaza en lugar de cargarse a medias.
//
// Se escribe primero en un archivo temporal y luego se sustituye el bueno, asi que un corte de
// luz a mitad de guardar no destruye la partida anterior.
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
inline constexpr int kSaveVersion = 5;

struct SaveData {
    std::uint32_t seed = 0;
    GameModeId mode = GameModeId::Normal;
    ShipState ship;
    eps::Vec3d absolutePosition;
    std::vector<Station> stations;
    int nextStation = 1;
    std::vector<Anomaly> anomalies;
    std::vector<char> knownSystems;
    std::vector<char> visitedSystems;
    std::vector<std::uint64_t> scanned;  // astros analizados
    double simTime = 0;
    int collected = 0;
    std::size_t tutorialStep = 0;
    bool cursorUsed = false;
    int orbitKind = 0, orbitSystem = -1, orbitIndex = -1;  // TargetRef; kind 0 = sin orbita
    eps::Vec3d orbitOffset;
    eps::Vec3d orbitNormal{0, -1, 0};
    double orbitRate = 0;
    bool docked = false;  // atracada en la estacion que dice la orbita
};

std::string WriteSave(const SaveData& data);
std::optional<SaveData> ReadSave(const std::string& text, std::string* error = nullptr);
void UpgradeFromFirstVersion(SaveData& data);  // ReadSave la llama al leer una partida antigua

std::filesystem::path FilePath(const std::string& utf8);  // las rutas van en UTF-8, como las da SDL

bool SaveToFile(const std::string& path, const SaveData& data, std::string* error = nullptr);
std::optional<SaveData> LoadFromFile(const std::string& path, std::string* error = nullptr);

}  // namespace game
