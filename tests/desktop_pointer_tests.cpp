#include "desktop/sdl_pointer_input.hpp"
#include "desktop/sdl_display.hpp"
#include "engine/desktop.hpp"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <iostream>
#include <limits>
using namespace engine;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void hostTest(DesktopBackend backend) {
    DesktopSettings settings;settings.hidden=true;settings.width=800;settings.height=600;settings.backend=backend;
    DesktopHost host(settings);auto scene=std::make_unique<Scene>("Contacts");auto& camera=scene->spawn("Camera").addComponent<CameraComponent>();
    const auto sceneId=host.application().loadScene(std::move(scene));const DesktopView view{camera.id()};host.setViews(sceneId,{&view,1});host.application().start(sceneId);
    const auto start=host.nowSeconds();while(!host.diagnostics().ready){require(host.nowSeconds()-start<20,"Host startup timeout");host.step();}host.step();
    int count=0;auto** windows=SDL_GetWindows(&count);require(count==1,"Host window");const auto id=SDL_GetWindowID(windows[0]);const auto units=desktopCoordinateScale(*windows[0]);SDL_free(windows);
    int clicks=0;auto& button=host.gui().root().add<Button>("Touch action");button.setSize({100,50});button.onClick([&]{++clicks;});host.step();
    auto push=[&](SDL_Event e){require(SDL_PushEvent(&e),"Push host event");};
    SDL_Event event{};event.type=SDL_EVENT_FINGER_DOWN;event.tfinger.windowID=id;event.tfinger.touchID=7;event.tfinger.fingerID=1;event.tfinger.x=.6f;event.tfinger.y=.6f;event.tfinger.pressure=.8f;push(event);
    event.tfinger.fingerID=2;event.tfinger.x=.8f;push(event);host.step();
    require(host.application().input().pointers().size()==2,"DesktopHost lost multitouch records");
    for(const auto& [key,p]:host.application().input().pointers())require(p.device==InputDevice::Touch&&std::abs(p.pressure-.8f)<.001,"Host touch metadata");
    host.cancelInput();host.step();require(host.application().input().pointers().empty(),"Host cancellation retained contacts");
    event.type=SDL_EVENT_FINGER_DOWN;event.tfinger.fingerID=3;event.tfinger.x=.03f;event.tfinger.y=.03f;push(event);host.step();
    event.type=SDL_EVENT_FINGER_UP;push(event);host.step();require(clicks==1,"Host touch GUI activation");
    event={};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.windowID=id;event.button.which=SDL_TOUCH_MOUSEID;event.button.button=SDL_BUTTON_LEFT;event.button.down=true;event.button.x=20;event.button.y=20;push(event);
    event.type=SDL_EVENT_MOUSE_BUTTON_UP;event.button.down=false;push(event);host.step();require(clicks==1,"Synthetic mouse double activation");
    bool reserve=true,handlePointer=false;int previews=0,notifications=0;bool guiConsumed=false;
    host.setEventHandler([&](const DesktopEvent& e){
        if(e.type==DesktopEventType::PointerPreview){++previews;return reserve;}
        if(e.type==DesktopEventType::Pointer){++notifications;guiConsumed=e.guiConsumed;return handlePointer;}
        return false;
    });
    auto mouse=[&](bool down,float x,float y){SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID=id;e.button.which=0;e.button.button=SDL_BUTTON_LEFT;e.button.down=down;e.button.x=x*units;e.button.y=y*units;push(e);host.step();};
    mouse(true,20,20);mouse(false,20,20);
    require(previews==2&&notifications==2&&guiConsumed&&clicks==1,"Reserved mouse contact reached GUI or lost notification ownership");
    reserve=false;mouse(true,20,20);mouse(false,20,20);require(clicks==2&&guiConsumed,"Unreserved mouse contact bypassed retained GUI");
    handlePointer=true;mouse(true,400,300);require(!guiConsumed&&host.application().input().pointers().empty(),"Handled post-GUI contact also reached gameplay");mouse(false,400,300);
    SDL_Event touch{};touch.type=SDL_EVENT_FINGER_DOWN;touch.tfinger.windowID=id;touch.tfinger.touchID=7;touch.tfinger.fingerID=4;touch.tfinger.x=.03f;touch.tfinger.y=.03f;push(touch);host.step();
    const auto beforePreview=previews;mouse(true,400,300);mouse(false,400,300);require(previews==beforePreview&&guiConsumed,"Mouse preview stole active touch GUI capture");
    touch.type=SDL_EVENT_FINGER_UP;push(touch);host.step();require(clicks==3,"Mouse notifications disrupted touch GUI capture");host.setEventHandler({});
    event={};event.type=SDL_EVENT_PEN_DOWN;event.ptouch.windowID=id;event.ptouch.which=5;event.ptouch.x=400*units;event.ptouch.y=300*units;event.ptouch.pen_state=SDL_PEN_INPUT_DOWN;push(event);host.step();
    require(host.application().input().pointers().size()==1&&host.application().input().pointers().begin()->second.device==InputDevice::Pen,"Host pen routing");
    host.setPaused(true);host.step();require(host.application().input().pointers().empty(),"Pause retained pen contact");
    host.requestClose();host.step();
}
int main(int argc,char** argv){try {
    SDL_SetMainReady();require(SDL_Init(SDL_INIT_VIDEO),"SDL init");
    auto* window=SDL_CreateWindow("Contact routing",800,600,SDL_WINDOW_HIDDEN);require(window,"Window");
    GuiSystem gui;int clicks=0;auto& button=gui.root().add<Button>("Apply");button.setSize({100,50});button.onClick([&]{++clicks;});gui.layout({800,600});
    std::vector<InputPacket> packets;SdlPointerInput input(gui,[&](auto batch){packets.insert(packets.end(),batch.begin(),batch.end());return true;});
    auto finger=[&](SDL_EventType type,Uint64 device,Uint64 id,float x,float y,float pressure=.7f,bool mouse=false){
        SDL_Event e{};e.type=type;e.tfinger.windowID=SDL_GetWindowID(window);e.tfinger.touchID=device;e.tfinger.fingerID=id;e.tfinger.x=x;e.tfinger.y=y;e.tfinger.pressure=pressure;
        input.event(e,*window,{800,600},{1600,1200},true,mouse);
    };
    finger(SDL_EVENT_FINGER_DOWN,0x100000001,0x200000001,.5f,.5f);const auto first=packets.back().id;
    finger(SDL_EVENT_FINGER_DOWN,0x200000001,0x200000001,.6f,.6f);const auto second=packets.back().id;
    require(first!=second&&first>1,"Distinct 64-bit SDL contacts collided");
    require(packets.front().pixelX==800&&packets.front().pixelY==600&&packets.front().device==1,"Touch DPI/device mapping");
    finger(SDL_EVENT_FINGER_CANCELED,0x100000001,0x200000001,.5f,.5f);
    require(packets.back().type==int(InputEventType::PointerCancel)&&packets.back().id==first,"Targeted cancellation");
    finger(SDL_EVENT_FINGER_MOTION,0x200000001,0x200000001,.7f,.7f);require(packets.back().id==second,"Other contact lost");
    input.cancel();packets.clear();
    finger(SDL_EVENT_FINGER_DOWN,1,1,.05f,.05f);require(input.ownsGui(),"GUI touch capture");
    finger(SDL_EVENT_FINGER_DOWN,1,2,.05f,.05f);finger(SDL_EVENT_FINGER_UP,1,2,.05f,.05f);
    require(clicks==0&&packets.empty(),"Second finger stole GUI capture");
    finger(SDL_EVENT_FINGER_UP,1,1,.05f,.05f);require(clicks==1,"Touch did not activate GUI");
    finger(SDL_EVENT_FINGER_DOWN,1,3,.05f,.05f,.7f,true);finger(SDL_EVENT_FINGER_UP,1,3,.05f,.05f,.7f,true);
    require(clicks==1&&packets.empty(),"Touch stole mouse GUI capture");
    finger(SDL_EVENT_FINGER_DOWN,1,4,.05f,.05f);input.cancel();finger(SDL_EVENT_FINGER_UP,1,4,.05f,.05f);
    require(clicks==1,"Cancelled GUI touch activated");
    button.onClick([&]{++clicks;input.cancel();});
    finger(SDL_EVENT_FINGER_DOWN,1,5,.05f,.05f);finger(SDL_EVENT_FINGER_UP,1,5,.05f,.05f);
    require(clicks==2&&!input.ownsGui(),"Reentrant lifecycle cancellation");
    SDL_Event pen{};pen.type=SDL_EVENT_PEN_AXIS;pen.paxis.windowID=SDL_GetWindowID(window);pen.paxis.which=9;
    pen.paxis.axis=SDL_PEN_AXIS_PRESSURE;pen.paxis.value=.65f;pen.paxis.x=400;pen.paxis.y=300;
    input.event(pen,*window,{800,600},{1600,1200},true,false);const auto penId=packets.back().id;
    require(packets.back().device==2&&std::abs(packets.back().pressure-.65)<.001,"Pen pressure/hover");
    pen={};pen.type=SDL_EVENT_PEN_DOWN;pen.ptouch.windowID=SDL_GetWindowID(window);pen.ptouch.which=9;pen.ptouch.x=400;pen.ptouch.y=300;
    pen.ptouch.pen_state=SDL_PEN_INPUT_DOWN|SDL_PEN_INPUT_BUTTON_1|SDL_PEN_INPUT_ERASER_TIP;
    input.event(pen,*window,{800,600},{1600,1200},true,false);
    require(packets.back().id==penId&&packets.back().buttons==35,"Pen button/eraser mapping or hover identity");
    require(std::abs(packets.back().x-400/desktopCoordinateScale(*window))<.01,"Pen DPI conversion");
    pen={};pen.type=SDL_EVENT_PEN_PROXIMITY_OUT;pen.pproximity.which=9;input.event(pen,*window,{800,600},{1600,1200},true,false);
    require(packets.back().type==int(InputEventType::PointerCancel),"Pen proximity loss");
    const auto count=packets.size();finger(SDL_EVENT_FINGER_DOWN,1,8,std::numeric_limits<float>::quiet_NaN(),0);
    require(packets.size()==count,"Malformed contact accepted");
    finger(SDL_EVENT_FINGER_DOWN,SDL_PEN_TOUCHID,8,.5f,.5f);require(packets.size()==count,"Emulated pen touch duplicated");
    input.cancel();SDL_DestroyWindow(window);SDL_Quit();
    hostTest(argc>1&&std::string(argv[1])=="d3d12"?DesktopBackend::Direct3D12:DesktopBackend::Dawn);
    std::cout<<"Desktop touch/pen identity, pressure, DPI, ownership, cancellation, reentrancy and real host routing passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
