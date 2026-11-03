#include "scene.hpp"
#include "engine/application.hpp"
#include "engine/renderer.hpp"
#include <emscripten.h>
#include <cstdlib>
namespace {
engine::Application app;foliage_example::Demo demo;std::unique_ptr<engine::Renderer> renderer;int frames=0;
bool cpu=false,dense=false;double cpuMs=0;
void status(const char* text){EM_ASM({document.getElementById('status').textContent=UTF8ToString($0);},text);}
void frame(){try{
    const auto started=emscripten_get_now();
    renderer->serviceRecovery();engine::ApplicationFrameCallbacks callbacks;
    callbacks.afterSimulation=[](engine::Scene&,const engine::ApplicationFrame& f){demo.advance(f.simulation.delta);};
    app.advance(emscripten_get_now()/1000,callbacks);
    if(renderer->ready()){renderer->resizeCanvas();renderer->render();++frames;}
    cpuMs=emscripten_get_now()-started;
    const auto& s=demo.foliage->stats();const auto saved=demo.foliage->saveRemoved();const auto motion=demo.motion();
    EM_ASM({window.foliageState=({frames:$0,backend:$1,instances:$2,colliders:$3,visualCells:$4,collisionCells:$5,motion:$6,time:$7,removed:$8,saved:UTF8ToString($9)});},frames,int(renderer->capabilities().backend),double(s.instances),double(s.colliders),double(s.visualCells),double(s.collisionCells),motion,demo.time,double(demo.foliage->removed().size()),saved.c_str());
    EM_ASM({Object.assign(window.foliageState,({cpuMs:$0,cpuReference:!!$1,benchmark:!!$2,bulkComponents:$3,gpuInstances:$4,cpuDeformedVertices:$5,instancing:!!$6}));},cpuMs,int(cpu),int(dense),double(s.bulkComponents),double(s.gpuInstances),double(s.deformedVertices),int(renderer->capabilities().instancing));
}catch(const std::exception& e){status(e.what());emscripten_cancel_main_loop();}}
}
extern "C" EMSCRIPTEN_KEEPALIVE int foliage_command(int command){try{
    if(command==6){if(app.state()==engine::ApplicationState::Paused)app.resume();else app.pause();}
    else if(command==8)renderer->simulateDeviceLoss();
    else if(command==9)renderer->setInstancingEnabled(false);
    else if(command==10)renderer->setInstancingEnabled(true);
    else if(command==4){demo.command(4);auto saved=demo.saved;const auto ok=EM_ASM_INT({try{localStorage.setItem('webengine.foliage.v1',UTF8ToString($0));return 1;}catch(e){return 0;}},saved.c_str());if(!ok)throw std::runtime_error("Storage unavailable");}
    else if(command==5){char* saved=reinterpret_cast<char*>(EM_ASM_PTR({try{const s=localStorage.getItem('webengine.foliage.v1');if(s===null||s.length>3000000)return 0;return stringToNewUTF8(s);}catch(e){return 0;}}));if(!saved)throw std::runtime_error("No saved removals");std::string text(saved);std::free(saved);demo.foliage->loadRemoved(text);demo.advance(0);}
    else demo.command(command);status("Ready");return 1;
}catch(const std::exception& e){status(e.what());return 0;}}
int main(){try{
    const auto gl=EM_ASM_INT({return new URLSearchParams(location.search).get('backend')==='webgl2';});
    cpu=EM_ASM_INT({return new URLSearchParams(location.search).get('cpu')==='1';});dense=EM_ASM_INT({return new URLSearchParams(location.search).get('benchmark')==='1';});
    auto scene=demo.make(gl,cpu,dense);auto* world=scene.get();const auto id=app.loadScene(std::move(scene));
    renderer=std::make_unique<engine::Renderer>(*world,*demo.camera);renderer->initialize(gl?engine::RenderBackendPreference::WebGl2:engine::RenderBackendPreference::WebGpu);
    app.start(id);status("Ready");emscripten_set_main_loop(frame,0,true);return 0;
}catch(const std::exception& e){status(e.what());return 1;}}
