// DemoStation.h - Una estacion hecha solo con la API de modelos, para probar la escala humana.
//
// Todas sus medidas estan en metros. Un juego real la cargaria de un .glb con LoadGltf y
// BuildModel; aqui se genera por codigo para que la demo no dependa de archivos externos.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "epsilon/Models.h"
#include "epsilon/SdlGpuBackend.h"

namespace eps::demo {

struct StationAssets {
    ModelRef station;
    ModelRef astronaut;
    int damagedPanels = 0;
};

// Chapas con juntas y remaches: da escala a la superficie.
inline TextureId MakeHullTexture(SdlGpuBackend& backend) {
    constexpr int kSize = 256;
    std::vector<std::uint32_t> pixels(kSize * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const bool seam = x % 64 < 2 || y % 64 < 2;
            const bool rivet = (x % 64 - 8) * (x % 64 - 8) + (y % 64 - 8) * (y % 64 - 8) < 5;
            const std::uint32_t grain = ((static_cast<std::uint32_t>(x) * 73u ^ static_cast<std::uint32_t>(y) * 151u) * 2654435761u) >> 28;
            std::uint32_t level = 205 + grain;
            if (seam) level = 110;
            if (rivet) level = 150;
            pixels[static_cast<std::size_t>(y) * kSize + x] = 0xFF000000u | level << 16 | level << 8 | level;
        }
    }
    return backend.CreateTexture(pixels.data(), kSize, kSize);
}

inline TextureId MakeSolarTexture(SdlGpuBackend& backend) {
    constexpr int kSize = 128;
    std::vector<std::uint32_t> pixels(kSize * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const bool line = x % 16 == 0 || y % 16 == 0;
            pixels[static_cast<std::size_t>(y) * kSize + x] = line ? 0xFFC0B0A0u : 0xFF7A3A18u;  // ABGR
        }
    }
    return backend.CreateTexture(pixels.data(), kSize, kSize);
}

