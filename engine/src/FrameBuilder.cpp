#include "epsilon/FrameBuilder.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <stdexcept>

namespace eps {

namespace {

constexpr std::size_t kCullGrain = 4096;
constexpr std::size_t kSerialSortLimit = 8192;

// Orden total: de atras hacia adelante y, a igual profundidad, por id.
// Al no haber empates, el resultado no depende del numero de hilos.
bool BackToFront(const SpriteInstance& a, const SpriteInstance& b) {
    if (a.viewPosition.z != b.viewPosition.z) return a.viewPosition.z > b.viewPosition.z;
    return a.id < b.id;
}

void SortSprites(std::vector<SpriteInstance>& sprites, JobSystem& jobs) {
    const std::size_t threads = jobs.ThreadCount();
    if (threads == 1 || sprites.size() < kSerialSortLimit) {
        std::sort(sprites.begin(), sprites.end(), BackToFront);
        return;
    }

    // Ordena tramos en paralelo y los mezcla por parejas hasta dejar uno solo.
    std::size_t run = (sprites.size() + threads - 1) / threads;
    SpriteInstance* data = sprites.data();
    const std::size_t total = sprites.size();

    jobs.ParallelFor(total, run, [data](std::size_t begin, std::size_t end, std::size_t) {
        std::sort(data + begin, data + end, BackToFront);
    });

    for (; run < total; run *= 2) {
        jobs.ParallelFor(JobSystem::ChunkCount(total, run * 2), 1,
                         [data, run, total](std::size_t begin, std::size_t, std::size_t) {
                             const std::size_t first = begin * run * 2;
                             const std::size_t mid = std::min(first + run, total);
                             const std::size_t last = std::min(first + run * 2, total);
                             std::inplace_merge(data + first, data + mid, data + last, BackToFront);
                         });
    }
}

void BuildBatches(const std::vector<SpriteInstance>& sprites, std::vector<SpriteBatch>& batches) {
    batches.clear();
    for (std::uint32_t i = 0; i < sprites.size(); ++i) {
        const SpriteInstance& s = sprites[i];
        if (!batches.empty() && batches.back().texture == s.texture && batches.back().blend == s.blend) {
            ++batches.back().count;
        } else {
            batches.push_back({i, 1, s.texture, s.blend});
        }
    }
}

template <class T>
void ConcatChunks(std::vector<std::vector<T>>& chunks, std::vector<T>& out, JobSystem& jobs) {
    std::size_t total = 0;
    std::vector<std::size_t> offsets(chunks.size());
    for (std::size_t c = 0; c < chunks.size(); ++c) {
        offsets[c] = total;
        total += chunks[c].size();
    }
    out.resize(total);
    T* dst = out.data();
    jobs.ParallelFor(chunks.size(), 1, [&](std::size_t begin, std::size_t, std::size_t) {
        std::copy(chunks[begin].begin(), chunks[begin].end(), dst + offsets[begin]);
    });
}

}  // namespace

void FrameBuilder::Build(const FrameInput& input, JobSystem& jobs, FramePacket& out) {
    if (!input.scene || !input.renderables) throw std::invalid_argument("FrameBuilder: entrada incompleta");

    const SceneGraph& scene = *input.scene;
    const Camera& camera = input.camera;
    const std::vector<Renderable>& renderables = *input.renderables;

    out.frameNumber = input.frameNumber;
    out.time = input.time;
    out.width = camera.Width();
    out.height = camera.Height();
    out.focalPx = camera.FocalPx();
    out.nearZ = camera.NearZ();
    out.viewRotation = camera.ViewRotation();
    out.projection = camera.Projection();
    out.clearColor = input.clearColor;
    out.screenTint = input.screenTint;
    out.fogDistance = input.fogDistance;
    out.fogColor = input.fogColor;
    out.fogOrigin = input.fogOrigin;
    out.fogRampOn = input.fogRampOn;
    out.fogRamp = input.fogRamp;
    out.fogTone = input.fogTone;
    out.skyTint = input.skyTint;
    const bool sky = input.features.Enabled(Feature::Sky);
    out.skyTexture = sky ? input.skyTexture : kNoTexture;
    out.sky = sky ? input.sky : nullptr;
    out.surfaces = input.surfaces;
    out.post = input.post;
    out.post.bloom = input.post.bloom && input.features.Enabled(Feature::Bloom);
    if (!input.features.Enabled(Feature::Vignette)) out.post.vignette = 0.0f;
    if (!input.features.Enabled(Feature::Aberration)) out.post.aberration = 0.0f;
    if (!input.features.Enabled(Feature::Grain)) out.post.grain = 0.0f;
    out.meshes.clear();
    out.meshBatches.clear();
    out.craters.clear();
    out.nebulae.clear();
    out.blackHoles.clear();
    out.starLights.clear();
    out.atmospheres.clear();
    out.lines.clear();
    out.stats = {};

    const Frustum frustum = camera.CameraRelativeFrustum();
    const Mat4f viewRotation = camera.ViewRotation();
    const Vec3d cameraPos = camera.Position();
    const float focal = camera.FocalPx();
    const float nearZ = camera.NearZ();
    const bool spheresEnabled = input.surfaces && !input.surfaces->empty();

    const bool asteroids = input.features.Enabled(Feature::Asteroids);
    const bool models = input.features.Enabled(Feature::Models);
    spriteChunks_.resize(JobSystem::ChunkCount(renderables.size(), kCullGrain));
    sphereChunks_.resize(spriteChunks_.size());
    meshChunks_.resize(spriteChunks_.size());
    jobs.ParallelFor(renderables.size(), kCullGrain, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
        std::vector<SpriteInstance>& output = spriteChunks_[chunk];
        std::vector<SphereInstance>& sphereOutput = sphereChunks_[chunk];
        output.clear();
        sphereOutput.clear();
        meshChunks_[chunk].clear();
        for (std::size_t i = begin; i < end; ++i) {
            const Renderable& r = renderables[i];
            if (r.node < 0 || r.hidden) continue;
            const Vec3d worldPosition = scene.WorldPosition(r.node);
            const Vec3f rel(worldPosition - cameraPos);  // resta en double
            const float worldRadius = r.radius * static_cast<float>(scene.WorldMaxScale(r.node));
            const Vec3f view = viewRotation.TransformDirection(rel);

            if (r.asteroid && !asteroids) continue;

            if (spheresEnabled && r.surface >= 0) {
                const float distance = Length(rel);
                if (distance <= worldRadius) continue;  // camara dentro del cuerpo

                const float pixels = SilhouettePixels(worldRadius, distance, focal);
                if (pixels >= input.lod.sphereMinPixels) {
                    // Sin la prueba view.z: el centro puede quedar detras de la camara
                    // mientras la superficie sigue llenando la pantalla.
                    if (!frustum.SphereVisible(rel, worldRadius * (r.mesh != kNoMesh ? 1.5f : 1.0f))) continue;

                    const Quatf body = BodyOrientation(r, input.simTime);

                    // Lo que no ha colapsado por su gravedad no es redondo.
                    if (r.mesh != kNoMesh && models) {
                        MeshInstance m;
                        m.viewPosition = view;
                        m.scale = worldRadius;
                        m.rotation = camera.Orientation().Conjugate() * body;
                        if (r.lightNode >= 0) m.lightDirection = Normalize(viewRotation.TransformDirection(Vec3f(scene.WorldPosition(r.lightNode) - worldPosition)));
                        // Un asteroide oscuro refleja un 5 %: se aclara para que su forma se lea.
                        m.color = {std::min(r.tint.r * 2.6f, 1.0f), std::min(r.tint.g * 2.6f, 1.0f), std::min(r.tint.b * 2.6f, 1.0f), 1.0f};
                        m.ambient = 0.07f;
                        m.id = 0x10000000u + static_cast<std::uint32_t>(i);
                        m.mesh = r.mesh;
                        meshChunks_[chunk].push_back(m);
                        continue;
                    }

                    SphereInstance s;
                    s.viewCenter = view;
                    s.radius = worldRadius;
                    s.viewToBody = body.Conjugate() * camera.Orientation();
                    s.scarDirection = r.scarDirection;
                    s.scarSize = r.scarSize;
                    s.scarHeat = r.scarHeat;
                    if (r.lightNode >= 0) {
                        const Vec3f toLight(scene.WorldPosition(r.lightNode) - worldPosition);
                        s.lightDirection = Normalize(viewRotation.TransformDirection(toLight));
                    }
                    if (r.lightNode2 >= 0) {
                        // Se compara el flujo de cada estrella en este punto. La mas debil se
                        // suaviza con una raiz: el ojo, y una pantalla, no ven un factor 100.
                        const Vec3f toLight2(scene.WorldPosition(r.lightNode2) - worldPosition);
                        const Vec3f toLight1(scene.WorldPosition(r.lightNode) - worldPosition);
                        const float d1 = std::max(Length(toLight1), 1.0f), d2 = std::max(Length(toLight2), 1.0f);
                        const float flux1 = r.lightPower / (d1 * d1), flux2 = r.lightPower2 / (d2 * d2);
                        const float brightest = std::max(std::max(flux1, flux2), 1e-30f);
                        const float strength1 = std::sqrt(flux1 / brightest), strength2 = std::sqrt(flux2 / brightest);
                        s.light2Direction = Normalize(viewRotation.TransformDirection(toLight2));
                        s.light2Strength = strength2;
                        s.lightColor = {r.lightColor.r * strength1, r.lightColor.g * strength1, r.lightColor.b * strength1, 1.0f};
                        s.light2Color = r.lightColor2;
                    }
                    s.pixelRadius = pixels;
                    s.detail = SurfaceDetail(focal * worldRadius / (distance - worldRadius), input.lod);
                    s.seed = r.seed;
                    s.distance = distance;
                    s.id = static_cast<std::uint32_t>(i);
                    s.surface = static_cast<std::uint16_t>(r.surface);
                    sphereOutput.push_back(s);

                    continue;
                }
            }

            if (view.z <= nearZ) continue;

            // Un planeta real no se ve a distancias interestelares, aunque su sprite tenga tamano minimo.
            float fade = 1.0f;
            if (r.hideInside > 0.0f) {
                const float near = Length(rel) / r.hideInside;
                if (near <= 1.0f) continue;
                fade = std::min(near - 1.0f, 1.0f);
            }
            if (r.fadeDistance > 0.0f) {
                fade = std::min(fade, 2.0f - Length(rel) / r.fadeDistance);
                if (fade <= 0.0f) continue;
            }

            // Una estrella con radio minimo en pixeles ocupa mas que su radio real.
            const float spriteRadius = worldRadius * r.spriteScale;
            const float cullRadius = std::max(spriteRadius, r.minPixelRadius * view.z / focal);
            if (!frustum.SphereVisible(rel, cullRadius)) continue;

            SpriteInstance s;
            s.viewPosition = view;
            s.pixelRadius = PixelSize(spriteRadius, view.z, focal, r.minPixelRadius, r.maxPixelRadius);
            s.tint = r.tint;
            s.tint.a *= fade;
            // De cerca, el brillo de una estrella en la niebla lo da la dispersion, que no depende
            // de donde quede su centro: un sprite enorme desapareceria de golpe al girar.
            if (input.fogDistance > 0.0f && r.fogReach > 1.0f) {
                s.tint.a *= 1.0f - std::clamp((s.pixelRadius - 40.0f) / 200.0f, 0.0f, 1.0f);
                if (s.tint.a <= 0.0f) continue;
            }
            s.id = static_cast<std::uint32_t>(i);
            s.texture = r.texture;
            s.blend = r.blend;
            s.fogReach = r.fogReach;
            output.push_back(s);
        }
    });
    ConcatChunks(spriteChunks_, out.sprites, jobs);
    ConcatChunks(meshChunks_, out.meshes, jobs);

