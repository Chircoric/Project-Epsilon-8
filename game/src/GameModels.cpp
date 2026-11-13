#include "game/GameModels.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <tuple>

namespace game {

using namespace eps;

namespace {

constexpr float kQuarter = 1.5707963f;

const Quatf kAlongZ = Quatf::FromAxisAngle({1, 0, 0}, kQuarter);
const Quatf kAlongX = Quatf::FromAxisAngle({0, 0, 1}, kQuarter);

constexpr float kShipEdge = 12.0f;  // m: arista mas larga de la nave, para que se pueda abollar
constexpr float kStationEdge = 80.0f;  // m: lo mismo en un bastion alienigena, que mide kilometros
constexpr float kEmissiveScale = 0.4f, kEmissiveMaximum = 3.0f;  // brillo de un .glb: intensidad del material

// Ayuda a montar un modelo con piezas repetidas sin subir la misma malla varias veces.
class Builder {
public:
    // keepTriangles: cada pieza guarda sus triangulos (choque y zonas), partidos hasta aristas de edge m.
    Builder(const MeshUpload& upload, TextureId hull, const std::string& name, bool keepTriangles = false, float edge = kShipEdge) : upload_(upload), hull_(hull), model_(std::make_shared<ModelAsset>()), keep_(keepTriangles), edge_(edge) { model_->name = name; }

    void Add(const std::string& name, const MeshData& source, const Vec3f& position, const Quatf& rotation, const Color& color, float emissive = 0.0f, bool textured = true) {
        // La nave se abolla vertice a vertice (crateres de mesh.hlsl): sus caras grandes se parten.
        MeshData refined;
        if (keep_) {
            refined = source;
            SubdivideLongEdges(refined, edge_, 60000);
        }
        const MeshData& mesh = keep_ ? refined : source;
        ModelPart p;
        p.name = name;
        p.mesh = upload_(mesh);
        if (keep_) p.collision = std::make_shared<const MeshData>(mesh);
        p.texture = textured ? hull_ : 0;
        p.position = position;
        p.rotation = rotation;
        p.radius = MeshRadius(mesh);
        p.color = color;
        p.emissive = emissive;
        model_->parts.push_back(std::move(p));
    }

