#include "epsilon/AstroLayers.h"

#include <algorithm>
#include <cmath>

namespace eps {

namespace {

constexpr std::size_t kPathGrain = 8;
constexpr int kPathSegments = 160;
constexpr float kPathMinPixels = 6.0f;    // por debajo, la elipse no se distingue de un punto
constexpr float kPathFadePixels = 40.0f;
constexpr std::size_t kDustGrain = 512;
constexpr std::size_t kRockGrain = 128;
constexpr int kRingSegments = 96;

template <class T>
void Append(std::vector<std::vector<T>>& chunks, std::vector<T>& out) {
    for (std::vector<T>& c : chunks) out.insert(out.end(), c.begin(), c.end());
}

// Posicion de la familia en el mundo. La orbita esta en el plano del sistema.
Vec3d FamilyCenter(const SceneGraph& scene, const AsteroidBelt& belt, const AsteroidFamily& f, double simTime) {
    const Vec3d center = scene.WorldPosition(belt.parent);
    if (f.fixed) return center;
    return center + scene.World(belt.parent).TransformDirection(OrbitPosition(f.orbit, simTime));
}

// Resolver la ecuacion de Kepler de miles de orbitas en cada fotograma era la mitad del trabajo del
// procesador. Cada orbita se resuelve de verdad uno de cada kOrbitRefresh fotogramas, por turnos, y
// entre medias avanza en linea recta con su velocidad. Si el tiempo simulado salta (acelerado, o al
// cargar una partida) y la orbita giraria mas de kOrbitDrift, se resuelve en el acto. El error de
// la linea recta es radio x angulo^2 / 2: como mucho 2e-8 del radio, unos km en un anillo de
// cientos de millones.
constexpr std::uint64_t kOrbitRefresh = 16;
constexpr double kOrbitDrift = 2.0e-4;  // rad

template <class Sample>
Vec3d SampledOrbitPosition(const KeplerOrbit& o, Sample& s, double simTime, bool due) {
    const double dt = simTime - s.time;
    if (s.valid && !due && std::abs(dt) * o.meanMotion <= kOrbitDrift) return s.position + s.velocity * dt;
    const double E = EccentricAnomalyAt(o, simTime);
    const double rate = o.meanMotion / (1.0 - o.elements.eccentricity * std::cos(E));  // dE/dt
    s.position = OrbitPositionFromEccentric(o, E);
    s.velocity = o.p * (-o.elements.semiMajorAxis * std::sin(E) * rate) + o.q * (o.semiMinorAxis * std::cos(E) * rate);
    s.time = simTime;
    s.valid = true;
    return s.position;
}

}  // namespace

Vec3d FamilyPosition(const SceneGraph& scene, const AsteroidBelt& belt, const AsteroidFamily& family, double simTime) { return FamilyCenter(scene, belt, family, simTime); }

void OrbitLayer::Collect(const FrameContext& c, FramePacket& out) {
    if (!c.features.Enabled(Feature::OrbitLines)) return;
    const std::vector<KeplerOrbit>& orbits = world_.orbits;
    const std::vector<OrbitPathRecord>& paths = world_.orbitPaths;
    const float focal = c.camera.FocalPx();
    const float nearZ = c.camera.NearZ();

    chunks_.resize(JobSystem::ChunkCount(paths.size(), kPathGrain));
    c.jobs.ParallelFor(paths.size(), kPathGrain, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
        std::vector<LineInstance>& output = chunks_[chunk];
        output.clear();
        for (std::size_t i = begin; i < end; ++i) {
            const OrbitPathRecord& path = paths[i];
            if (!path.used) continue;
            const KeplerOrbit& orbit = orbits[path.orbit];
            if (orbit.node == kNoNode || !orbit.active) continue;
            if (path.quiet) {
                const int owner = path.owner, shown = world_.highlightBody;
                if (owner < 0 || shown < 0 || (owner != shown && world_.bodies[static_cast<std::size_t>(owner)].parent != shown)) continue;
            }
            const Vec3d center = c.scene.WorldPosition(path.parent);
            const double distance = std::max(Length(center - c.cameraPosition), 1.0);
            const float pixels = static_cast<float>(orbit.elements.semiMajorAxis / distance) * focal;
            if (pixels < kPathMinPixels) continue;

            Color color = path.color;
            color.a *= std::min((pixels - kPathMinPixels) / kPathFadePixels, 1.0f);

            // Los puntos se concentran cerca del cuerpo y pasan justo por su centro. Asi la
            // linea no se separa de el aunque la camara este a pocos radios.
            const Mat4d& parent = c.scene.World(path.parent);
            const double bodyE = EccentricAnomalyAt(orbit, c.simTime);
            const auto point = [&](int step) {
                const double u = 2.0 * step / kPathSegments - 1.0;
                const Vec3d local = OrbitPositionFromEccentric(orbit, bodyE + kPi * (0.85 * u * u * u + 0.15 * u));
                const Vec3d world = center + parent.TransformDirection(local);
                return c.viewRotation.TransformDirection(Vec3f(world - c.cameraPosition));
            };
            Vec3f previous = point(0);
            for (int step = 1; step <= kPathSegments; ++step) {
                const Vec3f current = point(step);
                if (previous.z > nearZ || current.z > nearZ) output.push_back({previous, current, color});
                previous = current;
            }
        }
    });
    Append(chunks_, out.lines);
}

