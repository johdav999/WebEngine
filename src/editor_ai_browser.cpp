#include "editor_ai_browser.hpp"
#include "editor_browser.hpp"
#include "ai_demo.hpp"
#include "engine/editor_ai.hpp"
#include "ai_event_browser.hpp"
#include "ai_task_browser.hpp"
#include <emscripten.h>
#include <iomanip>
#include <sstream>
#include <map>
#include <set>
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
bool beneath(const StateMachineData& d,std::uint32_t id,std::uint32_t ancestor){for(unsigned depth=0;depth<16;++depth){if(id==ancestor)return true;auto it=std::find_if(d.states.begin(),d.states.end(),[&](const auto& s){return s.id==id;});if(it==d.states.end()||!it->parent)return false;id=*it->parent;}return false;}
void repairEntries(StateMachineData& d){for(auto& s:d.states){auto first=std::find_if(d.states.begin(),d.states.end(),[&](const auto& c){return c.parent==s.id;});auto chosen=std::find_if(d.states.begin(),d.states.end(),[&](const auto& c){return s.initialChild==c.id&&c.parent==s.id;});if(first==d.states.end())s.initialChild.reset();else if(chosen==d.states.end())s.initialChild=first->id;}auto initial=std::find_if(d.states.begin(),d.states.end(),[&](const auto& s){return s.id==d.initial&&!s.parent;});if(initial==d.states.end()){auto root=std::find_if(d.states.begin(),d.states.end(),[](const auto& s){return !s.parent;});if(root!=d.states.end())d.initial=root->id;}}
void snapshot(){
    const auto& edit=document();const auto& d=edit.definition()->data();std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"{\"name\":"<<std::quoted(d.name)<<",\"initial\":"<<d.initial<<",\"dirty\":"<<(edit.dirty()?"true":"false")<<",\"undo\":"<<edit.undoCount()<<",\"redo\":"<<edit.redoCount()<<",\"states\":[";
    bool comma=false;for(const auto& s:d.states){if(comma)out<<',';comma=true;out<<"{\"id\":"<<s.id<<",\"name\":"<<std::quoted(s.name)<<",\"behavior\":"<<std::quoted(s.behavior)<<",\"x\":"<<s.position.x<<",\"y\":"<<s.position.y<<",\"parent\":"<<(s.parent?std::to_string(*s.parent):"null")<<",\"initialChild\":"<<(s.initialChild?std::to_string(*s.initialChild):"null");aiTaskBrowser::state(out,s);out<<'}';}
    out<<"],\"transitions\":[";comma=false;
    for(const auto& t:d.transitions){if(comma)out<<',';comma=true;out<<"{\"id\":"<<t.id<<",\"from\":"<<t.from<<",\"to\":"<<t.to<<",\"condition\":"<<std::quoted(t.condition)<<",\"delay\":"<<t.minimumSeconds<<",\"event\":"<<std::quoted(t.event)<<'}';}
    out<<"],\"blackboard\":[";comma=false;for(const auto& f:d.blackboard){if(comma)out<<',';comma=true;out<<"{\"key\":"<<std::quoted(f.key)<<",\"type\":"<<int(f.type)<<",\"minimum\":"<<f.minimum<<",\"maximum\":"<<f.maximum<<",\"initial\":";std::visit([&](const auto& x){using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,bool>)out<<(x?"true":"false");else if constexpr(std::is_same_v<T,Vec3>)out<<'['<<x.x<<','<<x.y<<','<<x.z<<']';else if constexpr(std::is_same_v<T,std::string>)out<<std::quoted(x);else out<<x;},f.initial);out<<'}';}
    out<<"],\"taskSchemas\":";aiTaskBrowser::schemas(out);out<<",\"events\":";aiEventBrowser::definitions(out,d.events);out<<",\"eventQueue\":";if(preview.machine)aiEventBrowser::diagnostics(out,*preview.machine);else out<<"null";
    out<<",\"preview\":"<<(preview.machine?"true":"false")<<",\"current\":"<<(preview.machine?preview.machine->current():d.initial)<<",\"seconds\":"<<(preview.machine?preview.machine->stateSeconds():0)<<",\"entries\":"<<preview.entries<<",\"exits\":"<<preview.exits<<",\"path\":[";comma=false;if(preview.machine)for(auto id:preview.machine->activePath()){if(comma)out<<',';comma=true;out<<id;}out<<"]}";
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
    else if(action==5){StateMachineData d;d.name="Hierarchical agent";d.states={{0,"Alive","idle",{60,80},{},1},{1,"Patrol","move",{100,220},0,{}},{2,"Combat","idle",{460,80},0,3},{3,"Chase","move",{460,220},2,{}},{4,"Attack","idle",{730,220},2,{}},{5,"Retreat","move",{730,380},2,{}},{6,"Dead","idle",{1050,80}}};d.transitions={{0,0,6,"flag",0},{1,1,2,"always",.5},{2,3,4,"always",.5},{3,4,5,"always",.5},{4,5,3,"always",.5}};d.blackboard={{"dead",AiParameterType::Boolean,false}};document().edit(std::move(d));}
    else if(action==6){StateMachineData d;d.name="Event-driven agent";d.states={{0,"Alive","idle",{60,80},{},1},{1,"Patrol","move",{100,220},0,{}},{2,"Investigate","move",{400,220},0,{}},{3,"Combat","idle",{720,80},0,4},{4,"Attack","idle",{720,220},3,{}},{5,"Recover","idle",{980,220},3,{}},{6,"Return","move",{400,420},0,{}}};d.events={{"Damaged",{{"amount",AiParameterType::Number,1.0,0,100},{"source",AiParameterType::Actor,std::string{}}}},{"HeardNoise",{{"position",AiParameterType::Vector,Vec3{},-100,100},{"urgent",AiParameterType::Boolean,false}}},{"AnimationFinished",{{"channel",AiParameterType::Number,0.0,0,8}}}};d.blackboard={{"allowDamage",AiParameterType::Boolean,true}};d.transitions={{0,0,3,"flag",0,"Damaged"},{1,1,2,"always",0,"HeardNoise"},{2,2,6,"always",0,"AnimationFinished"},{3,4,5,"always",0,"AnimationFinished"},{4,5,1,"always",0,"AnimationFinished"},{5,6,1,"always",0,"AnimationFinished"}};document().edit(std::move(d));}
    else if(action==7){StateMachineData d;d.name="Task sequence agent";d.states={{0,"Work","idle",{100,120}},{1,"Done","idle",{500,80}},{2,"Interrupted","idle",{500,300}}};d.events={{"TaskSucceeded",{}},{"TaskFailed",{}},{"AnimationFinished",{}},{"Damaged",{}}};auto r=AiBindingRegistry::builtins();auto wait=r->taskDefaults("wait");wait.parameters["seconds"]=.5;auto event=r->taskDefaults("wait-event");event.event="AnimationFinished";event.timeout=10;d.states[0].tasks={wait,event,wait};d.states[0].succeededEvent="TaskSucceeded";d.states[0].failedEvent="TaskFailed";d.transitions={{0,0,1,"always",0,"TaskSucceeded"},{1,0,2,"always",0,"TaskFailed"},{2,0,2,"always",0,"Damaged"}};document().edit(std::move(d));}
    else throw std::invalid_argument("Unknown AI graph action");
},action!=3);}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_tasks(const char* bytes,int length){return perform([&]{if(!bytes||length<0||length>524288)throw std::invalid_argument("Invalid AI task graph");auto graph=StateMachineArchive::load(std::string_view(bytes,std::size_t(length)));for(const auto& state:graph->data().states)for(const auto& task:state.tasks)AiBindingRegistry::builtins()->validateTask(task,*graph);document().edit(graph->data());});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_open(const char* bytes,int size){return perform([&]{if(!bytes||size<0||size>524288)throw std::invalid_argument("Invalid AI file size");document().open(std::string_view(bytes,std::size_t(size)));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_blackboard(const char* bytes,int length){return perform([&]{if(!bytes||length<0||length>65536)throw std::invalid_argument("Invalid AI schema payload");std::istringstream in{std::string(bytes,std::size_t(length))};in.imbue(std::locale::classic());std::size_t count=0;if(!(in>>count)||count>64)throw std::invalid_argument("Invalid blackboard field count");auto d=document().definition()->data();d.blackboard.clear();for(std::size_t i=0;i<count;++i){AiParameter p;if(!(in>>std::quoted(p.key)))throw std::invalid_argument("Invalid field name");p.initial=readAiValue(in);p.type=AiParameterType(p.initial.index());if(!(in>>p.minimum>>p.maximum))throw std::invalid_argument("Invalid field bounds");d.blackboard.push_back(std::move(p));}in>>std::ws;if(!in.eof())throw std::invalid_argument("Invalid schema suffix");document().edit(std::move(d));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_name(const char* name){return perform([&]{auto d=document().definition()->data();d.name=name?name:"";document().edit(std::move(d));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_state(int operation,unsigned id,const char* name,const char* behavior,float x,float y){return perform([&]{
    auto d=document().definition()->data();
    if(operation==0)d.states.push_back({id,name?name:"",behavior?behavior:"",{x,y}});
    else {
        auto it=std::find_if(d.states.begin(),d.states.end(),[&](const auto& s){return s.id==id;});if(it==d.states.end())throw std::invalid_argument("Select an AI state");
        if(operation==1){const auto delta=Vec2{x-it->position.x,y-it->position.y};it->name=name?name:"";it->behavior=behavior?behavior:"";for(auto& child:d.states)if(beneath(d,child.id,id)){child.position.x+=delta.x;child.position.y+=delta.y;}}
        else if(operation==2){std::set<std::uint32_t> removed;for(const auto& child:d.states)if(beneath(d,child.id,id))removed.insert(child.id);std::erase_if(d.states,[&](const auto& s){return removed.contains(s.id);});std::erase_if(d.transitions,[&](const auto& t){return removed.contains(t.from)||removed.contains(t.to);});repairEntries(d);}
        else if(operation==3){if(it->parent){for(auto& p:d.states)if(p.id==*it->parent)p.initialChild=id;}else d.initial=id;}else throw std::invalid_argument("Unknown state edit");
    }
    document().edit(std::move(d));
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_hierarchy(int operation,unsigned id,int hasParent,unsigned parent){return perform([&]{auto d=document().definition()->data();if(operation==0){auto it=std::find_if(d.states.begin(),d.states.end(),[&](const auto& s){return s.id==id;});if(it==d.states.end())throw std::invalid_argument("Select a state");if(hasParent&&beneath(d,parent,id))throw std::invalid_argument("AI parent would create a cycle");it->parent=hasParent?std::optional<std::uint32_t>(parent):std::nullopt;}
    else if(operation==1){auto it=std::find_if(d.states.begin(),d.states.end(),[&](const auto& s){return s.id==id;});if(it==d.states.end())throw std::invalid_argument("Select a group");it->initialChild=parent;}
    else if(operation==2){std::uint32_t next=0;for(const auto& s:d.states){if(s.id==UINT32_MAX)throw std::invalid_argument("AI state IDs exhausted");next=std::max(next,s.id+1);}AiState child{next,"New state","idle",{100,220}};if(hasParent){auto group=std::find_if(d.states.begin(),d.states.end(),[&](const auto& s){return s.id==parent;});if(group==d.states.end())throw std::invalid_argument("Missing parent");child.parent=parent;child.position={group->position.x+40,group->position.y+140};}d.states.push_back(std::move(child));}
    else throw std::invalid_argument("Unknown hierarchy edit");if(operation!=1)repairEntries(d);document().edit(std::move(d));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_transition(int operation,unsigned id,unsigned from,unsigned to,const char* condition,double delay){return perform([&]{
    auto d=document().definition()->data();
    if(operation==0)d.transitions.push_back({id,from,to,condition?condition:"",delay});
    else {
        auto it=std::find_if(d.transitions.begin(),d.transitions.end(),[&](const auto& t){return t.id==id;});if(it==d.transitions.end())throw std::invalid_argument("Select a transition");
        if(operation==1){auto event=it->event;*it={id,from,to,condition?condition:"",delay,std::move(event)};}else if(operation==2)d.transitions.erase(it);
        else if(operation==3){if(it!=d.transitions.begin())std::iter_swap(it,it-1);}
        else if(operation==4){if(it+1!=d.transitions.end())std::iter_swap(it,it+1);}
        else throw std::invalid_argument("Unknown transition edit");
    }
    document().edit(std::move(d));
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_events(const char* bytes,int length){return perform([&]{if(!bytes||length<0||length>262144)throw std::invalid_argument("Invalid AI event definitions");std::istringstream in{std::string(bytes,std::size_t(length))};in.imbue(std::locale::classic());auto d=document().definition()->data();d.events.clear();std::size_t count=0;if(!(in>>count)||count>32)throw std::invalid_argument("Invalid event count");for(std::size_t i=0;i<count;++i){AiEventDefinition e;std::size_t n=0;if(!(in>>std::quoted(e.key)>>n)||n>8)throw std::invalid_argument("Invalid event schema");for(std::size_t j=0;j<n;++j){AiParameter p;if(!(in>>std::quoted(p.key)))throw std::invalid_argument("Invalid event field");p.initial=readAiValue(in);p.type=AiParameterType(p.initial.index());if(!(in>>p.minimum>>p.maximum))throw std::invalid_argument("Invalid event range");e.parameters.push_back(std::move(p));}d.events.push_back(std::move(e));}in>>std::ws;if(!in.eof())throw std::invalid_argument("Unexpected event schema suffix");document().edit(std::move(d));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_trigger(unsigned id,const char* key){return perform([&]{auto d=document().definition()->data();auto it=std::find_if(d.transitions.begin(),d.transitions.end(),[&](const auto& t){return t.id==id;});if(it==d.transitions.end())throw std::invalid_argument("Select a transition");it->event=key?key:"";document().edit(std::move(d));});}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_event(const char* bytes,int length){return perform([&]{if(!preview.machine||!bytes||length<0||length>65536)throw std::invalid_argument("Start preview before posting events");auto request=aiEventBrowser::request(std::string_view(bytes,std::size_t(length)));if(!preview.machine->postEvent(std::move(request.first),request.second))throw std::invalid_argument("AI event rejected; inspect event trace");},false);}
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
