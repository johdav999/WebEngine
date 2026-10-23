#pragma once
#include "engine/planet.hpp"
#include "engine/physics.hpp"
#include "engine/collider_component.hpp"

namespace engine {
// Planet-centred radial gravity in m/s^2. Surface acceleration is configurable;
// inverse-square falloff applies outside the radius, bounded to surface strength
// inside. No scene, solver, render-origin or clock ownership.
PlanetPosition planetGravity(PlanetPosition position,double radius,double surfaceAcceleration=9.81);

struct PlanetCollisionSettings {
    double halfExtent=256;
    std::uint32_t cells=128;
    void validate() const;
};
// Immutable source collision in a stable local translation frame. There are no
// visual LODs, morphs, skirts or view-dependent samples in this resource.
struct PlanetCollisionPatch {
    PlanetPosition origin,east,north,up;
    double halfExtent=0;
    std::uint32_t cells=0;
    std::shared_ptr<const CollisionShape> shape;
    bool contains(PlanetPosition position,double margin=0) const;
};
std::shared_ptr<const PlanetCollisionPatch> preparePlanetCollision(
    const PlanetTerrain&,PlanetPosition focus,PlanetCollisionSettings settings={});

struct PlanetMotionSettings {
    float radius=.65f,skin=.02f,snapDistance=.22f,jumpSpeed=5;
    float maxSlopeAngle=pi*.28f;
    double surfaceGravity=9.81;
    std::uint32_t slideIterations=6;
    std::uint16_t collisionLayers=0xffff;
    void validate() const;
};
struct PlanetMotionCommand {
    PlanetPosition velocity; // Desired planet-frame m/s. Ground mode uses its tangent.
    bool poweredFlight=false; // Velocity control compensates gravity (thrusters).
    bool jump=false; // One fixed-step request; only supported ground may jump.
};
struct PlanetMotionState {
    PlanetPosition position,velocity,gravity;
    Vec3 groundNormal{};
    ComponentId groundBody;
    bool grounded=false,landed=false,jumped=false,waitingForCollision=false,contactLimited=false;
    std::uint64_t steps=0,landings=0,jumps=0,sweeps=0;
};
// Optional spherical explorer/lander motor over existing convex world queries.
// Caller owns scene, collision publication and fixed-step order. World coordinates
// are patch-relative floats; motion position/velocity stay planet-frame doubles.
// A supplied patch must be the one currently registered at world origin zero.
// A null world/patch is permitted only above the 1500 m collision activation shell.
class PlanetSurfaceMotor {
public:
    explicit PlanetSurfaceMotor(PlanetMotionSettings settings={});
    const PlanetMotionSettings& settings() const { return settings_; }
    const PlanetMotionState& state() const { return state_; }
    void reset(PlanetPosition position,PlanetPosition velocity={});
    void step(const PlanetTerrain&,const PlanetMotionCommand&,double fixedSeconds,
        const PhysicsWorld* world=nullptr,const PlanetCollisionPatch* patch=nullptr);
private:
    PlanetMotionSettings settings_;
    PlanetMotionState state_;
    std::shared_ptr<const CollisionShape> shape_;
    bool initialized_=false;
};
} // namespace engine
