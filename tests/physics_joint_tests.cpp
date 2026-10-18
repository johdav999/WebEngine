#include "engine/physics.hpp"
#include "engine/collider_component.hpp"
#include "engine/scene.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void near(float a,float b,float tolerance,const char* message) {
    if(std::abs(a-b)>=tolerance||!std::isfinite(a)) throw std::runtime_error(std::string(message)+": got "+std::to_string(a)+", expected "+std::to_string(b));
}
template<class F> void rejects(F&& action,const char* message) {
    try { action(); } catch(const std::exception&) { return; }
    throw std::runtime_error(message);
}
struct Body { Actor& actor;RigidBodyComponent& rigid;ColliderComponent& collider; };
Body add(Scene& scene,Vec3 position={},BodyMotion motion=BodyMotion::Dynamic) {
    auto& actor=scene.spawn("Joint body");actor.transform.position=position;
    RigidBodySettings settings;settings.motion=motion;settings.linearDamping=0;settings.angularDamping=0;
    auto& rigid=actor.addComponent<RigidBodyComponent>("Body",settings);
    auto& collider=actor.addComponent<ColliderComponent>("Shape",CollisionShape::box({0.2f,0.2f,0.2f}));
    return {actor,rigid,collider};
}
void steps(Scene& scene,int count=120) { for(int i=0;i<count;++i) scene.fixedUpdate(1.0/60); }
void hinge() {
    Scene scene;scene.physics().setGravity({});auto body=add(scene,{1,0,0});scene.synchronize();
    HingeJointSettings settings;settings.bodies.first=body.rigid.id();settings.axis={0,0,5};
    settings.minAngle=-0.5f;settings.maxAngle=0.5f;
    const auto id=scene.physics().createHinge(settings);
    check(scene.physics().hasJoint(id)&&scene.physics().stats().joints==1,"Hinge publishes a live handle");
    auto drive=[&](float force) {
        for(int i=0;i<120;++i) {
            body.rigid.addForce(cross(Vec3{0,0,force},body.actor.transform.position));scene.fixedUpdate(1.0/60);
            check(std::abs(scene.physics().hingeAngle(id))<0.55f,"Hinge stays within angular limits under load");
        }
    };
    drive(2);
    near(scene.physics().hingeAngle(id),0.5f,0.03f,"Hinge upper angular limit");
    const auto& pose=body.actor.worldMatrix();
    near(std::sqrt(pose.m[12]*pose.m[12]+pose.m[13]*pose.m[13]),1,0.03f,"Offset hinge anchor stays fixed");
    near(pose.m[14],0,0.01f,"Hinge blocks off-axis movement");
    drive(-2);
    near(scene.physics().hingeAngle(id),-0.5f,0.03f,"Hinge lower angular limit");
    check(scene.physics().removeJoint(id)&&!scene.physics().hasJoint(id),"Explicit removal cancels hinge");
    check(!scene.physics().removeJoint(id),"Repeated removal returns false");
    body.rigid.setLinearVelocity({0,0,1});steps(scene,30);
    check(body.actor.transform.position.z>0.4f,"Released hinge body moves freely");
}
void slider() {
    Scene scene;scene.physics().setGravity({});auto body=add(scene);scene.synchronize();
    SliderJointSettings settings;settings.bodies.first=body.rigid.id();settings.axis={0,2,0};
    settings.minDistance=-0.5f;settings.maxDistance=0.75f;
    const auto id=scene.physics().createSlider(settings);
    body.rigid.setLinearVelocity({2,2,2});body.rigid.setAngularVelocity({1,2,3});steps(scene);
    near(scene.physics().sliderPosition(id),0.75f,0.03f,"Slider upper travel limit");
    near(body.actor.transform.position.x,0,0.01f,"Slider locks transverse x");
    near(body.actor.transform.position.z,0,0.01f,"Slider locks transverse z");
    near(body.actor.transform.rotation.x,0,0.01f,"Slider locks rotation");
    body.rigid.setLinearVelocity({0,-2,0});steps(scene);
    near(scene.physics().sliderPosition(id),-0.5f,0.03f,"Slider lower travel limit");
}
void spring() {
    Scene scene;scene.physics().setGravity({});auto body=add(scene,{3,0,0});scene.synchronize();
    SpringJointSettings settings;settings.bodies.first=body.rigid.id();settings.firstAnchor={3,0,0};
    settings.restLength=1;settings.frequency=2;settings.dampingRatio=0.8f;
    const auto id=scene.physics().createSpring(settings);scene.fixedUpdate(1.0/60);
    check(body.actor.transform.position.x<3&&body.actor.transform.position.x>1,"Soft spring pulls without instant projection");
    steps(scene,240);near(body.actor.transform.position.x,1,0.02f,"Damped spring converges to rest length");
    scene.physics().removeJoint(id);settings.firstAnchor=body.actor.transform.position;settings.frequency=0;
    scene.physics().createSpring(settings);body.rigid.setLinearVelocity({4,0,0});steps(scene);
    near(body.actor.transform.position.x,1,0.02f,"Zero-frequency spring is a hard tether");
}
void grab() {
    Scene scene;scene.physics().setGravity({});auto body=add(scene,{1,0,0});scene.synchronize();
    const auto id=scene.physics().createGrab({body.rigid.id(),{1,0,0},{1,0,0}});
    steps(scene,180);check(scene.physics().stats().activeBodies==0,"Held body can sleep");
    scene.physics().setGrabTarget(id,{2,1,-1});check(scene.physics().stats().activeBodies==1,"Moving target wakes sleeping body");
    steps(scene);near(body.actor.transform.position.x,2,0.02f,"Grab follows target x");
    near(body.actor.transform.position.y,1,0.02f,"Grab follows target y");
    near(body.actor.transform.position.z,-1,0.02f,"Grab follows target z");
    body.rigid.setAngularVelocity({0,1,0});steps(scene,30);
    check(std::abs(body.actor.transform.rotation.y)>0.1f,"Grab leaves rotation free");
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{ scene.physics().setGrabTarget(id,{nan,0,0}); },"Nonfinite target rejected");
    steps(scene,30);near(body.actor.transform.position.x,2,0.02f,"Invalid target preserves valid constraint");
    scene.physics().removeJoint(id);rejects([&]{ scene.physics().setGrabTarget(id,{}); },"Released grab cannot be retargeted");
    // Off-center picking follows the material point as the body rotates, not its COM.
    Transform pose;body.rigid.teleport(pose);const Vec3 anchor{0.2f,0,0};
    const auto offset=scene.physics().createGrab({body.rigid.id(),anchor,anchor});
    body.rigid.setAngularVelocity({0,0,2});steps(scene,30);
    const auto point=transformPoint(body.actor.worldMatrix(),anchor);
    near(point.x,anchor.x,0.02f,"Off-center grab fixes picked x");near(point.y,0,0.02f,"Off-center grab fixes picked y");
    check(scene.physics().hasJoint(offset),"Off-center grab remains live");
}
void bodyPairs() {
    Scene scene;scene.physics().setGravity({});auto reference=add(scene,{},BodyMotion::Kinematic);
    auto moving=add(scene,{2,0,0});scene.synchronize();
    HingeJointSettings settings;settings.bodies={moving.rigid.id(),reference.rigid.id()};settings.anchor={1,0,0};
    const auto id=scene.physics().createHinge(settings);
    Transform pose;pose.position={0,1,0};reference.rigid.moveKinematic(pose);steps(scene,120);
    near(moving.actor.transform.position.y,1,0.03f,"Joint follows moving kinematic reference");
    check(scene.physics().hasJoint(id),"Two-body hinge remains registered");
    Scene pair;pair.physics().setGravity({});auto a=add(pair,{-1,0,0});auto b=add(pair,{1,0,0});pair.synchronize();
    SpringJointSettings spring;spring.bodies={a.rigid.id(),b.rigid.id()};spring.firstAnchor={-1,0,0};spring.secondAnchor={1,0,0};spring.restLength=1;
    pair.physics().createSpring(spring);steps(pair,240);
    near(b.actor.transform.position.x-a.actor.transform.position.x,1,0.025f,"Spring couples two dynamic bodies");
    near(b.actor.transform.position.x+a.actor.transform.position.x,0,0.025f,"Spring conserves center of mass");
}
void frictionAndOffsetFrames() {
    Scene scene;scene.physics().setGravity({});auto body=add(scene);scene.synchronize();
    HingeJointSettings hinge;hinge.bodies.first=body.rigid.id();hinge.frictionTorque=1;
    auto id=scene.physics().createHinge(hinge);body.rigid.setAngularVelocity({0,2,0});steps(scene,60);
    near(body.rigid.angularVelocity().y,0,0.01f,"Hinge friction stops rotation");scene.physics().removeJoint(id);
    SliderJointSettings slider;slider.bodies.first=body.rigid.id();slider.frictionForce=2;
    id=scene.physics().createSlider(slider);body.rigid.setLinearVelocity({1,0,0});steps(scene,120);
    near(body.rigid.linearVelocity().x,0,0.01f,"Slider friction stops sliding");scene.physics().removeJoint(id);
    Transform pose;pose.position={3,2,1};pose.rotation={0.2f,0.5f,-0.3f};body.rigid.teleport(pose);
    body.collider.transform.position={0.6f,0,0};scene.synchronize();
    const Vec3 localAnchor{0.2f,0,0};const auto anchor=transformPoint(body.actor.worldMatrix(),localAnchor);
    const Vec3 target=anchor+Vec3{1,1,1};scene.physics().createGrab({body.rigid.id(),anchor,target});
    steps(scene,120);const auto point=transformPoint(body.actor.worldMatrix(),localAnchor);
    near(point.x,target.x,0.025f,"Rotated body with offset COM preserves grab point x");
    near(point.y,target.y,0.025f,"Rotated body with offset COM preserves grab point y");
    near(point.z,target.z,0.025f,"Rotated body with offset COM preserves grab point z");
}
void validationAndIdentity() {
    Scene scene,other;auto body=add(scene);auto foreign=add(other);scene.synchronize();other.synchronize();
    HingeJointSettings settings;settings.bodies.first=body.rigid.id();
    const auto id=scene.physics().createHinge(settings);
    auto bad=settings;bad.axis={};rejects([&]{ scene.physics().createHinge(bad); },"Zero axis rejected");
    bad=settings;bad.axis.x=std::numeric_limits<float>::infinity();rejects([&]{ scene.physics().createHinge(bad); },"Nonfinite axis rejected");
    bad=settings;bad.maxAngle=4;rejects([&]{ scene.physics().createHinge(bad); },"Hinge range rejected");
    bad=settings;bad.minAngle=0.1f;rejects([&]{ scene.physics().createHinge(bad); },"Limits excluding zero rejected");
    bad=settings;bad.bodies.second=body.rigid.id();rejects([&]{ scene.physics().createHinge(bad); },"Self-joint rejected");
    bad=settings;bad.bodies.second=foreign.rigid.id();rejects([&]{ scene.physics().createHinge(bad); },"Foreign endpoint rejected");
    bad=settings;bad.bodies.second.generation=1;rejects([&]{ scene.physics().createHinge(bad); },"Malformed world endpoint rejected");
    SliderJointSettings slider;slider.bodies=settings.bodies;slider.frictionForce=-1;
    rejects([&]{ scene.physics().createSlider(slider); },"Negative friction rejected");
    SpringJointSettings spring;spring.bodies=settings.bodies;spring.restLength=-1;
    rejects([&]{ scene.physics().createSpring(spring); },"Negative spring length rejected");
    spring.restLength=1;spring.frequency=-1;rejects([&]{ scene.physics().createSpring(spring); },"Negative spring frequency rejected");
    spring.frequency=1;spring.dampingRatio=2;rejects([&]{ scene.physics().createSpring(spring); },"Invalid damping rejected");
    check(scene.physics().stats().joints==1&&scene.physics().hasJoint(id),"Failed creation preserves existing joint");
    body.rigid.transform.position.x=1;
    rejects([&]{ scene.physics().createHinge(settings); },"Invalid body component transform rejects creation");
    body.rigid.transform.position={};body.actor.transform.scale={2,2,2};
    rejects([&]{ scene.physics().createHinge(settings); },"Invalid endpoint world pose rejects creation");
    body.actor.transform.scale={1,1,1};
    check(scene.physics().stats().joints==1,"Invalid endpoint validation does not publish partial joint");
    rejects([&]{ scene.physics().sliderPosition(id); },"Wrong-type coordinate query rejected");
    rejects([&]{ scene.physics().setGrabTarget(id,{}); },"Wrong-type retarget rejected");
    check(!other.physics().hasJoint(id)&&!other.physics().removeJoint(id),"Foreign world rejects identity");
    check(!scene.physics().hasJoint({})&&!scene.physics().removeJoint({}),"Empty identity is inert");
    scene.physics().removeJoint(id);const auto newId=scene.physics().createHinge(settings);
    check(newId!=id&&!scene.physics().hasJoint(id),"Removed identities never resolve again");
    auto fixed=add(scene,{3,0,0},BodyMotion::Static);scene.synchronize();settings.bodies.first=fixed.rigid.id();settings.bodies.second={};
    rejects([&]{ scene.physics().createHinge(settings); },"Static-to-world joint rejected");
    auto pending=add(scene,{4,0,0});settings.bodies.first=pending.rigid.id();
    rejects([&]{ scene.physics().createHinge(settings); },"Unregistered body rejected without partial creation");
    scene.synchronize();auto sensing=pending.rigid.settings();sensing.isSensor=true;pending.rigid.configure(sensing);scene.synchronize();
    rejects([&]{ scene.physics().createHinge(settings); },"Sensor endpoint rejected");
}
void lifecycle() {
    Scene scene,other;auto body=add(scene);auto peer=add(scene,{2,0,0});scene.synchronize();
    HingeJointSettings settings;settings.bodies={body.rigid.id(),peer.rigid.id()};
    auto id=scene.physics().createHinge(settings);peer.collider.setEnabled(false);
    check(!scene.physics().hasJoint(id)&&scene.physics().stats().joints==0,"Disabled endpoint immediately invalidates handle");
    scene.synchronize();peer.collider.setEnabled(true);scene.synchronize();
    check(!scene.physics().hasJoint(id),"Re-enabled body does not resurrect joint");
    id=scene.physics().createHinge(settings);auto config=body.rigid.settings();config.mass=2;body.rigid.configure(config);
    check(!scene.physics().hasJoint(id),"Changed body settings invalidate joint before reconcile");
    scene.synchronize();check(!scene.physics().hasJoint(id),"Body rebuild cancels attached joint");
    id=scene.physics().createHinge(settings);body.collider.setShape(CollisionShape::sphere(0.2f));scene.synchronize();
    check(!scene.physics().hasJoint(id),"Shape rebuild cancels attached joint");
    id=scene.physics().createHinge(settings);const auto oldBody=body.rigid.id();
    auto detached=scene.detachChild(body.actor);check(!scene.physics().hasJoint(id),"Detach releases joint before body");
    other.adoptChild(std::move(detached));other.synchronize();check(body.rigid.id()!=oldBody,"Transfer changes body identity");
    check(!other.physics().hasJoint(id),"Transfer does not move world-owned joints");
    settings.bodies={body.rigid.id(),{}};id=other.physics().createHinge(settings);
    body.actor.destroy();check(!other.physics().hasJoint(id),"Pending destruction hides joint immediately");
    other.flushPendingDestroy();check(other.physics().stats().joints==0,"Destruction releases all joints");
    rejects([&]{ scene.physics().hingeAngle(id); },"Stale coordinate query rejected");
}
void collisionsAndCallbacks() {
    auto run=[](bool collide) {
        Scene scene;scene.physics().setGravity({});auto a=add(scene);auto b=add(scene,{},BodyMotion::Static);scene.synchronize();
        int contacts=0;scene.physics().onCollision=[&](const CollisionEvent& e) { if(e.phase!=CollisionPhase::End) ++contacts; };
        scene.fixedUpdate(1.0/60);check(contacts>0,"Contact before joint creation");
        Transform pose;a.rigid.teleport(pose);contacts=0;
        HingeJointSettings settings;settings.bodies={a.rigid.id(),b.rigid.id(),collide};
        const auto id=scene.physics().createHinge(settings);steps(scene,3);
        check(collide ? contacts>0 : contacts==0,"Connected collision option refreshes cached contacts");
        scene.physics().removeJoint(id);a.rigid.teleport(pose);contacts=0;steps(scene,3);
        check(contacts>0,"Joint removal restores collision eligibility");
    };
    run(false);run(true);
    Scene scene;scene.physics().setGravity({});auto a=add(scene);add(scene,{},BodyMotion::Static);
    auto grabbed=add(scene,{5,0,0});scene.synchronize();
    const auto id=scene.physics().createGrab({grabbed.rigid.id(),{5,0,0},{5,0,0}});bool delivered=false;
    scene.physics().onCollision=[&](const CollisionEvent&) {
        if(delivered) return;delivered=true;
        check(scene.physics().removeJoint(id),"Joint can be released from contact callback");
        const auto next=scene.physics().createGrab({grabbed.rigid.id(),{5,0,0},{6,0,0}});
        scene.physics().setGrabTarget(next,{7,0,0});grabbed.actor.destroy();
        check(!scene.physics().hasJoint(next),"Callback destruction immediately hides new joint");
    };
    scene.fixedUpdate(1.0/60);check(delivered&&scene.physics().stats().joints==0,"Callback mutation and deferred cleanup complete");
    (void)a;
}
void capacityAndWorldLifetime() {
    PhysicsJointId former;
    {
        Scene scene;auto body=add(scene);scene.synchronize();
        const GrabJointSettings grab{body.rigid.id(),{}, {}};
        std::vector<PhysicsJointId> ids;ids.reserve(4096);
        for(int i=0;i<4096;++i) ids.push_back(scene.physics().createGrab(grab));
        check(scene.physics().stats().joints==4096,"Joint capacity is explicit");
        rejects([&]{ scene.physics().createGrab(grab); },"Joint overflow rejected");
        check(scene.physics().hasJoint(ids.front())&&scene.physics().hasJoint(ids.back())&&scene.physics().stats().joints==4096,"Overflow preserves all valid joints");
        former=ids.front();body.actor.destroy();scene.flushPendingDestroy();
        check(scene.physics().stats().joints==0,"Body destruction cancels many attached joints");
        // Destruction with live constraints also releases references before Jolt.
        auto replacement=add(scene);scene.synchronize();scene.physics().createGrab({replacement.rigid.id(),{}, {}});
    }
    Scene fresh;auto body=add(fresh);fresh.synchronize();const auto next=fresh.physics().createGrab({body.rigid.id(),{}, {}});
    check(next.world!=former.world&&!fresh.physics().hasJoint(former),"Destroyed world identity never resolves in a fresh world");
}
}
int main() {
    try {
        hinge();slider();spring();grab();bodyPairs();frictionAndOffsetFrames();validationAndIdentity();lifecycle();collisionsAndCallbacks();capacityAndWorldLifetime();
        std::cout<<"Physics joint tests passed\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
