#include "epsilon/Engine.h"

namespace eps {

Engine::Engine(const EngineConfig& config, const AstroArt& art)
    : world_(BuildGalaxy(config.galaxy, art)),
      orbitLayer_(world_),
      nebulaLayer_(world_),
      blackHoleLayer_(world_),
      asteroidLayer_(world_, config.asteroids),
      collisions_(world_, config.blasts),
      rockRate_(config.rockCollisionsPerMinute) {
    input_.scene = &world_.scene;
    input_.renderables = &world_.renderables;
    input_.links = &world_.links;
    input_.sky = world_.sky;
    input_.surfaces = world_.surfaces;
    input_.lod = config.lod;
    input_.post = config.post;
    input_.clearColor = config.clearColor;
    input_.skyTexture = config.skyTexture;
    input_.skyTint = config.skyTint;
    input_.layers = {&asteroidLayer_, &modelLayer_, &orbitLayer_, &nebulaLayer_, &blackHoleLayer_, &collisions_};
    input_.features.Set(Feature::Routes, false);

    RegisterCoreCommands(commands_, input_.features);
    commands_.AddVariable("bloom.fuerza", &input_.post.bloomStrength);
    commands_.AddVariable("bloom.umbral", &input_.post.bloomThreshold);
    commands_.AddVariable("exposicion", &input_.post.exposure);
    commands_.AddVariable("vineta.fuerza", &input_.post.vignette);
    commands_.AddVariable("aberracion.fuerza", &input_.post.aberration);
    commands_.AddVariable("grano.fuerza", &input_.post.grain);
    commands_.AddVariable("detalle", &input_.lod.detailBias);
    commands_.AddVariable("colision.rocas", &rockRate_);
    commands_.Add("impactos", "lista los choques pendientes y los ultimos ocurridos", [this](const CommandRegistry::Args&) {
        std::string out;
        for (const ScheduledImpact& p : collisions_.Pending()) {
            const double days = (p.time - world_.simTime) / astro::kDay;
            out += world_.bodies[static_cast<std::size_t>(p.attacker)].name + " > " + world_.bodies[static_cast<std::size_t>(p.target)].name + " en " + std::to_string(days) + " dias\n";
        }
        for (const ImpactReport& r : collisions_.Reports())
            out += r.attacker + " > " + r.target + ": " + astro::Name(r.result.outcome) + ", " + std::to_string(r.result.speed) + " km/s\n";
        return out.empty() ? std::string("Ningun choque") : out;
    });
    commands_.Add("cargados", "cuenta lo que hay en memoria", [this](const CommandRegistry::Args&) {
        const StreamStats& s = world_.stats;
        return "Sistemas cargados: " + std::to_string(s.loadedSystems) + " de " + std::to_string(world_.state.size()) +
               "   Orbitas calculadas: " + std::to_string(s.activeOrbits) + " de " + std::to_string(s.totalOrbits) +
               "   Cargas: " + std::to_string(s.loads) + "   Descargas: " + std::to_string(s.unloads);
    });
}

LocalSpace& Engine::CreateLocalSpace(NodeId anchor, NodeId lightNode) {
    spaces_.push_back(std::make_unique<LocalSpace>());
    LocalSpace& space = *spaces_.back();
    space.anchor = anchor;
    space.lightNode = lightNode;
    modelLayer_.Add(&space);
    return space;
}

void Engine::Advance(const Vec3d& cameraPosition, float focalPx, double simTime, const std::vector<int>& pinnedBodies, double effectSeconds) {
    if (!started_) lastSimTime_ = simTime;
    started_ = true;
    world_.simTime = simTime;

    pins_ = pinnedBodies;
    collisions_.AppendPins(pins_);
    StreamWorld(world_, cameraPosition, focalPx, pins_, input_.features);
    PublishWorld(world_);
    UpdateOrbits(world_.orbits, simTime, world_.scene, jobs_);
    world_.scene.UpdateWorld(jobs_);

    collisions_.Update(lastSimTime_, simTime, effectSeconds, input_.features);
    for (const RockHit& hit : asteroidLayer_.Tick(effectSeconds, cameraPosition, rockRate_, world_, input_.features)) collisions_.AddRockBlast(hit.belt, hit.family, hit.offset, hit.radius, hit.speed);
    lastSimTime_ = simTime;

    PublishWorld(world_);  // un choque puede haber cambiado el catalogo
    input_.simTime = simTime;
    input_.surfaces = world_.surfaces;
}

void Engine::Build(const Camera& camera, double time, FramePacket& out) {
    input_.camera = camera;
    input_.time = time;
    input_.frameNumber = ++frameNumber_;
    builder_.Build(input_, jobs_, out);
}

}  // namespace eps
