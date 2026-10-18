#include "engine/manipulation.hpp"
#include "engine/mesh_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/character_motor_component.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool close(float a,float b) { return std::abs(a-b)<0.001f; }
Actor& box(Scene& scene) {
    auto& actor=scene.spawn("Piece");actor.addComponent<MeshComponent>("Mesh",Mesh::cube());
    actor.addComponent<InteractionComponent>();return actor;
}
SelectionHit hit(Actor& actor,Vec3 point={0,0,0.5f}) {
    return {actor.id(),actor.getComponent<InteractionComponent>()->id(),actor.getComponent<MeshComponent>()->id(),point};
}
void translation() {
    Scene scene;auto& actor=box(scene);ManipulationController controller(scene);
    const auto picked=hit(actor);const Ray start{{0,0,5},{0,0,-1}};
    check(controller.begin(picked,start),"Begin plane drag");
    check(!controller.begin(picked,start),"A new session cannot replace ownership");
    check(controller.update({{2,3,5},{0,0,-10}})&&close(actor.transform.position.x,2)&&close(actor.transform.position.y,3),"Plane drag preserves picked offset");
    check(!controller.update({{0,0,5},{1,0,0}})&&close(actor.transform.position.x,2),"Parallel plane ray preserves last pose");
    check(!controller.update({{0,0,5},{0,0,1}}),"Behind-ray plane ignored");
    check(controller.cancel()&&close(actor.transform.position.x,0)&&!controller.target(),"Cancel restores exact initial transform");
    ManipulationSettings axis;axis.mode=ManipulationMode::AxisTranslation;axis.direction={2,0,0};
    check(controller.begin(picked,start,axis),"Begin world axis drag");
    check(controller.update({{4,8,5},{0,0,-1}})&&close(actor.transform.position.x,4)&&close(actor.transform.position.y,0),"Axis drag constrains off-axis movement");
    check(!controller.update({{0,0,5},{1,0,0}}),"Parallel axis ray ignored");
    check(controller.commit()&&!controller.target()&&close(actor.transform.position.x,4),"Commit retains authoritative pose");
    check(!controller.cancel(),"Committed session cannot rollback");
    const auto initial=actor.transform.matrix();
    { ManipulationController scoped(scene);check(scoped.begin(hit(actor,{4,0,0.5f}),{{4,0,5},{0,0,-1}}),"Scoped begin");
      check(scoped.update({{7,0,5},{0,0,-1}}),"Scoped update"); }
    check(actor.transform.matrix().m==initial.m,"Destructor cancels uncommitted movement");
}
void rotationAndHierarchy() {
    Scene scene;auto& actor=box(scene);actor.transform.position={2,0,0};
    ManipulationController controller(scene);ManipulationSettings rotation;rotation.mode=ManipulationMode::AxisRotation;
    check(controller.begin(hit(actor,{2,0,0}),{{2,0,5},{0,0,-1}},rotation),"Begin pivot rotation");
    check(controller.update({{0,2,5},{0,0,-1}})&&close(actor.transform.position.x,0)&&close(actor.transform.position.y,2)
        &&close(actor.transform.rotation.z,pi/2),"World pivot rotates position and orientation");
    check(controller.update({{-2,0,5},{0,0,-1}})&&controller.update({{0,-2,5},{0,0,-1}})
        &&controller.update({{2,0,5},{0,0,-1}})&&close(actor.transform.position.x,2),"Incremental rotation crosses angle wrap");
    check(controller.cancel()&&close(actor.transform.position.x,2)&&close(actor.transform.rotation.z,0),"Rotation cancel");
    auto& parent=scene.spawn("Parent");parent.transform.position={3,1,0};parent.transform.rotation.z=pi/2;parent.transform.scale={-2,3,1};
    actor.reparent(parent);actor.transform={};
    const auto initial=actor.transform.matrix();
    check(controller.begin(hit(actor,{3,1,0.5f}),{{3,1,5},{0,0,-1}}),"Transformed parent drag");
    check(controller.update({{5,4,5},{0,0,-1}}),"World translation converts through scaled mirrored parent");
    const auto world=actor.worldMatrix();check(close(world.m[12],5)&&close(world.m[13],4),"World drag location");
    check(controller.cancel()&&actor.transform.matrix().m==initial.m,"Cancel preserves original local TRS");
    rotation.pivot={3,1,0};
    check(controller.begin(hit(actor,{4,1,0}),{{4,1,5},{0,0,-1}},rotation),"Begin under nonuniform parent");
    check(!controller.update({{4,2,5},{0,0,-1}})&&actor.transform.matrix().m==initial.m,"Shearing rotation rejected without partial state");
    controller.cancel();
}
void invalidation() {
    Scene scene;auto& actor=box(scene);ManipulationController controller(scene);const Ray start{{0,0,5},{0,0,-1}};
    check(controller.begin(hit(actor),start)&&controller.update({{2,0,5},{0,0,-1}}),"Begin invalidation drag");
    actor.transform.position={9,0,0};
    check(!controller.target()&&!controller.cancel()&&close(actor.transform.position.x,9),"External writes cannot be overwritten by cancellation");
    actor.transform={};check(controller.begin(hit(actor),start),"New session after cancellation");
    auto& parent=scene.spawn("Parent");actor.reparent(parent);
    check(!controller.update(start)&&!controller.target(),"Reparent invalidates ownership");
    check(controller.begin(hit(actor),start),"Restart after hierarchy change");parent.transform.position={1,0,0};
    check(!controller.cancel()&&close(actor.worldMatrix().m[12],1),"Parent movement invalidates rollback");
    actor.reparent(scene);check(controller.begin(hit(actor),start),"Restart after parent movement");
    actor.enabled=false;check(!controller.update(start),"Disable terminates session");actor.enabled=true;
    check(controller.begin(hit(actor),start),"Restart enabled actor");
    actor.getComponent<InteractionComponent>()->selectable=false;
    check(!controller.commit(),"Provider opt-out invalidates commit");actor.getComponent<InteractionComponent>()->selectable=true;
    check(controller.begin(hit(actor),start),"Restart after provider change");const auto stale=hit(actor);
    actor.destroy();scene.flushPendingDestroy();check(!controller.update(start)&&!controller.target(),"Stale target rejected without dereferencing deleted nodes");
    auto& replacement=box(scene);check(!controller.begin(stale,start),"Recycled actor cannot inherit old session");
    replacement.addComponent<RigidBodyComponent>();check(!controller.begin(hit(replacement),start),"Physics transform writer cannot be bypassed");
    Scene other;auto& foreign=box(other);check(!controller.begin(hit(foreign),start),"Foreign scene rejected");
    auto& motor=box(scene);motor.addComponent<CharacterMotorComponent>();
    check(!controller.begin(hit(motor),start),"Character motor ownership cannot be bypassed");
    auto& removed=box(scene);check(controller.begin(hit(removed),start),"Begin surface removal test");
    removed.getComponent<MeshComponent>()->destroy();check(!controller.update(start),"Pending surface removal cancels session");
    auto& valid=box(scene);valid.transform.position={3,0,0};
    check(controller.begin(hit(valid,{3,0,0.5f}),{{3,0,5},{0,0,-1}}),"Valid begin for malformed input test");
    bool rejected=false;try {controller.update({{0,0,5},{0,0,0}});} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&controller.target()==valid.id()&&close(valid.transform.position.x,3),"Malformed ray rejected without losing valid session");
    controller.cancel();
    ManipulationSettings settings;settings.direction={std::numeric_limits<float>::infinity(),0,0};rejected=false;
    try {controller.begin(hit(valid),start,settings);} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&!controller.target(),"Malformed settings cannot claim ownership");
}
}
int main() {
    try {translation();rotationAndHierarchy();invalidation();std::cout<<"Manipulation tests passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
