// GameModels.h - Modelos provisionales del juego, hechos por codigo y medidos en metros.
//
// Son maquetas: dan silueta y tamano a cada cosa, y se sustituyen cargando un .glb con
// LoadGltf y BuildModel. Solo necesitan una funcion que suba mallas a la GPU, asi que tambien
// se pueden construir en las pruebas, sin ventana.
#pragma once

#include <functional>

#include "epsilon/Models.h"
#include "game/Anomalies.h"
#include "game/HullZones.h"
#include "game/ShipModules.h"
#include "game/OnFoot.h"
#include "game/Stations.h"

namespace game {

struct GameAssets {
    eps::ModelRef ship;      // cada pieza lleva sus triangulos en collision: ahi se clavan los crateres
    bool customShip = false; // la nave viene de un .glb y no de la maqueta
    // Lo que asoma de un crater y lo que sale despedido: vigas en I, chapas arrancadas y astillas.
    // Miden 1 de largo a lo largo de +Z y se escalan al dibujarlas.
    eps::MeshId girder = eps::kNoMesh, plate = eps::kNoMesh, shard = eps::kNoMesh;
    // Modulos exteriores (por ShipModuleKind) y mejoras visibles, con la base en el origen y +Y hacia
    // fuera del casco, en metros.
    std::array<eps::MeshId, static_cast<std::size_t>(ShipModuleKind::Count)> moduleMeshes{};
    eps::MeshId dome = eps::kNoMesh, thrusterPod = eps::kNoMesh, heatCap = eps::kNoMesh;
    // Rendija de iones de los propulsores de maniobra (+Y hacia fuera) y caja de lado 1 para el ducto
    // de atraque, que se estira al dibujarlo.
    eps::MeshId rcsSlit = eps::kNoMesh, unitBox = eps::kNoMesh;
    eps::ModelRef miningStation, powerStation;
    eps::ModelRef hostileShip, neutralShip;
    eps::ModelRef hostileStation, neutralStation;
    eps::ModelRef derelict;
    std::vector<eps::MeshId> debris;  // polvo de la nebulosa y restos
    eps::ModelRef miningDeck, powerDeck;  // las cubiertas, en metros: lo mismo que se pisa
    eps::ModelRef pilot, crew;            // personas de 1,8 m, con los pies en el origen
};

using MeshUpload = std::function<eps::MeshId(const eps::MeshData&)>;

// hull es una textura de chapas; 0 = color liso.
GameAssets BuildGameAssets(const MeshUpload& upload, eps::TextureId hull = 0);

const eps::ModelRef& ModelFor(const GameAssets& assets, const Anomaly& anomaly);

// La nave desde un .glb o .gltf, en metros, con +Y arriba y la proa hacia -Z (en Blender: proa
// hacia +Y y arriba +Z). Cada nodo es una pieza; su nombre dice de que zona del casco es
// (ZoneOfPart). texture carga la textura de una pieza por su ruta; puede ser nula.
// Devuelve nullptr si no se pudo leer, con el motivo en error.
eps::ModelRef LoadShipModel(const std::string& path, const MeshUpload& upload, const std::function<eps::TextureId(const std::string&)>& texture, std::string* error = nullptr);

// La forma de las zonas de una nave cargada (SetZoneLayout): sus piezas, repartidas por zonas con
// ZoneOfPart, se cubren con bolas de unos 30 m. Necesita los triangulos de cada pieza (collision).
std::shared_ptr<ZoneLayout> BuildZoneLayout(const eps::ModelAsset& ship);

// Las ranuras y las piezas de una nave (SetShipDesign). Una pieza por cada pieza del modelo, en su
// orden. Las ranuras exteriores son los nodos vacios "hp_..." del .glb (LoadGltfMarkers); sin
// ninguno, las de la maqueta. Las interiores, las bahias de cada zona: hace falta SetZoneLayout antes.
std::shared_ptr<ShipDesign> BuildShipDesign(const eps::ModelAsset& ship, const std::vector<eps::ImportedMarker>& markers);

}  // namespace game
