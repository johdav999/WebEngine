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
int main(){runtime();authoring();if(failures)return 1;std::cout<<"State machine and AI editor contracts passed\n";}
