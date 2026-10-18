#include "engine/camera_follow.hpp"
#include "engine/scene.hpp"
#include <algorithm>
namespace engine {
void CameraFollowSettings::validate() const {
    if(!target.empty()&&!Node::validSavedId(target))throw std::invalid_argument("Invalid camera follow saved target");
    auto vector=[](Vec3 v){for(float n:{v.x,v.y,v.z})if(!std::isfinite(n)||std::abs(n)>1000000)throw std::invalid_argument("Invalid camera follow offset");};vector(focusOffset);vector(offset);
    auto scalar=[](float v,float maximum){if(!std::isfinite(v)||v<0||v>maximum)throw std::invalid_argument("Invalid camera follow parameter");};
    scalar(lookAheadSeconds,10);scalar(maxLookAhead,10000);scalar(positionHalfLife,10);scalar(rotationHalfLife,10);scalar(collisionRadius,100);scalar(clearance,100);if(collisionRadius==0)throw std::invalid_argument("Camera collision radius must be positive");
}
void CameraComponent::setFollow(CameraFollowSettings value){value.validate();if(value.target==savedId())throw std::invalid_argument("Camera cannot follow itself");follow_=std::move(value);}
CameraFollowRig::CameraFollowRig(Scene& scene,ComponentId camera):scene_(scene),camera_(camera){if(!dynamic_cast<CameraComponent*>(scene.findComponent(camera)))throw std::invalid_argument("Invalid follow camera");}
void CameraFollowRig::reset(){controller_.reset();follow_=nullptr;tracking_=nullptr;smoothing_=nullptr;collision_=nullptr;target_.clear();targetActor_={};targetComponent_={};}
bool CameraFollowRig::update(double delta,float aspect){
    if(!std::isfinite(delta)||delta<0||!std::isfinite(aspect)||aspect<=0)throw std::invalid_argument("Invalid follow time/aspect");
    auto* camera=dynamic_cast<CameraComponent*>(scene_.findComponent(camera_));if(!camera||!camera->active()){reset();return false;}const auto& s=camera->follow();s.validate();if(!s.enabled){reset();return false;}
    auto* node=scene_.findSavedNode(s.target);if(!node||!node->isActiveInHierarchy()){reset();return false;}if(node==camera)throw std::invalid_argument("Camera cannot follow itself");
    CameraTarget target;if(auto* a=dynamic_cast<Actor*>(node))target=CameraTarget(a->id());else if(auto* c=dynamic_cast<Component*>(node))target=CameraTarget(c->id());else throw std::invalid_argument("Camera follows an actor or component");
    target.offset=s.focusOffset;target.poseSource=s.presentationPose?CameraPoseSource::Presentation:CameraPoseSource::Simulation;
    const auto actor=std::get_if<ActorId>(&target.source)?std::get<ActorId>(target.source):ActorId{};const auto component=std::get_if<ComponentId>(&target.source)?std::get<ComponentId>(target.source):ComponentId{};
    const bool fresh=actor!=targetActor_||component!=targetComponent_||!controller_||target_!=s.target||source_!=target.poseSource;if(fresh){reset();controller_=std::make_unique<CameraController>(scene_,camera_);follow_=&controller_->add<FollowCameraBehaviour>();tracking_=&controller_->add<TrackingCameraBehaviour>();smoothing_=&controller_->add<SmoothingCameraBehaviour>();collision_=&controller_->add<CollisionCameraBehaviour>();target_=s.target;source_=target.poseSource;targetActor_=actor;targetComponent_=component;}
    follow_->target=tracking_->target=target;follow_->offset=s.offset;follow_->offsetInTargetSpace=s.localOffset;tracking_->lookAheadSeconds=s.lookAheadSeconds;tracking_->maxLookAhead=s.maxLookAhead;
    smoothing_->positionHalfLife=fresh?0:s.positionHalfLife;smoothing_->rotationHalfLife=fresh?0:s.rotationHalfLife;smoothing_->lensHalfLife=0;
    collision_->enabled=s.collision;collision_->radius=s.collisionRadius;collision_->clearance=s.clearance;collision_->layers=s.layers;
    if(auto* a=dynamic_cast<Actor*>(node))collision_->ignoredActor=a->id();else if(auto* c=dynamic_cast<Component*>(node);c&&c->owner())collision_->ignoredActor=c->owner()->id();else collision_->ignoredActor={};
    return controller_->update(delta,aspect);
}
}
