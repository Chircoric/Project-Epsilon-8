// AstroLayers.h - Lo que el modulo astral aporta a cada fotograma.
//
// El juego elige que capas usa anadiendolas a FrameInput::layers.
#pragma once

#include <map>
#include <memory>
#include <random>
#include <vector>

#include "epsilon/Layer.h"
#include "epsilon/Universe.h"

namespace eps {

// Lineas de orbita, solo de lo que ocupa pixeles.
class OrbitLayer final : public IFrameLayer {
public:
    explicit OrbitLayer(const World& world) : world_(world) {}
    void Collect(const FrameContext& c, FramePacket& out) override;

private:
    const World& world_;
    std::vector<std::vector<LineInstance>> chunks_;
};

class NebulaLayer final : public IFrameLayer {
public:
    explicit NebulaLayer(const World& world) : world_(world) {}
    void Collect(const FrameContext& c, FramePacket& out) override;

private:
    const World& world_;
};

class BlackHoleLayer final : public IFrameLayer {
public:
    explicit BlackHoleLayer(const World& world) : world_(world) {}
    void Collect(const FrameContext& c, FramePacket& out) override;

private:
    const World& world_;
};

struct AsteroidLayerSettings {
    std::vector<MeshId> meshes;       // sin mallas, todas las rocas son puntos
    TextureId dot = 1;                // textura de los puntos
    float meshMinPixels = 2.5f;       // por debajo, punto
    float dotMinPixels = 0.35f;       // por debajo, nada
    float dustPixels = 1.7f;          // radio del polvo visto de lejos
    float beltMinPixels = 8.0f;       // anchura del anillo para calcular su polvo
    float nearRange = 6000.0f;       // km: a esta distancia del borde se generan las rocas
    std::size_t maxCachedFamilies = 8;
    // El medio del anillo (BeltField): el polvo se ve hasta aqui, las rocas sueltas hasta alli.
    double fieldDustReach = 70000.0;   // km
    double fieldRockReach = 15000.0;   // km
    float fieldDustPixels = 1.3f;
};

// Centro de una familia en el instante dado; sirve con el campo sin rocas generadas.
Vec3d FamilyPosition(const SceneGraph& scene, const AsteroidBelt& belt, const AsteroidFamily& family, double simTime);

// Roca apuntada dentro de un campo.
struct RockPick {
    int belt = -1, family = -1, rock = -1;
    AsteroidRock data;
    double distance = 0;  // km
};

// Dos rocas que acaban de chocar. Las aprovecha el modulo de colisiones.
struct RockHit {
    int belt = -1, family = -1;
    Vec3f offset;   // km, dentro de la familia
    float radius = 0;
    double speed = 0;  // km/s
};

class AsteroidLayer final : public IFrameLayer {
public:
    AsteroidLayer(const World& world, AsteroidLayerSettings settings) : world_(world), settings_(std::move(settings)) {}
    void Collect(const FrameContext& c, FramePacket& out) override;

    std::size_t CachedFamilies() const { return cache_.size(); }
    // Centro de la familia mas cercana al punto, en el instante dado. false si no hay ninguna.
    // belt >= 0 limita la busqueda a ese campo.
    static bool NearestFamily(const World& world, const Vec3d& position, double simTime, Vec3d& center, int belt = -1);

    // Roca en la direccion indicada, entre las familias que estan generadas. Admite un margen
    // angular para que una roca pequena se pueda apuntar. false si no hay ninguna.
    bool PickRock(const Vec3d& origin, const Vec3d& direction, double simTime, float marginRadians, RockPick& out) const;

    // Pone dos rocas cercanas en curso de choque. Devuelve false si no hay ninguna a mano.
    bool StartRockCollision(const Vec3d& camera);

    // Avanza los choques en marcha y, con ratePerMinute > 0, provoca otros cerca de la camara.
    std::vector<RockHit> Tick(double seconds, const Vec3d& camera, float ratePerMinute, World& world, const FeatureSet& features);

private:
    struct Cached {
        std::vector<AsteroidRock> rocks;
        std::uint64_t lastUsed = 0;
        int belt = -1, family = -1;
    };
    struct RockDuel {
        std::uint32_t seed = 0;
        int belt = -1, family = -1;
        int mover = -1, target = -1;
        Vec3f from, to;
        float age = 0, duration = 3.0f;
        double speed = 0;
    };
    // Donde estaba un punto de una orbita y como se movia, en el marco del padre. Entre un calculo
    // exacto y el siguiente se avanza en linea recta: en unos segundos una orbita de anos no se curva.
    struct OrbitSample {
        Vec3d position, velocity;
        double time = 0;
        bool valid = false;
    };
    // Lo guardado de un cinturon; inner, outer y seed dicen si sigue siendo el mismo.
    struct BeltSamples {
        std::uint32_t seed = 0;
        double inner = -1, outer = -1;
        std::vector<OrbitSample> dust, families;
    };
    const World& world_;
    AsteroidLayerSettings settings_;
    std::vector<BeltSamples> samples_;       // por cinturon
    std::map<std::uint32_t, Cached> cache_;  // por semilla: se generan al acercarse
    std::uint64_t frame_ = 0;
    std::vector<RockDuel> duels_;
    Vec3f viewForward_{0, 0, 1};  // para elegir rocas que se esten viendo
    std::mt19937 rng_{20261106u};
    double nextRockCollision_ = 2.0;
    std::vector<std::vector<SpriteInstance>> dustChunks_;
    std::vector<std::vector<MeshInstance>> meshChunks_;
    std::vector<std::vector<SpriteInstance>> rockChunks_;
    std::vector<BeltFieldRock> field_;  // el medio del anillo alrededor de la camara, en este fotograma
};

}  // namespace eps
