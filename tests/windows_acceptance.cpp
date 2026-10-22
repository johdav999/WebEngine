#include "../examples/windows_acceptance_scene.hpp"
#include "engine/desktop.hpp"
#include "desktop/sdl_display.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <mutex>
#include <vector>

using namespace engine;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string quote(std::string_view text){std::string s="\"";for(unsigned char c:text){if(c=='"'||c=='\\'){s+='\\';s+=c;}else if(c=='\n')s+="\\n";else if(c<32)s+='?';else s+=c;}return s+'"';}
const char* safe(const char* text){return text?text:"";}
SDL_Window* onlyWindow(){int count=0;auto** windows=SDL_GetWindows(&count);require(count==1,"Expected one host window");auto* window=windows[0];SDL_free(windows);return window;}
struct Report {
    std::filesystem::path directory;
    std::vector<std::string> cases,captures;
    std::string displays="[]",controllers="[]",failure;
    void result(std::string_view id,std::string_view status,std::string_view detail){cases.push_back("{\"id\":"+quote(id)+",\"status\":"+quote(status)+",\"detail\":"+quote(detail)+"}");std::cout<<id<<": "<<status<<" - "<<detail<<std::endl;}
    void write() const {
        std::ofstream out(directory/"native-report.json");out<<"{\"schema\":1,\"backend\":\"Dawn D3D12\",\"failure\":"<<quote(failure)<<",\"displays\":"<<displays<<",\"controllers\":"<<controllers;
        auto array=[&](const char* key,const auto& list){out<<",\""<<key<<"\":[";bool first=true;for(const auto& item:list){if(!first)out<<',';first=false;out<<item;}out<<']';};
        array("cases",cases);array("captures",captures);out<<"}\n";require(bool(out),"Cannot write acceptance report");
    }
};
struct PadWatch {
    std::mutex mutex;std::vector<SDL_Event> events;
    PadWatch(){require(SDL_AddEventWatch(callback,this),"Cannot watch real controller events");}
    ~PadWatch(){SDL_RemoveEventWatch(callback,this);}
    static bool SDLCALL callback(void* data,SDL_Event* event){
        if(event->type==SDL_EVENT_GAMEPAD_AXIS_MOTION||event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||event->type==SDL_EVENT_GAMEPAD_REMOVED||event->type==SDL_EVENT_GAMEPAD_ADDED){
            auto& self=*static_cast<PadWatch*>(data);std::lock_guard lock(self.mutex);if(self.events.size()<4096)self.events.push_back(*event);
        }return true;
    }
    std::vector<SDL_Event> drain(){std::lock_guard lock(mutex);std::vector<SDL_Event> result;result.swap(events);return result;}
};
void run(Report& report,int controllerSeconds,bool interactive) {
    DesktopSettings settings;settings.title="WebEngine Windows acceptance gallery";settings.width=960;settings.height=640;
    settings.hidden=true;settings.allowGraphicsFaultInjection=true;settings.quality.directionalCascades=2;settings.quality.shadowDistance=45;
    settings.quality.resolution.maxPixelRatio=4;
    DesktopHost host(settings);auto gallery=acceptance::makeGallery(FbxImporter::loadFile(std::filesystem::path(DESKTOP_FIXTURE_DIR)/"skeletal_column_binary.fbx"));
    auto& camera=*gallery.camera;const auto sceneId=host.application().loadScene(std::move(gallery.scene));
    const DesktopView view{camera.id()};host.setViews(sceneId,{&view,1});host.application().start(sceneId);host.setPaused(true);
    double time=0;
    auto step=[&](double delta=0.){time+=delta;require(host.step({},time),"Host unexpectedly closed");require(host.captureError().empty(),"GPU readback failed");};
    auto wait=[&](auto condition,const char* message){const double deadline=host.nowSeconds()+20;while(!condition()){require(host.nowSeconds()<deadline,message);step();SDL_Delay(2);}};
    wait([&]{return host.diagnostics().ready;},"Dawn initialization timeout");
    auto capture=[&](const char* name){step();host.captureFrame(report.directory/(std::string(name)+".ppm"));
        const auto deadline=host.nowSeconds()+20;
        while(host.capturePending()){
            if(host.nowSeconds()>=deadline){std::cerr<<"Capture "<<name<<" suspended="<<host.application().suspended()<<" frames="<<host.diagnostics().presentedFrames<<" state="<<host.diagnostics().renderState<<std::endl;throw std::runtime_error("Framebuffer capture timeout");}
            step();SDL_Delay(2);
        }
        const auto d=host.diagnostics();report.captures.push_back("{\"name\":"+quote(name)+",\"width\":"+std::to_string(d.width)+",\"height\":"+std::to_string(d.height)+",\"renderState\":"+d.renderState+",\"performance\":"+d.performance+"}");};
    require(gallery.animated->skinPose()->bones.size()>=3,"Missing skeletal palette");require(gallery.particles->particles().size()>20,"Particles did not warm up");
    capture("all-on-1x");
    const auto pose=gallery.animated->skinPose()->bones;
    gallery.animated->animation().play(1);gallery.animated->animation().seek(1);gallery.animated->animation().pause();
    bool changedPose=false;const auto changed=gallery.animated->skinPose();
    for(std::size_t i=0;i<pose.size();++i)changedPose|=pose[i].transform.m!=changed->bones[i].transform.m;
    require(changedPose,"Animation did not change skin palette");capture("animation-right");
    gallery.animated->animation().play(0);gallery.animated->animation().seek(1);gallery.animated->animation().pause();
    gallery.terrain->enabled=false;gallery.flatTerrain->enabled=true;capture("terrain-flat");gallery.terrain->enabled=true;gallery.flatTerrain->enabled=false;
    gallery.water->enabled=false;capture("water-off");gallery.water->enabled=true;
    gallery.particles->enabled=false;capture("particles-off");gallery.particles->enabled=true;
    gallery.sun->enabled=false;gallery.spot->enabled=false;capture("lights-off");gallery.sun->enabled=true;gallery.spot->enabled=true;
    host.setShadowsEnabled(false);capture("shadows-off");host.setShadowsEnabled(true);
    capture("water-time-0");for(int i=0;i<90;++i)step(1./60);capture("water-time-1");
    // Same clocks, camera and content for post/AA controls. No CPU image substitute.
    auto quality=host.quality();quality.post.enabled=true;quality.post.exposure=.7f;quality.post.bloomStrength=.45f;quality.post.bloomThreshold=.7f;quality.post.saturation=.6f;
    host.setQuality(quality);capture("all-on-post-1x");require(host.capabilities().hdrPostProcessing,"Dawn did not expose HDR post");
    quality.post.bloomStrength=0;host.setQuality(quality);capture("post-no-bloom-1x");quality.post.bloomStrength=.45f;
    quality.msaaSamples=4;host.setQuality(quality);capture("all-on-post-4x");require(host.diagnostics().effectiveMsaaSamples==4,"4x MSAA was not active");
    gallery.camera->transform.position={4,-.1f,8};gallery.camera->lookAt({4,-.2f,0});capture("underwater-post-4x");
    MaterialInstance underwaterControl;auto waterSettings=gallery.water->resolvedMaterial(0).water;waterSettings.underwaterStrength=0;underwaterControl.water=waterSettings;
    gallery.water->setMaterial(0,underwaterControl);capture("underwater-effects-off");gallery.water->clearMaterial(0);
    gallery.camera->transform.position={13,10,17};gallery.camera->lookAt({0,1,0});
    report.result("WIN-ACC-001","captured","Skinned FBX, chunked terrain, reflective/refractive water and underwater, seeded soft particles, PBR/cascaded sun/spot shadows, HDR post and 1x/4x MSAA; pixel assertions run separately");

    auto* window=onlyWindow();auto hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    auto focus=[&]{require(SDL_ShowWindow(window),"Cannot show acceptance window");SDL_RaiseWindow(window);SetForegroundWindow(hwnd);};
    focus();wait([&]{return !host.application().suspended();},"Acceptance window did not regain focus");
    const auto simulation=host.application().clocks().simulation.seconds;
    host.setWindowSize(800,600);wait([&]{int w=0,h=0;SDL_GetWindowSizeInPixels(window,&w,&h);return host.diagnostics().width==unsigned(w)&&host.diagnostics().height==unsigned(h)&&w==800&&h==600;},"Resize did not reach swapchain");capture("resized-800x600");
    host.setFullscreen(true);require(SDL_SyncWindow(window),"Fullscreen synchronization failed");step();require(host.fullscreen(),"Fullscreen flag missing");
    int fw=0,fh=0;SDL_GetWindowSizeInPixels(window,&fw,&fh);const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));require(mode&&fw==mode->w&&fh==mode->h,"Fullscreen did not cover actual display");capture("fullscreen");
    host.setFullscreen(false);require(SDL_SyncWindow(window),"Windowed synchronization failed");step();require(!host.fullscreen(),"Fullscreen exit failed");host.setWindowSize(960,640);step();
    require(SDL_MinimizeWindow(window),"Cannot minimize");wait([&]{return host.application().suspended()&&(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED);},"Minimize did not suspend");
    for(int i=0;i<5;++i)step(1);require(host.application().clocks().simulation.seconds==simulation,"Minimized simulation advanced");
    require(SDL_RestoreWindow(window),"Cannot restore");focus();wait([&]{return !host.application().suspended();},"Restore did not resume host");
    require(host.application().state()==ApplicationState::Paused,"Restore lost explicit pause");capture("restored");
    host.setPaused(false);step(1./60);step(1./60);const auto runningSimulation=host.application().clocks().simulation.seconds;
    require(SDL_MinimizeWindow(window),"Cannot minimize running application");wait([&]{return host.application().suspended();},"Running minimize did not suspend");
    for(int i=0;i<3;++i)step(10);require(host.application().clocks().simulation.seconds==runningSimulation,"Suspended running scene advanced");
    require(SDL_RestoreWindow(window),"Cannot restore running application");focus();wait([&]{return !host.application().suspended();},"Running restore did not resume");
    step(10);require(host.application().state()==ApplicationState::Running&&host.application().clocks().simulation.delta<=.05,"Restore lost running state or accumulated unbounded time");host.setPaused(true);
    // Real Win32 route, injected notifications. This does NOT put the computer to sleep.
    SendMessageW(hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);step(1);require(host.application().suspended(),"Power suspend was ignored");
    SendMessageW(hwnd,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);step(30);require(!host.application().suspended()&&host.application().state()==ApplicationState::Paused,"Power resume lost pause");
    host.setPaused(false);step(30);require(host.application().clocks().simulation.delta<=.05,"Power resume accumulated unbounded simulation time");host.setPaused(true);
    report.result("WIN-ACC-002","passed","Actual resize, borderless fullscreen/exit, minimize/restore; injected Win32 power notifications preserve explicit pause and reset timing");
    report.result("system-sleep","not-run","OS sleep/hibernate and wake require a separate operator session; power message routing alone is not S3/modern-standby acceptance");

    int count=0;auto* displays=SDL_GetDisplays(&count);require(displays&&count>0,"No display inventory");
    std::ostringstream inventory;inventory<<'[';float firstScale=0;bool differentDpi=false;
    for(int i=0;i<count;++i){SDL_Rect bounds{};require(SDL_GetDisplayBounds(displays[i],&bounds),"Display bounds unavailable");const float scale=SDL_GetDisplayContentScale(displays[i]);if(i)inventory<<',';
        inventory<<"{\"name\":"<<quote(safe(SDL_GetDisplayName(displays[i])))<<",\"scale\":"<<scale<<",\"width\":"<<bounds.w<<",\"height\":"<<bounds.h<<'}';
        if(i==0)firstScale=scale;else if(std::abs(firstScale-scale)>.01f)differentDpi=true;
        require(SDL_SetWindowPosition(window,bounds.x+40,bounds.y+60),"Cannot move window to display");require(SDL_SyncWindow(window),"Display move failed");
        wait([&]{return SDL_GetDisplayForWindow(window)==displays[i];},"Window did not move to display");step();
        const auto size=host.size();int pw=0,ph=0;SDL_GetWindowSizeInPixels(window,&pw,&ph);
        require(std::abs(size.width*size.pixelRatio-pw)<2&&std::abs(size.height*size.pixelRatio-ph)<2,"DPI logical/backing extent mismatch");
        require(std::abs(size.pixelRatio-SDL_GetWindowDisplayScale(window))<.01f,"DPI content scale lost");
    }inventory<<']';report.displays=inventory.str();SDL_free(displays);
    require(desktopCoordinateScale(1.5f,1)==1.5f&&desktopCoordinateScale(2,2)==1&&desktopCoordinateScale(0,1)==1,"Coordinate scale contract failed");
    report.result("WIN-ACC-003-dpi",differentDpi?"passed":"not-run",differentDpi?"Actual window moved across differently scaled displays; logical/backing extents checked":"Display inventory and current scale checked; no differently scaled displays available. Pure coordinate conversion checks cover 100/150/200%, not a physical DPI transition");

    // A real device.Destroy() triggers Dawn's lost callback. Rebuilds all GPU-owned
    // resources using surviving shared CPU scene assets, including water/HDR/MSAA.
    capture("before-recovery");const auto before=host.diagnostics();const auto frozen=host.application().clocks().simulation.seconds;
    bool callbackRejected=false;ApplicationFrameCallbacks nested;nested.present=[&](Scene&,const ApplicationFrame&){try{host.simulateGraphicsLoss();}catch(const std::logic_error&){callbackRejected=true;}};
    require(host.step(nested,time),"Reentrant fault test closed host");require(callbackRejected,"Fault injection accepted in frame callback");
    require(host.simulateGraphicsLoss(),"Fault injection was not accepted");bool sawRecovering=false;
    wait([&]{const auto d=host.diagnostics();sawRecovering|=d.renderState.find("recovering")!=std::string::npos;return sawRecovering&&d.ready&&d.renderState!=before.renderState;},"Dawn device loss did not recover");
    require(host.application().activeSceneId()==sceneId&&host.application().state()==ApplicationState::Paused&&host.application().clocks().simulation.seconds==frozen,"GPU recovery mutated scene/pause/simulation");
    capture("after-recovery");require(host.diagnostics().effectiveMsaaSamples==4&&host.quality().post.enabled,"Recovery lost HDR/MSAA quality");
    report.result("WIN-ACC-004","passed","Induced Dawn device destruction -> lost callback -> new generation -> rendered paused scene with HDR/4x MSAA, water, skinning and particles");
    report.result("driver-reset","not-run","Induced device loss is not a physical driver reset, adapter removal or TDR");

    auto next=std::make_unique<Scene>("Post-gallery transition");auto& nextCamera=next->spawn("Camera").addComponent<CameraComponent>();
    nextCamera.transform.position={0,0,5};nextCamera.lookAt({0,0,0});acceptance::object(*next,"Cube",Mesh::cube(),{0,0,0},{1,1,1},acceptance::material({.1f,.7f,.9f}));
    const auto nextId=host.application().loadScene(std::move(next));const DesktopView nextView{nextCamera.id()};host.setViews(nextId,{&nextView,1});
    if(!interactive){host.application().requestTransition(nextId,true);step();require(!host.application().findScene(sceneId),"Gallery resources retained after unload");capture("scene-transition");report.result("scene-transition","passed","Scene/camera binding and CPU/GPU resource replacement after recovery");}
    else host.application().unloadScene(nextId);

    std::set<SDL_JoystickID> hardware;int pads=0;auto* ids=SDL_GetGamepads(&pads);std::ostringstream padInventory;padInventory<<'[';
    bool first=true;for(int i=0;i<pads;++i)if(!SDL_IsJoystickVirtual(ids[i])){hardware.insert(ids[i]);if(!first)padInventory<<',';first=false;padInventory<<"{\"name\":"<<quote(safe(SDL_GetGamepadNameForID(ids[i])))<<",\"vendor\":"<<SDL_GetGamepadVendorForID(ids[i])<<",\"product\":"<<SDL_GetGamepadProductForID(ids[i])<<'}';}padInventory<<']';report.controllers=padInventory.str();SDL_free(ids);
    if(controllerSeconds>0){
        std::cout<<"Physical controller check: neutralize sticks/triggers, move left stick and right trigger, press A, disconnect/reconnect. Virtual devices do not count."<<std::endl;
        const InputBinding mapping[]={{1,controlId(DesktopControl::PadLeftX),100,{1,0}},{1,controlId(DesktopControl::PadRightTrigger),101,{1,0}}};
        host.bindings().setBindings(mapping);const InputContext context{1,true};host.bindings().setContexts({&context,1});host.setPaused(false);focus();
        PadWatch watch;bool axis=false,button=false,disconnect=false,reconnect=false,mapped=false,cancelled=false;std::ofstream trace(report.directory/"physical-controller.jsonl");
        const double end=host.nowSeconds()+controllerSeconds;
        while(host.nowSeconds()<end){step(1./60);for(const auto& e:watch.drain()){
            auto id=(e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION?e.gaxis.which:e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN?e.gbutton.which:e.gdevice.which);
            if(e.type==SDL_EVENT_GAMEPAD_ADDED&&!SDL_IsJoystickVirtual(id)){hardware.insert(id);reconnect|=disconnect;}
            if(!hardware.contains(id))continue;axis|=e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&std::abs(e.gaxis.value)>8000;button|=e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            if(e.type==SDL_EVENT_GAMEPAD_REMOVED){disconnect=true;cancelled=!host.application().input().action(100).held&&!host.application().input().action(101).held;}
            trace<<"{\"sdlTimestampNS\":"<<e.common.timestamp<<",\"type\":"<<e.type<<",\"device\":"<<id<<"}\n";
        }mapped|=host.application().input().action(100).held&&host.application().input().action(101).held;SDL_Delay(8);}
        report.result("WIN-ACC-003-controller",axis&&button&&disconnect&&reconnect&&mapped&&cancelled?"passed":"incomplete","Physical-only event capture; requires axes+button+disconnect+reconnect, mapped input and cancellation; inspect physical-controller.jsonl");host.setPaused(true);
    }else report.result("WIN-ACC-003-controller","not-run","No physical controller supplied; inventory recorded. Virtual-pad regressions remain separate evidence");
    if(interactive){
        host.setEventHandler([&](const DesktopEvent& e){if(e.type!=DesktopEventType::Control||!e.down||e.guiConsumed)return false;
            if(e.control==DesktopControl::Escape){host.requestClose();return true;}if(e.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}return false;});
        host.setPaused(false);gallery.animated->animation().resume();gallery.particles->resume();host.application().resetClocks();report.write();std::cout<<"Interactive gallery: Escape exits, F11 toggles fullscreen"<<std::endl;host.run();
    }
}
}
int main(int argc,char** argv){
    Report report;report.directory="windows-acceptance";int seconds=0;bool interactive=false;
    try{for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--evidence"&&i+1<argc)report.directory=argv[++i];else if(arg=="--controller-seconds"&&i+1<argc){seconds=std::stoi(argv[++i]);require(seconds>0&&seconds<=600,"Controller duration must be 1..600 seconds");}else if(arg=="--interactive")interactive=true;else throw std::invalid_argument("Unknown acceptance argument");}
        std::filesystem::create_directories(report.directory);require(!std::filesystem::exists(report.directory/"native-report.json"),"Use a fresh evidence directory to avoid stale evidence");
        run(report,seconds,interactive);report.write();std::cout<<"Native acceptance completed\n";
    }catch(const std::exception& error){report.failure=error.what();std::cerr<<"Acceptance failed: "<<error.what()<<'\n';if(std::filesystem::exists(report.directory)&&!std::filesystem::exists(report.directory/"native-report.json"))report.write();return 1;}
}
