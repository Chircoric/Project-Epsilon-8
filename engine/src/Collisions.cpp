#include "epsilon/Collisions.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>

#include "epsilon/FrameBuilder.h"

namespace eps {

namespace {

using namespace astro;

constexpr int kMaxSweepSteps = 8;

// Del interior blanco de una explosion al rojo apagado de la roca que se enfria.
Color HeatGlow(float heat) {
    const float t = std::clamp(heat, 0.0f, 1.0f);
    if (t > 0.5f) {
        const float k = (t - 0.5f) * 2.0f;
        return {1.0f, 0.55f + 0.40f * k, 0.15f + 0.65f * k, 1.0f};
    }
    const float k = t * 2.0f;
    return {0.25f + 0.75f * k, 0.05f + 0.50f * k, 0.02f + 0.13f * k, 1.0f};
}

Vec3f RandomDirection(std::mt19937& rng) {
    const double z = Uniform(rng, -1, 1), yaw = Uniform(rng, 0, 2.0 * kPi), flat = std::sqrt(1.0 - z * z);
    return {static_cast<float>(flat * std::cos(yaw)), static_cast<float>(z), static_cast<float>(flat * std::sin(yaw))};
}

// Distancia minima entre dos puntos que se mueven en linea recta durante un tramo.
double ClosestApproach(const Vec3d& relativeStart, const Vec3d& relativeEnd) {
    const Vec3d step = relativeEnd - relativeStart;
    const double length2 = Dot(step, step);
    const double t = length2 > 0.0 ? std::clamp(-Dot(relativeStart, step) / length2, 0.0, 1.0) : 0.0;
    return Length(relativeStart + step * t);
}

}  // namespace

bool CollisionSystem::Schedule(int attacker, int target, double now, double flightSeconds, std::string* error) {
    const auto fail = [&](const char* text) {
        if (error) *error = text;
        return false;
    };
    if (attacker < 0 || target < 0 || attacker == target) return fail("Hacen falta dos cuerpos distintos");
    const std::size_t ai = static_cast<std::size_t>(attacker), ti = static_cast<std::size_t>(target);
    if (ai >= world_.bodies.size() || ti >= world_.bodies.size()) return fail("Cuerpo desconocido");
    const Body& a = world_.bodies[ai];
    const Body& t = world_.bodies[ti];
    if (!Alive(a) || !Alive(t)) return fail("Ese cuerpo no esta cargado");
    if (a.isStar || a.orbit < 0 || a.kind == PlanetClass::Belt || t.kind == PlanetClass::Belt) return fail("Solo chocan planetas, lunas, cometas y asteroides");
    for (const ScheduledImpact& p : pending_)
        if (p.attacker == attacker) return fail("Ese cuerpo ya va camino de un choque");

    const KeplerOrbit orbit = world_.orbits[static_cast<std::size_t>(a.orbit)];
    const NodeId frame = world_.scene.Parent(orbit.node);
    const bool toCenter = t.node == frame;
    const bool sameFrame = t.orbit >= 0 && world_.orbits[static_cast<std::size_t>(t.orbit)].node != kNoNode && world_.scene.Parent(world_.orbits[static_cast<std::size_t>(t.orbit)].node) == frame;
    if (!toCenter && !sameFrame) return fail("No orbitan lo mismo: no hay camino entre ellos");

    const KeplerOrbit* goal = toCenter ? nullptr : &world_.orbits[static_cast<std::size_t>(t.orbit)];
    const double mu = orbit.elements.mu;
    const Vec3d start = OrbitPosition(orbit, now);
    const double period = OrbitPeriod(goal ? goal->elements : orbit.elements);
    const StarSystemData& system = *world_.systems[static_cast<std::size_t>(a.system)];
    const astro::BodyData* info = BodyInfo(world_, a);
    const double keepOut = a.parent >= 0 && world_.bodies[static_cast<std::size_t>(a.parent)].isStar && info ? system.HostOf(*info).radius * 1.5 : 0.0;

    // No todos los tiempos de vuelo sirven: unos pasan por la estrella o exigen salir del sistema.
    // Primero se intenta lo que se pidio; si no hay camino, una fraccion del periodo siempre da.
    // Transferencia de Hohmann: el tiempo natural para ir de una distancia a otra.
    const double middle = 0.5 * (Length(start) + (goal ? goal->elements.semiMajorAxis : t.radius));
    const double transfer = kPi * std::sqrt(middle * middle * middle / mu);
    for (const double base : {flightSeconds > 0.0 ? flightSeconds : transfer, 0.25 * period, transfer}) {
        for (const double factor : {1.0, 1.3, 0.75, 1.7, 0.55, 2.2, 0.4, 3.0}) {
            const double arrival = now + base * factor;
            // Hacia su propio centro se apunta a un punto de la superficie, a un tercio de vuelta.
            const Vec3d end = goal ? OrbitPosition(*goal, arrival)
                                   : Mat4d(RotationY(2.1)).TransformDirection(Normalize(start)) * t.radius;
            Vec3d departure, unused;
            OrbitalElements elements;
            if (!SolveLambert(start, end, base * factor, mu, departure, unused)) continue;
            if (!OrbitFromState(start, departure, mu, now, elements) || elements.eccentricity > 0.97) continue;
            if (goal && Periapsis(elements) < keepOut) continue;

            world_.orbits[static_cast<std::size_t>(a.orbit)] = MakeOrbit(orbit.node, elements);
            if (a.data >= 0) EditSystem(world_, a.system).bodies[static_cast<std::size_t>(a.data)].orbit = elements;
            else world_.rockData[static_cast<std::size_t>(a.extra)].orbit = elements;
            for (const int path : world_.state[static_cast<std::size_t>(a.system)].paths)
                if (world_.orbitPaths[static_cast<std::size_t>(path)].owner == attacker) world_.orbitPaths[static_cast<std::size_t>(path)].quiet = false;
            pending_.push_back({attacker, target, arrival});
            world_.dirty = true;
            return true;
        }
    }
    return fail("No se encontro una trayectoria");
}

CollisionSystem::Blast CollisionSystem::MakeBlast(NodeId parent, const KeplerOrbit* orbit, const Vec3f& offset, const Vec3f& normal, float reach, ImpactOutcome outcome, std::uint32_t seed) {
    // La orbita de origen puede vivir en World::orbits, que quiza crezca aqui.
    const std::optional<KeplerOrbit> source = orbit ? std::optional<KeplerOrbit>(*orbit) : std::nullopt;
    Blast b;
    b.node = world_.scene.CreateNode(parent);
    if (source) {
        std::size_t slot = world_.orbits.size();
        if (!world_.freeOrbits.empty()) {
            slot = static_cast<std::size_t>(world_.freeOrbits.back());
            world_.freeOrbits.pop_back();
        } else {
            world_.orbits.emplace_back();
        }
        world_.orbits[slot] = MakeOrbit(b.node, source->elements);
        b.orbit = static_cast<int>(slot);
        world_.scene.Local(b.node).position = OrbitPosition(*source, world_.simTime);
    }
    b.offset = offset;
    b.normal = normal;
    b.reach = reach;
    b.outcome = outcome;
    b.seed = seed;
    b.duration = settings_.duration;

    const bool shattered = outcome == ImpactOutcome::Shattered;
    const int fragments = outcome == ImpactOutcome::Crater ? 40 : outcome == ImpactOutcome::Merger ? 90 : outcome == ImpactOutcome::Partial ? 140 : 220;
    std::mt19937 rng(seed);
    const auto make = [&](float minSize, float maxSize) {
        Fragment f;
        Vec3f d = RandomDirection(rng);
        // Lo que sale de un crater se abre en cono; una rotura total sale en todas direcciones.
        if (!shattered) d = Normalize(d * 0.75f + normal * 1.1f);
        f.direction = d;
        f.travel = reach * static_cast<float>(LogUniform(rng, 0.5, 3.2));
        f.size = reach * static_cast<float>(LogUniform(rng, minSize, maxSize));
        f.axis = RandomDirection(rng);
        f.spin = static_cast<float>(Uniform(rng, -3.0, 3.0));
        f.mesh = settings_.meshes.empty() ? kNoMesh : settings_.meshes[rng() % settings_.meshes.size()];
        return f;
    };
    if (!settings_.meshes.empty()) {
        b.fragments.resize(static_cast<std::size_t>(fragments));
        for (Fragment& f : b.fragments) f = make(0.006f, 0.05f);
    }
    b.sparks.resize(static_cast<std::size_t>(fragments) * 4);
    for (Fragment& s : b.sparks) s = make(0.002f, 0.01f);
    return b;
}

void CollisionSystem::Fire(int attacker, int target, double time) {
    const Body a = world_.bodies[static_cast<std::size_t>(attacker)];
    const Body t = world_.bodies[static_cast<std::size_t>(target)];
    const double aMass = BodyMass(world_, a), tMass = BodyMass(world_, t);

    // Velocidad relativa en el marco que comparten. El objetivo puede ser su propio centro.
    const KeplerOrbit& aOrbit = world_.orbits[static_cast<std::size_t>(a.orbit)];
    const NodeId frame = world_.scene.Parent(aOrbit.node);
    Vec3d velocity = OrbitVelocity(aOrbit, time);
    if (t.orbit >= 0 && t.node != frame) velocity = velocity - OrbitVelocity(world_.orbits[static_cast<std::size_t>(t.orbit)], time);
    const double speed = Length(velocity);
    const std::uint32_t seed = counter_++ * 2654435761u + static_cast<std::uint32_t>(target);

    SystemHistory& history = world_.history[static_cast<std::size_t>(t.system)];
    const std::size_t changesBefore = history.changes.size(), debrisBefore = history.debris.size();
    ImpactResult result;
    if (t.isStar) {
        result = ResolveImpact(aMass, a.radius, tMass, t.radius, speed);
        result.outcome = ImpactOutcome::Absorbed;
        result.remnantMass = tMass + aMass;
        if (a.data >= 0) {
            BodyChange gone;
            gone.data = a.data;
            gone.destroyed = true;
            history.changes.push_back(gone);
        }
    } else {
        const StarSystemData& system = *world_.systems[static_cast<std::size_t>(t.system)];
        result = RecordImpact(history, system, a.data, aMass, a.radius, t.data, tMass, t.radius, speed, seed);
    }

    // El lado por el que llego, en ejes del mundo.
    const Vec3f normal = Normalize(Vec3f(world_.scene.World(frame).TransformDirection(velocity * -1.0)));
    const float targetRadius = static_cast<float>(t.radius);
    const float attackerRadius = static_cast<float>(a.radius);
    float reach = targetRadius * 1.2f;
    bool atSurface = true;
    switch (result.outcome) {
        case ImpactOutcome::Crater: reach = std::max(attackerRadius * 3.5f, targetRadius * 0.15f); break;
        case ImpactOutcome::Merger: reach = targetRadius * 1.3f; break;
        case ImpactOutcome::Partial: reach = targetRadius * 1.9f; break;
        case ImpactOutcome::Shattered: reach = targetRadius * 2.6f; atSurface = false; break;
        case ImpactOutcome::Absorbed: reach = std::min(attackerRadius * 12.0f, targetRadius * 0.4f); break;
    }

    Renderable* drawn = t.renderable >= 0 ? &world_.renderables[static_cast<std::size_t>(t.renderable)] : nullptr;
    // Una copia de su orbita: asi la explosion sigue su camino aunque el cuerpo ya no exista.
    const KeplerOrbit* follow = t.orbit >= 0 && t.node != frame ? &world_.orbits[static_cast<std::size_t>(t.orbit)] : nullptr;
    const NodeId blastParent = follow ? world_.scene.Parent(follow->node) : t.node;
    Blast blast = MakeBlast(blastParent, follow, normal * (atSurface ? targetRadius : 0.0f), normal, reach, result.outcome, seed);
    blast.system = t.system;
    blast.target = target;
    blast.anchor = target;
    if (drawn && drawn->lightNode >= 0) blast.light = drawn->lightNode;
    if (blast.light == kNoNode) blast.light = world_.sectors[static_cast<std::size_t>(t.system)];

    // Una luna depende de su planeta: mientras dure la explosion, el planeta sigue cargado.
    if (t.parent >= 0 && !world_.bodies[static_cast<std::size_t>(t.parent)].isStar) blast.anchor = t.parent;

    reports_.push_back({a.name, t.name, result, time, t.system, target, normal});
    if (reports_.size() > 32) reports_.erase(reports_.begin());

    RemoveBody(world_, attacker);

    // El orden importa: lo que le pasa al objetivo es la ultima entrada de la historia.
    if (history.changes.size() > changesBefore && !t.isStar) {
        BodyChange& change = history.changes.back();
        if (change.data == t.data && t.data >= 0 && drawn) {
            const Quatf toBody = BodyOrientation(*drawn, time).Conjugate();
            change.scarDirection = Normalize(toBody.ToMatrix().TransformDirection(normal));
            blast.scarSize = change.scarSize;
            if (change.destroyed) {
                // Antes de romperse se ve la grieta: el planeta se abre y luego se deshace.
                blast.breakAt = 1.4;
                drawn->scarDirection = change.scarDirection;
                drawn->scarSize = change.scarSize;
                if (t.data >= 0) EditSystem(world_, t.system).bodies[static_cast<std::size_t>(t.data)].destroyed = true;
            } else {
                ApplyChange(world_, target, change);
            }
        }
    }
    if (t.extra >= 0 && (result.outcome == ImpactOutcome::Shattered || result.outcome == ImpactOutcome::Partial)) blast.breakAt = 0.2;
    blast.hasDebris = history.debris.size() > debrisBefore;
    blasts_.push_back(std::move(blast));
    world_.dirty = true;
}

void CollisionSystem::Detect(double previousTime, double simTime) {
    const double dt = simTime - previousTime;
    if (dt <= 0.0) return;

    for (std::size_t s = 0; s < world_.state.size(); ++s) {
        const SystemState& state = world_.state[s];
        if (!state.loaded) continue;

        // Choques ya escritos en la orbita: llegan cuando llega su hora.
        const std::vector<int> asteroids = state.asteroids;
        for (const int index : asteroids) {
            const Body& b = world_.bodies[static_cast<std::size_t>(index)];
            if (!Alive(b)) continue;
            const astro::BodyData& data = world_.systems[s]->bodies[static_cast<std::size_t>(b.data)];
            if (data.impactTime < 0.0 || data.impactTime > simTime) continue;
            bool busy = false;
            for (const ScheduledImpact& p : pending_) busy = busy || p.attacker == index;
            if (busy) continue;
            for (const int other : state.bodies)
                if (world_.bodies[static_cast<std::size_t>(other)].data == data.impactTarget) {
                    pending_.push_back({index, other, data.impactTime});
                    break;
                }
        }

        // Cuerpos pequenos contra planetas. Solo se calculan las parejas cuyas orbitas pueden
        // tocarse: casi siempre es ninguna.
        for (const int mover : state.bodies) {
            const Body& m = world_.bodies[static_cast<std::size_t>(mover)];
            if (m.kind != PlanetClass::Comet && m.kind != PlanetClass::Asteroid) continue;
            if (m.orbit < 0) continue;
            const KeplerOrbit& mo = world_.orbits[static_cast<std::size_t>(m.orbit)];
            const NodeId frame = world_.scene.Parent(mo.node);
            for (const int planet : state.bodies) {
                const Body& p = world_.bodies[static_cast<std::size_t>(planet)];
                if (p.orbit < 0 || p.kind == PlanetClass::Comet || p.kind == PlanetClass::Asteroid || p.kind == PlanetClass::Moon) continue;
                const KeplerOrbit& po = world_.orbits[static_cast<std::size_t>(p.orbit)];
                if (world_.scene.Parent(po.node) != frame) continue;
                const double reach = m.radius + p.radius;
                if (Periapsis(mo.elements) > Apoapsis(po.elements) + reach || Apoapsis(mo.elements) < Periapsis(po.elements) - reach) continue;
                bool scheduled = false;
                for (const ScheduledImpact& q : pending_) scheduled = scheduled || q.attacker == mover;
                if (scheduled) continue;

                const double relative = Length(OrbitVelocity(mo, simTime) - OrbitVelocity(po, simTime));
                const int steps = std::clamp(static_cast<int>(std::ceil(dt * relative / reach)), 1, kMaxSweepSteps);
                Vec3d last = OrbitPosition(mo, previousTime) - OrbitPosition(po, previousTime);
                for (int k = 1; k <= steps; ++k) {
                    const double time = previousTime + dt * k / steps;
                    const Vec3d now = OrbitPosition(mo, time) - OrbitPosition(po, time);
                    if (ClosestApproach(last, now) <= reach) {
                        pending_.push_back({mover, planet, time});
                        break;
                    }
                    last = now;
                }
            }
        }
    }
}

void CollisionSystem::Update(double previousTime, double simTime, double realSeconds, const FeatureSet& features) {
    world_.simTime = simTime;
    if (features.Enabled(Feature::Collisions)) {
        Detect(previousTime, simTime);

        for (std::size_t i = 0; i < pending_.size();) {
            const ScheduledImpact p = pending_[i];
            const Body& a = world_.bodies[static_cast<std::size_t>(p.attacker)];
            const Body& t = world_.bodies[static_cast<std::size_t>(p.target)];
            if (!Alive(a) || !Alive(t)) {
                pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            const bool touching = Length(world_.scene.WorldPosition(a.node) - world_.scene.WorldPosition(t.node)) <= a.radius + t.radius;
            if (touching || simTime >= p.time) {
                pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
                Fire(p.attacker, p.target, std::min(simTime, p.time));
                continue;
            }
            ++i;
        }
    }

    for (std::size_t i = 0; i < blasts_.size();) {
        Blast& b = blasts_[i];
        b.age += realSeconds;
        const float x = static_cast<float>(b.age / b.duration);

        const Body* target = b.target >= 0 ? &world_.bodies[static_cast<std::size_t>(b.target)] : nullptr;
        if (target && Alive(*target) && target->renderable >= 0) {
            // Enrojece enseguida y se enfria durante toda la explosion.
            Renderable& r = world_.renderables[static_cast<std::size_t>(target->renderable)];
            if (r.surface >= 0 && b.scarSize > 0.0f) {
                const float rise = std::min(static_cast<float>(b.age) * 3.0f, 1.0f);
                r.scarHeat = rise * std::pow(std::max(1.0f - x, 0.0f), 1.5f);
                const float opening = b.breakAt > 0.0 ? 1.0f + 2.5f * static_cast<float>(std::min(b.age / b.breakAt, 1.0)) : 1.0f;
                r.scarSize = b.scarSize * opening;
            }
        }

        if (b.breakAt >= 0.0 && b.age >= b.breakAt) {
            if (target && Alive(*target)) RemoveBody(world_, b.target);
            b.breakAt = -1.0;
            b.target = -1;
            b.anchor = -1;
        }

        // Cuando la nube se abre ya hay campo de escombros que recorrer.
        if (b.hasDebris && x > 0.55f) {
            SystemHistory& history = world_.history[static_cast<std::size_t>(b.system)];
            if (world_.state[static_cast<std::size_t>(b.system)].loaded) {
                StarSystemData& system = EditSystem(world_, b.system);
                for (std::size_t d = 0; d < history.debris.size(); ++d) {
                    bool present = false;
                    for (const BeltData& existing : system.belts) present = present || existing.seed == history.debris[d].seed;
                    if (present) continue;
                    system.belts.push_back(history.debris[d]);
                    const int body = InstallBelt(world_, b.system, static_cast<int>(system.belts.size()) - 1);
                    world_.state[static_cast<std::size_t>(b.system)].bodies.push_back(body);
                }
            }
            b.hasDebris = false;
        }

        if (b.age >= b.duration) {
            if (b.orbit >= 0) {
                world_.orbits[static_cast<std::size_t>(b.orbit)] = KeplerOrbit{};
                world_.freeOrbits.push_back(b.orbit);
            }
            if (world_.scene.Alive(b.node)) world_.scene.DestroyNode(b.node);
            blasts_.erase(blasts_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void CollisionSystem::AddRockBlast(int beltIndex, int familyIndex, const Vec3f& offset, float radius, double speed) {
    const AsteroidBelt& belt = world_.belts[static_cast<std::size_t>(beltIndex)];
    if (!belt.used || belt.parent == kNoNode) return;
    const AsteroidFamily& family = belt.families[static_cast<std::size_t>(familyIndex)];
    const std::uint32_t seed = counter_++ * 2654435761u + family.seed;
    std::mt19937 rng(seed);
    const KeplerOrbit* orbit = family.fixed ? nullptr : &family.orbit;
    // La orbita de la familia esta en ejes locales, igual que la de cualquier cuerpo del sistema.
    Blast blast = MakeBlast(belt.parent, orbit, offset, RandomDirection(rng), radius * 7.0f, ImpactOutcome::Shattered, seed);
    blast.system = belt.system;
    blast.light = belt.parent;
    blast.duration = settings_.duration * 0.5;
    blasts_.push_back(std::move(blast));

    ImpactReport report;
    report.attacker = "Roca";
    report.target = "roca";
    const double mass = 4.0 / 3.0 * kPi * static_cast<double>(radius) * radius * radius * 2.5e12;
    report.result = ResolveImpact(mass, radius, mass, radius, speed);
    report.time = world_.simTime;
    report.system = belt.system;
    reports_.push_back(report);
    if (reports_.size() > 32) reports_.erase(reports_.begin());
}

void CollisionSystem::AppendPins(std::vector<int>& pinned) const {
    for (const ScheduledImpact& p : pending_) {
        pinned.push_back(p.attacker);
        pinned.push_back(p.target);
    }
    for (const Blast& b : blasts_) {
        if (b.anchor >= 0) pinned.push_back(b.anchor);
        else if (world_.state[static_cast<std::size_t>(b.system)].starBody >= 0) pinned.push_back(world_.state[static_cast<std::size_t>(b.system)].starBody);
    }
}

void CollisionSystem::Collect(const FrameContext& c, FramePacket& out) {
    if (!c.features.Enabled(Feature::Collisions)) return;
    const float focal = c.camera.FocalPx();
    const float nearZ = c.camera.NearZ();
    const Quatf toView = c.camera.Orientation().Conjugate();

    for (std::size_t index = 0; index < blasts_.size(); ++index) {
        const Blast& b = blasts_[index];
        if (!c.scene.Alive(b.node)) continue;
        const Vec3d origin = c.scene.WorldPosition(b.node) + Vec3d(b.offset);
        const Vec3f rel(origin - c.cameraPosition);
        const float distance = std::max(Length(rel), 1e-3f);
        const float x = static_cast<float>(b.age / b.duration);
        const float cool = std::exp(-x * 5.0f);  // 1 = recien nacida
        const float reachPixels = b.reach / distance * focal;
        const Vec3f view = c.viewRotation.TransformDirection(rel);
        ++out.stats.blastsVisible;

        const auto sprite = [&](const Vec3f& where, float pixels, const Color& tint, TextureId texture, std::uint32_t id) {
            if (where.z <= nearZ) return;
            SpriteInstance s;
            s.viewPosition = where;
            s.pixelRadius = std::min(pixels, 4000.0f);
            s.tint = tint;
            s.id = 0x50000000u + static_cast<std::uint32_t>(index) * 4096u + id;
            s.texture = texture;
            s.blend = BlendMode::Additive;
            out.sprites.push_back(s);
        };

        // Desde lejos solo se distingue el destello.
        const float flash = std::exp(-static_cast<float>(b.age) * 7.0f);
        if (flash > 0.01f) sprite(view, std::max(reachPixels * (0.7f + 1.1f * (1.0f - flash)), 7.0f), {5.0f * flash, 4.2f * flash, 3.4f * flash, 1.0f}, settings_.glow, 0);
        if (reachPixels < 0.6f) continue;
        if (!c.frustum.SphereVisible(rel, b.reach * 5.0f)) continue;

        // Onda de choque: un anillo que se abre rapido y se apaga.
        const float wave = std::min(x * 4.0f, 1.0f);
        if (wave < 1.0f) {
            const float strength = (1.0f - wave) * (1.0f - wave) * 0.75f;
            sprite(view, reachPixels * (0.5f + 3.6f * std::sqrt(wave)), {0.75f * strength, 0.9f * strength, 1.3f * strength, 1.0f}, settings_.ring, 1);
        }

        // Nube: reutiliza el trazado volumetrico de las nebulosas y respeta lo que hay delante.
        const float appear = std::min(x * 12.0f, 1.0f);
        const float fade = std::pow(std::max(1.0f - x, 0.0f), 1.4f);
        NebulaInstance cloud;
        cloud.viewCenter = view;
        cloud.radius = b.reach * (0.8f + 1.9f * std::sqrt(x));
        const Color hot = HeatGlow(cool);
        const float glow = 0.6f + 3.5f * cool;
        cloud.inner = {hot.r * glow, hot.g * glow, hot.b * glow, 1.0f};
        cloud.outer = {0.30f * hot.r + 0.08f, 0.16f * hot.g + 0.06f, 0.10f * hot.b + 0.06f, 1.0f};
        cloud.density = 3.0f * appear * fade;
        cloud.seed = static_cast<float>(b.seed % 64u);
        cloud.distance = distance;
        out.nebulae.push_back(cloud);

        // Lo despedido frena pronto y queda flotando: asi la explosion no acaba de golpe.
        const float travel = 1.0f - std::exp(-x * 4.5f);
        const float heat = std::max(cool * 1.4f, 0.0f);
        const Vec3f light = Normalize(c.viewRotation.TransformDirection(Vec3f(c.scene.WorldPosition(b.light) - origin)));

        for (std::size_t i = 0; i < b.fragments.size(); ++i) {
            const Fragment& f = b.fragments[i];
            const Vec3f piece = rel + f.direction * (f.travel * travel);
            if (!c.frustum.SphereVisible(piece, f.size * 1.5f)) continue;
            if (f.size / std::max(Length(piece), 1e-3f) * focal < 0.4f) continue;
            MeshInstance m;
            m.viewPosition = c.viewRotation.TransformDirection(piece);
            m.scale = f.size;
            m.rotation = toView * Quatf::FromAxisAngle(f.axis, f.spin * static_cast<float>(b.age));
            m.lightDirection = light;
            const Color tone = HeatGlow(heat);
            m.color = {0.32f + tone.r * heat, 0.27f + tone.g * heat, 0.24f + tone.b * heat, 1.0f};
            m.emissive = heat * 2.5f;
            m.id = 0x60000000u + static_cast<std::uint32_t>(index) * 4096u + static_cast<std::uint32_t>(i);
            m.mesh = f.mesh;
            out.meshes.push_back(m);
        }

        const float sparkFade = std::pow(std::max(1.0f - x * 1.6f, 0.0f), 1.5f);
        if (sparkFade > 0.0f) {
            const float streak = std::exp(-x * 4.5f) * 0.09f;  // mas largo cuanto mas rapido va
            for (std::size_t i = 0; i < b.sparks.size(); ++i) {
                const Fragment& f = b.sparks[i];
                const Vec3f piece = rel + f.direction * (f.travel * 1.3f * travel);
                const Vec3f where = c.viewRotation.TransformDirection(piece);
                if (where.z <= nearZ) continue;
                const Color tone = HeatGlow(cool * 1.2f + 0.35f);
                const float brightness = sparkFade * 2.4f;
                sprite(where, std::max(f.size / where.z * focal, 1.3f), {tone.r * brightness, tone.g * brightness, tone.b * brightness, 1.0f}, settings_.glow, 2u + static_cast<std::uint32_t>(i));
                const Vec3f tail = c.viewRotation.TransformDirection(piece - f.direction * (f.travel * streak));
                if (tail.z > nearZ) out.lines.push_back({where, tail, {tone.r, tone.g, tone.b, sparkFade * 0.7f}});
            }
        }
    }
}

}  // namespace eps
