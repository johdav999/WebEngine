#include "scene.hpp"
#include "engine/application.hpp"
#include "engine/renderer.hpp"
#include <emscripten.h>
#include <iostream>
namespace {
std::unique_ptr<counter_example::Demo> demo;
engine::Application app;
counter_example::View view;
std::unique_ptr<engine::Renderer> renderer;
int frames=0;
bool rejected=false;
void frame() {
    renderer->serviceRecovery();
    engine::ApplicationFrameCallbacks cb;
    cb.simulate=[](engine::Scene&,const engine::ApplicationFrame&){view.project(*demo);};
    app.advance(emscripten_get_now()/1000,cb);
    if(renderer->ready()){renderer->resizeCanvas();renderer->render();++frames;}
    EM_ASM({window.moduleDemoState=({a:$0,b:$1,frames:$2,rejected:!!$3,backend:$4});document.getElementById('values').textContent='Session A: '+$0+'   /   Session B: '+$1;},demo->first->counter.value,demo->second->counter.value,frames,rejected,int(renderer->capabilities().backend));
}
}
extern "C" EMSCRIPTEN_KEEPALIVE int module_demo_command(int command) {
    if(command==0||command==1)demo->add(command);
    else if(command==2||command==3)demo->reset(command-2);
    else if(command==4)rejected=demo->rejectsBrokenReplacement();
    else return 0;
    return 1;
}
int main() {
    demo=std::make_unique<counter_example::Demo>();
    auto scene=view.make();auto* world=scene.get();auto id=app.loadScene(std::move(scene));
    renderer=std::make_unique<engine::Renderer>(*world,*view.camera);
    const int backend=EM_ASM_INT({return new URLSearchParams(location.search).get('backend')==='webgl2'?1:0;});
    renderer->initialize(backend?engine::RenderBackendPreference::WebGl2:engine::RenderBackendPreference::WebGpu);
    app.start(id);emscripten_set_main_loop(frame,0,true);return 0;
}
