#pragma once
#include "engine/actor.hpp"
#include "engine/rigid_body_component.hpp"
#include <functional>
#include <optional>

namespace engine {
class Scene;
class CollisionShape;
struct SceneFrame;
enum class CollisionPhase { Begin, Stay, End };
struct CollisionEvent {
    CollisionPhase phase=CollisionPhase::Begin;
    ActorId first,second;
    ComponentId firstBody,secondBody;
    Vec3 point,normal; // Normal from first towards second; End has zero vectors.
};
struct PhysicsRayHit {
    ActorId actor;
    ComponentId body;
    Vec3 point,normal;
    float distance=0;
};
// Independent of the simulation layer-pair matrix. Exclusions use scene-local,
// generation-checked handles; stale/foreign handles cannot exclude a new body.
struct PhysicsQueryFilter {
    std::uint16_t layers=0xffff;
    bool includeStatic=true,includeDynamic=true,includeKinematic=true;
    bool includeSensors=true,includeSolids=true;
    std::vector<ActorId> excludedActors;
    std::vector<ComponentId> excludedBodies;
};
struct PhysicsOverlapHit {
    ActorId actor;
    ComponentId body;
    Vec3 point,normal; // Target surface point and outward normal towards query.
    float penetrationDepth=0;
};
struct PhysicsSweepHit {
    ActorId actor;
    ComponentId body;
    Vec3 point,normal; // Target surface point and outward normal towards query.
    float distance=0,fraction=0,penetrationDepth=0;
    bool startedOverlapping=false;
};
enum class TriggerPhase { Enter, Exit };
struct TriggerEvent {
    TriggerPhase phase=TriggerPhase::Enter;
    ActorId sensor,other;
    ComponentId sensorBody,otherBody;
};
struct PhysicsDebugLine { Vec3 from,to,color; };
struct PhysicsStats {
    std::size_t bodies=0,activeBodies=0;
    std::uint64_t steps=0,droppedEvents=0;
    double lastStepMilliseconds=0;
};
// One single-threaded Jolt system per scene. Jolt types stay out of public headers.
class PhysicsWorld {
public:
    explicit PhysicsWorld(Scene& scene);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&)=delete;
    PhysicsWorld& operator=(const PhysicsWorld&)=delete;
    void setGravity(Vec3 gravity);
    Vec3 gravity() const;
    bool layerCollision(std::uint8_t first,std::uint8_t second) const;
    void setLayerCollision(std::uint8_t first,std::uint8_t second,bool collide);
    std::optional<PhysicsRayHit> raycast(Vec3 origin,Vec3 direction,float maxDistance=1000,std::uint16_t layers=0xffff) const;
    std::optional<PhysicsRayHit> raycast(Vec3 origin,Vec3 direction,float maxDistance,const PhysicsQueryFilter& filter) const;
    // Convex query geometry (box, sphere, capsule, hull); unit-scale world pose.
    // Overlap returns one deepest hit per body, ordered by component handle.
    std::vector<PhysicsOverlapHit> overlap(const CollisionShape& shape,const Transform& worldPose,const PhysicsQueryFilter& filter={}) const;
    // Linear translation at a fixed orientation. Direction is normalized.
    // Initial penetration is reported at distance/fraction zero.
    std::optional<PhysicsSweepHit> sweep(const CollisionShape& shape,const Transform& worldPose,Vec3 direction,float maxDistance,const PhysicsQueryFilter& filter={}) const;
    // Called after the solver releases its body locks, inside the scene's traversal guard.
    // destroy() is safe; hierarchy detachment/reparenting remains forbidden here.
    std::function<void(const CollisionEvent&)> onCollision;
    // One Enter/Exit per directed sensor/body pair, sampled after each fixed step.
    // Exit retains original handles even after removal; resolve before dereferencing.
    std::function<void(const TriggerEvent&)> onTrigger;
    PhysicsStats stats() const;
    void setInterpolationAlpha(float alpha);
    void setDebugDrawEnabled(bool enabled) { debugDraw_=enabled; }
    bool debugDrawEnabled() const { return debugDraw_; }
    void debugLines(std::vector<PhysicsDebugLine>& output,std::size_t limit=2048) const;
private:
    friend class Scene;
    friend class SelectionSystem;
    friend class RigidBodyComponent;
    friend class ColliderComponent;
    friend class CharacterMotorComponent;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    float alpha_=1;
    bool debugDraw_=false;
    void synchronize();
    void step(double seconds);
    void removeBody(ComponentId id) noexcept;
    void removeCollider(ComponentId id) noexcept;
    void removeCharacter(ComponentId id) noexcept;
    void synchronizeCharacters();
    void stepCharacters(double seconds);
    bool hasBody(ComponentId id) const;
    Vec3 velocity(ComponentId id,bool angular) const;
    void command(ComponentId id,Vec3 value,int command);
    void teleport(ComponentId id,const Transform& pose,bool clearVelocity);
    void moveKinematic(ComponentId id,const Transform& pose);
    Mat4 renderMatrix(const Node& node,const Mat4& world) const;
    void appendDebugDraws(SceneFrame& frame) const;
};
// Frame delta is capped and excess catch-up time is discarded. A hidden/paused
// application must reset the clock instead of simulating its time away.
class FixedStepClock {
public:
    explicit FixedStepClock(double stepSeconds=1.0/60.0,std::size_t maxSteps=8);
    std::size_t advance(Scene& scene,double deltaSeconds);
    void reset() { accumulator_=0; }
    double alpha() const { return accumulator_/step_; }
    double droppedSeconds() const { return dropped_; }
private:
    double step_,accumulator_=0,dropped_=0;
    std::size_t maxSteps_;
};
} // namespace engine
