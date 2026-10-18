#pragma once
#include "engine/component.hpp"
#include <limits>

namespace engine {
// Opaque, world-scoped runtime identity. Removed identities are never reused.
struct PhysicsJointId {
    std::uint64_t world=0,value=0;
    explicit operator bool() const { return world!=0&&value!=0; }
    bool operator==(const PhysicsJointId&) const = default;
};
struct PhysicsJointBodies {
    ComponentId first;
    ComponentId second; // Exactly {} attaches to the fixed world.
    bool collideConnected=false; // Does not override collision-layer exclusions.
};
// All anchors/axes are world-space at creation, then follow the attached bodies.
// At least one endpoint must be a dynamic solid with a registered solver body.
struct HingeJointSettings {
    PhysicsJointBodies bodies;
    Vec3 anchor;
    Vec3 axis{0,1,0};
    // Relative creation orientation is zero; radians, min <= 0 <= max.
    float minAngle=-3.14159265358979323846f,maxAngle=3.14159265358979323846f;
    float frictionTorque=0; // N m
};
struct SliderJointSettings {
    PhysicsJointBodies bodies;
    Vec3 anchor;
    Vec3 axis{1,0,0};
    // First anchor relative to second along axis; min <= 0 <= max.
    float minDistance=-std::numeric_limits<float>::max(),maxDistance=std::numeric_limits<float>::max();
    float frictionForce=0; // N
};
struct SpringJointSettings {
    PhysicsJointBodies bodies;
    Vec3 firstAnchor,secondAnchor;
    float restLength=1; // World units; zero gives a hard distance tether if frequency=0.
    float frequency=2,dampingRatio=0.7f; // Hz; 0 Hz = hard distance, damping in [0,1].
};
// A hard point constraint; holds the picked body point at a movable world target,
// leaving rotation free. The application owns pointer mapping and release policy.
struct GrabJointSettings {
    ComponentId body;
    Vec3 anchor,target;
};
} // namespace engine