void NebulaLayer::Collect(const FrameContext& c, FramePacket& out) {
    if (!c.features.Enabled(Feature::Nebula)) return;
    const float focal = c.camera.FocalPx();
    for (const NebulaRecord& n : world_.nebulae) {
        const Vec3d world = n.position - world_.origin;
        const Vec3f rel(world - c.cameraPosition);
        const double distance = Length(world - c.cameraPosition);
        // Una nube menor que unos pixeles no compensa una pasada de pantalla completa.
        if (distance > n.radius && n.radius / distance * focal < 3.0) continue;
        if (distance > n.radius && !c.frustum.SphereVisible(rel, static_cast<float>(n.radius))) continue;

        NebulaInstance v;
        v.viewCenter = c.viewRotation.TransformDirection(rel);
        v.radius = static_cast<float>(n.radius);
        v.inner = n.inner;
        v.outer = n.outer;
        v.density = n.density;
        v.seed = n.seed;
        v.distance = static_cast<float>(distance);
        out.nebulae.push_back(v);
    }
}

void BlackHoleLayer::Collect(const FrameContext& c, FramePacket& out) {
    if (!c.features.Enabled(Feature::BlackHoles)) return;
    const float focal = c.camera.FocalPx();
    for (const BlackHoleRecord& b : world_.blackHoles) {
        const Vec3d world = c.scene.WorldPosition(b.node);
        const Vec3f rel(world - c.cameraPosition);
        const double distance = Length(world - c.cameraPosition);
        const double reachRadii = std::max(36.0, static_cast<double>(b.discOuter) * 1.8);
        const double reach = b.schwarzschildRadius * reachRadii;
        const double pixels = reach / std::max(distance - reach, reach * 0.01) * focal;
        // Lejos basta con el sprite: la lente no llegaria a mover ni un pixel.
        if (pixels < 12.0) continue;
        if (distance > reach && !c.frustum.SphereVisible(rel, static_cast<float>(reach))) continue;

        BlackHoleInstance h;
        h.viewCenter = c.viewRotation.TransformDirection(rel);
        h.schwarzschildRadius = static_cast<float>(b.schwarzschildRadius);
        h.viewAxis = Normalize(c.viewRotation.TransformDirection(b.orientation.ToMatrix().TransformDirection(Vec3f{0, -1, 0})));
        h.spin = b.spin;
        h.discInner = b.discInner;
        h.discOuter = b.discOuter;
        h.discBrightness = b.brightness;
        h.seed = b.seed;
        h.reach = static_cast<float>(reachRadii);
        h.pixelRadius = static_cast<float>(std::min(pixels, 1.0e6));
        h.distance = static_cast<float>(distance);
        out.blackHoles.push_back(h);
    }
}

