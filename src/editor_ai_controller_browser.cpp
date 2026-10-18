#include "editor_browser.hpp"
#include "editor_ai_browser.hpp"
#include "engine/editor.hpp"
#include "engine/ai_controller.hpp"
#include <emscripten.h>
#include <sstream>
#include <iomanip>
namespace engine {
namespace {
std::string q(std::string_view s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)out+=' ';else out+=char(c);}return out+'"';}
void value(std::ostream& o,const AiValue& v){std::visit([&](const auto& x){using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,bool>)o<<(x?"true":"false");else if constexpr(std::is_same_v<T,std::string>)o<<q(x);else if constexpr(std::is_same_v<T,Vec3>)o<<'['<<x.x<<','<<x.y<<','<<x.z<<']';else o<<x;},v);}
void binding(std::ostream& o,const AiBinding& b){o<<"{\"key\":"<<q(b.key)<<",\"parameters\":{";bool comma=false;for(const auto&[k,v]:b.parameters){if(comma)o<<',';comma=true;o<<q(k)<<':';value(o,v);}o<<"}}";}
void graph(std::ostream& o,const StateMachineDefinition& def){auto& d=def.data();o<<"{\"name\":"<<q(d.name)<<",\"initial\":"<<d.initial<<",\"states\":[";bool comma=false;for(const auto& s:d.states){if(comma)o<<',';comma=true;o<<"{\"id\":"<<s.id<<",\"name\":"<<q(s.name)<<",\"behavior\":"<<q(s.behavior)<<",\"x\":"<<s.position.x<<",\"y\":"<<s.position.y<<'}';}o<<"],\"transitions\":[";comma=false;for(const auto& t:d.transitions){if(comma)o<<',';comma=true;o<<"{\"id\":"<<t.id<<",\"from\":"<<t.from<<",\"to\":"<<t.to<<",\"condition\":"<<q(t.condition)<<",\"delay\":"<<t.minimumSeconds<<'}';}o<<"]}";}
Actor* selectedActor(){auto* session=browserEditorSession();if(!session)return nullptr;auto* n=session->selected();if(auto* a=dynamic_cast<Actor*>(n))return a;if(auto* c=dynamic_cast<Component*>(n))return c->owner();return nullptr;}
EM_JS(void,ai_actor_report,(const char* data),{window.EngineEditorAiActor?.inspect(JSON.parse(UTF8ToString(data)));});
EM_JS(void,ai_actor_message,(const char* data),{window.EngineEditorAiActor?.message(UTF8ToString(data),true);});
void snapshot(){auto* scene=browserEditorScene();auto* actor=selectedActor();auto* ai=actor?actor->getComponent<AiControllerComponent>():nullptr;std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17);
    o<<"{\"actor\":"<<q(actor?actor->savedId():"")<<",\"name\":"<<q(actor?actor->name:"")<<",\"controller\":"<<q(ai?ai->savedId():"")<<",\"motor\":"<<(actor&&actor->hasComponent<CharacterMotorComponent>()?"true":"false")<<",\"playing\":"<<(browserEditorPlaying()?"true":"false")<<",\"behaviors\":[";
    auto registry=AiBindingRegistry::builtins();auto schema=[&](const auto& list){bool comma=false;for(const auto& b:list){if(comma)o<<',';comma=true;o<<"{\"key\":"<<q(b.key)<<",\"parameters\":[";bool sep=false;for(const auto& p:b.parameters){if(sep)o<<',';sep=true;o<<"{\"key\":"<<q(p.key)<<",\"type\":"<<int(p.type)<<",\"minimum\":"<<p.minimum<<",\"maximum\":"<<p.maximum<<",\"initial\":";value(o,p.initial);o<<'}';}o<<"]}";}};
    schema(registry->behaviors());o<<"],\"conditions\":[";schema(registry->conditions());o<<"],\"actors\":[";std::size_t count=0;if(scene)scene->visit([&](const Node& n,const Mat4&){if(auto* a=dynamic_cast<const Actor*>(&n);a&&count<256){if(count++)o<<',';o<<"{\"id\":"<<q(a->savedId())<<",\"name\":"<<q(a->name)<<'}';}});o<<']';
    if(ai){o<<",\"configuration\":{\"graph\":";graph(o,*ai->settings().graph);o<<",\"states\":[";bool comma=false;for(const auto&[id,b]:ai->settings().states){if(comma)o<<',';comma=true;o<<"{\"id\":"<<id<<",\"binding\":";binding(o,b);o<<'}';}o<<"],\"conditions\":[";comma=false;for(const auto&[k,b]:ai->settings().conditions){if(comma)o<<',';comma=true;o<<"{\"condition\":"<<q(k)<<",\"binding\":";binding(o,b);o<<'}';}o<<"]}";}
    auto* runtime=browserEditorPlaySession().scene();auto* live=ai&&runtime?dynamic_cast<AiControllerComponent*>(runtime->findSavedNode(ai->savedId())):nullptr;
    if(live){o<<",\"live\":{\"state\":"<<live->currentState()<<",\"seconds\":"<<live->stateSeconds()<<",\"status\":"<<int(live->status())<<",\"error\":"<<q(live->error())<<",\"position\": [";auto p=transformPoint(live->owner()->worldMatrix(),{});o<<p.x<<','<<p.y<<','<<p.z<<"],\"transitions\":[";bool comma=false;for(const auto& t:live->transitions()){if(comma)o<<',';comma=true;o<<"{\"from\":"<<t.from<<",\"to\":"<<t.to<<",\"seconds\":"<<t.simulationSeconds<<'}';}o<<"]}";}
    o<<'}';auto text=o.str();ai_actor_report(text.c_str());
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_actor_snapshot(){if(!browserEditorActive())return 0;try{snapshot();return 1;}catch(const std::exception& e){ai_actor_message(e.what());return 0;}}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_actor_apply(const char* bytes,int length){try{if(!browserEditorActive()||browserEditorPlaying()||browserEditorFxActive()||!bytes||length<0||length>2097152)return 0;auto settings=AiControllerArchive::load(std::string_view(bytes,std::size_t(length)),*AiBindingRegistry::builtins());if(!browserEditorSession()->setAiController(std::move(settings)))return 0;refreshBrowserEditor();snapshot();return 1;}catch(const std::exception& e){ai_actor_message(e.what());snapshot();return 0;}}
EMSCRIPTEN_KEEPALIVE int engine_editor_ai_actor_action(int action){try{if(!browserEditorActive())return 0;auto* session=browserEditorSession();auto* ai=session->selectedAiController();if(action==2){if(!browserEditorPlaying()||!ai)return 0;auto* live=dynamic_cast<AiControllerComponent*>(browserEditorPlaySession().scene()->findSavedNode(ai->savedId()));if(!live)return 0;live->restart();}
    else{if(browserEditorPlaying())return 0;if(action==0){if(!ai)return 0;loadBrowserAiDefinition(ai->settings().graph);}else if(action==1){if(!session->addComponent("CharacterMotorComponent"))return 0;refreshBrowserEditor();}else return 0;}snapshot();return 1;
}catch(const std::exception& e){ai_actor_message(e.what());return 0;}}
}
} // namespace engine
