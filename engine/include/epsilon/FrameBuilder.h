// FrameBuilder.h - Convierte la escena en un FramePacket usando todos los nucleos.
//
// Sustituye a GRAFICOS::DibujarVista3D, que en un solo hilo mezclaba transformaciones,
// dibujado, cambios de estado (ActualizarEstrellas) y seleccion (sistemaApuntado).
//
// Etapas: transformar y recortar en paralelo > concatenar > ordenar en paralelo > agrupar.
#pragma once

#include <memory>
#include <vector>

#include "epsilon/Camera.h"
#include "epsilon/JobSystem.h"
#include "epsilon/Features.h"
#include "epsilon/Layer.h"
#include "epsilon/RenderTypes.h"
#include "epsilon/SceneGraph.h"

namespace eps {

struct FrameInput {
    const SceneGraph* scene = nullptr;
    const std::vector<Renderable>* renderables = nullptr;
    const std::vector<LinkRenderable>* links = nullptr;  // opcional
    std::vector<IFrameLayer*> layers;                    // lo que aportan los modulos
    FeatureSet features;
    PostSettings post;
    std::shared_ptr<const std::vector<SkyStar>> sky;     // opcional
    std::shared_ptr<const std::vector<SurfaceLook>> surfaces;  // opcional; sin ella no hay esferas
    LodSettings lod;
    Camera camera;
    double time = 0;     // segundos reales: animaciones de pantalla, titileo, nubes
    double simTime = 0;  // segundos simulados: giro de los cuerpos
    std::uint64_t frameNumber = 0;
    Color clearColor{0.02f, 0.02f, 0.04f, 1.0f};
    Color screenTint{0, 0, 0, 0};
    float fogDistance = 0;
    Color fogColor;
    Vec3f fogOrigin;
    float fogScale = 0;  // km de la baldosa de ruido de la nube; 0 = niebla de siempre
    bool fogRampOn = false;
    std::array<Color, 4> fogRamp{};
    float fogTone = 0;
    TextureId skyTexture = kNoTexture;
    Color skyTint;
};

class FrameBuilder {
public:
    // El paquete se reutiliza entre fotogramas para evitar reservas de memoria.
    void Build(const FrameInput& input, JobSystem& jobs, FramePacket& out);

private:
    std::vector<std::vector<SpriteInstance>> spriteChunks_;
    std::vector<std::vector<SphereInstance>> sphereChunks_;
    std::vector<std::vector<MeshInstance>> meshChunks_;
    std::vector<std::vector<LineInstance>> lineChunks_;
};

// Orientacion de un cuerpo: inclinacion del eje por el giro acumulado en simTime.
Quatf BodyOrientation(const Renderable& r, double simTime);

// Capas de ruido que caben sin bajar de 2 pixeles por ondulacion. Recibe los pixeles que
// ocupa un radio en el punto mas cercano de la superficie: focal * radio / altura.
// Lejos quedan pocas, asi que la superficie se ve difusa y cuesta menos.
float SurfaceDetail(float pixelsPerRadius, const LodSettings& lod);

// Brillo titilante como funcion pura del tiempo: el render no guarda estado.
float SkyStarBrightness(const SkyStar& star, double time);

// Estrellas distribuidas uniformemente sobre la esfera. Misma semilla, mismo cielo.
std::shared_ptr<const std::vector<SkyStar>> GenerateSky(std::size_t count, std::uint32_t seed);

}  // namespace eps
