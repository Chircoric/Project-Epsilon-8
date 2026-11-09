#include "epsilon/SceneGraph.h"

#include <algorithm>
#include <stdexcept>

namespace eps {

namespace {
constexpr std::size_t kNodeGrain = 1024;
}


NodeId SceneGraph::CreateNode(NodeId parent, const Transform& local) {
    if (parent != kNoNode && !Alive(parent))
        throw std::out_of_range("SceneGraph::CreateNode: padre inexistente");

    const int depth = parent == kNoNode ? 0 : depth_[Index(parent)] + 1;

    NodeId id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
        parent_[Index(id)] = parent;
        depth_[Index(id)] = depth;
        local_[Index(id)] = local;
        world_[Index(id)] = Mat4d::Identity();
        alive_[Index(id)] = 1;
    } else {
        id = static_cast<NodeId>(local_.size());
        parent_.push_back(parent);
        depth_.push_back(depth);
        local_.push_back(local);
        world_.push_back(Mat4d::Identity());
        levelSlot_.push_back(0);
        children_.emplace_back();
        alive_.push_back(1);
    }

    if (levels_.size() <= static_cast<std::size_t>(depth)) levels_.resize(static_cast<std::size_t>(depth) + 1);
    std::vector<NodeId>& level = levels_[static_cast<std::size_t>(depth)];
    levelSlot_[Index(id)] = level.size();
    level.push_back(id);
    if (parent != kNoNode) children_[Index(parent)].push_back(id);
    return id;
}

void SceneGraph::DestroyNode(NodeId id) {
    if (!Alive(id)) throw std::out_of_range("SceneGraph::DestroyNode: nodo inexistente");

    // Primero los hijos. La lista se vacia antes para no modificarla mientras se recorre.
    std::vector<NodeId> children = std::move(children_[Index(id)]);
    children_[Index(id)].clear();
    for (const NodeId child : children) {
        parent_[Index(child)] = kNoNode;
        DestroyNode(child);
    }

    const NodeId parent = parent_[Index(id)];
    if (parent != kNoNode) {
        std::vector<NodeId>& siblings = children_[Index(parent)];
        siblings.erase(std::find(siblings.begin(), siblings.end(), id));
    }

    // Quitar del nivel en tiempo constante: el ultimo ocupa su lugar.
    std::vector<NodeId>& level = levels_[static_cast<std::size_t>(depth_[Index(id)])];
    const std::size_t slot = levelSlot_[Index(id)];
    level[slot] = level.back();
    levelSlot_[Index(level[slot])] = slot;
    level.pop_back();

    alive_[Index(id)] = 0;
    free_.push_back(id);
}

double SceneGraph::WorldMaxScale(NodeId id) const {
    const Mat4d& w = World(id);
    const double sx = Length(Vec3d{w.m[0][0], w.m[0][1], w.m[0][2]});
    const double sy = Length(Vec3d{w.m[1][0], w.m[1][1], w.m[1][2]});
    const double sz = Length(Vec3d{w.m[2][0], w.m[2][1], w.m[2][2]});
    return std::max({sx, sy, sz});
}

void SceneGraph::UpdateWorld(JobSystem& jobs) {
    for (const std::vector<NodeId>& level : levels_) {
        jobs.ParallelFor(level.size(), kNodeGrain, [&](std::size_t begin, std::size_t end, std::size_t) {
            for (std::size_t i = begin; i < end; ++i) {
                const std::size_t node = Index(level[i]);
                const Mat4d local = local_[node].ToMatrix();
                world_[node] = parent_[node] == kNoNode ? local : world_[Index(parent_[node])] * local;
            }
        });
    }
}


}  // namespace eps
