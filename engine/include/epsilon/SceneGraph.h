// SceneGraph.h - Jerarquia de transformaciones: galaxia > sistema > planeta > luna.
//
// Mundo(hijo) = Mundo(padre) * Local(hijo)
//
// Generaliza NodoCuerpoCeleste::orbitas. Los nodos se agrupan por profundidad: los de un
// mismo nivel no dependen entre si y se calculan en paralelo.
// Las posiciones usan double; las coordenadas float pierden precision lejos del origen.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "epsilon/JobSystem.h"
#include "epsilon/Math3D.h"

namespace eps {

using NodeId = std::int32_t;
inline constexpr NodeId kNoNode = -1;

struct Transform {
    Vec3d position;
    Quatd rotation;
    Vec3d scale{1, 1, 1};

    Mat4d ToMatrix() const { return Translation(position) * rotation.ToMatrix() * Scaling(scale); }
};

class SceneGraph {
public:
    // El padre debe existir antes que el hijo.
    NodeId CreateNode(NodeId parent, const Transform& local = {});

    // Libera el nodo y todos sus descendientes. Sus indices se reutilizan.
    void DestroyNode(NodeId id);
    bool Alive(NodeId id) const { return id >= 0 && Index(id) < alive_.size() && alive_[Index(id)]; }

    std::size_t NodeCount() const { return local_.size() - free_.size(); }
    NodeId Parent(NodeId id) const { return parent_[Index(id)]; }
    Transform& Local(NodeId id) { return local_[Index(id)]; }
    const Transform& Local(NodeId id) const { return local_[Index(id)]; }
    const Mat4d& World(NodeId id) const { return world_[Index(id)]; }
    Vec3d WorldPosition(NodeId id) const { return World(id).Translation(); }

    // Mayor factor de escala del nodo en el mundo, para radios de esfera.
    double WorldMaxScale(NodeId id) const;

    void UpdateWorld(JobSystem& jobs);

private:
    static std::size_t Index(NodeId id) { return static_cast<std::size_t>(id); }

    std::vector<NodeId> parent_;
    std::vector<int> depth_;
    std::vector<Transform> local_;
    std::vector<Mat4d> world_;
    std::vector<std::vector<NodeId>> levels_;
    std::vector<std::size_t> levelSlot_;   // posicion de cada nodo en su nivel
    std::vector<std::vector<NodeId>> children_;
    std::vector<char> alive_;
    std::vector<NodeId> free_;
};


}  // namespace eps