void AsteroidLayer::Collect(const FrameContext& c, FramePacket& out) {
    if (!c.features.Enabled(Feature::Asteroids)) return;
    ++frame_;
    viewForward_ = c.camera.Forward();

    const float focal = c.camera.FocalPx();
    const float nearZ = c.camera.NearZ();
    std::size_t calculated = 0;

    for (std::size_t beltIndex = 0; beltIndex < world_.belts.size(); ++beltIndex) {
        const AsteroidBelt& belt = world_.belts[beltIndex];
        if (!belt.used || belt.parent == kNoNode) continue;
        const Vec3d center = c.scene.WorldPosition(belt.parent);
        const Mat4d& frame = c.scene.World(belt.parent);
        const double centerDistance = Length(center - c.cameraPosition);
        const bool selected = belt.body >= 0 && belt.body == world_.highlightBody;

        // Las orbitas guardadas de este cinturon; si en su sitio hay ahora otro, se empieza de cero.
        if (samples_.size() < world_.belts.size()) samples_.resize(world_.belts.size());
        BeltSamples& samples = samples_[beltIndex];
        if (samples.seed != belt.seed || samples.inner != belt.inner || samples.outer != belt.outer || samples.dust.size() != belt.dust.size() || samples.families.size() != belt.families.size()) {
            samples.seed = belt.seed;
            samples.inner = belt.inner;
            samples.outer = belt.outer;
            samples.dust.assign(belt.dust.size(), OrbitSample{});
            samples.families.assign(belt.families.size(), OrbitSample{});
        }

        // Dos anillos marcan los bordes del campo: asi se ve donde esta sin calcular ni una roca.
        const float ringPixels = static_cast<float>(belt.outer / std::max(centerDistance, 1.0)) * focal;
        if (belt.outer > belt.inner && ringPixels >= kPathMinPixels * 3.0f && c.features.Enabled(Feature::OrbitLines)) {
            Color color = belt.dustColor;
            color.a = selected ? 0.75f : 0.13f * std::min(ringPixels / 80.0f, 1.0f);
            for (const double radius : {belt.inner, belt.outer}) {
                const auto point = [&](int step) {
                    const double angle = 2.0 * kPi * step / kRingSegments;
                    const Vec3d world = center + frame.TransformDirection(Vec3d{std::cos(angle) * radius, 0.0, std::sin(angle) * radius});
                    return c.viewRotation.TransformDirection(Vec3f(world - c.cameraPosition));
                };
                Vec3f previous = point(0);
                for (int step = 1; step <= kRingSegments; ++step) {
                    const Vec3f current = point(step);
                    if (previous.z > nearZ || current.z > nearZ) out.lines.push_back({previous, current, color});
                    previous = current;
                }
            }
        }

        // Polvo: no se calcula hasta que el anillo ocupe pantalla.
        const double width = belt.outer - belt.inner;
        const float beltPixels = static_cast<float>(width / std::max(centerDistance, belt.outer * 0.2)) * focal;
        if (!belt.dust.empty() && beltPixels >= settings_.beltMinPixels) {
            const float alpha = belt.dustColor.a * std::min((beltPixels - settings_.beltMinPixels) / 60.0f, 1.0f) * (selected ? 1.8f : 1.0f);
            const float dustRadius = settings_.dustPixels * (selected ? 1.4f : 1.0f);
            dustChunks_.resize(JobSystem::ChunkCount(belt.dust.size(), kDustGrain));
            c.jobs.ParallelFor(belt.dust.size(), kDustGrain, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
                std::vector<SpriteInstance>& output = dustChunks_[chunk];
                output.clear();
                for (std::size_t i = begin; i < end; ++i) {
                    // Cada mota la toca un solo hilo: su muestra se puede escribir aqui.
                    const Vec3d world = center + frame.TransformDirection(SampledOrbitPosition(belt.dust[i], samples.dust[i], c.simTime, (i + frame_) % kOrbitRefresh == 0));
                    const Vec3f view = c.viewRotation.TransformDirection(Vec3f(world - c.cameraPosition));
                    if (view.z <= nearZ) continue;
                    SpriteInstance s;
                    s.viewPosition = view;
                    s.pixelRadius = dustRadius;
                    s.tint = belt.dustColor;
                    s.tint.a = alpha;
                    s.id = 0x40000000u + static_cast<std::uint32_t>(i);
                    s.texture = settings_.dot;
                    s.blend = BlendMode::Additive;
                    output.push_back(s);
                }
            });
            Append(dustChunks_, out.sprites);
            calculated += belt.dust.size();
        }

        // Dentro del anillo: su polvo forma la banda alrededor, y cerca pasan rocas sueltas.
        if (belt.mu > 0.0 && !belt.families.empty() && !belt.families.front().fixed) {
            field_.clear();
            BeltField(belt, frame, center, c.cameraPosition, settings_.fieldDustReach, c.simTime, BeltDustTier(belt), field_);
            calculated += field_.size();
            const float reach = static_cast<float>(settings_.fieldDustReach);
            for (const BeltFieldRock& r : field_) {
                const Vec3f rel(r.position - c.cameraPosition);
                const float d = Length(rel);
                if (d < 1.0f || !c.frustum.SphereVisible(rel, 1.0f)) continue;
                const Vec3f view = c.viewRotation.TransformDirection(rel);
                if (view.z <= nearZ) continue;
                // Polvo iluminado por la estrella: motas calidas, mas grandes y vivas cuanto mas cerca.
                // (La textura del punto solo sirve sumando luz: con mezcla normal no se veria.)
                const float fade = 1.0f - d / reach;
                SpriteInstance s;
                s.viewPosition = view;
                s.pixelRadius = settings_.fieldDustPixels * (0.8f + r.radius * 0.3f) * (1.6f + 3.0f * fade * fade);
                s.tint = Color{2.3f, 1.8f, 1.1f, 0.35f + 0.6f * fade};
                s.id = 0x50000000u + (r.id & 0x0FFFFFFFu);
                s.texture = settings_.dot;
                s.blend = BlendMode::Additive;
                out.sprites.push_back(s);
            }
            field_.clear();
            BeltField(belt, frame, center, c.cameraPosition, settings_.fieldRockReach, c.simTime, BeltRockTier(belt), field_);
            calculated += field_.size();
            const bool hasMeshes = !settings_.meshes.empty() && c.features.Enabled(Feature::Models);
            const Vec3f lightView = Normalize(c.viewRotation.TransformDirection(Vec3f(center - c.cameraPosition)));
            for (const BeltFieldRock& r : field_) {
                const Vec3f rel(r.position - c.cameraPosition);
                const float d = Length(rel);
                const float pixels = r.radius / std::max(d, 1e-3f) * focal;
                if (pixels < settings_.dotMinPixels || !c.frustum.SphereVisible(rel, r.radius * 1.4f)) continue;
                const Vec3f view = c.viewRotation.TransformDirection(rel);
                if (hasMeshes && pixels >= settings_.meshMinPixels) {
                    const double angle = std::fmod(static_cast<double>(r.spinPhase) + static_cast<double>(r.spinRate) * c.simTime, 2.0 * kPi);
                    MeshInstance m;
                    m.viewPosition = view;
                    m.scale = r.radius;
                    m.rotation = c.camera.Orientation().Conjugate() * Quatf::FromAxisAngle(r.spinAxis, static_cast<float>(angle));
                    m.lightDirection = lightView;
                    m.color = r.color;
                    m.id = 0x58000000u + (r.id & 0x07FFFFFFu);
                    m.mesh = settings_.meshes[r.variant % settings_.meshes.size()];
                    m.texture = 0;
                    out.meshes.push_back(m);
                } else if (view.z > nearZ) {
                    SpriteInstance s;
                    s.viewPosition = view;
                    s.pixelRadius = std::max(pixels * 1.6f, 1.2f);
                    s.tint = Color{r.color.r * 2.2f, r.color.g * 2.0f, r.color.b * 1.8f, std::min(pixels / settings_.dotMinPixels - 1.0f, 1.0f)};
                    s.id = 0x5C000000u + (r.id & 0x03FFFFFFu);
                    s.texture = settings_.dot;
                    s.blend = BlendMode::Additive;
                    out.sprites.push_back(s);
                }
            }
        }

        // Familias: solo se resuelve la ecuacion si la camara llega a su distancia maxima.
        const double farthest = belt.outer * 1.35 + settings_.nearRange;
        if (centerDistance > farthest && !belt.families.front().fixed) continue;

        for (std::size_t familyIndex = 0; familyIndex < belt.families.size(); ++familyIndex) {
            const AsteroidFamily& f = belt.families[familyIndex];
            // De lejos basta la posicion guardada para saber que no esta a la vista; la que se va a
            // dibujar se resuelve exacta, igual que la calcula el juego para apuntar y chocar.
            ++calculated;
            if (!f.fixed) {
                const Vec3d rough = center + frame.TransformDirection(SampledOrbitPosition(f.orbit, samples.families[familyIndex], c.simTime, (familyIndex + frame_) % kOrbitRefresh == 0));
                if (Length(rough - c.cameraPosition) > f.radius + settings_.nearRange + 200.0) continue;
            }
            const Vec3d familyCenter = FamilyCenter(c.scene, belt, f, c.simTime);
            const double distance = Length(familyCenter - c.cameraPosition);
            if (distance > f.radius + settings_.nearRange) continue;

            // Primera vez que se acerca: aqui nacen sus rocas.
            Cached& cached = cache_[f.seed];
            if (cached.rocks.empty()) cached.rocks = GenerateRocks(f);
            cached.lastUsed = frame_;
            cached.belt = static_cast<int>(beltIndex);
            cached.family = static_cast<int>(familyIndex);

            // Lo que un choque deshizo no vuelve, y lo que va a chocar ya se esta moviendo.
            const auto deadEntry = world_.deadRocks.find(f.seed);
            const std::set<int>* dead = deadEntry != world_.deadRocks.end() ? &deadEntry->second : nullptr;
            int movingRock = -1;
            Vec3f movingOffset;
            for (const RockDuel& duel : duels_) {
                if (duel.seed != f.seed) continue;
                const float s = std::min(duel.age / duel.duration, 1.0f);
                movingRock = duel.mover;
                movingOffset = duel.from + (duel.to - duel.from) * (s * s);
            }

            const std::vector<AsteroidRock>& rocks = cached.rocks;
            const Vec3f lightView = Normalize(c.viewRotation.TransformDirection(Vec3f(center - familyCenter)));
            const Vec3f base(familyCenter - c.cameraPosition);
            const bool hasMeshes = !settings_.meshes.empty() && c.features.Enabled(Feature::Models);

            meshChunks_.resize(JobSystem::ChunkCount(rocks.size(), kRockGrain));
            rockChunks_.resize(meshChunks_.size());
            c.jobs.ParallelFor(rocks.size(), kRockGrain, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
                std::vector<MeshInstance>& meshes = meshChunks_[chunk];
                std::vector<SpriteInstance>& dots = rockChunks_[chunk];
                meshes.clear();
                dots.clear();
                for (std::size_t i = begin; i < end; ++i) {
                    const AsteroidRock& r = rocks[i];
                    if (dead && dead->count(static_cast<int>(i)) != 0) continue;
                    const Vec3f rel = base + (static_cast<int>(i) == movingRock ? movingOffset : r.offset);
                    const float d = Length(rel);
                    const float pixels = r.radius / std::max(d, 1e-3f) * focal;
                    if (pixels < settings_.dotMinPixels) continue;
                    if (!c.frustum.SphereVisible(rel, r.radius * 1.4f)) continue;
                    const Vec3f view = c.viewRotation.TransformDirection(rel);

                    if (hasMeshes && pixels >= settings_.meshMinPixels) {
                        const double angle = std::fmod(static_cast<double>(r.spinPhase) + static_cast<double>(r.spinRate) * c.simTime, 2.0 * kPi);
                        const Quatf spin = Quatf::FromAxisAngle(r.spinAxis, static_cast<float>(angle));
                        MeshInstance m;
                        m.viewPosition = view;
                        m.scale = r.radius;
                        m.rotation = c.camera.Orientation().Conjugate() * spin;
                        m.lightDirection = lightView;
                        m.color = r.color;
                        m.id = 0x20000000u + static_cast<std::uint32_t>(i);
                        m.mesh = settings_.meshes[r.variant % settings_.meshes.size()];
                        m.texture = 0;
                        meshes.push_back(m);
                    } else if (view.z > nearZ) {
                        SpriteInstance s;
                        s.viewPosition = view;
                        s.pixelRadius = std::max(pixels * 1.6f, 1.0f);
                        s.tint = r.color;
                        s.tint.a = std::min(pixels / settings_.dotMinPixels - 1.0f, 1.0f);
                        s.id = 0x30000000u + static_cast<std::uint32_t>(i);
                        s.texture = settings_.dot;
                        s.blend = BlendMode::Additive;
                        dots.push_back(s);
                    }
                }
            });
            Append(meshChunks_, out.meshes);
            Append(rockChunks_, out.sprites);
            calculated += rocks.size();
        }
    }

    // Las familias lejanas se olvidan: la semilla basta para volver a crearlas.
    if (cache_.size() > settings_.maxCachedFamilies) {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.lastUsed + 120 < frame_) it = cache_.erase(it);
            else ++it;
        }
    }
    out.stats.asteroidsCalculated += calculated;}

