#include "engine/physics_demo.hpp"
#include "engine/collider_component.hpp"
#include "engine/mesh_component.hpp"

namespace engine {
void PhysicsDemo::start() { active_=true;reset(); }
void PhysicsDemo::stop() {
    active_=false;
    for(auto id:actors_) if(auto* actor=scene_.findActor(id)) actor->enabled=false;
    if(auto* physics=scene_.physicsIfCreated()) physics->setDebugDrawEnabled(false);
    scene_.update(0);selected_={};
}
void PhysicsDemo::reset() {
    if(!active_) throw std::logic_error("Start the physics playground first");
    for(auto id:actors_) if(auto* actor=scene_.findActor(id)) actor->destroy();
    scene_.flushPendingDestroy();actors_.clear();selected_={};serial_=0;
    auto& ground=scene_.spawn("Physics floor");ground.transform.position={0,-0.25f,0};
    RigidBodySettings settings;settings.motion=BodyMotion::Static;settings.friction=0.7f;
    ground.addComponent<RigidBodyComponent>("Floor body",settings);
    ground.addComponent<ColliderComponent>("Floor collider",CollisionShape::box({8,0.25f,8}));actors_.push_back(ground.id());
    for(int i=0;i<6;++i) spawn(i%2!=0);
    scene_.update(0);
}
void PhysicsDemo::spawn(bool sphere) {
    if(!active_) throw std::logic_error("Start the physics playground first");
    if(actors_.size()>=65) throw std::length_error("Playground body limit reached (64); reset to start again");
    auto& actor=scene_.spawn(sphere ? "Physics sphere" : "Physics box");
    actor.transform.position={float(int(serial_%3)-1)*1.7f,2.5f+float(serial_/3)*1.3f,float(int(serial_%2))*0.8f};
    if(!sphere) actor.transform.rotation={0.15f,0.2f*float(serial_),0.1f};
    static const auto cube=Mesh::cube(),ball=Mesh::sphere(16,8);
    auto& mesh=actor.addComponent<MeshComponent>("Body mesh",sphere ? ball : cube);
    mesh.color=sphere ? Vec3{0.95f,0.38f,0.13f} : Vec3{0.1f,0.6f,0.9f};
    RigidBodySettings settings;settings.friction=0.6f;settings.restitution=sphere ? 0.6f : 0.15f;settings.continuousCollision=true;
    actor.addComponent<RigidBodyComponent>("Rigid body",settings);
    actor.addComponent<ColliderComponent>("Collider",sphere ? CollisionShape::sphere(0.5f) : CollisionShape::box({0.5f,0.5f,0.5f}));
    actors_.push_back(actor.id());++serial_;
}
bool PhysicsDemo::kick(Vec3 origin,Vec3 direction) {
    if(!active_) return false;
    const auto hit=scene_.physics().raycast(origin,direction,100);
    if(!hit) { selected_={};return false; }
    auto* body=dynamic_cast<RigidBodyComponent*>(scene_.findComponent(hit->body));
    if(!body||body->settings().motion!=BodyMotion::Dynamic) { selected_={};return false; }
    selected_=hit->actor;body->addImpulse(normalized(direction)*3+Vec3{0,5,0});return true;
}
} // namespace engine
