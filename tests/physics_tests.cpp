#include "engine/physics.hpp"
#include "engine/collider_component.hpp"
#include "engine/scene.hpp"
#include "engine/mesh_component.hpp"
#include "engine/physics_demo.hpp"
#include <iostream>

using namespace engine;
namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
void near(float a,float b,float tolerance,const char* message) { check(std::abs(a-b)<tolerance,message); }
template<class F> bool rejected(F&& action) { try { action(); } catch(const std::exception&) { return true; }return false; }
struct Body { Actor& actor;RigidBodyComponent& rigid;ColliderComponent& collider; };
Body add(Scene& scene,Vec3 position,std::shared_ptr<const CollisionShape> shape,RigidBodySettings settings={}) {
    auto& actor=scene.spawn("Physics actor");actor.transform.position=position;
    // Deliberately add the rigid body first to exercise order independence.
    auto& rigid=actor.addComponent<RigidBodyComponent>("Rigid",settings);
    auto& collider=actor.addComponent<ColliderComponent>("Collider",std::move(shape));return {actor,rigid,collider};
}
RigidBodySettings fixed() { RigidBodySettings s;s.motion=BodyMotion::Static;return s; }
void fallAndContacts() {
    Scene scene;scene.update(0);check(!scene.physicsIfCreated(),"Scenes without physics stay lazy");
    auto floor=add(scene,{0,-0.5f,0},CollisionShape::box({10,0.5f,10}),fixed());
    auto ball=add(scene,{0,4,0},CollisionShape::sphere(0.5f));scene.update(0);
    check(ball.rigid.hasBody()&&floor.rigid.hasBody()&&scene.physics().stats().bodies==2,"Bodies register on update");
    int begin=0,stay=0,end=0;
    scene.physics().onCollision=[&](const CollisionEvent& event) {
        check(scene.findActor(event.first)&&scene.findActor(event.second),"Contact actor handles resolve");
        check(scene.findComponent(event.firstBody)&&scene.findComponent(event.secondBody),"Contact component handles resolve");
        if(event.phase==CollisionPhase::Begin) { ++begin;check(std::abs(event.normal.y)>0.9f,"Contact normal is world-space"); }
        if(event.phase==CollisionPhase::Stay) ++stay;
        if(event.phase==CollisionPhase::End) ++end;
    };
    for(int i=0;i<180;++i) scene.fixedUpdate(1.0/60);
    near(ball.actor.transform.position.y,0.5f,0.035f,"Sphere rests on the floor");check(begin>0&&stay>0,"Begin and Stay contacts dispatch");
    Transform pose;pose.position={0,4,0};ball.rigid.teleport(pose);scene.fixedUpdate(1.0/60);check(end>0,"Separation emits End");
    near(ball.rigid.linearVelocity().y,-9.81f/60,0.01f,"Gravity applies after teleport clears velocity");
    check(scene.physics().stats().steps==181,"Physics step statistics");
    check(scene.physics().stats().lastStepMilliseconds>=0,"Physics timing recorded");
}
void commandsAndKinematic() {
    Scene scene;scene.physics().setGravity({0,0,0});RigidBodySettings settings;settings.mass=2;settings.linearDamping=0;
    auto body=add(scene,{0,0,0},CollisionShape::box({0.5f,0.5f,0.5f}),settings);scene.update(0);
    body.rigid.addImpulse({4,0,0});near(body.rigid.linearVelocity().x,2,0.001f,"Impulse respects configured mass");
    body.rigid.addForce({120,0,0});scene.fixedUpdate(1.0/60);near(body.rigid.linearVelocity().x,3,0.01f,"Force integrates once per fixed step");
    body.rigid.setAngularVelocity({0,1,0});scene.fixedUpdate(1.0/60);check(std::abs(body.actor.transform.rotation.y)>0.01f,"Angular velocity rotates actor");
    Transform destination;destination.position={3,2,0};destination.rotation={0.2f,0.4f,-0.3f};body.rigid.teleport(destination);
    near(body.actor.worldMatrix().m[12],3,0.001f,"Teleport writes world position");near(body.rigid.linearVelocity().x,0,0.001f,"Teleport clears velocity");
    check(rejected([&]{ body.rigid.moveKinematic(destination); }),"Dynamic bodies reject kinematic target commands");
    RigidBodySettings kinematic;kinematic.motion=BodyMotion::Kinematic;
    auto platform=add(scene,{0,5,0},CollisionShape::box({1,0.2f,1}),kinematic);scene.update(0);
    destination={};destination.position={2,5,0};platform.rigid.moveKinematic(destination);scene.fixedUpdate(1.0/60);
    near(platform.actor.transform.position.x,2,0.001f,"Kinematic target is reached in one fixed step");
    near(platform.rigid.linearVelocity().x,120,0.1f,"Kinematic target generates solver velocity");
    scene.fixedUpdate(1.0/60);near(platform.actor.transform.position.x,2,0.001f,"Kinematic body holds target without drifting");
    check(rejected([&]{ platform.rigid.addImpulse({1,0,0}); }),"Kinematic bodies reject impulses");
    auto floor=add(scene,{0,-1,0},CollisionShape::box({10,0.5f,10}),fixed());scene.update(0);
    check(rejected([&]{ floor.rigid.addForce({1,0,0}); }),"Static bodies reject force");
}
void layersAndRaycast() {
    Scene scene;scene.physics().setGravity({0,0,0});RigidBodySettings first;first.collisionLayer=1;
    auto front=add(scene,{0,0,0},CollisionShape::sphere(0.5f),first);
    auto behind=add(scene,{0,0,-3},CollisionShape::sphere(0.5f));scene.update(0);
    auto hit=scene.physics().raycast({0,0,5},{0,0,-2},10);
    check(hit&&hit->actor==front.actor.id()&&hit->body==front.rigid.id(),"Ray returns closest actor/component handles");
    near(hit->distance,4.5f,0.01f,"Ray direction is normalized");near(hit->normal.z,1,0.01f,"Ray hit surface normal");
    hit=scene.physics().raycast({0,0,5},{0,0,-1},10,1);
    check(hit&&hit->actor==behind.actor.id(),"Ray layer mask skips closer excluded body");
    check(!scene.physics().raycast({0,0,5},{0,1,0},10),"Ray miss is empty");
    front.actor.destroy();hit=scene.physics().raycast({0,0,5},{0,0,-1},10);
    check(hit&&hit->actor==behind.actor.id(),"Ray immediately ignores pending-destroy actor");scene.flushPendingDestroy();
    Scene falling;falling.physics().setLayerCollision(0,1,false);
    auto floor=add(falling,{0,-0.5f,0},CollisionShape::box({10,0.5f,10}),fixed());
    auto ball=add(falling,{0,2,0},CollisionShape::sphere(0.5f),first);
    for(int i=0;i<90;++i) falling.fixedUpdate(1.0/60);
    check(ball.actor.transform.position.y<-1,"Disabled layer pair passes through floor");
    falling.physics().setLayerCollision(1,0,true);Transform pose;pose.position={0,2,0};ball.rigid.teleport(pose);
    for(int i=0;i<120;++i) falling.fixedUpdate(1.0/60);
    near(ball.actor.transform.position.y,0.5f,0.04f,"Layer collision can be restored symmetrically");(void)floor;
}
void lifecycleAndCallbacks() {
    Scene scene,other;scene.physics().setGravity({0,0,0});
    auto a=add(scene,{0,0,0},CollisionShape::sphere(1));auto b=add(scene,{1,0,0},CollisionShape::sphere(1));scene.update(0);
    const auto id=a.actor.id();const auto component=a.rigid.id();int calls=0;
    scene.physics().onCollision=[&](const CollisionEvent&) { ++calls;a.actor.destroy(); };
    scene.fixedUpdate(1.0/60);check(calls==1&&!scene.findActor(id)&&!scene.findComponent(component),"Collision callback safely destroys actor");
    check(scene.physics().stats().bodies==1,"Destroyed body removed from solver");
    b.actor.enabled=false;scene.update(0);check(!b.rigid.hasBody()&&scene.physics().stats().bodies==0,"Disable removes body");
    b.actor.enabled=true;scene.update(0);check(b.rigid.hasBody(),"Re-enable recreates body");
    b.collider.enabled=false;scene.update(0);check(!b.rigid.hasBody(),"Collider disable removes its body");
    b.collider.enabled=true;scene.update(0);check(b.rigid.hasBody(),"Collider re-enable recreates body");
    const auto oldId=b.rigid.id();auto detached=scene.detachChild(b.actor);
    check(!scene.findComponent(oldId)&&scene.physics().stats().bodies==0,"Detaching actor cleans old world immediately");
    other.adoptChild(std::move(detached));other.update(0);check(b.rigid.hasBody()&&b.rigid.id()!=oldId,"Cross-scene transfer creates new body and handle");
    b.collider.destroy();other.flushPendingDestroy();other.update(0);check(!b.rigid.hasBody(),"Collider removal makes body inert");
    auto& replacement=b.actor.addComponent<ColliderComponent>("New collider",CollisionShape::sphere(1));other.update(0);
    check(b.rigid.hasBody(),"Replacement collider recreates body");
    auto& visual=b.actor.addComponent<MeshComponent>("Parent",Mesh::cube());replacement.reparent(visual);
    other.update(0);check(!b.rigid.hasBody(),"Nested/reparented collider cannot leave an orphan solver body");
    // Destruction with multiple live worlds exercises shared Jolt runtime lifetime.
}
void shapesAndValidation() {
    check(rejected([]{ CollisionShape::box({1,0,1}); }),"Invalid box dimensions rejected");
    check(rejected([]{ CollisionShape::sphere(-1); }),"Invalid sphere rejected");
    check(rejected([]{ CollisionShape::capsule(0,1); }),"Invalid capsule rejected");
    Scene scene;auto capsule=add(scene,{0,3,0},CollisionShape::capsule(0.5f,0.3f));
    const Vec3 hull[]={{-1,-1,-1},{1,-1,-1},{0,1,-1},{0,0,1}};
    auto convex=add(scene,{3,3,0},CollisionShape::convexHull(hull));
    const Vec3 vertices[]={{-10,0,-10},{10,0,10},{10,0,-10},{-10,0,10}};
    const std::uint32_t indices[]={0,1,2,0,3,1};
    auto mesh=add(scene,{0,0,0},CollisionShape::triangleMesh(vertices,indices),fixed());scene.update(0);
    check(capsule.rigid.hasBody()&&convex.rigid.hasBody()&&mesh.rigid.hasBody(),"Capsule, hull and static mesh create");
    for(int i=0;i<120;++i) scene.fixedUpdate(1.0/60);
    near(capsule.actor.transform.position.y,0.8f,0.05f,"Capsule collides with static mesh");
    RigidBodySettings bad;bad.mass=-1;check(rejected([&]{ capsule.rigid.configure(bad); }),"Configuration validates before committing");
    check(capsule.rigid.settings().mass==1,"Failed configuration preserves settings");
    Scene invalid;auto scaled=add(invalid,{0,0,0},CollisionShape::sphere(1));scaled.actor.transform.scale={2,2,2};
    check(rejected([&]{ invalid.update(0); }),"Scaled physics actors rejected");
    Scene dynamicMesh;add(dynamicMesh,{0,0,0},CollisionShape::triangleMesh(vertices,indices));
    check(rejected([&]{ dynamicMesh.update(0); }),"Moving triangle mesh rejected");
    check(rejected([&]{ scene.physics().raycast({},{},1); }),"Zero direction rejected");
    check(rejected([&]{ scene.physics().setLayerCollision(16,0,true); }),"Invalid layer rejected");
    check(rejected([&]{ scene.physics().setInterpolationAlpha(2); }),"Invalid interpolation rejected");
    auto changed=capsule.rigid.settings();changed.mass=2;capsule.rigid.configure(changed);scene.update(0);check(capsule.rigid.hasBody(),"Changed body settings reconcile");
    capsule.collider.setShape(CollisionShape::sphere(0.2f));scene.update(0);check(capsule.rigid.hasBody(),"Changed collider shape reconciles");
}
void fixedClockAndInterpolation() {
    Scene first,second;first.physics().setGravity({0,0,0});second.physics().setGravity({0,0,0});
    RigidBodySettings settings;settings.linearDamping=0;
    auto a=add(first,{},CollisionShape::box({0.5f,0.5f,0.5f}),settings);
    auto b=add(second,{},CollisionShape::box({0.5f,0.5f,0.5f}),settings);
    first.update(0);second.update(0);a.rigid.setLinearVelocity({1,0,0});b.rigid.setLinearVelocity({1,0,0});
    FixedStepClock c1,c2;
    for(int i=0;i<30;++i) c1.advance(first,1.0/30);
    for(int i=0;i<144;++i) c2.advance(second,1.0/144);
    near(a.actor.transform.position.x,b.actor.transform.position.x,0.0001f,"Physics independent of render frame rate");
    near(a.actor.transform.position.x,1,0.001f,"Fixed clock advances one second");
    check(c1.advance(first,5)==8&&c1.droppedSeconds()>4.8,"Long stall has bounded catch-up");c1.reset();near(static_cast<float>(c1.alpha()),0,0.001f,"Clock reset discards pending time");
    auto& mesh=a.actor.addComponent<MeshComponent>("Visual",Mesh::cube());mesh.transform.position={0,1,0};
    first.fixedUpdate(1.0/60);const auto current=a.actor.transform.position.x;
    first.physics().setInterpolationAlpha(0);const auto oldFrame=first.collect();
    first.physics().setInterpolationAlpha(1);const auto newFrame=first.collect();
    near(newFrame.meshes[0].model.m[12]-oldFrame.meshes[0].model.m[12],1.0f/60,0.001f,"Rendering interpolates previous/current physics pose");
    near(a.actor.transform.position.x,current,0.001f,"Interpolation never overwrites simulation transforms");
    near(oldFrame.meshes[0].model.m[13],1,0.001f,"Child local transform survives interpolation");
    first.physics().setDebugDrawEnabled(true);std::vector<PhysicsDebugLine> lines;first.physics().debugLines(lines);
    check(!lines.empty()&&lines.size()<=2048,"Collider geometry produces bounded debug lines");
    const auto debugFrame=first.collect();check(debugFrame.meshes.size()>newFrame.meshes.size(),"Debug colliders enter render frame");
    for(std::size_t i=newFrame.meshes.size();i<debugFrame.meshes.size();++i) {
        check(debugFrame.meshes[i].indexCount>0&&!debugFrame.meshes[i].castsShadow,"Collider debug draws have geometry and do not cast shadows");
    }
    check(first.collect(0).meshes.empty(),"Debug drawing respects a hidden camera mask");
}
void restitutionFrictionAndCcd() {
    Scene scene;auto floorSettings=fixed();floorSettings.restitution=0.8f;floorSettings.friction=1;
    add(scene,{0,-0.5f,0},CollisionShape::box({20,0.5f,20}),floorSettings);
    RigidBodySettings bouncing;bouncing.restitution=0.8f;
    auto ball=add(scene,{0,2,0},CollisionShape::sphere(0.5f),bouncing);scene.update(0);
    bool rebounded=false;for(int i=0;i<90;++i) { scene.fixedUpdate(1.0/60);rebounded|=ball.rigid.linearVelocity().y>2; }
    check(rebounded,"Restitution produces an upward rebound");
    Scene slide;slide.physics().setGravity({0,-9.81f,0});add(slide,{0,-0.5f,0},CollisionShape::box({20,0.5f,20}),floorSettings);
    RigidBodySettings rough;rough.friction=1;rough.linearDamping=0;
    auto box=add(slide,{0,0.52f,0},CollisionShape::box({0.5f,0.5f,0.5f}),rough);slide.update(0);box.rigid.setLinearVelocity({4,0,0});
    for(int i=0;i<120;++i) slide.fixedUpdate(1.0/60);
    check(std::abs(box.rigid.linearVelocity().x)<0.5f&&box.actor.transform.position.x<3,"Friction slows a sliding box");
    Scene fast;fast.physics().setGravity({0,0,0});add(fast,{},CollisionShape::box({0.03f,10,10}),fixed());
    RigidBodySettings continuous;continuous.continuousCollision=true;continuous.linearDamping=0;
    auto projectile=add(fast,{-1,0,0},CollisionShape::sphere(0.05f),continuous);fast.update(0);projectile.rigid.setLinearVelocity({300,0,0});fast.fixedUpdate(1.0/60);
    check(projectile.actor.transform.position.x<0,"Continuous collision prevents tunnelling through a thin wall");
}
void demoLifecycle() {
    Scene scene;PhysicsDemo demo(scene);demo.start();check(scene.physics().stats().bodies==7,"Demo starts six bodies and a floor");
    demo.spawn(false);scene.update(0);check(scene.physics().stats().bodies==8,"Demo can add bodies");
    demo.reset();check(scene.physics().stats().bodies==7,"Demo reset removes old solver bodies");
    demo.stop();check(scene.physics().stats().bodies==0,"Demo stop removes enabled solver bodies");
    demo.start();check(scene.physics().stats().bodies==7,"Demo restart does not leak bodies");
}
void spatialQueries() {
    Scene scene;scene.physics().setGravity({0,0,0});
    auto first=add(scene,{},CollisionShape::sphere(1),fixed());
    auto layered=fixed();layered.collisionLayer=2;
    auto second=add(scene,{4,0,0},CollisionShape::sphere(1),layered);
    RigidBodySettings moving;moving.motion=BodyMotion::Kinematic;
    auto third=add(scene,{8,0,0},CollisionShape::sphere(1),moving);
    auto sensing=fixed();sensing.isSensor=true;
    auto sensor=add(scene,{12,0,0},CollisionShape::sphere(1),sensing);
    scene.update(0);auto& world=scene.physics();
    const auto query=CollisionShape::sphere(0.5f);Transform pose;pose.position={-5,0,0};
    auto hit=world.sweep(*query,pose,{4,0,0},20);
    check(hit&&hit->body==first.rigid.id(),"Sweep returns closest body");
    near(hit->distance,3.5f,0.02f,"Sweep uses geometry and normalized direction");
    near(hit->fraction,3.5f/20,0.002f,"Sweep fraction matches distance");
    near(hit->normal.x,-1,0.01f,"Sweep normal points out of target");near(hit->point.x,-1,0.02f,"Sweep contact point on target");
    check(!hit->startedOverlapping,"Separated sweep has no initial penetration");
    check(!world.sweep(*query,pose,{0,1,0},20),"Sweep miss");
    check(!world.sweep(*query,pose,{1,0,0},3),"Sweep respects distance limit");
    PhysicsQueryFilter filter;filter.excludedActors={first.actor.id()};
    check(world.raycast(pose.position,{1,0,0},20,filter)->body==second.rigid.id(),"Ray actor exclusion skips closest body");
    check(world.sweep(*query,pose,{1,0,0},20,filter)->body==second.rigid.id(),"Sweep actor exclusion skips closest body");
    filter.excludedBodies={second.rigid.id()};
    check(world.raycast(pose.position,{1,0,0},20,filter)->body==third.rigid.id(),"Ray body exclusion");
    check(world.sweep(*query,pose,{1,0,0},20,filter)->body==third.rigid.id(),"Sweep body exclusion");
    filter={};filter.layers=1u<<2;
    check(world.sweep(*query,pose,{1,0,0},20,filter)->body==second.rigid.id(),"Sweep layer filter");
    const auto broad=CollisionShape::box({8,2,2});Transform broadPose;broadPose.position={6,0,0};
    auto overlaps=world.overlap(*broad,broadPose);
    check(overlaps.size()==4,"Overlap collects all bodies once");
    for(std::size_t i=1;i<overlaps.size();++i) check(overlaps[i-1].body.index<overlaps[i].body.index,"Overlap result order is stable");
    check(world.overlap(*broad,broadPose,filter).size()==1,"Overlap layer filter");
    world.setLayerCollision(0,2,false);
    check(world.overlap(*broad,broadPose,filter).size()==1,"Query layers are independent of simulation pair matrix");
    filter={};filter.includeStatic=false;
    check(world.overlap(*broad,broadPose,filter).size()==1,"Overlap motion filter");
    check(world.raycast(pose.position,{1,0,0},20,filter)->body==third.rigid.id(),"Ray motion filter");
    check(world.sweep(*query,pose,{1,0,0},20,filter)->body==third.rigid.id(),"Sweep motion filter");
    filter={};filter.includeSolids=false;
    check(world.overlap(*broad,broadPose,filter).size()==1,"Sensor-only overlap");
    check(world.raycast(pose.position,{1,0,0},20,filter)->body==sensor.rigid.id(),"Sensor-only ray");
    check(world.sweep(*query,pose,{1,0,0},20,filter)->body==sensor.rigid.id(),"Sensor-only sweep");
    filter.includeSensors=false;check(world.overlap(*broad,broadPose,filter).empty(),"Empty target category filter");
    filter={};filter.includeSensors=false;check(world.overlap(*broad,broadPose,filter).size()==3,"Sensor exclusion");
    filter.excludedActors={first.actor.id()};filter.excludedBodies={second.rigid.id()};
    check(world.overlap(*broad,broadPose,filter).size()==1,"Overlap actor and body exclusions");
    pose.position={0.25f,0,0};hit=world.sweep(*query,pose,{-1,0,0},10);
    check(hit&&hit->startedOverlapping&&hit->fraction==0&&hit->distance==0&&hit->penetrationDepth>1,"Sweep reports initial overlap while moving out");
    overlaps=world.overlap(*query,pose);check(overlaps.size()==1&&overlaps[0].penetrationDepth>1,"Overlap penetration data");
    near(overlaps[0].normal.x,1,0.03f,"Overlap outward target normal");
    pose.position={1.2f,1.2f,0};check(world.overlap(*query,pose).empty(),"Overlap rejects broadphase-only intersection");
    // Disabled and pending destruction are invisible before body reconciliation.
    second.actor.enabled=false;first.actor.destroy();
    filter={};check(world.overlap(*broad,broadPose,filter).size()==2,"Overlap immediately skips disabled and pending-destroy bodies");
    pose.position={-5,0,0};check(world.sweep(*query,pose,{1,0,0},20)->body==third.rigid.id(),"Sweep immediately skips dead bodies");
    const auto oldActor=first.actor.id();const auto oldBody=first.rigid.id();scene.flushPendingDestroy();
    auto replacement=add(scene,{},CollisionShape::sphere(1),fixed());scene.update(0);
    Scene foreign;auto foreignBody=add(foreign,{},CollisionShape::sphere(1),fixed());foreign.update(0);
    filter.excludedActors={oldActor,foreignBody.actor.id()};filter.excludedBodies={oldBody,foreignBody.rigid.id()};
    check(world.raycast({-5,0,0},{1,0,0},20,filter)->body==replacement.rigid.id(),"Stale and foreign exclusions cannot hide a replacement");
    check(rejected([&]{ world.sweep(*query,{}, {},1); }),"Sweep rejects zero direction");
    check(rejected([&]{ world.sweep(*query,{}, {1,0,0},0); }),"Sweep rejects zero distance");
    check(rejected([&]{ world.raycast({}, {std::numeric_limits<float>::max(),0,0},1); }),"Overflowing ray direction rejected");
    pose={};pose.scale={2,1,1};check(rejected([&]{ world.overlap(*query,pose); }),"Scaled overlap pose rejected");
    pose={};pose.rotation.x=std::numeric_limits<float>::quiet_NaN();
    check(rejected([&]{ world.sweep(*query,pose,{1,0,0},10); }),"Nonfinite query pose rejected");
}
void queryGeometry() {
    Scene scene;auto target=add(scene,{0,1.5f,0},CollisionShape::sphere(0.2f),fixed());scene.update(0);
    const auto box=CollisionShape::box({2,0.1f,0.1f});Transform pose;
    check(scene.physics().overlap(*box,pose).empty(),"Unrotated thin box misses");
    pose.rotation.z=pi/2;check(scene.physics().overlap(*box,pose).size()==1,"Overlap respects query orientation");
    target.collider.transform.position={0,0,2};scene.update(0);
    check(scene.physics().overlap(*box,pose).empty(),"Overlap respects collider local pose");
    pose.position.z=2;check(scene.physics().overlap(*box,pose).size()==1,"Overlap finds translated collider geometry");
    pose.position.x=-4;auto boxHit=scene.physics().sweep(*box,pose,{1,0,0},8);
    check(boxHit.has_value(),"Rotated box sweep supported");near(boxHit->distance,3.7f,0.04f,"Box sweep respects its rotated thin axis");
    const auto capsule=CollisionShape::capsule(2,0.3f);pose={};pose.position.z=2;
    check(scene.physics().overlap(*capsule,pose).size()==1,"Capsule query supported");
    pose.position={-4,0,2};check(scene.physics().sweep(*capsule,pose,{1,0,0},8).has_value(),"Capsule sweep supported");
    Scene offset;add(offset,{},CollisionShape::sphere(1),fixed());offset.update(0);
    const Vec3 hullPoints[]={{2,-1,-1},{4,-1,-1},{2,1,-1},{4,1,-1},{2,-1,1},{4,-1,1},{2,1,1},{4,1,1}};
    const auto hull=CollisionShape::convexHull(hullPoints);pose={};pose.position={-3,0,0};
    check(offset.physics().overlap(*hull,pose).size()==1,"Hull query preserves nonzero center of mass");
    pose.position.x=-10;auto hit=offset.physics().sweep(*hull,pose,{1,0,0},10);
    check(hit.has_value(),"Hull sweep supported");near(hit->distance,5,0.07f,"Hull sweep uses shape origin rather than center of mass");
    const Vec3 vertices[]={{-5,0,-5},{5,0,5},{5,0,-5},{-5,0,5}};const std::uint32_t indices[]={0,1,2,0,3,1};
    const auto mesh=CollisionShape::triangleMesh(vertices,indices);Scene floor;
    add(floor,{},mesh,fixed());floor.update(0);pose={};pose.position.y=0.2f;
    check(floor.physics().overlap(*CollisionShape::sphere(1),pose).size()==1,"Mesh triangles deduplicate to one overlap per body");
    pose.position.y=4;hit=floor.physics().sweep(*CollisionShape::sphere(1),pose,{0,-1,0},8);
    check(hit.has_value(),"Sweep hits static triangle mesh");near(hit->distance,3,0.02f,"Sweep against mesh uses narrowphase");
    check(rejected([&]{ floor.physics().overlap(*mesh,{}); }),"Mesh query geometry rejected");
    check(rejected([&]{ floor.physics().sweep(*mesh,{}, {1,0,0},1); }),"Mesh sweep geometry rejected");
    auto sensorSettings=fixed();sensorSettings.isSensor=true;Scene invalid;
    add(invalid,{},mesh,sensorSettings);check(rejected([&]{ invalid.update(0); }),"Mesh sensor geometry rejected");
}
void sensorTransitions() {
    Scene scene;auto sensorSettings=fixed();sensorSettings.isSensor=true;
    auto sensor=add(scene,{},CollisionShape::box({1,1,1}),sensorSettings);
    auto target=add(scene,{},CollisionShape::sphere(0.25f),fixed());scene.update(0);
    std::vector<TriggerEvent> events;int contacts=0;
    scene.physics().onTrigger=[&](const TriggerEvent& event) { events.push_back(event); };
    scene.physics().onCollision=[&](const CollisionEvent&) { ++contacts; };
    scene.fixedUpdate(1.0/60);
    check(events.size()==1&&events[0].phase==TriggerPhase::Enter&&events[0].sensorBody==sensor.rigid.id()&&events[0].otherBody==target.rigid.id(),"Static sensor and static target emit directed Enter");
    for(int i=0;i<60;++i) scene.fixedUpdate(1.0/60);
    check(events.size()==1&&contacts==0,"Persistent stationary overlap emits no duplicate Enter or collision");
    Transform pose;pose.position={3,0,0};target.rigid.teleport(pose);scene.fixedUpdate(1.0/60);
    check(events.size()==2&&events.back().phase==TriggerPhase::Exit,"Leaving sensor emits Exit");
    target.rigid.teleport({});scene.fixedUpdate(1.0/60);check(events.size()==3,"Reentry emits Enter");
    scene.physics().setLayerCollision(0,0,false);scene.fixedUpdate(1.0/60);
    check(events.size()==4&&events.back().phase==TriggerPhase::Exit,"Layer exclusion ends sensor pair");
    scene.physics().setLayerCollision(0,0,true);scene.fixedUpdate(1.0/60);check(events.size()==5,"Layer restoration emits Enter");
    target.collider.enabled=false;scene.update(0);check(!target.rigid.hasBody(),"Disabled sensor target collider removed");
    scene.fixedUpdate(1.0/60);check(events.size()==6&&events.back().phase==TriggerPhase::Exit,"Disabled target emits Exit on next fixed step");
    target.collider.enabled=true;scene.update(0);scene.fixedUpdate(1.0/60);check(events.size()==7,"Reenabled target emits Enter");
    const auto targetId=target.rigid.id();target.actor.destroy();scene.flushPendingDestroy();scene.fixedUpdate(1.0/60);
    check(events.size()==8&&events.back().phase==TriggerPhase::Exit&&events.back().otherBody==targetId&&!scene.findComponent(targetId),"Destroyed target emits Exit with safe original handle");
    auto replacement=add(scene,{},CollisionShape::sphere(0.25f),fixed());scene.update(0);scene.fixedUpdate(1.0/60);
    check(events.size()==9&&events.back().otherBody==replacement.rigid.id()&&events.back().otherBody!=targetId,"Recycled component cannot inherit a trigger pair");
    auto changed=sensorSettings;changed.isSensor=false;sensor.rigid.configure(changed);scene.update(0);scene.fixedUpdate(1.0/60);
    check(events.size()==10&&events.back().phase==TriggerPhase::Exit,"Sensor reconfiguration to solid closes pair");
    sensor.rigid.configure(sensorSettings);scene.update(0);scene.fixedUpdate(1.0/60);check(events.size()==11,"Sensor restored with fresh Enter");
    const auto sensorId=sensor.rigid.id();auto detached=scene.detachChild(sensor.actor);Scene other;other.adoptChild(std::move(detached));other.update(0);scene.fixedUpdate(1.0/60);
    check(events.size()==12&&events.back().phase==TriggerPhase::Exit&&events.back().sensorBody==sensorId,"Transferred sensor closes old-world pair");
}
void sensorMotionAndCallbacks() {
    Scene scene;scene.physics().setGravity({0,0,0});auto sensorSettings=fixed();sensorSettings.isSensor=true;
    add(scene,{},CollisionShape::box({1,1,1}),sensorSettings);
    RigidBodySettings motion;motion.linearDamping=0;
    auto target=add(scene,{-3,0,0},CollisionShape::sphere(0.25f),motion);scene.update(0);target.rigid.setLinearVelocity({3,0,0});
    int enters=0,exits=0,contacts=0;
    scene.physics().onTrigger=[&](const TriggerEvent& event) { if(event.phase==TriggerPhase::Enter) ++enters;else ++exits; };
    scene.physics().onCollision=[&](const CollisionEvent&) { ++contacts; };
    for(int i=0;i<120;++i) scene.fixedUpdate(1.0/60);
    near(target.actor.transform.position.x,3,0.005f,"Dynamic body passes through sensor without response");
    check(enters==1&&exits==1&&contacts==0,"Moving body produces exactly Enter/Exit");
    Scene callbacks;auto sensor=add(callbacks,{},CollisionShape::sphere(2),sensorSettings);
    auto solid=add(callbacks,{},CollisionShape::sphere(0.5f),fixed());callbacks.update(0);
    const auto id=solid.rigid.id();enters=0;exits=0;
    callbacks.physics().onTrigger=[&](const TriggerEvent& event) {
        if(event.phase==TriggerPhase::Enter) {
            ++enters;
            check(callbacks.physics().overlap(*CollisionShape::sphere(1),{}).size()==2,"Queries are safe in trigger callback");
            check(rejected([&]{ callbacks.fixedUpdate(1.0/60); }),"Nested fixed step rejected in trigger callback");
            solid.actor.destroy();
            check(callbacks.physics().overlap(*CollisionShape::sphere(1),{}).size()==1,"Callback destruction immediately updates queries");
        } else { ++exits;check(event.otherBody==id&&!callbacks.findComponent(id),"Exit callback safely handles dead component"); }
    };
    callbacks.fixedUpdate(1.0/60);check(enters==1&&!callbacks.findComponent(id),"Trigger callback may destroy its target");
    callbacks.fixedUpdate(1.0/60);check(exits==1,"Callback destruction gets one deferred Exit");
    auto second=add(callbacks,{},CollisionShape::sphere(0.5f),sensorSettings);callbacks.update(0);
    callbacks.physics().onTrigger=[&](const TriggerEvent&) { ++enters;callbacks.physics().onTrigger={}; };
    callbacks.fixedUpdate(1.0/60);check(enters==2,"Trigger listener can remove itself");
    int directed=0;Scene pair;add(pair,{},CollisionShape::sphere(1),sensorSettings);add(pair,{},CollisionShape::sphere(1),sensorSettings);pair.update(0);
    pair.physics().onTrigger=[&](const TriggerEvent& event) { check(event.sensorBody!=event.otherBody,"Sensors never report themselves");++directed; };
    pair.fixedUpdate(1.0/60);check(directed==2,"Two sensors get one directed Enter each");
    pair.fixedUpdate(1.0/60);check(directed==2,"Sensor pair stays deduplicated");
    (void)sensor;(void)second;
}
void sensorSleepingMeshesAndRecovery() {
    const auto sphere=CollisionShape::sphere(0.25f);
    Scene scene;scene.physics().setGravity({0,0,0});auto sensing=fixed();sensing.isSensor=true;
    auto sensor=add(scene,{},CollisionShape::sphere(2),sensing);
    auto sleeper=add(scene,{},sphere);scene.update(0);int enter=0,exit=0;
    scene.physics().onTrigger=[&](const TriggerEvent& event) { if(event.phase==TriggerPhase::Enter) ++enter;else ++exit; };
    for(int i=0;i<120;++i) scene.fixedUpdate(1.0/60);
    check(enter==1&&exit==0&&scene.physics().stats().activeBodies==0,"Sleeping dynamic overlap retains one trigger pair");
    sensor.rigid.enabled=false;scene.update(0);scene.fixedUpdate(1.0/60);
    check(exit==1,"Disabled sensor emits Exit");
    sensor.rigid.enabled=true;scene.update(0);scene.fixedUpdate(1.0/60);check(enter==2,"Reenabled sensor detects sleeping target");
    sensor.actor.destroy();scene.flushPendingDestroy();scene.fixedUpdate(1.0/60);check(exit==2,"Destroyed sensor emits Exit");
    Scene kinematic;RigidBodySettings settings;settings.motion=BodyMotion::Kinematic;settings.isSensor=true;
    auto mover=add(kinematic,{-3,0,0},CollisionShape::sphere(1),settings);
    auto target=add(kinematic,{},sphere,fixed());kinematic.update(0);enter=exit=0;
    kinematic.physics().onTrigger=[&](const TriggerEvent& event) { if(event.phase==TriggerPhase::Enter) ++enter;else ++exit; };
    mover.rigid.moveKinematic({});kinematic.fixedUpdate(1.0/60);check(enter==1,"Kinematic sensor detects static target after movement");
    Transform pose;pose.position={3,0,0};mover.rigid.moveKinematic(pose);kinematic.fixedUpdate(1.0/60);
    check(exit==1,"Kinematic sensor movement emits Exit");near(target.actor.transform.position.x,0,0.001f,"Moving sensor does not push static target");
    const Vec3 vertices[]={{-5,0,-5},{5,0,5},{5,0,-5},{-5,0,5}};const std::uint32_t indices[]={0,1,2,0,3,1};
    Scene mesh;add(mesh,{},CollisionShape::triangleMesh(vertices,indices),fixed());
    auto meshSensor=add(mesh,{0,0.2f,0},CollisionShape::sphere(1),sensing);mesh.update(0);enter=exit=0;
    mesh.physics().onTrigger=[&](const TriggerEvent& event) { if(event.phase==TriggerPhase::Enter) ++enter;else ++exit; };
    mesh.fixedUpdate(1.0/60);mesh.fixedUpdate(1.0/60);check(enter==1,"Mesh triangle contacts produce one sensor Enter");
    pose.position={0,3,0};meshSensor.rigid.teleport(pose);mesh.fixedUpdate(1.0/60);check(exit==1,"All mesh contacts leave with one Exit");
    Scene throwing;auto zone=add(throwing,{},CollisionShape::sphere(1),sensing);add(throwing,{},sphere,fixed());throwing.update(0);
    throwing.physics().onTrigger=[](const TriggerEvent&) { throw std::runtime_error("Gameplay callback error"); };
    check(rejected([&]{ throwing.fixedUpdate(1.0/60); }),"Trigger callback exception propagates");
    int recovered=0;throwing.physics().onTrigger=[&](const TriggerEvent& event) { if(event.phase==TriggerPhase::Exit) ++recovered; };
    zone.rigid.teleport(pose);throwing.fixedUpdate(1.0/60);check(recovered==1,"World and traversal recover after callback exception");
    (void)sleeper;
}
}
int main() {
    try {
        fallAndContacts();commandsAndKinematic();layersAndRaycast();lifecycleAndCallbacks();shapesAndValidation();fixedClockAndInterpolation();restitutionFrictionAndCcd();demoLifecycle();
        spatialQueries();queryGeometry();sensorTransitions();sensorMotionAndCallbacks();sensorSleepingMeshesAndRecovery();
        std::cout<<"Physics tests passed\n";return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
