#include "engine/crowd_navigation.hpp"
#include "engine/scene.hpp"
#include "engine/collider_component.hpp"
#include "engine/performance_timeline.hpp"
#include <numeric>
namespace engine {
namespace {
float length(Vec3 v){return std::hypot(v.x,v.z);}
float squared(Vec3 v){return v.x*v.x+v.z*v.z;}
Vec3 position(const Actor& a){return transformPoint(a.worldMatrix(),{});}
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
float closest(Vec3 p,Vec3 velocity,float horizon){const auto v=squared(velocity);const auto t=v>1e-8f?std::clamp(-(p.x*velocity.x+p.z*velocity.z)/v,0.f,horizon):0;return length(p+velocity*t);}
}
CrowdSettings::CrowdSettings(){navigation.following.stuckSeconds=4;navigation.following.maxSeconds=navigation.maxSeconds=120;navigation.maxReplans=32;navigation.queryBudget=256;}
void CrowdSettings::validate() const {navigation.validate();if(!maxAgents||maxAgents>64||!maxNeighbors||maxNeighbors>64||!obstacleTests||obstacleTests>32||!std::isfinite(neighborDistance)||neighborDistance<=0||neighborDistance>32||!std::isfinite(timeHorizon)||timeHorizon<.1f||timeHorizon>5||!std::isfinite(separation)||separation<0||separation>1)throw std::invalid_argument("Invalid crowd settings");}
CrowdNavigation::CrowdNavigation(CrowdSettings settings):settings_(std::move(settings)){settings_.validate();entries_.reserve(settings_.maxAgents);}
void CrowdNavigation::add(CharacterMotorComponent& motor,std::shared_ptr<const NavigationSource> source,Vec3 goal){
    if(!motor.id()||!motor.owner()||!motor.settings().collisionAware||!source||!finite(goal)||(scene_&&scene_!=motor.id().scene))throw std::invalid_argument("Invalid crowd agent/source/goal");
    if(entries_.size()>=settings_.maxAgents||std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){return e.motor==motor.id();}))throw std::invalid_argument("Crowd capacity or duplicate motor");
    const auto& m=motor.settings();auto probe=CollisionShape::capsule(m.halfHeight,m.radius+settings_.navigation.clearance);
    Navigator nav(settings_.navigation);nav.navigateSource(source,goal);entries_.push_back({motor.id(),std::move(source),std::move(nav),{motor.id()},std::move(probe),m.radius,m.halfHeight});scene_=motor.id().scene;
}
void CrowdNavigation::setGoal(ComponentId id,Vec3 goal){const auto it=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.motor==id;});if(it==entries_.end())throw std::invalid_argument("Unknown crowd motor");it->navigator.navigateSource(it->source,goal);it->state.yielding=false;}
bool CrowdNavigation::remove(Scene& scene,ComponentId id){if(scene_&&scene.runtimeIdentity()!=scene_)throw std::invalid_argument("Crowd belongs to another scene");const auto it=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.motor==id;});if(it==entries_.end())return false;if(auto* m=dynamic_cast<CharacterMotorComponent*>(scene.findComponent(id)))it->navigator.cancel(*m);entries_.erase(it);return true;}
void CrowdNavigation::clear(Scene& scene) noexcept {if(scene_&&scene.runtimeIdentity()!=scene_)return;for(auto& e:entries_)if(auto* m=dynamic_cast<CharacterMotorComponent*>(scene.findComponent(e.motor)))e.navigator.cancel(*m);entries_.clear();scene_=0;stats_={};}
std::vector<CrowdAgentState> CrowdNavigation::snapshot() const {std::vector<CrowdAgentState> out;for(const auto& e:entries_)out.push_back(e.state);return out;}
void CrowdNavigation::step(Scene& scene,double dt){
    ProfileScope profile(ProfileCategory::AI,"Crowd navigation");if(!std::isfinite(dt)||dt<=0||dt>.25||(scene_&&scene_!=scene.runtimeIdentity()))throw std::invalid_argument("Invalid crowd scene/fixed step");stats_={};
    try {
    const auto count=entries_.size();std::array<CharacterMotorComponent*,64> motors{};std::array<float,64> radii{},heights{};std::array<Vec3,64> actual{},output{};
    // Planning reads the same authoritative pose snapshot. No motor has moved yet.
    for(std::size_t i=0;i<count;++i){auto& e=entries_[i];auto* m=dynamic_cast<CharacterMotorComponent*>(scene.findComponent(e.motor));e.state.neighbors=0;
        if(!m||!m->owner()||!m->isActiveInHierarchy()||!m->fixedTickEnabled()||!m->settings().collisionAware){if(m)e.navigator.cancel(*m);e.state.status=NavigationStatus::Cancelled;e.state.velocity=e.state.preferred={};continue;}
        if(m->settings().radius!=e.probeRadius||m->settings().halfHeight!=e.probeHalfHeight){auto probe=CollisionShape::capsule(m->settings().halfHeight,m->settings().radius+settings_.navigation.clearance);e.probe=std::move(probe);e.probeRadius=m->settings().radius;e.probeHalfHeight=m->settings().halfHeight;}
        motors[i]=m;++stats_.agents;radii[i]=m->settings().radius;heights[i]=2*(m->settings().radius+m->settings().halfHeight);actual[i]=m->movementResult().velocity;
        e.state.position=position(*m->owner());e.navigator.step(*m,dt,e.state.yielding);e.state.status=e.navigator.status();e.state.preferred=m->movementCommand().velocity;e.state.preferred.y=0;
    }
    for(std::size_t i=0;i<count;++i)if(motors[i]){
        auto& e=entries_[i];const auto preferred=e.state.preferred;const auto speed=length(preferred);if(speed<1e-6f){output[i]={};continue;}
        std::vector<std::pair<float,std::size_t>> near;
        for(std::size_t j=0;j<count;++j)if(j!=i&&motors[j]&&e.state.position.y<entries_[j].state.position.y+heights[j]&&entries_[j].state.position.y<e.state.position.y+heights[i]){const auto d=squared(entries_[j].state.position-e.state.position);if(d<=settings_.neighborDistance*settings_.neighborDistance)near.push_back({d,j});}
        std::sort(near.begin(),near.end());if(near.size()>settings_.maxNeighbors)near.resize(settings_.maxNeighbors);e.state.neighbors=near.size();stats_.neighbors+=near.size();
        struct Candidate{Vec3 velocity;float score;std::size_t order;};std::vector<Candidate> candidates;
        auto add=[&](Vec3 v,float bias){float score=squared(v-preferred)/std::max(.01f,speed*speed)+.15f*squared(v-e.state.velocity)/std::max(.01f,speed*speed)+bias;
            for(const auto& [d,j]:near){const auto offset=entries_[j].state.position-e.state.position;const auto minimum=radii[i]+radii[j]+settings_.separation;const auto separation=closest(offset,actual[j]-v,settings_.timeHorizon);if(separation<minimum)score+=100+500*(1-separation/minimum);}
            candidates.push_back({v,score,candidates.size()});++stats_.candidates;};
        add({},0);for(float fraction:{1.f,.5f})for(float angle:{0.f,15.f,-15.f,30.f,-30.f,45.f,-45.f,60.f,-60.f,90.f,-90.f,135.f,-135.f,180.f}){const auto r=angle*pi/180;add({(preferred.x*std::cos(r)+preferred.z*std::sin(r))*fraction,0,(-preferred.x*std::sin(r)+preferred.z*std::cos(r))*fraction},angle<0?.025f:0);}
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score<b.score;});
        std::size_t tests=0;const auto* world=scene.physicsIfCreated();const auto nav=std::dynamic_pointer_cast<const NavMesh>(e.source->surface());
        for(const auto& c:candidates){if(length(c.velocity)<1e-6f){output[i]={};break;}if(tests==settings_.obstacleTests)break;++tests;
            // The navigator owns validated route-following tolerances. Apply the
            // extra surface ray to altered trajectories, not its existing intent.
            const bool detour=std::abs(c.velocity.x*preferred.z-c.velocity.z*preferred.x)>1e-5f||dot(c.velocity,preferred)<0;
            const auto look=e.state.position+c.velocity*float(dt);if(nav&&detour&&!nav->canTraverseSegment(e.state.position,look))continue;
            if(world){auto filter=settings_.navigation.obstacles;filter.includeSensors=false;filter.excludedActors.push_back(motors[i]->owner()->id());for(unsigned layer=0;layer<16;++layer)if(!world->layerCollision(motors[i]->settings().collisionLayer,std::uint8_t(layer)))filter.layers=std::uint16_t(filter.layers&~(1u<<layer));Transform pose;pose.position=e.state.position+Vec3{0,motors[i]->settings().halfHeight+motors[i]->settings().radius+settings_.navigation.clearance+settings_.navigation.groundLift,0};++stats_.obstacleQueries;if(world->sweep(*e.probe,pose,normalized(c.velocity),length(c.velocity)*float(std::max(dt,.35)),filter))continue;}
            output[i]=c.velocity;break;
        }
    }
    // Simultaneous next-step guard; virtual motors have no mutual solver bodies.
    for(unsigned pass=0;pass<4;++pass)for(std::size_t i=0;i<count;++i)if(motors[i])for(std::size_t j=i+1;j<count;++j)if(motors[j]){
        const auto offset=entries_[j].state.position-entries_[i].state.position;if(std::abs(offset.y)>=std::max(heights[i],heights[j]))continue;
        const auto minimum=radii[i]+radii[j]+settings_.separation*.25f;const auto relative=output[j]-output[i];
        if(closest(offset,relative,float(dt))<minimum&&length(offset+relative*float(dt))<length(offset)){output[i]=output[j]={};++stats_.safetyStops;}
    }
    for(std::size_t i=0;i<count;++i)if(motors[i]){auto& e=entries_[i];e.state.velocity=output[i];e.state.yielding=e.state.neighbors>0&&length(output[i])<length(e.state.preferred)*.3f;motors[i]->submitMovement({output[i],false});}
    }catch(...){clear(scene);throw;}
}
}
