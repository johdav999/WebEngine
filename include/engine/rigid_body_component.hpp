#pragma once
#include "engine/component.hpp"
#include <tuple>

namespace engine {
enum class BodyMotion { Static, Dynamic, Kinematic };
struct RigidBodySettings {
    BodyMotion motion=BodyMotion::Dynamic;
    float mass=1,friction=0.5f,restitution=0,gravityFactor=1;
    Vec3 centerOfMassOffset{}; // Shape COM offset, actor-local metres.
    Vec3 inertiaScale{1,1,1}; // Local tensor axis multiplier, positive.
    float linearDamping=0.05f,angularDamping=0.05f;
    std::uint8_t collisionLayer=0; // 0..15, separate from render visibility masks.
    bool continuousCollision=false;
    bool isSensor=false; // Trigger volume: overlaps without collision response.
    bool operator==(const RigidBodySettings& b) const {
        auto key=[](const auto& s){return std::tuple{s.motion,s.mass,s.friction,s.restitution,s.gravityFactor,s.centerOfMassOffset.x,s.centerOfMassOffset.y,s.centerOfMassOffset.z,s.inertiaScale.x,s.inertiaScale.y,s.inertiaScale.z,s.linearDamping,s.angularDamping,s.collisionLayer,s.continuousCollision,s.isSensor};};return key(*this)==key(b);
    }
    void validate() const;
};
class RigidBodyComponent : public Component {
public:
    explicit RigidBodyComponent(std::string name="Rigid body",RigidBodySettings settings={});
    std::string_view typeName() const noexcept override { return "RigidBodyComponent"; }
    const RigidBodySettings& settings() const { return settings_; }
    void configure(const RigidBodySettings& settings);
    bool hasBody() const;
    Vec3 linearVelocity() const;
    Vec3 angularVelocity() const;
    void setLinearVelocity(Vec3 velocity);
    void setAngularVelocity(Vec3 velocity);
    void addForce(Vec3 force);
    void addTorque(Vec3 torque); // World-space N m.
    void addForceAtPosition(Vec3 force,Vec3 worldPoint);
    void addImpulseAtPosition(Vec3 impulse,Vec3 worldPoint);
    Vec3 pointVelocity(Vec3 worldPoint) const;
    void addImpulse(Vec3 impulse);
    // World pose, unit scale. Also resets interpolation; optionally clears velocity.
    void teleport(const Transform& pose,bool clearVelocity=true);
    // Kinematic targets are reached during the next fixed step.
    void moveKinematic(const Transform& pose);
    void validate() const override;
protected:
    void onDetach() noexcept override;
    void onDisable() noexcept override;
    void onOwnerChanged(Actor*,Actor*) noexcept override;
private:
    RigidBodySettings settings_;
};
} // namespace engine