    ModelRef Done() { return model_; }

private:
    const MeshUpload& upload_;
    TextureId hull_;
    std::shared_ptr<ModelAsset> model_;
    bool keep_ = false;
    float edge_ = kShipEdge;
};

Vec3f Around(float angle, float radius, float z = 0.0f) { return {std::cos(angle) * radius, std::sin(angle) * radius, z}; }
Quatf TurnZ(float angle) { return Quatf::FromAxisAngle({0, 0, 1}, angle); }

const Color kSteel{0.74f, 0.76f, 0.80f, 1.0f}, kDark{0.26f, 0.28f, 0.32f, 1.0f}, kCopper{0.78f, 0.50f, 0.28f, 1.0f};
const Color kWarp{0.35f, 0.75f, 1.0f, 1.0f}, kEngine{0.55f, 0.85f, 1.0f, 1.0f}, kVoid{0.62f, 0.28f, 1.0f, 1.0f};

// La nave: 1,2 km de proa a popa. El anillo es el motor de curvatura, y el nucleo violeta es el
// que alimenta el emisor de energia negativa.
ModelRef BuildShip(const MeshUpload& upload, TextureId hull) {
    Builder b(upload, hull, "Nave Epsilon", true);
    b.Add("espina", MakeCylinder(45.0f, 900.0f, 32), {0, 0, 0}, kAlongZ, kSteel);
    b.Add("proa", MakeSphere(70.0f, 32), {0, 0, 470.0f}, {}, kSteel);
    b.Add("puente", MakeBox(90.0f, 40.0f, 170.0f), {0, -80.0f, 340.0f}, {}, kDark);
    b.Add("ventanas", MakeBox(80.0f, 6.0f, 3.0f), {0, -88.0f, 427.0f}, {}, Color{1.0f, 0.86f, 0.55f, 1.0f}, 3.0f, false);

    // Bodegas: de aqui salen las estaciones.
    for (int i = 0; i < 6; ++i) {
        const float angle = kQuarter * 0.66667f * static_cast<float>(i) + 0.5f;
        b.Add("bodega", MakeBox(70.0f, 70.0f, 260.0f), Around(angle, 95.0f, 90.0f), TurnZ(angle), kDark);
    }

    // Motor de curvatura.
    b.Add("anillo_curvatura", MakeTorus(230.0f, 18.0f, 96, 14), {0, 0, -120.0f}, kAlongZ, kSteel);
    b.Add("anillo_campo", MakeTorus(230.0f, 7.0f, 96, 8), {0, 0, -120.0f}, kAlongZ, kWarp, 2.4f, false);
    for (int i = 0; i < 4; ++i) {
        const float angle = kQuarter * static_cast<float>(i) + 0.785f;
        b.Add("brazo", MakeBox(190.0f, 14.0f, 40.0f), Around(angle, 130.0f, -120.0f), TurnZ(angle), kDark);
    }

    b.Add("nucleo_oscuro", MakeSphere(38.0f, 24), {0, 0, -300.0f}, {}, kVoid, 3.0f, false);
    for (const float side : {-1.0f, 1.0f}) {
        b.Add("gondola", MakeCylinder(55.0f, 300.0f, 24), {side * 150.0f, 0, -380.0f}, kAlongZ, kSteel);
        b.Add("tobera", MakeTorus(46.0f, 10.0f, 32, 8), {side * 150.0f, 0, -535.0f}, kAlongZ, kCopper);
        b.Add("escape", MakeCylinder(42.0f, 6.0f, 24), {side * 150.0f, 0, -535.0f}, kAlongZ, kEngine, 1.6f, false);
        b.Add("radiador", MakeBox(6.0f, 180.0f, 240.0f), {side * 70.0f, 110.0f, -280.0f}, {}, kCopper);
    }
    return b.Done();
}

// Energia: una corola de colectores que mira a la estrella y una cuchara delante.
ModelRef BuildPower(const MeshUpload& upload, TextureId hull) {
    Builder b(upload, hull, "Estacion de energia");
    const Color collector{0.16f, 0.22f, 0.42f, 1.0f}, glow{1.0f, 0.74f, 0.32f, 1.0f};
    b.Add("eje", MakeCylinder(36.0f, 520.0f, 16), {0, 0, 0}, kAlongZ, kSteel);
    b.Add("corona", MakeTorus(420.0f, 22.0f, 96, 10), {0, 0, 120.0f}, kAlongZ, kDark);
    b.Add("corona_activa", MakeTorus(420.0f, 8.0f, 96, 8), {0, 0, 150.0f}, kAlongZ, glow, 3.0f, false);
    for (int i = 0; i < 12; ++i) {
        const float angle = kQuarter * 0.33333f * static_cast<float>(i);
        const Quatf petal = TurnZ(angle) * Quatf::FromAxisAngle({0, 1, 0}, 0.25f);
        b.Add("colector", MakeBox(340.0f, 120.0f, 6.0f), Around(angle, 240.0f, 90.0f), petal, collector, 0.12f);
    }
    b.Add("cuchara", MakeCylinder(150.0f, 60.0f, 24), {0, 0, 300.0f}, kAlongZ, kDark);
    b.Add("boca", MakeTorus(150.0f, 12.0f, 48, 8), {0, 0, 335.0f}, kAlongZ, glow, 2.0f, false);
    b.Add("deposito", MakeSphere(110.0f, 24), {0, 0, -240.0f}, {}, kSteel);
    // Escotilla de atraque: encima del deposito, lejos de los colectores (GameSession::Port).
    const Color dock{0.35f, 1.0f, 0.55f, 1.0f};
    b.Add("collar", MakeBox(74.0f, 40.0f, 74.0f), {0, -110.0f, -240.0f}, {}, kDark);
    b.Add("escotilla_luz", MakeBox(84.0f, 4.0f, 84.0f), {0, -131.0f, -240.0f}, {}, dock, 4.0f, false);
    b.Add("escotilla", MakeBox(60.0f, 6.0f, 60.0f), {0, -134.0f, -240.0f}, {}, kSteel);
    b.Add("guia", MakeSphere(8.0f, 10), {48.0f, -134.0f, -240.0f}, {}, dock, 6.0f, false);
    for (int i = 0; i < 4; ++i) {
        const float angle = kQuarter * static_cast<float>(i);
        b.Add("mod_" + std::to_string(i + 1), MakeCylinder(55.0f, 120.0f, 12), Around(angle, 190.0f, -240.0f), kAlongZ, kCopper);
    }
    return b.Done();
}

// Las naves ajenas no usan piezas humanas: formas continuas y cristales encendidos. Guardan sus
// triangulos como la nave: de ellos salen sus zonas (AlienZoneOfPart) y se abollan al recibir.
ModelRef BuildAlienShip(const MeshUpload& upload, bool hostile) {
    Builder b(upload, 0, hostile ? "Nave hostil" : "Nave neutral", true);
    const Color skin = hostile ? Color{0.16f, 0.07f, 0.09f, 1.0f} : Color{0.46f, 0.62f, 0.60f, 1.0f};
    const Color light = hostile ? Color{1.0f, 0.18f, 0.10f, 1.0f} : Color{0.30f, 1.0f, 0.82f, 1.0f};
    if (hostile) {
        // Una punta de flecha con dos cuchillas hacia delante.
        MeshData body;
        AppendMesh(body, MakeAsteroid(11, 3), {}, {}, 190.0f);
        b.Add("cuerpo", body, {0, 0, 0}, {}, skin);
        for (const float side : {-1.0f, 1.0f}) {
            const Quatf sweep = Quatf::FromAxisAngle({0, 1, 0}, side * 0.5f);
            b.Add("cuchilla", MakeBox(30.0f, 12.0f, 420.0f), {side * 130.0f, 0, 90.0f}, sweep, skin);
            b.Add("filo", MakeBox(6.0f, 6.0f, 400.0f), {side * 143.0f, 0, 92.0f}, sweep, light, 4.0f);
        }
        b.Add("ojo", MakeSphere(40.0f, 16), {0, 0, -60.0f}, {}, light, 5.0f);
    } else {
        b.Add("disco", MakeCylinder(300.0f, 70.0f, 48), {0, 0, 0}, {}, skin);
        b.Add("aro", MakeTorus(340.0f, 26.0f, 64, 12), {0, 0, 0}, {}, skin);
        b.Add("luz", MakeTorus(340.0f, 8.0f, 64, 8), {0, -18.0f, 0}, {}, light, 3.0f);
        b.Add("centro", MakeSphere(90.0f, 20), {0, -60.0f, 0}, {}, light, 2.5f);
    }
    return b.Done();
}

ModelRef BuildAlienStation(const MeshUpload& upload, bool hostile) {
    Builder b(upload, 0, hostile ? "Bastion hostil" : "Estacion neutral", true, kStationEdge);
    const Color skin = hostile ? Color{0.14f, 0.07f, 0.08f, 1.0f} : Color{0.44f, 0.58f, 0.60f, 1.0f};
    const Color light = hostile ? Color{1.0f, 0.16f, 0.10f, 1.0f} : Color{0.30f, 1.0f, 0.82f, 1.0f};
    b.Add("nucleo", MakeSphere(620.0f, 40), {0, 0, 0}, {}, skin);
    b.Add("corazon", MakeSphere(180.0f, 24), {0, 0, -600.0f}, {}, light, 4.0f);
    b.Add("anillo", MakeTorus(1500.0f, 90.0f, 96, 12), {0, 0, 0}, {}, skin);
    b.Add("anillo_luz", MakeTorus(1500.0f, 28.0f, 96, 8), {0, -80.0f, 0}, {}, light, 3.0f);
    const int spokes = hostile ? 10 : 6;
    for (int i = 0; i < spokes; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(spokes);
        const Quatf turn = Quatf::FromAxisAngle({0, 1, 0}, -angle);
        const Vec3f out{std::cos(angle), 0.0f, std::sin(angle)};
        b.Add("radio", MakeBox(900.0f, 60.0f, 80.0f), out * 1050.0f, turn, skin);
        if (hostile) b.Add("pincho", MakeBox(1100.0f, 36.0f, 36.0f), out * 2150.0f, turn, skin);
        if (hostile) b.Add("punta", MakeSphere(70.0f, 12), out * 2720.0f, {}, light, 5.0f);
    }
    return b.Done();
}

// El planeta coraza: una esfera blindada de 70 000 km de radio (Jupiter), con anillos de placas y
// costuras encendidas. En metros, como todo modelo.
ModelRef BuildArmorPlanet(const MeshUpload& upload, double radiusKm) {
    Builder b(upload, 0, "Planeta coraza");
    const float r = static_cast<float>(radiusKm * 1000.0);
    const Color armor{0.12f, 0.08f, 0.09f, 1.0f}, plate{0.20f, 0.13f, 0.13f, 1.0f}, seam{1.0f, 0.20f, 0.08f, 1.0f};
    b.Add("coraza", MakeSphere(r, 96), {0, 0, 0}, {}, armor);
    // Cinturones de placas a varias latitudes, y una costura de luz en cada uno.
    for (int i = -3; i <= 3; ++i) {
        const float lat = 0.33f * static_cast<float>(i);
        const float ring = r * std::cos(lat);
        const float y = r * std::sin(lat);
        b.Add("placas", MakeTorus(ring, r * 0.025f, 128, 8), {0, y, 0}, {}, plate);
        b.Add("costura", MakeTorus(ring * 1.004f, r * 0.006f, 128, 6), {0, y, 0}, {}, seam, 3.0f);
    }
    // Dos grandes ojos de luz en los polos.
    b.Add("polo", MakeSphere(r * 0.08f, 24), {0, -r, 0}, {}, seam, 4.0f);
    b.Add("polo", MakeSphere(r * 0.08f, 24), {0, r, 0}, {}, seam, 4.0f);
    return b.Done();
}

// La escala de lo abandonado: 24 km. Un anillo roto, monolitos y un nucleo aun encendido.
ModelRef BuildDerelict(const MeshUpload& upload) {
    Builder b(upload, 0, "Estructura abandonada");
    const Color stone{0.22f, 0.21f, 0.27f, 1.0f}, trim{0.40f, 0.36f, 0.48f, 1.0f};
    for (int i = 0; i < 9; ++i) {
        if (i == 3 || i == 6) continue;  // los tramos que faltan
        const float angle = 0.6981317f * static_cast<float>(i);
        const float jitter = i % 2 == 0 ? 1.0f : 0.92f;
        const Quatf turn = TurnZ(angle);
        b.Add("segmento", MakeBox(1800.0f, 2600.0f, 3600.0f), Around(angle, 10000.0f * jitter), turn, stone);
        b.Add("linea", MakeBox(120.0f, 2500.0f, 200.0f), Around(angle, 10000.0f * jitter - 950.0f), turn, kVoid, 1.6f);
    }
    for (int i = 0; i < 3; ++i) {
        const float angle = 2.0943951f * static_cast<float>(i) + 0.4f;
        b.Add("monolito", MakeBox(1100.0f, 9000.0f, 1100.0f), Around(angle, 3800.0f), TurnZ(angle + 0.5f), trim);
    }
    b.Add("nucleo", MakeSphere(1500.0f, 32), {0, 0, 0}, {}, kVoid, 2.8f);
    b.Add("halo", MakeTorus(2300.0f, 70.0f, 96, 8), {0, 0, 0}, kAlongZ, kVoid, 3.5f);
    return b.Done();
}

// En el mundo, arriba es -Y: por eso cada altura de la cubierta y de las personas va en negativo.
ModelRef BuildDeck(const MeshUpload& upload, StationKind kind) {
    Builder b(upload, 0, "Cubierta de central");
    int index = 0;
    for (const DeckBox& box : MakeDeck(kind).boxes) {
        const Vec3f center{static_cast<float>(box.center.x), static_cast<float>(-box.center.y), static_cast<float>(box.center.z)};
        // Algo de luz propia: dentro de una caja cerrada no llega la de la estrella.
        b.Add("pieza" + std::to_string(index++), MakeBox(static_cast<float>(box.size.x), static_cast<float>(box.size.y), static_cast<float>(box.size.z)), center, {}, box.color, std::max(box.emissive, 0.35f), false);
    }
    return b.Done();
}

ModelRef BuildPerson(const MeshUpload& upload, const Color& suit, const std::string& name) {
    Builder b(upload, 0, name);
    const Color skin{0.86f, 0.68f, 0.54f, 1.0f}, boots{0.12f, 0.12f, 0.14f, 1.0f};
    b.Add("pierna_i", MakeBox(0.16f, 0.82f, 0.18f), {-0.11f, -0.41f, 0}, {}, suit, 0.35f, false);
    b.Add("pierna_d", MakeBox(0.16f, 0.82f, 0.18f), {0.11f, -0.41f, 0}, {}, suit, 0.35f, false);
    b.Add("botas", MakeBox(0.42f, 0.10f, 0.28f), {0, -0.05f, 0.04f}, {}, boots, 0.35f, false);
    b.Add("torso", MakeBox(0.46f, 0.60f, 0.26f), {0, -1.13f, 0}, {}, suit, 0.35f, false);
    b.Add("mochila", MakeBox(0.36f, 0.42f, 0.14f), {0, -1.15f, -0.19f}, {}, Color{0.40f, 0.42f, 0.46f, 1.0f}, 0.35f, false);
    b.Add("brazo_i", MakeBox(0.11f, 0.60f, 0.12f), {-0.30f, -1.12f, 0}, {}, suit, 0.35f, false);
    b.Add("brazo_d", MakeBox(0.11f, 0.60f, 0.12f), {0.30f, -1.12f, 0}, {}, suit, 0.35f, false);
    b.Add("cabeza", MakeSphere(0.12f, 16), {0, -1.60f, 0}, {}, skin, 0.35f, false);
    b.Add("visor", MakeBox(0.18f, 0.06f, 0.04f), {0, -1.62f, 0.11f}, {}, Color{0.35f, 0.85f, 1.0f, 1.0f}, 2.0f, false);
    return b.Done();
}

}  // namespace

// Viga en I de 1 de largo, de z = 0 a z = 1: asoma desde donde nace.
MeshData MakeGirder() {
    MeshData m;
    AppendMesh(m, MakeBox(0.12f, 0.018f, 1.0f), {0, 0.05f, 0.5f});
    AppendMesh(m, MakeBox(0.12f, 0.018f, 1.0f), {0, -0.05f, 0.5f});
    AppendMesh(m, MakeBox(0.016f, 0.1f, 1.0f), {0, 0, 0.5f});
    // Un tramo doblado al final: lo que queda de una cuaderna arrancada.
    MeshData bent;
    AppendMesh(bent, MakeBox(0.12f, 0.018f, 0.35f), {0, 0.05f, 0.175f});
    AppendMesh(bent, MakeBox(0.016f, 0.1f, 0.35f), {0, 0, 0.175f});
    AppendMesh(m, bent, {0, 0, 0.98f}, Quatf::FromAxisAngle({1, 0, 0}, 0.6f));
    return m;
}

// Chapa del casco arrancada: dos planchas en angulo, como un petalo abierto.
MeshData MakePlate() {
    MeshData m;
    AppendMesh(m, MakeBox(0.55f, 0.025f, 0.55f), {0, 0, 0.275f});
    AppendMesh(m, MakeBox(0.45f, 0.025f, 0.5f), {0.04f, 0.1f, 0.75f}, Quatf::FromAxisAngle({1, 0, 0}, -0.45f));
    return m;
}

// Astilla: una roca aplastada y estirada, de ~1 de largo.
MeshData MakeShard() {
    MeshData m = MakeAsteroid(97u, 1);
    for (MeshVertex& v : m.vertices) v.position = {v.position.x * 0.22f, v.position.y * 0.08f, v.position.z * 0.5f};
    ComputeNormals(m);
    return m;
}

namespace {

// Una nave de un .gltf, la propia o una alienigena: piezas centradas en su caja y con sus triangulos.
ModelRef LoadHullModel(const std::string& path, const std::string& name, const MeshUpload& upload, const std::function<TextureId(const std::string&)>& texture, std::string* error) {
    std::vector<ImportedPart> parts;
    if (!LoadGltf(path, parts, error)) return nullptr;
    if (parts.empty()) {
        if (error) *error = "el archivo no tiene mallas";
        return nullptr;
    }
    auto model = std::make_shared<ModelAsset>();
    model->name = name;
    for (ImportedPart& imported : parts) {
        MeshData& mesh = imported.mesh;
        if (mesh.vertices.empty() || mesh.indices.empty()) continue;
        // Cada pieza se centra en su caja: asi su posicion dice donde esta y a que zona pertenece.
        Vec3f low = mesh.vertices[0].position, high = low;
        for (const MeshVertex& v : mesh.vertices) {
            low = {std::min(low.x, v.position.x), std::min(low.y, v.position.y), std::min(low.z, v.position.z)};
            high = {std::max(high.x, v.position.x), std::max(high.y, v.position.y), std::max(high.z, v.position.z)};
        }
        const Vec3f center = (low + high) * 0.5f;
        for (MeshVertex& v : mesh.vertices) v.position = v.position - center;
        SubdivideLongEdges(mesh, kShipEdge, 120000);  // solo parte lo que tenga caras enormes
        ModelPart p;
        p.name = imported.name;
        p.mesh = upload(mesh);
        p.position = center;
        p.radius = MeshRadius(mesh);
        p.color = imported.color;
        // Blender exporta intensidades de 2 a 20; aqui el brillo de la maqueta va de 1,5 a 3.
        p.emissive = std::min(imported.emissive * kEmissiveScale, kEmissiveMaximum);
        if (texture && !imported.texturePath.empty()) p.texture = texture(imported.texturePath);
        p.collision = std::make_shared<const MeshData>(mesh);
        model->parts.push_back(std::move(p));
    }
    if (model->parts.empty()) {
        if (error) *error = "ninguna malla tenia triangulos";
        return nullptr;
    }
    return model;
}

}  // namespace

ModelRef LoadShipModel(const std::string& path, const MeshUpload& upload, const std::function<TextureId(const std::string&)>& texture, std::string* error) {
    return LoadHullModel(path, "Nave Epsilon", upload, texture, error);
}

ModelRef LoadAlienShipModel(const std::string& path, const std::string& name, const MeshUpload& upload, const std::function<TextureId(const std::string&)>& texture, std::string* error) {
    return LoadHullModel(path, name, upload, texture, error);
}

ModelRef NeutralColors(const ModelAsset& hostile) {
    // Los colores de las maquetas (BuildAlienShip): la piel hostil, rojiza y oscura, pasa a la verdosa
    // conservando cuanto mas clara u oscura es cada pieza; lo que brilla, a turquesa.
    const Color hostileSkin{0.16f, 0.07f, 0.09f, 1.0f}, neutralSkin{0.46f, 0.62f, 0.60f, 1.0f}, neutralLight{0.30f, 1.0f, 0.82f, 1.0f};
    const auto luminance = [](const Color& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
    auto model = std::make_shared<ModelAsset>(hostile);
    model->name = hostile.name + " (neutral)";
    for (ModelPart& p : model->parts) {
        if (p.emissive > 0.0f) {
            p.color = neutralLight;
            continue;
        }
        const float k = std::clamp(luminance(p.color) / luminance(hostileSkin), 0.3f, 2.0f);
        p.color = {std::min(neutralSkin.r * k, 1.0f), std::min(neutralSkin.g * k, 1.0f), std::min(neutralSkin.b * k, 1.0f), p.color.a};
    }
    return model;
}

const char* TurretModelName(ShipModuleKind kind) {
    switch (kind) {
        case ShipModuleKind::PointDefensePlatform: return "defensa_puntual";
        case ShipModuleKind::CoilgunLight: return "coilgun_ligera";
        case ShipModuleKind::CoilgunHeavy: return "coilgun_pesada";
        case ShipModuleKind::RailSpinalLight: return "espinal_ligera";
        case ShipModuleKind::RailSpinalHeavy: return "espinal_pesada";
        case ShipModuleKind::Rail360Light: return "railgun360_ligera";
        case ShipModuleKind::Rail360Heavy: return "railgun360_pesada";
        // Las tres bahias de cada tamano comparten lanzador.
        case ShipModuleKind::BayLight10:
        case ShipModuleKind::BayLight25:
        case ShipModuleKind::BayLight50: return "misiles_ligera";
        case ShipModuleKind::BayHeavy10:
        case ShipModuleKind::BayHeavy25:
        case ShipModuleKind::BayHeavy50: return "misiles_pesada";
        case ShipModuleKind::NuclearLight: return "nuclear_ligera";
        case ShipModuleKind::NuclearHeavy: return "nuclear_pesada";
        default: return nullptr;
    }
}

ModelRef LoadGltfModel(const std::string& path, const std::string& name, const MeshUpload& upload, const std::function<TextureId(const std::string&)>& texture, std::string* error) {
    std::vector<ImportedPart> parts;
    if (!LoadGltf(path, parts, error)) return nullptr;
    auto model = std::make_shared<ModelAsset>();
    model->name = name;
    for (ImportedPart& imported : parts) {
        MeshData& mesh = imported.mesh;
        if (mesh.vertices.empty() || mesh.indices.empty()) continue;
        Vec3f low = mesh.vertices[0].position, high = low;
        for (const MeshVertex& v : mesh.vertices) {
            low = {std::min(low.x, v.position.x), std::min(low.y, v.position.y), std::min(low.z, v.position.z)};
            high = {std::max(high.x, v.position.x), std::max(high.y, v.position.y), std::max(high.z, v.position.z)};
        }
        const Vec3f center = (low + high) * 0.5f;
        for (MeshVertex& v : mesh.vertices) v.position = v.position - center;
        ModelPart p;
        p.name = imported.name;
        p.mesh = upload(mesh);
        p.position = center;
        p.radius = MeshRadius(mesh);
        p.color = imported.color;
        p.emissive = std::min(imported.emissive * kEmissiveScale, kEmissiveMaximum);
        if (texture && !imported.texturePath.empty()) p.texture = texture(imported.texturePath);
        model->parts.push_back(std::move(p));
    }
    if (model->parts.empty()) {
        if (error) *error = "ninguna malla tenia triangulos";
        return nullptr;
    }
    return model;
}

void LoadStationAndDrones(GameAssets& a, const std::string& folder, const MeshUpload& upload, const std::function<TextureId(const std::string&)>& texture, std::string* log) {
    const auto note = [&](const std::string& line) {
        if (log) *log += line + "\n";
    };
    std::error_code ignored;
    std::string error;
    const std::string station = folder + "estacion.gltf";
    if (std::filesystem::exists(station, ignored)) {
        if (ModelRef model = LoadGltfModel(station, "Estacion origami", upload, texture, &error)) {
            auto animation = std::make_shared<ModelAnimation>();
            if (LoadGltfAnimation(station, "despliegue", *animation, 24.0, &error)) {
                std::map<std::string, int> index;
                for (std::size_t i = 0; i < animation->parts.size(); ++i) index[animation->parts[i]] = static_cast<int>(i);
                a.origamiTrack.clear();
                for (const ModelPart& p : model->parts) {
                    const auto found = index.find(p.name);
                    a.origamiTrack.push_back(found == index.end() ? -1 : found->second);
                }
                a.origamiDeploy = animation;
            } else {
                note("Estacion origami sin animacion: " + error);
            }
            a.origami = model;
        } else {
            note("No se pudo cargar la estacion origami: " + error);
        }
    } else {
        note("Sin " + station + ": la origami usa la maqueta de la central.");
    }
    const std::string bay = folder + "bahia_drones.gltf";
    if (std::filesystem::exists(bay, ignored)) {
        if (ModelRef model = LoadGltfModel(bay, "Bahia de drones", upload, texture, &error)) {
            a.droneBay = model;
            std::vector<ImportedMarker> markers;
            if (LoadGltfMarkers(bay, markers, &error)) {
                std::vector<Vec3f> spots;
                for (const char* wanted : {"dron_1", "dron_2"})
                    for (const ImportedMarker& m : markers)
                        if (m.name == wanted) spots.push_back(m.position);
                if (spots.size() == 2) a.droneSpots = spots;
            }
        } else {
            note("No se pudo cargar la bahia de drones: " + error);
        }
    }
    const std::string drone = folder + "dron.gltf";
    if (std::filesystem::exists(drone, ignored)) {
        if (ModelRef model = LoadGltfModel(drone, "Dron minero", upload, texture, &error)) a.drone = model;
        else note("No se pudo cargar el dron: " + error);
    }
}

namespace {
// Las zonas de un modelo alienigena: su forma y la zona de cada pieza, en su orden.
void RebuildAlienZones(GameAssets& a, AlienModel model) {
    const std::size_t m = static_cast<std::size_t>(model);
    const ModelAsset& asset = *AlienModelAsset(a, model);
    a.alienZones[m] = BuildAlienZoneLayout(asset, model);
    a.alienPartZones[m].clear();
    for (const ModelPart& p : asset.parts) a.alienPartZones[m].push_back(AlienZoneOfPart(p.name, p.position));
}
}  // namespace

void LoadCombatModels(GameAssets& a, const std::string& folder, const MeshUpload& upload, const std::function<TextureId(const std::string&)>& texture, std::string* log) {
    const auto note = [&](const std::string& line) {
        if (log) *log += line + "\n";
    };
    std::error_code ignored;
    std::string error;
    static const char* const kFiles[kAlienClassModelCount] = {"alien_dron.gltf", "alien_corveta.gltf", "alien_fragata.gltf"};
    static const char* const kNames[kAlienClassModelCount] = {"Dron alienigena", "Corveta alienigena", "Fragata alienigena"};
    for (int i = 0; i < kAlienClassModelCount; ++i) {
        const std::string path = folder + kFiles[i];
        if (!std::filesystem::exists(path, ignored)) {
            note("Sin " + path + ": la " + std::string(kShipClassNames[i]) + " alienigena usa la maqueta.");
            continue;
        }
        ModelRef model = LoadAlienShipModel(path, kNames[i], upload, texture, &error);
        if (!model) {
            note("No se pudo cargar " + path + ": " + error);
            continue;
        }
        const ShipClassId id = static_cast<ShipClassId>(i);
        a.hostileClassShips[static_cast<std::size_t>(i)] = model;
        a.neutralClassShips[static_cast<std::size_t>(i)] = NeutralColors(*model);
        RebuildAlienZones(a, ClassModel(id, true));
        RebuildAlienZones(a, ClassModel(id, false));
    }

    const std::string turrets = folder + "torretas.gltf";
    if (!std::filesystem::exists(turrets, ignored)) {
        note("Sin " + turrets + ": las armas usan la maqueta de la torreta.");
        return;
    }
    const ModelRef all = LoadGltfModel(turrets, "Torretas", upload, texture, &error);
    if (!all) {
        note("No se pudo cargar " + turrets + ": " + error);
        return;
    }
    // Un solo archivo con todas: cada arma son las piezas con su prefijo.
    for (int k = 0; k < static_cast<int>(ShipModuleKind::Count); ++k) {
        const char* name = TurretModelName(static_cast<ShipModuleKind>(k));
        if (!name) continue;
        const std::string prefix = std::string(name) + "_";
        auto model = std::make_shared<ModelAsset>();
        model->name = name;
        for (const ModelPart& p : all->parts)
            if (p.name.rfind(prefix, 0) == 0) model->parts.push_back(p);
        if (model->parts.empty()) note(std::string("torretas.gltf no tiene las piezas de ") + name);
        else a.moduleModels[static_cast<std::size_t>(k)] = model;
    }
}

std::shared_ptr<ZoneLayout> BuildZoneLayout(const ModelAsset& ship) { return BuildZoneLayout(ship, ZoneOfPart, 30.0); }

std::shared_ptr<ZoneLayout> BuildZoneLayout(const ModelAsset& ship, const std::function<int(const std::string&, const Vec3f&)>& zoneOf, double kCell) {
    constexpr double kMinBall = 2.0;   // m: una pieza diminuta sigue siendo blanco
    const auto lowest = [](const Vec3d& a, const Vec3d& b) { return Vec3d{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; };
    const auto highest = [](const Vec3d& a, const Vec3d& b) { return Vec3d{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; };
    const Vec3d empty0{1e300, 1e300, 1e300}, empty1{-1e300, -1e300, -1e300};

    struct Box {
        Vec3d low, high;
    };
    std::array<std::map<std::tuple<long, long, long>, Box>, kHullZoneCount> cells;
    std::array<Box, kHullZoneCount> zoneBox;
    zoneBox.fill(Box{empty0, empty1});
    auto layout = std::make_shared<ZoneLayout>();
    Vec3d extent{0, 0, 0};

    for (const ModelPart& p : ship.parts) {
        if (!p.collision) continue;
        const std::size_t zone = static_cast<std::size_t>(std::clamp(zoneOf(p.name, p.position), 0, kHullZoneCount - 1));
        const Mat4f turn = p.rotation.ToMatrix();
        Box part{empty0, empty1};
        for (const MeshVertex& v : p.collision->vertices) {
            const Vec3f w = turn.TransformDirection(v.position * p.scale) + p.position;
            const Vec3d q{w.x, w.y, w.z};
            const auto key = std::make_tuple(static_cast<long>(std::floor(q.x / kCell)), static_cast<long>(std::floor(q.y / kCell)), static_cast<long>(std::floor(q.z / kCell)));
            auto found = cells[zone].find(key);
            if (found == cells[zone].end()) found = cells[zone].emplace(key, Box{q, q}).first;
            found->second.low = lowest(found->second.low, q);
            found->second.high = highest(found->second.high, q);
            part.low = lowest(part.low, q);
            part.high = highest(part.high, q);
        }
        if (part.low.x > part.high.x) continue;
        zoneBox[zone].low = lowest(zoneBox[zone].low, part.low);
        zoneBox[zone].high = highest(zoneBox[zone].high, part.high);
        layout->plan[zone].push_back({part.low.x, part.low.z, part.high.x, part.high.z});
        extent = highest(extent, highest(Vec3d{std::fabs(part.low.x), std::fabs(part.low.y), std::fabs(part.low.z)}, Vec3d{std::fabs(part.high.x), std::fabs(part.high.y), std::fabs(part.high.z)}));
    }

    for (std::size_t z = 0; z < static_cast<std::size_t>(kHullZoneCount); ++z) {
        // Una zona sin piezas no recibe golpes; su centro sigue siendo el de la maqueta.
        if (cells[z].empty()) {
            layout->centers[z] = ZoneInfo(static_cast<int>(z)).center;
            layout->bounds[z] = {layout->centers[z], 0.0};
            continue;
        }
        const Vec3d center = (zoneBox[z].low + zoneBox[z].high) * 0.5;
        layout->centers[z] = center;
        double reach = 0.0;
        for (const auto& [key, box] : cells[z]) {
            const ZoneBall ball{(box.low + box.high) * 0.5, std::max(Length(box.high - box.low) * 0.5, kMinBall)};
            layout->balls[z].push_back(ball);
            reach = std::max(reach, Length(ball.center - center) + ball.radius);
        }
        layout->bounds[z] = {center, reach};
    }
    layout->extent = extent;

    // El radio del anillo: lo mas alejado del eje, sin contar los radios ni el cubo.
    const auto& ring = layout->balls[static_cast<std::size_t>(HullZone::WarpRing)];
    double far = 0.0;
    for (const ZoneBall& b : ring) far = std::max(far, std::hypot(b.center.x, b.center.y));
    double sum = 0.0;
    int count = 0;
    for (const ZoneBall& b : ring) {
        const double r = std::hypot(b.center.x, b.center.y);
        if (r > far * 0.7) {
            sum += r;
            ++count;
        }
    }
    if (count > 0) layout->ringRadius = sum / count;
    return layout;
}

// --- Modulos y mejoras visibles: en metros, con la base en el origen y +Y hacia fuera del casco ---

MeshData MakeTurret() {
    MeshData m;
    AppendMesh(m, MakeCylinder(13.0f, 6.0f, 24), {0, 3.0f, 0});      // anillo de giro
    AppendMesh(m, MakeBox(20.0f, 10.0f, 24.0f), {0, 11.0f, -2.0f});  // cuerpo
    AppendMesh(m, MakeSphere(7.0f, 16), {0, 15.0f, -8.0f});          // optica
    for (const float side : {-4.5f, 4.5f}) AppendMesh(m, MakeCylinder(2.0f, 34.0f, 12), {side, 11.0f, 24.0f}, Quatf::FromAxisAngle({1, 0, 0}, 1.5707963f));
    return m;
}

MeshData MakeRadiatorFin() {
    MeshData m;
    AppendMesh(m, MakeCylinder(3.0f, 10.0f, 12), {0, 5.0f, 0});
    AppendMesh(m, MakeBox(3.0f, 70.0f, 46.0f), {0, 45.0f, 0});
    for (int i = 0; i < 5; ++i) AppendMesh(m, MakeBox(5.0f, 2.0f, 46.0f), {0, 16.0f + 14.0f * static_cast<float>(i), 0});
    return m;
}

MeshData MakeSensorDish() {
    MeshData m;
    AppendMesh(m, MakeCylinder(2.5f, 26.0f, 12), {0, 13.0f, 0});
    MeshData dish = MakeSphere(16.0f, 20);
    for (MeshVertex& v : dish.vertices) v.position.y = std::max(v.position.y * 0.3f, -1.5f);  // un plato
    ComputeNormals(dish);
    AppendMesh(m, dish, {0, 28.0f, 0}, Quatf::FromAxisAngle({1, 0, 0}, 0.5f));
    AppendMesh(m, MakeCylinder(1.0f, 12.0f, 8), {0, 34.0f, 3.0f}, Quatf::FromAxisAngle({1, 0, 0}, 0.5f));
    return m;
}

MeshData MakeArmorPlate() {
    MeshData m;
    AppendMesh(m, MakeBox(54.0f, 5.0f, 74.0f), {0, 2.5f, 0});
    for (const float x : {-20.0f, 20.0f})
        for (const float z : {-28.0f, 28.0f}) AppendMesh(m, MakeCylinder(2.5f, 2.0f, 10), {x, 6.0f, z});
    return m;
}

MeshData MakeDome() {
    MeshData m = MakeSphere(14.0f, 20);
    for (MeshVertex& v : m.vertices) v.position.y = std::max(v.position.y, 0.0f) * 0.8f;
    ComputeNormals(m);
    AppendMesh(m, MakeCylinder(15.0f, 3.0f, 20), {0, 1.5f, 0});
    return m;
}

MeshData MakeThrusterPod() {
    MeshData m;
    AppendMesh(m, MakeBox(10.0f, 8.0f, 10.0f), {0, 4.0f, 0});
    AppendMesh(m, MakeCylinder(2.2f, 6.0f, 10), {0, 10.0f, 0});
    AppendMesh(m, MakeCylinder(2.2f, 6.0f, 10), {7.0f, 4.0f, 0}, Quatf::FromAxisAngle({0, 0, 1}, 1.5707963f));
    AppendMesh(m, MakeCylinder(2.2f, 6.0f, 10), {-7.0f, 4.0f, 0}, Quatf::FromAxisAngle({0, 0, 1}, 1.5707963f));
    return m;
}

// Un escudo termico: un disco abombado de radio 1, que se escala al dibujarlo.
MeshData MakeHeatCap() {
    MeshData m = MakeSphere(1.0f, 24);
    for (MeshVertex& v : m.vertices) v.position.y = std::max(v.position.y, 0.0f) * 0.25f;
    ComputeNormals(m);
    return m;
}

// Bahia de drones de reserva (sin el glTF): la losa del hangar sobre la base, +Y hacia fuera.
MeshData MakeDroneBay() {
    MeshData m;
    AppendMesh(m, MakeCylinder(14.0f, 2.0f, 24), {0, 1.0f, 0});
    AppendMesh(m, MakeBox(25.0f, 6.0f, 21.0f), {0, 5.0f, 0});
    AppendMesh(m, MakeBox(8.0f, 4.0f, 16.0f), {-8.0f, 10.0f, 0});
    AppendMesh(m, MakeBox(8.0f, 4.0f, 16.0f), {8.0f, 10.0f, 0});
    return m;
}

// Dron de reserva: cuerpo, bodega y broca, con la proa a +Z y -Y arriba, en metros.
ModelRef BuildDroneMaquette(const MeshUpload& upload) {
    ImportedPart body;
    body.name = "dron_casco";
    AppendMesh(body.mesh, MakeBox(9.6f, 7.8f, 24.0f), {0, -1.0f, -2.0f});
    AppendMesh(body.mesh, MakeCylinder(5.2f, 16.0f, 16), {0, 5.0f, -3.0f}, Quatf::FromAxisAngle({1, 0, 0}, 1.5707963f));
    AppendMesh(body.mesh, MakeCylinder(3.0f, 9.0f, 12), {0, -1.0f, 15.0f}, Quatf::FromAxisAngle({1, 0, 0}, 1.5707963f));
    body.color = {0.82f, 0.80f, 0.74f, 1.0f};
    return BuildModel("Dron minero", {body}, upload);
}

std::shared_ptr<ShipDesign> BuildShipDesign(const ModelAsset& ship, const std::vector<ImportedMarker>& markers) {
    auto design = std::make_shared<ShipDesign>();
    for (const ModelPart& p : ship.parts) {
        DesignPiece piece;
        piece.name = p.name;
        piece.zone = ZoneOfPart(p.name, p.position);
        piece.center = {p.position.x, p.position.y, p.position.z};
        piece.radius = std::max(static_cast<double>(p.radius * p.scale), 1.0);
        if (p.collision && !p.collision->vertices.empty()) {
            const Mat4f turn = p.rotation.ToMatrix();
            const std::size_t count = p.collision->vertices.size(), step = std::max<std::size_t>(count / 96, 1);
            for (std::size_t v = 0; v < count; v += step) {
                const Vec3f w = turn.TransformDirection(p.collision->vertices[v].position * p.scale) + p.position;
                piece.samples.push_back({w.x, w.y, w.z});
            }
        }
        design->pieces.push_back(piece);
    }
    // Anclajes del modelo: los nodos vacios "hp_..."; sin ninguno, los de la maqueta.
    std::vector<ModuleSlot> hardpoints;
    for (const ImportedMarker& m : markers) {
        if (m.name.rfind("hp_", 0) != 0 || m.name.find("atraque") != std::string::npos) continue;
        ModuleSlot s;
        s.name = m.name;
        s.kind = SlotKind::External;
        s.position = {m.position.x, m.position.y, m.position.z};
        s.normal = Normalize(Vec3d{m.up.x, m.up.y, m.up.z} + Vec3d{0, 1e-9, 0});
        s.zone = NearestZone(s.position);
        hardpoints.push_back(s);
    }
    if (hardpoints.empty()) hardpoints = MaquetteHardpoints();
    std::sort(hardpoints.begin(), hardpoints.end(), [](const ModuleSlot& a, const ModuleSlot& b) { return a.name < b.name; });
    // Cada anclaje va montado en la pieza que tiene debajo.
    for (ModuleSlot& s : hardpoints) {
        double nearest = 1e300;
        for (std::size_t i = 0; i < design->pieces.size(); ++i) {
            const double gap = Length(design->pieces[i].center - s.position) - design->pieces[i].radius;
            if (gap < nearest) {
                nearest = gap;
                s.piece = static_cast<int>(i);
            }
        }
        if (s.piece >= 0) s.zone = design->pieces[static_cast<std::size_t>(s.piece)].zone;
        design->slots.push_back(s);
    }
    for (const ModuleSlot& s : InteriorBays()) design->slots.push_back(s);

    // Compuerta de atraque: el nodo "atraque", o el punto mas a estribor del puente cerca de su
    // ecuador (en la maqueta, el costado de la caja del puente).
    bool fixed = false;
    for (const ImportedMarker& m : markers)
        if (m.name.find("atraque") != std::string::npos) {
            design->dockPort = {m.position.x, m.position.y, m.position.z};
            design->dockNormal = Normalize(Vec3d{m.up.x, m.up.y, m.up.z} + Vec3d{1e-9, 0, 0});
            fixed = true;
        }
    if (!fixed) {
        const int bridge = static_cast<int>(HullZone::Bridge);
        double cy = 0.0, count = 0.0;
        for (const DesignPiece& p : design->pieces)
            if (p.zone == bridge)
                for (const Vec3d& s : p.samples) {
                    cy += s.y;
                    count += 1.0;
                }
        cy = count > 0.0 ? cy / count : 0.0;
        double best = -1e300;
        for (const DesignPiece& p : design->pieces) {
            if (p.zone != bridge) continue;
            for (const Vec3d& s : p.samples) {
                const double score = s.x - std::fabs(s.y - cy) * 2.0;  // a estribor y sin subir ni bajar
                if (score > best) {
                    best = score;
                    design->dockPort = s;
                }
            }
        }
    }
    return design;
}

GameAssets BuildGameAssets(const MeshUpload& upload, TextureId hull) {
    GameAssets a;
    a.ship = BuildShip(upload, hull);
    a.moduleMeshes[static_cast<std::size_t>(ShipModuleKind::Turret)] = upload(MakeTurret());
    a.moduleMeshes[static_cast<std::size_t>(ShipModuleKind::Radiator)] = upload(MakeRadiatorFin());
    a.moduleMeshes[static_cast<std::size_t>(ShipModuleKind::SensorArray)] = upload(MakeSensorDish());
    a.moduleMeshes[static_cast<std::size_t>(ShipModuleKind::ArmorPlate)] = upload(MakeArmorPlate());
    a.moduleMeshes[static_cast<std::size_t>(ShipModuleKind::DroneBay)] = upload(MakeDroneBay());
    // Armas humanas y defensa puntual: sin torretas.gltf (LoadCombatModels), se ven como la torreta.
    for (int k = static_cast<int>(ShipModuleKind::PointDefensePlatform); k < static_cast<int>(ShipModuleKind::Count); ++k)
        a.moduleMeshes[static_cast<std::size_t>(k)] = a.moduleMeshes[static_cast<std::size_t>(ShipModuleKind::Turret)];
    a.drone = BuildDroneMaquette(upload);
    a.dome = upload(MakeDome());
    a.thrusterPod = upload(MakeThrusterPod());
    a.heatCap = upload(MakeHeatCap());
    a.rcsSlit = upload(MakeBox(18.0f, 1.2f, 5.0f));
    a.unitBox = upload(MakeBox(1.0f, 1.0f, 1.0f));
    a.girder = upload(MakeGirder());
    a.plate = upload(MakePlate());
    a.shard = upload(MakeShard());
    a.powerStation = BuildPower(upload, hull);
    a.hostileShip = BuildAlienShip(upload, true);
    a.neutralShip = BuildAlienShip(upload, false);
    // Dron, corveta y fragata: la maqueta hasta que LoadCombatModels lea sus modelos.
    a.hostileClassShips.fill(a.hostileShip);
    a.neutralClassShips.fill(a.neutralShip);
    a.hostileStation = BuildAlienStation(upload, true);
    a.neutralStation = BuildAlienStation(upload, false);
    for (int m = 0; m < kAlienModelCount; ++m) RebuildAlienZones(a, static_cast<AlienModel>(m));
    a.derelict = BuildDerelict(upload);
    a.armorPlanet = BuildArmorPlanet(upload, AnomalyRules{}.armorPlanetRadius);
    for (std::uint32_t seed = 1; seed <= 4; ++seed) a.debris.push_back(upload(MakeAsteroid(seed * 53u, 2)));
    a.powerDeck = BuildDeck(upload, StationKind::Power);
    a.pilot = BuildPerson(upload, Color{1.0f, 0.55f, 0.16f, 1.0f}, "Capitan");
    a.crew = BuildPerson(upload, Color{0.30f, 0.48f, 0.62f, 1.0f}, "Tripulante");
    return a;
}

const ModelRef& ModelFor(const GameAssets& assets, const Anomaly& a) {
    if (IsArmorPlanet(a)) return assets.armorPlanet;
    const int model = AlienModelOf(a);
    return model >= 0 ? AlienModelAsset(assets, static_cast<AlienModel>(model)) : assets.derelict;
}

const ModelRef& AlienModelAsset(const GameAssets& assets, AlienModel model) {
    switch (model) {
        case AlienModel::HostileShip: return assets.hostileShip;
        case AlienModel::NeutralShip: return assets.neutralShip;
        case AlienModel::HostileStation: return assets.hostileStation;
        case AlienModel::NeutralStation: return assets.neutralStation;
        default: {
            const int m = static_cast<int>(model);
            const int hostile = static_cast<int>(AlienModel::HostileDrone), neutral = static_cast<int>(AlienModel::NeutralDrone);
            if (m >= neutral) return assets.neutralClassShips[static_cast<std::size_t>(std::min(m - neutral, kAlienClassModelCount - 1))];
            return assets.hostileClassShips[static_cast<std::size_t>(std::clamp(m - hostile, 0, kAlienClassModelCount - 1))];
        }
    }
}

std::shared_ptr<ZoneLayout> BuildAlienZoneLayout(const ModelAsset& model, AlienModel kind) {
    const bool station = kind == AlienModel::HostileStation || kind == AlienModel::NeutralStation;
    // Un bastion mide kilometros: bolas mas grandes, o serian miles. Una nave pequena (un dron de 34 m)
    // las necesita mas finas, o una bola de su cuerpo taparia sus cuchillas: un decimo de su medida.
    double size = 0.0;
    for (const ModelPart& p : model.parts) size = std::max(size, static_cast<double>(Length(p.position) + p.radius * p.scale));
    const double cell = station ? 150.0 : std::clamp(size * 0.1, 2.0, 30.0);
    std::shared_ptr<ZoneLayout> layout = BuildZoneLayout(model, AlienZoneOfPart, cell);
    layout->touch = station ? 4.0 : 0.5;
    const auto name = [&](HullZone z, const char* text, bool masculine) {
        layout->names[static_cast<std::size_t>(z)] = text;
        layout->masculine[static_cast<std::size_t>(z)] = masculine;
    };
    // Las naves, por los nombres de sus piezas: sirve para las maquetas y para los modelos por clase.
    const auto has = [&](const char* word) {
        for (const ModelPart& p : model.parts)
            if (p.name.find(word) != std::string::npos) return true;
        return false;
    };
    switch (kind) {
        case AlienModel::HostileStation:
        case AlienModel::NeutralStation: break;
        default:
            name(HullZone::Spine, has("disco") ? "disco" : "cuerpo", true);
            if (has("centro")) name(HullZone::Core, "centro", true);
            else if (has("ojo")) name(HullZone::Core, "ojo", true);
            else name(HullZone::Core, "núcleo", true);
            name(HullZone::Bridge, "corazón", true);
            name(HullZone::WarpRing, "aro", true);
            name(HullZone::PortNacelle, "cuchilla de babor", false);
            name(HullZone::StarboardNacelle, "cuchilla de estribor", false);
            return layout;
    }
    switch (kind) {
        default:
            name(HullZone::Core, "núcleo", true);
            name(HullZone::Bridge, "corazón", true);
            name(HullZone::WarpRing, "anillo", true);
            for (int i = 0; i < 6; ++i) {
                layout->names[static_cast<std::size_t>(HullZone::Hold1) + static_cast<std::size_t>(i)] = "brazo " + std::to_string(i + 1);
                layout->masculine[static_cast<std::size_t>(HullZone::Hold1) + static_cast<std::size_t>(i)] = true;
            }
            break;
    }
    return layout;
}

void RegisterAlienZones(const GameAssets& assets) {
    for (int m = 0; m < kAlienModelCount; ++m) SetAlienZones(static_cast<AlienModel>(m), assets.alienZones[static_cast<std::size_t>(m)]);
}

}  // namespace game
