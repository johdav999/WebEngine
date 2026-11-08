#include "scene.hpp"
#include "engine/application.hpp"
#include "engine/renderer.hpp"
#include <emscripten.h>
#include <cstdlib>
namespace {
engine::Application app;clothing_example::Demo demo;std::unique_ptr<engine::Renderer> renderer;int frames=0;
void status(const char* text) {EM_ASM({document.getElementById('status').textContent=UTF8ToString($0);},text);}
void frame() {
    renderer->serviceRecovery();engine::ApplicationFrameCallbacks callbacks;
    callbacks.afterSimulation=[](engine::Scene&,const engine::ApplicationFrame& frame){demo.advance(frame.simulation.delta);};
    callbacks.present=[](engine::Scene& scene,const engine::ApplicationFrame&){demo.clothing->synchronize(scene);};
    app.advance(emscripten_get_now()/1000,callbacks);
    if(renderer->ready()){renderer->resizeCanvas();renderer->render();++frames;}
    auto saved=demo.save();EM_ASM({window.clothingState=({frames:$0,selection:UTF8ToString($1),backend:$2});},frames,saved.c_str(),int(renderer->capabilities().backend));
    const auto stats=demo.clothing->clothStats(*demo.scene);
    EM_ASM({Object.assign(window.clothingState,({clothSteps:$0,clothResets:$1,clothMotion:$2}));},double(stats.steps),double(stats.resets),demo.clothMotion());
    EM_ASM({Object.assign(window.clothingState,({selfContacts:$0,environmentContacts:$1}));},double(stats.selfContacts),double(stats.environmentContacts));
    EM_ASM({Object.assign(window.clothingState,({surfaceContacts:$0,sweptContacts:$1}));},double(stats.surfaceContacts),double(stats.sweptContacts));
    EM_ASM({Object.assign(window.clothingState,({edgeContacts:$0}));},double(stats.edgeContacts));
    EM_ASM({Object.assign(window.clothingState,({aerodynamicFaces:$0,movingContacts:$1,obstacleMotion:$2,milestone:8}));},double(stats.aerodynamicFaces),double(stats.movingContacts),double(demo.obstacleOffset.x+demo.obstacleOffset.z));
}
}
extern "C" EMSCRIPTEN_KEEPALIVE int clothing_command(int command) {try{
    if(command<5)demo.command(command);
    else if(command==5) {
        auto text=demo.save();const int ok=EM_ASM_INT({try{localStorage.setItem('webengine.clothing.v1',UTF8ToString($0));return 1;}catch(e){return 0;}},text.c_str());
        if(!ok)throw std::runtime_error("Browser storage unavailable");
    } else if(command==6) {
        char* text=reinterpret_cast<char*>(EM_ASM_PTR({try{const s=localStorage.getItem('webengine.clothing.v1');if(s===null||s.length>4096)return 0;return stringToNewUTF8(s);}catch(e){return 0;}}));
        if(!text)throw std::runtime_error("No saved outfit or browser storage unavailable");
        std::string saved(text);std::free(text);demo.load(saved);
    } else if(command==7) {if(app.state()==engine::ApplicationState::Paused)app.resume();else app.pause();}
    else if(command>=8&&command<=12)demo.command(command);
    else return 0;
    status(command==5?"Outfit saved":command==6?"Outfit restored":"Ready");return 1;
}catch(const std::exception& e){status(e.what());return 0;}}
int main(){try{
    auto scene=demo.make();demo.command(8);auto* world=scene.get();auto id=app.loadScene(std::move(scene));
    renderer=std::make_unique<engine::Renderer>(*world,*demo.camera);
    const int gl=EM_ASM_INT({return new URLSearchParams(location.search).get('backend')==='webgl2';});
    renderer->initialize(gl?engine::RenderBackendPreference::WebGl2:engine::RenderBackendPreference::WebGpu);
    app.start(id);status("Ready");emscripten_set_main_loop(frame,0,true);return 0;
}catch(const std::exception& e){status(e.what());return 1;}}
