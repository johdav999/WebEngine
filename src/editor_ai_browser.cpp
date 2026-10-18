#include "editor_ai_browser.hpp"
#include "editor_browser.hpp"
#include "ai_demo.hpp"
#include "engine/editor_ai.hpp"
#include <emscripten.h>
#include <iomanip>
#include <sstream>
#include <map>
namespace engine {
namespace {
AiGraphEditor& document(){static AiGraphEditor value(aiDemoStateMachine());return value;}
struct Preview:StateMachineHost {
    std::unique_ptr<StateMachine> machine;
    std::map<std::string,bool> conditions;
    std::size_t entries=0,exits=0;
    void enter(const AiState&) override{++entries;}
    void exit(const AiState&) noexcept override{++exits;}
    bool condition(std::string_view key) override{return conditions[std::string(key)];}
    void stop(){if(machine)machine->stop(*this);machine.reset();}
} preview;
EM_JS(void,report,(const char* json),{window.EngineEditorAi?.inspect(JSON.parse(UTF8ToString(json)));});
EM_JS(void,error,(const char* text),{window.EngineEditorAi?.message(UTF8ToString(text),true);});
EM_JS(void,file,(const char* bytes),{window.EngineEditorAi?.download(UTF8ToString(bytes));});
void snapshot(){
    const auto& edit=document();const auto& d=edit.definition()->data();std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"{\"name\":"<<std::quoted(d.name)<<",\"initial\":"<<d.initial<<",\"dirty\":"<<(edit.dirty()?"true":"false")<<",\"undo\":"<<edit.undoCount()<<",\"redo\":"<<edit.redoCount()<<",\"states\":[";
    bool comma=false;for(const auto& s:d.states){if(comma)out<<',';comma=true;out<<"{\"id\":"<<s.id<<",\"name\":"<<std::quoted(s.name)<<",\"behavior\":"<<std::quoted(s.behavior)<<",\"x\":"<<s.position.x<<",\"y\":"<<s.position.y<<'}';}
    out<<"],\"transitions\":[";comma=false;
    for(const auto& t:d.transitions){if(comma)out<<',';comma=true;out<<"{\"id\":"<<t.id<<",\"from\":"<<t.from<<",\"to\":"<<t.to<<",\"condition\":"<<std::quoted(t.condition)<<",\"delay\":"<<t.minimumSeconds<<'}';}
    out<<"],\"preview\":"<<(preview.machine?"true":"false")<<",\"current\":"<<(preview.machine?preview.machine->current():d.initial)<<",\"seconds\":"<<(preview.machine?preview.machine->stateSeconds():0)<<",\"entries\":"<<preview.entries<<",\"exits\":"<<preview.exits<<'}';
    const auto json=out.str();report(json.c_str());
}
bool usable(){return browserEditorActive()&&!browserEditorPlaying()&&!browserEditorFxActive();}
template<class F> int perform(F fn,bool editing=true){try{if(!usable()||(editing&&preview.machine))return 0;fn();snapshot();return 1;}catch(const std::exception& e){error(e.what());snapshot();return 0;}}
}
std::shared_ptr<const StateMachineDefinition> browserAiDefinition(){return document().definition();}
void stopBrowserAiPreview(){preview.stop();}
void loadBrowserAiDefinition(std::shared_ptr<const StateMachineDefinition> graph){if(preview.machine)throw std::logic_error("Stop graph preview before loading an actor graph");document().edit(graph->data());snapshot();}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_snapshot(){return perform([]{},false);}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_action(int action){return perform([&]{
    if(action==0)document().edit(aiDemoStateMachine()->data());
    else if(action==1)document().undo();else if(action==2)document().redo();
    else if(action==3){const auto bytes=StateMachineArchive::save(*document().definition());file(bytes.c_str());document().markSaved();}
    else if(action==4){StateMachineData d;d.name="Actor movement cycle";d.states={{0,"Idle","idle",{100,120}},{1,"Move","move",{520,120}}};d.transitions={{0,0,1,"always",.5},{1,1,0,"always",.5}};document().edit(std::move(d));}
    else throw std::invalid_argument("Unknown AI graph action");
},action!=3);}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_open(const char* bytes,int size){return perform([&]{if(!bytes||size<0||size>524288)throw std::invalid_argument("Invalid AI file size");document().open(std::string_view(bytes,std::size_t(size)));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_name(const char* name){return perform([&]{auto d=document().definition()->data();d.name=name?name:"";document().edit(std::move(d));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_state(int operation,unsigned id,const char* name,const char* behavior,float x,float y){return perform([&]{
    auto d=document().definition()->data();
    if(operation==0)d.states.push_back({id,name?name:"",behavior?behavior:"",{x,y}});
    else {
        auto it=std::find_if(d.states.begin(),d.states.end(),[&](const auto& s){return s.id==id;});if(it==d.states.end())throw std::invalid_argument("Select an AI state");
        if(operation==1){it->name=name?name:"";it->behavior=behavior?behavior:"";it->position={x,y};}
        else if(operation==2){d.states.erase(it);std::erase_if(d.transitions,[&](const auto& t){return t.from==id||t.to==id;});if(d.initial==id&&!d.states.empty())d.initial=d.states.front().id;}
        else if(operation==3)d.initial=id;else throw std::invalid_argument("Unknown state edit");
    }
    document().edit(std::move(d));
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_transition(int operation,unsigned id,unsigned from,unsigned to,const char* condition,double delay){return perform([&]{
    auto d=document().definition()->data();
    if(operation==0)d.transitions.push_back({id,from,to,condition?condition:"",delay});
    else {
        auto it=std::find_if(d.transitions.begin(),d.transitions.end(),[&](const auto& t){return t.id==id;});if(it==d.transitions.end())throw std::invalid_argument("Select a transition");
        if(operation==1)*it={id,from,to,condition?condition:"",delay};else if(operation==2)d.transitions.erase(it);
        else if(operation==3){if(it!=d.transitions.begin())std::iter_swap(it,it-1);}
        else if(operation==4){if(it+1!=d.transitions.end())std::iter_swap(it,it+1);}
        else throw std::invalid_argument("Unknown transition edit");
    }
    document().edit(std::move(d));
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_preview(int action){return perform([&]{
    if(action==0)preview.stop();
    else if(action==1){preview.stop();preview.conditions.clear();preview.entries=preview.exits=0;preview.machine=std::make_unique<StateMachine>(document().definition());preview.machine->step(preview,1.0/60);}
    else if(action==2){if(!preview.machine)throw std::logic_error("Start preview first");preview.machine->step(preview,.1);}
    else throw std::invalid_argument("Unknown preview action");
},false);}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_condition(const char* key,int value){return perform([&]{
    if(!preview.machine||!key)throw std::logic_error("Start preview first");
    const auto& ts=document().definition()->data().transitions;if(std::none_of(ts.begin(),ts.end(),[&](const auto& t){return t.condition==key;}))throw std::invalid_argument("Unknown preview condition");
    preview.conditions[key]=value!=0;
},false);}
}
} // namespace engine
