#include "engine/state_machine.hpp"
#include "engine/editor_ai.hpp"
#include <iostream>
#include <limits>
#include <map>
using namespace engine;
namespace {
int failures=0;void check(bool ok,const char* why){if(!ok){++failures;std::cerr<<"FAIL: "<<why<<'\n';}}
template<class F>bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
StateMachineData data(){StateMachineData d;d.states={{1,"Idle","idle",{20,30}},{2,"Follow","follow",{300,30}}};d.initial=1;d.transitions={{1,1,2,"visible",.2},{2,2,1,"lost",0}};return d;}
struct Host:StateMachineHost {
    std::vector<std::string> events;std::map<std::string,bool> flags;bool fail=false,reenter=false;StateMachine* machine=nullptr;
    void enter(const AiState& s)override{events.push_back("enter "+s.name);}
    void exit(const AiState& s)noexcept override{events.push_back("exit "+s.name);}
    void update(const AiState& s,double)override{events.push_back("update "+s.name);if(fail)throw std::runtime_error("Game failure");if(reenter)machine->step(*this,.1);}
    bool condition(std::string_view key)override{return flags[std::string(key)];}
};
void runtime(){
    auto definition=StateMachineDefinition::create(data());StateMachine machine(definition);Host host;
    host.flags["visible"]=true;machine.step(host,.1);check(machine.current()==1&&machine.stateSeconds()==.1,"Minimum state time gates transitions");
    machine.step(host,.1);check(machine.current()==2&&machine.stateSeconds()==0,"Eligible condition enters destination and resets state timer");
    check(host.events==std::vector<std::string>{"enter Idle","update Idle","update Idle","exit Idle","enter Follow"},"Callbacks run enter/update/exit/enter in order");
    const auto count=host.events.size();check(rejects([&]{machine.step(host,0);})&&host.events.size()==count,"Invalid time is rejected before callbacks");
    machine.stop(host);machine.stop(host);machine.step(host,.1);check(host.events.size()==count+1&&machine.status()==StateMachineStatus::Stopped,"Stop exits exactly once and cannot resume implicitly");
    check(rejects([&]{machine.restart(host,definition,99);})&&machine.status()==StateMachineStatus::Stopped,"Invalid replacement initial state preserves runtime");
    machine.restart(host,definition,1);host.fail=true;check(rejects([&]{machine.step(host,.1);})&&machine.status()==StateMachineStatus::Failed&&host.events.back()=="exit Idle","Callback failure cleans up and stops execution");
    host.fail=false;host.reenter=true;host.machine=&machine;machine.restart(host,definition,1);check(rejects([&]{machine.step(host,.1);})&&machine.status()==StateMachineStatus::Failed,"Callback reentry is rejected without recursive execution");
    auto loop=data();loop.transitions={{1,1,2,"yes",0},{2,2,1,"yes",0}};StateMachine bounded(StateMachineDefinition::create(loop),{3,8});Host cycling;cycling.flags["yes"]=true;bounded.step(cycling,.1);
    check(bounded.transitionsLastStep()==3&&bounded.current()==2,"Cycles are capped and deferred at the per-step transition bound");
    bounded.step(cycling,.1);check(bounded.transitionsLastStep()==3,"Next step retains a finite transition allowance");
    StateMachine budget(StateMachineDefinition::create(loop),{3,1});check(rejects([&]{budget.step(cycling,.1);})&&budget.status()==StateMachineStatus::Failed,"Condition work exhaustion fails with cleanup");
    auto priority=data();priority.transitions={{1,1,2,"yes",0},{2,1,1,"yes",0}};Host ordered;ordered.flags["yes"]=true;StateMachine first(StateMachineDefinition::create(priority));first.step(ordered,.1);check(first.current()==2,"Transition order is deterministic priority");
}
StateMachineData hierarchy(){StateMachineData d;d.name="Hierarchy";d.states={{0,"Alive","idle",{}, {},1},{1,"Patrol","move",{},0,{}},{2,"Combat","idle",{},0,3},{3,"Chase","move",{},2,{}},{4,"Attack","idle",{},2,{}},{5,"Dead","idle",{}}};d.transitions={{0,3,4,"attack",0},{1,0,5,"dead",0},{2,1,2,"seen",0},{3,4,2,"reset",0}};return d;}
void hierarchical(){
    auto def=StateMachineDefinition::create(hierarchy());StateMachine m(def);Host h;m.step(h,.1);check(m.current()==1&&std::vector<std::uint32_t>(m.activePath().begin(),m.activePath().end())==std::vector<std::uint32_t>{0,1},"Root entry recursively enters initial child");check(h.events==std::vector<std::string>{"enter Alive","enter Patrol","update Alive","update Patrol"},"Hierarchical entry/update run outer to inner");
    h.events.clear();h.flags["seen"]=true;m.step(h,.1);check(m.current()==3&&h.events==std::vector<std::string>{"update Alive","update Patrol","exit Patrol","enter Combat","enter Chase"},"Sibling-to-group transition retains common parent and enters nested initial child");check(m.stateSeconds(0)==.2&&m.stateSeconds(2)==0&&m.stateSeconds()==0,"Parent time survives child transitions");
    h.events.clear();h.flags["attack"]=true;m.step(h,.1);check(m.current()==4&&m.stateSeconds(2)==.1&&h.events==std::vector<std::string>{"update Alive","update Combat","update Chase","exit Chase","enter Attack"},"Sibling transition preserves group lifetime and timer");
    h.events.clear();h.flags["dead"]=h.flags["reset"]=true;m.step(h,.1);check(m.current()==5&&h.events==std::vector<std::string>{"update Alive","update Combat","update Attack","exit Attack","exit Combat","exit Alive","enter Dead"},"Ancestor interrupt wins over earlier child edge and exits entire group inner to outer");
    m.restart(h,def,3);h.flags.clear();h.events.clear();m.step(h,.1);check(m.current()==3&&h.events[0]=="enter Alive"&&h.events[1]=="enter Combat"&&h.events[2]=="enter Chase","Explicit nested restart reconstructs ancestors");h.events.clear();m.stop(h);m.stop(h);check(m.activePath().empty()&&h.events==std::vector<std::string>{"exit Chase","exit Combat","exit Alive"},"Stop cancels every active ancestor exactly once");
    auto self=hierarchy();self.transitions={{0,2,2,"yes",0}};StateMachine loop(StateMachineDefinition::create(self),{2,8});Host cycling;loop.restart(cycling,StateMachineDefinition::create(self),3);cycling.flags["yes"]=true;loop.step(cycling,.1);check(loop.transitionsLastStep()==2&&loop.current()==3&&loop.stateSeconds(0)==.1&&loop.stateSeconds(2)==0,"Parent self-transitions reenter their subtree and remain bounded");
    auto timed=hierarchy();timed.transitions={{0,0,5,"yes",.25},{1,1,2,"yes",.1},{2,3,4,"yes",.1}};StateMachine timers(StateMachineDefinition::create(timed));Host yes;yes.flags["yes"]=true;timers.step(yes,.1);timers.step(yes,.1);timers.step(yes,.1);check(timers.current()==5,"Ancestor delay uses total active group time across sibling changes");
    StateMachine budget(def,{1,1});Host limited;check(rejects([&]{budget.step(limited,.1);})&&budget.status()==StateMachineStatus::Failed&&budget.activePath().empty(),"Condition exhaustion unwinds complete active hierarchy");
    struct Failing:StateMachineHost{std::vector<std::uint32_t> exits;void enter(const AiState& s)override{if(s.id==3)throw std::runtime_error("enter");}void exit(const AiState& s)noexcept override{exits.push_back(s.id);}bool condition(std::string_view)override{return false;}} failed;StateMachine fail(def);fail.restart(failed,def,3);check(rejects([&]{fail.step(failed,.1);})&&failed.exits==std::vector<std::uint32_t>{3,2,0},"Partially entered nested state cleans up child and parents");
    auto invalid=hierarchy();invalid.states[0].parent=2;check(rejects([&]{StateMachineDefinition::create(invalid);}),"Hierarchy cycles reject");invalid=hierarchy();invalid.states[1].parent=99;check(rejects([&]{StateMachineDefinition::create(invalid);}),"Missing parents reject");invalid=hierarchy();invalid.states[0].initialChild=3;check(rejects([&]{StateMachineDefinition::create(invalid);}),"Initial child must be direct");invalid=hierarchy();invalid.states[2].initialChild.reset();check(rejects([&]{StateMachineDefinition::create(invalid);}),"Groups require an initial child");invalid=hierarchy();invalid.initial=3;check(rejects([&]{StateMachineDefinition::create(invalid);}),"Graph entry must be a root");
    StateMachineData deep;for(unsigned i=0;i<17;++i){AiState state{i,"Nested","idle",{}};if(i)state.parent=i-1;if(i<16)state.initialChild=i+1;deep.states.push_back(state);}check(rejects([&]{StateMachineDefinition::create(deep);}),"Depth beyond sixteen rejects");
    auto bytes=StateMachineArchive::save(*def);auto restored=StateMachineArchive::load(bytes);check(restored->state(0).initialChild==1&&restored->state(3).parent==2&&StateMachineArchive::save(*restored)==bytes,"WEAI3 roundtrips hierarchy");check(StateMachineArchive::load("WEAI 2\n\"Old\" 0 1 0\n0 \"Idle\" \"idle\" 0 0\n0\n")->state(0).parent==std::nullopt,"Version two graph remains flat and readable");
    AiGraphEditor edit(def);const auto before=edit.definition();check(rejects([&]{edit.edit(invalid);})&&edit.definition()==before&&edit.undoCount()==0,"Invalid hierarchical edit preserves graph and history");
}

StateMachineData eventData(){
    StateMachineData d;d.states={{0,"Alive","idle",{},{},1},{1,"Idle","idle",{},0,{}},{2,"Investigate","idle",{},0,{}},{3,"Combat","idle",{},0,4},{4,"Attack","idle",{},3,{}},{5,"Recover","idle",{},3,{}}};
    d.events={{"HeardNoise",{{"position",AiParameterType::Vector,Vec3{},-100,100},{"target",AiParameterType::Actor,std::string{}},{"urgent",AiParameterType::Boolean,false}}},{"Damaged",{{"amount",AiParameterType::Number,1.0,0,100}}},{"AnimationFinished",{}}};
    d.transitions={{0,0,3,"yes",0,"Damaged"},{1,1,2,"yes",0,"HeardNoise"},{2,4,5,"yes",0,"AnimationFinished"}};return d;
}
AiEvent noise(){return {"HeardNoise",{{"position",Vec3{2,0,3}},{"target",std::string{}},{"urgent",true}}};}
void events(){
    auto definition=StateMachineDefinition::create(eventData());StateMachine machine(definition);Host host;host.flags["yes"]=true;
    check(machine.postEvent(noise())&&machine.postEvent({"Damaged",{{"amount",5.0}}}),"Typed events queue before initial entry");machine.step(host,.1);check(machine.current()==2&&machine.pendingEvents()==1,"FIFO consumes one triggering event under transition budget");machine.step(host,.1);check(machine.current()==4&&machine.pendingEvents()==0,"Ancestor event interrupts a nested state on the next fixed step");machine.step(host,.1);check(machine.current()==4,"Consumed event is never replayed");check(machine.currentEvent()==nullptr,"Event payload context never leaks outside callbacks");
    auto token=machine.eventScope(4);machine.postEvent({"AnimationFinished",{}},token);machine.step(host,.1);check(machine.current()==5,"Scoped completion triggers its active state");check(!machine.postEvent({"AnimationFinished",{}},token)&&machine.eventHistory().back().outcome==AiEventOutcome::StaleScope,"Late completion is rejected after its state exits");
    StateMachine other(definition);Host second;other.step(second,.1);check(!other.postEvent({"AnimationFinished",{}},machine.eventScope(5)),"Tokens cannot cross agent instances");auto old=other.eventScope(1);other.restart(second,definition,1);other.step(second,.1);check(!other.postEvent({"AnimationFinished",{}},old),"Restart creates new activation tokens");
    StateMachine queued(definition);Host accepting;accepting.flags["yes"]=true;queued.step(accepting,.1);queued.postEvent(noise());queued.postEvent({"AnimationFinished",{}},queued.eventScope(1));queued.step(accepting,.1);check(queued.current()==2&&queued.pendingEvents()==0&&queued.eventHistory().back().outcome==AiEventOutcome::Canceled,"Leaving a state cancels its queued completions before they can reach a sibling");
    StateMachine bounded(definition,{4,1024,2,1});Host none;bounded.postEvent({"AnimationFinished",{}});bounded.postEvent({"AnimationFinished",{}});check(!bounded.postEvent({"AnimationFinished",{}})&&bounded.pendingEvents()==2&&bounded.eventHistory().back().outcome==AiEventOutcome::Overflow,"Full queue rejects newest event without evicting pending inputs");bounded.step(none,.1);check(bounded.pendingEvents()==1&&bounded.eventHistory().back().outcome==AiEventOutcome::Unmatched,"Per-step event quota bounds unmatched delivery");bounded.step(none,.1);check(bounded.pendingEvents()==0,"Remaining event is deferred to the next step");
    const auto retained=machine.pendingEvents();check(rejects([&]{machine.postEvent({"Damaged",{{"amount",true}}});})&&rejects([&]{machine.postEvent({"Damaged",{{"amount",101.0}}});})&&rejects([&]{machine.postEvent({"unknown",{}});})&&machine.pendingEvents()==retained,"Invalid payloads and unknown event keys reject atomically");
    StateMachine rejected(definition);Host guarded;rejected.postEvent(noise());rejected.step(guarded,.1);guarded.flags["yes"]=true;rejected.step(guarded,.1);check(rejected.current()==1&&rejected.eventHistory().back().outcome==AiEventOutcome::Unmatched,"False guard discards the occurrence rather than storing a future trigger");
    struct Deferred:Host {StateMachine* machine=nullptr;bool posted=false;Vec3 received{};void update(const AiState& s,double)override{if(s.id==1&&!posted){posted=true;machine->postEvent(noise());}}bool condition(std::string_view)override{if(const auto* e=machine->currentEvent()){if(e->key=="HeardNoise"){received=std::get<Vec3>(e->parameters.at("position"));machine->postEvent({"Damaged",{{"amount",2.0}}});}}return true;}} producer;StateMachine deferred(definition,{4,1024});producer.machine=&deferred;deferred.step(producer,.1);check(deferred.current()==1&&deferred.pendingEvents()==1,"Update-posted event waits for the following fixed tick");deferred.step(producer,.1);check(deferred.current()==2&&deferred.pendingEvents()==1&&producer.received.x==2,"Guard payload is typed and callback-posted events cannot recurse in the same tick");deferred.step(producer,.1);check(deferred.current()==4,"Deferred callback occurrence is delivered next tick");
    auto priority=eventData();priority.transitions.push_back({9,1,1,"yes",0});StateMachine parent(StateMachineDefinition::create(priority));Host yes;yes.flags["yes"]=true;parent.postEvent({"Damaged",{{"amount",4.0}}});parent.step(yes,.1);check(parent.current()==4,"Parent event interrupt precedes a child polled transition");
    StateMachine stopped(definition);Host stopHost;stopped.postEvent(noise());stopped.stop(stopHost);check(stopped.pendingEvents()==0&&!stopped.postEvent(noise())&&stopped.eventHistory().back().outcome==AiEventOutcome::Stopped,"Stop cancels pending events and rejects new inputs");
    auto delayed=eventData();delayed.transitions[1].minimumSeconds=1;StateMachine early(StateMachineDefinition::create(delayed));Host ready;ready.flags["yes"]=true;early.postEvent(noise());early.step(ready,.1);check(early.pendingEvents()==0&&early.current()==1,"An ineligible timed event is discarded once examined");
    for(int i=0;i<80;++i){rejected.postEvent({"AnimationFinished",{}});rejected.step(guarded,.1);}check(rejected.eventHistory().size()==32,"Event trace memory stays bounded");
    auto bytes=StateMachineArchive::save(*definition);check(bytes.starts_with("WEAI 5")&&StateMachineArchive::save(*StateMachineArchive::load(bytes))==bytes,"Event schemas/triggers roundtrip in WEAI4");check(StateMachineArchive::load("WEAI 3\n\"Old\" 0 1 0\n0 \"Idle\" \"idle\" 0 0 0 0 0 0\n0\n")->data().events.empty(),"WEAI3 files load without event definitions");auto invalid=eventData();invalid.transitions[0].event="missing";check(rejects([&]{StateMachineDefinition::create(invalid);}),"Undeclared event triggers reject");invalid=eventData();invalid.events.push_back(invalid.events[0]);check(rejects([&]{StateMachineDefinition::create(invalid);}),"Duplicate event declarations reject");
}

void authoring(){
    auto original=StateMachineDefinition::create(data());AiGraphEditor editor(original,2);auto next=data();next.states[0].name="Waiting";
    check(editor.edit(next)&&editor.dirty()&&editor.undoCount()==1,"Graph edits publish immutable definitions with independent history");
    editor.markSaved();check(!editor.dirty(),"Save point follows graph identity");check(editor.undo()&&editor.dirty()&&editor.definition()==original,"Undo restores exact previous immutable graph");
    check(editor.redo()&&!editor.dirty(),"Redo restores the saved identity");
    auto bytes=StateMachineArchive::save(*editor.definition());check(StateMachineArchive::save(*StateMachineArchive::load(bytes))==bytes,"Versioned graph file roundtrips IDs, bindings, positions, delays and priority");
    auto retained=editor.definition();check(rejects([&]{editor.open("WEAI 99");})&&editor.definition()==retained,"Malformed import preserves valid document and history");
    auto invalid=next;invalid.transitions[0].to=99;check(rejects([&]{editor.edit(invalid);})&&editor.definition()==retained,"Orphan transitions reject atomically");
    invalid=next;invalid.states[0].position.x=std::numeric_limits<float>::infinity();check(rejects([&]{editor.edit(invalid);}),"Nonfinite graph coordinates reject");
    invalid=next;invalid.states[1].id=1;check(rejects([&]{editor.edit(invalid);}),"Duplicate state identities reject");
    invalid=next;invalid.initial=99;check(rejects([&]{editor.edit(invalid);}),"Missing entry state rejects");
    check(rejects([&]{editor.open(bytes+"garbage");})&&editor.definition()==retained,"Trailing input rejects without replacing current graph");
    check(rejects([&]{editor.open(std::string(524289,'x'));}),"Oversized graph files reject before parsing");
    for(int i=0;i<5;++i){next.name="Graph "+std::to_string(i);editor.edit(next);}check(editor.undoCount()==2,"Graph history stays bounded");
    editor.open(bytes);check(!editor.dirty()&&editor.undoCount()==0&&editor.redoCount()==0,"Successful open starts a clean independent document");
}
}
int main(){runtime();hierarchical();events();authoring();if(failures)return 1;std::cout<<"State machine and AI editor contracts passed\n";}
