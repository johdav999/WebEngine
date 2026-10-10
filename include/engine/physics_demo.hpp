#pragma once
#include "engine/scene.hpp"
#include "engine/physics.hpp"

namespace engine {
// Reusable bounded playground; all bodies are scene-root actors.
class PhysicsDemo {
public:
    explicit PhysicsDemo(Scene& scene):scene_(scene) {}
    void start();
    void stop();
    void reset();
    void spawn(bool sphere);
    bool kick(Vec3 origin,Vec3 direction);
    bool active() const { return active_; }
    const auto& actors() const { return actors_; }
    ActorId selected() const { return selected_; }
private:
    Scene& scene_;
    std::vector<ActorId> actors_;
    ActorId selected_;
    bool active_=false;
    std::size_t serial_=0;
};
} // namespace engine
