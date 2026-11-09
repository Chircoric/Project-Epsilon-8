// GameLayer.h - Lo que el juego anade a cada fotograma.
//
//   Modelos de la nave, las estaciones y las anomalias.
//   Motas de polvo alrededor de la nave: sin ellas, en la niebla no se notaria la velocidad.
//   Nubes cercanas que se quedan atras al avanzar.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "epsilon/Layer.h"
#include "game/GameModels.h"
#include "game/Session.h"

namespace game {

// De rojo a gris, pasando por naranja y amarillo: los colores de la nebulosa.
std::array<eps::Color, 4> NebulaRamp();
eps::Color NebulaColor(float tone);

// Lo que ilumina un punto: varia con lo que tenga cerca, y en la nave se nota.
struct SceneLight {
    eps::Vec3f direction, direction2;  // en vista
    eps::Color color, color2{0, 0, 0, 0};
    float ambient = 0.4f;
    eps::Color ambientColor;
};

class GameLayer final : public eps::IFrameLayer {
public:
    GameLayer(const GameSession& session, GameAssets assets, eps::TextureId glow, eps::TextureId ring, eps::Color fogColor) : session_(session), assets_(std::move(assets)), glow_(glow), ring_(ring), fogColor_(fogColor) {
        if (assets_.ship)
            for (const eps::ModelPart& p : assets_.ship->parts) {
                partZones_.push_back(ZoneOfPart(p.name, p.position));
                partSpin_.push_back(RotorOfPart(p.name));
            }
    }
    // Rotor al que pertenece una pieza de la nave: "rotor1_..." gira con el 1, "rotor2_..." con el
    // 2. 0 = va fija al casco. Los rotores impares giran en un sentido y los pares en el otro.
    static int RotorOfPart(const std::string& name);
    void Collect(const eps::FrameContext& c, eps::FramePacket& out) override;
    // QualityProfile::hullDamage: cuanto ensena el casco danado. Con 0 no se calcula nada.
    void SetHullDetail(int detail) { hullDetail_ = detail; }

    // Crateres que ya se han clavado en el casco (solo con calidad maxima). Para pruebas.
    std::size_t CraterCount() const { return craters_.size(); }

private:
    // Un golpe ya situado sobre el casco: el rayo lo clava en la superficie de verdad del modelo.
    struct Crater {
        bool valid = false;
        int zone = -1;
        eps::Vec3f center, normal;  // m, en el modelo de la nave
        float radius = 0, depth = 0;
        std::uint32_t seed = 0;
        // En una pieza que gira, el crater va en el marco del rotor (center y normal, sin girar) y
        // gira con el; phase es el giro que llevaba al clavarse.
        int spin = 0;
        double phase = 0;
    };
    void DrawLostPieces(const eps::FrameContext& c, eps::FramePacket& out) const;
    void SyncCraters() const;
    bool RayShip(const eps::Vec3f& origin, const eps::Vec3f& direction, int onlyZone, eps::Vec3f& hit, eps::Vec3f& normal, int& zone, int* spin = nullptr) const;
    void PartCraters(const eps::ModelPart& p, int zone, int spin, eps::MeshInstance& m, eps::FramePacket& out) const;
    // Giro de un rotor alrededor del eje de la nave (+Z del modelo), ahora o con otra fase.
    eps::Quatf SpinOf(int spin) const { return SpinOf(spin, ringPhase_); }
    static eps::Quatf SpinOf(int spin, double phase);
    void UpdateRings(const eps::FrameContext& c);
    void DrawCraterDetail(const eps::FrameContext& c, eps::FramePacket& out, const eps::Vec3d& position, const eps::Quatd& rotation, const SceneLight& light) const;
    void DrawDebris(const eps::FrameContext& c, eps::FramePacket& out) const;
    void DrawAdrift(const eps::FrameContext& c, eps::FramePacket& out) const;
    void DrawModules(const eps::FrameContext& c, eps::FramePacket& out, const eps::Vec3d& position, const eps::Quatd& rotation, const SceneLight& light) const;
    SceneLight ShipLight(const eps::FrameContext& c, SceneLight light) const;  // luz relativista de la propia nave
    void DrawExhaust(const eps::FrameContext& c, eps::FramePacket& out) const;
    void DrawThrusters(const eps::FrameContext& c, eps::FramePacket& out, const SceneLight& light) const;
    void DrawDockTube(const eps::FrameContext& c, eps::FramePacket& out) const;
    bool UpgradeMount(int key, int zone, const eps::Vec3f& target, const eps::Vec3f& from, eps::Vec3f& at, eps::Vec3f& normal) const;
    void DrawModel(const eps::FrameContext& c, eps::FramePacket& out, const eps::ModelAsset& model, const eps::Vec3d& position, const eps::Quatd& rotation, int modules, std::uint32_t id, double assembly = 1.0, const eps::Vec3d* origin = nullptr) const;
    void DrawClouds(const eps::FrameContext& c, eps::FramePacket& out) const;
    void DrawDust(const eps::FrameContext& c, eps::FramePacket& out) const;
    void DrawWreck(const eps::FrameContext& c, eps::FramePacket& out) const;
    SceneLight LightAt(const eps::FrameContext& c, const eps::Vec3d& position) const;
    void CollectStarLights(const eps::FrameContext& c, eps::FramePacket& out) const;
    void CollectAtmospheres(const eps::FrameContext& c, eps::FramePacket& out) const;
    void DrawWarpLens(const eps::FrameContext& c, eps::FramePacket& out) const;
    void DrawScanPulse(const eps::FrameContext& c, eps::FramePacket& out) const;

    const GameSession& session_;
    GameAssets assets_;
    eps::TextureId glow_, ring_;
    eps::Color fogColor_;
    eps::Quatd wreckRotation_;  // ultima orientacion de la nave, para que se rompa desde ahi
    int hullDetail_ = 2;
    std::vector<int> partZones_;  // zona del casco de cada pieza del modelo de la nave
    std::vector<int> partSpin_;   // rotor de cada pieza (RotorOfPart)
    double ringPhase_ = 0.0;      // rad que llevan girados los anillos
    double ringRate_ = -1.0;      // rad/s reales; < 0 hasta el primer fotograma
    double lastTime_ = -1.0;
    mutable std::map<int, Crater> craters_;  // por HullHit::id
    mutable float shipLights_ = 1.0f;        // las luces de la nave: se apagan al perder la energia
    mutable std::map<int, std::pair<eps::Vec3f, eps::Vec3f>> upgradeMounts_;  // sitio y normal de cada mejora visible
};


}  // namespace game
