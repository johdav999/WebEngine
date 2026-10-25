#include "host_controller.hpp"
#include "engine/desktop.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <SDL3/SDL.h>
#define NOMINMAX
#include <windows.h>

namespace {
LONG WINAPI smokeCrash(EXCEPTION_POINTERS* exception){
    auto address=exception->ExceptionRecord->ExceptionAddress;
    HMODULE module=nullptr;char path[MAX_PATH]{};
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address),&module);
    if(module)GetModuleFileNameA(module,path,MAX_PATH);
    std::cerr<<"Native exception 0x"<<std::hex<<exception->ExceptionRecord->ExceptionCode
        <<" module="<<path<<" offset=0x"<<(reinterpret_cast<std::uintptr_t>(address)-reinterpret_cast<std::uintptr_t>(module))<<std::endl;
    return EXCEPTION_EXECUTE_HANDLER;
}
}

int main(int argc,char**argv){try{
    using namespace engine;using namespace rpg_example;
    bool smoke=false;std::filesystem::path output="rpg-smoke";
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--smoke"&&i+1<argc){smoke=true;output=argv[++i];}else throw std::invalid_argument("Use --smoke output-directory");}
    const std::string isolated=smoke&&std::getenv("RPG_SMOKE_LAYER")?std::getenv("RPG_SMOKE_LAYER"):"";
    if(smoke){SetUnhandledExceptionFilter(smokeCrash);std::cerr<<"RPG smoke: constructing desktop host"<<std::endl;}
    DesktopSettings settings;settings.title="RPG - Training Court";settings.backend=DesktopBackend::Direct3D12;settings.hidden=smoke;settings.width=1100;settings.height=760;
    DesktopHost host(settings);if(smoke)std::cerr<<"RPG smoke: desktop host constructed"<<std::endl;DesktopFileSettings fs;fs.application="WebEngineRpgSample";if(smoke)fs.userDirectory=std::filesystem::absolute(output/"user");DesktopFiles files(fs);
    bool failWrite=false;Storage storage;
    storage.read=[&]()->std::optional<std::vector<std::uint8_t>>{auto path=files.userPath(DesktopStorage::Saves,"rpg.save");if(!std::filesystem::exists(path))return {};auto raw=readDesktopFile(path,r::snapshotByteLimit);auto*b=reinterpret_cast<const std::uint8_t*>(raw.data());return std::vector<std::uint8_t>(b,b+raw.size());};
    storage.write=[&](auto bytes){if(failWrite)throw std::runtime_error("Injected failed write; previous save retained");files.writeUser(DesktopStorage::Saves,"rpg.save",std::as_bytes(bytes),r::snapshotByteLimit);};
    Controller controller(host.application(),storage,[&](SceneId id,HostScene&scene){if(isolated=="gui")scene.cameraView().setVisibilityMask(0);DesktopView view{scene.camera};host.setViews(id,{&view,1});});
    struct Cleanup {DesktopHost&host;~Cleanup(){host.setEventHandler({});host.gui().clear();host.application().shutdown();}} cleanup{host};
    bool dragging=false;Vec2 previous{};
    host.setEventHandler([&](const DesktopEvent&e){
        if(e.type==DesktopEventType::InputCancelled){dragging=false;controller.cancelInput();}
        if(e.type==DesktopEventType::Pointer&&(e.pointer==GuiPointer::Up||e.pointer==GuiPointer::Cancel))dragging=false;
        if(e.guiConsumed)return false;
        if(e.type==DesktopEventType::Pointer){if(e.pointer==GuiPointer::Down){dragging=true;previous=e.position;}else if(e.pointer==GuiPointer::Up||e.pointer==GuiPointer::Cancel)dragging=false;else if(dragging){controller.orbit((e.position.x-previous.x)*.008f,(e.position.y-previous.y)*.006f);previous=e.position;}}
        if(e.type==DesktopEventType::Wheel)controller.orbit(0,0,-e.wheel.y*.5f);
        if(e.type==DesktopEventType::Control&&e.down&&!e.repeat){if(e.control==DesktopControl::M)controller.enqueue(Action::Move);else if(e.control==DesktopControl::A)controller.enqueue(Action::Strike);else if(e.control==DesktopControl::Space)controller.enqueue(Action::EndTurn);}
        return false;
    });
    if(smoke)std::cerr<<"RPG smoke: starting controller"<<std::endl;
    controller.start();host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"),16));
    if(smoke)std::cerr<<"RPG smoke: controller and font ready"<<std::endl;
    auto&panel=host.gui().root().add<Panel>();panel.setPosition({14,14});panel.setSize({280,710});panel.setPadding(10);panel.setLayout(VerticalLayout{4});
    if(isolated=="scene")panel.setVisible(false);
    auto&title=panel.add<TextLabel>("Training Court");title.setSize({260,28});auto&status=panel.add<TextLabel>();status.setSize({260,70});auto&message=panel.add<TextLabel>();message.setSize({260,44});
    for(int row=0;row<6;++row){auto&line=panel.add<Panel>();line.setSize({260,34});line.setLayout(HorizontalLayout{8});
        for(int column=0;column<2;++column){int i=row*2+column;auto&button=line.add<Button>(actionNames[i]);button.setSize({126,34});button.onClick([&,i]{controller.enqueue(Action(i));});}}
    auto callbacks=controller.callbacks();auto present=callbacks.present;callbacks.present=[&](Scene&s,const ApplicationFrame&f){present(s,f);auto&e=controller.state().encounter;
        panel.setSize({280,std::max(420.f,host.size().height-28)});
        status.setText("XP "+std::to_string(controller.state().characters.at(controller.player()).experience)+" | Stacks "+std::to_string(controller.state().items.size())+"\nTurn "+std::to_string(controller.state().turnEpoch)+" | AP "+std::to_string(e?e->allowance.ap:0)+(controller.paused()?" | Paused":""));message.setText(controller.message);};
    bool readyBefore=false;
    auto step=[&](){auto size=host.size();controller.aspect(size.width/std::max(1.f,size.height));bool ready=host.diagnostics().ready;if(readyBefore&&!ready)controller.cancelInput();readyBefore=ready;controller.drain();return host.step(callbacks);};
    if(!smoke){while(!host.closeRequested()&&step()){}return 0;}
    std::filesystem::create_directories(output);auto require=[](bool b,const char*s){if(!b)throw std::runtime_error(s);};double deadline=host.nowSeconds()+90;
    auto tick=[&](){require(step(),"Host stopped");auto diagnostic=host.diagnostics();if(!diagnostic.error.empty())throw std::runtime_error(diagnostic.error);require(host.nowSeconds()<deadline,"Smoke timeout");};
    while(host.diagnostics().presentedFrames<10)tick();
    std::ofstream(output/"device.json")<<host.diagnostics().renderState;
    auto capture=[&](const char*name){host.captureFrame(output/name);do{tick();}while(host.capturePending());require(host.captureError().empty(),"Capture failed");};
    auto act=[&](Action a){controller.execute(a);tick();};capture("before.ppm");
    act(Action::Equip);act(Action::Deliver);require(controller.awards()==2,"Reward missing");act(Action::Begin);act(Action::Move);
    auto turn=controller.state().turnEpoch;act(Action::Pause);controller.orbit(.4f);for(int i=0;i<5;++i)tick();require(controller.state().turnEpoch==turn,"Pause advanced turn");
    act(Action::Recover);act(Action::Pause);act(Action::Save);auto saved=storage.read();require(saved.has_value(),"Save missing");
    failWrite=true;try{controller.execute(Action::Save);throw std::logic_error("Write should fail");}catch(const std::runtime_error&){}failWrite=false;require(storage.read()==saved,"Failed write replaced save");
    // Inject into SDL's native event queue: hidden tests never steal OS focus.
    int count=0;auto windows=SDL_GetWindows(&count);require(windows&&count==1,"Native sample window missing");
    auto window=SDL_GetWindowID(windows[0]);float scale=SDL_GetWindowDisplayScale(windows[0]);SDL_free(windows);
    auto push=[&](SDL_Event event){require(SDL_PushEvent(&event),"Native event enqueue failed");};
    SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;focus.window.windowID=window;push(focus);tick();
    SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.windowID=window;key.key.scancode=SDL_SCANCODE_SPACE;key.key.key=SDLK_SPACE;key.key.down=true;push(key);
    key.type=SDL_EVENT_KEY_UP;key.key.down=false;push(key);for(int i=0;i<5;++i)tick();require(controller.state().turnEpoch==turn+1,"Native keyboard did not end exactly one turn");
    auto yaw=controller.scene().orbit->yaw;auto size=host.size();
    SDL_Event pointer{};pointer.type=SDL_EVENT_MOUSE_BUTTON_DOWN;pointer.button.windowID=window;pointer.button.button=SDL_BUTTON_LEFT;pointer.button.down=true;pointer.button.x=size.width*.75f*scale;pointer.button.y=size.height*.5f*scale;push(pointer);
    SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=window;motion.motion.state=SDL_BUTTON_LMASK;motion.motion.x=pointer.button.x+30*scale;motion.motion.y=pointer.button.y;push(motion);
    pointer.type=SDL_EVENT_MOUSE_BUTTON_UP;pointer.button.down=false;pointer.button.x=motion.motion.x;push(pointer);tick();require(controller.scene().orbit->yaw!=yaw,"Native drag did not inspect camera");
    controller.enqueue(Action::Travel);controller.enqueue(Action::EndTurn);tick();require(controller.discarded>0,"Travel retained input");
    act(Action::Load);require(controller.awards()==2&&controller.state().turnEpoch==turn,"Reload changed receipts/turn");
    auto old=&controller.state();auto corrupt=*saved;corrupt.back()^=1;try{controller.load(corrupt,controller.epoch());throw std::logic_error("Corrupt load should fail");}catch(const std::invalid_argument&){}require(&controller.state()==old,"Corrupt load published");
    capture("after.ppm");std::ofstream report(output/"report.json");report<<"{\"passed\":true,\"backend\":\"D3D12\",\"frames\":"<<host.diagnostics().presentedFrames<<",\"awards\":"<<controller.awards()<<"}\n";
    host.setEventHandler({});host.gui().clear();host.requestClose();host.step();require(host.application().loadedSceneCount()==0,"Shutdown retained scenes");std::cout<<"R9 D3D12 smoke passed\n";return 0;
}catch(const std::exception&e){std::ofstream("rpg-host-error.txt")<<e.what()<<'\n';std::cerr<<e.what()<<'\n';return 1;}}