bool AsteroidLayer::NearestFamily(const World& world, const Vec3d& position, double simTime, Vec3d& center, int beltIndex) {
    double best = 1.0e300;
    for (std::size_t b = 0; b < world.belts.size(); ++b) {
        const AsteroidBelt& belt = world.belts[b];
        if (!belt.used || belt.parent == kNoNode) continue;
        if (beltIndex >= 0 && static_cast<int>(b) != beltIndex) continue;
        for (const AsteroidFamily& f : belt.families) {
            const Vec3d c = FamilyCenter(world.scene, belt, f, simTime);
            const double d = Length(c - position);
            if (d < best) {
                best = d;
                center = c;
            }
        }
    }
    return best < 1.0e300;
}

bool AsteroidLayer::PickRock(const Vec3d& origin, const Vec3d& direction, double simTime, float margin, RockPick& out) const {
    bool found = false;
    double nearest = 1.0e300;
    for (const auto& [seed, cached] : cache_) {
        if (cached.belt < 0 || cached.lastUsed + 2 < frame_) continue;
        const AsteroidBelt& belt = world_.belts[static_cast<std::size_t>(cached.belt)];
        if (!belt.used || static_cast<std::size_t>(cached.family) >= belt.families.size()) continue;
        const AsteroidFamily& f = belt.families[static_cast<std::size_t>(cached.family)];
        if (f.seed != seed) continue;
        const Vec3d center = FamilyCenter(world_.scene, belt, f, simTime);
        const auto dead = world_.deadRocks.find(seed);
        for (std::size_t i = 0; i < cached.rocks.size(); ++i) {
            const AsteroidRock& r = cached.rocks[i];
            if (dead != world_.deadRocks.end() && dead->second.count(static_cast<int>(i)) != 0) continue;
            const Vec3d rel = center + Vec3d(r.offset) - origin;
            const double along = Dot(rel, direction);
            if (along <= 0.0 || along >= nearest) continue;
            const double miss = Length(rel - direction * along);
            if (miss > std::max(static_cast<double>(r.radius) * 1.3, along * margin)) continue;
            nearest = along;
            out.belt = cached.belt;
            out.family = cached.family;
            out.rock = static_cast<int>(i);
            out.data = r;
            out.distance = along;
            found = true;
        }
    }
    return found;
}

