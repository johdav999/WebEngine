#include "scene.hpp"
#include "flight.hpp"
#include "engine/application.hpp"
#include "engine/renderer.hpp"
#include <emscripten.h>
namespace {
hybrid_example::Demo demo;
std::unique_ptr<engine::Renderer> renderer;
engine::Application app;
engine::InputBindings flightBindings([](std::span<const engine::InputPacket> packets){return app.submitInput(packets);});
hybrid_example::FlightCamera flight;
int frames=0,loads=0;bool archived=false;
EM_JS(int,storeCheckpoint,(const void* bytes,int size),{
    try{let binary='';for(let i=0;i<size;i+=32768)binary+=String.fromCharCode(...HEAPU8.subarray(bytes+i,bytes+Math.min(size,i+32768)));
        localStorage.setItem('webengine.hybrid.checkpoint.v5',btoa(binary));window.hybridCheckpointStatus='Saved checkpoint';return 1;
    }catch(e){window.hybridCheckpointStatus='Save failed: '+e.message;return 0;}
});
EM_JS(int,readCheckpoint,(void* bytes,int capacity),{
    try{const saved=localStorage.getItem('webengine.hybrid.checkpoint.v5');if(!saved)throw Error('No saved checkpoint');
        if(saved.length>6*1024*1024)throw Error('Checkpoint exceeds storage budget');const binary=atob(saved);
        if(!bytes)return binary.length;if(binary.length!==capacity)throw Error('Checkpoint changed while reading');
        for(let i=0;i<capacity;++i)HEAPU8[bytes+i]=binary.charCodeAt(i);return capacity;
    }catch(e){window.hybridCheckpointStatus='Load failed: '+e.message;return -1;}
});
void loadCheckpoint(){
    const int count=readCheckpoint(nullptr,0);hybrid_example::check(count>0,"No readable checkpoint");
    std::vector<std::byte> bytes(count);hybrid_example::check(readCheckpoint(bytes.data(),count)==count,"Checkpoint read failed");
    auto document=engine::SceneArchive::load(bytes);hybrid_example::Demo candidate;candidate.bindCheckpoint(document);
    const auto id=app.loadScene(std::move(document.scene));
    try{app.requestTransition(id,true);hybrid_example::check(app.commitTransition(),"Checkpoint transition not ready");}
    catch(...){app.unloadScene(id);throw;}
    demo=std::move(candidate);flightBindings.cancel();flight.cancel();++loads;EM_ASM({window.hybridCheckpointStatus='Loaded checkpoint · LOD rebuilt';});
}
void frame(){try{
    renderer->serviceRecovery();demo.update();
    engine::ApplicationFrameCallbacks callbacks;
    callbacks.input=[](engine::Scene&,const engine::InputSystem& input,const engine::ApplicationFrame& frame){flight.update(*demo.camera,input,frame.input.delta);};
    const auto& frameState=app.advance(emscripten_get_now()/1000,callbacks);renderer->setWaterTime(frameState.simulation.seconds);
    if(renderer->ready()){renderer->resizeCanvas();renderer->render();++frames;}
    EM_ASM({window.hybridState=({frames:$0,backend:$1,z:$2,y:$3,moving:!!$4,view:$5,coarse:!!$6,archived:!!$7,x:$8});
        document.getElementById('status').textContent=($4?'Walking':$5===11?'Grotto basin':$5===6?'Lower grotto · tenfold volume':$5===5?'Grand chamber':($2>1?'Inside the cave':'Outside'))+' · '+($6?'Coarse terrain':'Full detail');},
        frames,int(renderer->capabilities().backend),demo.walker->transform.position.z,demo.walker->transform.position.y,demo.moving,demo.view,demo.coarse,archived,demo.walker->transform.position.x);
    EM_ASM({Object.assign(window.hybridState,{painted:$0,undo:$1,redo:$2,width:$3,height:$4,depth:$5,stroke:!!$6,tool:$7});},demo.paintedCells(),int(demo.historyPosition),int(demo.history.size()-demo.historyPosition),demo.entrance.halfWidth*2,demo.entrance.ceiling,demo.entrance.depth,bool(demo.stroke),demo.tool);
    EM_ASM({Object.assign(window.hybridState,{blending:!!$0,seamTriangles:$1});},demo.blending,int(demo.cavePart->mesh->indices.size()/3));
    EM_ASM({Object.assign(window.hybridState,{cameraDetail:$0,shadowDetail:$1,lodLevels:$2});},demo.cameraDetail,demo.shadowDetail,int(demo.cavePart->mesh->lods.size()));
    EM_ASM({Object.assign(window.hybridState,{loads:$0,chambers:$1});},loads,demo.hasSystem()?3:1);
    EM_ASM({Object.assign(window.hybridState,{shapedEntrance:!!$0});},demo.shapedEntrance);
    EM_ASM({Object.assign(window.hybridState,{largeGrotto:!!$0,grottoScale:$1,grottoCentreZ:$2});},demo.hasLargeGrotto(),hybrid_example::grottoScale,demo.lowerCentreZ());
    EM_ASM({Object.assign(window.hybridState,{pond:!!$0,submerged:!!$1});},demo.hasPond(),bool(engine::underwaterView(*demo.world,demo.camera->worldPosition())));
    EM_ASM({Object.assign(window.hybridState,{cliffTriangles:$0,cliffLods:$1,cliffCells:$2});},demo.cliffPart?int(demo.cliffPart->mesh->indices.size()/3):0,demo.cliffPart?int(demo.cliffPart->mesh->lods.size()):0,int(std::count(demo.cliffFootprint.begin(),demo.cliffFootprint.end(),std::uint8_t(1))));
    const auto p=demo.camera->transform.position,f=demo.camera->forward();
    EM_ASM({Object.assign(window.hybridState,{camera:{x:$0,y:$1,z:$2,fx:$3,fy:$4,fz:$5},looking:!!$6});},p.x,p.y,p.z,f.x,f.y,f.z,flight.dragging);
}catch(const std::exception& e){EM_ASM({window.hybridError=UTF8ToString($0);},e.what());emscripten_cancel_main_loop();}}
}
extern "C" EMSCRIPTEN_KEEPALIVE void hybrid_flight(int kind,int code,float x,float y){
    if(kind==0){flightBindings.control(code,x);return;}
    if(kind==4){flightBindings.cancel();flight.cancel();}
    engine::InputPacket packet;packet.type=int(kind==1?engine::InputEventType::PointerDown:kind==2?engine::InputEventType::PointerMove:kind==3?engine::InputEventType::PointerUp:engine::InputEventType::CancelAll);
    packet.id=1;packet.x=x;packet.y=y;app.submitInput({&packet,1});
}
extern "C" EMSCRIPTEN_KEEPALIVE int hybrid_edit(int action,float a,float b,float c,float d){try{
    if(action==0)demo.setTool(int(a));
    else if(action==1){if(!std::isfinite(a)||a<.5f||a>4)throw std::invalid_argument("Brush radius must be 0.5-4 m");demo.endStroke(true);demo.brushRadius=a;}
    else if(action==2){if(!std::isfinite(a)||!std::isfinite(c)||a<4||a>10||c<6||c>14||a!=std::round(a/2)*2||c!=std::round(c))throw std::invalid_argument("Width must be 4-10 even metres and depth 6-14 whole metres");demo.editEntrance({int(a/2),int(c),b});}
    else if(action==3)return demo.undo();else if(action==4)return demo.redo();
    else if(action>=10&&action<=13)demo.pointer(action-10,a,b,c,d);else return 0;
    EM_ASM({window.hybridEditError='';});return 1;
}catch(const std::exception& e){EM_ASM({window.hybridEditError=UTF8ToString($0);},e.what());return 0;}}
extern "C" EMSCRIPTEN_KEEPALIVE int hybrid_probe(float x,float z,float width,float height){
    const auto p=demo.camera->worldToScreen({x,hybrid_example::height(z),z},{0,0,width,height});
    const auto hit=demo.world->physics().raycast({x,12,z},{0,-1,0},20);
    EM_ASM({window.hybridProbe=({x:$0,y:$1,ground:!!$2,collision:!!$3});},p.pixel.x,p.pixel.y,bool(engine::terrainGround(*demo.world,x,z)),bool(hit));return 1;
}
extern "C" EMSCRIPTEN_KEEPALIVE int hybrid_command(int command){try{
    if(command<0||command>21)return 0;
    if(command<3)demo.setView(command);
    else if(command==3)demo.walk(true);
    else if(command==4)demo.walk(false);
    else if(command==5)demo.detail(!demo.coarse);
    else if(command==6){archived=demo.roundTrip();}
    else if(command==7)renderer->simulateDeviceLoss();
    else if(command==20){if(!demo.cliffPart)return 0;demo.setView(10);demo.verifyCliff(*demo.world);}
    else if(command==21){if(!demo.hasLargeGrotto())return 0;demo.setView(11);demo.verifyBasin();}
    else if(command==9)demo.setView(3);
    else if(command==10)demo.blend(!demo.blending);
    else if(command==11){demo.setView(4);demo.detailLevels(-1,-1);}
    else if(command==12){const auto bytes=demo.saveCheckpoint();hybrid_example::check(bytes.size()<=4*1024*1024,"Checkpoint exceeds demo storage budget");return storeCheckpoint(bytes.data(),int(bytes.size()));}
    else if(command==13)loadCheckpoint();
    else if(command==14&&demo.hasSystem())demo.setView(5);
    else if(command==15&&demo.hasSystem())demo.setView(6);
    else if(command==16)demo.walkSystem();
    else if(command>=17&&command<=19)demo.setView(command-10);
    else {demo.motor->teleport({0,.05f,-7});demo.motor->stop();demo.moving=false;demo.setView(0);}
    return 1;
}catch(const std::exception& e){if(command>=12)EM_ASM({window.hybridCheckpointStatus='Checkpoint failed: '+UTF8ToString($0);},e.what());else EM_ASM({window.hybridError=UTF8ToString($0);},e.what());return 0;}}
extern "C" EMSCRIPTEN_KEEPALIVE int hybrid_lod(int camera,int shadow){try{demo.detailLevels(camera,shadow);return 1;}catch(...){return 0;}}
int main(){try{
    hybrid_example::FlightCamera::bind(flightBindings,{1,2,3,4,5,6,7,8,9});
    auto scene=demo.make();hybrid_example::Demo::verify(*scene);const auto id=app.loadScene(std::move(scene));
    renderer=std::make_unique<engine::Renderer>(*demo.world,*demo.camera);
    const bool gl=EM_ASM_INT({return new URLSearchParams(location.search).get('backend')==='webgl2';});
    renderer->initialize(gl?engine::RenderBackendPreference::WebGl2:engine::RenderBackendPreference::WebGpu);
    app.setSceneBinding([](engine::Scene* next){if(!next){renderer.reset();return;}
        auto* camera=dynamic_cast<engine::CameraComponent*>(next->findSavedNode("hybrid-camera"));hybrid_example::check(camera,"Missing checkpoint camera");
        const engine::RenderView view{camera};renderer->setScene(*next,{&view,1});});
    app.start(id);emscripten_set_main_loop(frame,0,true);return 0;
}catch(const std::exception& e){EM_ASM({window.hybridError=UTF8ToString($0);},e.what());return 1;}}
