// Pruebas del motor. No necesitan ventana, GPU ni librerias externas.
// Las pruebas "legado" comparan contra copias literales de las formulas del juego original.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "epsilon/Asteroids.h"
#include "epsilon/AstroLayers.h"
#include "epsilon/BlackHole.h"
#include "epsilon/Features.h"
#include "epsilon/HudModel.h"
#include "epsilon/Models.h"
#include "epsilon/Astro.h"
#include "epsilon/Camera.h"
#include "epsilon/Engine.h"
#include "epsilon/FrameBuilder.h"
#include "epsilon/JobSystem.h"
#include "epsilon/Kepler.h"
#include "epsilon/SceneGraph.h"
#include "epsilon/TripleBuffer.h"

using namespace eps;

static int gChecks = 0, gFailures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++gChecks;                                                           \
        if (!(cond)) {                                                       \
            ++gFailures;                                                     \
            std::printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static bool NearD(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// --- Copias de GRAFICOS.cpp y MENUMANAGER.cpp ---------------------------------

struct LegacyPoint { float x, y, escala, profundidad; bool visible; };

static LegacyPoint LegacyCalcularProyeccion(float objX, float objY, float objZ, float camX, float camY, float camZ,
                                            float yaw, float pitch, float roll, float fov, float screenW, float screenH) {
    float dx = objX - camX; float dy = objY - camY; float dz = objZ - camZ;
    float x1 = dx * cos(-yaw) - dz * sin(-yaw); float y1 = dy; float z1 = dx * sin(-yaw) + dz * cos(-yaw);
    float x2 = x1; float y2 = y1 * cos(-pitch) - z1 * sin(-pitch); float z2 = y1 * sin(-pitch) + z1 * cos(-pitch);
    float x3 = x2 * cos(-roll) - y2 * sin(-roll); float y3 = x2 * sin(-roll) + y2 * cos(-roll); float z3 = z2;
    if (z3 <= 1.0f) return {0, 0, 0, z3, false};
    float sx = (x3 / z3) * fov + (screenW / 2.0f); float sy = (y3 / z3) * fov + (screenH / 2.0f);
    float escala = 1000.0f / z3;
    if (escala > 100.0f) escala = 100.0f;
    if (escala < 1.0f) escala = 1.0f;
    return {sx, sy, escala, z3, true};
}

static void LegacyProyectar3D(float x, float y, float z, int cx, int cy, float rotXDeg, float rotYDeg, float zoom,
                              float& outX, float& outY, float& outDepth) {
    const float pi = 3.14159265358979f;
    float radY = rotYDeg * pi / 180.0f;
    float x1 = x * cos(radY) - z * sin(radY);
    float z1 = x * sin(radY) + z * cos(radY);
    float radX = rotXDeg * pi / 180.0f;
    float y2 = y * cos(radX) - z1 * sin(radX);
    float z2 = y * sin(radX) + z1 * cos(radX);
    outX = cx + x1 * zoom; outY = cy + y2 * zoom; outDepth = z2;
}

// --- Pruebas -------------------------------------------------------------------

static void TestMatrixAlgebra() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> d(-50.0, 50.0);
    for (int i = 0; i < 500; ++i) {
        Transform t;
        t.position = {d(rng), d(rng), d(rng)};
        t.rotation = Quatd::FromAxisAngle({d(rng), d(rng), d(rng) + 0.1}, d(rng));
        t.scale = {0.5 + std::fabs(d(rng)), 0.5 + std::fabs(d(rng)), 0.5 + std::fabs(d(rng))};
        const Mat4d m = t.ToMatrix();
        const Mat4d back = InverseAffine(m) * m;
        for (int c = 0; c < 4; ++c)
            for (int f = 0; f < 4; ++f) CHECK(NearD(back.m[c][f], c == f ? 1.0 : 0.0, 1e-9));
    }

    // El cuaternion y la matriz de rotacion deben coincidir.
    const Mat4f a = Quatf::FromAxisAngle({0, 1, 0}, 0.7f).ToMatrix();
    const Mat4f b = RotationY(0.7f);
    for (int c = 0; c < 4; ++c)
        for (int f = 0; f < 4; ++f) CHECK(Near(a.m[c][f], b.m[c][f], 1e-6f));

    // Una direccion ignora la traslacion; un punto no.
    const Mat4f t = Translation(Vec3f{1, 2, 3});
    CHECK(Near(t.TransformDirection({1, 0, 0}).x, 1.0f, 0.0f));
    CHECK(Near(t.TransformPoint({1, 0, 0}).x, 2.0f, 0.0f));
}

static void TestLegacyProjectionGolden() {
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> pos(-1500.0f, 1500.0f), ang(-6.3f, 6.3f);
    Camera cam;
    cam.SetViewport(1920, 1080, 960.0f, 1.0f);

    int compared = 0;
    for (int i = 0; i < 20000; ++i) {
        const float cx = pos(rng), cy = pos(rng), cz = pos(rng);
        const float ox = pos(rng), oy = pos(rng), oz = pos(rng);
        const float yaw = ang(rng), pitch = ang(rng), roll = ang(rng);

        cam.SetPosition({cx, cy, cz});
        cam.SetLegacyAngles(yaw, pitch, roll);
        const ScreenPoint now = cam.WorldToScreen({ox, oy, oz});
        const LegacyPoint old = LegacyCalcularProyeccion(ox, oy, oz, cx, cy, cz, yaw, pitch, roll, 960.0f, 1920.0f, 1080.0f);

        if (std::fabs(old.profundidad - 1.0f) < 0.01f) continue;  // justo en el plano cercano
        CHECK(now.visible == old.visible);
        CHECK(Near(now.depth, old.profundidad, 0.02f));
        if (old.visible && now.visible && old.profundidad > 20.0f) {
            const float tol = 0.02f + 2e-4f * (std::fabs(old.x - 960.0f) + std::fabs(old.y - 540.0f));
            CHECK(Near(now.x, old.x, tol));
            CHECK(Near(now.y, old.y, tol));
            ++compared;
        }
    }
    CHECK(compared > 1000);
    std::printf("  %d proyecciones coinciden con CalcularProyeccion\n", compared);
}

static void TestClipSpaceMatchesLegacy() {
    Camera cam;
    cam.SetViewport(1920, 1080, 960.0f, 1.0f);
    cam.SetPosition({10, 20, -30});
    cam.SetLegacyAngles(0.4f, -0.2f, 0.1f);

    const Vec3d p{50, -10, 300};
    const ScreenPoint s = cam.WorldToScreen(p);
    const Vec3f v = cam.ToViewSpace(p);
    const Vec4f clip = cam.Projection() * Vec4f{v.x, v.y, v.z, 1.0f};
    CHECK(Near((clip.x / clip.w + 1.0f) * 0.5f * 1920.0f, s.x, 0.01f));
    CHECK(Near((1.0f - clip.y / clip.w) * 0.5f * 1080.0f, s.y, 0.01f));
    CHECK(Near(clip.z / clip.w, 1.0f / v.z, 1e-6f));  // Z invertida
}

static void TestLegacySpriteSize() {
    // Diametro original: escala * 4. Mismo resultado con tamano mundial 4000 / focal.
    const float focal = 960.0f;
    const float worldDiameter = 4000.0f / focal;
    for (float z : {1.5f, 5.0f, 10.0f, 80.0f, 400.0f, 1000.0f, 5000.0f}) {
        const LegacyPoint old = LegacyCalcularProyeccion(0, 0, z, 0, 0, 0, 0, 0, 0, focal, 1920, 1080);
        CHECK(Near(PixelSize(worldDiameter, z, focal, 4.0f, 400.0f), old.escala * 4.0f, 0.01f));
    }
}

static void TestTacticalMapGolden() {
    std::mt19937 rng(13);
    std::uniform_real_distribution<float> pos(-2000.0f, 2000.0f), rot(-180.0f, 180.0f), zoom(0.2f, 3.0f);
    for (int i = 0; i < 5000; ++i) {
        const float x = pos(rng), y = pos(rng), z = pos(rng), rx = rot(rng), ry = rot(rng), zm = zoom(rng);
        float ox, oy, oz;
        LegacyProyectar3D(x, y, z, 960, 540, rx, ry, zm, ox, oy, oz);
        const Vec3f n = TacticalMapMatrix(rx, ry, zm, 960.0f, 540.0f).TransformPoint({x, y, z});
        CHECK(Near(n.x, ox, 0.02f));
        CHECK(Near(n.y, oy, 0.02f));
        CHECK(Near(n.z, oz, 0.02f));
    }
}

static void TestCameraConvention() {
    // Con la convencion del motor, girar a la derecha mueve el objeto a la izquierda.
    Camera cam;
    cam.SetViewport(1920, 1080, 960.0f);
    cam.SetYawPitchRoll(0.2f, 0, 0);
    CHECK(cam.WorldToScreen({0, 0, 100}).x < 960.0f);
    cam.SetYawPitchRoll(0, 0.2f, 0);  // mirar abajo sube el objeto en pantalla
    CHECK(cam.WorldToScreen({0, 0, 100}).y < 540.0f);

    // Y la formula original hace lo contrario: esa es la inconsistencia que se corrigio.
    CHECK(LegacyCalcularProyeccion(0, 0, 100, 0, 0, 0, 0.2f, 0, 0, 960, 1920, 1080).x > 960.0f);
    CHECK(LegacyCalcularProyeccion(0, 0, 100, 0, 0, 0, 0, 0.2f, 0, 960, 1920, 1080).y > 540.0f);

    // Un enemigo definido por angulos queda centrado al mirarlo.
    const float enemyYaw = 2.1f, enemyPitch = -0.4f;
    cam.SetYawPitchRoll(enemyYaw, enemyPitch, 0);
    const Vec3f dir = DirectionFromYawPitch(enemyYaw, enemyPitch);
    const ScreenPoint s = cam.WorldToScreen(Vec3d(dir) * 500.0);
    CHECK(Near(s.x, 960.0f, 0.05f));
    CHECK(Near(s.y, 540.0f, 0.05f));
}

static void TestFrustum() {
    Camera cam;
    cam.SetViewport(1920, 1080, 960.0f, 1.0f);
    cam.SetYawPitchRoll(0.9f, -0.3f, 0.2f);
    const Frustum frustum = cam.CameraRelativeFrustum();

    std::mt19937 rng(17);
    std::uniform_real_distribution<float> pos(-500.0f, 500.0f);
    int inside = 0;
    for (int i = 0; i < 20000; ++i) {
        const Vec3f p{pos(rng), pos(rng), pos(rng)};
        const ScreenPoint s = cam.WorldToScreen(Vec3d(p));
        const bool expected = s.visible && s.x >= 0 && s.x <= 1920 && s.y >= 0 && s.y <= 1080;
        const bool border = s.x < 1.0f || s.x > 1919.0f || s.y < 1.0f || s.y > 1079.0f || Near(s.depth, 1.0f, 0.01f);
        if (!(s.visible && border) && !(!s.visible && Near(s.depth, 1.0f, 0.01f))) {
            CHECK(frustum.SphereVisible(p, 0.0f) == expected);
        }
        inside += expected ? 1 : 0;
    }
    CHECK(inside > 100);
}

static void TestLargeWorldPrecision() {
    // A mil millones de unidades del origen, float tiene pasos de 64 unidades.
    const double offset = 1.0e9;
    Camera cam;
    cam.SetViewport(1920, 1080, 960.0f);
    cam.SetPosition({offset, 0, offset});
    const ScreenPoint s = cam.WorldToScreen({offset + 10.25, 0, offset + 100.0});
    CHECK(Near(s.x, 960.0f + 10.25f / 100.0f * 960.0f, 0.001f));

    const float lost = static_cast<float>(offset + 10.25) - static_cast<float>(offset);
    CHECK(lost != 10.25f);
}

static void TestSceneHierarchy() {
    JobSystem jobs(3);
    SceneGraph scene;

    Transform sys;
    sys.position = {100, 0, 0};
    sys.scale = {0.5, 0.5, 0.5};
    const NodeId sector = scene.CreateNode(kNoNode, sys);

    const NodeId planet = scene.CreateNode(sector);
    Transform moonLocal;
    moonLocal.position = {10, 0, 0};
    const NodeId moon = scene.CreateNode(planet, moonLocal);

    OrbitalElements circle;
    circle.semiMajorAxis = 40.0;
    circle.mu = 40.0 * 40.0 * 40.0 * (kPi / 2.0) * (kPi / 2.0);  // pi/2 rad/s
    std::vector<KeplerOrbit> orbits{MakeOrbit(planet, circle)};
    UpdateOrbits(orbits, 1.0, scene, jobs);  // un cuarto de vuelta: planeta en +Z local
    scene.UpdateWorld(jobs);

    const Vec3d m = scene.WorldPosition(moon);
    CHECK(NearD(m.x, 100.0 + 5.0, 1e-9));
    CHECK(NearD(m.y, 0.0, 1e-9));
    CHECK(NearD(m.z, 20.0, 1e-9));
    CHECK(NearD(scene.WorldMaxScale(moon), 0.5, 1e-12));

    bool threw = false;
    try { scene.CreateNode(999); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
}

static void TestJobSystem() {
    JobSystem jobs(7);
    CHECK(jobs.ThreadCount() == 8);

    // Cada elemento se visita exactamente una vez, repetido para detectar carreras.
    for (int round = 0; round < 2000; ++round) {
        const std::size_t n = 1 + static_cast<std::size_t>(round) * 13 % 5000;
        std::vector<std::atomic<int>> visits(n);
        jobs.ParallelFor(n, 16, [&](std::size_t b, std::size_t e, std::size_t chunk) {
            CHECK(b == chunk * 16);
            for (std::size_t i = b; i < e; ++i) visits[i].fetch_add(1, std::memory_order_relaxed);
        });
        bool ok = true;
        for (auto& v : visits) ok = ok && v.load() == 1;
        CHECK(ok);
    }

    // Vacio, anidado y excepciones.
    jobs.ParallelFor(0, 1, [](std::size_t, std::size_t, std::size_t) { std::abort(); });

    std::atomic<int> inner{0};
    jobs.ParallelFor(64, 1, [&](std::size_t, std::size_t, std::size_t) {
        jobs.ParallelFor(8, 1, [&](std::size_t, std::size_t, std::size_t) { inner.fetch_add(1); });
    });
    CHECK(inner.load() == 64 * 8);

    bool threw = false;
    try {
        jobs.ParallelFor(100, 1, [](std::size_t b, std::size_t, std::size_t) {
            if (b == 50) throw std::runtime_error("prueba");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // Sigue funcionando despues de una excepcion, y sin hilos auxiliares.
    std::atomic<int> count{0};
    jobs.ParallelFor(100, 1, [&](std::size_t, std::size_t, std::size_t) { count.fetch_add(1); });
    CHECK(count.load() == 100);

    JobSystem single(0);
    int serial = 0;
    single.ParallelFor(100, 7, [&](std::size_t b, std::size_t e, std::size_t) { serial += static_cast<int>(e - b); });
    CHECK(serial == 100);
}

struct TornCheck { std::uint64_t a = 0; char padding[4096]{}; std::uint64_t b = 0; };

static void TestTripleBuffer() {
    TripleBuffer<TornCheck> buffer;
    const std::uint64_t total = 2'000'000;
    std::atomic<bool> finished{false};
    std::atomic<std::uint64_t> torn{0}, backwards{0}, reads{0};

    std::thread consumer([&] {
        std::uint64_t last = 0;
        for (;;) {
            const bool done = finished.load(std::memory_order_acquire);
            if (buffer.AcquireLatest()) {
                const TornCheck& t = buffer.ReadSlot();
                if (t.a != t.b) torn.fetch_add(1);
                if (t.a < last) backwards.fetch_add(1);
                last = t.a;
                reads.fetch_add(1);
            } else if (done) {
                break;
            }
        }
        if (last != total) backwards.fetch_add(1);  // debe llegar la ultima publicacion
    });

    for (std::uint64_t i = 1; i <= total; ++i) {
        TornCheck& t = buffer.WriteSlot();
        t.a = i;
        t.b = i;
        buffer.Publish();
    }
    finished.store(true, std::memory_order_release);
    consumer.join();

    CHECK(torn.load() == 0);
    CHECK(backwards.load() == 0);
    CHECK(reads.load() > 0);
}

static void BuildTestScene(SceneGraph& scene, std::vector<Renderable>& renderables, std::vector<LinkRenderable>& links, JobSystem& jobs) {
    std::mt19937 rng(23);
    std::uniform_real_distribution<double> pos(-20000.0, 20000.0);
    for (int i = 0; i < 60000; ++i) {
        Transform t;
        t.position = {pos(rng), pos(rng), pos(rng)};
        const NodeId n = scene.CreateNode(kNoNode, t);
        Renderable r;
        r.node = n;
        r.radius = 3.0f;
        r.texture = static_cast<TextureId>(i % 3);
        r.blend = i % 5 == 0 ? BlendMode::Additive : BlendMode::Alpha;
        renderables.push_back(r);
        if (i > 0) links.push_back({n - 1, n, Color{}});
    }
    scene.UpdateWorld(jobs);
}

static void TestFrameBuilderDeterminism() {
    JobSystem one(0), many(7);
    SceneGraph scene;
    std::vector<Renderable> renderables;
    std::vector<LinkRenderable> links;
    BuildTestScene(scene, renderables, links, one);

    FrameInput input;
    input.scene = &scene;
    input.renderables = &renderables;
    input.links = &links;
    input.camera.SetViewport(1920, 1080, 960.0f);
    input.camera.SetYawPitchRoll(0.3f, 0.1f, 0.0f);

    FrameBuilder a, b;
    FramePacket pa, pb;
    a.Build(input, one, pa);
    b.Build(input, many, pb);

    CHECK(pa.sprites.size() > 1000);
    CHECK(pa.sprites.size() == pb.sprites.size());
    CHECK(pa.lines.size() == pb.lines.size());
    CHECK(pa.batches.size() == pb.batches.size());

    bool sameSprites = pa.sprites.size() == pb.sprites.size();
    for (std::size_t i = 0; sameSprites && i < pa.sprites.size(); ++i) {
        sameSprites = pa.sprites[i].id == pb.sprites[i].id && pa.sprites[i].viewPosition.z == pb.sprites[i].viewPosition.z &&
                      pa.sprites[i].pixelRadius == pb.sprites[i].pixelRadius;
    }
    CHECK(sameSprites);

    bool sorted = true, lod = true;
    for (std::size_t i = 1; i < pb.sprites.size(); ++i) sorted = sorted && pb.sprites[i - 1].viewPosition.z >= pb.sprites[i].viewPosition.z;
    for (const SpriteInstance& s : pb.sprites) lod = lod && s.pixelRadius >= 2.0f && s.pixelRadius <= 200.0f && s.viewPosition.z > 1.0f;
    CHECK(sorted);
    CHECK(lod);

    std::size_t batched = 0;
    for (const SpriteBatch& batch : pb.batches) {
        CHECK(batch.first == batched);
        batched += batch.count;
    }
    CHECK(batched == pb.sprites.size());

    // Reutilizar el paquete con una camara que no ve nada no deja restos.
    input.camera.SetPosition({1.0e7, 1.0e7, 1.0e7});
    many.ParallelFor(1, 1, [](std::size_t, std::size_t, std::size_t) {});
    b.Build(input, many, pb);
    CHECK(pb.batches.size() <= pb.sprites.size() + 1);
}

static void TestSphereLod() {
    JobSystem one(0), many(5);
    SceneGraph scene;
    const NodeId sun = scene.CreateNode(kNoNode);
    Transform t;
    t.position = {0, 0, 10000};
    const NodeId planet = scene.CreateNode(kNoNode, t);
    scene.UpdateWorld(one);

    std::vector<Renderable> renderables(1);
    renderables[0].node = planet;
    renderables[0].radius = 100.0f;
    renderables[0].surface = 0;
    renderables[0].lightNode = sun;
    renderables[0].spriteScale = 1.25f;
    renderables[0].spinRate = 0.5f;

    FrameInput input;
    input.scene = &scene;
    input.renderables = &renderables;
    input.surfaces = std::make_shared<std::vector<SurfaceLook>>(1);
    input.camera.SetViewport(1920, 1080, 960.0f);

    FrameBuilder builder;
    FramePacket packet;

    // A 100 km de radio y 1e6 de distancia ocupa menos de 6 pixeles: se dibuja el sprite.
    input.camera.SetPosition({0, 0, -990000});
    builder.Build(input, one, packet);
    CHECK(packet.spheres.empty());
    CHECK(packet.sprites.size() == 1);
    CHECK(Near(packet.sprites[0].pixelRadius, std::max(125.0f * 960.0f / 1000000.0f, 2.0f), 1e-3f));

    // Cerca es una esfera, sin limite de tamano en pantalla.
    input.camera.SetPosition({0, 0, 9850});
    builder.Build(input, one, packet);
    CHECK(packet.sprites.empty());
    CHECK(packet.spheres.size() == 1);
    CHECK(packet.spheres[0].pixelRadius > 800.0f);
    CHECK(packet.spheres[0].detail >= 9.0f);
    CHECK(Near(packet.spheres[0].lightDirection.z, -1.0f, 1e-5f));  // el sol esta detras de la camara

    // Silueta: desde 2 radios, sin(a) = 1/2 y tan(a) = 1 / sqrt(3).
    CHECK(Near(SilhouettePixels(100.0f, 200.0f, 960.0f), 960.0f / std::sqrt(3.0f), 1e-2f));

    // El giro cambia la orientacion, pero es funcion del tiempo y se repite tras una vuelta.
    const Quatf atZero = packet.spheres[0].viewToBody;
    input.simTime = 2.0;
    builder.Build(input, one, packet);
    const Quatf atTwo = packet.spheres[0].viewToBody;
    CHECK(std::abs(atZero.y - atTwo.y) > 0.1f);
    input.simTime = 2.0 * kPi / 0.5;
    builder.Build(input, one, packet);
    CHECK(Near(std::abs(packet.spheres[0].viewToBody.w), std::abs(atZero.w), 1e-4f));

    // Con el centro detras de la camara, la superficie todavia llena la pantalla.
    input.simTime = 0;
    input.camera.SetPosition({0, -102, 9990});
    builder.Build(input, one, packet);
    CHECK(packet.spheres[0].viewCenter.z > 0.0f);
    input.camera.SetPosition({0, -102, 10010});
    builder.Build(input, one, packet);
    CHECK(packet.spheres.size() == 1);
    CHECK(packet.spheres[0].viewCenter.z < 0.0f);

    // Dentro del cuerpo o sin tabla de superficies no se genera ninguna esfera.
    input.camera.SetPosition({0, 0, 9950});
    builder.Build(input, one, packet);
    CHECK(packet.spheres.empty());
    input.camera.SetPosition({0, 0, 9850});
    input.surfaces = nullptr;
    builder.Build(input, one, packet);
    CHECK(packet.spheres.empty());
    CHECK(packet.sprites.size() == 1);

    // El detalle crece con el tamano y respeta sus limites.
    const LodSettings lod;
    CHECK(SurfaceDetail(6.0f, lod) >= 1.0f);
    CHECK(SurfaceDetail(100.0f, lod) > SurfaceDetail(20.0f, lod));
    CHECK(SurfaceDetail(1.0e6f, lod) == lod.maxOctaves);

    // Mismo resultado con cualquier numero de hilos.
    std::vector<Renderable> many_bodies;
    SceneGraph crowd;
    for (int i = 0; i < 9000; ++i) {
        Transform p;
        p.position = {static_cast<double>(i % 30) * 500.0, static_cast<double>(i / 30 % 30) * 500.0, 4000.0 + i / 900 * 500.0};
        Renderable r = renderables[0];
        r.node = crowd.CreateNode(kNoNode, p);
        r.lightNode = r.node;
        many_bodies.push_back(r);
    }
    crowd.UpdateWorld(one);
    FrameInput inputMany = input;
    inputMany.scene = &crowd;
    inputMany.renderables = &many_bodies;
    inputMany.surfaces = std::make_shared<std::vector<SurfaceLook>>(1);
    inputMany.camera.SetPosition({0, 0, 0});
    FramePacket pa, pb;
    FrameBuilder a, b;
    a.Build(inputMany, one, pa);
    b.Build(inputMany, many, pb);
    CHECK(pa.spheres.size() > 100);
    CHECK(pa.spheres.size() == pb.spheres.size());
    bool same = pa.spheres.size() == pb.spheres.size(), sorted = true;
    for (std::size_t i = 0; same && i < pa.spheres.size(); ++i) same = pa.spheres[i].id == pb.spheres[i].id;
    for (std::size_t i = 1; i < pb.spheres.size(); ++i) sorted = sorted && pb.spheres[i - 1].distance >= pb.spheres[i].distance;
    CHECK(same);
    CHECK(sorted);
}

static void TestKeplerSolver() {
    bool residual = true, inRange = true;
    for (double e = 0.0; e < 0.995; e += 0.03) {
        for (double m = -3.14; m <= 3.14; m += 0.01) {
            const double E = SolveKepler(m, e);
            residual = residual && NearD(E - e * std::sin(E), m, 1e-11);
            inRange = inRange && std::abs(E) <= kPi + 1e-9;
        }
    }
    CHECK(residual);
    CHECK(inRange);
    CHECK(NearD(SolveKepler(0.0, 0.5), 0.0, 1e-12));
    CHECK(NearD(std::abs(SolveKepler(kPi, 0.97)), kPi, 1e-9));
    CHECK(NearD(SolveKepler(7.0 * kPi, 0.0), kPi, 1e-9) || NearD(SolveKepler(7.0 * kPi, 0.0), -kPi, 1e-9));
}

static void TestKeplerMotion() {
    using namespace astro;
    const double muSun = kGravity * kSolarMass;

    // Tercera ley de Kepler contra la hoja de datos de la NASA: millones de km y dias.
    // El margen cubre el redondeo de la tabla; Neptuno es el peor caso, con un 1,3 %.
    const struct { double distance, days; } planets[] = {
        {57.9, 88.0}, {108.2, 224.7}, {149.6, 365.2}, {228.0, 687.0}, {778.5, 4331}, {1432.0, 10747}, {2867.0, 30589}, {4515.0, 59800}};
    for (const auto& p : planets) {
        OrbitalElements e;
        e.semiMajorAxis = p.distance * 1.0e6;
        e.mu = muSun;
        CHECK(std::abs(OrbitPeriod(e) / kDay / p.days - 1.0) < 0.015);
    }

    OrbitalElements e;
    e.semiMajorAxis = 2.0 * kAstronomicalUnit;
    e.eccentricity = 0.6;
    e.inclination = 0.4;
    e.ascendingNode = 1.1;
    e.argPeriapsis = 2.3;
    e.meanAnomaly0 = 0.7;
    e.mu = muSun;
    const KeplerOrbit o = MakeOrbit(0, e);
    const double T = OrbitPeriod(e);

    CHECK(NearD(Length(o.p), 1.0, 1e-12));
    CHECK(NearD(Length(o.q), 1.0, 1e-12));
    CHECK(NearD(Dot(o.p, o.q), 0.0, 1e-12));
    CHECK(NearD(Length(OrbitPositionFromEccentric(o, 0.0)), Periapsis(e), 1.0));
    CHECK(NearD(Length(OrbitPositionFromEccentric(o, kPi)), Apoapsis(e), 1.0));

    // Energia (vis-viva), momento angular constante y velocidad igual a la derivada.
    const Vec3d angular = Cross(OrbitPosition(o, 0.0), OrbitVelocity(o, 0.0));
    bool visViva = true, area = true, derivative = true;
    for (int i = 0; i < 400; ++i) {
        const double t = T * i / 137.0;
        const Vec3d r = OrbitPosition(o, t), v = OrbitVelocity(o, t);
        visViva = visViva && NearD(Dot(v, v) / (e.mu * (2.0 / Length(r) - 1.0 / e.semiMajorAxis)), 1.0, 1e-10);
        area = area && Length(Cross(r, v) - angular) / Length(angular) < 1e-10;
        const double h = 10.0;
        const Vec3d numeric = (OrbitPosition(o, t + h) - OrbitPosition(o, t - h)) * (0.5 / h);
        derivative = derivative && Length(numeric - v) / Length(v) < 1e-6;
    }
    CHECK(visViva);
    CHECK(area);
    CHECK(derivative);

    // Verificacion independiente: integrar la gravedad con RK4 da la misma trayectoria.
    Vec3d r = OrbitPosition(o, 0.0), v = OrbitVelocity(o, 0.0);
    const auto gravity = [&](const Vec3d& p) { const double d = Length(p); return p * (-e.mu / (d * d * d)); };
    const int steps = 40000;
    const double h = T / steps;
    for (int i = 0; i < steps; ++i) {
        const Vec3d k1r = v, k1v = gravity(r);
        const Vec3d k2r = v + k1v * (h / 2), k2v = gravity(r + k1r * (h / 2));
        const Vec3d k3r = v + k2v * (h / 2), k3v = gravity(r + k2r * (h / 2));
        const Vec3d k4r = v + k3v * h, k4v = gravity(r + k3r * h);
        r += (k1r + k2r * 2.0 + k3r * 2.0 + k4r) * (h / 6);
        v += (k1v + k2v * 2.0 + k3v * 2.0 + k4v) * (h / 6);
    }
    CHECK(Length(r - OrbitPosition(o, T)) / e.semiMajorAxis < 1e-5);

    // Tras mil millones de anos simulados sigue siendo periodica: no hay error acumulado.
    const double late = 3.0e16;
    CHECK(Length(OrbitPosition(o, late + T) - OrbitPosition(o, late)) / e.semiMajorAxis < 1e-3);

    // Sentido directo: de +X hacia +Z, con el norte en -Y.
    OrbitalElements flat;
    flat.semiMajorAxis = 1000.0;
    flat.mu = 1.0e9;
    const KeplerOrbit f = MakeOrbit(0, flat);
    CHECK(NearD(OrbitPosition(f, 0.0).x, 1000.0, 1e-6));
    CHECK(OrbitPosition(f, OrbitPeriod(flat) * 0.25).z > 999.0);
    CHECK(Normalize(Cross(f.p, f.q)).y < -0.999);

    bool threw = false;
    OrbitalElements bad;
    bad.eccentricity = 1.0;
    try { MakeOrbit(0, bad); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // El informe contiene la ecuacion con sus numeros.
    CHECK(OrbitEquation(o).find("sin(E)") != std::string::npos);
    CHECK(SpinEquation({}).find("giro(t)") != std::string::npos);
}

static void TestSpinModel() {
    using namespace astro;
    // El eje: -Y del cuerpo termina en el eje pedido, incluso en los casos limite.
    for (const Vec3d axis : {Vec3d{0, -1, 0}, Vec3d{0, 1, 0}, Vec3d{1, 0, 0}, Normalize(Vec3d{0.3, -0.8, 0.5})}) {
        const Vec3d mapped = SpinAxisRotation(axis).ToMatrix().TransformDirection(Vec3d{0, -1, 0});
        CHECK(Length(mapped - axis) < 1e-5);
    }

    // Giro directo: un punto del ecuador avanza igual que la orbita, de +X hacia +Z.
    Renderable r;
    r.spinRate = 1.0;
    const Vec3f quarter = BodyOrientation(r, kPi / 2.0).ToMatrix().TransformDirection(Vec3f{1, 0, 0});
    CHECK(Near(quarter.z, 1.0f, 1e-5f));

    // Una luna acoplada ensena siempre la misma cara a su planeta.
    std::mt19937 rng(5);
    GeneratorOptions options;
    options.planetsMin = options.planetsMax = 8;
    int locked = 0;
    for (int s = 0; s < 40 && locked < 40; ++s) {
        StarSystemData system = GenerateSystem(StarClass::G, rng, options);
        for (BodyData& b : system.bodies) {
            if (b.kind != PlanetClass::Moon) continue;
            b.orbit.eccentricity = 0;  // sin libracion optica
            b.orbit.inclination = 0;
            b.spin.phase = b.orbit.ascendingNode + b.orbit.argPeriapsis + b.orbit.meanAnomaly0;
            const KeplerOrbit orbit = MakeOrbit(0, b.orbit);
            Renderable moon;
            moon.spinRate = b.spin.rate;
            moon.spinPhase = b.spin.phase;
            bool same = true;
            for (double fraction = 0.0; fraction < 1.0; fraction += 0.13) {
                const double t = OrbitPeriod(b.orbit) * fraction;
                const Vec3f face = BodyOrientation(moon, t).ToMatrix().TransformDirection(Vec3f{1, 0, 0});
                same = same && Dot(Vec3d(face), Normalize(OrbitPosition(orbit, t))) > 0.999;
            }
            CHECK(same);
            ++locked;
        }
    }
    CHECK(locked > 0);
}

static void TestAstroFormulas() {
    using namespace astro;
    std::mt19937 rng(11);

    // Distribuciones: la media medida coincide con la teorica.
    double beta = 0, rayleigh = 0, normal = 0, normal2 = 0;
    const int samples = 200000;
    for (int i = 0; i < samples; ++i) {
        beta += Beta(rng, 0.867, 3.03);
        rayleigh += Rayleigh(rng, 2.0);
        const double n = Normal(rng);
        normal += n;
        normal2 += n * n;
    }
    CHECK(NearD(beta / samples, 0.867 / (0.867 + 3.03), 0.002));
    CHECK(NearD(rayleigh / samples, 2.0 * std::sqrt(kPi / 2.0), 0.01));
    CHECK(NearD(normal / samples, 0.0, 0.01));
    CHECK(NearD(normal2 / samples, 1.0, 0.02));

    // Contra valores conocidos.
    CHECK(NearD(Luminosity(1.0, kSolarTemperature), 1.0, 1e-12));
    CHECK(NearD(EquilibriumTemperature(1.0, kAstronomicalUnit), 254.7, 1.0));          // Tierra
    CHECK(NearD(HillRadius(kAstronomicalUnit, 0.0167, kEarthMass, kSolarMass) / 1.0e6, 1.47, 0.05));
    CHECK(NearD(RadiusFromMass(1.0), 1.0, 1e-12));
    CHECK(NearD(RadiusFromMass(0.107) * kEarthRadius, 3390.0, 300.0));                 // Marte
    CHECK(NearD(RadiusFromMass(14.5) * kEarthRadius, 25360.0, 2500.0));                // Urano
    CHECK(NearD(RocheLimit(1.0, 2.0, 1.0), std::cbrt(4.0), 1e-12));

    const Vec3f sun = BlackbodyColor(5772.0), blue = BlackbodyColor(30000.0), red = BlackbodyColor(3000.0);
    CHECK(sun.x > 0.95f && sun.y > 0.85f && sun.z > 0.8f);
    CHECK(blue.z > blue.x);
    CHECK(red.x > red.z * 1.5f);

    // Cada clase respeta su tabla, con un margen por la variacion aleatoria.
    double previousMass = 1.0e9;
    for (int c = 0; c < static_cast<int>(StarClass::Count); ++c) {
        const StarClass kind = static_cast<StarClass>(c);
        const StarClassInfo& info = Info(kind);
        const double rLow = std::min(info.radiusMin, info.radiusMax) * 0.85, rHigh = std::max(info.radiusMin, info.radiusMax) * 1.17;
        bool inside = true;
        for (int i = 0; i < 2000; ++i) {
            const StarData s = MakeStar(kind, rng);
            const double r = s.radius / kSolarRadius;
            inside = inside && r >= rLow && r <= rHigh && s.mass / kSolarMass >= info.massMin * 0.999 && s.mass / kSolarMass <= info.massMax * 1.001;
            if (kind == StarClass::BlackHole) continue;  // solo brilla su disco: no sigue la ley de una estrella
            inside = inside && s.temperature >= info.tempMin * 0.94 && s.temperature <= info.tempMax * 1.06 && s.luminosity > 0.0 && s.spinRate > 0.0;
            inside = inside && NearD(s.luminosity / Luminosity(r, s.temperature), 1.0, 1e-9);
        }
        CHECK(inside);
        if (c <= static_cast<int>(StarClass::M)) {
            CHECK(info.massMax <= previousMass);  // de O a M, de mas a menos masa
            previousMass = info.massMax;
        }
    }

    // Una G se parece al Sol y sigue, a grandes rasgos, L = M^3,5.
    double luminosity = 0;
    for (int i = 0; i < 4000; ++i) luminosity += MakeStar(StarClass::G, rng).luminosity;
    CHECK(luminosity / 4000.0 > 0.5 && luminosity / 4000.0 < 1.6);

    // Frecuencias reales: tres de cada cuatro estrellas de la secuencia principal son enanas rojas.
    int redDwarfs = 0, mainSequence = 0;
    for (int i = 0; i < 40000; ++i) {
        const StarClass c = RandomStarClass(rng, true);
        redDwarfs += c == StarClass::M;
        mainSequence += c <= StarClass::M;
    }
    CHECK(NearD(static_cast<double>(redDwarfs) / mainSequence, 0.76, 0.01));
}

static void TestSystemGenerator() {
    using namespace astro;
    std::mt19937 rng(2026);
    int systems = 0, planets = 0, moons = 0, comets = 0, habitable = 0, locked = 0, eccentric = 0;
    std::string firstProblem;
    bool sane = true;
    for (int c = 0; c < static_cast<int>(StarClass::Count); ++c) {
        for (int i = 0; i < 1500; ++i) {
            const StarSystemData system = GenerateSystem(static_cast<StarClass>(c), rng);
            ++systems;
            const std::string problem = CheckStability(system);
            if (!problem.empty() && firstProblem.empty()) firstProblem = std::string(Info(static_cast<StarClass>(c)).name) + ": " + problem;
            for (const BodyData& b : system.bodies) {
                sane = sane && std::isfinite(b.orbit.semiMajorAxis) && std::isfinite(b.spin.rate) && b.spin.rate > 0.0 && NearD(Length(b.spin.axis), 1.0, 1e-9);
                if (b.kind == PlanetClass::Moon) ++moons;
                else if (b.kind == PlanetClass::Comet) ++comets;
                else {
                    ++planets;
                    habitable += b.kind == PlanetClass::Terran;
                    locked += b.tidallyLocked;
                    eccentric += b.orbit.eccentricity > 0.02;
                }
            }
        }
    }
    if (!firstProblem.empty()) std::printf("  Primer problema: %s\n", firstProblem.c_str());
    CHECK(firstProblem.empty());
    CHECK(sane);
    CHECK(planets > systems);
    CHECK(moons > 0);
    CHECK(comets > 0);
    CHECK(habitable > 0);
    CHECK(locked > 0);
    CHECK(eccentric > planets / 3);

    // Misma semilla, mismo sistema.
    std::mt19937 a(7), b(7);
    const StarSystemData sa = GenerateSystem(a), sb = GenerateSystem(b);
    CHECK(sa.bodies.size() == sb.bodies.size());
    CHECK(DescribeSystem(sa, "A") == DescribeSystem(sb, "A"));
    CHECK(DescribeSystem(sa, "A").find("giro(t)") != std::string::npos || sa.bodies.empty());

    // Los nombres de las lunas dependen de su planeta.
    StarSystemData named;
    named.bodies.resize(3);
    named.bodies[1].parent = 0;
    named.bodies[2].parent = 0;
    CHECK(BodyName(named, 0, "S") == "S b");
    CHECK(BodyName(named, 2, "S") == "S b II");

    // A 50 anos luz del origen, el paso minimo de double sigue siendo menor de 100 metros.
    const double galaxy = 50.0 * kLightYear;
    CHECK(std::nextafter(galaxy, 2.0 * galaxy) - galaxy < 0.1);
}

static void TestOrbitPaths() {
    using namespace astro;
    JobSystem one(0), many(5);
    SceneGraph scene;
    const NodeId sun = scene.CreateNode(kNoNode);
    const NodeId planet = scene.CreateNode(sun);

    OrbitalElements e;
    e.semiMajorAxis = kAstronomicalUnit;
    e.eccentricity = 0.3;
    e.inclination = 0.2;
    e.mu = kGravity * kSolarMass;
    std::vector<KeplerOrbit> orbits{MakeOrbit(planet, e)};
    World orbitWorld;
    orbitWorld.orbits = orbits;
    orbitWorld.orbitPaths = {{0, sun, Color{1, 1, 1, 1}, true}};
    OrbitLayer orbitLayer(orbitWorld);

    FrameInput input;
    input.scene = &scene;
    std::vector<Renderable> nothing;
    input.renderables = &nothing;
    input.layers = {&orbitLayer};
    input.simTime = 1.0e7;
    input.camera.SetViewport(1600, 900, 800.0f, 1.0f);
    UpdateOrbits(orbits, input.simTime, scene, one);
    scene.UpdateWorld(one);

    // La linea pasa exactamente por el cuerpo, aunque la camara este pegada a el.
    const Vec3d body = scene.WorldPosition(planet);
    input.camera.SetPosition(body + Vec3d{0, -20000, -30000});
    FrameBuilder builder;
    FramePacket packet, other;
    builder.Build(input, one, packet);
    CHECK(packet.lines.size() > 20);  // el resto de la elipse queda detras de la camara
    float closest = 1.0e30f;
    const Vec3f target = input.camera.ToViewSpace(body);
    for (const LineInstance& l : packet.lines) closest = std::min({closest, Length(l.viewA - target), Length(l.viewB - target)});
    CHECK(closest < 1.0f);

    FrameBuilder second;
    second.Build(input, many, other);
    CHECK(other.lines.size() == packet.lines.size());

    // Desde otra estrella, la orbita es menor que un pixel y no se dibuja.
    input.camera.SetPosition({0, 0, -4.0 * kLightYear});
    builder.Build(input, one, packet);
    CHECK(packet.lines.empty());

    // Un planeta con sprite minimo se apaga con la distancia en vez de amontonarse.
    Renderable dot;
    dot.node = planet;
    dot.radius = 6371.0f;
    dot.minPixelRadius = 2.0f;
    dot.fadeDistance = 1.0e9f;
    std::vector<Renderable> dots{dot};
    input.renderables = &dots;
    input.camera.SetPosition(body + Vec3d{0, 0, -5.0e8});
    input.camera.SetYawPitchRoll(0, 0, 0);
    builder.Build(input, one, packet);
    CHECK(packet.sprites.size() == 1);
    CHECK(Near(packet.sprites[0].tint.a, 1.0f, 1e-6f));
    input.camera.SetPosition(body + Vec3d{0, 0, -1.5e9});
    builder.Build(input, one, packet);
    CHECK(Near(packet.sprites[0].tint.a, 0.5f, 1e-3f));
    input.camera.SetPosition(body + Vec3d{0, 0, -2.5e9});
    builder.Build(input, one, packet);
    CHECK(packet.sprites.empty());
}

static void TestMaterials() {
    using namespace astro;
    Materials earth;
    earth.iron = 0.32;
    earth.silicate = 0.68;
    CHECK(NearD(Density(earth) / 1.0e12, 4.05, 0.05));

    Materials iron = earth, wet = earth;
    iron.iron = 0.7;
    iron.silicate = 0.3;
    wet.water = 0.4;
    wet.silicate = 0.28;
    CHECK(Density(iron) > Density(earth));
    CHECK(Density(wet) < Density(earth));

    CHECK(NearD(SurfaceTemperature(255.0, 1.0), 288.0, 2.0));   // Tierra
    CHECK(NearD(SurfaceTemperature(255.0, 0.0), 255.0, 1e-9));

    // Achatamiento real, de la NASA: Tierra 0,34 %, Jupiter 6,5 %, Saturno 9,8 %.
    const double hour = 2.0 * kPi / kHour;
    CHECK(std::abs(Flattening(kEarthMass, 6371.0, hour / 23.93, false) / 0.00335 - 1.0) < 0.2);
    CHECK(std::abs(Flattening(1.898e27, 69911.0, hour / 9.93, true) / 0.0649 - 1.0) < 0.25);
    CHECK(std::abs(Flattening(5.68e26, 58232.0, hour / 10.66, true) / 0.098 - 1.0) < 0.25);

    // Lo generado cumple las reglas fisicas y depende del lugar.
    std::mt19937 rng(99);
    GeneratorOptions options;
    options.planetsMin = options.planetsMax = 8;
    bool sums = true, terran = true, gas = true, ice = true, flat = true;
    double innerIron = 0, outerIron = 0;
    int inner = 0, outer = 0, terrans = 0;
    for (int i = 0; i < 400; ++i) {
        const StarSystemData system = GenerateSystem(StarClass::G, rng, options);
        for (const BodyData& b : system.bodies) {
            const Materials& m = b.materials;
            const double total = m.iron + m.silicate + m.water + m.ices + m.gas;
            if (b.kind != PlanetClass::Comet) sums = sums && NearD(total, 1.0, 1e-9);
            const Appearance look = DeriveAppearance(b);
            flat = flat && look.flattening >= 0.0f && look.flattening <= 0.2f;
            if (b.kind == PlanetClass::Terran) {
                ++terrans;
                terran = terran && m.ocean > 0.0 && m.surfaceTemperature > 273.0 && m.surfaceTemperature < 373.0 && look.kind == SurfaceKind::Terrestrial;
            }
            if (b.kind == PlanetClass::GasGiant) gas = gas && m.gas > 0.5 && look.kind == SurfaceKind::Gas;
            if (b.kind == PlanetClass::Ice) ice = ice && m.iceCap == 1.0 && m.water > 0.2;
            if (b.parent < 0 && b.kind != PlanetClass::Comet && m.gas == 0.0 && m.ices == 0.0) {
                const double x = b.orbit.semiMajorAxis / system.star.snowLine;
                if (x < 0.15) { innerIron += m.iron; ++inner; }
                else if (x > 0.5) { outerIron += m.iron; ++outer; }
            }
        }
    }
    CHECK(sums);
    CHECK(terrans > 0);
    CHECK(terran);
    CHECK(gas);
    CHECK(ice);
    CHECK(flat);
    CHECK(inner > 0 && outer > 0);
    CHECK(innerIron / inner > outerIron / outer + 0.2);  // el hierro delata la cercania a la estrella

    // Un gigante cambia de color con la temperatura, como en las clases de Sudarsky.
    BodyData giant;
    giant.kind = PlanetClass::GasGiant;
    giant.mass = 1.0e27;
    giant.radius = 7.0e4;
    giant.spin.rate = 1.0e-4;
    giant.temperature = 120.0;
    const Appearance cold = DeriveAppearance(giant);
    giant.temperature = 600.0;
    const Appearance warm = DeriveAppearance(giant);
    CHECK(cold.b.x > cold.b.z);
    CHECK(warm.b.z > warm.b.x);
}

static void TestHud() {
    JobSystem one(0);
    GalaxyOptions options;
    options.systems = 6;
    World w = BuildGalaxy(options);
    UpdateOrbits(w.orbits, 0.0, w.scene, one);
    w.scene.UpdateWorld(one);

    // Las referencias entre cuerpos son coherentes.
    bool links = true;
    for (const Body& b : w.bodies) {
        if (!Alive(b)) continue;
        const bool belt = !b.isStar && b.kind == astro::PlanetClass::Belt;
        links = links && (belt ? b.renderable < 0 && b.belt >= 0 : b.renderable >= 0 && w.renderables[static_cast<std::size_t>(b.renderable)].node == b.node);
        if (!b.isStar) {
            links = links && (belt || w.orbits[static_cast<std::size_t>(b.orbit)].node == b.node);
            links = links && w.bodies[static_cast<std::size_t>(b.parent)].system == b.system;
            if (b.kind == astro::PlanetClass::Moon) links = links && !w.bodies[static_cast<std::size_t>(b.parent)].isStar;
            else if (b.kind == astro::PlanetClass::Asteroid) links = links && w.bodies[static_cast<std::size_t>(b.parent)].kind == astro::PlanetClass::Belt;
            else links = links && w.bodies[static_cast<std::size_t>(b.parent)].isStar;
        }
    }
    CHECK(links);

    Camera cam;
    cam.SetViewport(1600, 900, 800.0f, 1.0f);
    const Vec3d star = w.scene.WorldPosition(w.sectors[0]);
    const double extent = w.extents[0];

    // Dentro del sistema se ven sus planetas.
    cam.SetPosition(star + Vec3d{0, -extent * 0.6, -extent * 0.8});
    cam.SetYawPitchRoll(0.0f, 0.6f, 0.0f);
    HudFrame hud;
    BuildHud(w, cam, -1, hud);
    CHECK(hud.inside);
    CHECK(hud.system == 0);
    bool anyPlanet = false, onlyThisSystem = true, onScreen = true;
    for (const HudLabel& l : hud.labels) {
        const Body& b = w.bodies[static_cast<std::size_t>(l.body)];
        anyPlanet = anyPlanet || !b.isStar;
        onlyThisSystem = onlyThisSystem && b.system == 0;
        onScreen = onScreen && l.x >= 0 && l.y >= 0 && l.x <= 1600 && l.y <= 900;
    }
    CHECK(anyPlanet);
    CHECK(onlyThisSystem);
    CHECK(onScreen);
    CHECK(hud.list.size() > 2);

    // Fuera, solo estrellas.
    cam.SetPosition(star + Vec3d{0, 0, -astro::kLightYear * 0.5});
    BuildHud(w, cam, -1, hud);
    CHECK(!hud.inside);
    bool onlyStars = !hud.list.empty();
    for (const HudLabel& l : hud.labels) onlyStars = onlyStars && w.bodies[static_cast<std::size_t>(l.body)].isStar;
    for (const HudListEntry& e : hud.list) onlyStars = onlyStars && w.bodies[static_cast<std::size_t>(e.body)].isStar;
    CHECK(onlyStars);
    CHECK(hud.labels.size() >= 1);

    // El clic toma lo mas cercano al punto de mira.
    HudFrame fake;
    fake.width = 1600;
    fake.height = 900;
    fake.labels = {{4, 900, 450, 1, 1}, {7, 805, 455, 1, 1}, {9, 100, 100, 1, 1}};
    CHECK(PickHudBody(fake) == 7);
    fake.labels = {{9, 100, 100, 1, 1}};
    CHECK(PickHudBody(fake) == -1);

    // La lista da la vuelta en los dos sentidos.
    fake.list = {{3, 0}, {5, 0}, {8, 0}};
    fake.selected = 8;
    CHECK(StepHudSelection(fake, 1) == 3);
    fake.selected = 3;
    CHECK(StepHudSelection(fake, -1) == 8);
    fake.selected = -1;
    CHECK(StepHudSelection(fake, 1) == 3);

    // Un cuerpo fuera de pantalla da una flecha en el borde.
    const int body = w.state[0].bodies[0];
    cam.SetPosition(w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(body)].node) + Vec3d{0, 0, 1.0e6});
    cam.SetYawPitchRoll(0.0f, 0.0f, 0.0f);
    BuildHud(w, cam, body, hud);
    CHECK(!hud.selectedOnScreen);
    CHECK(hud.selectedX >= 0 && hud.selectedX <= 1600 && hud.selectedY >= 0 && hud.selectedY <= 900);
    CHECK(hud.selectedDistance > 9.0e5);
    CHECK(hud.selectedRange > 0.0);

    // La interfaz cabe en ventanas pequenas y grandes.
    for (const auto& size : {std::pair{1280, 720}, std::pair{1600, 900}, std::pair{2560, 1440}}) {
        const HudLayout l = ComputeHudLayout(size.first, size.second);
        CHECK(l.panelX >= 0.0f);
        CHECK(l.panelY + l.panelH <= static_cast<float>(size.second));
        CHECK(l.miniX >= static_cast<int>(l.panelX));
        CHECK(l.miniX + l.miniSize <= static_cast<int>(l.panelX + l.panelW) + 1);
    }
}

static void TestCommands() {
    FeatureSet features;
    CommandRegistry commands;
    RegisterCoreCommands(commands, features);
    float strength = 0.5f;
    commands.AddVariable("bloom.fuerza", &strength);

    CHECK(features.Enabled(Feature::Nebula));
    commands.Execute("desactivar nebulosa");
    CHECK(commands.LastSucceeded());
    CHECK(!features.Enabled(Feature::Nebula));
    commands.Execute("on nebulosa");
    CHECK(features.Enabled(Feature::Nebula));
    commands.Execute("alternar BLOOM");
    CHECK(!features.Enabled(Feature::Bloom));
    commands.Execute("activar bloom vineta");
    CHECK(features.Enabled(Feature::Bloom));

    commands.Execute("fijar bloom.fuerza 0.8");
    CHECK(Near(strength, 0.8f, 1e-6f));
    commands.Execute("fijar bloom.fuerza abc");
    CHECK(!commands.LastSucceeded());
    CHECK(Near(strength, 0.8f, 1e-6f));

    commands.Execute("volar");
    CHECK(!commands.LastSucceeded());
    commands.Execute("desactivar nada");
    CHECK(!commands.LastSucceeded());
    commands.Execute("");
    CHECK(commands.LastSucceeded());

    commands.Execute("desactivar todo");
    bool allOff = true;
    for (std::size_t i = 0; i < static_cast<std::size_t>(Feature::Count); ++i) allOff = allOff && !features.Enabled(static_cast<Feature>(i));
    CHECK(allOff);

    CHECK(commands.ExecuteScript("# configuracion\nactivar todo\n\ndesactivar grano  \nmal comando\n") == 1);
    CHECK(features.Enabled(Feature::Sky));
    CHECK(!features.Enabled(Feature::Grain));

    // Una funcion apagada elimina su trabajo.
    SceneGraph scene;
    JobSystem one(0);
    Renderable r;
    r.node = scene.CreateNode(kNoNode);
    scene.UpdateWorld(one);
    std::vector<Renderable> items{r};
    auto stars = GenerateSky(10, 1);
    FrameInput input;
    input.scene = &scene;
    input.renderables = &items;
    input.sky = stars;
    input.skyTexture = 3;
    input.features.Set(Feature::Sky, false);
    input.features.Set(Feature::Bloom, false);
    input.features.Set(Feature::Vignette, false);
    FrameBuilder builder;
    FramePacket packet;
    builder.Build(input, one, packet);
    CHECK(!packet.sky);
    CHECK(packet.skyTexture == kNoTexture);
    CHECK(!packet.post.bloom);
    CHECK(packet.post.vignette == 0.0f);
}

static void TestSceneRemoval() {
    JobSystem one(0);
    SceneGraph scene;
    Transform t;
    t.position = {10, 0, 0};
    const NodeId root = scene.CreateNode(kNoNode, t);
    const NodeId a = scene.CreateNode(root, t);
    const NodeId b = scene.CreateNode(a, t);
    const NodeId other = scene.CreateNode(root, t);
    CHECK(scene.NodeCount() == 4);

    scene.DestroyNode(a);  // se lleva a b
    CHECK(scene.NodeCount() == 2);
    CHECK(!scene.Alive(a) && !scene.Alive(b));
    CHECK(scene.Alive(other));

    const NodeId reused = scene.CreateNode(other, t);
    CHECK(reused == a || reused == b);
    scene.UpdateWorld(one);
    CHECK(NearD(scene.WorldPosition(reused).x, 30.0, 1e-9));
    CHECK(NearD(scene.WorldPosition(other).x, 20.0, 1e-9));

    bool threw = false;
    try { scene.DestroyNode(b == reused ? a : b); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
}

static void TestStreaming() {
    using namespace astro;
    JobSystem one(0);
    GalaxyOptions eagerOptions;
    eagerOptions.systems = 12;
    eagerOptions.maxStars = 1;  // aqui se mide lo que se carga, sin estrellas companeras
    World eager = BuildGalaxy(eagerOptions);

    GalaxyOptions lazyOptions = eagerOptions;
    lazyOptions.streaming = true;
    World lazy = BuildGalaxy(lazyOptions);

    // Sin nadie cerca solo existen las estrellas.
    CHECK(lazy.bodies.size() == lazy.sectors.size());
    CHECK(lazy.orbits.empty());
    CHECK(eager.orbits.size() == lazy.stats.totalOrbits);

    const FeatureSet features;
    const double t1 = 3.0e7, t2 = 9.1e8;

    // Acercarse crea los cuerpos.
    const Vec3d star = lazy.scene.WorldPosition(lazy.sectors[3]);
    lazy.scene.UpdateWorld(one);
    const Vec3d star3 = lazy.scene.WorldPosition(lazy.sectors[3]);
    (void)star;
    StreamWorld(lazy, star3 + Vec3d{0, 0, lazy.extents[3]}, 800.0f, {}, features);
    PublishWorld(lazy);
    CHECK(lazy.state[3].loaded);
    CHECK(lazy.stats.loadedSystems == 1);
    CHECK(lazy.catalog->systems[3]->bodies.size() == eager.systems[3]->bodies.size());

    // La posicion no depende de la historia: no se simulo nada entre t = 0 y t2.
    const auto compare = [&](double time) {
        bool same = true;
        UpdateOrbits(eager.orbits, time, eager.scene, one);
        UpdateOrbits(lazy.orbits, time, lazy.scene, one);
        eager.scene.UpdateWorld(one);
        lazy.scene.UpdateWorld(one);
        for (const int e : eager.state[3].bodies) {
            const Body& a = eager.bodies[static_cast<std::size_t>(e)];
            if (a.data < 0) continue;  // un campo no tiene orbita
            if (a.parent >= 0 && !eager.bodies[static_cast<std::size_t>(a.parent)].isStar && a.kind != PlanetClass::Asteroid) continue;  // las lunas pueden dormir
            for (const int l : lazy.state[3].bodies) {
                const Body& b = lazy.bodies[static_cast<std::size_t>(l)];
                if (b.data == a.data) {
                    same = same && b.name == a.name && NearD(b.radius, a.radius, 1e-9);
                    same = same && Length(lazy.scene.WorldPosition(b.node) - eager.scene.WorldPosition(a.node)) < 1e-3;
                }
            }
        }
        return same;
    };
    CHECK(compare(t1));

    // Al alejarse se liberan; lo seleccionado se queda.
    const std::size_t nodes = lazy.scene.NodeCount();
    const std::size_t orbitsAlive = lazy.stats.liveOrbits + lazy.state[3].beltSlots.size();  // cada campo tiene su nodo
    CHECK(orbitsAlive > 0);
    const Vec3d faraway = star3 + Vec3d{0, 0, lazy.extents[3] * 1000.0};
    const int planet = lazy.state[3].bodies[0];
    StreamWorld(lazy, faraway, 800.0f, {planet}, features);
    CHECK(lazy.state[3].loaded);
    StreamWorld(lazy, faraway, 800.0f, {}, features);
    PublishWorld(lazy);
    CHECK(!lazy.state[3].loaded);
    CHECK(lazy.scene.NodeCount() == nodes - orbitsAlive);
    CHECK(lazy.catalog->systems[3]->bodies.empty());
    CHECK(!Alive(lazy.bodies[static_cast<std::size_t>(planet)]));
    CHECK(lazy.stats.liveOrbits <= orbitsAlive);

    // Vuelve mucho despues: mismos cuerpos, en el sitio exacto segun las ecuaciones.
    StreamWorld(lazy, star3 + Vec3d{0, 0, lazy.extents[3]}, 800.0f, {}, features);
    PublishWorld(lazy);
    CHECK(lazy.state[3].loaded);
    CHECK(compare(t2));
    CHECK(lazy.stats.loads == 2 && lazy.stats.unloads == 1);

    // Muchos ciclos no dejan basura: los huecos se reutilizan.
    const std::size_t capacity = lazy.renderables.size();
    for (int i = 0; i < 40; ++i) {
        UnloadSystem(lazy, 3);
        LoadSystem(lazy, 3);
    }
    CHECK(lazy.renderables.size() == capacity);
    CHECK(lazy.scene.NodeCount() == nodes);

    // Una luna que no ocupa un pixel no se calcula; al acercarse, reaparece en su sitio.
    GalaxyOptions moonOptions;
    moonOptions.systems = 3;
    moonOptions.seed = 5;
    moonOptions.planetsMin = moonOptions.planetsMax = 6;
    moonOptions.moonsMax = 4;
    World moons = BuildGalaxy(moonOptions);
    int moon = -1, system = -1;
    for (int s = 0; s < 3 && moon < 0; ++s) if (!moons.state[static_cast<std::size_t>(s)].moons.empty()) { moon = moons.state[static_cast<std::size_t>(s)].moons[0]; system = s; }
    CHECK(moon >= 0);
    if (moon >= 0) {
        const Body body = moons.bodies[static_cast<std::size_t>(moon)];
        const Body host = moons.bodies[static_cast<std::size_t>(body.parent)];
        UpdateOrbits(moons.orbits, 0.0, moons.scene, one);
        moons.scene.UpdateWorld(one);
        const Vec3d hostPosition = moons.scene.WorldPosition(host.node);
        StreamWorld(moons, moons.scene.WorldPosition(moons.sectors[static_cast<std::size_t>(system)]) + Vec3d{0, 0, 1.0e11}, 800.0f, {}, features);
        CHECK(!moons.orbits[static_cast<std::size_t>(body.orbit)].active);
        CHECK(moons.renderables[static_cast<std::size_t>(body.renderable)].hidden);
        CHECK(moons.stats.activeOrbits < moons.stats.liveOrbits);

        // Seleccionada no duerme, aunque no se vea.
        StreamWorld(moons, moons.scene.WorldPosition(moons.sectors[static_cast<std::size_t>(system)]) + Vec3d{0, 0, 1.0e11}, 800.0f, {moon}, features);
        CHECK(moons.orbits[static_cast<std::size_t>(body.orbit)].active);

        StreamWorld(moons, hostPosition + Vec3d{0, 0, host.radius * 3.0}, 800.0f, {}, features);
        CHECK(moons.orbits[static_cast<std::size_t>(body.orbit)].active);
        CHECK(!moons.renderables[static_cast<std::size_t>(body.renderable)].hidden);

        FeatureSet noCulling;
        noCulling.Set(Feature::BodyCulling, false);
        StreamWorld(moons, Vec3d{0, 0, 1.0e15}, 800.0f, {}, noCulling);
        CHECK(moons.stats.activeOrbits == moons.stats.liveOrbits);
    }

    // Origen flotante: nada cambia respecto a la camara y la precision vuelve.
    GalaxyOptions originOptions;
    originOptions.systems = 4;
    World shifted = BuildGalaxy(originOptions);
    UpdateOrbits(shifted.orbits, 0.0, shifted.scene, one);
    shifted.scene.UpdateWorld(one);
    const Vec3d target = shifted.scene.WorldPosition(shifted.sectors[2]);
    Vec3d camera = target + Vec3d{1000.0, 0, 0};
    const Vec3d before = shifted.scene.WorldPosition(shifted.sectors[1]) - camera;
    RecenterOrigin(shifted, camera);
    shifted.scene.UpdateWorld(one);
    CHECK(Length(camera) < 1.0);
    CHECK(Length(shifted.scene.WorldPosition(shifted.sectors[1]) - camera - before) < 0.1);
    CHECK(NearD(Length(shifted.scene.WorldPosition(shifted.sectors[2]) - camera), 1000.0, 1e-6));
    Vec3d stay = {5.0, 0, 0};
    CHECK(Length(RecenterOrigin(shifted, stay)) == 0.0);  // cerca del origen no hace nada

    // La nebulosa acompana al origen.
    const Vec3d absolute = shifted.origin;
    CHECK(Length(absolute - target) < 2000.0);
}

static void TestBlackHoles() {
    using namespace astro;
    // Radio de Schwarzschild del Sol: 2,95 km. De Sagitario A*, unos 1,27e7 km.
    CHECK(NearD(SchwarzschildRadius(kSolarMass), 2.953, 0.01));
    CHECK(NearD(SchwarzschildRadius(4.3e6 * kSolarMass) / 1.27e7, 1.0, 0.01));

    // Valores exactos de Schwarzschild, en radios de Schwarzschild.
    CHECK(NearD(KerrHorizon(0.0), 1.0, 1e-12));
    CHECK(NearD(InnermostStableOrbit(0.0), 3.0, 1e-9));
    CHECK(NearD(PhotonSphere(0.0, true), 1.5, 1e-9));
    double left = 0, right = 0;
    KerrShadowEdges(0.0, left, right);
    CHECK(NearD(right, 2.598, 1e-3));

    // Y de Kerr en el limite: el horizonte y la ultima orbita llegan a la mitad de rs.
    CHECK(NearD(KerrHorizon(0.9999), 0.507, 0.01));
    CHECK(InnermostStableOrbit(0.9) < 1.2);
    CHECK(NearD(InnermostStableOrbit(0.9), 1.16, 0.01));  // 2,32 M, tabla de Bardeen
    CHECK(PhotonSphere(0.9, true) < 1.5 && PhotonSphere(0.9, false) > 1.5);

    // El integrador del shader reproduce la sombra exacta de cada forma.
    const auto edge = [](double spin, double sign) {
        double captured = 0.0, escaped = 12.0;
        for (int i = 0; i < 26; ++i) {
            const double b = (captured + escaped) * 0.5;
            const LensResult r = TraceLensRay(Vec3d{sign * b, 0, -55.0}, Vec3d{0, 0, 1}, spin);
            (r.captured ? captured : escaped) = b;
        }
        return sign * captured;
    };

    const double schwarzschild = edge(0.0, 1.0);
    std::printf("  sombra Schwarzschild: %.3f (exacta %.3f)\n", schwarzschild, std::sqrt(27.0) * 0.5);
    CHECK(std::abs(schwarzschild / (std::sqrt(27.0) * 0.5) - 1.0) < 0.02);
    CHECK(NearD(edge(0.0, -1.0), -schwarzschild, 1e-3));  // simetrica

    KerrShadowEdges(0.9, left, right);
    const double a = edge(0.9, 1.0), b = edge(0.9, -1.0);
    const double width = a - b, exactWidth = right - left;
    const double shift = std::abs(a + b) * 0.5, exactShift = std::abs(left + right) * 0.5;
    std::printf("  sombra Kerr a=0.9: anchura %.3f (exacta %.3f), desplazamiento %.3f (exacto %.3f)\n", width, exactWidth, shift, exactShift);
    CHECK(std::abs(width / exactWidth - 1.0) < 0.03);
    CHECK(shift > exactShift * 0.5 && shift < exactShift * 1.1);  // aproximacion: ver docs/astrofisica.md

    // Lejos de la sombra, la desviacion es la de la relatividad general:
    // 2 rs / b + (15 pi / 16) (rs / b)^2, los dos primeros terminos de la serie.
    constexpr double kB = 30.0;
    const LensResult weak = TraceLensRay(Vec3d{kB, 0, -1500.0}, Vec3d{0, 0, 1}, 0.0, 20000, 1500.0);
    const double bend = std::atan2(-weak.direction.x, weak.direction.z);
    CHECK(!weak.captured);
    const double expected = 2.0 / kB + 15.0 * kPi / 16.0 / (kB * kB);
    std::printf("  desviacion debil: %.4f rad (teorica %.4f)\n", bend, expected);
    CHECK(std::abs(bend / expected - 1.0) < 0.02);

    // La galaxia contiene las dos formas y el supermasivo del centro.
    GalaxyOptions options;
    options.systems = 8;
    options.blackHoles = true;
    options.nebulae = 2;
    World w = BuildGalaxy(options);
    int still = 0, spinning = 0, giant = 0;
    for (const BlackHoleRecord& h : w.blackHoles) {
        CHECK(h.discInner < h.discOuter);
        if (h.spin == 0.0f) ++still;
        else ++spinning;
        if (h.schwarzschildRadius > 1.0e6) ++giant;
    }
    CHECK(still >= 1);
    CHECK(spinning >= 2);
    CHECK(giant == 1);
    CHECK(w.sectors.size() == 9);
    CHECK(w.nebulae.size() == 2);

    // La capa solo lo aporta si ocupa pantalla y esta activo.
    JobSystem one(0);
    UpdateOrbits(w.orbits, 0.0, w.scene, one);
    w.scene.UpdateWorld(one);
    BlackHoleLayer layer(w);
    NebulaLayer nebula(w);
    FrameInput input;
    input.scene = &w.scene;
    input.renderables = &w.renderables;
    input.surfaces = w.surfaces;
    input.layers = {&layer, &nebula};
    input.camera.SetViewport(1600, 900, 800.0f, 5e-5f);
    const BlackHoleRecord& first = w.blackHoles.front();
    input.camera.SetPosition(w.scene.WorldPosition(first.node) + Vec3d{0, 0, -first.schwarzschildRadius * 60.0});
    FrameBuilder builder;
    FramePacket packet;
    builder.Build(input, one, packet);
    CHECK(packet.blackHoles.size() == 1);
    if (!packet.blackHoles.empty()) CHECK(Near(Length(packet.blackHoles[0].viewAxis), 1.0f, 1e-3f));
    input.features.Set(Feature::BlackHoles, false);
    builder.Build(input, one, packet);
    CHECK(packet.blackHoles.empty());
    input.features.Set(Feature::BlackHoles, true);
    input.camera.SetPosition(w.scene.WorldPosition(first.node) + Vec3d{0, 0, -kAstronomicalUnit * 50.0});
    builder.Build(input, one, packet);
    CHECK(packet.blackHoles.empty());  // de lejos basta el sprite
}

static void TestAsteroids() {
    using namespace astro;
    JobSystem one(0), many(5);

    AsteroidFamily family;
    family.rockCount = 3000;
    family.radius = 400.0f;
    family.seed = 77;
    const std::vector<AsteroidRock> a = GenerateRocks(family), b = GenerateRocks(family);
    CHECK(a.size() == 3000);
    bool same = true, inside = true;
    float biggest = 0;
    int small = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        same = same && a[i].radius == b[i].radius && a[i].offset.x == b[i].offset.x;
        inside = inside && Length(a[i].offset) <= family.radius * 1.001f;
        biggest = std::max(biggest, a[i].radius);
        small += a[i].radius < 0.4f ? 1 : 0;
    }
    CHECK(same);
    CHECK(inside);
    CHECK(biggest <= 14.0f);
    CHECK(small > 2000);  // la ley de potencias produce sobre todo rocas pequenas

    // Sistema con un hueco: el cinturon cae dentro y el tipo depende de la distancia.
    StarSystemData system;
    system.star.snowLine = 2.7 * kAstronomicalUnit;
    for (const double au : {1.0, 5.2}) {
        BodyData planet;
        planet.kind = PlanetClass::Rock;
        planet.orbit.semiMajorAxis = au * kAstronomicalUnit;
        system.bodies.push_back(planet);
    }
    const std::vector<AsteroidBelt> belts = GenerateBelts(system, 9);
    CHECK(belts.size() == 2);
    CHECK(belts[0].inner > kAstronomicalUnit && belts[0].outer < 5.2 * kAstronomicalUnit);
    CHECK(belts[1].inner > 5.2 * kAstronomicalUnit);
    int carbonInner = 0, carbonOuter = 0;
    for (const AsteroidFamily& f : belts[0].families) carbonInner += f.type == AsteroidType::Carbon ? 1 : 0;
    for (const AsteroidFamily& f : belts[1].families) carbonOuter += f.type == AsteroidType::Carbon ? 1 : 0;
    const auto share = [](int count, const AsteroidBelt& belt) { return static_cast<double>(count) / static_cast<double>(belt.families.size()); };
    CHECK(share(carbonOuter, belts[1]) > share(carbonInner, belts[0]));
    bool orbitOk = true;
    for (const AsteroidFamily& f : belts[0].families) orbitOk = orbitOk && f.orbit.elements.semiMajorAxis >= belts[0].inner && f.orbit.elements.semiMajorAxis <= belts[0].outer && f.orbit.elements.eccentricity < 0.31;
    CHECK(orbitOk);

    // Lejos no se calcula nada; junto a una familia nacen sus rocas.
    GalaxyOptions options;
    options.systems = 3;
    options.planetsMin = options.planetsMax = 6;
    World w = BuildGalaxy(options);
    UpdateOrbits(w.orbits, 0.0, w.scene, one);
    w.scene.UpdateWorld(one);
    CHECK(!w.belts.empty());

    AsteroidLayerSettings settings;
    settings.meshes = {4, 5, 6};
    settings.nearRange = 20000.0f;
    AsteroidLayer layer(w, settings);
    FrameInput input;
    input.scene = &w.scene;
    input.renderables = &w.renderables;
    input.layers = {&layer};
    input.simTime = 2.0e8;
    input.camera.SetViewport(1600, 900, 800.0f, 5e-5f);
    input.camera.SetPosition({1.0e15, 0, 0});
    FrameBuilder builder;
    FramePacket packet;
    builder.Build(input, one, packet);
    CHECK(packet.stats.asteroidsCalculated == 0);
    CHECK(layer.CachedFamilies() == 0);

    Vec3d center;
    CHECK(AsteroidLayer::NearestFamily(w, w.scene.WorldPosition(w.sectors[0]), input.simTime, center));
    input.camera.SetPosition(center + Vec3d{0, 0, -120.0});
    builder.Build(input, one, packet);
    CHECK(layer.CachedFamilies() >= 1);
    CHECK(packet.stats.asteroidsCalculated > 0);
    CHECK(packet.stats.meshesVisible > 0);
    bool known = true, scaled = true;
    for (const MeshInstance& m : packet.meshes) {
        known = known && (m.mesh == 4 || m.mesh == 5 || m.mesh == 6);
        scaled = scaled && m.scale > 0.0f && m.scale <= 90.0f;  // el mayor perfil
    }
    CHECK(known);
    CHECK(scaled);

    // Mismo resultado con cualquier numero de hilos.
    FrameBuilder second;
    FramePacket other;
    AsteroidLayer layer2(w, settings);
    FrameInput input2 = input;
    input2.layers = {&layer2};
    second.Build(input2, many, other);
    CHECK(other.meshes.size() == packet.meshes.size());
    bool sameMeshes = other.meshes.size() == packet.meshes.size();
    for (std::size_t i = 0; sameMeshes && i < other.meshes.size(); ++i) sameMeshes = other.meshes[i].id == packet.meshes[i].id;
    CHECK(sameMeshes);

    input.features.Set(Feature::Asteroids, false);
    builder.Build(input, one, packet);
    CHECK(packet.meshes.empty());
    CHECK(packet.stats.asteroidsCalculated == 0);

    // Un campo suelto no necesita sistema ni orbita.
    const AsteroidBelt field = MakeField(0, 500.0f, 200, AsteroidType::Metal, 3);
    CHECK(field.families.size() == 1 && field.families[0].fixed);
    CHECK(GenerateRocks(field.families[0]).size() == 200);
}

// Volumen con signo: positivo si todos los triangulos miran hacia fuera.
static double SignedVolume(const MeshData& m) {
    double volume = 0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const Vec3f& a = m.vertices[m.indices[i]].position;
        const Vec3f& b = m.vertices[m.indices[i + 1]].position;
        const Vec3f& c = m.vertices[m.indices[i + 2]].position;
        volume += Dot(Vec3d(a), Cross(Vec3d(b), Vec3d(c))) / 6.0;
    }
    return volume;
}

static bool ValidIndices(const MeshData& m) {
    bool ok = m.indices.size() % 3 == 0 && !m.indices.empty();
    for (const std::uint32_t i : m.indices) ok = ok && i < m.vertices.size();
    return ok;
}

static void TestModels() {
    // Formas cerradas con los triangulos hacia fuera. El volumen confirma tamano y sentido.
    const double pi = kPi;
    CHECK(NearD(SignedVolume(MakeBox(2, 3, 4)), 24.0, 1e-5));
    CHECK(std::abs(SignedVolume(MakeCylinder(1, 2, 128)) / (pi * 2.0) - 1.0) < 0.01);
    CHECK(std::abs(SignedVolume(MakeTorus(3, 0.5f, 96, 48)) / (2.0 * pi * pi * 3.0 * 0.25) - 1.0) < 0.01);
    CHECK(std::abs(SignedVolume(MakeSphere(2, 96)) / (4.0 / 3.0 * pi * 8.0) - 1.0) < 0.01);
    const MeshData quad = MakeQuad(2, 1);
    CHECK(ValidIndices(quad));
    const MeshData boxMesh = MakeBox(1, 1, 1);
    CHECK(MeshRadius(boxMesh) > 0.86f && MeshRadius(boxMesh) < 0.87f);

    const MeshData rock = MakeAsteroid(11), rock2 = MakeAsteroid(11), rock3 = MakeAsteroid(12);
    CHECK(ValidIndices(rock));
    CHECK(SignedVolume(rock) > 0.5);
    CHECK(rock.vertices.size() == rock2.vertices.size() && rock.vertices[20].position.x == rock2.vertices[20].position.x);
    CHECK(rock.vertices[20].position.x != rock3.vertices[20].position.x);

    MeshData joined;
    AppendMesh(joined, MakeBox(1, 1, 1), {5, 0, 0});
    AppendMesh(joined, MakeBox(1, 1, 1), {-5, 0, 0}, Quatf::FromAxisAngle({0, 1, 0}, 1.0f), 2.0f);
    CHECK(ValidIndices(joined));
    CHECK(NearD(SignedVolume(joined), 9.0, 1e-3));

    // OBJ: un cuadrado con una cara de 4 lados, indices negativos y UV.
    const std::string obj = "# prueba\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/2/1 4/1/1\nf -4 -3 -2\n";
    MeshData parsed;
    std::string error;
    CHECK(ParseObj(obj, parsed, &error));
    CHECK(parsed.indices.size() == 9);
    CHECK(Near(parsed.vertices[2].position.y, -1.0f, 1e-6f));  // Y hacia arriba pasa a -Y
    CHECK(Near(parsed.vertices[2].normal.z, -1.0f, 1e-6f));
    CHECK(Near(parsed.vertices[1].v, 0.0f, 1e-6f));  // V invertida
    CHECK(!ParseObj("v 0 0 0\nf 1 2 3\n", parsed, &error));
    CHECK(!ParseObj("hola\n", parsed, &error));
    CHECK(!LoadObj("no_existe.obj", parsed, &error));
    std::printf("  glTF %s\n", GltfSupported() ? "disponible" : "no disponible en esta compilacion");

    if (GltfSupported()) {
        const float vertices[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        const std::uint16_t triangle[3] = {0, 1, 2};
        std::string bytes(reinterpret_cast<const char*>(vertices), sizeof(vertices));
        bytes.append(reinterpret_cast<const char*>(triangle), sizeof(triangle));
        static const char* const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        for (std::size_t i = 0; i < bytes.size(); i += 3) {
            const std::size_t left = bytes.size() - i;
            const auto byte = [&](std::size_t k) { return k < bytes.size() ? static_cast<unsigned>(static_cast<unsigned char>(bytes[k])) : 0u; };
            const unsigned value = byte(i) << 16 | byte(i + 1) << 8 | byte(i + 2);
            encoded += alphabet[value >> 18 & 63];
            encoded += alphabet[value >> 12 & 63];
            encoded += left > 1 ? alphabet[value >> 6 & 63] : '=';
            encoded += left > 2 ? alphabet[value & 63] : '=';
        }
        const std::string json =
            R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
            R"("nodes":[{"name":"casco","mesh":0,"translation":[2,0,0]}],)"
            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
            R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.25,0.5,0.75,1]}}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
            R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
            R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
            R"("buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,)" + encoded + R"("}]})";
        const std::string path = "epsilon_prueba.gltf";
        {
            std::ofstream file(path, std::ios::binary);
            file << json;
        }
        std::vector<ImportedPart> parts;
        const bool loaded = LoadGltf(path, parts, &error);
        if (!loaded) std::printf("  %s\n", error.c_str());
        CHECK(loaded);
        CHECK(parts.size() == 1);
        if (parts.size() == 1) {
            CHECK(parts[0].name == "casco");
            CHECK(parts[0].mesh.indices.size() == 3);
            CHECK(Near(parts[0].mesh.vertices[1].position.x, 3.0f, 1e-6f));   // la traslacion del nodo
            CHECK(Near(parts[0].mesh.vertices[2].position.y, -1.0f, 1e-6f));  // Y hacia arriba pasa a -Y
            CHECK(Near(parts[0].color.b, 0.75f, 1e-6f));
            int uploads = 0;
            const ModelRef built = BuildModel("prueba", parts, [&](const MeshData&) { return static_cast<MeshId>(40 + uploads++); });
            CHECK(built->parts.size() == 1 && built->parts[0].mesh == 40);
        }
        std::remove(path.c_str());
        std::vector<ImportedPart> none;
        CHECK(!LoadGltf("no_existe.glb", none, &error));
    }

    // Escala humana lejos del origen. A 50 anos luz, un km en double salta 60 m.
    JobSystem one(0);
    SceneGraph scene;
    Transform t;
    t.position = {50.0 * astro::kLightYear, 0, 0};
    const NodeId station = scene.CreateNode(kNoNode, t);
    scene.UpdateWorld(one);

    auto part = [] {
        ModelPart p;
        p.name = "panel";
        p.mesh = 3;
        p.radius = 0.5f;
        p.color = {0.5f, 0.5f, 0.5f, 1.0f};
        return p;
    }();
    auto model = std::make_shared<ModelAsset>();
    model->name = "prueba";
    model->parts.push_back(part);
    part.name = "lejos";
    part.position = {0, 0, 100};
    model->parts.push_back(part);

    LocalSpace space;
    space.anchor = station;
    const int id = space.Spawn(model, {0.003, 0, 2.0});  // 3 mm a un lado, 2 m delante
    space.hasViewer = true;
    space.viewer = {0, 0, 0};
    ModelLayer layer;
    layer.Add(&space);

    std::vector<Renderable> nothing;
    FrameInput input;
    input.scene = &scene;
    input.renderables = &nothing;
    input.layers = {&layer};
    input.camera.SetViewport(1600, 900, 800.0f, 5e-5f);
    input.camera.SetPosition(space.ToWorldKm(scene, space.viewer));
    FrameBuilder builder;
    FramePacket packet;
    builder.Build(input, one, packet);
    CHECK(packet.meshes.size() == 2);
    if (packet.meshes.size() == 2) {
        const MeshInstance& m = packet.meshes[0];
        CHECK(Near(m.viewPosition.x * 1000.0f, 0.003f, 1e-5f));  // los 3 mm sobreviven
        CHECK(Near(m.viewPosition.z * 1000.0f, 2.0f, 1e-4f));
        CHECK(Near(m.scale, 0.001f, 1e-9f));
    }

    // Partes: se ocultan por nombre y un rayo encuentra la mas cercana.
    const RayHit hit = space.Raycast({0, 0, 0}, {0, 0, 1});
    CHECK(hit.instance == id && hit.part == 0);
    CHECK(space.PartName(id, hit.part) == "panel");
    CHECK(NearD(hit.distance, 1.5, 0.01));
    CHECK(space.SetPartVisible(id, "panel", false));
    CHECK(!space.SetPartVisible(id, "nada", false));
    builder.Build(input, one, packet);
    CHECK(packet.meshes.size() == 1);
    const RayHit second = space.Raycast({0, 0, 0}, {0, 0, 1});
    CHECK(second.part == 1);
    CHECK(space.Raycast({0, 0, 0}, {0, 0, -1}).instance == -1);
    CHECK(space.SetPartColor(id, "lejos", {1, 0, 0, 1}, 2.0f));
    builder.Build(input, one, packet);
    CHECK(packet.meshes.size() == 1 && packet.meshes[0].emissive == 2.0f);

    // Con triangulos, el rayo pasa por el hueco que una esfera tapaba.
    auto ring = std::make_shared<ModelAsset>();
    ModelPart hoop;
    hoop.name = "aro";
    hoop.mesh = 3;
    const MeshData torus = MakeTorus(10.0f, 1.0f);
    hoop.radius = MeshRadius(torus);
    hoop.collision = std::make_shared<const MeshData>(torus);
    ring->parts.push_back(hoop);
    LocalSpace hollow;
    hollow.Spawn(ring);
    CHECK(hollow.Raycast({0, -20, 0}, {0, 1, 0}).instance == -1);  // por el centro del aro
    const RayHit rim = hollow.Raycast({10, -20, 0}, {0, 1, 0});
    CHECK(rim.instance == 0);
    CHECK(NearD(rim.distance, 19.0, 0.02));

    // Menos de un pixel: no se dibuja. Asi una estacion lejana no cuesta nada.
    space.hasViewer = false;
    input.camera.SetPosition(space.ToWorldKm(scene, {0, 0, 0}) + Vec3d{0, 0, -3.0e6});
    builder.Build(input, one, packet);
    CHECK(packet.meshes.empty());

    // Apagar el modulo elimina su trabajo y el resto del motor sigue igual.
    space.hasViewer = true;
    input.camera.SetPosition(space.ToWorldKm(scene, space.viewer));
    input.features.Set(Feature::Models, false);
    builder.Build(input, one, packet);
    CHECK(packet.meshes.empty());
    input.features.Set(Feature::Models, true);

    // Instancias: se reciclan y las mallas iguales forman un lote.
    const int extra = space.Spawn(model, {0.5, 0, 4.0});
    CHECK(space.InstanceCount() == 2);
    builder.Build(input, one, packet);
    CHECK(packet.meshBatches.size() == 1);
    CHECK(packet.meshBatches[0].count == packet.meshes.size());
    space.Remove(extra);
    CHECK(space.InstanceCount() == 1);
    CHECK(space.Spawn(model) == extra);

    // Modelo 2D: siempre de frente, sin importar hacia donde mire la camara.
    auto flat = std::make_shared<ModelAsset>();
    flat->parts.push_back(MakeSpritePart("cartel", 8, 2, 2.0f, 1.0f));
    LocalSpace sign;
    sign.Spawn(flat, {0, 0, 3.0});
    sign.hasViewer = true;
    ModelLayer signLayer;
    signLayer.Add(&sign);
    input.layers = {&signLayer};
    input.camera.SetPosition({0, 0, 0});
    input.camera.SetYawPitchRoll(0.0f, 0.0f, 0.0f);
    builder.Build(input, one, packet);
    CHECK(packet.meshes.size() == 1);
    if (!packet.meshes.empty()) {
        CHECK(packet.meshes[0].mesh == 8);
        CHECK(Near(packet.meshes[0].rotation.w, 1.0f, 1e-6f));
        CHECK(packet.meshes[0].stretchX == 2.0f && packet.meshes[0].stretchY == 1.0f);
    }
}

static void TestEngineFacade() {
    using namespace astro;
    EngineConfig config;
    config.galaxy.systems = 5;
    config.galaxy.streaming = true;
    config.galaxy.blackHoles = true;
    config.galaxy.nebulae = 1;
    config.galaxy.maxStars = 1;
    Engine engine(config);
    World& w = engine.GetWorld();
    CHECK(w.orbits.empty());

    // Un comando apaga un modulo del juego sin tocar nada mas.
    CHECK(!engine.Features().Enabled(Feature::Routes));
    engine.Commands().Execute("activar rutas");
    CHECK(engine.Features().Enabled(Feature::Routes));
    engine.Commands().Execute("fijar exposicion 1.5");
    CHECK(Near(engine.Post().exposure, 1.5f, 1e-6f));

    Camera camera;
    camera.SetViewport(1600, 900, 800.0f, kNearPlaneKm);
    const Vec3d star = w.scene.WorldPosition(w.sectors[0]);
    camera.SetPosition(star + Vec3d{0, 0, -w.extents[0] * 0.5});
    engine.Advance(camera.Position(), camera.FocalPx(), 1.0e6);
    CHECK(w.state[0].loaded);
    FramePacket packet;
    engine.Build(camera, 1.0, packet);
    CHECK(packet.stats.renderablesTotal > 10);
    CHECK(Near(packet.post.exposure, 1.5f, 1e-6f));

    // Escalas mezcladas en un mismo fotograma: una bola de 10 m delante y la estrella detras.
    LocalSpace& space = engine.CreateLocalSpace(w.sectors[0], w.sectors[0]);
    space.anchorOffset = {0.0, 0.0, -w.extents[0] * 0.5 + 0.03};
    auto ball = std::make_shared<ModelAsset>();
    ModelPart part;
    part.mesh = 2;
    part.radius = 5.0f;
    ball->parts.push_back(part);
    space.Spawn(ball);
    engine.Build(camera, 1.0, packet);
    CHECK(packet.meshes.size() == 1);
    if (!packet.meshes.empty()) {
        CHECK(Near(packet.meshes[0].viewPosition.z, 0.03f, 1e-3f));  // 30 m, medidos en km
        // Los depths de 30 m y de un planeta a 1 UA son distintos, y el orden no se invierte.
        const float nearDepth = kNearPlaneKm / packet.meshes[0].viewPosition.z;
        const float farDepth = kNearPlaneKm / 1.5e8f;
        CHECK(nearDepth > farDepth && farDepth > 0.0f);
    }

    engine.Commands().Execute("desactivar modelos orbitas rutas");
    engine.Build(camera, 1.0, packet);
    CHECK(packet.meshes.empty());
    bool noPaths = true;
    for (const LineInstance& l : packet.lines) noPaths = noPaths && l.color.a <= 0.0f;
    CHECK(noPaths);
    const std::string report = engine.Commands().Execute("cargados");
    std::printf("  %s\n", report.c_str());
    CHECK(report.find("Sistemas cargados: ") == 0);
    CHECK(w.stats.loadedSystems < w.state.size());
}

static void TestMultipleStars() {
    using namespace astro;
    JobSystem one(0);

    // Valores publicados: pareja igual y circular, y el sistema de Kepler-16.
    CHECK(Near(static_cast<float>(CircumbinaryLimit(0.5, 0.0)), 2.39f, 0.01f));
    CHECK(Near(static_cast<float>(CircumstellarLimit(0.5, 0.0)), 0.274f, 0.002f));
    CHECK(Near(static_cast<float>(CircumbinaryLimit(0.2, 0.16)), 2.87f, 0.06f));
    CHECK(Near(static_cast<float>(HierarchyLimit(1.0, 2.0, 1.0, 0.0)), 3.3f, 0.01f));  // q = 0,5

    // Cualquier forma y cualquier clase dan sistemas que prometen lo mismo que uno simple.
    const StarLayout layouts[] = {StarLayout::Close, StarLayout::Wide, StarLayout::Triple, StarLayout::Quadruple};
    const int counts[] = {2, 2, 3, 4};
    std::mt19937 rng(77);
    std::string firstProblem;
    bool shape = true, barycenter = true, samePeriod = true, inZone = true;
    int circumbinary = 0, circumstellar = 0;
    for (std::size_t l = 0; l < std::size(layouts); ++l) {
        for (int c = 0; c < static_cast<int>(StarClass::Count); ++c) {
            for (int i = 0; i < 120; ++i) {
                GeneratorOptions options;
                options.layout = layouts[l];
                const StarSystemData s = GenerateSystem(static_cast<StarClass>(c), rng, options);
                shape = shape && s.StarCount() == counts[l];
                const std::string problem = CheckStability(s);
                if (!problem.empty() && firstProblem.empty()) firstProblem = std::string(Info(static_cast<StarClass>(c)).name) + " " + std::to_string(l) + ": " + problem;

                // El centro de masas de cada pareja no se mueve y las dos partes tardan lo mismo.
                for (const StellarNode& node : s.nodes) {
                    if (node.first < 0) continue;
                    const StellarNode& a = s.nodes[static_cast<std::size_t>(node.first)];
                    const StellarNode& b = s.nodes[static_cast<std::size_t>(node.second)];
                    const KeplerOrbit oa = MakeOrbit(kNoNode, a.orbit), ob = MakeOrbit(kNoNode, b.orbit);
                    const double t = Uniform(rng, 0, 1.0e9);
                    const Vec3d weighted = OrbitPosition(oa, t) * a.effective.mass + OrbitPosition(ob, t) * b.effective.mass;
                    barycenter = barycenter && Length(weighted) / (a.effective.mass + b.effective.mass) < node.separation * 1e-9;
                    samePeriod = samePeriod && std::abs(OrbitPeriod(a.orbit) / OrbitPeriod(b.orbit) - 1.0) < 1e-9;
                    samePeriod = samePeriod && NearD(Length(OrbitPosition(oa, t) - OrbitPosition(ob, t)) / node.separation, Length(OrbitPosition(oa, t) - OrbitPosition(ob, t)) / node.separation, 1e-12);
                }
                for (const BodyData& b : s.bodies) {
                    if (b.parent >= 0 || b.kind == PlanetClass::Comet) continue;
                    const StellarNode& host = s.nodes[static_cast<std::size_t>(b.host)];
                    inZone = inZone && host.planetOuter > 0.0;
                    if (host.first >= 0) ++circumbinary;
                    else ++circumstellar;
                }
            }
        }
    }
    if (!firstProblem.empty()) std::printf("  Primer problema: %s\n", firstProblem.c_str());
    CHECK(firstProblem.empty());
    CHECK(shape);
    CHECK(barycenter);
    CHECK(samePeriod);
    CHECK(inZone);
    CHECK(circumbinary > 100);
    CHECK(circumstellar > 100);

    // Una pareja cerrada se ve como una fuente; sus planetas tienen el apellido de las dos.
    GeneratorOptions close;
    close.layout = StarLayout::Close;
    close.planetsMin = close.planetsMax = 3;
    const StarSystemData pair = GenerateSystem(StarClass::G, rng, close);
    CHECK(NearD(pair.nodes[0].effective.mass, pair.star.mass + pair.companions[0].mass, 1.0));
    CHECK(NearD(pair.nodes[0].effective.luminosity, pair.star.luminosity + pair.companions[0].luminosity, 1e-9));
    CHECK(BodyName(pair, 0, "S").find("S AB b") == 0);

    // El azar respeta las frecuencias: ni todo sencillo ni todo multiple.
    int multiple = 0;
    GeneratorOptions any;
    any.maxStars = 4;
    for (int i = 0; i < 3000; ++i) multiple += GenerateSystem(StarClass::G, rng, any).StarCount() > 1 ? 1 : 0;
    CHECK(multiple > 1100 && multiple < 1600);  // 45 %

    // En la escena, cada estrella es un cuerpo que se mueve de verdad.
    GalaxyOptions options;
    options.systems = 8;
    World w = BuildGalaxy(options);
    CHECK(w.state[3].starBodies.size() == 2);
    CHECK(w.state[4].starBodies.size() == 2);
    CHECK(w.state[5].starBodies.size() == 3);
    CHECK(w.state[6].starBodies.size() == 4);
    CHECK(w.state[0].starBodies.size() == 1);

    const auto starPosition = [&](World& world, int system, int member, double time) {
        UpdateOrbits(world.orbits, time, world.scene, one);
        world.scene.UpdateWorld(one);
        return world.scene.WorldPosition(world.state[static_cast<std::size_t>(system)].starNodes[static_cast<std::size_t>(member)]);
    };
    const Vec3d early = starPosition(w, 3, 1, 0.0), later = starPosition(w, 3, 1, 3.0e6);
    CHECK(Length(early - later) > 1000.0);
    const Vec3d sector = w.scene.WorldPosition(w.sectors[3]);
    const StarSystemData& shown = *w.systems[3];
    const Vec3d weighted = (starPosition(w, 3, 0, 5.0e6) - sector) * shown.star.mass + (starPosition(w, 3, 1, 5.0e6) - sector) * shown.companions[0].mass;
    CHECK(Length(weighted) / (shown.star.mass + shown.companions[0].mass) < 1.0);

    // Cada planeta conoce sus dos soles y el fotograma recibe las dos luces.
    bool twoLights = !w.state[3].bodies.empty();
    for (const int b : w.state[3].bodies) {
        const Body& body = w.bodies[static_cast<std::size_t>(b)];
        if (body.renderable >= 0) twoLights = twoLights && w.renderables[static_cast<std::size_t>(body.renderable)].lightNode2 >= 0;
    }
    CHECK(twoLights);
    int planet = -1;
    for (const int b : w.state[3].bodies)
        if (w.bodies[static_cast<std::size_t>(b)].kind != PlanetClass::Belt && w.bodies[static_cast<std::size_t>(b)].kind != PlanetClass::Asteroid && w.bodies[static_cast<std::size_t>(b)].kind != PlanetClass::Moon) {
            planet = b;
            break;
        }
    CHECK(planet >= 0);
    if (planet >= 0) {
        const Body& body = w.bodies[static_cast<std::size_t>(planet)];
        FrameInput input;
        input.scene = &w.scene;
        input.renderables = &w.renderables;
        input.surfaces = w.surfaces;
        input.camera.SetViewport(1600, 900, 800.0f, 5e-5f);
        const Vec3d center = w.scene.WorldPosition(body.node);
        input.camera.SetPosition(center + Vec3d{0, 0, -body.radius * 5.0});
        FrameBuilder builder;
        FramePacket packet;
        builder.Build(input, one, packet);
        bool lit = false;
        for (const SphereInstance& s : packet.spheres) lit = lit || (s.light2Strength > 0.0f && s.radius == static_cast<float>(body.radius) && Near(Length(s.light2Direction), 1.0f, 1e-3f));
        CHECK(lit);
    }

    // Lejos, las estrellas de una pareja no se calculan; cerca, si.
    GalaxyOptions lazyOptions = options;
    lazyOptions.streaming = true;
    World lazy = BuildGalaxy(lazyOptions);
    lazy.scene.UpdateWorld(one);
    const FeatureSet features;
    const Vec3d near = lazy.scene.WorldPosition(lazy.sectors[3]) + Vec3d{0, 0, lazy.extents[3]};
    StreamWorld(lazy, Vec3d{1.0e17, 0, 0}, 800.0f, {}, features);
    CHECK(!lazy.orbits[static_cast<std::size_t>(lazy.state[3].starOrbits[0])].active);
    const std::size_t farActive = lazy.stats.activeOrbits;
    CHECK(farActive == 0);
    StreamWorld(lazy, near, 800.0f, {}, features);
    CHECK(lazy.orbits[static_cast<std::size_t>(lazy.state[3].starOrbits[0])].active);

    // Descargar un sistema no se lleva a sus estrellas.
    const std::size_t stars = lazy.state[3].starBodies.size();
    UnloadSystem(lazy, 3);
    CHECK(lazy.state[3].starBodies.size() == stars);
    CHECK(lazy.systems[3]->companions.size() + 1 == stars);
    CHECK(lazy.systems[3]->bodies.empty());
    LoadSystem(lazy, 3);
    CHECK(!lazy.state[3].bodies.empty());
}

static void TestOrbitAdaptation() {
    using namespace astro;
    std::mt19937 rng(91);

    // Limbach y Turner: a mas planetas, orbitas mas circulares.
    CHECK(MedianEccentricity(2) > MedianEccentricity(5));
    CHECK(Near(static_cast<float>(MedianEccentricity(8)), 0.048f, 0.002f));

    const auto measure = [&](int planets, double& pitch, double& eccentricity, double& inclination) {
        GeneratorOptions options;
        options.planetsMin = options.planetsMax = planets;
        options.moonsMax = 0;
        options.cometsMax = 0;
        std::vector<double> ratios, es, tilts;
        for (int i = 0; i < 1500; ++i) {
            const StarSystemData s = GenerateSystem(StarClass::G, rng, options);
            for (std::size_t b = 0; b < s.bodies.size(); ++b) {
                es.push_back(s.bodies[b].orbit.eccentricity);
                tilts.push_back(s.bodies[b].orbit.inclination);
                if (b > 0) ratios.push_back(s.bodies[b].orbit.semiMajorAxis / s.bodies[b - 1].orbit.semiMajorAxis);
            }
        }
        const auto median = [](std::vector<double>& v) {
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
            return v[v.size() / 2];
        };
        pitch = median(ratios);
        eccentricity = median(es);
        inclination = median(tilts);
    };
    double pitch3, e3, tilt3, pitch12, e12, tilt12;
    measure(3, pitch3, e3, tilt3);
    measure(12, pitch12, e12, tilt12);
    std::printf("  3 planetas: paso %.2f, e %.3f   12 planetas: paso %.2f, e %.3f\n", pitch3, e3, pitch12, e12);
    CHECK(pitch12 < pitch3);
    CHECK(e12 < e3 * 0.5);
    CHECK(tilt12 < tilt3);

    // Con poco espacio caben menos: una companera cerca deja sitio a pocos planetas, y nunca fuera.
    int freePlanets = 0, boundPlanets = 0;
    GeneratorOptions freeOptions, boundOptions;
    freeOptions.planetsMin = freeOptions.planetsMax = boundOptions.planetsMin = boundOptions.planetsMax = 8;
    freeOptions.moonsMax = boundOptions.moonsMax = 0;
    freeOptions.cometsMax = boundOptions.cometsMax = 0;
    boundOptions.layout = StarLayout::Wide;
    for (int i = 0; i < 400; ++i) {
        freePlanets += static_cast<int>(GenerateSystem(StarClass::G, rng, freeOptions).bodies.size());
        const StarSystemData s = GenerateSystem(StarClass::G, rng, boundOptions);
        for (const BodyData& b : s.bodies) boundPlanets += b.host == 1 ? 1 : 0;  // la estrella principal
        CHECK(CheckStability(s).empty());
    }
    std::printf("  planetas por estrella: sola %.2f, con companera %.2f\n", freePlanets / 400.0, boundPlanets / 400.0);
    CHECK(boundPlanets < freePlanets);
}

static void TestBeltProfiles() {
    using namespace astro;
    const double mu = kGravity * kSolarMass;
    const auto make = [&](BeltProfile p) {
        BeltOptions base;
        base.families = 200;
        base.dust = 100;
        AsteroidBelt belt = MakeBelt(2.0 * kAstronomicalUnit, 3.0 * kAstronomicalUnit, mu, 2.7 * kAstronomicalUnit, 5, OptionsFor(p, base));
        return belt;
    };
    const AsteroidBelt sparse = make(BeltProfile::Sparse), regular = make(BeltProfile::Regular), dense = make(BeltProfile::Dense), rubble = make(BeltProfile::Rubble), giants = make(BeltProfile::Giants);
    CHECK(BeltRockDensity(dense) > BeltRockDensity(regular));
    CHECK(BeltRockDensity(regular) > BeltRockDensity(sparse));
    CHECK(BeltRockCount(dense) > BeltRockCount(sparse) * 3);
    std::printf("  rocas por millon de km3: disperso %.3f, regular %.3f, denso %.3f, escombros %.3f, gigantes %.3f\n", BeltRockDensity(sparse), BeltRockDensity(regular), BeltRockDensity(dense), BeltRockDensity(rubble), BeltRockDensity(giants));

    // Los tamanos cambian: grava fina en unos, moles de decenas de km en otros.
    const auto largest = [](const AsteroidBelt& belt) {
        float biggest = 0;
        for (const AsteroidFamily& f : belt.families)
            for (const AsteroidRock& r : GenerateRocks(f)) biggest = std::max(biggest, r.radius);
        return biggest;
    };
    CHECK(largest(rubble) <= 2.5f);
    CHECK(largest(giants) > 40.0f && largest(giants) <= 90.0f);
    bool inRange = true;
    float smallest = 1.0e9f;
    for (const AsteroidRock& r : GenerateRocks(giants.families[0])) {
        inRange = inRange && r.radius >= 2.0f && r.radius <= 90.0f;
        smallest = std::min(smallest, r.radius);
    }
    CHECK(inRange);
    CHECK(BeltMass(giants) / static_cast<double>(BeltRockCount(giants)) > BeltMass(rubble) / static_cast<double>(BeltRockCount(rubble)) * 100.0);

    // Dentro de un cinturon no todos los cumulos son iguales.
    int fewest = 1 << 30, most = 0;
    for (const AsteroidFamily& f : regular.families) {
        fewest = std::min(fewest, f.rockCount);
        most = std::max(most, f.rockCount);
    }
    CHECK(most > fewest * 2);

    // Un campo suelto admite cualquier tamano.
    const AsteroidBelt custom = MakeField(0, 300.0f, 50, AsteroidType::Metal, 9, 1.0f, 3.0f);
    bool customSizes = true;
    for (const AsteroidRock& r : GenerateRocks(custom.families[0])) customSizes = customSizes && r.radius >= 1.0f && r.radius <= 3.0f;
    CHECK(customSizes);

    // Planificar es determinista y da asteroides con nombre dentro del campo.
    std::mt19937 rng(4);
    GeneratorOptions options;
    options.planetsMin = options.planetsMax = 6;
    StarSystemData a = GenerateSystem(StarClass::G, rng, options);
    StarSystemData b = a;
    PlanBelts(a, 11);
    PlanBelts(b, 11);
    CHECK(!a.belts.empty());
    CHECK(a.bodies.size() == b.bodies.size());
    CHECK(CheckStability(a).empty());
    bool inside = true;
    int notable = 0;
    for (const BodyData& body : a.bodies) {
        if (body.kind != PlanetClass::Asteroid) continue;
        ++notable;
        const BeltData& belt = a.belts[static_cast<std::size_t>(body.belt)];
        inside = inside && body.orbit.semiMajorAxis >= belt.inner && body.orbit.semiMajorAxis <= belt.outer;
    }
    CHECK(notable > 0);
    CHECK(inside);
    CHECK(BodyName(a, a.bodies.size() - 1, "S").find("S asteroide") == 0);

    // En la galaxia aparecen perfiles distintos.
    GalaxyOptions galaxy;
    galaxy.systems = 30;
    World w = BuildGalaxy(galaxy);
    std::array<int, static_cast<std::size_t>(BeltProfile::Count)> seen{};
    for (const AsteroidBelt& belt : w.belts) ++seen[static_cast<std::size_t>(belt.profile)];
    int kinds = 0;
    for (const int count : seen) kinds += count > 0 ? 1 : 0;
    CHECK(kinds == static_cast<int>(BeltProfile::Count));
}

static void TestAsteroidSelection() {
    using namespace astro;
    JobSystem one(0);
    GalaxyOptions options;
    options.systems = 3;
    options.planetsMin = options.planetsMax = 6;
    options.maxStars = 1;
    World w = BuildGalaxy(options);
    UpdateOrbits(w.orbits, 0.0, w.scene, one);
    w.scene.UpdateWorld(one);
    const FeatureSet features;

    // Cada campo es un cuerpo con nombre, en el punto del anillo mas cercano a la camara.
    CHECK(!w.state[0].beltSlots.empty());
    const int slot = w.state[0].beltSlots[0];
    const AsteroidBelt& belt = w.belts[static_cast<std::size_t>(slot)];
    const int beltBody = belt.body;
    CHECK(w.bodies[static_cast<std::size_t>(beltBody)].kind == PlanetClass::Belt);
    const Vec3d star = w.scene.WorldPosition(w.sectors[0]);
    const Vec3d camera = star + w.scene.World(w.sectors[0]).TransformDirection(Vec3d{belt.outer * 4.0, -belt.outer, 0.0});
    StreamWorld(w, camera, 800.0f, {}, features);
    w.scene.UpdateWorld(one);
    const Vec3d label = w.scene.WorldPosition(w.bodies[static_cast<std::size_t>(beltBody)].node);
    CHECK(NearD(Length(label - star), belt.outer, belt.outer * 1e-9));
    CHECK(Length(label - camera) < Length(star - camera));

    // Aparece en la lista; sus asteroides, solo con el campo seleccionado.
    Camera cam;
    cam.SetViewport(1600, 900, 800.0f, 1.0f);
    cam.SetPosition(star + Vec3d{0, -w.extents[0] * 0.6, -w.extents[0] * 0.8});
    cam.SetYawPitchRoll(0.0f, 0.6f, 0.0f);
    HudFrame hud;
    BuildHud(w, cam, -1, hud);
    bool listed = false, notableListed = false;
    for (const HudListEntry& e : hud.list) {
        listed = listed || e.body == beltBody;
        notableListed = notableListed || w.bodies[static_cast<std::size_t>(e.body)].kind == PlanetClass::Asteroid;
    }
    CHECK(listed);
    CHECK(!notableListed);
    BuildHud(w, cam, beltBody, hud);
    int inList = 0;
    for (const HudListEntry& e : hud.list) inList += w.bodies[static_cast<std::size_t>(e.body)].parent == beltBody ? 1 : 0;
    CHECK(inList > 0);
    CHECK(hud.selected == beltBody);
    CHECK(w.catalog->belts[static_cast<std::size_t>(slot)].rocks == BeltRockCount(belt));
    CHECK(w.catalog->belts[static_cast<std::size_t>(slot)].notable == inList);

    // Un asteroide con nombre es un cuerpo normal, con orbita, ficha y malla propia.
    int notable = -1;
    for (const int b : w.state[0].asteroids) notable = b;
    CHECK(notable >= 0);
    const Body& asteroid = w.bodies[static_cast<std::size_t>(notable)];
    CHECK(BodyInfo(w, asteroid) != nullptr);
    CHECK(asteroid.orbit >= 0);
    BuildHud(w, cam, notable, hud);
    CHECK(hud.selectedSpeed > 0.0);

    // Una roca cualquiera se apunta con un rayo y pasa a ser un cuerpo mientras dure.
    AsteroidLayerSettings settings;
    settings.nearRange = 20000.0f;
    AsteroidLayer layer(w, settings);
    FrameInput input;
    input.scene = &w.scene;
    input.renderables = &w.renderables;
    input.layers = {&layer};
    input.simTime = 2.0e7;
    w.simTime = input.simTime;
    input.camera.SetViewport(1600, 900, 800.0f, 5e-5f);
    Vec3d family;
    CHECK(AsteroidLayer::NearestFamily(w, star, input.simTime, family, slot));
    input.camera.SetPosition(family + Vec3d{0, 0, -200.0});
    FrameBuilder builder;
    FramePacket packet;
    builder.Build(input, one, packet);
    CHECK(layer.CachedFamilies() >= 1);

    RockPick pick;
    CHECK(!layer.PickRock(family + Vec3d{0, 0, -200.0}, Vec3d{0, 0, -1.0}, input.simTime, 0.002f, pick));  // hacia atras no hay nada
    CHECK(layer.PickRock(family + Vec3d{0, 0, -200.0}, Vec3d{0, 0, 1.0}, input.simTime, 0.05f, pick));
    if (pick.rock >= 0) {
        const std::size_t nodes = w.scene.NodeCount();
        const std::size_t bodies = w.bodies.size();
        const int rock = PromoteRock(w, pick.belt, pick.family, pick.rock, pick.data);
        PublishWorld(w);
        UpdateOrbits(w.orbits, input.simTime, w.scene, one);
        w.scene.UpdateWorld(one);
        const Body& body = w.bodies[static_cast<std::size_t>(rock)];
        const AsteroidBelt& owner = w.belts[static_cast<std::size_t>(pick.belt)];
        const Vec3d expected = w.scene.WorldPosition(owner.parent) + w.scene.World(owner.parent).TransformDirection(OrbitPosition(owner.families[static_cast<std::size_t>(pick.family)].orbit, input.simTime)) + Vec3d(pick.data.offset);
        CHECK(Length(w.scene.WorldPosition(body.node) - expected) < 1e-3);
        CHECK(NearD(body.radius, pick.data.radius, 1e-3));
        CHECK(w.catalog->rockData[static_cast<std::size_t>(body.extra)].radius > 0.0);
        CHECK(body.parent == owner.body);
        BuildHud(w, cam, rock, hud);
        bool labelled = false;
        for (const HudLabel& l : hud.labels) labelled = labelled || l.body == rock;
        CHECK(hud.selected == rock);
        (void)labelled;

        ReleaseRock(w, rock);
        CHECK(w.scene.NodeCount() == nodes);
        CHECK(!Alive(w.bodies[static_cast<std::size_t>(rock)]));
        const int again = PromoteRock(w, pick.belt, pick.family, pick.rock, pick.data);
        CHECK(w.bodies.size() <= bodies + 1);
        UnloadSystem(w, w.bodies[static_cast<std::size_t>(again)].system);  // sin fugas
        CHECK(w.state[0].rocks.empty());
    }
}

static void TestImpactOrbits() {
    using namespace astro;
    const double mu = kGravity * kSolarMass;
    std::mt19937 rng(13);

    // De la orbita al estado y vuelta, en cualquier orientacion.
    bool roundTrip = true;
    for (int i = 0; i < 3000; ++i) {
        OrbitalElements e;
        e.semiMajorAxis = LogUniform(rng, 1e7, 1e10);
        e.eccentricity = i % 3 == 0 ? 0.0 : Uniform(rng, 0.0, 0.95);
        e.inclination = i % 5 == 0 ? 0.0 : Uniform(rng, 0.0, kPi);
        e.ascendingNode = Uniform(rng, 0, 6.28);
        e.argPeriapsis = Uniform(rng, 0, 6.28);
        e.meanAnomaly0 = Uniform(rng, 0, 6.28);
        e.mu = mu;
        const KeplerOrbit o = MakeOrbit(kNoNode, e);
        const double t = Uniform(rng, 0, 1e9);
        const Vec3d r = OrbitPosition(o, t), v = OrbitVelocity(o, t);
        OrbitalElements back;
        roundTrip = roundTrip && OrbitFromState(r, v, mu, t, back);
        const KeplerOrbit again = MakeOrbit(kNoNode, back);
        const double later = t + Uniform(rng, 0, 1e7);
        roundTrip = roundTrip && Length(OrbitPosition(again, later) - OrbitPosition(o, later)) < e.semiMajorAxis * 1e-6;
        roundTrip = roundTrip && Length(OrbitPosition(again, t) - r) < e.semiMajorAxis * 1e-9;
    }
    CHECK(roundTrip);

    // Lambert: la orbita calculada llega al punto pedido en el tiempo pedido.
    bool arrives = true;
    int solved = 0;
    for (int i = 0; i < 2000; ++i) {
        const double ang1 = Uniform(rng, 0, 6.28), ang2 = ang1 + Uniform(rng, 0.2, 2.8);
        const double a1 = LogUniform(rng, 5e7, 1e9), a2 = LogUniform(rng, 5e7, 1e9);
        const Vec3d r1{std::cos(ang1) * a1, Uniform(rng, -1e6, 1e6), std::sin(ang1) * a1};
        const Vec3d r2{std::cos(ang2) * a2, Uniform(rng, -1e6, 1e6), std::sin(ang2) * a2};
        const double dt = LogUniform(rng, 1e7, 2e8);  // con menos tiempo la orbita seria hiperbolica
        Vec3d departure, arrival;
        if (!SolveLambert(r1, r2, dt, mu, departure, arrival)) continue;
        OrbitalElements e;
        if (!OrbitFromState(r1, departure, mu, 1000.0, e)) continue;
        ++solved;
        const KeplerOrbit o = MakeOrbit(kNoNode, e);
        arrives = arrives && Length(OrbitPosition(o, 1000.0 + dt) - r2) < 1.0e3;
        arrives = arrives && Length(OrbitVelocity(o, 1000.0 + dt) - arrival) < 1.0e-2;
    }
    std::printf("  Lambert resuelto en %d de 2000 casos\n", solved);
    CHECK(solved > 500);
    CHECK(arrives);
}

static void TestImpactPhysics() {
    using namespace astro;
    const auto sphere = [](double radius, double density) { return 4.0 / 3.0 * kPi * radius * radius * radius * density; };
    const double rock = 3.0e12;

    // Q* tiene su minimo en cuerpos de unos cientos de metros: los mas faciles de romper.
    const double small = DisruptionThreshold(0.001, rock), weak = DisruptionThreshold(0.3, rock), big = DisruptionThreshold(100.0, rock);
    CHECK(weak < small);
    CHECK(weak < big);

    // Una roca contra un planeta deja un crater; el planeta casi no nota su masa.
    const double earth = kEarthMass;
    const ImpactResult crater = ResolveImpact(sphere(20.0, rock), 20.0, earth, kEarthRadius, 20.0);
    CHECK(crater.outcome == ImpactOutcome::Crater);
    CHECK(crater.energy > 1.0e25 && crater.energy < 1.0e26);  // Chicxulub, con un cuarto de radio: unos 1e24 J

    // Lo equivalente al impacto que formo la Luna: mucha energia, casi toda la masa se queda.
    const ImpactResult giant = ResolveImpact(0.1 * earth, 3400.0, earth, kEarthRadius, 10.0);
    CHECK(giant.outcome == ImpactOutcome::Merger);

    // Dos rocas iguales a la velocidad del cinturon se deshacen.
    const ImpactResult shattered = ResolveImpact(sphere(50.0, rock), 50.0, sphere(50.0, rock), 50.0, 5.0);
    CHECK(shattered.outcome == ImpactOutcome::Shattered);
    CHECK(shattered.remnantMass < 0.1 * 2.0 * sphere(50.0, rock));

    // Mas velocidad, menos resto: el resultado no se puede invertir subiendo la energia.
    double last = 1.0e300;
    bool falling = true;
    for (double v = 1.0; v < 200.0; v *= 1.4) {
        const ImpactResult r = ResolveImpact(earth * 0.5, 5000.0, earth, kEarthRadius, v);
        falling = falling && r.remnantMass <= last;
        last = r.remnantMass;
    }
    CHECK(falling);

    // Una historia es pura: los mismos datos dan el mismo resultado.
    StarSystemData system;
    system.bodies.resize(2);
    system.bodies[0].kind = PlanetClass::Rock;
    system.bodies[0].mass = earth;
    system.bodies[0].radius = kEarthRadius;
    system.bodies[0].orbit.semiMajorAxis = kAstronomicalUnit;
    system.bodies[1].kind = PlanetClass::Asteroid;
    system.bodies[1].mass = earth;
    system.bodies[1].radius = kEarthRadius;
    SystemHistory a, b;
    const ImpactResult ra = RecordImpact(a, system, 1, earth, kEarthRadius, 0, earth, kEarthRadius, 100.0, 5);
    const ImpactResult rb = RecordImpact(b, system, 1, earth, kEarthRadius, 0, earth, kEarthRadius, 100.0, 5);
    CHECK(ra.remnantMass == rb.remnantMass);
    CHECK(a.changes.size() == 2 && a.changes[0].destroyed);
    CHECK(a.changes[1].destroyed || a.changes[1].mass > 0.0);
    CHECK(a.debris.size() == 1);
    CHECK(a.debris[0].debris && a.debris[0].profile == BeltProfile::Rubble);
    CHECK(a.debris[0].inner < kAstronomicalUnit && a.debris[0].outer > kAstronomicalUnit);
}

static void TestCollisions() {
    using namespace astro;
    const auto makeEngine = [](std::uint32_t seed, bool streaming, bool impacts = false) {
        EngineConfig config;
        config.galaxy.systems = 9;
        config.galaxy.seed = seed;
        config.galaxy.planetsMin = config.galaxy.planetsMax = 6;
        config.galaxy.streaming = streaming;
        config.galaxy.impacts = impacts;
        config.galaxy.maxStars = 1;
        config.blasts.meshes = {1, 2};
        config.blasts.duration = 4.0f;
        return std::make_unique<Engine>(config);
    };
    const auto planetOf = [](const World& w, int system, std::size_t index) {
        std::size_t seen = 0;
        for (const int b : w.state[static_cast<std::size_t>(system)].bodies) {
            const Body& body = w.bodies[static_cast<std::size_t>(b)];
            if (body.kind == PlanetClass::Belt || body.kind == PlanetClass::Asteroid || body.kind == PlanetClass::Comet || body.kind == PlanetClass::Moon) continue;
            if (seen++ == index) return b;
        }
        return -1;
    };

    // Un asteroide cambia de orbita y llega a la hora exacta.
    auto first = makeEngine(44, false);
    Engine& engine = *first;
    World& w = engine.GetWorld();
    Camera camera;
    camera.SetViewport(800, 450, 400.0f, kNearPlaneKm);
    camera.SetPosition(w.scene.WorldPosition(w.sectors[0]) + Vec3d{0, 0, -w.extents[0]});

    double now = 1.0e6;
    engine.Advance(camera.Position(), camera.FocalPx(), now);
    const int rockBody = w.state[0].asteroids.front();
    const int target = planetOf(w, 0, 2);
    CHECK(target >= 0);
    std::string error;
    CHECK(engine.Collisions().Schedule(rockBody, target, now, 40.0 * kDay, &error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    CHECK(engine.Collisions().Pending().size() == 1);
    CHECK(!engine.Collisions().Schedule(rockBody, target, now, 0.0, &error));  // ya va de camino
    CHECK(!engine.Collisions().Schedule(-1, target, now, 0.0, &error));
    CHECK(!engine.Collisions().Schedule(rockBody, rockBody, now, 0.0, &error));
    CHECK(!engine.Collisions().Schedule(w.state[0].starBody, target, now, 0.0, &error));  // una estrella no se lanza

    const double when = engine.Collisions().Pending()[0].time;
    const Body attackerBody = w.bodies[static_cast<std::size_t>(rockBody)];
    const KeplerOrbit newOrbit = w.orbits[static_cast<std::size_t>(attackerBody.orbit)];
    const KeplerOrbit targetOrbit = w.orbits[static_cast<std::size_t>(w.bodies[static_cast<std::size_t>(target)].orbit)];
    CHECK(Length(OrbitPosition(newOrbit, when) - OrbitPosition(targetOrbit, when)) < 1.0e3);
    const Vec3d before = w.scene.WorldPosition(attackerBody.node);
    UpdateOrbits(w.orbits, now, w.scene, engine.Jobs());
    w.scene.UpdateWorld(engine.Jobs());
    CHECK(Length(w.scene.WorldPosition(attackerBody.node) - before) < 1.0);  // no salta: solo cambia de rumbo

    // Antes de tiempo no pasa nada.
    engine.Advance(camera.Position(), camera.FocalPx(), when - 2.0 * kDay);
    CHECK(Alive(w.bodies[static_cast<std::size_t>(rockBody)]));
    CHECK(engine.Collisions().Reports().empty());

    const astro::BodyData targetBefore = *BodyInfo(w, w.bodies[static_cast<std::size_t>(target)]);
    engine.Advance(camera.Position(), camera.FocalPx(), when + 1.0);
    CHECK(engine.Collisions().Pending().empty());
    CHECK(engine.Collisions().Reports().size() == 1);
    CHECK(!Alive(w.bodies[static_cast<std::size_t>(rockBody)]));
    CHECK(Alive(w.bodies[static_cast<std::size_t>(target)]));
    CHECK(engine.Collisions().ActiveBlasts() == 1);
    const ImpactResult crater = engine.Collisions().Reports()[0].result;
    CHECK(crater.outcome == ImpactOutcome::Crater);
    const Renderable& scarred = w.renderables[static_cast<std::size_t>(w.bodies[static_cast<std::size_t>(target)].renderable)];
    CHECK(scarred.scarSize > 0.0f);
    CHECK(Near(Length(scarred.scarDirection), 1.0f, 1e-3f));
    CHECK(BodyInfo(w, w.bodies[static_cast<std::size_t>(target)])->mass > targetBefore.mass);

    // La explosion se ve aunque el cuerpo atacante ya no exista.
    const Body& hit = w.bodies[static_cast<std::size_t>(target)];
    camera.SetPosition(w.scene.WorldPosition(hit.node) + Vec3d{0, 0, -hit.radius * 6.0});
    FramePacket packet;
    engine.Advance(camera.Position(), camera.FocalPx(), when + 2.0, {}, 0.3);
    camera.SetPosition(w.scene.WorldPosition(hit.node) + Vec3d{0, 0, -hit.radius * 6.0});
    engine.Build(camera, 1.0, packet);
    CHECK(packet.stats.blastsVisible == 1);
    CHECK(!packet.nebulae.empty());
    CHECK(packet.stats.meshesVisible > 0);
    std::size_t lines = packet.lines.size();
    CHECK(lines > 0);
    bool hot = false;
    for (const SpriteInstance& s : packet.sprites) hot = hot || s.tint.r > 1.0f;
    CHECK(hot);  // mas brillante que el blanco: de eso se alimenta el bloom
    bool heated = false;
    for (const SphereInstance& s : packet.spheres) heated = heated || s.scarHeat > 0.0f;
    CHECK(heated);

    engine.Commands().Execute("desactivar colisiones");
    engine.Build(camera, 1.0, packet);
    CHECK(packet.stats.blastsVisible == 0);
    engine.Commands().Execute("activar colisiones");

    // Termina y no deja nada detras, salvo el crater.
    const std::size_t nodes = w.scene.NodeCount();
    for (int i = 0; i < 30; ++i) engine.Advance(camera.Position(), camera.FocalPx(), when + 3.0, {}, 0.2);
    CHECK(engine.Collisions().ActiveBlasts() == 0);
    CHECK(w.scene.NodeCount() == nodes - 1);
    CHECK(scarred.scarSize > 0.0f);
    CHECK(Near(scarred.scarHeat, 0.0f, 0.02f));

    // Dos asteroides se rompen del todo y nace un campo de escombros en su orbita.
    const int small = w.state[0].asteroids[0], big = w.state[0].asteroids[1];
    const double start = when + 10.0;
    CHECK(engine.Collisions().Schedule(small, big, start, 30.0 * kDay, &error));
    const double second = engine.Collisions().Pending()[0].time;
    const std::size_t beltsBefore = w.state[0].beltSlots.size();
    engine.Advance(camera.Position(), camera.FocalPx(), second + 1.0, {}, 0.0);
    CHECK(engine.Collisions().Reports().size() == 2);
    const ImpactOutcome outcome = engine.Collisions().Reports().back().result.outcome;
    CHECK(outcome == ImpactOutcome::Shattered || outcome == ImpactOutcome::Partial || outcome == ImpactOutcome::Merger);
    if (outcome == ImpactOutcome::Shattered) {
        CHECK(Alive(w.bodies[static_cast<std::size_t>(big)]));  // aun no: primero se agrieta
        engine.Advance(camera.Position(), camera.FocalPx(), second + 2.0, {}, 2.0);
        CHECK(!Alive(w.bodies[static_cast<std::size_t>(big)]));
    }
    for (int i = 0; i < 30; ++i) engine.Advance(camera.Position(), camera.FocalPx(), second + 3.0, {}, 0.2);
    CHECK(engine.Collisions().ActiveBlasts() == 0);
    if (outcome != ImpactOutcome::Merger) {
        CHECK(w.state[0].beltSlots.size() == beltsBefore + 1);
        CHECK(w.catalog->belts[static_cast<std::size_t>(w.state[0].beltSlots.back())].debris);
    }

    // Un sistema distinto no se alcanza, y un cuerpo no cargado tampoco.
    const int foreign = w.state[1].starBody;
    CHECK(!engine.Collisions().Schedule(planetOf(w, 0, 0), foreign, now, 0.0, &error));

    // Lo destruido sigue destruido: descargar y volver a cargar no lo resucita.
    const int data = w.bodies[static_cast<std::size_t>(target)].data;
    const std::size_t historySize = w.history[0].changes.size();
    const std::size_t asteroidCount = w.state[0].asteroids.size();
    UnloadSystem(w, 0);
    LoadSystem(w, 0);
    CHECK(w.history[0].changes.size() == historySize);
    CHECK(w.state[0].asteroids.size() == asteroidCount);
    bool scarKept = false;
    for (const int b : w.state[0].bodies)
        if (w.bodies[static_cast<std::size_t>(b)].data == data) scarKept = w.renderables[static_cast<std::size_t>(w.bodies[static_cast<std::size_t>(b)].renderable)].scarSize > 0.0f;
    CHECK(scarKept);

    // Un planeta contra otro: se ve venir, se funde o se rompe, y su historia queda.
    auto second2 = makeEngine(45, false);
    World& w2 = second2->GetWorld();
    second2->Advance(camera.Position(), camera.FocalPx(), 0.0);
    const int p1 = planetOf(w2, 0, 1), p2 = planetOf(w2, 0, 2);
    std::size_t moonsBefore = w2.state[0].moons.size();
    (void)moonsBefore;
    CHECK(second2->Collisions().Schedule(p1, p2, 0.0, 0.0, &error));
    const double crash = second2->Collisions().Pending()[0].time;
    second2->Advance(camera.Position(), camera.FocalPx(), crash + 1.0, {}, 0.1);
    CHECK(second2->Collisions().Reports().size() == 1);
    CHECK(!Alive(w2.bodies[static_cast<std::size_t>(p1)]));
    bool orphan = false;
    for (const int moon : w2.state[0].moons) orphan = orphan || !Alive(w2.bodies[static_cast<std::size_t>(w2.bodies[static_cast<std::size_t>(moon)].parent)]);
    CHECK(!orphan);  // sus lunas se van con el

    // Choque escrito en el sistema: con la camara delante ocurre en vivo; sin nadie, igual.
    auto live = makeEngine(46, false, true);
    World& w3 = live->GetWorld();
    const double impactTime = 25.0 * kDay;
    live->Advance(camera.Position(), camera.FocalPx(), impactTime - 10.0);
    int impactor = -1;
    for (const int b : w3.state[7].asteroids)
        if (BodyInfo(w3, w3.bodies[static_cast<std::size_t>(b)])->impactTime >= 0.0) impactor = b;
    CHECK(impactor >= 0);
    live->Advance(camera.Position(), camera.FocalPx(), impactTime + 10.0);
    CHECK(live->Collisions().Reports().size() == 1);
    const ImpactOutcome liveOutcome = live->Collisions().Reports().empty() ? ImpactOutcome::Crater : live->Collisions().Reports()[0].result.outcome;

    GalaxyOptions quiet;
    quiet.systems = 9;
    quiet.seed = 46;
    quiet.planetsMin = quiet.planetsMax = 6;
    quiet.streaming = true;
    quiet.impacts = true;
    quiet.maxStars = 1;
    World late = BuildGalaxy(quiet);
    late.simTime = impactTime + 1.0e6;
    LoadSystem(late, 7);
    bool gone = true;
    for (const int b : late.state[7].asteroids) gone = gone && BodyInfo(late, late.bodies[static_cast<std::size_t>(b)])->impactTime < 0.0;
    CHECK(gone);
    CHECK(late.history[7].changes.size() == 2);
    CHECK(late.history[7].changes[1].destroyed == (liveOutcome == ImpactOutcome::Shattered));
    CHECK(late.history[7].changes[1].molten == (liveOutcome == ImpactOutcome::Merger || liveOutcome == ImpactOutcome::Partial));

    // Deteccion: un cometa cuya orbita corta la de un planeta justo en ese punto.
    auto sweep = makeEngine(47, false);
    World& w4 = sweep->GetWorld();
    sweep->Advance(camera.Position(), camera.FocalPx(), 0.0);
    const int planetBody = planetOf(w4, 0, 3);
    const int comet = w4.state[0].asteroids.front();
    const KeplerOrbit po = w4.orbits[static_cast<std::size_t>(w4.bodies[static_cast<std::size_t>(planetBody)].orbit)];
    const double crossing = 20.0 * kDay;
    OrbitalElements crossingOrbit;
    CHECK(OrbitFromState(OrbitPosition(po, crossing), OrbitVelocity(po, crossing) * 0.8 + Vec3d{0.0, 3.0, 0.0}, po.elements.mu, crossing, crossingOrbit));
    const NodeId cometNode = w4.bodies[static_cast<std::size_t>(comet)].node;
    w4.orbits[static_cast<std::size_t>(w4.bodies[static_cast<std::size_t>(comet)].orbit)] = MakeOrbit(cometNode, crossingOrbit);
    double t = crossing - 2.0 * kDay;
    sweep->Advance(camera.Position(), camera.FocalPx(), t);
    CHECK(sweep->Collisions().Reports().empty());
    for (int i = 0; i < 400 && sweep->Collisions().Reports().empty(); ++i) {
        t += 1800.0;
        sweep->Advance(camera.Position(), camera.FocalPx(), t);
    }
    CHECK(sweep->Collisions().Reports().size() == 1);
    CHECK(t <= crossing + 3600.0);

    // Dos rocas de un campo se lanzan una contra otra y desaparecen de la lista.
    auto rocks = makeEngine(48, false);
    World& w5 = rocks->GetWorld();
    rocks->Advance(camera.Position(), camera.FocalPx(), 1000.0);
    Vec3d field;
    CHECK(AsteroidLayer::NearestFamily(w5, w5.scene.WorldPosition(w5.sectors[0]), 1000.0, field));
    Camera near;
    near.SetViewport(800, 450, 400.0f, kNearPlaneKm);
    near.SetPosition(field + Vec3d{0, 0, -60.0});
    FramePacket rockPacket;
    rocks->Build(near, 1.0, rockPacket);
    CHECK(rocks->Asteroids().StartRockCollision(near.Position()));
    CHECK(!rocks->Asteroids().StartRockCollision(near.Position()));  // solo una a la vez por familia
    for (int i = 0; i < 40; ++i) rocks->Advance(near.Position(), near.FocalPx(), 1000.0, {}, 0.1);
    CHECK(rocks->Collisions().Reports().size() == 1);
    CHECK(!w5.deadRocks.empty());
    CHECK(rocks->Collisions().ActiveBlasts() == 1);
    rocks->Build(near, 1.0, rockPacket);
    CHECK(rockPacket.stats.blastsVisible == 1);
}

static void TestPicking() {    Camera cam;
    cam.SetViewport(1920, 1080, 960.0f);
    const std::vector<Vec3d> targets{{300, 0, 400}, {10, 5, 400}, {0, 0, -400}, {100, 0, 400}};
    CHECK(PickNearestToCenter(cam, targets.data(), targets.size()) == 1);

    const std::vector<Vec3d> far{{400, 0, 400}, {0, 0, -100}};
    CHECK(PickNearestToCenter(cam, far.data(), far.size()) == -1);
    CHECK(PickNearestToCenter(cam, nullptr, 0) == -1);
}

static void TestSky() {
    const auto a = GenerateSky(400, 42);
    const auto b = GenerateSky(400, 42);
    CHECK(a->size() == 400);
    bool same = true, unit = true;
    Vec3f sum;
    for (std::size_t i = 0; i < a->size(); ++i) {
        same = same && (*a)[i].direction.x == (*b)[i].direction.x;
        unit = unit && Near(Length((*a)[i].direction), 1.0f, 1e-5f);
        sum += (*a)[i].direction;
    }
    CHECK(same);
    CHECK(unit);
    CHECK(Length(sum) / 400.0f < 0.15f);  // distribucion sin sesgo marcado
}

int main() {
    const struct { const char* name; void (*fn)(); } tests[] = {
        {"Algebra de matrices", TestMatrixAlgebra},
        {"Legado: CalcularProyeccion", TestLegacyProjectionGolden},
        {"Legado: espacio de recorte", TestClipSpaceMatchesLegacy},
        {"Legado: tamano de sprites", TestLegacySpriteSize},
        {"Legado: mapa tactico", TestTacticalMapGolden},
        {"Convencion de camara", TestCameraConvention},
        {"Frustum", TestFrustum},
        {"Precision lejos del origen", TestLargeWorldPrecision},
        {"Jerarquia de escena", TestSceneHierarchy},
        {"JobSystem", TestJobSystem},
        {"TripleBuffer", TestTripleBuffer},
        {"FrameBuilder determinista", TestFrameBuilderDeterminism},
        {"Esferas y detalle", TestSphereLod},
        {"Ecuacion de Kepler", TestKeplerSolver},
        {"Movimiento orbital", TestKeplerMotion},
        {"Giro de los cuerpos", TestSpinModel},
        {"Formulas astrofisicas", TestAstroFormulas},
        {"Generador de sistemas", TestSystemGenerator},
        {"Lineas de orbita y apagado", TestOrbitPaths},
        {"Materiales", TestMaterials},
        {"Interfaz", TestHud},
        {"Comandos y funciones", TestCommands},
        {"Retirar nodos", TestSceneRemoval},
        {"Carga bajo demanda", TestStreaming},
        {"Agujeros negros", TestBlackHoles},
        {"Asteroides", TestAsteroids},
        {"Modelos a escala humana", TestModels},
        {"Fachada del motor", TestEngineFacade},
        {"Estrellas multiples", TestMultipleStars},
        {"Orbitas segun planetas y estrellas", TestOrbitAdaptation},
        {"Cinturones distintos", TestBeltProfiles},
        {"Seleccion de campos y rocas", TestAsteroidSelection},
        {"Trayectorias de choque", TestImpactOrbits},
        {"Fisica de los choques", TestImpactPhysics},
        {"Choques en el mundo", TestCollisions},
        {"Seleccion de objetivo", TestPicking},
        {"Cielo procedural", TestSky},
    };

    for (const auto& t : tests) {
        std::printf("[ %s ]\n", t.name);
        std::fflush(stdout);
        t.fn();
    }
    std::printf("\n%d comprobaciones, %d fallos\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