bool AsteroidLayer::StartRockCollision(const Vec3d& camera) {
    // La familia generada mas reciente y mas cercana es la que esta a la vista.
    const Cached* best = nullptr;
    std::uint32_t bestSeed = 0;
    double nearest = 1.0e300;
    for (const auto& [seed, cached] : cache_) {
        if (cached.belt < 0 || cached.lastUsed + 2 < frame_) continue;
        const AsteroidBelt& belt = world_.belts[static_cast<std::size_t>(cached.belt)];
        if (!belt.used || static_cast<std::size_t>(cached.family) >= belt.families.size()) continue;
        const AsteroidFamily& f = belt.families[static_cast<std::size_t>(cached.family)];
        if (f.seed != seed) continue;
        const double d = Length(FamilyCenter(world_.scene, belt, f, world_.simTime) - camera);
        if (d < nearest) {
            nearest = d;
            best = &cached;
            bestSeed = seed;
        }
    }
    if (!best) return false;
    for (const RockDuel& duel : duels_)
        if (duel.seed == bestSeed) return false;

    const AsteroidBelt& belt = world_.belts[static_cast<std::size_t>(best->belt)];
    const AsteroidFamily& f = belt.families[static_cast<std::size_t>(best->family)];
    const Vec3f toCamera(camera - FamilyCenter(world_.scene, belt, f, world_.simTime));
    const auto dead = world_.deadRocks.find(bestSeed);
    const auto isDead = [&](int i) { return dead != world_.deadRocks.end() && dead->second.count(i) != 0; };

    // Primero una roca visible, cerca de la camara; despues su vecina mas proxima.
    int first = -1;
    float score = 1.0e30f;
    const int count = static_cast<int>(best->rocks.size());
    for (int i = 0; i < count; ++i) {
        const AsteroidRock& r = best->rocks[static_cast<std::size_t>(i)];
        if (isDead(i)) continue;
        const Vec3f fromCamera = r.offset - toCamera;
        const float distance = Length(fromCamera);
        const float facing = Dot(fromCamera, viewForward_) / std::max(distance, 1e-3f);
        if (facing < 0.93f) continue;  // fuera del encuadre
        const float value = distance / std::max(r.radius, 0.05f) * (1.0f + (1.0f - facing) * 8.0f) * static_cast<float>(1.0 + astro::Uniform(rng_, 0, 1));
        if (value < score) {
            score = value;
            first = i;
        }
    }
    if (first < 0) return false;
    int second = -1;
    score = 1.0e30f;
    const AsteroidRock& a = best->rocks[static_cast<std::size_t>(first)];
    for (int i = 0; i < count; ++i) {
        if (i == first || isDead(i)) continue;
        const AsteroidRock& r = best->rocks[static_cast<std::size_t>(i)];
        const float value = Length(r.offset - a.offset) / std::sqrt(std::max(r.radius, 0.05f));
        if (value < score) {
            score = value;
            second = i;
        }
    }
    if (second < 0) return false;

    RockDuel duel;
    duel.seed = bestSeed;
    duel.belt = best->belt;
    duel.family = best->family;
    duel.mover = first;
    duel.target = second;
    duel.from = a.offset;
    duel.to = best->rocks[static_cast<std::size_t>(second)].offset;
    duel.duration = 3.0f;
    duel.speed = astro::Uniform(rng_, 0.3, 5.0);  // velocidad tipica de un choque en un cinturon
    duels_.push_back(duel);
    return true;
}

