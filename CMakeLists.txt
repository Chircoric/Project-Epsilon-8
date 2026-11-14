cmake_minimum_required(VERSION 3.25)
project(EpsilonEngine VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

find_package(Threads REQUIRED)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# --- Modulos -----------------------------------------------------------------
#
#   core    matematicas, camara, escena, hilos, comandos y FrameBuilder. Siempre.
#   astro   orbitas, sistemas, asteroides, nebulosas y agujeros negros.
#   models  mallas, archivos 3D y burbujas a escala humana. No conoce astro.
#   sdl     backend grafico sobre SDL_GPU. Solo conoce core.
#   hud     interfaz sci-fi con Dear ImGui. Requiere astro y sdl.
#   engine  fachada que une astro y models en una sola clase. No conoce SDL.
option(EPSILON_MODULE_ASTRO "Modulo astral" ON)
option(EPSILON_MODULE_MODELS "Modulo de modelos a escala humana" ON)
option(EPSILON_MODULE_HUD "Interfaz sci-fi; requiere EPSILON_WITH_SDL y Dear ImGui" ON)
option(EPSILON_WITH_SDL "Compilar el backend SDL_GPU y la demo con ventana" OFF)
option(EPSILON_BUILD_DEMOS "Compilar ejemplos y pruebas" ${PROJECT_IS_TOP_LEVEL})
option(EPSILON_INSTALL "Generar las reglas de instalacion" ${PROJECT_IS_TOP_LEVEL})
option(EPSILON_ASAN "Compilar con AddressSanitizer" OFF)

set(EPSILON_TARGETS)

# epsilon_add_module(<nombre> <fuentes...>) crea epsilon_<nombre> y el alias epsilon::<nombre>.
function(epsilon_add_module name)
    add_library(epsilon_${name} STATIC ${ARGN})
    add_library(epsilon::${name} ALIAS epsilon_${name})
    set_target_properties(epsilon_${name} PROPERTIES EXPORT_NAME ${name})
    target_include_directories(epsilon_${name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_features(epsilon_${name} PUBLIC cxx_std_20)  # tambien lo exige a quien lo incluya
    if(MSVC)
        target_compile_options(epsilon_${name} PRIVATE /W4 /permissive-)
        target_compile_options(epsilon_${name} PUBLIC /utf-8 /Zc:__cplusplus)
        target_compile_definitions(epsilon_${name} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    if(EPSILON_ASAN AND MSVC)
        target_compile_options(epsilon_${name} PUBLIC /fsanitize=address)
        target_link_options(epsilon_${name} PUBLIC /INCREMENTAL:NO)
    endif()
    list(APPEND EPSILON_TARGETS epsilon_${name})
    set(EPSILON_TARGETS ${EPSILON_TARGETS} PARENT_SCOPE)
endfunction()

epsilon_add_module(core
    engine/src/Camera.cpp
    engine/src/Features.cpp
    engine/src/FrameBuilder.cpp
    engine/src/Humanoid.cpp
    engine/src/JobSystem.cpp
    engine/src/SceneGraph.cpp
    engine/src/SoftwareBackend.cpp
)
target_link_libraries(epsilon_core PUBLIC Threads::Threads)

if(EPSILON_MODULE_ASTRO)
    epsilon_add_module(astro
        engine/src/Asteroids.cpp
        engine/src/Astro.cpp
        engine/src/AstroLayers.cpp
        engine/src/BlackHole.cpp
        engine/src/Collisions.cpp
        engine/src/Kepler.cpp
        engine/src/Universe.cpp
    )
    target_link_libraries(epsilon_astro PUBLIC epsilon_core)
endif()

if(EPSILON_MODULE_MODELS)
    epsilon_add_module(models engine/src/Models.cpp)
    target_link_libraries(epsilon_models PUBLIC epsilon_core)
    # glTF es opcional: sin cgltf, el motor sigue leyendo OBJ.
    find_path(EPSILON_CGLTF_DIR cgltf.h)
    if(EPSILON_CGLTF_DIR)
        target_include_directories(epsilon_models PRIVATE $<BUILD_INTERFACE:${EPSILON_CGLTF_DIR}>)
        target_compile_definitions(epsilon_models PRIVATE EPSILON_HAS_GLTF _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

# La fachada solo existe si estan los dos mundos: es lo que los une.
if(EPSILON_MODULE_ASTRO AND EPSILON_MODULE_MODELS)
    epsilon_add_module(engine engine/src/Engine.cpp)
    target_link_libraries(epsilon_engine PUBLIC epsilon_astro epsilon_models)
endif()

# El juego: reglas sin ventana ni GPU, para poder probarlas solas.
if(EPSILON_BUILD_DEMOS AND TARGET epsilon_engine)
    add_library(epsilon_game_logic STATIC
        game/src/Economy.cpp
        game/src/Ship.cpp
        game/src/HullZones.cpp
        game/src/ShipModules.cpp
        game/src/Armament.cpp
        game/src/Stations.cpp
        game/src/Anomalies.cpp
        game/src/Combat.cpp
        game/src/Defense.cpp
        game/src/OnFoot.cpp
        game/src/SaveGame.cpp
        game/src/Settings.cpp
        game/src/Session.cpp
        game/src/Net.cpp
        game/src/Tutorial.cpp
        game/src/GameModels.cpp
        game/src/GameLayer.cpp)
    target_include_directories(epsilon_game_logic PUBLIC game/include)
    target_link_libraries(epsilon_game_logic PUBLIC epsilon_engine)
    # La red (docs/multijugador.md, fase 6) va sobre ENet, de vcpkg. Sin el, el juego compila igual y
    # solo queda el transporte en memoria.
    find_package(unofficial-enet CONFIG QUIET)
    if(TARGET unofficial::enet::enet)
        target_sources(epsilon_game_logic PRIVATE game/src/NetEnet.cpp)
        target_link_libraries(epsilon_game_logic PUBLIC unofficial::enet::enet)
        if(WIN32)
            target_link_libraries(epsilon_game_logic PUBLIC ws2_32 winmm)  # los sockets de Windows, que ENet usa
        endif()
        target_compile_definitions(epsilon_game_logic PUBLIC EPSILON_WITH_ENET)
    endif()

    # El servidor sin ventana: solo la logica del juego y la red, sin SDL ni GPU.
    add_executable(epsilon_server game/src/server_main.cpp)
    target_link_libraries(epsilon_server PRIVATE epsilon_game_logic)
    if(MSVC)
        target_compile_options(epsilon_game_logic PRIVATE /W4 /permissive-)
    endif()
endif()

if(EPSILON_BUILD_DEMOS AND EPSILON_MODULE_ASTRO)
    add_executable(epsilon_demo demo/demo.cpp)
    target_link_libraries(epsilon_demo PRIVATE epsilon_astro)

    add_executable(epsilon_system demo/system_report.cpp)
    target_link_libraries(epsilon_system PRIVATE epsilon_astro)

    if(EPSILON_MODULE_MODELS)
        enable_testing()
        add_executable(epsilon_tests tests/engine_tests.cpp)
        target_link_libraries(epsilon_tests PRIVATE epsilon_engine)
        add_test(NAME epsilon_tests COMMAND epsilon_tests)

        add_executable(epsilon_game_tests tests/game_tests.cpp)
        target_link_libraries(epsilon_game_tests PRIVATE epsilon_game_logic)
        # Donde esta la partida dorada (tests/data/partida_dorada.txt).
        target_compile_definitions(epsilon_game_tests PRIVATE EPSILON_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        add_test(NAME epsilon_game_tests COMMAND epsilon_game_tests)
    endif()
endif()

# --- Backend de GPU (presets x64-sdl-*) -------------------------------------
if(EPSILON_WITH_SDL)
    find_package(SDL3 CONFIG REQUIRED)
    find_program(EPSILON_DXC dxc HINTS "$ENV{WindowsSdkVerBinPath}x64" REQUIRED)

    set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders)
    set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)
    file(MAKE_DIRECTORY ${SHADER_OUT})

    # El juego puede compilar sus propios shaders con esta funcion. Ver cmake/EpsilonShaders.cmake.
    include(cmake/EpsilonShaders.cmake)

    set(SHADER_HEADERS)
    foreach(entry IN ITEMS
            "epsilon.hlsl|vs_6_0|VSSprite|gVsSprite|vs_sprite"
            "epsilon.hlsl|vs_6_0|VSLine|gVsLine|vs_line"
            "epsilon.hlsl|vs_6_0|VSFullscreen|gVsFullscreen|vs_fullscreen"
            "epsilon.hlsl|ps_6_0|PSTextured|gPsTextured|ps_textured"
            "epsilon.hlsl|ps_6_0|PSSoftSprite|gPsSoftSprite|ps_soft_sprite"
            "epsilon.hlsl|ps_6_0|PSLine|gPsLine|ps_line"
            "epsilon.hlsl|ps_6_0|PSSky|gPsSky|ps_sky"
            "sphere.hlsl|vs_6_0|VSSphere|gVsSphere|vs_sphere"
            "sphere.hlsl|ps_6_0|PSSphere|gPsSphere|ps_sphere"
            "mesh.hlsl|vs_6_0|VSMesh|gVsMesh|vs_mesh"
            "mesh.hlsl|ps_6_0|PSMesh|gPsMesh|ps_mesh"
            "post.hlsl|vs_6_0|VSPost|gVsPost|vs_post"
            "post.hlsl|ps_6_0|PSBloomDown|gPsBloomDown|ps_bloom_down"
            "post.hlsl|ps_6_0|PSBloomUp|gPsBloomUp|ps_bloom_up"
            "post.hlsl|ps_6_0|PSComposite|gPsComposite|ps_composite"
            "post.hlsl|ps_6_0|PSCopy|gPsCopy|ps_copy"
            "post.hlsl|ps_6_0|PSNebulaUpsample|gPsNebulaUpsample|ps_nebula_upsample"
            "mesh.hlsl|ps_6_0|PSShadow|gPsShadow|ps_shadow"
            "nebula.hlsl|ps_6_0|PSNebula|gPsNebula|ps_nebula"
            "nebula.hlsl|ps_6_0|PSStarCorona|gPsStarCorona|ps_star_corona"
            "blackhole.hlsl|ps_6_0|PSBlackHole|gPsBlackHole|ps_blackhole"
            "effects/scanlines.hlsl|ps_6_0|PSEffect|gPsScanlines|ps_scanlines")
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 source)
        list(GET parts 1 profile)
        list(GET parts 2 function)
        list(GET parts 3 variable)
        list(GET parts 4 file)
        epsilon_add_shader(${SHADER_DIR}/${source} ${profile} ${function} ${variable} ${SHADER_OUT}/${file}.h)
        list(APPEND SHADER_HEADERS ${SHADER_OUT}/${file}.h)
    endforeach()

    epsilon_add_module(sdl engine/src/SdlGpuBackend.cpp ${SHADER_HEADERS})
    # Los shaders compilados son cabeceras: sin esto, un cambio en un .hlsl no siempre recompilaba
    # el backend y el juego se enlazaba con el shader anterior.
    set_source_files_properties(engine/src/SdlGpuBackend.cpp PROPERTIES OBJECT_DEPENDS "${SHADER_HEADERS}")
    target_include_directories(epsilon_sdl PRIVATE ${SHADER_OUT})
    target_link_libraries(epsilon_sdl PUBLIC epsilon_core SDL3::SDL3)

    if(EPSILON_MODULE_HUD AND EPSILON_MODULE_ASTRO)
        find_package(imgui CONFIG REQUIRED)
        epsilon_add_module(hud engine/src/SciFiHud.cpp)
        target_link_libraries(epsilon_hud PUBLIC epsilon_astro epsilon_sdl PRIVATE imgui::imgui)
    endif()

    if(EPSILON_BUILD_DEMOS AND TARGET epsilon_engine)
        add_executable(epsilon_minimal demo/minimal_game.cpp)
        target_link_libraries(epsilon_minimal PRIVATE epsilon_engine epsilon_sdl)
        if(TARGET epsilon_hud)
            add_executable(epsilon_window demo/window_demo.cpp)
            target_link_libraries(epsilon_window PRIVATE epsilon_engine epsilon_sdl epsilon_hud)
            target_include_directories(epsilon_window PRIVATE ${SHADER_OUT})

            epsilon_add_shader(${CMAKE_CURRENT_SOURCE_DIR}/game/shaders/warp.hlsl ps_6_0 PSEffect gPsWarp ${SHADER_OUT}/ps_warp.h)
            epsilon_add_shader(${CMAKE_CURRENT_SOURCE_DIR}/game/shaders/reticle.hlsl ps_6_0 PSEffect gPsReticle ${SHADER_OUT}/ps_reticle.h)
            add_executable(epsilon_game game/src/main.cpp game/src/GameHud.cpp game/src/UiSounds.cpp ${SHADER_OUT}/ps_warp.h ${SHADER_OUT}/ps_reticle.h)
            target_include_directories(epsilon_game PRIVATE ${SHADER_OUT})
            # main.cpp incrusta los shaders: sin esto, Ninja no lo recompila al cambiar un .hlsl.
            set_source_files_properties(game/src/main.cpp PROPERTIES OBJECT_DEPENDS "${SHADER_OUT}/ps_warp.h;${SHADER_OUT}/ps_reticle.h")
            target_link_libraries(epsilon_game PRIVATE epsilon_game_logic epsilon_sdl imgui::imgui)
            # Las fuentes, la nave (modelos/nave.gltf, con sus texturas) y los sonidos viajan junto al ejecutable.
            add_custom_command(TARGET epsilon_game POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/game/assets/fonts $<TARGET_FILE_DIR:epsilon_game>/fonts
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/game/assets/modelos $<TARGET_FILE_DIR:epsilon_game>/modelos
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/game/assets/audio/ui $<TARGET_FILE_DIR:epsilon_game>/audio/ui)
        endif()
    endif()
endif()

# --- Instalacion: find_package(EpsilonEngine) --------------------------------
if(EPSILON_INSTALL)
    install(TARGETS ${EPSILON_TARGETS} EXPORT EpsilonEngineTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY engine/include/epsilon DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT EpsilonEngineTargets NAMESPACE epsilon:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EpsilonEngine)

    set(EPSILON_PACKAGE_SDL ${EPSILON_WITH_SDL})
    set(EPSILON_PACKAGE_HUD OFF)
    if(TARGET epsilon_hud)
        set(EPSILON_PACKAGE_HUD ON)
    endif()
    configure_package_config_file(cmake/EpsilonEngineConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/EpsilonEngineConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EpsilonEngine)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/EpsilonEngineConfigVersion.cmake
        COMPATIBILITY SameMinorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/EpsilonEngineConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/EpsilonEngineConfigVersion.cmake
        cmake/EpsilonShaders.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EpsilonEngine)
    install(DIRECTORY tools/ DESTINATION ${CMAKE_INSTALL_DATADIR}/epsilon/tools FILES_MATCHING PATTERN "*.py")
    install(DIRECTORY engine/shaders/effects DESTINATION ${CMAKE_INSTALL_DATADIR}/epsilon/shaders)
endif()