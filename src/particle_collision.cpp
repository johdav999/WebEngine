#include "engine/particle_emitter_component.hpp"
#include "engine/scene.hpp"
#include "engine/physics.hpp"
#include "engine/collider_component.hpp"
namespace engine {
void ParticleCollisionSettings::validate() const {
    if(response<ParticleCollisionResponse::Bounce||response>ParticleCollisionResponse::Settle
        ||!std::isfinite(radius)||radius<.001f||radius>100||!std::isfinite(restitution)||restitution<0||restitution>1
        ||!std::isfinite(friction)||friction<0||friction>1||!std::isfinite(settleSpeed)||settleSpeed<0
        ||!maxQueries||maxQueries>65536||!maxImpacts||maxImpacts>4096)
        throw std::invalid_argument("Invalid particle collision settings");
}
void ParticleEmitterComponent::collide(Particle& p,Vec3 previous,Vec3 previousVelocity,Vec3 added,float dt){
    const auto& c=definition_->collision;
    if(collisionQueries_>=c.maxQueries){p.position=previous;p.velocity=previousVelocity;++skippedCollisionSteps_;return;}
    if(!scene())return;
    const bool local=definition_->space==ParticleSpace::Local;
    Mat4 world=Mat4::identity(),inverse=Mat4::identity();
    if(local){world=worldMatrix();try{inverse=inverseAffine(world);}catch(const std::invalid_argument&){p.position=previous;p.velocity=previousVelocity;return;}}
    const auto start=transformPoint(world,previous),end=transformPoint(world,p.position),movement=end-start;
    const float distance=std::sqrt(dot(movement,movement));if(distance<=1e-7f)return;
    PhysicsQueryFilter filter;filter.layers=c.layers;filter.includeSensors=false;
    if(c.excludeOwner&&owner())filter.excludedActors.push_back(owner()->id());
    Transform pose;pose.position=start;++collisionQueries_;
    const auto hit=scene()->physics().sweep(*collisionShape_,pose,movement,distance,filter);
    if(!hit)return;
    const auto n=normalized(hit->normal);if(dot(n,n)<.5f){p.position=previous;p.velocity=previousVelocity;return;}
    const auto incoming=transformDirection(world,p.velocity+added);const float speed=std::sqrt(dot(incoming,incoming));
    if(eventMask_&2){if(events_.size()<eventCapacity_)events_.push_back({ParticleEventType::Impact,hit->point,n,incoming});else ++droppedEvents_;}
    if(impacts_.size()<c.maxImpacts)impacts_.push_back({hit->actor,hit->body,hit->point,n,incoming,speed,p.age+dt*hit->fraction});else ++droppedImpacts_;
    const auto center=start+movement*hit->fraction+n*(.001f+hit->penetrationDepth);
    p.position=transformPoint(inverse,center);
    if(c.response==ParticleCollisionResponse::Kill){p.age=p.lifetime;return;}
    const float vn=dot(incoming,n);
    Vec3 response=incoming;
    if(vn<0){const auto tangent=(incoming-n*vn)*(1-c.friction);response=tangent+(c.response==ParticleCollisionResponse::Bounce?n*(-vn*c.restitution):Vec3{});}
    if(c.response==ParticleCollisionResponse::Settle||dot(response,response)<=c.settleSpeed*c.settleSpeed){p.settled=true;p.velocity={};}
    else p.velocity=transformDirection(inverse,response)-added;
}
}
