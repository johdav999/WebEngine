#pragma once
#include "engine/scene.hpp"
#include "engine/mesh_component.hpp"

namespace engine {
// Static ships share one fitted asset; only per-ship transforms are allocated.
class ShipStressDemo {
public:
    explicit ShipStressDemo(Scene& scene):scene_(scene) {}
    static constexpr int maxShips=10000;
    void setAsset(std::shared_ptr<const Mesh> mesh);
    bool hasAsset() const { return bool(mesh_); }
    void setCount(int count);
    void stop();
    bool active() const { return bool(root_); }
    int count() const { return count_; }
    const Actor* root() const { return scene_.findActor(root_); }
private:
    Scene& scene_;
    std::shared_ptr<const Mesh> mesh_;
    ActorId root_;
    int count_=0;
};
} // namespace engine
