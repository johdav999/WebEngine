#include "engine/vehicle_component.hpp"
#include "engine/physics.hpp"
#include "engine/scene.hpp"
#include "engine/collider_component.hpp"
#include "engine/character_motor_component.hpp"
#include <cmath>
namespace engine {
namespace {
void bindings(const VehicleComponent& vehicle,const std::array<std::string,4>& ids) {
    for(int i=0;i<4;++i)if(!ids[i].empty()){
        if(!Node::validSavedId(ids[i]))throw std::invalid_argument("Invalid wheel visual ID");
        for(int j=0;j<i;++j)if(ids[j]==ids[i])throw std::invalid_argument("Duplicate wheel visual");
        if(vehicle.scene()){auto* node=vehicle.scene()->findSavedNode(ids[i]);if(!node||node->parent()!=vehicle.owner()||dynamic_cast<const RigidBodyComponent*>(node)||dynamic_cast<const ColliderComponent*>(node)||dynamic_cast<const CharacterMotorComponent*>(node)||dynamic_cast<const VehicleComponent*>(node))throw std::invalid_argument("Wheel visual must be a direct nonphysics chassis child");}
    }
}
void range(float v,float lo,float hi) {if(!std::isfinite(v)||v<lo||v>hi)throw std::invalid_argument("Vehicle value outside range");} }
VehicleDefinition::VehicleDefinition() {
    for(int i=0;i<4;++i) {auto& w=wheels[i];w.position={i%2? .85f:-.85f,0,i<2?1.35f:-1.35f};w.maxSteerAngle=i<2?.5f:0;w.handBrakeTorque=i<2?0:4000;}
}
void VehicleDefinition::validate() const {
    range(engineTorque,0,100000);range(minRPM,1,100000);range(maxRPM,minRPM+1,100000);
    range(shiftDownRPM,minRPM,maxRPM);range(shiftUpRPM,shiftDownRPM+1,maxRPM);
    range(reverseRatio,-100,-.01f);range(differentialRatio,.01f,100);range(frontTorqueFraction,0,1);range(antiRollStiffness,0,1000000);
    for(float r:forwardRatios)range(r,.01f,100);
    for(const auto& w:wheels) {
        range(w.position.x,-100,100);range(w.position.y,-100,100);range(w.position.z,-100,100);
        range(w.radius,.01f,10);range(w.width,.01f,10);range(w.suspensionMin,0,10);range(w.suspensionMax,w.suspensionMin+.001f,10);
        range(w.suspensionFrequency,.01f,100);range(w.suspensionDamping,.01f,10);range(w.maxSteerAngle,0,1.5f);
        range(w.brakeTorque,0,100000);range(w.handBrakeTorque,0,100000);range(w.friction,.01f,10);
    }
}
void VehicleCommand::validate() const {range(throttle,-1,1);range(steering,-1,1);range(brake,0,1);range(handbrake,0,1);}
VehicleComponent::VehicleComponent(std::string name,std::shared_ptr<const VehicleDefinition> definition):Component(std::move(name)) {configure(std::move(definition));}
void VehicleComponent::configure(std::shared_ptr<const VehicleDefinition> d) {if(!d)throw std::invalid_argument("Missing vehicle definition");d->validate();if(definition_!=d){cancel();state_={};pending_.reset();}definition_=std::move(d);}
void VehicleComponent::submit(VehicleCommand c) {c.validate();command_=c;}
VehicleCheckpoint VehicleComponent::checkpoint() const {if(pending_)return *pending_;VehicleCheckpoint c;c.rpm=state_.active?state_.rpm:definition_->minRPM;c.gear=state_.active?state_.gear:0;for(int i=0;i<4;++i){c.rotation[i]=state_.wheels[i].rotation;c.angularVelocity[i]=state_.wheels[i].angularVelocity;}return c;}
void VehicleComponent::restore(const VehicleCheckpoint& c) {range(c.rpm,0,definition_->maxRPM);if(c.gear< -1||c.gear>5)throw std::invalid_argument("Invalid vehicle gear");for(int i=0;i<4;++i){range(c.rotation[i],-1e8f,1e8f);range(c.angularVelocity[i],-1e5f,1e5f);}pending_=c;cancel();}
void VehicleComponent::setVisuals(std::array<std::string,4> ids) {bindings(*this,ids);visuals_=std::move(ids);}
void VehicleComponent::validateVisualBindings() const {bindings(*this,visuals_);}
void VehicleComponent::validate() const {Component::validate();definition_->validate();const auto& t=transform;if(t.position.x||t.position.y||t.position.z||t.rotation.x||t.rotation.y||t.rotation.z||t.scale.x!=1||t.scale.y!=1||t.scale.z!=1)throw std::invalid_argument("Vehicle component transform must be identity");}
void VehicleComponent::onDetach() noexcept {cancel();state_={};pending_.reset();if(scene()&&scene()->physicsIfCreated())scene()->physicsIfCreated()->removeVehicle(id());}
void VehicleComponent::onDisable() noexcept {onDetach();}
void VehicleComponent::onOwnerChanged(Actor* previous,Actor*) noexcept {if(previous)onDetach();}
}