std::vector<RockHit> AsteroidLayer::Tick(double seconds, const Vec3d& camera, float ratePerMinute, World& world, const FeatureSet& features) {
    std::vector<RockHit> hits;
    if (!features.Enabled(Feature::Collisions) || !features.Enabled(Feature::Asteroids)) return hits;

    for (std::size_t i = 0; i < duels_.size();) {
        RockDuel& duel = duels_[i];
        duel.age += static_cast<float>(seconds);
        if (duel.age < duel.duration) {
            ++i;
            continue;
        }
        const auto cached = cache_.find(duel.seed);
        if (cached != cache_.end()) {
            const std::vector<AsteroidRock>& rocks = cached->second.rocks;
            hits.push_back({duel.belt, duel.family, duel.to, std::max(rocks[static_cast<std::size_t>(duel.mover)].radius, rocks[static_cast<std::size_t>(duel.target)].radius), duel.speed});
            world.deadRocks[duel.seed].insert(duel.mover);
            world.deadRocks[duel.seed].insert(duel.target);
        }
        duels_.erase(duels_.begin() + static_cast<std::ptrdiff_t>(i));
    }

    if (ratePerMinute > 0.0f) {
        nextRockCollision_ -= seconds;
        if (nextRockCollision_ <= 0.0) {
            nextRockCollision_ = 60.0 / ratePerMinute * astro::Uniform(rng_, 0.5, 1.5);
            StartRockCollision(camera);
        }
    }
    return hits;
}

}  // namespace eps
