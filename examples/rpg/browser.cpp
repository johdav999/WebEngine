#include "host_controller.hpp"
#include "engine/renderer.hpp"
#include <emscripten.h>

EM_JS(int,readSave,(unsigned char* out,int capacity),{
 try{let text=localStorage.getItem('webengine.rpg.v1');if(text===null)return 0;if(text.length>capacity*2)return -1;
 let bytes=atob(text);if(!bytes.length||bytes.length>capacity)return -1;for(let i=0;i<bytes.length;i++)HEAPU8[out+i]=bytes.charCodeAt(i);return bytes.length;}catch(e){return -1;}
});
EM_JS(int,writeSave,(const unsigned char* data,int size),{
 try{if(window.rpgFailWrite)throw Error('Injected storage failure');let text="";for(let i=0;i<size;i++)text+=String.fromCharCode(HEAPU8[data+i]);
 localStorage.setItem('webengine.rpg.v1',btoa(text));return 1;}catch(e){return 0;}
});
namespace {
engine::Application app;
std::unique_ptr<engine::Renderer> renderer;
std::unique_ptr<rpg_example::Controller> controller;
unsigned frames=0;bool readyBefore=false;
void publish(){
 auto&s=controller->state();auto&p=s.characters.at(controller->player());auto e=s.encounter;
 auto active=e&&e->active?e->active->value:0;auto ap=e?e->allowance.ap:0;
 EM_ASM({window.rpgState=({frames:$0,backend:$1,paused:!!$2,pending:!!$3,awards:$4,xp:$5,active:$6,ap:$7,turn:$8,scene:$9,sceneCount:$10,location:$11,items:$12,message:UTF8ToString($13),epoch:$14,saves:$15});},
 frames,renderer?int(renderer->capabilities().backend):-1,controller->paused(),bool(s.presentation),controller->awards(),double(p.experience),double(active),double(ap),double(s.turnEpoch),double(app.activeSceneId().value),app.loadedSceneCount(),controller->location(),s.items.size(),controller->message.c_str(),double(controller->epoch()),controller->saves);
 EM_ASM({window.rpgState.loads=$0;window.rpgState.yaw=$1;window.refreshRpgUi(window.rpgState);},controller->loads,controller->scene().orbit->yaw);
}
void frame(){try{
 controller->aspect(float(EM_ASM_DOUBLE({return Module.canvas.clientWidth/Math.max(1,Module.canvas.clientHeight);} )));
 renderer->serviceRecovery();bool ready=renderer->ready();if(readyBefore&&!ready)controller->cancelInput();readyBefore=ready;
 controller->drain();if(ready){app.advance(emscripten_get_now()/1000,controller->callbacks());renderer->resizeCanvas();renderer->render();++frames;}else app.resetClocks();publish();
 }catch(const std::exception&e){controller->message=std::string("Host failed: ")+e.what();app.pause();publish();}}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE void rpg_command(int n){if(controller&&n>=0&&n<12)controller->enqueue(rpg_example::Action(n));}
EMSCRIPTEN_KEEPALIVE void rpg_orbit(float yaw,float elevation,float zoom){if(controller)controller->orbit(yaw,elevation,zoom);}
EMSCRIPTEN_KEEPALIVE void rpg_focus(int hidden){if(controller){controller->cancelInput();app.setSuspended(hidden!=0);}}
EMSCRIPTEN_KEEPALIVE void rpg_shutdown(){emscripten_cancel_main_loop();if(controller)controller->cancelInput();app.shutdown();app.setSceneBinding({});controller.reset();renderer.reset();}
}
int main(){
 int backend=EM_ASM_INT({return new URLSearchParams(location.search).get('backend')==='webgl2'?1:0;});
 app.setSceneBinding([backend](engine::Scene*s){if(!s){renderer.reset();return;}auto&scene=dynamic_cast<rpg_example::HostScene&>(*s);engine::RenderView view{&scene.cameraView()};
 if(renderer)renderer->setScene(scene,{&view,1});else{auto candidate=std::make_unique<engine::Renderer>(scene,scene.cameraView());candidate->initialize(backend?engine::RenderBackendPreference::WebGl2:engine::RenderBackendPreference::WebGpu);renderer=std::move(candidate);}});
 rpg_example::Storage storage;
 storage.read=[]()->std::optional<std::vector<std::uint8_t>>{std::vector<std::uint8_t> bytes(engine::rpg::snapshotByteLimit);auto n=readSave(bytes.data(),int(bytes.size()));if(n<0)throw std::runtime_error("Invalid browser save");if(!n)return {};bytes.resize(n);return bytes;};
 storage.write=[](auto bytes){if(!writeSave(bytes.data(),int(bytes.size())))throw std::runtime_error("Save failed; previous save retained");};
 controller=std::make_unique<rpg_example::Controller>(app,std::move(storage),[](auto,auto&){});controller->start();
 app.setSuspended(EM_ASM_INT({return document.hidden?1:0;})!=0);emscripten_set_main_loop(frame,0,true);
}
