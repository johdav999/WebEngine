#include "engine/dynamic_navigation.hpp"
#include "engine/collider_component.hpp"
#include "engine/scene.hpp"
#include "engine/performance_timeline.hpp"
namespace engine {
void DynamicNavigationSettings::validate() const {build.validate();if(!std::isfinite(rebuildInterval)||rebuildInterval<0||rebuildInterval>10||!std::isfinite(movementThreshold)||movementThreshold<0||movementThreshold>1)throw std::invalid_argument("Invalid dynamic navigation settings");}
DynamicNavigation::DynamicNavigation(DynamicNavigationSettings settings):settings_(std::move(settings)){settings_.validate();}
void DynamicNavigation::invalidate(){source_->invalidate();stats_.dirty=true;failed_=false;stats_.error.clear();}
void DynamicNavigation::track(Scene& scene,ComponentId id){
    scene.synchronize();auto* body=dynamic_cast<RigidBodyComponent*>(scene.findComponent(id));
    if(!body||!body->isActiveInHierarchy()||body->settings().isSensor||(scene_&&scene_!=id.scene))throw std::invalid_argument("Track a solid body in the bound navigation scene");
    if(std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){return e.body==id;}))return;
    if(entries_.size()==64)throw std::invalid_argument("Dynamic navigation tracks at most 64 bodies");
    const auto bounds=scene.physics().bodyBounds(id);auto staged=entries_;staged.push_back({id,bounds});invalidate();entries_=std::move(staged);scene_=id.scene;
}
bool DynamicNavigation::untrack(ComponentId id){const auto it=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.body==id;});if(it==entries_.end())return false;invalidate();entries_.erase(it);return true;}
void DynamicNavigation::retry(){failed_=false;remaining_=0;stats_.error.clear();}
bool DynamicNavigation::prepare(Scene& scene,double seconds){
    ProfileScope profile(ProfileCategory::AI,"Dynamic navigation preparation");
    if(!std::isfinite(seconds)||seconds<0||seconds>.25)throw std::invalid_argument("Invalid dynamic navigation time step");
    if(scene_&&scene.runtimeIdentity()!=scene_)throw std::invalid_argument("Dynamic navigation belongs to another scene");
    scene.synchronize();scene_=scene.runtimeIdentity();remaining_=std::max(0.,remaining_-seconds);bool changed=false;std::vector<NavMeshObstacle> obstacles;auto filter=settings_.geometry;
    for(auto& e:entries_){std::optional<PhysicsBounds> now;auto* body=dynamic_cast<RigidBodyComponent*>(scene.findComponent(e.body));
        if(body&&body->isActiveInHierarchy()&&!body->settings().isSensor){now=scene.physics().bodyBounds(e.body);obstacles.push_back({now->minimum,now->maximum});}
        auto delta=[](Vec3 a,Vec3 b){return std::max({std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)});};
        if(now.has_value()!=e.bounds.has_value()||(now&&e.bounds&&(delta(now->minimum,e.bounds->minimum)>settings_.movementThreshold||delta(now->maximum,e.bounds->maximum)>settings_.movementThreshold))){changed=true;e.bounds=now;}
        filter.excludedBodies.push_back(e.body);
    }
    if(changed)invalidate();stats_.obstacles=obstacles.size();stats_.dirty=!source_->ready();
    if(source_->ready()||failed_||remaining_>0)return false;
    remaining_=settings_.rebuildInterval;
    try {auto candidate=NavMesh::bakeWithObstacles(scene,obstacles,settings_.build,filter);if(!source_->publish(source_->revision(),candidate))return false;++stats_.builds;stats_.dirty=false;stats_.error.clear();return true;}
    catch(const std::exception& e){++stats_.failures;stats_.dirty=true;stats_.error=e.what();failed_=true;return false;}
}
}
