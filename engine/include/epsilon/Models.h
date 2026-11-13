// Models.h - Modelos 2D y 3D a escala humana. No depende del modulo astral.
//
// Todo aqui se mide en metros. Un LocalSpace es una burbuja de coordenadas locales enganchada
// a un nodo: una estacion que orbita un planeta, una nave o un campo de escombros.
//
// Por que hace falta: una posicion en km con double tiembla milimetros lejos de su origen, lo
// que se nota al mirar un panel a un metro. Dentro de la burbuja, el visitante y los modelos
// se miden entre si, asi que la precision no depende de donde este la estacion en el sistema.
//
// El puente con lo astral es solo el nodo de anclaje y la direccion del sol. Depth buffer,
// camara y luz son los mismos, por eso un planeta tapa bien a una nave y al reves.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "epsilon/Layer.h"
#include "epsilon/Mesh.h"

namespace eps {

// --- Mallas procedurales: la unidad es la que reciban --------------------------

MeshData MakeBox(float width, float height, float depth);
MeshData MakeCylinder(float radius, float height, int segments = 24);  // a lo largo de Y
MeshData MakeTorus(float radius, float tube, int segments = 48, int sides = 12);  // alrededor de Y
MeshData MakeSphere(float radius, int segments = 32);
MeshData MakeQuad(float width, float height);  // plano XY, mirando a -Z

// Roca irregular de radio aproximado 1. Cada semilla da una forma distinta.
MeshData MakeAsteroid(std::uint32_t seed, int subdivisions = 3);

// Copia una malla dentro de otra, aplicando una transformacion.
void AppendMesh(MeshData& to, const MeshData& from, const Vec3f& position = {}, const Quatf& rotation = {}, float scale = 1.0f);

// Calcula normales suaves a partir de los triangulos.
void ComputeNormals(MeshData& mesh);

// Parte las aristas mas largas que maxEdge hasta que ninguna lo sea, sin dejar grietas: hace falta
// para que una malla de pocas caras se pueda abollar vertice a vertice. Para al llegar a
// maxTriangles. Las normales y las UV del punto nuevo son la media de las de la arista.
void SubdivideLongEdges(MeshData& mesh, float maxEdge, std::size_t maxTriangles = 200000);

// --- Archivos ------------------------------------------------------------------

// Los formatos usan Y hacia arriba; el motor, -Y. Es una rotacion, asi que conserva la forma.
void ConvertYUpToEngine(MeshData& mesh);

// Wavefront OBJ: posiciones, normales, UV y caras de cualquier numero de lados.
bool LoadObj(const std::string& path, MeshData& out, std::string* error = nullptr);
bool ParseObj(const std::string& text, MeshData& out, std::string* error = nullptr);

struct ImportedPart {
    std::string name;
    MeshData mesh;              // ya en metros y con la transformacion de su nodo aplicada
    Color color;
    std::string texturePath;    // vacio si no tiene textura o va incrustada
    float emissive = 0;         // brillo propio: el mayor canal de emissiveFactor por su intensidad
};

// glTF 2.0 (.gltf o .glb). Requiere compilar con cgltf; sin el devuelve false.
bool LoadGltf(const std::string& path, std::vector<ImportedPart>& out, std::string* error = nullptr);
bool GltfSupported();

// Un nodo sin malla de un glTF: un punto de anclaje (una ranura, un soporte de arma).
struct ImportedMarker {
    std::string name;
    Vec3f position;      // m, ya en los ejes del motor
    Vec3f up{0, -1, 0};  // su +Y local, en los ejes del motor: hacia donde mira el anclaje
};
bool LoadGltfMarkers(const std::string& path, std::vector<ImportedMarker>& out, std::string* error = nullptr);

// Una animacion de un glTF, muestreada. Las partes de LoadGltf llevan ya aplicada la pose de reposo
// (la de los nodos); aqui esta lo que se mueve cada una respecto a esa pose, en los ejes del motor:
// un punto de la parte en reposo p pasa a estar en rotation * p + offset. Solo giro y traslacion: un
// cambio de escala en la animacion se ignora.
struct PartMotion {
    Vec3f offset;   // m
    Quatf rotation;
};

struct ModelAnimation {
    std::string name;
    double duration = 0;  // s
    double step = 0;      // s entre muestras
    std::vector<std::string> parts;                // nombre de cada parte, en el orden de LoadGltf
    std::vector<std::vector<PartMotion>> samples;  // [muestra][parte]

