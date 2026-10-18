#pragma once
#include "engine/actor.hpp"
#include "engine/physical_surface.hpp"
#include <array>
#include <optional>

namespace engine {
// Metres, kg, seconds; local +Z forward, +Y up; wheel order FL, FR, RL, RR.
struct VehicleWheelSettings {
    Vec3 position{};
    float radius=.35f,width=.2f,suspensionMin=.1f,suspensionMax=.45f;
    float suspensionFrequency=2,suspensionDamping=.6f,maxSteerAngle=0;
    float brakeTorque=1500,handBrakeTorque=0,friction=1;
};
struct VehicleDefinition {
    std::array<VehicleWheelSettings,4> wheels;
    float engineTorque=500,minRPM=1000,maxRPM=6000;
    std::array<float,5> forwardRatios{2.66f,1.78f,1.3f,1,.74f};
    float reverseRatio=-2.9f,differentialRatio=3.42f,frontTorqueFraction=0;
    float shiftUpRPM=4000,shiftDownRPM=2000,antiRollStiffness=1000;
    VehicleDefinition();
    void validate() const;
};
struct VehicleCommand {
    float throttle=0,steering=0,brake=0,handbrake=0;
    // Signed throttle selects automatic forward/reverse; steering +1 is right.
    void validate() const;
};
struct VehicleWheelState {
    bool contact=false;
    ActorId contactActor{};
    PhysicsSurfaceHit surface;
    float combinedLongitudinalFriction=0,combinedLateralFriction=0;
    Vec3 contactPoint{},contactNormal{};
    float suspensionLength=0,steerAngle=0,rotation=0,angularVelocity=0;
    float longitudinalSlip=0,lateralSlip=0;
    Mat4 localPose=Mat4::identity();
};
struct VehicleState {
    bool active=false;
    float speed=0,rpm=1000;
    int gear=0;
    std::array<VehicleWheelState,4> wheels;
};
// Deliberately partial resumable state, not a solver checkpoint. No held commands.
struct VehicleCheckpoint {
    float rpm=1000;
    int gear=0;
    std::array<float,4> rotation{},angularVelocity{};
};
class VehicleComponent : public Component {
public:
    explicit VehicleComponent(std::string name="Vehicle",std::shared_ptr<const VehicleDefinition> definition=std::make_shared<const VehicleDefinition>());
    std::string_view typeName() const noexcept override { return "VehicleComponent"; }
    const std::shared_ptr<const VehicleDefinition>& definition() const { return definition_; }
    void configure(std::shared_ptr<const VehicleDefinition> definition);
    void submit(VehicleCommand command);
    void cancel() noexcept { command_={}; }
    const VehicleCommand& command() const { return command_; }
    const VehicleState& state() const { return state_; }
    VehicleCheckpoint checkpoint() const;
    void restore(const VehicleCheckpoint& checkpoint);
    // Optional stable saved IDs of direct visual children of the chassis actor.
    const std::array<std::string,4>& visuals() const { return visuals_; }
    void setVisuals(std::array<std::string,4> ids);
    void validate() const override;
protected:
    void onDisable() noexcept override;
    void onDetach() noexcept override;
    void onOwnerChanged(Actor*,Actor*) noexcept override;
private:
    friend class PhysicsWorld;
    friend class ArchiveCodec;
    void validateVisualBindings() const;
    std::shared_ptr<const VehicleDefinition> definition_;
    VehicleCommand command_;
    VehicleState state_;
    std::array<std::string,4> visuals_;
    std::optional<VehicleCheckpoint> pending_;
};
} // namespace engine
