#include "engine/application.hpp"
#include "engine/camera_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/collider_component.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool near(double a,double b) { return std::abs(a-b)<1e-8; }
template<class F> void rejects(F&& action,const char* message) {
    bool rejected=false;try { action(); } catch(const std::exception&) { rejected=true; }check(rejected,message);
}
struct Counts { int load=0,unload=0,activate=0,deactivate=0,pause=0,resume=0,destroy=0,tick=0,fixed=0,despawn=0;double delta=0; };
struct TickActor : Actor {
    Counts& counts;std::function<void()> action;
    explicit TickActor(Counts& c):counts(c) { setTickEnabled(true); }
    void tick(double delta) override { ++counts.tick;counts.delta=delta;if(action) action(); }
    void fixedTick(double) override { ++counts.fixed; }
    void onDespawn() noexcept override { ++counts.despawn; }
};
struct TestScene : Scene {
    Counts& counts;bool failLoad=false;TickActor* actor;
    explicit TestScene(Counts& c):counts(c),actor(&spawn<TickActor>(c)) {}
    ~TestScene() override { ++counts.destroy; }
    void onLoad() override { ++counts.load;if(failLoad) throw std::runtime_error("Load failed"); }
    void onUnload() noexcept override { ++counts.unload; }
    void onActivate() noexcept override { ++counts.activate; }
    void onDeactivate() noexcept override { ++counts.deactivate; }
    void onPause() noexcept override { ++counts.pause; }
    void onResume() noexcept override { ++counts.resume; }
};
InputPacket held() { InputPacket p;p.id=InputActions::Move;p.x=1;return p; }
void lifetimeAndFailure() {
    Counts a,b,c;Application app,foreign;
    auto first=std::make_unique<TestScene>(a);auto* firstPtr=first.get();
    const auto id=app.loadScene(std::move(first));const auto second=app.loadScene([&]{return std::make_unique<TestScene>(b);});
    check(a.load==1&&firstPtr->lifecycleState()==SceneLifecycleState::Loaded,"Loading runs hooks without activation");
    check(!foreign.findScene(id),"Scene handles are application scoped");
    rejects([&]{app.start({second.value,second.application+999});},"Invalid initial scene rejected");
    Scene* bound=nullptr;bool failBinding=false;
    app.setSceneBinding([&](Scene* next){if(failBinding&&next) throw std::runtime_error("Binding failed");bound=next;});
    app.start(id);check(bound==firstPtr&&a.activate==1&&app.state()==ApplicationState::Running,"Start binds and activates");
    app.advance(0);check(a.tick==1&&b.tick==0,"Only active scene is updated");
    rejects([&]{app.start(id);},"Double start rejected");
    auto broken=std::make_unique<TestScene>(c);broken->failLoad=true;
    rejects([&]{app.loadScene(std::move(broken));},"Load failure propagated");
    check(c.unload==1&&c.destroy==1&&app.activeSceneId()==id&&app.loadedSceneCount()==2,"Load failure cleans candidate and retains active scene");
    rejects([&]{app.loadScene([]()->std::unique_ptr<Scene>{throw std::runtime_error("Loader failed");});},"Loader failure propagated");
    rejects([&]{app.unloadScene(id);},"Active scene cannot be unloaded");
    failBinding=true;app.requestTransition(second,true);
    rejects([&]{app.commitTransition();},"Presentation validation failure propagated");
    check(bound==firstPtr&&app.activeSceneId()==id&&a.deactivate==0&&b.activate==0,"Failed binding leaves prior scene active");
    failBinding=false;app.pause();app.requestTransition(second,true);check(app.commitTransition(),"Explicit transition committed");
    check(a.deactivate==1&&a.unload==1&&a.destroy==1&&a.despawn==1&&!app.findScene(id),"Outgoing scene fully unloaded after rebind");
    check(b.activate==1&&b.pause==1&&app.state()==ApplicationState::Paused,"Transitions preserve pause state");
    app.shutdown();check(!bound&&b.deactivate==1&&b.unload==1&&b.destroy==1&&app.loadedSceneCount()==0,"Shutdown unbinds and destroys all scenes");
    app.shutdown();check(b.unload==1,"Shutdown is idempotent");
    rejects([&]{app.loadScene(std::make_unique<Scene>());},"Shutdown is terminal");
    app.advance(100);check(app.clocks().presentation.delta==0,"Stopped application has no frames");
}
void clocksAndInput() {
    Counts a;Application app({0.05,0.01,3});const auto id=app.loadScene(std::make_unique<TestScene>(a));app.start(id);
    int inputFrames=0,presentFrames=0,simFrames=0;
    ApplicationFrameCallbacks callbacks;
    callbacks.input=[&](Scene&,const InputSystem&,const ApplicationFrame&){++inputFrames;};
    callbacks.simulate=[&](Scene&,const ApplicationFrame&){++simFrames;};
    callbacks.present=[&](Scene&,const ApplicationFrame&){++presentFrames;};
    auto packet=held();check(app.submitInput({&packet,1}),"Input accepted while running");
    app.advance(0,callbacks);check(app.input().action(InputActions::Move).pressed,"Input snapshot begins once at timestamp zero");
    app.advance(0.025,callbacks);check(a.tick==2&&a.fixed==2&&near(a.delta,0.025),"Variable and fixed clocks advance independently");
    check(near(app.clocks().interpolationAlpha,0.5)&&near(app.clocks().fixedSeconds,0.02),"Fixed remainder and elapsed exposed");
    const auto sim=app.clocks().simulation.seconds;
    app.pause();app.pause();check(a.pause==1&&!app.input().action(InputActions::Move).held,"Pause clears held and queued input once");
    app.advance(20,callbacks);app.advance(20.02,callbacks);
    check(a.tick==2&&a.fixed==2&&simFrames==2&&near(app.clocks().simulation.seconds,sim),"Pause does not call zero-delta simulation ticks");
    check(inputFrames==4&&presentFrames==4&&app.clocks().presentation.delta>0,"Paused input and presentation remain live");
    app.submitInput({&packet,1});app.advance(20.04,callbacks);check(app.input().action(InputActions::Move).held,"Camera/menu input available while paused");
    app.setSuspended(true);const auto frameCount=app.clocks().presentation.frames;
    check(!app.submitInput({&packet,1}),"Suspended host rejects input");app.advance(200,callbacks);
    check(app.clocks().presentation.frames==frameCount&&!app.input().action(InputActions::Move).held,"Suspension freezes all clocks and cancels input");
    app.setSuspended(false);app.advance(300,callbacks);check(app.state()==ApplicationState::Paused&&app.clocks().interval==0,"Visibility restoration preserves explicit pause without catch-up");
    app.resume();app.resume();app.advance(400,callbacks);check(a.resume==1&&app.clocks().fixedSteps==0&&a.delta==0,"Resume rebases clocks and drops fixed remainder");
    app.advance(410,callbacks);check(app.clocks().fixedSteps==3&&near(a.delta,0.05)&&app.clocks().droppedSimulationSeconds>9.9,"Long frames clamp variable delta and bound fixed catch-up");
    rejects([&]{app.advance(409);},"Backwards timestamps rejected");
    rejects([&]{app.advance(std::numeric_limits<double>::quiet_NaN());},"NaN timestamp rejected");
    rejects([&]{Application invalid({0,0.01,3});},"Invalid settings rejected");
    app.shutdown();
}
void deferredOperations() {
    Counts a,b;Application app;auto first=std::make_unique<TestScene>(a);auto* actor=first->actor;
    const auto id=app.loadScene(std::move(first)),next=app.loadScene(std::make_unique<TestScene>(b));app.start(id);
    actor->action=[&]{
        app.requestTransition(next,true);
        rejects([&]{app.unloadScene(next);},"Unload during traversal rejected");
        rejects([&]{app.advance(0);},"Recursive frame rejected");
        check(a.destroy==0&&app.activeSceneId()==id,"Tick cannot destroy its scene");
    };
    app.advance(0);check(app.activeSceneId()==id&&a.destroy==0,"Transition deferred through old frame");
    app.advance(100);check(app.activeSceneId()==next&&a.destroy==1&&app.clocks().interval==0&&b.tick==1,"Next frame transitions before clocks/input");
    auto* second=static_cast<TestScene*>(app.activeScene());
    second->actor->action=[&]{app.shutdown();check(b.destroy==0,"Shutdown deferred through callback");};
    app.advance(100.02);check(app.state()==ApplicationState::Stopped&&b.destroy==1,"Deferred shutdown completed at frame boundary");

    Counts errors;Application failing;auto scene=std::make_unique<TestScene>(errors);scene->actor->action=[] {throw std::runtime_error("Tick failed");};
    const auto target=failing.loadScene(std::move(scene));failing.start(target);
    rejects([&]{failing.advance(0);},"Tick failure propagated");
    check(failing.state()==ApplicationState::Paused&&errors.destroy==0,"Frame failure pauses without destroying active scene");
    failing.advance(1);check(errors.tick==1,"Failed tick is not rerun while paused");failing.shutdown();
}
void physicsAndBoundaries() {
    Application app;auto candidate=std::make_unique<Scene>();auto& body=candidate->spawn("Falling body");
    body.transform.position={0,5,0};body.addComponent<ColliderComponent>("Shape",CollisionShape::sphere(0.5f));
    body.addComponent<RigidBodyComponent>("Body");auto* scene=candidate.get();
    const auto id=app.loadScene(std::move(candidate));app.start(id);app.advance(0);app.advance(0.04);
    const auto y=body.transform.position.y;
    const auto steps=scene->physics().stats().steps;
    check(y<5&&steps>0,"Application fixed scheduler advances actual physics");
    app.pause();app.advance(100);app.advance(100.2);
    check(body.transform.position.y==y&&scene->physics().stats().steps==steps,"Paused synchronization does not advance physics");
    app.resume();app.advance(200);check(scene->physics().stats().steps==steps,"Resume does not catch up physics");
    app.advance(200.02);check(body.transform.position.y<y,"Physics resumes on subsequent frame");
    app.setSuspended(true);const auto hiddenY=body.transform.position.y;app.advance(400);
    app.setSuspended(false);app.advance(600);check(body.transform.position.y==hiddenY,"Hidden time never reaches the physics solver");
    const auto inactive=app.loadScene(std::make_unique<Scene>());
    app.requestTransition(inactive);rejects([&]{app.unloadScene(inactive);},"Pending targets cannot be unloaded");
    app.requestTransition(id,true);check(!app.commitTransition()&&app.loadedSceneCount()==2,"Last request wins; same-scene transition never unloads itself");
    check(app.unloadScene(inactive)&&!app.unloadScene(inactive),"Inactive unload invalidates its handle");app.shutdown();

    Counts unstarted;{Application neverStarted;neverStarted.loadScene(std::make_unique<TestScene>(unstarted));}
    check(unstarted.load==1&&unstarted.activate==0&&unstarted.unload==1&&unstarted.destroy==1,"Unstarted applications clean up loaded scenes on destruction");
}
}
int main() {
    try { lifetimeAndFailure();clocksAndInput();deferredOperations();physicsAndBoundaries();std::cout<<"Application ownership, lifecycle, transitions, failures and clocks passed\n"; }
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
