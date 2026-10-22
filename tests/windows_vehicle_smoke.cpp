#include "../examples/windows_vehicle_app.hpp"
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
using namespace engine;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SDL_WindowID windowId() {int count=0;auto** windows=SDL_GetWindows(&count);require(count==1,"Expected one desktop window");const auto id=SDL_GetWindowID(windows[0]);SDL_free(windows);return id;}
void key(SDL_Scancode code,bool down=true,bool shift=false) {
    SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;event.key.scancode=code;event.key.down=down;
    event.key.mod=shift?SDL_KMOD_SHIFT:SDL_KMOD_NONE;event.key.windowID=windowId();require(SDL_PushEvent(&event),"Cannot push SDL key");
}
void windowEvent(Uint32 type){SDL_Event event{};event.type=type;event.window.windowID=windowId();require(SDL_PushEvent(&event),"Cannot push window event");}
void text(const char* value) {
    SDL_Event event{};event.type=SDL_EVENT_TEXT_INPUT;event.text.windowID=windowId();event.text.text=value;
    require(SDL_PushEvent(&event),"Cannot push SDL text");
}
struct VirtualPad {
    SDL_JoystickID id=0;SDL_Joystick* joystick=nullptr;
    VirtualPad() {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1"); // The acceptance window is hidden.
        SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.name="WebEngine acceptance controller";
        desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
        id=SDL_AttachVirtualJoystick(&desc);require(id!=0&&SDL_IsGamepad(id),"Cannot attach virtual gamepad");
        joystick=SDL_OpenJoystick(id);require(joystick!=nullptr,"Cannot open virtual joystick");
        SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
        SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
    }
    ~VirtualPad(){if(joystick)SDL_CloseJoystick(joystick);if(id)SDL_DetachVirtualJoystick(id);}
    void button(SDL_GamepadButton button,bool down){require(SDL_SetJoystickVirtualButton(joystick,button,down),"Cannot set virtual button");}
};
void mouse(GuiRect bounds) {
    const float x=bounds.x+bounds.width/2,y=bounds.y+bounds.height/2;
    for(const auto type:{SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_EVENT_MOUSE_BUTTON_UP}) {
        SDL_Event event{};event.type=type;event.button.windowID=windowId();event.button.button=SDL_BUTTON_LEFT;event.button.x=x;event.button.y=y;event.button.down=type==SDL_EVENT_MOUSE_BUTTON_DOWN;
        require(SDL_PushEvent(&event),"Cannot push SDL mouse");
    }
}
void run(const std::filesystem::path& evidence,const std::filesystem::path& model) {
    std::filesystem::create_directories(evidence);DesktopSettings settings;settings.hidden=true;
    DesktopHost host(settings);auto paths=vehicle_example::vehicleFileSettings();paths.userDirectory=evidence/"user";
    vehicle_example::VehicleDesktopDemo demo(host,model,paths);double time=0;
    auto step=[&]{require(host.step(demo.callbacks(),time+=1./60),"Desktop closed during smoke");require(host.captureError().empty(),"Framebuffer readback failed");};
    const auto started=host.nowSeconds();while(!host.diagnostics().ready){require(host.nowSeconds()-started<15,"Graphics initialization timeout");step();}
    if(!model.empty()){
        const auto modelStarted=host.nowSeconds();
        while(demo.modelLoadProgress().status==AssetStatus::Loading||demo.modelLoadProgress().status==AssetStatus::Queued){require(host.nowSeconds()-modelStarted<15,"Background GLB load timeout");step();}
        step();require(demo.modelLoadProgress().status==AssetStatus::Ready&&host.application().findScene(demo.sceneId())->findActor(demo.previewId()),"Background GLB preview not adopted");
    }
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    key(SDL_SCANCODE_LALT);key(SDL_SCANCODE_LALT,false); // Unmapped key releases are harmless.
    for(int i=0;i<10;++i)step();
    key(SDL_SCANCODE_W);float peak=0,steer=0;bool contact=false;float brakingStart=0,brakingEnd=0;
    for(int frame=0;frame<280;++frame) {
        if(frame==50)host.captureFrame(evidence/"driving.ppm");
        if(frame==80)key(SDL_SCANCODE_D);
        if(frame==115)key(SDL_SCANCODE_D,false);
        if(frame==140){key(SDL_SCANCODE_W,false);key(SDL_SCANCODE_SPACE);brakingStart=std::abs(demo.vehicle().state().speed);}
        if(frame==260){brakingEnd=std::abs(demo.vehicle().state().speed);key(SDL_SCANCODE_SPACE,false);}
        step();peak=std::max(peak,std::abs(demo.vehicle().state().speed));steer=std::max(steer,std::abs(demo.vehicle().state().wheels[0].steerAngle));
        for(const auto& wheel:demo.vehicle().state().wheels)contact|=wheel.contact;
    }
    require(contact&&peak>2&&steer>.05f,"Vehicle contact/driving/steering failed");require(brakingStart>1&&brakingEnd<brakingStart*.5f,"Braking failed");
    // Programmatic text focus is also an ownership boundary, even without a pause menu.
    auto& probe=host.gui().root().add<TextInput>();probe.setPosition({600,16});probe.setSize({240,38});
    key(SDL_SCANCODE_W);step();require(demo.vehicle().command().throttle>0,"Probe throttle did not start");
    host.gui().layout({host.size().width,host.size().height});host.gui().focusWidget(probe.id());step();
    require(demo.vehicle().command().throttle==0,"Programmatic textbox focus retained gameplay input");
    text("Focused");step();require(probe.text()=="Focused","Unpaused text focus did not accept input");
    host.gui().blur();probe.setVisible(false);step();require(demo.vehicle().command().throttle==0,"Textbox blur reactivated held input");key(SDL_SCANCODE_W,false);step();
    host.setWindowSize(960,540);for(int i=0;i<8;++i)step();host.captureFrame(evidence/"resized.ppm");
    while(host.capturePending())step();
    // Open with a real SDL mouse event while driving, then navigate the real menu.
    key(SDL_SCANCODE_W);step();mouse(demo.pauseButton().bounds());step();
    require(demo.menuOpen()&&host.application().state()==ApplicationState::Paused,"Mouse pause button failed");
    require(demo.vehicle().command().throttle==0,"Menu retained held throttle");
    const auto frozen=host.application().clocks().simulation.seconds;
    // Focus starts on Resume; backwards traversal visits Quit, Reset, Defaults, Fullscreen,
    // Resolution, MSAA, Shadows. Pointer actions also exercise widget hit testing.
    mouse(demo.shadowSetting().bounds());step();require(!host.shadowsEnabled(),"Shadow checkbox did not apply");
    mouse(demo.msaaSetting().bounds());step();require(host.quality().msaaSamples==4,"MSAA checkbox did not apply");
    if(host.capabilities().msaa4x)require(host.diagnostics().effectiveMsaaSamples==4,"Native MSAA did not resolve");
    mouse(demo.resolutionSetting().bounds());step();key(SDL_SCANCODE_LEFT);step();
    require(host.quality().resolution.scale<1&&host.quality().resolution.scale>=.5f,"Keyboard slider did not apply");
    for(int i=0;i<4;++i)step();require(host.application().clocks().simulation.seconds==frozen,"Menu advanced vehicle simulation");
    host.captureFrame(evidence/"menu.ppm");while(host.capturePending())step();
    // Reach the shared textbox through keyboard focus, then click its revealed geometry.
    key(SDL_SCANCODE_TAB);step();key(SDL_SCANCODE_TAB);step();key(SDL_SCANCODE_TAB);step();
    require(host.gui().focused()==&demo.driverName(),"Keyboard traversal did not reveal driver textbox");
    mouse(demo.driverName().bounds());step();
    SDL_Event select{};select.type=SDL_EVENT_KEY_DOWN;select.key.windowID=windowId();select.key.scancode=SDL_SCANCODE_A;select.key.down=true;select.key.mod=SDL_KMOD_CTRL;
    require(SDL_PushEvent(&select),"Cannot select driver name");step();
    key(SDL_SCANCODE_P);step();text("\xc3\x85sa Driver");step();
    require(demo.menuOpen()&&demo.driverName().text()=="\xc3\x85sa Driver"&&demo.vehicle().command().throttle==0,"Text entry escaped into menu/driving shortcuts");
    host.captureFrame(evidence/"menu-text.ppm");while(host.capturePending())step();
    key(SDL_SCANCODE_RETURN);step();require(host.gui().focused()==&demo.resumeButton(),"Textbox submit did not return focus to Resume");
    // Smaller viewport keeps fixed Resume/Reset/Quit actions visible; wheel scrolls settings.
    host.setWindowSize(640,360);step();step();
    const auto settingsBounds=demo.settingsPanel().bounds();SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.windowID=windowId();
    wheel.wheel.mouse_x=settingsBounds.x+20;wheel.wheel.mouse_y=settingsBounds.y+20;wheel.wheel.y=-4;require(SDL_PushEvent(&wheel),"Cannot push wheel");step();
    require(demo.settingsPanel().scrollOffset()>0,"Settings wheel did not scroll");
    const auto resumeBounds=demo.resumeButton().bounds();require(resumeBounds.y>=0&&resumeBounds.y+resumeBounds.height<=360,"Fixed resume action clipped");
    host.captureFrame(evidence/"menu-small.ppm");while(host.capturePending())step();
    key(SDL_SCANCODE_ESCAPE);step();require(!demo.menuOpen(),"Escape did not close menu");
    for(int i=0;i<5;++i)step();require(demo.vehicle().command().throttle==0,"Resume reactivated held keyboard input");
    require(host.quality().msaaSamples==4&&!host.shadowsEnabled(),"Resume lost settings");
    key(SDL_SCANCODE_P);step();key(SDL_SCANCODE_RETURN);step();
    require(!demo.menuOpen()&&host.application().state()==ApplicationState::Running,"Keyboard Resume activation failed");
    // Cancel a slider drag on window blur, preserving explicit pause on refocus.
    key(SDL_SCANCODE_P);step();
    host.gui().focusWidget(demo.resolutionSetting().id());step();
    const auto sliderBounds=demo.resolutionSetting().bounds();SDL_Event down{};down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;down.button.windowID=windowId();
    down.button.button=SDL_BUTTON_LEFT;down.button.down=true;down.button.x=sliderBounds.x+sliderBounds.width/2;down.button.y=sliderBounds.y+sliderBounds.height/2;
    require(SDL_PushEvent(&down),"Cannot start slider drag");step();require(host.gui().capturesPointer(),"Slider did not capture pointer");
    windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST);step();const auto suspended=host.application().clocks().simulation.seconds;
    require(!host.gui().capturesPointer(),"Focus loss retained slider capture");
    for(int i=0;i<3;++i)step();require(host.application().clocks().simulation.seconds==suspended&&demo.vehicle().command().throttle==0,"Focus suspension failed");
    windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED);step();require(host.application().state()==ApplicationState::Paused,"Focus restore cleared explicit pause");
    // Real SDL virtual device exercises discovery, mapped button events, focus and neutral gate.
    VirtualPad pad;step();step();
    require(SDL_GetGamepadFromID(pad.id)!=nullptr,"Host did not discover the virtual gamepad");
    auto padPress=[&](SDL_GamepadButton button){pad.button(button,true);step();pad.button(button,false);step();};
    padPress(SDL_GAMEPAD_BUTTON_EAST);require(!demo.menuOpen(),"Controller B did not close menu");
    padPress(SDL_GAMEPAD_BUTTON_START);require(demo.menuOpen()&&host.gui().focused()==&demo.resumeButton(),"Controller Start did not open menu");
    padPress(SDL_GAMEPAD_BUTTON_DPAD_DOWN);require(host.gui().focused()==&demo.resetButton(),"Controller focus traversal failed");
    padPress(SDL_GAMEPAD_BUTTON_DPAD_UP);padPress(SDL_GAMEPAD_BUTTON_SOUTH);require(!demo.menuOpen(),"Controller A did not activate Resume");
    padPress(SDL_GAMEPAD_BUTTON_START);host.gui().focusWidget(demo.resolutionSetting().id());step();
    const auto oldScale=demo.resolutionSetting().value();padPress(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    require(demo.resolutionSetting().value()<oldScale||oldScale==.5f,"Controller D-pad did not adjust slider");
    // A held throttle must stay cancelled through a menu handoff until released.
    padPress(SDL_GAMEPAD_BUTTON_EAST);
    SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767);step();step();
    require(demo.vehicle().command().throttle>.9f,"Controller throttle failed");
    padPress(SDL_GAMEPAD_BUTTON_START);padPress(SDL_GAMEPAD_BUTTON_EAST);step();
    require(demo.vehicle().command().throttle==0,"Menu handoff rearmed held controller throttle");
    SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);step();step();
    padPress(SDL_GAMEPAD_BUTTON_START);
    // Restore original quality and window before comparing the reset scene.
    mouse(demo.resetButton().bounds());step();
    require(demo.menuOpen()&&std::abs(demo.vehicle().owner()->transform.position.z)<.1f,"Reset menu action failed");
    host.setQuality(RenderQuality{});host.setShadowsEnabled(true);host.setWindowSize(960,540);demo.setMenu(false);
    for(int i=0;i<35;++i)step();host.captureFrame(evidence/"reset.ppm");while(host.capturePending())step();
    const auto position=demo.vehicle().owner()->transform.position;
    require(std::abs(position.x)<.1f&&std::abs(position.z)<.1f&&std::abs(demo.vehicle().state().speed)<.1f,"Reset failed");
    const auto previous=demo.previewId();demo.loadModel(evidence/"missing.glb");
    const auto importStart=host.nowSeconds();
    while(demo.modelLoadProgress().status!=AssetStatus::Failed){require(host.nowSeconds()-importStart<10,"Background model failure timeout");step();}
    step();require(demo.previewId()==previous,"Failed model import replaced preview");
    demo.loadModel(model.empty()?evidence/"missing.glb":model);demo.setMenu(true);
    require(host.gui().activateWidget(demo.cancelModelButton().id()),"Cancel model GUI action failed");step();
    require(demo.modelLoadProgress().status==AssetStatus::Cancelled&&demo.previewId()==previous,"Cancelled import changed existing preview");demo.setMenu(false);
    const auto d=host.diagnostics();
    key(SDL_SCANCODE_P);step();key(SDL_SCANCODE_TAB);step();key(SDL_SCANCODE_TAB);step();
    require(host.gui().focused()==&demo.quitButton(),"Tab navigation did not reach Quit");key(SDL_SCANCODE_RETURN);
    require(!host.step(demo.callbacks(),time+=1./60)&&host.application().state()==ApplicationState::Stopped,"Quit menu did not safely shut down");
    std::ofstream report(evidence/"report.json");report.exceptions(std::ios::badbit|std::ios::failbit);
    report<<"{\"passed\":true,\"backend\":\"Dawn D3D12\",\"hiddenWindow\":true,\"frames\":"<<d.presentedFrames<<",\"peakSpeed\":"<<peak<<",\"steerAngle\":"<<steer
        <<",\"brakingStart\":"<<brakingStart<<",\"brakingEnd\":"<<brakingEnd<<",\"wheelContact\":true,\"focusSuspension\":true,\"pause\":true,\"reset\":true,\"invalidImportPreserved\":true,\"interactiveMenu\":true,\"nativeTextEntry\":true,\"virtualControllerNavigation\":true,\"controllerNeutralRearm\":true,\"keyboardResumeAndQuit\":true,\"dragCancelledOnBlur\":true,\"menuInputCancelled\":true,\"settingsApplied\":true,\"smallMenuScrolled\":true,\"modelLoaded\":"<<(!model.empty()?"true":"false")
        <<",\"width\":"<<d.width<<",\"height\":"<<d.height<<",\"renderState\":"<<d.renderState<<",\"performance\":"<<d.performance<<"}\n";
    std::cout<<"Desktop vehicle, input and menu smoke passed: "<<evidence<<std::endl;
}
}
int main(int argc,char** argv){try{std::filesystem::path evidence="build/evidence/windows-desktop",model;for(int i=1;i<argc;++i){const std::string key=argv[i];if((key=="--smoke-dir"||key=="--model")&&i+1<argc)(key=="--model"?model:evidence)=std::filesystem::u8path(argv[++i]);else throw std::invalid_argument("Unknown smoke argument");}run(evidence,model);}catch(const std::exception& error){std::cerr<<"Desktop smoke failed: "<<error.what()<<std::endl;return 1;}}
