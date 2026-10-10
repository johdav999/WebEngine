#include "engine/rigid_body_component.hpp"
#include "engine/physics.hpp"
#include "engine/scene.hpp"

namespace engine {
void RigidBodySettings::validate() const {
    if(motion!=BodyMotion::Static&&motion!=BodyMotion::Dynamic&&motion!=BodyMotion::Kinematic) throw std::invalid_argument("Invalid body motion");
    if(!std::isfinite(mass)||mass<=0||!std::isfinite(friction)||friction<0||friction>1
        ||!std::isfinite(restitution)||restitution<0||restitution>1||!std::isfinite(gravityFactor)
        ||!std::isfinite(linearDamping)||linearDamping<0||linearDamping>1
        ||!std::isfinite(angularDamping)||angularDamping<0||angularDamping>1||collisionLayer>=16)
        throw std::invalid_argument("Invalid rigid body settings");
}
RigidBodyComponent::RigidBodyComponent(std::string name,RigidBodySettings settings):Component(std::move(name)) { configure(settings); }
void RigidBodyComponent::configure(const RigidBodySettings& settings) { settings.validate();settings_=settings; }
void RigidBodyComponent::validate() const {
    Component::validate();settings_.validate();
    const auto& t=transform;
    if(t.position.x||t.position.y||t.position.z||t.rotation.x||t.rotation.y||t.rotation.z||t.scale.x!=1||t.scale.y!=1||t.scale.z!=1)
        throw std::invalid_argument("Rigid body component transform must be identity; position its actor");
}
namespace {
PhysicsWorld& world(const RigidBodyComponent& body) {
    if(!body.scene()||!body.scene()->physicsIfCreated()||!body.hasBody()) throw std::logic_error("Rigid body is not active; call scene.update(0) first");
    return *const_cast<Scene*>(body.scene())->physicsIfCreated();
}
}
bool RigidBodyComponent::hasBody() const { return scene()&&scene()->physicsIfCreated()&&scene()->physicsIfCreated()->hasBody(id()); }
Vec3 RigidBodyComponent::linearVelocity() const { return world(*this).velocity(id(),false); }
Vec3 RigidBodyComponent::angularVelocity() const { return world(*this).velocity(id(),true); }
void RigidBodyComponent::setLinearVelocity(Vec3 value) { world(*this).command(id(),value,0); }
void RigidBodyComponent::setAngularVelocity(Vec3 value) { world(*this).command(id(),value,1); }
void RigidBodyComponent::addForce(Vec3 value) { world(*this).command(id(),value,2); }
void RigidBodyComponent::addImpulse(Vec3 value) { world(*this).command(id(),value,3); }
void RigidBodyComponent::teleport(const Transform& pose,bool clearVelocity) { world(*this).teleport(id(),pose,clearVelocity); }
void RigidBodyComponent::moveKinematic(const Transform& pose) { world(*this).moveKinematic(id(),pose); }
void RigidBodyComponent::onDetach() noexcept { if(scene()&&scene()->physicsIfCreated()) scene()->physicsIfCreated()->removeBody(id()); }
void RigidBodyComponent::onDisable() noexcept { onDetach(); }
void RigidBodyComponent::onOwnerChanged(Actor*,Actor*) noexcept { onDetach(); }
} // namespace engine
