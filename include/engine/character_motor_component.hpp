#pragma once
#include "engine/actor.hpp"

namespace engine {
// Commands are world-space metres/second, independent of any input device.
// Horizontal velocity persists until replaced. Jump is latched until the next
// fixed step (multiple submissions before a step cannot erase a jump request).
struct MovementCommand {
    Vec3 velocity{}; // Y must be zero in collision-aware mode; free mode accepts XYZ.
    bool jump=false;
};
struct CharacterMotorSettings {
    bool collisionAware=true;
    float radius=0.3f,halfHeight=0.6f; // Capsule cylinder half height; origin is feet.
    float maxSlopeAngle=pi/4,stepHeight=0.3f,groundSnapDistance=0.3f;
    float jumpSpeed=5,gravity=9.81f; // +Y up; gravity is a nonnegative magnitude.
    float padding=0.02f,maxPushForce=100,mass=70;
    std::uint8_t collisionLayer=0;
    bool operator==(const CharacterMotorSettings&) const = default;
    void validate() const;
};
enum class GroundState { InAir, Grounded, Steep, Unsupported };
struct MovementResult {
    Vec3 requestedDisplacement{},displacement{},velocity{}; // velocity = displacement / dt.
    Vec3 groundNormal{},groundVelocity{};
    ActorId groundActor;
    ComponentId groundBody;
    GroundState groundState=GroundState::InAir;
    bool grounded=false,jumped=false,landed=false,hitWall=false,hitCeiling=false;
    bool contactLimitExceeded=false;
};
// Optional upright capsule motor. Owns no rigid body/collider component and has
// no input dependency. Scene fixed updates move it after the rigid-body solver.
class CharacterMotorComponent : public Component {
public:
    explicit CharacterMotorComponent(std::string name="Character motor",CharacterMotorSettings settings={});
    std::string_view typeName() const noexcept override { return "CharacterMotorComponent"; }
    const CharacterMotorSettings& settings() const { return settings_; }
    void configure(const CharacterMotorSettings& settings); // Resets velocity and result.
    void submitMovement(const MovementCommand& command);
    MovementCommand movementCommand() const { return {command_.velocity,false}; }
    void stop(); // Clears persistent velocity and pending jump.
    const MovementResult& movementResult() const { return result_; }
    // World foot position. Clears velocity/results/jump; movement command persists.
    void teleport(Vec3 position);
    void validate() const override;
protected:
    void fixedTick(double seconds) override;
    void onDetach() noexcept override;
    void onDisable() noexcept override;
    void onOwnerChanged(Actor*,Actor*) noexcept override;
private:
    friend class PhysicsWorld;
    CharacterMotorSettings settings_;
    MovementCommand command_;
    bool jumpPending_=false;
    MovementResult result_;
    void resetRuntime() noexcept;
};
} // namespace engine
