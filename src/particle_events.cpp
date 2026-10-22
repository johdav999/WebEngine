#include "engine/particle_emitter_component.hpp"
namespace engine {
void ParticleEmitterComponent::captureEvents(std::uint32_t mask,std::uint32_t capacity){
    if(mask>7||!capacity||capacity>4096)throw std::invalid_argument("Invalid particle event capture");
    if(mask)events_.reserve(capacity);
    eventMask_=mask;eventCapacity_=capacity;events_.clear();
}
void ParticleEmitterComponent::event(ParticleEventType type,const Particle& p){
    if(!(eventMask_&(1u<<std::uint32_t(type))))return;
    if(events_.size()>=eventCapacity_){++droppedEvents_;return;}
    const bool local=definition_->space==ParticleSpace::Local;const auto world=local?worldMatrix():Mat4::identity();
    Vec3 normal=p.planeNormal;
    if(local){try{normal=normalized(transformDirection(transpose(inverseAffine(world)),normal));}catch(const std::invalid_argument&){normal={0,1,0};}}
    events_.push_back({type,transformPoint(world,p.position),normal,transformDirection(world,p.velocity)});
}
}
