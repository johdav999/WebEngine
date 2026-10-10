#include "engine/animation_component.hpp"
#include "engine/actor.hpp"
#include "engine/scene.hpp"
#include "engine/scene_archive.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool near(double a,double b) { return std::abs(a-b)<0.0001; }
template<class F> void rejects(F action,const char* message) {
    bool rejected=false;try { action(); } catch(const std::exception&) { rejected=true; }check(rejected,message);
}
std::shared_ptr<Skeleton> rig() {
    auto result=std::make_shared<Skeleton>();result->joints.push_back({"Root",-1,{}});result->bindings={{},{0,Mat4::identity()}};
    AnimationClip a{"Walk",1,2,{{},{{2,0,0},{1,1,1},{}}}};
    a.markers={{0,"start",""},{0.25,"step","left"},{0.25,"cloth",""},{0.75,"step","right"},{1,"end",""}};
    auto b=a;b.name="Run";b.duration=2;b.frames[1].translation.x=8;b.markers={{0.5,"run",""}};
    auto zero=a;zero.name="Still";zero.duration=0;zero.frameCount=1;zero.frames.resize(1);zero.markers={{0,"zero",""}};
    result->clips={a,b,zero};result->validate();return result;
}
std::shared_ptr<AnimationControllerDefinition> graph() {
    auto result=std::make_shared<AnimationControllerDefinition>();
    result->parameters={{"speed",AnimationParameterType::Number,0.0},{"stop",AnimationParameterType::Boolean,false},{"act",AnimationParameterType::Trigger,false}};
    result->states={{"Idle",0,true,1,{}},{"Move",0,true,1,AnimationBlend{1,0,0,1}},{"Action",0,false,2,{}}};
    result->transitions={{0,2,0.2,{},{{2,AnimationComparison::Equal,true}}},
        {0,1,0.4,{},{{0,AnimationComparison::Greater,0.0}}},
        {1,2,0.2,{},{{2,AnimationComparison::Equal,true}}},
        {1,0,0.4,{},{{1,AnimationComparison::Equal,true}}},
        {2,0,0.1,1.0,{}}};
    return result;
}
void markers() {
    SkeletonComponent skeleton;skeleton.setSkeleton(rig());AnimationComponent player("Player",skeleton);
    player.enableRootMotion();player.play(0);player.update(2.25);
    check(player.events().size()==12,"Every loop and tied marker is emitted across a large update");
    const auto output=player.consumeOutput();check(near(output.rootMotion.translation.x,4.5)&&output.events[0].payload=="left","Output combines motion and owned marker payloads");
    for(std::size_t i=1;i<output.events.size();++i) check(output.events[i-1].time<=output.events[i].time,"Events are chronological");
    check(output.events[1].name=="cloth"&&output.events[3].name=="start"&&output.events[4].name=="end","Ties preserve authored order and endpoint/start markers remain distinct");
    check(player.consumeOutput().events.empty()&&near(player.consumeRootMotion().translation.x,0),"Output is consumed exactly once");
    player.update(0.1);check(player.events().empty(),"Previous exact marker is not repeated");
    player.seek(0.75);check(player.events().empty(),"Seek skips markers");player.update(0.25);
    check(player.events().size()==2,"Seek rebases loop boundary markers");
    player.play(0,false,2);player.update(4);check(player.events().size()==4&&player.state()==AnimationState::Paused,"Non-loop overshoot emits each marker once up to endpoint");
    player.consumeEvents();player.update(1);check(player.events().empty(),"Held endpoint does not repeat markers");
    player.play(2);player.update(100);check(player.events().empty(),"Zero duration does not emit markers");
    player.play(0);player.pause();player.update(1);check(player.events().empty(),"Explicit pause emits no events");
    player.resume();player.update(0.25);player.seek(0);check(player.events().empty(),"Seek clears queued events");
    player.update(0.25);player.stop();check(player.events().empty(),"Stop clears queued events");
    player.blend(0,1,1);player.update(0.5);check(player.events().size()==1&&player.events()[0].clip==1,"Zero-contribution source emits no markers");
    player.blend(0,1,0.5f);player.update(0.5);check(player.events().size()==3&&near(player.events()[0].weight,0.5),"Both blend sources emit weighted markers with independent durations");
    player.crossFade(0,1);player.update(0.25);check(player.events().size()==2&&near(player.events()[0].weight,0.25),"Frozen fade source emits no events; target weight is evaluated at crossing");
    player.play(0);player.update(0.25);const auto oldTime=player.time();const auto oldMotion=player.rootMotionDelta();const auto oldCount=player.events().size();
    rejects([&] { player.update(10000); },"Bounded queue rejects excessive marker crossings");
    check(player.time()==oldTime&&player.events().size()==oldCount&&near(player.rootMotionDelta().translation.x,oldMotion.translation.x),"Event overflow preserves clock, pending output and pose");
    auto invalid=rig();invalid->clips[0].markers[0].time=0.8;
    rejects([&] { skeleton.setSkeleton(invalid); },"Unordered markers rejected before replacing valid skeleton");
    check(skeleton.skeleton()!=invalid,"Failed resource replacement preserves previous skeleton");
    auto decimal=rig();decimal->clips[0].duration=0.3;decimal->clips[0].markers={{0.1,"decimal",""}};
    skeleton.setSkeleton(decimal);player.play(0);player.update(0.1);check(player.consumeEvents().size()==1,"Decimal marker fires at exact first boundary");
    player.update(0.3);check(player.consumeEvents().size()==1,"Decimal loop division does not repeat the previous boundary");
    player.update(0.3);check(player.consumeEvents().size()==1,"Decimal boundary crossing remains stable across loops");
}
void control() {
    SkeletonComponent skeleton;skeleton.setSkeleton(rig());AnimationComponent player("Player",skeleton);player.enableRootMotion();player.setController(graph());
    check(player.controllerState()==0&&player.time()==0,"Controller enters initial state");
    player.setParameter("speed",0.5);player.update(0);
    check(player.controllerState()==1&&player.blending()&&near(player.blendWeight(),0.5),"Parameter transition runs on variable zero tick and configures blend");
    const auto before=skeleton.skinPose()->bones[1].transform.m[12];player.update(0.2);const auto after=skeleton.skinPose()->bones[1].transform.m[12];
    check(near(before,after),"Root stripping persists under controller crossfade");
    player.clearController();player.blend(0,1,0.5);player.setController(graph());
    player.setParameter("speed",0.5);player.update(0);player.update(0.4);player.consumeOutput();player.update(0.1);
    player.setParameter("speed",1.0);player.update(0.1);
    check(near(player.blendWeight(),1)&&near(player.consumeRootMotion().translation.x,0.7),"Live parameter blend changes preserve accumulated movement and playback time");
    player.trigger("act");player.setParameter("stop",true);player.update(0);
    check(player.controllerState()==2&&!std::get<bool>(player.parameter("act")),"First eligible transition wins and only its triggers are consumed");
    player.trigger("act");player.update(0.5);check(player.state()==AnimationState::Paused,"Action completes at configured speed");
    player.update(0);check(player.controllerState()==0&&std::get<bool>(player.parameter("act")),"Exit time leaves completed state and unused trigger remains latched");
    player.pause();player.update(1);check(player.controllerState()==0,"Explicit pause freezes transitions");
    player.resume();player.update(0);check(player.controllerState()==2,"Latched trigger fires after resume");
    const auto definition=player.controller();const auto time=player.time();auto bad=graph();bad->states[0].clip=99;
    rejects([&] { player.setController(bad); },"Invalid controller rejected");
    check(player.controller()==definition&&player.time()==time,"Invalid replacement preserves controller and playback");
    player.setController(graph());player.trigger("act");
    rejects([&] { player.update(std::numeric_limits<double>::max()); },"Transition target clock overflow rejected before entry");
    check(player.controllerState()==0&&player.time()==0&&std::get<bool>(player.parameter("act"))&&player.events().empty(),"Rejected transition interval preserves source state and latched trigger");
    rejects([&] { player.setParameter("speed",true); },"Type mismatch rejected");
    rejects([&] { player.setParameter("speed",std::numeric_limits<double>::infinity()); },"Non-finite parameter rejected");
    rejects([&] { player.setParameter("act",true); },"Trigger requires explicit latch API");
    rejects([&] { player.trigger("speed"); },"Number cannot be triggered");
    rejects([&] { player.setParameter("missing",1.0); },"Unknown parameter rejected");
    player.resetTrigger("act");player.play(1);check(!player.controller(),"Manual playback detaches controller");
    player.setController(graph());skeleton.setSkeleton(rig());player.update(0);
    check(!player.controller()&&player.events().empty()&&!player.rootMotionEnabled(),"Rig replacement cancels controller/output/extraction");
    SkeletonComponent raw;raw.setSkeleton(rig());AnimationComponent unstripped("Raw",raw);auto shared=graph();unstripped.setController(shared);player.setController(shared);
    unstripped.update(0.3);const auto pose=raw.skinPose()->bones[1].transform.m[12];unstripped.setParameter("speed",0.5);unstripped.update(0);
    check(near(raw.skinPose()->bones[1].transform.m[12],pose)&&player.controllerState()==0&&std::get<double>(player.parameter("speed"))==0,"Crossfade entry preserves raw pose and shared definitions keep independent instance state");
    unstripped.update(0.1);const auto fading=raw.skinPose()->bones[1].transform.m[12];unstripped.trigger("act");unstripped.update(0);
    check(near(raw.skinPose()->bones[1].transform.m[12],fading),"Interrupted controller crossfade keeps the current raw pose");
}
void sceneAndArchive() {
    Scene scene;auto& actor=scene.spawn<Actor>("Character");auto& skeleton=actor.addComponent<SkeletonComponent>();skeleton.setSkeleton(rig());
    auto& player=skeleton.addChild<AnimationComponent>("Player",skeleton);player.enableRootMotion();player.setController(graph());player.trigger("act");
    actor.enabled=false;scene.update(0.5);check(player.time()==0&&player.controllerState()==0,"Disabled subtree freezes control");
    actor.enabled=true;scene.fixedUpdate(0.25);scene.synchronize();check(player.controllerState()==0,"Fixed/reconciliation passes do not transition or advance markers");
    scene.update(0.25);player.trigger("act");check(player.events().size()==2,"Scene variable tick advances controller/events once");
    const auto bytes=SceneArchive::save(scene);check(std::to_integer<int>(bytes[8])>=3,"New archives include animation control data");
    auto loaded=SceneArchive::load(bytes);AnimationComponent* restored=nullptr;
    loaded.scene->visit([&](const Node& node,const Mat4&) { if(const auto* value=dynamic_cast<const AnimationComponent*>(&node)) restored=const_cast<AnimationComponent*>(value); });
    check(restored!=nullptr,"Archived player exists");
    check(restored->controller()&&restored->controllerState()==player.controllerState()&&restored->events().size()==player.events().size()&&std::get<bool>(restored->parameter("act")),"Archives retain graph, state, latched parameters and pending events");
    const auto a=player.consumeOutput(),b=restored->consumeOutput();check(near(a.rootMotion.translation.x,b.rootMotion.translation.x)&&a.events[0].name==b.events[0].name,"Pending output survives archive restoration");
    scene.update(0.1);loaded.scene->update(0.1);check(near(player.time(),restored->time())&&near(player.rootMotionDelta().translation.x,restored->rootMotionDelta().translation.x),"Restored controller continues identical motion");
    auto library=SceneArchive::collectAssets(scene);auto restoredLibrary=SceneArchive::loadAssets(SceneArchive::saveAssets(library));
    check(restoredLibrary.skeletons[0]->clips[0].markers.size()==5,"Marker metadata survives asset library round trip");
    auto output=player.consumeOutput();actor.destroy();scene.flushPendingDestroy();check(!output.events.empty()||near(output.rootMotion.translation.x,0.4),"Consumed output remains owned after scene destruction");
}
}
int main() {
    try { markers();control();sceneAndArchive();std::cout<<"Animation control/event tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
