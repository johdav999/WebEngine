#include "engine/ai.hpp"
#include "engine/application.hpp"
#include "engine/collider_component.hpp"
#include "engine/scene_archive.hpp"
#include "ai_demo.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
int failures=0;
void check(bool value,const char* text) { if(!value) { ++failures;std::cerr<<"FAIL: "<<text<<'\n'; } }
template<class F> bool rejects(F fn) { try {fn();return false;} catch(const std::exception&) {return true;} }
void steps(Scene& scene,int n) { for(int i=0;i<n;++i) scene.fixedUpdate(1.0/60); }
Actor& box(Scene& scene,Vec3 p,Vec3 half) {
    auto& actor=scene.spawn();actor.transform.position=p;RigidBodySettings settings;settings.motion=BodyMotion::Static;
    actor.addComponent<RigidBodyComponent>("Body",settings);actor.addComponent<ColliderComponent>("Collider",CollisionShape::box(half));return actor;
}
void navigation() {
    WaypointGraph graph({{0,0,0},{1,0,0},{0,0,1},{2,0,0},{9,0,9}},{{0,2},{2,3},{0,1},{1,3},{1,3}});
    const auto path=graph.findPath(0,3);
    check(path.status==PathStatus::Complete&&path.nodes==std::vector<std::uint32_t>{0,1,3},"A* chooses shortest directed route");
    check(graph.findPath(3,0).status==PathStatus::Unreachable,"Directed edges do not imply reverse traversal");
    check(graph.findPath(0,4).status==PathStatus::Unreachable,"Disconnected goal is unreachable");
    const auto bounded=graph.findPath(0,3,1);check(bounded.status==PathStatus::BudgetExceeded&&bounded.points.empty()&&bounded.expanded==1,"Budget never publishes partial path");
    check(graph.findPath(0,0,1).points.size()==1,"Start equals goal");
    check(graph.nearest({0,0,0},0)==0&&!graph.nearest({20,0,0},1),"Nearest query has explicit snap range");
    check(rejects([&]{graph.findPath(8,0);})&&rejects([&]{graph.findPath(0,1,0);}),"Invalid path requests reject");
    check(rejects([]{WaypointGraph invalid({{0,0,0}},{{0,1}});}),"Graph validates edges");
    check(rejects([]{WaypointGraph invalid({{std::numeric_limits<float>::quiet_NaN(),0,0}},{});}),"Graph rejects nonfinite nodes");
    DecisionClock clock(.1);check(!clock.advance(0)&&clock.advance(.01)&&!clock.advance(.02),"Decision clock starts once and ignores zero time");
    check(clock.advance(10)&&!clock.advance(.01),"Decision clock drops catch-up bursts");
    check(rejects([&]{clock.advance(-1);}),"Decision clock rejects invalid time");
    WaypointSearchSettings query;query.snapDistance=.01f;
    query.canTraverse=[](Vec3 a,Vec3 b) { return !(a.x==0&&a.z==0&&b.x==1&&b.z==0); };
    const auto alternate=graph.findPath(Vec3{},Vec3{2,0,0},query);
    check(alternate.status==PathStatus::Complete&&alternate.nodes==std::vector<std::uint32_t>{0,2,3},"Traversal filter selects an alternate directed path");
    query.segmentBudget=1;const auto limited=graph.findPath(Vec3{},Vec3{2,0,0},query);
    check(limited.status==PathStatus::BudgetExceeded&&limited.points.empty()&&limited.testedSegments==1,"Segment budget rejects without publishing partial output");
    query.segmentBudget=64;query.expansionBudget=1;
    check(graph.findPath(Vec3{},Vec3{2,0,0},query).status==PathStatus::BudgetExceeded,"Augmented search expansion budget is bounded");
    WaypointGraph anchors({{.1f,0,0},{0,0,1},{0,0,3}},{{1,2}});
    query.expansionBudget=256;query.snapDistance=1.1f;query.canTraverse=[](Vec3,Vec3 b){return b.x!=.1f;};
    const auto connected=anchors.findPath(Vec3{},Vec3{0,0,3},query);
    check(connected.status==PathStatus::Complete&&connected.nodes.front()==1,"Blocked nearest connector does not hide a reachable alternate anchor");
}
class WalkingActor:public Actor {
public:
    PathFollower follower;
    explicit WalkingActor(CharacterMotorSettings settings={}):Actor("AI walker") {setTickEnabled(true);addComponent<CharacterMotorComponent>("Motor",settings);}
    CharacterMotorComponent& motor() {return *getComponent<CharacterMotorComponent>();}
protected:
    void fixedTick(double dt) override {follower.step(motor(),dt);}
    void onDisable() noexcept override {if(auto* m=getComponent<CharacterMotorComponent>()) follower.cancel(*m);}
};
void following() {
    Scene scene;box(scene,{0,-.5f,0},{20,.5f,20});auto& walker=scene.spawn<WalkingActor>();
    const std::array route={Vec3{1,0,0},Vec3{1,0,1}};walker.follower.follow(route);steps(scene,100);
    check(walker.follower.status()==FollowStatus::Arrived,"Follower arrives through real collision motor");
    check(std::hypot(walker.transform.position.x-1,walker.transform.position.z-1)<.22f,"Follower follows authored corners");
    check(dot(walker.motor().movementCommand().velocity,walker.motor().movementCommand().velocity)==0,"Arrival clears persistent velocity");
    walker.follower.follow(route);const auto count=walker.follower.points().size();
    const std::array bad={Vec3{std::numeric_limits<float>::infinity(),0,0}};
    check(rejects([&]{walker.follower.follow(bad);})&&walker.follower.points().size()==count,"Invalid route preserves valid route");
    walker.follower.cancel(walker.motor());steps(scene,1);check(walker.follower.status()==FollowStatus::Cancelled,"Explicit cancel remains cancelled");
    walker.follower.follow(route);steps(scene,1);walker.enabled=false;scene.synchronize();
    check(walker.follower.status()==FollowStatus::Cancelled,"Owner disable cancels action and command");
    walker.enabled=true;steps(scene,1);check(walker.follower.status()==FollowStatus::Cancelled,"Enable does not resurrect cancelled route");
    Scene blocked;box(blocked,{0,-.5f,0},{20,.5f,20});box(blocked,{1,1,0},{.15f,1,2});auto& stuck=blocked.spawn<WalkingActor>();
    const std::array throughWall={Vec3{3,0,0}};stuck.follower.follow(throughWall);steps(blocked,160);
    check(stuck.follower.status()==FollowStatus::Blocked&&stuck.transform.position.x<.7f,"Blocked route times out using achieved movement");
    check(stuck.motor().movementCommand().velocity.x==0,"Blocked route stops motor");
    CharacterMotorSettings free;free.collisionAware=false;Scene flying;auto& flyer=flying.spawn<WalkingActor>(free);
    const std::array up={Vec3{0,2,0}};flyer.follower.follow(up);steps(flying,100);
    check(flyer.follower.status()==FollowStatus::Arrived&&flyer.transform.position.y>1.4f&&!flying.physicsIfCreated(),"Free XYZ follower needs no physics and uses actor-before-component ticks");
    walker.destroy();scene.flushPendingDestroy();check(scene.findActorsWithTag("missing").empty(),"Deferred destruction safely cancels actor");
}
void perception() {
    Scene scene;auto& observer=scene.spawn();auto& target=scene.spawn();target.transform.position={0,0,4};
    check(observeActor(scene,observer.id(),target.id(),{0,0,1}).has_value()&&!scene.physicsIfCreated(),"Sight without physics uses range and FOV without lazy world");
    check(!observeActor(scene,observer.id(),target.id(),{0,0,-1}),"Sight rejects targets outside cone");
    SightSettings settings;settings.range=2;check(!observeActor(scene,observer.id(),target.id(),{0,0,1},settings),"Sight applies range");
    auto& wall=box(scene,{0,1,2},{1,1,.1f});scene.synchronize();
    check(!observeActor(scene,observer.id(),target.id(),{0,0,1}),"Static geometry occludes sight");
    wall.enabled=false;scene.synchronize();check(observeActor(scene,observer.id(),target.id(),{0,0,1}).has_value(),"Disabled occluder does not block sight");
    wall.enabled=true;auto* body=wall.getComponent<RigidBodyComponent>();auto sensor=body->settings();sensor.isSensor=true;body->configure(sensor);scene.synchronize();
    check(observeActor(scene,observer.id(),target.id(),{0,0,1}).has_value(),"Sensors do not occlude sight");
    target.enabled=false;check(!observeActor(scene,observer.id(),target.id(),{0,0,1}),"Disabled target cannot be perceived");target.enabled=true;
    const auto old=target.id();target.destroy();check(!observeActor(scene,observer.id(),old,{0,0,1}),"Pending target destruction invalidates sight immediately");scene.flushPendingDestroy();
    auto& replacement=scene.spawn();replacement.transform.position={0,0,4};check(!observeActor(scene,observer.id(),old,{0,0,1}),"Recycled handle does not retarget sight");
    Scene other;auto& foreign=other.spawn();check(!observeActor(scene,observer.id(),foreign.id(),{0,0,1}),"Foreign scene handles rejected");
    check(rejects([&]{observeActor(scene,observer.id(),replacement.id(),{});}),"Sight rejects invalid forward vector");
}
class NavigatingActor:public Actor {
public:
    WaypointNavigator navigator;
    std::size_t largestQueryCount=0,largestExpansionCount=0;
    explicit NavigatingActor(NavigationSettings settings={}):Actor("Recovering navigator"),navigator(std::move(settings)) {
        setTickEnabled(true);addComponent<CharacterMotorComponent>();
    }
    CharacterMotorComponent& motor() { return *getComponent<CharacterMotorComponent>(); }
protected:
    void fixedTick(double dt) override {
        navigator.step(motor(),dt);largestQueryCount=std::max(largestQueryCount,navigator.stats().queriesLastStep);
        largestExpansionCount=std::max(largestExpansionCount,navigator.stats().expandedLastStep);
    }
    void onDisable() noexcept override {if(auto* m=getComponent<CharacterMotorComponent>()) navigator.cancel(*m);}
};
std::shared_ptr<const WaypointGraph> recoveryGraph() {
    return std::make_shared<const WaypointGraph>(std::vector<Vec3>{{0,0,0},{6,0,0},{6,0,4},{0,0,4}},
        std::vector<WaypointEdge>{{0,1},{1,0},{1,2},{2,1},{2,3},{3,2},{3,0},{0,3}});
}
void recovery() {
    Scene scene;box(scene,{0,-.5f,0},{20,.5f,20});
    auto& obstacle=box(scene,{2,1,0},{.2f,1,1});obstacle.enabled=false;
    NavigationSettings settings;settings.snapDistance=1;settings.replanInterval=.2;settings.maxReplans=4;
    auto& agent=scene.spawn<NavigatingActor>(settings);const auto graph=recoveryGraph();agent.navigator.navigate(graph,{6,0,0});
    steps(scene,12);check(agent.navigator.status()==NavigationStatus::Moving&&agent.transform.position.x>.2f,"Initial direct route moves before obstacle appears");
    const auto goal=agent.navigator.goal();const auto prior=agent.transform.position;
    check(rejects([&]{agent.navigator.navigate(nullptr,{});})&&agent.navigator.goal().x==goal.x&&agent.navigator.status()==NavigationStatus::Moving,"Invalid navigation request preserves the active goal");
    obstacle.enabled=true;scene.synchronize();steps(scene,1);
    check(agent.navigator.status()==NavigationStatus::Waiting&&agent.motor().movementCommand().velocity.x==0,"Upcoming capsule check stops immediately before replanning");
    check(agent.transform.position.x==prior.x,"Blocked-segment detection does not move through the obstacle");
    steps(scene,20);check(agent.navigator.status()==NavigationStatus::Moving&&agent.navigator.stats().replans==1,"Delayed replan installs an alternate route");
    Transform moved;moved.position={3,1,4};obstacle.getComponent<RigidBodyComponent>()->teleport(moved);
    steps(scene,220);check(agent.navigator.stats().replans>=2,"Moving the obstacle onto the detour triggers another replan");
    steps(scene,900);check(agent.navigator.status()==NavigationStatus::Arrived&&std::hypot(agent.transform.position.x-6,agent.transform.position.z)<.2f,"Repeated recovery reaches the original goal through real motor movement");
    check(agent.largestQueryCount<=settings.queryBudget&&agent.largestExpansionCount<=settings.expansionBudget,"Per-step query and expansion budgets hold during recovery");
    obstacle.enabled=false;agent.motor().teleport({});
    auto& second=box(scene,{0,1,2},{1,1,.2f});obstacle.enabled=true;moved.position={2,1,0};obstacle.getComponent<RigidBodyComponent>()->teleport(moved);
    agent.navigator.navigate(graph,{6,0,0});steps(scene,150);
    check(agent.navigator.status()==NavigationStatus::Unreachable&&agent.navigator.stats().searches==settings.maxReplans+1,"Sealed routes stop after the configured retry budget");
    const auto exhausted=agent.navigator.stats().searches;steps(scene,120);
    check(agent.navigator.stats().searches==exhausted&&agent.motor().movementCommand().velocity.x==0,"Terminal failure never restarts an unbounded retry loop");
    second.enabled=false;obstacle.enabled=false;
    check(agent.navigator.retry(),"Explicit retry starts a fresh request budget");steps(scene,220);
    check(agent.navigator.status()==NavigationStatus::Arrived,"Cleared route plus explicit retry reaches the retained goal");
    check(!agent.navigator.retry(),"Successful requests cannot be retried accidentally");
    agent.motor().teleport({});obstacle.enabled=true;agent.navigator.navigate(graph,{6,0,0});steps(scene,1);agent.enabled=false;scene.synchronize();
    check(agent.navigator.status()==NavigationStatus::Cancelled,"Disable cancels a pending recovery");agent.enabled=true;steps(scene,60);
    check(agent.navigator.status()==NavigationStatus::Cancelled,"Re-enable does not resume a cancelled recovery");

    Scene transient;box(transient,{0,-.5f,0},{20,.5f,20});auto& wall=box(transient,{2,1,0},{.2f,1,1});
    auto lineGraph=std::make_shared<const WaypointGraph>(std::vector<Vec3>{{0,0,0},{4,0,0}},std::vector<WaypointEdge>{{0,1},{1,0}});
    auto& waiting=transient.spawn<NavigatingActor>(settings);waiting.navigator.navigate(lineGraph,{4,0,0});steps(transient,1);
    check(waiting.navigator.status()==NavigationStatus::Waiting,"Temporarily unreachable goal waits rather than publishing a partial path");
    wall.enabled=false;steps(transient,160);check(waiting.navigator.status()==NavigationStatus::Arrived,"Automatic retry recovers when an obstacle disappears within budget");

    Scene constrained;box(constrained,{0,-.5f,0},{20,.5f,20});auto& narrow=box(constrained,{2,1,.35f},{.1f,1,.1f});
    NavigationSettings oneTry=settings;oneTry.maxReplans=0;auto& wide=constrained.spawn<NavigatingActor>(oneTry);
    wide.navigator.navigate(lineGraph,{4,0,0});steps(constrained,1);
    check(!constrained.physics().raycast({0,1,0},{1,0,0},4),"Narrow-gap fixture has a clear center ray");
    check(wide.navigator.status()==NavigationStatus::Unreachable,"Capsule clearance rejects a route that a center ray incorrectly permits");
    auto sensor=narrow.getComponent<RigidBodyComponent>()->settings();sensor.isSensor=true;narrow.getComponent<RigidBodyComponent>()->configure(sensor);
    check(wide.navigator.retry(),"Unreachable request can be explicitly retried");steps(constrained,160);
    check(wide.navigator.status()==NavigationStatus::Arrived,"Sensors are ignored by navigation clearance");
    sensor.isSensor=false;sensor.collisionLayer=1;narrow.getComponent<RigidBodyComponent>()->configure(sensor);
    constrained.physics().setLayerCollision(0,1,false);wide.motor().teleport({});wide.navigator.navigate(lineGraph,{4,0,0});steps(constrained,160);
    check(wide.navigator.status()==NavigationStatus::Arrived,"Navigation clearance respects the motor's collision-layer matrix");

    Scene bounded;box(bounded,{0,-.5f,0},{20,.5f,20});NavigationSettings tiny=settings;tiny.queryBudget=1;
    auto& budgeted=bounded.spawn<NavigatingActor>(tiny);budgeted.navigator.navigate(lineGraph,{4,0,0});steps(bounded,1);
    check(budgeted.navigator.status()==NavigationStatus::BudgetExceeded&&budgeted.navigator.follower().points().empty()&&budgeted.navigator.stats().queriesLastStep<=1,"Search-query exhaustion stops safely with an explicit result");
    NavigationSettings deadline=settings;deadline.maxSeconds=.05;auto& timed=bounded.spawn<NavigatingActor>(deadline);timed.navigator.navigate(graph,{6,0,0});steps(bounded,4);
    check(timed.navigator.status()==NavigationStatus::TimedOut&&timed.motor().movementCommand().velocity.x==0,"Total navigation deadline includes planning and movement");
    check(rejects([]{NavigationSettings invalid;invalid.maxReplans=65;WaypointNavigator bad(invalid);}),"Navigator rejects unbounded retries");
    check(rejects([]{NavigationSettings invalid;invalid.replanInterval=0;WaypointNavigator bad(invalid);}),"Navigator rejects zero retry delay");
}
void demoAndPause() {
    Application app;auto sceneId=app.loadScene(std::make_unique<Scene>());auto& scene=*app.findScene(sceneId);
    auto& camera=scene.addChild<CameraComponent>();OrbitController orbit(camera);const auto originalCamera=camera.transform.position;
    AiDemo demo;demo.start(scene,orbit);check(camera.transform.position.y>10,"AI camera framing applies the orbit pose");app.start(sceneId);
    app.advance(0);app.advance(.1);const auto before=demo.snapshot(scene);app.pause();app.advance(.2);app.advance(2);
    const auto paused=demo.snapshot(scene);check(before.position.x==paused.position.x&&before.decisions==paused.decisions,"Application pause freezes AI decisions and motion");
    app.resume();steps(scene,300);check(demo.snapshot(scene).patrols>0,"Demo patrol reaches route nodes");
    demo.action(scene,0);steps(scene,8);check(demo.snapshot(scene).state==1&&demo.snapshot(scene).visible,"Demo sees nearby target and follows");
    const auto followedGoal=demo.snapshot(scene).lastSeen;demo.action(scene,3);steps(scene,8);
    check(demo.snapshot(scene).state==1&&demo.snapshot(scene).visible&&demo.snapshot(scene).lastSeen.z==followedGoal.z,"Moving a demo obstacle preserves the followed target and goal");
    demo.action(scene,8);
    demo.action(scene,1);steps(scene,8);check(demo.snapshot(scene).state==2&&!demo.snapshot(scene).visible,"Occluded target enters investigation");
    steps(scene,125);check(demo.snapshot(scene).state!=2,"Last-seen memory expires and returns");
    demo.action(scene,0);steps(scene,8);demo.action(scene,2);steps(scene,1);
    check(!demo.snapshot(scene).targetAlive&&demo.snapshot(scene).state==3,"Target removal cancels follow immediately");
    demo.action(scene,4);steps(scene,12);demo.action(scene,3);steps(scene,60);
    check(demo.snapshot(scene).state==0&&demo.snapshot(scene).replans>0,"Demo routes around a newly inserted obstacle");
    const auto completed=demo.snapshot(scene).patrols;steps(scene,950);
    check(demo.snapshot(scene).patrols>completed,"Demo detour completes the original patrol leg");
    demo.action(scene,7);steps(scene,180);check(demo.snapshot(scene).state==4&&demo.snapshot(scene).navigationStatus==int(NavigationStatus::Unreachable),"Demo sealed exits report unreachable after bounded retries");
    demo.action(scene,8);demo.action(scene,9);steps(scene,300);check(demo.snapshot(scene).state==0,"Clear obstacles and retry restores the retained goal");
    demo.action(scene,4);steps(scene,300);check(demo.snapshot(scene).state==0,"Restart restores patrol");
    demo.stop(scene,orbit);check(!demo.active(),"Demo stop releases custom actors");
    check(std::abs(camera.transform.position.x-originalCamera.x)<1e-5f&&std::abs(camera.transform.position.y-originalCamera.y)<1e-5f,"Demo return applies the original camera pose");
    check(!rejects([&]{SceneArchive::save(scene);}),"Returning from AI demo restores archiveable playground");
    demo.start(scene,orbit);steps(scene,8);
    const auto retained=demo.snapshot(scene);auto other=app.loadScene(std::make_unique<Scene>());
    app.requestTransition(other);app.commitTransition();app.advance(3);app.advance(3.1);
    check(retained.decisions==demo.snapshot(scene).decisions,"Inactive scene does not advance AI");
    app.requestTransition(sceneId);app.commitTransition();demo.stop(scene,orbit);
    demo.start(scene,orbit);app.requestTransition(other,true);app.commitTransition();
    check(!app.findScene(sceneId),"Scene replacement destroys active AI safely");app.shutdown();
}
}
int main() { navigation();following();perception();recovery();demoAndPause();if(failures) return 1;std::cout<<"AI contracts passed\n"; }
