// minimal_game.cpp - Lo minimo para un juego nuevo sobre la libreria.
//
// Un hilo, una camara fija y una galaxia completa: sistemas con carga bajo demanda, orbitas,
// asteroides, nebulosas, agujeros negros y una bola de 10 m a escala humana.
//
//   epsilon_minimal            termina con Esc o al cerrar la ventana
//   epsilon_minimal 300        termina tras 300 fotogramas
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>

#include "epsilon/Engine.h"
#include "epsilon/SdlGpuBackend.h"

using namespace eps;

int main(int argc, char** argv) {
    const int maxFrames = argc > 1 ? std::atoi(argv[1]) : 0;

    // 1. Plataforma y backend grafico: esto lo decide cada juego.
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window* window = SDL_CreateWindow("Juego nuevo", 1280, 720, SDL_WINDOW_RESIZABLE);
    SdlGpuBackend backend;
    if (!window || !backend.Initialize(window, false)) {
        std::printf("No se pudo iniciar: %s %s\n", SDL_GetError(), backend.LastError().c_str());
        return 1;
    }

    // 2. El motor. Todo lo que aparece aqui se puede cambiar o apagar.
    EngineConfig config;
    config.galaxy.systems = 20;
    config.galaxy.streaming = true;
    config.galaxy.blackHoles = true;
    config.galaxy.nebulae = 2;
    config.asteroids.meshes = {backend.CreateMesh(MakeAsteroid(1)), backend.CreateMesh(MakeAsteroid(2))};
    Engine engine(config);
    engine.Commands().Execute("desactivar grano");

    // 3. Algo a escala humana: una burbuja enganchada a la estrella del primer sistema.
    World& world = engine.GetWorld();
    LocalSpace& space = engine.CreateLocalSpace(world.sectors[0], world.sectors[0]);
    space.anchorOffset = {0.0, 0.0, world.extents[0]};
    auto ball = std::make_shared<ModelAsset>();
    ModelPart part;
    part.mesh = backend.CreateMesh(MakeSphere(5.0f));
    part.radius = 5.0f;
    ball->parts.push_back(part);
    space.Spawn(ball);

    Camera camera;
    FramePacket packet;
    double time = 0;
    bool running = true;
    for (int count = 0; running && (maxFrames == 0 || count < maxFrames); ++count) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
        }

        // 4. Camara: mira a la bola desde 30 m. Con esta posicion, el sistema entero existe.
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        camera.SetViewport(width, height, static_cast<float>(width) * 0.5f, kNearPlaneKm);
        camera.SetPosition(space.ToWorldKm(world.scene, {0.0, 0.0, -30.0}));

        // 5. Un paso de simulacion y un fotograma. Un dia simulado por segundo.
        time += 1.0 / 60.0;
        engine.Advance(camera.Position(), camera.FocalPx(), time * astro::kDay);
        engine.Build(camera, time, packet);
        backend.Render(packet);
    }

    std::printf("Driver: %s  Presentados: %llu  Mallas: %zu  Sistemas cargados: %zu de %zu\n", backend.DriverName().c_str(),
                static_cast<unsigned long long>(backend.PresentedFrames()), packet.stats.meshesVisible, world.stats.loadedSystems, world.state.size());
    const bool presented = backend.PresentedFrames() > 0;
    backend.Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return presented ? 0 : 1;
}
