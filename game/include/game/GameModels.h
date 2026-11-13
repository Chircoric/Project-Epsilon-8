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
    eps::ModelRef powerStation;
    // Estacion origami (modelos/estacion.gltf) con su animacion "despliegue": la pose de reposo es la
    // plegada. origamiTrack dice, por cada parte, su indice en la animacion (-1 = no se mueve). Las
    // partes "andamio_" son el andamio provisional que la sujeta a las bodegas: va con la nave y se
    // desecha al soltarla (GameLayer::ScaffoldMotion).
    // Sin el archivo, origami es nulo y la estacion se dibuja con la maqueta de la central.
    eps::ModelRef origami;
    std::shared_ptr<const eps::ModelAnimation> origamiDeploy;
    std::vector<int> origamiTrack;
    // Bahia de drones (modelos/bahia_drones.gltf) y dron minero (modelos/dron.gltf), en metros, ejes
    // del motor: la bahia con la base en el origen y -Y hacia fuera del casco; el dron con la proa a +Z
    // y -Y arriba. droneSpots son los puestos de los drones posados (nodos "dron_1", "dron_2").
    // Sin archivos: la bahia es moduleMeshes[DroneBay] y el dron, una maqueta.
    eps::ModelRef droneBay, drone;
    std::vector<eps::Vec3f> droneSpots{{-13.0f, -25.9f, 0.0f}, {13.0f, -25.9f, 0.0f}};
    eps::ModelRef hostileShip, neutralShip;
    // Naves de dron, corveta y fragata (por ShipClassId), de modelos/alien_dron.gltf, alien_corveta.gltf
    // y alien_fragata.gltf (LoadCombatModels). La neutral es la misma con otros colores. Sin archivo,
    // la maqueta de BuildAlienShip.
    std::array<eps::ModelRef, kAlienClassModelCount> hostileClassShips{}, neutralClassShips{};
    eps::ModelRef hostileStation, neutralStation;
    // Torretas humanas cuadradas de modelos/torretas.gltf (por ShipModuleKind), con la base en el origen
    // y -Y hacia fuera del casco, como la bahia de drones. Nulo: se dibuja moduleMeshes.
    std::array<eps::ModelRef, static_cast<std::size_t>(ShipModuleKind::Count)> moduleModels{};
    // Zonas de cada maqueta alienigena (por AlienModel): su forma, y la zona de cada pieza en su orden.
    std::array<std::shared_ptr<const ZoneLayout>, kAlienModelCount> alienZones{};
    std::array<std::vector<int>, kAlienModelCount> alienPartZones{};
    eps::ModelRef derelict;
    eps::ModelRef armorPlanet;  // el planeta coraza: una esfera blindada del tamano de Jupiter
    std::vector<eps::MeshId> debris;  // polvo de la nebulosa y restos
    eps::ModelRef powerDeck;              // la cubierta, en metros: lo mismo que se pisa
    eps::ModelRef pilot, crew;            // personas de 1,8 m, con los pies en el origen
};

using MeshUpload = std::function<eps::MeshId(const eps::MeshData&)>;

// hull es una textura de chapas; 0 = color liso.
GameAssets BuildGameAssets(const MeshUpload& upload, eps::TextureId hull = 0);

const eps::ModelRef& ModelFor(const GameAssets& assets, const Anomaly& anomaly);
const eps::ModelRef& AlienModelAsset(const GameAssets& assets, AlienModel model);

// Las zonas de una maqueta alienigena, por el nombre de sus piezas (AlienZoneOfPart), con los nombres
// que tienen en ella ("la cuchilla de babor").
std::shared_ptr<ZoneLayout> BuildAlienZoneLayout(const eps::ModelAsset& model, AlienModel kind);

// Hace que la sesion use las zonas de las maquetas alienigenas (SetAlienZones). Sin llamarla, los
// alienigenas solo tienen su casco entero, como antes.
void RegisterAlienZones(const GameAssets& assets);

// La nave desde un .glb o .gltf, en metros, con +Y arriba y la proa hacia -Z (en Blender: proa
// hacia +Y y arriba +Z). Cada nodo es una pieza; su nombre dice de que zona del casco es
// (ZoneOfPart). texture carga la textura de una pieza por su ruta; puede ser nula.
// Devuelve nullptr si no se pudo leer, con el motivo en error.
eps::ModelRef LoadShipModel(const std::string& path, const MeshUpload& upload, const std::function<eps::TextureId(const std::string&)>& texture, std::string* error = nullptr);

// Cualquier otro modelo glTF, en metros y ejes del motor: cada parte centrada en su caja, con su
// textura y su brillo, en el orden del archivo. Sin triangulos de choque.
eps::ModelRef LoadGltfModel(const std::string& path, const std::string& name, const MeshUpload& upload, const std::function<eps::TextureId(const std::string&)>& texture, std::string* error = nullptr);

// La estacion origami, la bahia de drones y el dron, de folder (con la barra final). Lo que no
// encuentre se queda con su maqueta; cada fallo deja una linea en log.
void LoadStationAndDrones(GameAssets& assets, const std::string& folder, const MeshUpload& upload, const std::function<eps::TextureId(const std::string&)>& texture, std::string* log = nullptr);

// Una nave alienigena de un .gltf: como LoadShipModel (piezas centradas, con sus triangulos de choque
// para las zonas y los crateres), con el nombre que se le de.
eps::ModelRef LoadAlienShipModel(const std::string& path, const std::string& name, const MeshUpload& upload, const std::function<eps::TextureId(const std::string&)>& texture, std::string* error = nullptr);

// La version neutral de un modelo hostil: las mismas mallas, con la piel verdosa y los cristales turquesa.
eps::ModelRef NeutralColors(const eps::ModelAsset& hostile);

// Prefijo de las piezas de cada arma en torretas.gltf ("coilgun_ligera"...); nullptr si no tiene.
const char* TurretModelName(ShipModuleKind kind);

// Las naves alienigenas por clase y las torretas humanas, de folder (con la barra final). Lo que no
// encuentre se queda con su maqueta; cada fallo deja una linea en log. Rehace las zonas de los modelos
// que cambien: despues hay que llamar a RegisterAlienZones.
void LoadCombatModels(GameAssets& assets, const std::string& folder, const MeshUpload& upload, const std::function<eps::TextureId(const std::string&)>& texture, std::string* log = nullptr);

// La forma de las zonas de una nave cargada (SetZoneLayout): sus piezas, repartidas por zonas con
// ZoneOfPart, se cubren con bolas de unos 30 m. Necesita los triangulos de cada pieza (collision).
std::shared_ptr<ZoneLayout> BuildZoneLayout(const eps::ModelAsset& ship);
// Lo mismo con otro reparto de piezas (zoneOf) y otro tamano de bola (cell, m).
std::shared_ptr<ZoneLayout> BuildZoneLayout(const eps::ModelAsset& model, const std::function<int(const std::string&, const eps::Vec3f&)>& zoneOf, double cell);

// Las ranuras y las piezas de una nave (SetShipDesign). Una pieza por cada pieza del modelo, en su
// orden. Las ranuras exteriores son los nodos vacios "hp_..." del .glb (LoadGltfMarkers); sin
// ninguno, las de la maqueta. Las interiores, las bahias de cada zona: hace falta SetZoneLayout antes.
std::shared_ptr<ShipDesign> BuildShipDesign(const eps::ModelAsset& ship, const std::vector<eps::ImportedMarker>& markers);

}  // namespace game
