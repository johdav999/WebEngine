#include "engine/navmesh.hpp"
#include "engine/collider_component.hpp"
#include "engine/scene.hpp"
#include <iostream>
#include <limits>
using namespace engine;
namespace {
int failures=0;
void check(bool value,const char* text) {if(!value){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
template<class F> bool rejects(F fn) {try{fn();return false;}catch(const std::exception&){return true;}}
Actor& box(Scene& scene,Vec3 p,Vec3 half,BodyMotion motion=BodyMotion::Static) {
    auto& a=scene.spawn();a.transform.position=p;RigidBodySettings s;s.motion=motion;
    a.addComponent<RigidBodyComponent>("Body",s);a.addComponent<ColliderComponent>("Shape",CollisionShape::box(half));return a;
}
WaypointSearchSettings query() {WaypointSearchSettings s;s.segmentBudget=1024;s.expansionBudget=4098;return s;}
void snapshot() {
    Scene scene;auto& floor=box(scene,{2,-.5f,3},{4,.5f,4});auto& dynamic=box(scene,{0,3,0},{1,1,1},BodyMotion::Dynamic);
    auto& sensor=box(scene,{1,2,1},{1,1,1});auto s=sensor.getComponent<RigidBodyComponent>()->settings();s.isSensor=true;sensor.getComponent<RigidBodyComponent>()->configure(s);
    scene.synchronize();PhysicsQueryFilter filter;filter.includeDynamic=filter.includeKinematic=filter.includeSensors=false;
    const auto geometry=scene.physics().triangleGeometry(filter);
    check(geometry.bodies.size()==1&&geometry.bodies[0]==floor.getComponent<RigidBodyComponent>()->id(),"Collision snapshot respects motion/sensor filters");
    check(geometry.indices.size()>=36,"Snapshot includes static collision triangles");
    check(rejects([&]{scene.physics().triangleGeometry(filter,1);}),"Snapshot rejects overflow rather than publishing partial geometry");
    filter.excludedActors.push_back(floor.id());check(scene.physics().triangleGeometry(filter).indices.empty(),"Collision snapshot respects handle exclusions");
    dynamic.enabled=false;sensor.enabled=false;floor.getComponent<ColliderComponent>()->transform.position={0,2,0};scene.synchronize();
    const auto nav=NavMesh::bake(scene);check(nav->project({2,2,3}).has_value(),"Collider-local offset is retained in baked world geometry");
    floor.destroy();check(scene.physics().triangleGeometry(filter).indices.empty(),"Pending removal disappears from geometry snapshots immediately");
}
void paths() {
    Scene scene;box(scene,{0,-.5f,0},{7,.5f,6});box(scene,{0,1.25f,0},{1,1.25f,1.6f});scene.synchronize();
    auto nav=NavMesh::bake(scene);check(nav->stats().polygons>0&&nav->debugMesh()->indices.size()>0,"Bake produces an immutable navmesh and debug geometry");
    const auto path=nav->findPath({-4,0,-2},{4,0,2},query());
    check(path.status==PathStatus::Complete&&path.points.size()>2,"Arbitrary endpoints route around baked collision without authored waypoints");
    for(auto p:path.points) check(!(std::abs(p.x)<1.2f&&std::abs(p.z)<1.8f&&p.y<1),"Path avoids static wall footprint and agent clearance");
    for(auto p:{Vec3{0,0,0},Vec3{50,0,0},Vec3{0,1.2f,0}}) {
        const auto projected=nav->project(p);
        check(!projected,"Projection rejects wall interiors, outside points and incorrect floors");
    }
    check(nav->raycast({-3,10,2},{0,-1,0}).has_value(),"CPU navmesh raycast supports click-to-move");
    auto limited=query();limited.expansionBudget=1;
    const auto noPartial=nav->findPath({-4,0,-2},{4,0,2},limited);
    check(noPartial.status==PathStatus::BudgetExceeded&&noPartial.points.empty(),"Navmesh search expansion budget publishes no partial path");
    limited=query();limited.segmentBudget=1;limited.canTraverse=[](Vec3,Vec3){return true;};
    check(nav->findPath({-4,0,-2},{4,0,2},limited).status==PathStatus::BudgetExceeded,"Navmesh traversal work is bounded");
    auto rejected=query();rejected.canTraverse=[](Vec3,Vec3){return false;};
    check(nav->findPath({-4,0,-2},{4,0,2},rejected).status==PathStatus::Unreachable,"Navmesh path queries compose caller clearance policy");
    const auto retained=nav;
    check(rejects([&]{NavMeshBuildSettings invalid;invalid.maxCells=1;nav=NavMesh::bake(scene,invalid);})&&nav==retained,"Failed bake preserves the prior published resource");
    check(rejects([&]{NavMeshBuildSettings invalid;invalid.maxRasterWork=1;NavMesh::bake(scene,invalid);}),"Raster work preflight bounds voxel allocation work");
    PhysicsGeometry invalid;invalid.vertices={{0,0,0},{1,0,0},{0,0,1}};invalid.indices={0,1,9};
    check(rejects([&]{NavMesh::bake(invalid);}),"Malformed geometry rejects before baking");
    CharacterMotorSettings motor;check(nav->compatible(motor),"Default motor fits default navmesh profile");motor.radius=1;
    check(!nav->compatible(motor),"Larger motors cannot use a smaller agent bake");
}
void profiles() {
    Scene narrow;box(narrow,{0,-.5f,0},{6,.5f,3});box(narrow,{0,1,2},{.3f,1,1.55f});box(narrow,{0,1,-2},{.3f,1,1.55f});narrow.synchronize();
    NavMeshBuildSettings small;small.cellSize=.1f;small.agentRadius=.2f;
    auto slim=NavMesh::bake(narrow,small);check(slim->findPath({-3,0,0},{3,0,0},query()).status==PathStatus::Complete,"Small radius can traverse a narrow passage");
    auto large=small;large.agentRadius=.6f;auto wide=NavMesh::bake(narrow,large);
    check(wide->findPath({-3,0,0},{3,0,0},query()).status==PathStatus::Unreachable,"Radius erosion closes an undersized passage");
    Scene ceiling;box(ceiling,{0,-.5f,0},{6,.5f,1});box(ceiling,{0,1.7f,0},{.4f,.1f,2});ceiling.synchronize();
    auto crouch=small;crouch.agentHeight=1.2f;auto low=NavMesh::bake(ceiling,crouch);
    check(low->findPath({-3,0,0},{3,0,0},query()).status==PathStatus::Complete,"Short agent retains low-ceiling clearance");
    auto tall=NavMesh::bake(ceiling,small);check(tall->findPath({-3,0,0},{3,0,0},query()).status==PathStatus::Unreachable,"Height filter blocks insufficient headroom");
    Scene stairs;box(stairs,{-2,-.5f,0},{2,.5f,2});box(stairs,{2,-.4f,0},{2,.6f,2});stairs.synchronize();
    auto step=small;step.cellHeight=.05f;auto climb=NavMesh::bake(stairs,step);
    check(climb->findPath({-2,0,0},{2,.2f,0},query()).status==PathStatus::Complete,"Bake connects steps below configured climb height");
    step.maxStepHeight=.05f;auto noClimb=NavMesh::bake(stairs,step);
    check(noClimb->findPath({-2,0,0},{2,.2f,0},query()).status==PathStatus::Unreachable,"Bake disconnects steps above configured climb height");
    PhysicsGeometry ramp;ramp.vertices={{-4,0,-2},{4,4,-2},{4,4,2},{-4,0,2}};ramp.indices={0,2,1,0,3,2};
    auto slope=small;slope.maxSlopeAngle=pi/4;auto walk=NavMesh::bake(ramp,slope);
    check(walk->findPath({-3,.5f,0},{3,3.5f,0},query()).status==PathStatus::Complete,"Walkable ramp respects slope profile");
    slope.maxSlopeAngle=pi/9;check(rejects([&]{NavMesh::bake(ramp,slope);}),"Steep-only source produces no usable navmesh");
}
class Walker:public Actor {
public:
    Navigator navigator;
    Walker():Actor("Navmesh agent"),navigator([]{NavigationSettings s;s.queryBudget=512;return s;}()) {setTickEnabled(true);addComponent<CharacterMotorComponent>();}
    CharacterMotorComponent& motor(){return *getComponent<CharacterMotorComponent>();}
protected:
    void fixedTick(double dt) override{navigator.step(motor(),dt);}
    void onDisable() noexcept override{if(auto* m=getComponent<CharacterMotorComponent>())navigator.cancel(*m);}
};
void movement() {
    Scene scene;box(scene,{0,-.5f,0},{7,.5f,6});box(scene,{0,1.25f,0},{1,1.25f,1.6f});scene.synchronize();const auto nav=NavMesh::bake(scene);
    auto& agent=scene.spawn<Walker>();agent.transform.position={-4,0,-2};agent.navigator.navigate(nav,{4,0,2});
    for(int i=0;i<900;++i)scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::Arrived&&std::hypot(agent.transform.position.x-4,agent.transform.position.z-2)<.25f,"Shared navigator reaches arbitrary navmesh goal through real physics");
    agent.motor().teleport({-4,0,-3});agent.navigator.navigate(nav,{4,0,-3});
    for(int i=0;i<12;++i)scene.fixedUpdate(1.0/60);
    const auto& follower=agent.navigator.follower();
    check(agent.navigator.status()==NavigationStatus::Moving,"Transient obstacle fixture starts with a moving route");
    if(agent.navigator.status()!=NavigationStatus::Moving) return;
    const auto pending=follower.points()[follower.nextPoint()];
    const auto obstruction=(agent.transform.position+pending)*.5f;
    auto& obstacle=box(scene,{obstruction.x,1,obstruction.z},{.3f,1,.3f});scene.synchronize();
    scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::Waiting,"Navmesh navigator stops when new collision blocks its pending segment");
    obstacle.enabled=false;scene.synchronize();
    for(int i=0;i<900;++i)scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::Arrived&&agent.navigator.stats().replans>0,"Navmesh recovery retains original goal when a transient obstacle clears");
    auto settings=agent.motor().settings();settings.radius=.8f;agent.motor().configure(settings);agent.navigator.navigate(nav,{-4,0,-2});scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::Unreachable,"Navigator rejects an incompatible motor before issuing movement");
}
void revisions() {
    Scene scene;box(scene,{-4,-.5f,0},{3,.5f,3});auto& bridge=box(scene,{0,-.5f,0},{1,.5f,3});box(scene,{4,-.5f,0},{3,.5f,3});
    scene.synchronize();const auto original=NavMesh::bake(scene);
    auto source=std::make_shared<NavigationSource>(original);
    auto& agent=scene.spawn<Walker>();agent.transform.position={-4,0,0};agent.navigator.navigateSource(source,{4,0,0});
    for(int i=0;i<10;++i)scene.fixedUpdate(1.0/60);
    const auto revision=source->invalidate();bridge.enabled=false;scene.synchronize();
    const auto before=agent.transform.position;
    for(int i=0;i<60;++i)scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::WaitingForSurface&&std::abs(agent.transform.position.x-before.x)<.001f,"Invalidation stops stale routes before removed support without spending searches");
    check(agent.navigator.stats().searches==1&&agent.navigator.stats().queriesLastStep==0,"Dirty surfaces consume no query/retry budget");
    check(rejects([&]{NavMeshBuildSettings bad;bad.maxCells=1;source->publish(revision,NavMesh::bake(scene,bad));})&&!source->ready()&&source->surface()==original,"Failed rebake retains prior resource but never makes stale topology usable");
    check(rejects([&]{source->publish(revision,{});})&&!source->ready()&&source->surface()==original,"Invalid publication retains prior diagnostic surface while remaining dirty");
    auto next=source->invalidate();check(!source->publish(revision,original)&&!source->ready(),"Stale completion cannot publish after a newer invalidation");
    auto split=NavMesh::bake(scene);check(source->publish(next,split)&&!source->publish(next,original),"Matching bake publishes exactly once");
    scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::Waiting&&agent.navigator.goal().x==4&&agent.transform.position.x< -2,"Replacement replans original goal and cannot cross a missing floor");
    const auto restore=source->invalidate();bridge.enabled=true;scene.synchronize();check(source->publish(restore,NavMesh::bake(scene)),"Restored floor publishes a replacement");
    for(int i=0;i<600;++i)scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::Arrived&&agent.navigator.stats().replans==2,"Rebuilt floor resumes original request without resetting retry statistics");
    agent.navigator.navigateSource(source,{-4,0,0});scene.fixedUpdate(1.0/60);source->invalidate();
    for(int i=0;i<3700;++i)scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::TimedOut,"Waiting for publication retains total request deadline");
    check(source->publish(source->revision(),original),"Complete candidate can publish after timeout");scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::TimedOut&&agent.navigator.retry(),"Publication does not resurrect terminal requests; explicit retry remains available");
    agent.navigator.cancel(agent.motor());const auto stopped=agent.transform.position;source->invalidate();source->publish(source->revision(),original);
    scene.fixedUpdate(1.0/60);check(agent.navigator.status()==NavigationStatus::Cancelled&&std::abs(agent.transform.position.x-stopped.x)<.001f,"Cancellation releases interest in future publications");
    check(rejects([&]{agent.navigator.navigateSource({},{});}),"Null publication source rejects before replacing a request");
    agent.navigator.navigateSource(source,{-4,0,0});source->invalidate();
    agent.navigator.navigate(original,{-4,0,0});scene.fixedUpdate(1.0/60);
    check(agent.navigator.status()==NavigationStatus::Moving,"Switching to an immutable surface releases stale source binding");
    source->publish(source->revision(),original);agent.navigator.navigateSource(source,{-4,0,0});scene.fixedUpdate(1.0/60);
    for(int i=0;i<8;++i) {const auto token=source->invalidate();source->publish(token,original);scene.fixedUpdate(1.0/60);}
    check(agent.navigator.status()==NavigationStatus::Unreachable&&agent.navigator.stats().searches==5&&agent.navigator.stats().replans==4,"Repeated mesh replacements cannot reset the finite request search budget");
}
}
int main(){snapshot();paths();profiles();movement();revisions();if(failures)return 1;std::cout<<"Navmesh contracts passed\n";}
