#include "crowd_demo.hpp"
#include "engine/scene.hpp"
#include "engine/collider_component.hpp"
#include "engine/mesh_component.hpp"
using namespace engine;
namespace {
struct Coordinator:Actor {
    CrowdNavigation crowd;
    Coordinator():Actor("Crowd coordinator"){setTickEnabled(true);}
    void fixedTick(double dt) override {crowd.step(*scene(),dt);for(const auto& s:crowd.snapshot())if(auto* m=dynamic_cast<CharacterMotorComponent*>(scene()->findComponent(s.motor));m&&m->owner()&&dot(s.velocity,s.velocity)>1e-5f){const auto yaw=std::atan2(s.velocity.x,s.velocity.z);for(auto* visual:m->owner()->getComponents<MeshComponent>())if(visual->name=="Facing"){visual->transform.rotation.y=yaw;visual->transform.position={std::sin(yaw)*.35f,1.3f,std::cos(yaw)*.35f};}}}
    void onDisable() noexcept override {if(scene())crowd.clear(*scene());}
    void onDespawn() noexcept override {if(scene())crowd.clear(*scene());}
};
Actor& solid(Scene& scene,const char* name,Vec3 p,Vec3 half,Vec3 color,BodyMotion motion=BodyMotion::Static){auto& a=scene.spawn(name);a.transform.position=p;RigidBodySettings s;s.motion=motion;a.addComponent<RigidBodyComponent>("Body",s);a.addComponent<ColliderComponent>("Shape",CollisionShape::box(half));auto& m=a.addComponent<MeshComponent>("Visible solid",Mesh::cube());m.transform.scale=half*2;m.color=color;return a;}
Coordinator* coordinator(Scene& scene,ActorId id){return dynamic_cast<Coordinator*>(scene.findActor(id));}
}
void CrowdDemo::start(Scene& scene,OrbitController& orbit){
    if(active())return;orbit_=orbit.snapshot();previous_.clear();for(const auto& n:scene.children()){previous_.push_back({n->savedId(),n->enabled});n->enabled=dynamic_cast<CameraComponent*>(n.get())!=nullptr;}
    try {
        auto& root=scene.spawn<Coordinator>();root_=root.id();scene.setChildIndex(root,0);
        owned_.push_back(solid(scene,"Crowd arena",{0,-.5f,0},{8,.5f,6},{.16f,.22f,.28f}).id());
        for(float z:{-6.2f,6.2f})owned_.push_back(solid(scene,"Arena boundary",{0,.6f,z},{8,.6f,.2f},{.25f,.34f,.42f}).id());
        auto& barrier=solid(scene,"Tracked moving gate",{0,1,4.8f},{.65f,1,1.5f},{.95f,.35f,.12f},BodyMotion::Kinematic);door_=barrier.id();owned_.push_back(door_);
        auto& overlay=root.addComponent<MeshComponent>("Live navigation",Mesh::plane());overlay_=overlay.id();overlay.transform.position.y=.03f;overlay.castsShadow=false;MaterialInstance tint;tint.shader=MaterialShader::Unlit;tint.tint={.1f,.8f,.55f};tint.alphaMode=AlphaMode::Blend;tint.opacity=.15f;overlay.setMaterial(0,tint);
        scene.synchronize();navigation_=std::make_shared<DynamicNavigation>();navigation_->track(scene,barrier.getComponent<RigidBodyComponent>()->id());navigation_->prepare(scene,0);reset(scene,1);
        orbit.restore({{0,0,0},{0,0,0},-.7f,1.1f,20});orbit.focus({0,0,0},20);debug(scene);
    }catch(...){stop(scene,orbit);throw;}
}
void CrowdDemo::stop(Scene& scene,OrbitController& orbit){
    if(!root_)return;if(auto* c=coordinator(scene,root_))c->crowd.clear(scene);for(auto id:agents_)if(auto* a=scene.findActor(id))a->destroy();for(auto id:owned_)if(auto* a=scene.findActor(id))a->destroy();if(auto* a=scene.findActor(root_))a->destroy();scene.flushPendingDestroy();
    for(const auto& [id,on]:previous_)if(auto* n=scene.findSavedNode(id))n->enabled=on;orbit.restore(orbit_);orbit.focus(orbit_.target,orbit_.distance);root_={};door_={};agents_.clear();owned_.clear();previous_.clear();navigation_.reset();shown_.reset();closed_=false;
}
void CrowdDemo::reset(Scene& scene,int mode){
    auto* c=coordinator(scene,root_);if(!c||mode<0||mode>1)throw std::invalid_argument("Open crowd arena first");c->crowd.clear(scene);for(auto id:agents_)if(auto* a=scene.findActor(id))a->destroy();scene.flushPendingDestroy();agents_.clear();
    std::vector<Vec3> starts;if(mode==0)starts={{-5,0,0},{5,0,0}};else for(float x:{-6.f,6.f})for(float z:{-3.f,-1.f,1.f,3.f})starts.push_back({x,0,z});
    for(auto p:starts){auto& a=scene.spawn("Crowd character");a.transform.position=p;agents_.push_back(a.id());auto& motor=a.addComponent<CharacterMotorComponent>();auto& body=a.addComponent<MeshComponent>("Character",Mesh::sphere(12,8));body.transform.position.y=.9f;body.transform.scale={.6f,1.8f,.6f};body.color=p.x<0?Vec3{.12f,.65f,1}:Vec3{1,.65f,.13f};auto& nose=a.addComponent<MeshComponent>("Facing",Mesh::cube());nose.transform.position={0,1.3f,.35f};nose.transform.scale={.18f,.18f,.25f};nose.color={1,1,1};scene.synchronize();c->crowd.add(motor,navigation_->source(),{-p.x,0,p.z});}
}
void CrowdDemo::door(Scene& scene,bool closed){if(!active())throw std::invalid_argument("Open crowd arena first");if(auto* a=scene.findActor(door_)){navigation_->invalidate();auto p=a->transform;p.position.z=closed?0:4.8f;a->getComponent<RigidBodyComponent>()->teleport(p);closed_=closed;navigation_->prepare(scene,0);debug(scene);}}
void CrowdDemo::prepare(Scene& scene,double dt){if(active()&&navigation_)navigation_->prepare(scene,dt);}
void CrowdDemo::debug(Scene& scene){if(!navigation_)return;if(navigation_->source()->surface()!=shown_){shown_=navigation_->source()->surface();if(auto nav=std::dynamic_pointer_cast<const NavMesh>(shown_))if(auto* m=dynamic_cast<MeshComponent*>(scene.findComponent(overlay_)))m->mesh=nav->debugMesh();}}
std::vector<CrowdAgentState> CrowdDemo::agents(const Scene& scene) const {if(auto* c=dynamic_cast<const Coordinator*>(scene.findActor(root_)))return c->crowd.snapshot();return {};}
CrowdStats CrowdDemo::crowdStats(const Scene& scene) const {if(auto* c=dynamic_cast<const Coordinator*>(scene.findActor(root_)))return c->crowd.stats();return {};}
