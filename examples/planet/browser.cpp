#include "demo.hpp"
#include "engine/renderer.hpp"
#include <emscripten.h>
namespace {
planet_example::Demo demo;
planet_example::Controls controls;
engine::Application app;
engine::InputBindings bindings([](std::span<const engine::InputPacket> packets){return app.submitInput(packets);});
std::unique_ptr<engine::Renderer> renderer;
int presented=0;
void fail(const char* error){EM_ASM({window.planetError=UTF8ToString($0);document.getElementById('status').textContent='Error: '+window.planetError;},error);}
void frame(){try{
    renderer->serviceRecovery();engine::ApplicationFrameCallbacks callbacks;
    callbacks.input=[](engine::Scene&,const engine::InputSystem& input,const engine::ApplicationFrame& frame){controls.update(demo,input,frame.input.delta);};
    callbacks.simulate=[](engine::Scene&,const engine::ApplicationFrame&){demo.prepareSimulation();};
    callbacks.present=[](engine::Scene&,const engine::ApplicationFrame& frame){demo.project(frame.presentation.delta,float(EM_ASM_INT({return Module.canvas.height;})),frame.interpolationAlpha);};
    app.advance(emscripten_get_now()/1000,callbacks);
    if(renderer->ready()){renderer->resizeCanvas();renderer->render();++presented;}
    const auto& stats=demo.terrain.stats();const auto f=demo.forward();const auto text=demo.status();
    EM_ASM({window.planetState=({frames:$0,backend:$1,altitude:$2,level:$3,patches:$4,resident:$5,splits:$6,merges:$7,triangles:$8});},
        presented,int(renderer->capabilities().backend),demo.altitude(),stats.deepest,int(stats.leaves),int(stats.resident),int(stats.splits),int(stats.merges),int(stats.triangles));
    EM_ASM({Object.assign(window.planetState,({x:$0,y:$1,z:$2,fx:$3,fy:$4,fz:$5,tour:$6,rebases:$7,localCamera:$8}));
        document.getElementById('status').textContent=UTF8ToString($9);},
        demo.position.x,demo.position.y,demo.position.z,f.x,f.y,f.z,demo.tour,int(demo.rebases),demo.camera->worldPosition().x,text.c_str());
    const auto& motion=demo.motor.state();
    EM_ASM({Object.assign(window.planetState,({grounded:!!$0,landings:$1,jumps:$2,thrusters:!!$3,gravity:$4,collisionReady:!!$5,
        collisionBuilds:$6,groundGap:$7,cargoGap:$8,physicsSteps:$9}));},
        motion.grounded,int(motion.landings),int(motion.jumps),demo.thrusters,motion.gravity.length(),demo.collisionReady,int(demo.collisionBuilds),demo.groundGap(),demo.cargoGap(),int(motion.steps));
    EM_ASM({Object.assign(window.planetState,({vx:$0,vy:$1,vz:$2,waiting:!!$3,coarse:!!$4,collisionTriangles:$5}));},
        motion.velocity.x,motion.velocity.y,motion.velocity.z,motion.waitingForCollision,demo.coarseVisual,demo.collision?int(demo.collision->shape->indices().size()/3):0);
}catch(const std::exception& e){fail(e.what());emscripten_cancel_main_loop();}}
}
extern "C" EMSCRIPTEN_KEEPALIVE int planet_command(int command){try{
    bindings.cancel();controls.cancel();
    if(command==5)renderer->simulateDeviceLoss();else if(command>=0&&command<=9)demo.request(command);else return 0;
    return 1;
}catch(const std::exception& e){fail(e.what());return 0;}}
extern "C" EMSCRIPTEN_KEEPALIVE void planet_input(int kind,int code,float x,float y){
    if(kind==0){bindings.control(code,x);return;}if(kind==4){bindings.cancel();controls.cancel();}
    engine::InputPacket p;p.type=int(kind==1?engine::InputEventType::PointerDown:kind==2?engine::InputEventType::PointerMove:kind==3?engine::InputEventType::PointerUp:engine::InputEventType::CancelAll);
    p.id=1;p.x=x;p.y=y;app.submitInput({&p,1});
}
int main(){try{
    planet_example::Controls::bind(bindings,{1,2,3,4,5,6,7,8,9,10,11});
    auto scene=demo.make();auto id=app.loadScene(std::move(scene));renderer=std::make_unique<engine::Renderer>(*demo.world,*demo.camera);
    const bool gl=EM_ASM_INT({return new URLSearchParams(location.search).get('backend')==='webgl2';});
    renderer->initialize(gl?engine::RenderBackendPreference::WebGl2:engine::RenderBackendPreference::WebGpu);
    renderer->setShadowsEnabled(false);app.setSceneBinding([](engine::Scene* next){if(!next)renderer.reset();});app.start(id);
    emscripten_set_main_loop(frame,0,true);return 0;
}catch(const std::exception& e){fail(e.what());return 1;}}