inline StationAssets BuildStation(SdlGpuBackend& backend) {
    const TextureId hull = MakeHullTexture(backend), solar = MakeSolarTexture(backend);
    const Quatf alongZ = Quatf::FromAxisAngle({1, 0, 0}, 1.5707963f);
    const Quatf alongX = Quatf::FromAxisAngle({0, 0, 1}, 1.5707963f);

    auto model = std::make_shared<ModelAsset>();
    model->name = "Estacion Vega";
    const auto add = [&](const std::string& name, const MeshData& mesh, TextureId texture, const Vec3f& position, const Quatf& rotation, const Color& color, float emissive = 0.0f) {
        ModelPart p;
        p.name = name;
        p.mesh = backend.CreateMesh(mesh);
        p.texture = texture;
        p.position = position;
        p.rotation = rotation;
        p.radius = MeshRadius(mesh);
        p.color = color;
        p.emissive = emissive;
        p.collision = std::make_shared<const MeshData>(mesh);  // para apuntar a la pieza exacta
        model->parts.push_back(p);
    };

    const Color steel{0.82f, 0.84f, 0.88f, 1.0f}, dark{0.32f, 0.34f, 0.38f, 1.0f}, gold{0.92f, 0.72f, 0.34f, 1.0f};

    // Nucleo presurizado y anillo habitable. Una persona cabe de pie: 6 m de diametro.
    add("nucleo", MakeCylinder(3.0f, 22.0f, 48), hull, {0, 0, 0}, alongZ, steel);
    add("esclusa", MakeCylinder(1.6f, 3.0f, 32), hull, {0, 0, -12.5f}, alongZ, dark);
    add("modulo_proa", MakeSphere(3.0f, 40), hull, {0, 0, 11.0f}, {}, steel);
    add("anillo", MakeTorus(18.0f, 1.8f, 96, 20), hull, {0, 0, 0}, alongZ, steel);
    for (int i = 0; i < 4; ++i) {
        const Quatf turn = Quatf::FromAxisAngle({0, 0, 1}, 1.5707963f * static_cast<float>(i));
        const Vec3f middle = turn.ToMatrix().TransformDirection(Vec3f{10.5f, 0, 0});
        add("radio", MakeCylinder(0.6f, 15.0f, 16), hull, middle, turn * alongX, dark);
    }

    // Paneles solares: 8 x 26 m.
    for (const float side : {-1.0f, 1.0f}) {
        add("mastil", MakeCylinder(0.25f, 30.0f, 12), hull, {side * 34.0f, 0, -7.0f}, alongX, dark);
        for (int k = 0; k < 3; ++k) {
            const float x = side * (24.0f + 10.0f * static_cast<float>(k));
            add("panel_solar", MakeBox(9.0f, 0.1f, 18.0f), solar, {x, 0, -7.0f}, {}, Color{1, 1, 1, 1});
        }
    }

    // Radiadores y antena.
    add("radiador", MakeBox(0.2f, 12.0f, 6.0f), hull, {0, -9.0f, -7.0f}, {}, gold);
    add("radiador", MakeBox(0.2f, 12.0f, 6.0f), hull, {0, 9.0f, -7.0f}, {}, gold);
    add("antena", MakeCylinder(0.08f, 6.0f, 8), hull, {0, -5.5f, 9.0f}, {}, dark);
    add("plato", MakeSphere(1.2f, 20), hull, {0, -8.5f, 9.0f}, {}, steel);

    // Asideros: su tamano se aprecia con la persona al lado.
    for (int i = 0; i < 8; ++i) {
        const float z = -9.0f + 2.6f * static_cast<float>(i);
        add("asidero", MakeBox(0.04f, 0.04f, 0.6f), hull, {0, -3.08f, z}, {}, gold);
    }

    // Paneles averiados: la parte rota y su repuesto, que empieza oculto.
    const MeshData plate = MakeBox(1.4f, 0.08f, 1.4f);
    const float zones[] = {-8.0f, -3.0f, 2.5f, 7.0f};
    for (int i = 0; i < 4; ++i) {
        const Quatf tilt = Quatf::FromAxisAngle({0, 0, 1}, 0.0f);
        add("roto_" + std::to_string(i), plate, 0, {0, -3.06f, zones[i]}, tilt, Color{1.0f, 0.18f, 0.08f, 1.0f}, 2.4f);
        add("nuevo_" + std::to_string(i), plate, hull, {0, -3.06f, zones[i]}, tilt, Color{0.42f, 0.95f, 0.58f, 1.0f}, 0.25f);
    }

    auto person = std::make_shared<ModelAsset>();
    person->name = "Astronauta";
    const auto addPerson = [&](const std::string& name, const MeshData& mesh, const Vec3f& position, const Color& color, float emissive = 0.0f) {
        ModelPart p;
        p.name = name;
        p.mesh = backend.CreateMesh(mesh);
        p.texture = 0;
        p.position = position;
        p.radius = MeshRadius(mesh);
        p.color = color;
        p.emissive = emissive;
        person->parts.push_back(p);
    };
    const Color suit{0.92f, 0.92f, 0.90f, 1.0f};
    addPerson("casco", MakeSphere(0.15f, 20), {0, -0.78f, 0}, suit);
    addPerson("visor", MakeSphere(0.12f, 16), {0, -0.78f, -0.05f}, Color{0.95f, 0.65f, 0.2f, 1.0f}, 0.2f);
    addPerson("torso", MakeBox(0.46f, 0.66f, 0.28f), {0, -0.32f, 0}, suit);
    addPerson("mochila", MakeBox(0.42f, 0.56f, 0.2f), {0, -0.34f, 0.24f}, Color{0.5f, 0.52f, 0.56f, 1.0f});
    addPerson("brazo", MakeBox(0.12f, 0.6f, 0.12f), {-0.31f, -0.30f, 0}, suit);
    addPerson("brazo", MakeBox(0.12f, 0.6f, 0.12f), {0.31f, -0.30f, 0}, suit);
    addPerson("pierna", MakeBox(0.16f, 0.8f, 0.16f), {-0.13f, 0.42f, 0}, suit);
    addPerson("pierna", MakeBox(0.16f, 0.8f, 0.16f), {0.13f, 0.42f, 0}, suit);

    StationAssets out;
    out.station = model;
    out.astronaut = person;
    out.damagedPanels = 4;
    return out;
}

}  // namespace eps::demo
