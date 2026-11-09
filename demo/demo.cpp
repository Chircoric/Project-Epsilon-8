// demo.cpp - Ejemplo de uso del motor sin ventana.
//
//   epsilon_demo render salida.bmp   dibuja una galaxia procedural con el backend de CPU
//   epsilon_demo bench 300000        mide la construccion del fotograma con 1..N hilos
//   epsilon_demo threaded 600        simulacion y render en hilos separados
//   epsilon_demo streaming 5000      coste de un paso con y sin carga bajo demanda
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "epsilon/AstroLayers.h"
#include "epsilon/JobSystem.h"
#include "epsilon/SoftwareBackend.h"
#include "epsilon/TripleBuffer.h"
#include "epsilon/Universe.h"

using namespace eps;
using Clock = std::chrono::steady_clock;

namespace {

int RunRender(const char* path) {
    JobSystem jobs;
    World w = BuildGalaxy({});
    UpdateOrbits(w.orbits, 0.0, w.scene, jobs);
    w.scene.UpdateWorld(jobs);

    FrameInput input;
    input.scene = &w.scene;
    input.renderables = &w.renderables;
    OrbitLayer orbitLayer(w);
    input.layers = {&orbitLayer};
    input.sky = w.sky;
    input.surfaces = w.surfaces;
    input.camera = MakeCamera(w);
    input.time = 3.5;
    input.clearColor = {0.012f, 0.012f, 0.03f, 1.0f};
    input.screenTint = {0.8f, 0.4f, 0.1f, 0.03f};

    FrameBuilder builder;
    FramePacket packet;
    builder.Build(input, jobs, packet);

    SoftwareBackend backend;
    backend.Render(packet);
    if (!backend.SaveBmp(path)) {
        std::printf("No se pudo escribir %s\n", path);
        return 1;
    }

    std::vector<Vec3d> targets;
    for (NodeId s : w.sectors) targets.push_back(w.scene.WorldPosition(s));
    const int picked = PickNearestToCenter(input.camera, targets.data(), targets.size());

    std::printf("Imagen: %s\n", path);
    std::printf("Nodos: %zu  Objetos: %zu  Sprites visibles: %zu  Lotes: %zu  Lineas: %zu  Sistema apuntado: %d\n",
                w.scene.NodeCount(), packet.stats.renderablesTotal, packet.stats.spritesVisible,
                packet.batches.size(), packet.stats.linesVisible, picked);
    return 0;
}

int RunBench(int targetObjects) {
    const int planetsPerSystem = 20;
    const int systems = std::max(1, targetObjects / (planetsPerSystem + 10));
    GalaxyOptions galaxy;
    galaxy.systems = systems;
    galaxy.planetsMin = galaxy.planetsMax = planetsPerSystem;
    galaxy.seed = 1234;
    galaxy.fade = false;  // sin esto, casi todos los planetas quedarian descartados
    World w = BuildGalaxy(galaxy);

    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    std::printf("CPU: %u hilos logicos. Nodos: %zu, objetos: %zu, orbitas: %zu\n", hw, w.scene.NodeCount(),
                w.renderables.size(), w.orbits.size());
    std::printf("%7s %12s %10s %10s %10s\n", "Hilos", "Total (ms)", "Aceleracion", "Eficiencia", "Visibles");

    double baseline = 0;
    std::vector<unsigned> counts;
    for (unsigned t = 1; t < hw; t *= 2) counts.push_back(t);
    counts.push_back(hw);

    for (unsigned threads : counts) {
        JobSystem jobs(threads - 1);
        FrameBuilder builder;
        FramePacket packet;

        FrameInput input;
        input.scene = &w.scene;
        input.renderables = &w.renderables;
        input.camera = MakeCamera(w);
        // La galaxia ocupa todas las direcciones: desde el primer sistema se ve una parte.
        input.camera.SetPosition(w.scene.WorldPosition(w.sectors[0]) + Vec3d{0.0, 0.0, -1.0e9});
        input.camera.SetYawPitchRoll(0.78f, 0.1f, 0.0f);

        const int warmup = 5, frames = 40;
        std::vector<double> samples;
        for (int i = 0; i < warmup + frames; ++i) {
            const auto start = Clock::now();
            input.simTime += astro::kDay / 60.0;
            UpdateOrbits(w.orbits, input.simTime, w.scene, jobs);
            w.scene.UpdateWorld(jobs);
            builder.Build(input, jobs, packet);
            const std::chrono::duration<double, std::milli> ms = Clock::now() - start;
            if (i >= warmup) samples.push_back(ms.count());
        }
        std::sort(samples.begin(), samples.end());
        const double median = samples[samples.size() / 2];
        if (baseline == 0) baseline = median;
        const double speedup = baseline / median;
        std::printf("%7u %12.2f %9.2fx %9.0f%% %10zu\n", threads, median, speedup, 100.0 * speedup / threads,
                    packet.stats.spritesVisible);
    }
    return 0;
}

// Coste de un paso con la camara dentro de un sistema, con y sin carga bajo demanda.
int RunStreaming(int systems) {
    JobSystem jobs;
    std::printf("Sistemas: %d. Paso = decidir que cargar + orbitas + jerarquia + fotograma.\n", systems);
    std::printf("%-26s %12s %10s %12s %12s %10s\n", "Modo", "Paso (ms)", "Orbitas", "Objetos", "Crear (ms)", "RAM (MB)");

    const auto run = [&](const char* name, bool streaming, bool culling) {
        GalaxyOptions options;
        options.systems = systems;
        options.seed = 1234;
        options.streaming = streaming;
        options.belts = false;
        const auto buildStart = Clock::now();
        World w = BuildGalaxy(options);
        const std::chrono::duration<double, std::milli> buildMs = Clock::now() - buildStart;

        FeatureSet features;
        features.Set(Feature::BodyCulling, culling);
        OrbitLayer orbits(w);
        FrameBuilder builder;
        FramePacket packet;
        FrameInput input;
        input.scene = &w.scene;
        input.renderables = &w.renderables;
        input.layers = {&orbits};
        input.camera = MakeCamera(w);
        input.features = features;

        const auto bytes = [&] {
            std::size_t total = w.bodies.capacity() * sizeof(Body) + w.orbits.capacity() * sizeof(KeplerOrbit) + w.renderables.capacity() * sizeof(Renderable);
            total += w.scene.NodeCount() * (sizeof(Mat4d) + sizeof(Transform) + 32);
            for (const auto& s : w.systems) total += s->bodies.capacity() * sizeof(astro::BodyData) + sizeof(astro::StarSystemData);
            return total;
        };

        std::vector<double> samples;
        for (int i = 0; i < 90; ++i) {
            const auto start = Clock::now();
            input.simTime += astro::kDay / 60.0;
            StreamWorld(w, input.camera.Position(), input.camera.FocalPx(), {}, features);
            PublishWorld(w);
            UpdateOrbits(w.orbits, input.simTime, w.scene, jobs);
            w.scene.UpdateWorld(jobs);
            input.surfaces = w.surfaces;
            builder.Build(input, jobs, packet);
            const std::chrono::duration<double, std::milli> ms = Clock::now() - start;
            if (i >= 20) samples.push_back(ms.count());
        }
        std::sort(samples.begin(), samples.end());
        std::printf("%-26s %12.3f %10zu %12zu %12.0f %10.1f\n", name, samples[samples.size() / 2], w.stats.activeOrbits, w.stats.liveRenderables, buildMs.count(),
                    static_cast<double>(bytes()) / (1024.0 * 1024.0));
    };

    run("Todo en memoria", false, false);
    run("Todo + lunas ocultas", false, true);
    run("Bajo demanda + lunas", true, true);
    return 0;
}

int RunThreaded(int frames) {
    GalaxyOptions galaxy;
    galaxy.systems = 200;
    galaxy.seed = 77;
    World w = BuildGalaxy(galaxy);
    JobSystem jobs;
    TripleBuffer<FramePacket> mailbox;
    std::atomic<bool> done{false};

    std::uint64_t rendered = 0, outOfOrder = 0, spritesSeen = 0;
    std::thread renderThread([&] {
        // Consumidor de prueba: simula un render de 16 ms por fotograma.
        std::uint64_t last = 0;
        for (;;) {
            const bool finished = done.load(std::memory_order_acquire);
            if (mailbox.AcquireLatest()) {
                const FramePacket& f = mailbox.ReadSlot();
                if (f.frameNumber <= last) ++outOfOrder;
                last = f.frameNumber;
                spritesSeen += f.sprites.size();
                ++rendered;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            } else if (finished) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
    });

    FrameBuilder builder;
    FrameInput input;
    input.scene = &w.scene;
    input.renderables = &w.renderables;
    input.sky = w.sky;
    input.camera = MakeCamera(w);

    const auto start = Clock::now();
    for (int i = 1; i <= frames; ++i) {
        const double dt = 1.0 / 240.0;  // simulacion mas rapida que el render
        input.time += dt;
        input.simTime = input.time * astro::kDay;
        UpdateOrbits(w.orbits, input.simTime, w.scene, jobs);
        w.scene.UpdateWorld(jobs);
        input.frameNumber = static_cast<std::uint64_t>(i);
        builder.Build(input, jobs, mailbox.WriteSlot());
        mailbox.Publish();
        std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(dt * i)));
    }
    done.store(true, std::memory_order_release);
    renderThread.join();

    std::printf("Simulados: %d  Dibujados: %llu  Omitidos: %llu  Fuera de orden: %llu  Sprites por fotograma: %llu\n",
                frames, static_cast<unsigned long long>(rendered),
                static_cast<unsigned long long>(frames) - rendered, static_cast<unsigned long long>(outOfOrder),
                rendered ? static_cast<unsigned long long>(spritesSeen / rendered) : 0ull);
    return outOfOrder == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "render";
    if (mode == "render") return RunRender(argc > 2 ? argv[2] : "epsilon_demo.bmp");
    if (mode == "bench") return RunBench(argc > 2 ? std::atoi(argv[2]) : 300000);
    if (mode == "threaded") return RunThreaded(argc > 2 ? std::atoi(argv[2]) : 600);
    if (mode == "streaming") return RunStreaming(argc > 2 ? std::atoi(argv[2]) : 5000);
    std::printf("Uso: epsilon_demo render [salida.bmp] | bench [objetos] | threaded [fotogramas] | streaming [sistemas]\n");
    return 2;
}
