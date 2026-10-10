#include "engine/character_motor_component.hpp"
#include "engine/physics.hpp"
#include "engine/scene.hpp"

namespace engine {
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
}
void CharacterMotorSettings::validate() const {
    if(!std::isfinite(radius)||radius<=0||!std::isfinite(halfHeight)||halfHeight<=0
        ||!std::isfinite(maxSlopeAngle)||maxSlopeAngle<=0||maxSlopeAngle>=pi/2
        ||!std::isfinite(stepHeight)||stepHeight<0||!std::isfinite(groundSnapDistance)||groundSnapDistance<0
        ||!std::isfinite(jumpSpeed)||jumpSpeed<0||!std::isfinite(gravity)||gravity<0
        ||!std::isfinite(padding)||padding<0||padding>=radius
        ||!std::isfinite(maxPushForce)||maxPushForce<0||!std::isfinite(mass)||mass<=0||collisionLayer>=16
        ||!std::isfinite(2*(halfHeight+radius)))
        throw std::invalid_argument("Invalid character motor settings");
}
CharacterMotorComponent::CharacterMotorComponent(std::string name,CharacterMotorSettings settings):Component(std::move(name)) {
    settings.validate();settings_=settings;setFixedTickEnabled(true);
}
void CharacterMotorComponent::resetRuntime() noexcept {
    if(scene()&&scene()->physicsIfCreated()) scene()->physicsIfCreated()->removeCharacter(id());
    result_={};jumpPending_=false;
}
void CharacterMotorComponent::configure(const CharacterMotorSettings& settings) {
    settings.validate();
    if(settings.collisionAware&&command_.velocity.y!=0) throw std::invalid_argument("Collision-aware movement velocity must be horizontal");
    resetRuntime();settings_=settings;
}
void CharacterMotorComponent::submitMovement(const MovementCommand& command) {
    if(!finite(command.velocity)||(settings_.collisionAware&&command.velocity.y!=0))
        throw std::invalid_argument("Invalid movement command");
    command_=command;jumpPending_=jumpPending_||command.jump;
}
void CharacterMotorComponent::stop() { command_={};jumpPending_=false; }
void CharacterMotorComponent::teleport(Vec3 position) {
    if(!finite(position)) throw std::invalid_argument("Nonfinite character position");
    if(!owner()||!scene()||!isActiveInHierarchy()) throw std::logic_error("Character motor is inactive");
    validate();auto matrix=owner()->worldMatrix();matrix.m[12]=position.x;matrix.m[13]=position.y;matrix.m[14]=position.z;
    owner()->setWorldTransform(matrix);resetRuntime();
}
void CharacterMotorComponent::validate() const {
    Component::validate();settings_.validate();
    if(transform.matrix().m!=Mat4::identity().m) throw std::invalid_argument("Character motor component transform must be identity");
    if(!scene()) return;
    const auto* actor=owner();
    if(!actor||parent()!=actor||actor->parent()!=scene()) throw std::invalid_argument("Character motors must be direct components of scene-root actors");
    const auto matrix=actor->worldMatrix();
    for(auto value:matrix.m) if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite character transform");
    const Vec3 x{matrix.m[0],matrix.m[1],matrix.m[2]},y{matrix.m[4],matrix.m[5],matrix.m[6]},z{matrix.m[8],matrix.m[9],matrix.m[10]};
    if(std::abs(dot(x,x)-1)>1e-4f||std::abs(dot(y,y)-1)>1e-4f||std::abs(dot(z,z)-1)>1e-4f
        ||std::abs(dot(x,y))>1e-4f||std::abs(dot(y,z))>1e-4f||std::abs(dot(x,z))>1e-4f||dot(x,cross(y,z))<0)
        throw std::invalid_argument("Character motors require unit world scale and no shear");
    std::size_t motors=0;
    for(const auto& child:actor->children()) if(child->isActiveInHierarchy()) {
        motors+=dynamic_cast<const CharacterMotorComponent*>(child.get())!=nullptr;
        if(dynamic_cast<const RigidBodyComponent*>(child.get())) throw std::invalid_argument("Character motor and rigid body cannot control the same actor");
    }
    if(motors!=1) throw std::invalid_argument("An actor supports one active character motor");
}
void CharacterMotorComponent::fixedTick(double seconds) {
    if(settings_.collisionAware) return; // The world's step solves collision-aware motors.
    validate();
    if(!std::isfinite(seconds)||seconds<=0||seconds>0.25) throw std::invalid_argument("Movement step must be in (0, 0.25] seconds");
    auto matrix=owner()->worldMatrix();const auto displacement=command_.velocity*static_cast<float>(seconds);
    const Vec3 position{matrix.m[12]+displacement.x,matrix.m[13]+displacement.y,matrix.m[14]+displacement.z};
    if(!finite(position)) throw std::invalid_argument("Unrepresentable movement position");
    matrix.m[12]=position.x;matrix.m[13]=position.y;matrix.m[14]=position.z;owner()->setWorldTransform(matrix);
    result_={};result_.requestedDisplacement=result_.displacement=displacement;result_.velocity=command_.velocity;jumpPending_=false;
}
void CharacterMotorComponent::onDetach() noexcept { resetRuntime();stop(); }
void CharacterMotorComponent::onDisable() noexcept { onDetach(); }
void CharacterMotorComponent::onOwnerChanged(Actor* previous,Actor*) noexcept { if(previous) onDetach(); }
} // namespace engine
