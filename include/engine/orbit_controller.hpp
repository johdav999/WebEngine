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
    void adoptCamera(float pivotDistance); // Capture current local eye/yaw/pitch without moving camera; roll is not an orbit degree of freedom.
    void restore(const OrbitState& state);
    Vec3 target{0,1,0};
    void orbit(float dx,float dy,float minimumElevation=0.05f,float maximumElevation=1.45f);
    void zoom(float delta,float minimumDistance=3,float maximumDistance=28);
    // Eye-fixed look and camera-local displacement (positive Z moves forward).
    void look(float dx,float dy);
    void translate(Vec3 displacement);
    void focus(Vec3 parentSpaceTarget,float distance);
    void pan(float dx,float dy,float viewportHeight);
    void update(const InputSystem& input,float dt,float viewportHeight);
    void reset();
private:
    friend class ArchiveCodec;
    CameraComponent& camera_;
    float yaw_=0.7f, elevation_=0.45f, distance_=11;
    Vec3 resetTarget_{0,1,0}, lastAppliedTarget_{0,1,0};
    void apply();
    void applyState(const OrbitState& state);
};
} // namespace engine
