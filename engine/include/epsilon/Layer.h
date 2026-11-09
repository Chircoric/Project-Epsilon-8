// Layer.h - Punto de union entre los modulos y el fotograma.
//
// El nucleo no conoce orbitas, asteroides ni modelos. Cada modulo aporta una capa que anade
// instancias al FramePacket; quitar el modulo es quitar la capa.
#pragma once

#include "epsilon/Camera.h"
#include "epsilon/Features.h"
#include "epsilon/JobSystem.h"
#include "epsilon/RenderTypes.h"
#include "epsilon/SceneGraph.h"

namespace eps {

// Lo que necesita una capa para decidir que dibujar. Todo es de solo lectura.
struct FrameContext {
    const SceneGraph& scene;
    const Camera& camera;
    const FeatureSet& features;
    JobSystem& jobs;
    const Frustum& frustum;       // relativo a la camara
    const Mat4f& viewRotation;    // mundo > vista
    Vec3d cameraPosition;         // km
    double time = 0;              // segundos reales
    double simTime = 0;           // segundos simulados
};

class IFrameLayer {
public:
    virtual ~IFrameLayer() = default;
    // Se llama una vez por fotograma, desde el hilo de simulacion, antes de ordenar los sprites.
    virtual void Collect(const FrameContext& context, FramePacket& out) = 0;
};

}  // namespace eps