    // El movimiento de cada parte en el instante t (s), interpolado entre muestras y sujeto a
    // [0, duration]. out queda con una entrada por parte.
    void Pose(double t, std::vector<PartMotion>& out) const;
};

// Lee la animacion llamada name (vacio = la primera) y la muestrea a fps. Requiere cgltf.
bool LoadGltfAnimation(const std::string& path, const std::string& name, ModelAnimation& out, double fps = 24.0, std::string* error = nullptr);

// --- Modelos -------------------------------------------------------------------

struct ModelPart {
    std::string name;
    MeshId mesh = kNoMesh;
    TextureId texture = 0;
    Vec3f position;         // m, respecto al modelo
    Quatf rotation;
    float scale = 1;        // malla > metros
    float radius = 1;       // m, radio de la parte ya escalada
    Color color;
    float emissive = 0;
    bool billboard = false; // modelo 2D: siempre mira a la camara
    float spriteWidth = 1, spriteHeight = 1;  // m, solo en modelos 2D
    std::shared_ptr<const MeshData> collision;  // si existe, el rayo usa estos triangulos
};

struct ModelAsset {
    std::string name;
    std::vector<ModelPart> parts;
};
using ModelRef = std::shared_ptr<const ModelAsset>;

// Parte de un modelo 2D: un cuadro con textura. Usa MakeQuad(1, 1) como malla.
ModelPart MakeSpritePart(const std::string& name, MeshId unitQuad, TextureId texture, float widthMetres, float heightMetres);

// Convierte partes importadas en un modelo. upload recibe la malla y devuelve su MeshId.
ModelRef BuildModel(const std::string& name, const std::vector<ImportedPart>& parts, const std::function<MeshId(const MeshData&)>& upload);

struct RayHit {
    int instance = -1;
    int part = -1;
    double distance = 0;  // m
};

class LocalSpace {
public:
    using InstanceId = int;

    NodeId anchor = kNoNode;        // kNoNode = origen del mundo
    NodeId lightNode = kNoNode;     // de donde viene la luz
    Quatd orientation;              // de lo local al mundo
    Vec3d anchorOffset;             // km, respecto al nodo
    float ambient = 0.05f;

    // Con visitante, la camara esta dentro de la burbuja y todo se mide desde el.
    bool hasViewer = false;
    Vec3d viewer;                   // m

    InstanceId Spawn(ModelRef model, const Vec3d& position = {}, const Quatd& rotation = {}, float scale = 1.0f);
    void Remove(InstanceId id);
    int InstanceCount() const;

    Vec3d& Position(InstanceId id) { return instances_[Slot(id)].position; }
    Quatd& Rotation(InstanceId id) { return instances_[Slot(id)].rotation; }
    void SetVisible(InstanceId id, bool visible) { instances_[Slot(id)].visible = visible; }

    // Devuelven false si no hay ninguna parte con ese nombre.
    bool SetPartVisible(InstanceId id, const std::string& part, bool visible);
    bool SetPartColor(InstanceId id, const std::string& part, const Color& color, float emissive = 0.0f);
    const std::string& PartName(InstanceId id, int part) const;

    // Rayo en metros locales contra las partes visibles: sus triangulos o, si no los tiene, su esfera.
    RayHit Raycast(const Vec3d& origin, const Vec3d& direction, double maxDistance = 1.0e9) const;

    // Punto del mundo, en km, de una posicion local.
    Vec3d ToWorldKm(const SceneGraph& scene, const Vec3d& localMetres) const;

    void Collect(const FrameContext& c, FramePacket& out) const;

private:
    struct PartState {
        bool visible = true;
        Color color;
        float emissive = 0;
    };
    struct Instance {
        ModelRef model;
        Vec3d position;
        Quatd rotation;
        float scale = 1;
        bool visible = true;
        bool used = false;
        std::vector<PartState> parts;
    };
    std::size_t Slot(InstanceId id) const { return static_cast<std::size_t>(id); }
    std::vector<Instance> instances_;
    std::vector<InstanceId> free_;
};

// Lleva las burbujas al fotograma. Los LocalSpace deben sobrevivir a la capa.
class ModelLayer final : public IFrameLayer {
public:
    void Add(LocalSpace* space) { spaces_.push_back(space); }
    void Collect(const FrameContext& c, FramePacket& out) override;

private:
    std::vector<LocalSpace*> spaces_;
};

}  // namespace eps
