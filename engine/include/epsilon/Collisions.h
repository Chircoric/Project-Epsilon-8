// Collisions.h - Choques entre cuerpos y sus explosiones.
//
// Las orbitas son funciones del tiempo, asi que un choque es un instante que se puede calcular:
//
//   Programado: el juego elige dos cuerpos y Schedule resuelve el problema de Lambert. El
//               atacante pasa a una orbita que llega al objetivo en el instante pedido.
//   Generado:   un sistema inestable trae de serie un cuerpo con esa orbita (AddImpactor).
//   Detectado:  cada paso se comparan los cometas y asteroides con los planetas cuyas orbitas
//               cruzan.
//
// El resultado sale de ResolveImpact y queda en SystemHistory, de modo que sobrevive a descargar
// el sistema. Lo que se ve dura segundos reales aunque el tiempo simulado vaya a un dia por
// segundo: es una representacion, no una simulacion de fluidos.
//
//   Destello > onda de choque > nube caliente que se enfria > fragmentos > campo de escombros.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "epsilon/Features.h"
#include "epsilon/Layer.h"
#include "epsilon/Universe.h"

namespace eps {

struct BlastSettings {
    TextureId glow = 1;             // destello y chispas
    TextureId ring = 1;             // onda de choque
    std::vector<MeshId> meshes;     // fragmentos; sin mallas solo hay chispas
    float duration = 16.0f;         // segundos reales
};

struct ScheduledImpact {
    int attacker = -1, target = -1;  // indices en World::bodies
    double time = 0;
};

struct ImpactReport {
    std::string attacker, target;
    astro::ImpactResult result;
    double time = 0;
    int system = 0;
    int targetBody = -1;
    Vec3f direction;  // desde el objetivo hacia el lado por el que llego el golpe, en el mundo
};

class CollisionSystem final : public IFrameLayer {
public:
    CollisionSystem(World& world, BlastSettings settings) : world_(world), settings_(std::move(settings)) {}

    // Pone al atacante en una orbita que acaba en el objetivo. flightSeconds <= 0 = la elige.
    // Sin orbita comun no hay solucion: dos planetas de estrellas distintas no pueden chocar.
    bool Schedule(int attacker, int target, double now, double flightSeconds = 0.0, std::string* error = nullptr);

    // Avanza detecciones y explosiones. Llamar tras actualizar orbitas y escena.
    void Update(double previousTime, double simTime, double realSeconds, const FeatureSet& features);

    // Dos rocas de un campo acaban de chocar.
    void AddRockBlast(int belt, int family, const Vec3f& offset, float radius, double speed);

    // Lo que no puede descargarse mientras esta en juego.
    void AppendPins(std::vector<int>& pinned) const;

    void Collect(const FrameContext& c, FramePacket& out) override;

    const std::vector<ScheduledImpact>& Pending() const { return pending_; }
    const std::vector<ImpactReport>& Reports() const { return reports_; }
    std::size_t ActiveBlasts() const { return blasts_.size(); }

private:
    struct Fragment {
        Vec3f direction;
        float travel = 0;    // km al final de la explosion
        float size = 0;      // km
        Vec3f axis;
        float spin = 0;
        MeshId mesh = 0;
    };
    struct Blast {
        NodeId node = kNoNode;    // sigue la orbita de lo que golpeo
        int orbit = -1;           // propia; se libera al terminar
        Vec3f offset, normal;     // km y direccion, en ejes del mundo
        float reach = 1;          // km, tamano de la nube
        double age = 0, duration = 1;
        astro::ImpactOutcome outcome = astro::ImpactOutcome::Crater;
        std::uint32_t seed = 0;
        NodeId light = kNoNode;
        int target = -1;          // cuerpo que se calienta o se rompe
        int system = 0;
        int anchor = -1;          // cuerpo que debe seguir cargado
        double breakAt = -1;      // segundos; a esa edad se rompe del todo
        float scarSize = 0;
        bool hasDebris = false;
        std::vector<Fragment> fragments, sparks;
    };

    void Fire(int attacker, int target, double time);
    void Detect(double previousTime, double simTime);
    Blast MakeBlast(NodeId parent, const KeplerOrbit* orbit, const Vec3f& offset, const Vec3f& normal, float reach, astro::ImpactOutcome outcome, std::uint32_t seed);

    World& world_;
    BlastSettings settings_;
    std::vector<ScheduledImpact> pending_;
    std::vector<ImpactReport> reports_;
    std::vector<Blast> blasts_;
    std::uint32_t counter_ = 1;
};

}  // namespace eps