    // Hay pocas esferas: cada una ocupa pantalla y se ordena sin repartir el trabajo.
    ConcatChunks(sphereChunks_, out.spheres, jobs);
    std::sort(out.spheres.begin(), out.spheres.end(), [](const SphereInstance& a, const SphereInstance& b) {
        return a.distance != b.distance ? a.distance > b.distance : a.id < b.id;
    });

    if (input.links && input.features.Enabled(Feature::Routes)) {
        const std::vector<LinkRenderable>& links = *input.links;
        lineChunks_.resize(JobSystem::ChunkCount(links.size(), kCullGrain));
        jobs.ParallelFor(links.size(), kCullGrain, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
            std::vector<LineInstance>& output = lineChunks_[chunk];
            output.clear();
            for (std::size_t i = begin; i < end; ++i) {
                const LinkRenderable& l = links[i];
                const Vec3f a = viewRotation.TransformDirection(Vec3f(scene.WorldPosition(l.a) - cameraPos));
                const Vec3f b = viewRotation.TransformDirection(Vec3f(scene.WorldPosition(l.b) - cameraPos));
                if (a.z <= nearZ && b.z <= nearZ) continue;
                output.push_back({a, b, l.color});
            }
        });
        ConcatChunks(lineChunks_, out.lines, jobs);
    }

    // Las capas anaden instancias antes de ordenar: sus sprites se mezclan con los demas.
    const FrameContext context{scene, camera, input.features, jobs, frustum, viewRotation, cameraPos, input.time, input.simTime};
    for (IFrameLayer* layer : input.layers) layer->Collect(context, out);

    // Lo opaco lo apaga el pase de niebla, que lee la profundidad. Sprites y lineas se dibujan
    // despues de el, asi que se apagan aqui; lo que ya no se ve se quita de la lista.
    if (input.fogDistance > 0.0f) {
        const float fog = input.fogDistance;
        std::erase_if(out.sprites, [fog](SpriteInstance& s) {
            s.tint.a *= std::exp(-std::max(s.viewPosition.z, 0.0f) / (fog * s.fogReach));
            return s.tint.a < 0.004f;
        });
        std::erase_if(out.lines, [fog](LineInstance& l) {
            l.color.a *= std::exp(-std::max(std::min(l.viewA.z, l.viewB.z), 0.0f) / fog);
            return l.color.a < 0.004f;
        });
    }

    SortSprites(out.sprites, jobs);
    BuildBatches(out.sprites, out.batches);

    // Opacas: se agrupan por recurso para dibujarlas con instancias. El id da un orden total.
    std::sort(out.meshes.begin(), out.meshes.end(), [](const MeshInstance& a, const MeshInstance& b) {
        if (a.foreground != b.foreground) return b.foreground;
        if (a.doubleSided != b.doubleSided) return b.doubleSided;
        if (a.mesh != b.mesh) return a.mesh < b.mesh;
        if (a.texture != b.texture) return a.texture < b.texture;
        return a.id < b.id;
    });
    for (std::uint32_t i = 0; i < out.meshes.size(); ++i) {
        const MeshInstance& m = out.meshes[i];
        const MeshBatch* last = out.meshBatches.empty() ? nullptr : &out.meshBatches.back();
        if (last && last->mesh == m.mesh && last->texture == m.texture && last->foreground == m.foreground && last->doubleSided == m.doubleSided) ++out.meshBatches.back().count;
        else out.meshBatches.push_back({i, 1, m.mesh, m.texture, m.foreground, m.doubleSided});
    }

    const auto farFirst = [](const auto& a, const auto& b) { return a.distance > b.distance; };
    std::sort(out.nebulae.begin(), out.nebulae.end(), farFirst);
    std::sort(out.blackHoles.begin(), out.blackHoles.end(), farFirst);

    out.stats.renderablesTotal = renderables.size();
    out.stats.spritesVisible = out.sprites.size();
    out.stats.spheresVisible = out.spheres.size();
    out.stats.linesVisible = out.lines.size();
    out.stats.meshesVisible = out.meshes.size();
}

