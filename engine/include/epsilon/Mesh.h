// Mesh.h - Datos de una malla. No sabe nada de la GPU ni de como se cargo.
//
// Vive en el nucleo porque es el contrato entre quien produce mallas (modulo de modelos) y
// quien las dibuja (backend). Unidad: la que use la malla; la instancia indica su escala.
#pragma once

#include <cstdint>
#include <vector>

#include "epsilon/Math3D.h"

namespace eps {

struct MeshVertex {
    Vec3f position;
    Vec3f normal;
    float u = 0, v = 0;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;  // tres por triangulo
};

// Mayor distancia de un vertice al origen de la malla.
inline float MeshRadius(const MeshData& mesh) {
    float best = 0;
    for (const MeshVertex& v : mesh.vertices) best = std::max(best, Dot(v.position, v.position));
    return std::sqrt(best);
}

}  // namespace eps
