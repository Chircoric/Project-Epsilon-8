// Engine.h - Fachada: reune los modulos en una sola clase. Es lo unico que necesita un juego.
//
//   eps::Engine engine(config);
//   engine.Commands().Execute("desactivar nebulosa");
//   ...
//   engine.Advance(camera.Position(), camera.FocalPx(), simTime);   // simulacion
//   engine.Build(camera, time, packet);                             // fotograma
//   backend.Render(packet);
//
// No conoce SDL: la ventana, la GPU y los hilos los decide el juego. Si solo quiere una parte,
// puede usar los modulos sueltos; esta clase no anade nada que ellos no permitan.
#pragma once

#include <memory>
#include <vector>

#include "epsilon/AstroLayers.h"
#include "epsilon/Collisions.h"
#include "epsilon/Features.h"
#include "epsilon/FrameBuilder.h"
#include "epsilon/JobSystem.h"
#include "epsilon/Models.h"
#include "epsilon/Universe.h"

namespace eps {

// Plano cercano unico para todas las escalas: 5 cm. Con Z invertida no cuesta precision a
// distancias astronomicas, asi que un tornillo y un planeta comparten depth buffer.
inline constexpr float kNearPlaneKm = 5.0e-5f;

struct EngineConfig {
    GalaxyOptions galaxy;
    AsteroidLayerSettings asteroids;
    BlastSettings blasts;
    float rockCollisionsPerMinute = 0;  // choques espontaneos entre rocas del campo donde se esta
    LodSettings lod;
    PostSettings post;
    Color clearColor{0.01f, 0.01f, 0.025f, 1.0f};
    TextureId skyTexture = kNoTexture;
    Color skyTint{0.26f, 0.22f, 0.27f, 1.0f};
};

class Engine {
public:
    explicit Engine(const EngineConfig& config = {}, const AstroArt& art = {});

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    World& GetWorld() { return world_; }
    const World& GetWorld() const { return world_; }
    FeatureSet& Features() { return input_.features; }
    CommandRegistry& Commands() { return commands_; }
    PostSettings& Post() { return input_.post; }
    LodSettings& Lod() { return input_.lod; }
    JobSystem& Jobs() { return jobs_; }
    CollisionSystem& Collisions() { return collisions_; }

    // Niebla uniforme: oculta lo que este a varias veces distance km. 0 la apaga.
    void SetFog(float distance, const Color& color, const Vec3f& origin = {}) {
        input_.fogDistance = distance;
        input_.fogColor = color;
        input_.fogOrigin = origin;
    }

    // La niebla recorre esta paleta; tone es el punto que le toca a esta zona, de 0 a 1.
    void SetFogRamp(const std::array<Color, 4>& ramp, float tone) {
        input_.fogRampOn = true;
        input_.fogRamp = ramp;
        input_.fogTone = tone;
    }

    AsteroidLayer& Asteroids() { return asteroidLayer_; }

    // Crea una burbuja a escala humana. Sigue viva hasta destruir el motor.
    LocalSpace& CreateLocalSpace(NodeId anchor = kNoNode, NodeId lightNode = kNoNode);

    // Anade una capa propia del juego. Debe sobrevivir al motor.
    void AddLayer(IFrameLayer* layer) { input_.layers.push_back(layer); }

    // Trae el origen a la camara si se alejo. Llamar antes de Advance.
    void Recenter(Vec3d& cameraPosition) { RecenterOrigin(world_, cameraPosition); }

    // Carga lo cercano, descarga lo lejano y coloca lo que importa en el instante simTime.
    // pinnedBodies no se descargan: lo seleccionado, un destino, una estacion.
    // effectSeconds hace avanzar las explosiones; 0 las congela, como en una pausa.
    void Advance(const Vec3d& cameraPosition, float focalPx, double simTime, const std::vector<int>& pinnedBodies = {}, double effectSeconds = 1.0 / 60.0);

    // Convierte el estado actual en un paquete para el backend.
    void Build(const Camera& camera, double time, FramePacket& out);

private:
    World world_;
    JobSystem jobs_;
    FrameBuilder builder_;
    FrameInput input_;
    CommandRegistry commands_;
    OrbitLayer orbitLayer_;
    NebulaLayer nebulaLayer_;
    BlackHoleLayer blackHoleLayer_;
    AsteroidLayer asteroidLayer_;
    CollisionSystem collisions_;
    float rockRate_ = 0;
    double lastSimTime_ = 0;
    bool started_ = false;
    std::vector<int> pins_;
    ModelLayer modelLayer_;
    std::vector<std::unique_ptr<LocalSpace>> spaces_;
    std::uint64_t frameNumber_ = 0;
};

}  // namespace eps