Quatf BodyOrientation(const Renderable& r, double simTime) {
    // Funcion pura del tiempo: dibujar no cambia ningun estado. Giro directo: alrededor del norte.
    const double spin = std::fmod(r.spinPhase + r.spinRate * simTime, 2.0 * kPi);
    return r.tilt * Quatf::FromAxisAngle({0, -1, 0}, static_cast<float>(spin));
}

float SurfaceDetail(float pixelsPerRadius, const LodSettings& lod) {
    // Frecuencia base del shader: 3 ondulaciones por radio, y cada capa duplica la anterior.
    const float octaves = std::log2(std::max(pixelsPerRadius, 1.0f) / 3.0f) + lod.detailBias;
    return std::clamp(octaves, 1.0f, lod.maxOctaves);
}

float SkyStarBrightness(const SkyStar& star, double time) {
    const float wave = 0.5f + 0.5f * static_cast<float>(std::sin(time * star.twinkleRate + star.phase));
    return star.baseBrightness * (0.35f + 0.65f * wave);
}

std::shared_ptr<const std::vector<SkyStar>> GenerateSky(std::size_t count, std::uint32_t seed) {
    // mt19937 es identico en cualquier compilador; las distribuciones de <random> no lo son.
    std::mt19937 rng(seed);
    const auto unit = [&rng] { return static_cast<float>(rng() / 4294967296.0); };

    auto stars = std::make_shared<std::vector<SkyStar>>(count);
    const float tau = static_cast<float>(2.0 * kPi);
    for (SkyStar& s : *stars) {
        const float z = 2.0f * unit() - 1.0f;
        const float theta = tau * unit();
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        s.direction = {r * std::cos(theta), r * std::sin(theta), z};
        s.size = unit() < 0.2f ? 2.0f : 1.0f;
        s.baseBrightness = 0.3f + 0.7f * unit();
        s.twinkleRate = 0.3f + 2.0f * unit();
        s.phase = tau * unit();
    }
    return stars;
}

}  // namespace eps
