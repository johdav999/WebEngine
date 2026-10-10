#include "engine/character_motor_component.hpp"
#include "engine/physics.hpp"
#include "engine/collider_component.hpp"
#include "engine/scene.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene_archive.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
int failures=0;
void check(bool value,const char* message) { if(!value) { ++failures;std::cerr<<"FAIL: "<<message<<'\n'; } }
void near(float a,float b,float tolerance,const char* message) { if(std::abs(a-b)>tolerance) { ++failures;std::cerr<<"FAIL: "<<message<<" ("<<a<<" vs "<<b<<")\n"; } }
template<class F> bool rejected(F fn) { try { fn();return false; } catch(const std::exception&) { return true; } }
Actor& box(Scene& scene,Vec3 position,Vec3 half,std::uint8_t layer=0,BodyMotion motion=BodyMotion::Static) {
    auto& actor=scene.spawn("Geometry");actor.transform.position=position;
    RigidBodySettings settings;settings.motion=motion;settings.collisionLayer=layer;
    actor.addComponent<RigidBodyComponent>("Body",settings);
    actor.addComponent<ColliderComponent>("Collider",CollisionShape::box(half));return actor;
}
CharacterMotorComponent& motor(Scene& scene,Vec3 feet={0,0,0},CharacterMotorSettings settings={}) {
    auto& actor=scene.spawn("Character");actor.transform.position=feet;
    return actor.addComponent<CharacterMotorComponent>("Motor",settings);
}
void steps(Scene& scene,int count) { for(int i=0;i<count;++i) scene.fixedUpdate(1.0/60); }
void freeMovement() {
    Scene scene;CharacterMotorSettings settings;settings.collisionAware=false;
    auto& m=motor(scene,{0,2,0},settings);m.submitMovement({{2,1,-3}});scene.update(0);
    check(!scene.physicsIfCreated(),"Free motor does not allocate a physics world");steps(scene,60);
    const auto p=m.owner()->transform.position;near(p.x,2,0.001f,"Persistent world-space X command");near(p.y,3,0.001f,"Free XYZ motion");near(p.z,-3,0.001f,"Persistent Z command");
    near(m.movementResult().velocity.x,2,0.001f,"Movement result actual velocity");
    m.stop();steps(scene,1);near(m.movementResult().displacement.x,0,0.001f,"Stop clears command");
    check(!m.movementResult().grounded&&!m.movementResult().jumped,"Free movement has no simulated grounding/jump");
    m.setFixedTickEnabled(false);m.submitMovement({{10,0,0}});steps(scene,1);near(m.owner()->transform.position.x,2,0.001f,"Tick opt-out freezes free motor");
}
void groundingJumpAndResults() {
    Scene scene;auto& floor=box(scene,{0,-0.5f,0},{20,0.5f,20});auto& m=motor(scene,{0,3,0});
    scene.update(0);check(scene.physicsIfCreated()!=nullptr,"Collision-aware motor creates physics lazily");
    bool landed=false;for(int i=0;i<100;++i) { steps(scene,1);landed|=m.movementResult().landed; }
    check(landed&&m.movementResult().grounded,"Falling lands and becomes grounded");near(m.owner()->transform.position.y,0,0.03f,"Actor origin is capsule feet");
    check(m.movementResult().groundActor==floor.id()&&scene.findComponent(m.movementResult().groundBody),"Ground result has live scene handles");near(m.movementResult().groundNormal.y,1,0.01f,"Ground normal");
    m.submitMovement({{2,0,0},true});m.submitMovement({{2,0,0},false});steps(scene,1);
    check(m.movementResult().jumped&&!m.movementResult().grounded,"Jump edge survives command replacement and releases ground");
    check(m.movementResult().velocity.y>4,"Jump gives upward movement");
    m.submitMovement({{},true});steps(scene,1);check(!m.movementResult().jumped,"Airborne jump rejected");
    float apex=m.owner()->transform.position.y;bool relanded=false;
    for(int i=0;i<100;++i) { steps(scene,1);apex=std::max(apex,m.owner()->transform.position.y);relanded|=m.movementResult().landed; }
    check(apex>1&&relanded&&m.movementResult().grounded,"Jump arc returns to ground");
    steps(scene,1);check(!m.movementResult().jumped&&!m.movementResult().landed,"Jump and landing results last one step");
    m.submitMovement({{3,0,0}});steps(scene,1);near(m.movementResult().displacement.x,0.05f,0.001f,"Displacement is per fixed step");near(m.movementResult().velocity.x,3,0.01f,"Actual velocity from displacement");
}
void wallsCeilingsAndHighSpeed() {
    Scene scene;box(scene,{0,-0.5f,0},{20,0.5f,20});box(scene,{2,2,0},{0.1f,2,20});
    auto& m=motor(scene);steps(scene,2);m.submitMovement({{6,0,2}});bool wall=false;
    for(int i=0;i<60;++i) { steps(scene,1);wall|=m.movementResult().hitWall; }
    check(m.owner()->transform.position.x<1.65f&&wall,"Capsule blocks at thin wall and reports contact");check(m.owner()->transform.position.z>1.5f,"Movement slides along wall");
    m.teleport({0,0,0});m.submitMovement({{200,0,0}});scene.fixedUpdate(0.1);
    check(m.owner()->transform.position.x<1.65f,"Swept motion cannot tunnel through a thin wall");
    check(m.movementResult().displacement.x<m.movementResult().requestedDisplacement.x,"Blocked result distinguishes requested and achieved motion");
    Scene ceiling;box(ceiling,{0,-0.5f,0},{5,0.5f,5});box(ceiling,{0,2.3f,0},{5,0.1f,5});auto& jumper=motor(ceiling);steps(ceiling,2);jumper.submitMovement({{},true});
    bool hit=false;float apex=0;for(int i=0;i<60;++i) { steps(ceiling,1);hit|=jumper.movementResult().hitCeiling;apex=std::max(apex,jumper.owner()->transform.position.y); }
    check(hit&&apex<0.45f,"Jump hits ceiling and reports obstruction");check(jumper.movementResult().grounded,"Ceiling collision falls back to ground");
}
void stairsAndSnap() {
    Scene scene;box(scene,{0,-0.5f,0},{20,0.5f,20});box(scene,{2,0.1f,0},{1,0.1f,2});auto& m=motor(scene);steps(scene,2);m.submitMovement({{2,0,0}});steps(scene,60);
    check(m.owner()->transform.position.x>1.8f,"Steps climb a low ledge");near(m.owner()->transform.position.y,0.2f,0.04f,"Low step top supports feet");check(m.movementResult().grounded,"Grounded on step");
    bool unsupported=false;for(int i=0;i<55;++i) { steps(scene,1);unsupported|=m.movementResult().groundState==GroundState::InAir; }
    check(!unsupported,"Ground snap follows a low step down");near(m.owner()->transform.position.y,0,0.04f,"Step down reaches floor");
    Scene tall;box(tall,{0,-0.5f,0},{20,0.5f,20});box(tall,{2,0.5f,0},{1,0.5f,2});auto& blocked=motor(tall);steps(tall,2);blocked.submitMovement({{2,0,0}});steps(tall,90);
    check(blocked.owner()->transform.position.x<0.8f,"Steps reject ledges above configured height");
    Scene disabled;box(disabled,{0,-0.5f,0},{20,0.5f,20});box(disabled,{2,0.15f,0},{1,0.15f,2});CharacterMotorSettings s;s.stepHeight=0;auto& noStep=motor(disabled,{},s);steps(disabled,2);noStep.submitMovement({{2,0,0}});steps(disabled,60);
    check(noStep.owner()->transform.position.x<0.8f,"Zero step height disables stair traversal");
    Scene noSnap;box(noSnap,{0,-0.5f,0},{20,0.5f,20});box(noSnap,{2,0.1f,0},{1,0.1f,2});s.stepHeight=0.3f;s.groundSnapDistance=0;
    auto& fall=motor(noSnap,{2,0.2f,0},s);steps(noSnap,2);fall.submitMovement({{2,0,0}});bool air=false;
    for(int i=0;i<55;++i) { steps(noSnap,1);air|=fall.movementResult().groundState==GroundState::InAir; }
    check(air,"Zero snap distance permits falling from a step");
    Scene cliff;box(cliff,{0,-0.5f,0},{20,0.5f,20});box(cliff,{2,0.5f,0},{1,0.5f,2});auto& leave=motor(cliff,{2,1,0});steps(cliff,2);leave.submitMovement({{2,0,0}});air=false;
    for(int i=0;i<60;++i) { steps(cliff,1);air|=leave.movementResult().groundState==GroundState::InAir; }
    check(air,"Ground snap cannot bridge drops beyond its configured range");
}
void slopes() {
    auto ramp=[](Scene& scene,float angle) -> Actor& { auto& r=box(scene,{0,-0.2f,0},{6,0.2f,3});r.transform.rotation.z=angle;return r; };
    Scene walkable;ramp(walkable,pi/6);auto& m=motor(walkable,{-2,1,0});steps(walkable,100);const auto start=m.owner()->transform.position;
    check(m.movementResult().grounded,"Walkable slope is grounded");m.submitMovement({{2,0,0}});steps(walkable,60);
    check(m.owner()->transform.position.x>start.x+1&&m.owner()->transform.position.y>start.y+0.5f,"Movement climbs walkable slope");
    Scene steep;ramp(steep,pi/3);CharacterMotorSettings s;s.stepHeight=0;auto& blocked=motor(steep,{0,1.5f,0},s);steps(steep,20);
    const auto before=blocked.owner()->transform.position;blocked.submitMovement({{2,0,0}});steps(steep,25);
    check(!blocked.movementResult().grounded,"Steep slope does not grant grounding");check(blocked.owner()->transform.position.y<before.y+0.1f,"Movement cannot climb slope above limit");
}
void layersPlatformsAndRemoval() {
    Scene scene;auto& ground=box(scene,{0,-0.5f,0},{20,0.5f,20},1);CharacterMotorSettings settings;settings.collisionLayer=2;auto& m=motor(scene,{0,2,0},settings);
    scene.physics().setLayerCollision(1,2,false);steps(scene,60);check(m.owner()->transform.position.y<-1,"Motor respects collision layer pair exclusions");
    scene.physics().setLayerCollision(1,2,true);m.teleport({0,2,0});steps(scene,90);check(m.movementResult().grounded,"Layer pair restoration permits grounding");
    ground.enabled=false;m.submitMovement({{},true});steps(scene,1);check(!m.movementResult().jumped&&!m.movementResult().grounded,"Removed support cannot grant a stale jump");
    Scene moving;auto& platform=box(moving,{0,-0.2f,0},{5,0.2f,5},0,BodyMotion::Kinematic);auto& rider=motor(moving);steps(moving,3);auto* body=platform.getComponent<RigidBodyComponent>();
    for(int i=0;i<30;++i) { auto pose=platform.transform;pose.position.x+=0.02f;body->moveKinematic(pose);steps(moving,1); }
    check(rider.owner()->transform.position.x>0.5f,"Ground velocity carries character on moving platform");near(rider.movementResult().groundVelocity.x,1.2f,0.02f,"Platform velocity reported");
}
void lifecycleAndInterpolation() {
    Scene scene,other;box(scene,{0,-0.5f,0},{20,0.5f,20});auto& m=motor(scene);auto& visual=m.owner()->addComponent<MeshComponent>("Visual",Mesh::cube());(void)visual;
    steps(scene,2);m.submitMovement({{6,0,0}});steps(scene,1);scene.physics().setInterpolationAlpha(0.5f);
    const auto authoritative=m.owner()->transform.position.x;const auto frame=scene.collect();near(frame.meshes[0].model.m[12],authoritative-0.05f,0.001f,"Motor render pose interpolates without changing actor");
    m.enabled=false;scene.update(0);steps(scene,2);near(m.owner()->transform.position.x,authoritative,0.001f,"Disabled motor does not move");check(!m.movementResult().grounded,"Disable clears ground result");
    m.enabled=true;steps(scene,1);near(m.owner()->transform.position.x,authoritative,0.001f,"Re-enable clears stale commands");
    m.setFixedTickEnabled(false);m.submitMovement({{10,0,0}});steps(scene,1);near(m.owner()->transform.position.x,authoritative,0.001f,"Tick opt-out freezes collision-aware motor");
    near(scene.collect().meshes[0].model.m[12],authoritative,0.001f,"Frozen motor collapses interpolation history");
    m.owner()->transform.rotation.y=pi/2;scene.update(0);near(scene.collect().meshes[0].model.m[0],0,0.001f,"Facing edits remain visible while motor ticks are frozen");m.setFixedTickEnabled(true);
    const auto old=m.id();auto detached=scene.detachChild(*m.owner());other.adoptChild(std::move(detached));box(other,{0,-0.5f,0},{20,0.5f,20});steps(other,2);
    check(m.id()!=old&&!scene.findComponent(old)&&m.movementResult().grounded,"Scene transfer resets old motor and rebuilds support");
    const auto actor=m.owner()->id();m.owner()->destroy();check(!other.findActor(actor),"Pending destroy invalidates motor owner immediately");steps(other,1);
}
void validation() {
    CharacterMotorSettings settings;settings.radius=-1;check(rejected([&]{ settings.validate(); }),"Invalid capsule rejected");
    Scene scene;auto& m=motor(scene);settings=m.settings();settings.maxSlopeAngle=pi;check(rejected([&]{ m.configure(settings); }),"Invalid reconfiguration rejected atomically");check(m.settings().maxSlopeAngle==pi/4,"Failed configuration preserves settings");
    check(rejected([&]{ m.submitMovement({{0,1,0}}); }),"Collision-aware commands reject vertical input");check(rejected([&]{ m.submitMovement({{std::numeric_limits<float>::quiet_NaN(),0,0}}); }),"Nonfinite command rejected");
    check(rejected([&]{ m.teleport({0,std::numeric_limits<float>::infinity(),0}); }),"Nonfinite teleport rejected");
    m.owner()->transform.scale={2,1,1};check(rejected([&]{ scene.update(0); }),"Scaled motor actor rejected");m.owner()->transform.scale={1,1,1};
    m.owner()->addComponent<RigidBodyComponent>();check(rejected([&]{ scene.update(0); }),"Multiple transform authorities rejected");
    Scene duplicate;auto& first=motor(duplicate);first.owner()->addComponent<CharacterMotorComponent>();check(rejected([&]{ duplicate.update(0); }),"Duplicate active motors rejected");
    Scene nested;auto& parent=nested.spawn();auto& actor=parent.addChild<Actor>();actor.addComponent<CharacterMotorComponent>();check(rejected([&]{ nested.update(0); }),"Nested motor ownership rejected");
    Scene bound;motor(bound);check(rejected([&]{ bound.fixedUpdate(0.3); }),"Collision-aware oversized steps rejected");
}
void archivesAndReconfiguration() {
    Scene scene;box(scene,{0,-0.5f,0},{20,0.5f,20});auto& m=motor(scene);steps(scene,2);
    m.submitMovement({{2,0,0},true});auto loaded=SceneArchive::load(SceneArchive::save(scene));
    CharacterMotorComponent* copy=nullptr;
    for(const auto& child:loaded.scene->children()) if(auto* actor=dynamic_cast<Actor*>(child.get())) if(auto* candidate=actor->getComponent<CharacterMotorComponent>()) copy=candidate;
    check(copy&&copy->settings()==m.settings(),"Archives preserve motor settings");
    if(copy) { steps(*loaded.scene,1);near(copy->movementResult().velocity.x,2,0.01f,"Archives preserve persistent velocity command");check(!copy->movementResult().jumped,"Archives do not replay pending jump edges"); }
    auto s=m.settings();s.collisionAware=false;m.configure(s);m.submitMovement({{0,1,0}});steps(scene,1);check(m.movementResult().velocity.y==1&&!m.movementResult().grounded,"Runtime switch to free movement resets support");
    m.stop();s.collisionAware=true;m.configure(s);m.teleport({0,0,0});steps(scene,1);check(m.movementResult().grounded,"Runtime switch back recreates collision support");
}
}
int main() {
    freeMovement();groundingJumpAndResults();wallsCeilingsAndHighSpeed();stairsAndSnap();slopes();layersPlatformsAndRemoval();lifecycleAndInterpolation();validation();archivesAndReconfiguration();
    if(failures) { std::cerr<<failures<<" movement checks failed\n";return 1; }
    std::cout<<"Movement controller checks passed\n";return 0;
}
