#pragma once
#include "engine/camera_component.hpp"
#include "engine/input.hpp"

namespace engine {
struct OrbitState {
    Vec3 target{0,1,0},resetTarget{0,1,0};
    float yaw=0.7f,elevation=0.45f,distance=11;
};
// Reusable orbit controller. Target and distance use the camera's parent coordinate system.
class OrbitController {
public:
    explicit OrbitController(CameraComponent& camera,bool preserveCamera=false);
    OrbitState snapshot() const;
    void restore(const OrbitState& state);
    Vec3 target{0,1,0};
    void orbit(float dx,float dy);
    void zoom(float delta);
    void pan(float dx,float dy,float viewportHeight);
    void update(const InputSystem& input,float dt,float viewportHeight);
    void reset();
private:
    friend class ArchiveCodec;
    CameraComponent& camera_;
    float yaw_=0.7f, elevation_=0.45f, distance_=11;
    Vec3 resetTarget_{0,1,0}, lastAppliedTarget_{0,1,0};
    void apply();
};
} // namespace engine
