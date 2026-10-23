#include "../examples/windows_vehicle_app.hpp"
#include <SDL3/SDL.h>
#include <Windows.h>
#include <fstream>
#include <iostream>
using namespace engine;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SDL_Window* window(){int n=0;auto** windows=SDL_GetWindows(&n);require(n==1,"Expected one SDL window");auto* result=windows[0];SDL_free(windows);return result;}
void key(SDL_Scancode code,bool down){SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.windowID=SDL_GetWindowID(window());e.key.scancode=code;e.key.down=down;require(SDL_PushEvent(&e),"Cannot inject native key");}
void click(GuiRect bounds){const float units=SDL_GetWindowDisplayScale(window())/SDL_GetWindowPixelDensity(window());for(bool down:{true,false}){SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;e.button.windowID=SDL_GetWindowID(window());e.button.down=down;e.button.button=SDL_BUTTON_LEFT;e.button.x=(bounds.x+bounds.width/2)*units;e.button.y=(bounds.y+bounds.height/2)*units;require(SDL_PushEvent(&e),"Cannot inject native mouse");}}
}
int main(int argc,char** argv){try{
    require(argc==3,"Usage: windows_backend_smoke auto|dawn|vulkan|d3d11|d3d12 evidence-directory");
    const std::string requested=argv[1];require(requested=="auto"||requested=="dawn"||requested=="vulkan"||requested=="d3d11"||requested=="d3d12","Unknown backend");
    const auto backend=requested=="auto"?DesktopBackend::Automatic:requested=="vulkan"?DesktopBackend::DawnVulkan:requested=="d3d11"?DesktopBackend::DawnD3D11:requested=="dawn"?DesktopBackend::Dawn:DesktopBackend::Direct3D12;
    const auto evidence=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(evidence);
    DesktopSettings settings;settings.hidden=true;settings.backend=backend;settings.width=1280;settings.height=720;
    DesktopHost host(settings);auto files=vehicle_example::vehicleFileSettings();files.userDirectory=evidence/"user";
    vehicle_example::VehicleDesktopDemo demo(host,{},files);double time=0;
    auto step=[&]{require(host.step(demo.callbacks(),time+=1./60),"Host stopped unexpectedly");require(host.captureError().empty(),host.captureError().c_str());};
    auto wait=[&](auto predicate){const auto end=host.nowSeconds()+60;while(!predicate()){require(host.nowSeconds()<end,"Native graphics acceptance timeout");step();SDL_Delay(2);}};
    wait([&]{return host.diagnostics().ready;});require(host.backend()==(backend==DesktopBackend::Automatic?DesktopBackend::Dawn:backend),"Selected backend was not retained");
    if(backend==DesktopBackend::Direct3D12){require(host.capabilities().backend==RenderBackend::Direct3D12,"Direct path reported WebGPU");require(host.capabilities().pbr&&host.capabilities().shadowMaps&&host.capabilities().spotLightShadows,"Static lighting milestone capabilities missing");}
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    key(SDL_SCANCODE_W,true);for(int i=0;i<120;++i)step();key(SDL_SCANCODE_W,false);step();
    const auto speed=std::abs(demo.vehicle().state().speed);require(speed>2,"Shared vehicle physics did not drive");
    auto capture=[&](const char* name){host.captureFrame(evidence/name);bool duplicateRejected=false;try{host.captureFrame(evidence/"duplicate.ppm");}catch(const std::logic_error&){duplicateRejected=true;}require(duplicateRejected,"A second capture replaced pending work");wait([&]{return !host.capturePending();});};
    capture("vehicle.ppm");host.setPaused(true);demo.setMenu(true);for(int i=0;i<4;++i)step();
    require(host.gui().focusWidget(demo.driverName().id()),"Native textbox cannot focus");step();demo.driverName().setSelection(0,demo.driverName().text().size());SDL_Event typed{};typed.type=SDL_EVENT_TEXT_INPUT;typed.text.windowID=SDL_GetWindowID(window());typed.text.text="D3D12 milestone";require(SDL_PushEvent(&typed),"Cannot inject native text");step();require(demo.driverName().text()=="D3D12 milestone","SDL text did not reach shared GUI");
    capture("menu.ppm");click(demo.resumeButton().bounds());step();require(!demo.menuOpen(),"Native mouse did not resume game");
    host.setWindowSize(960,540);for(int i=0;i<6;++i)step();capture("resized.ppm");require(host.diagnostics().width==960&&host.diagnostics().height==540,"Swapchain did not resize");
    auto quality=host.quality();quality.resolution.scale=.75f;host.setQuality(quality);step();capture("scaled.ppm");require(host.diagnostics().width==720&&host.diagnostics().height==405,"Resolution budget was ignored");quality.resolution.scale=1;host.setQuality(quality);
    // A visible native window exercises the actual borderless fullscreen transition.
    require(SDL_ShowWindow(window()),"Cannot show fullscreen acceptance window");for(int i=0;i<6;++i)step();host.setFullscreen(true);for(int i=0;i<8;++i)step();require(host.fullscreen(),"Fullscreen flag not set");
    auto* hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window()),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));require((GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CAPTION)==0,"Fullscreen retained window decorations");capture("fullscreen.ppm");const auto full=host.diagnostics();
    host.setFullscreen(false);for(int i=0;i<8;++i)step();require(!host.fullscreen(),"Fullscreen did not exit");capture("restored.ppm");
    host.setPaused(true);const auto pose=demo.vehicle().owner()->transform.matrix().m;for(int i=0;i<5;++i)step();require(demo.vehicle().owner()->transform.matrix().m==pose,"Presentation changed paused simulation");
    const auto diagnostics=host.diagnostics();host.captureFrame(evidence/"cancelled.ppm");host.requestClose();require(!host.step(demo.callbacks(),time+=1./60),"Shutdown did not stop host");require(!host.capturePending()&&!host.captureError().empty(),"Shutdown did not cancel pending capture");
    std::ofstream out(evidence/"report.json");out<<"{\"passed\":true,\"requested\":\""<<requested<<"\",\"nativeGuiText\":true,\"nativeGuiMouse\":true,\"resize\":true,\"resolutionBudget\":true,\"fullscreen\":true,\"borderless\":true,\"captureCancellation\":true,\"pausedSimulationPreserved\":true,\"speed\":"<<speed<<",\"fullscreenWidth\":"<<full.width<<",\"fullscreenHeight\":"<<full.height<<",\"frames\":"<<diagnostics.presentedFrames<<",\"renderState\":"<<diagnostics.renderState<<",\"performance\":"<<diagnostics.performance<<"}\n";
    require(bool(out),"Cannot write evidence report");std::cout<<requested<<" vehicle/GUI/resize/fullscreen/capture passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
