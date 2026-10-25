#include "host_controller.hpp"
#include "engine/desktop.hpp"
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

int main(int argc,char**argv){try{
    using namespace engine;using namespace rpg_example;
    bool smoke=false;std::filesystem::path output="rpg-smoke";
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--smoke"&&i+1<argc){smoke=true;output=argv[++i];}else throw std::invalid_argument("Use --smoke output-directory");}
    DesktopSettings settings;settings.title="RPG - Training Court";settings.backend=DesktopBackend::Direct3D12;settings.hidden=smoke;settings.width=1100;settings.height=760;
    DesktopHost host(settings);DesktopFileSettings fs;fs.application="WebEngineRpgSample";if(smoke)fs.userDirectory=std::filesystem::absolute(output/"user");DesktopFiles files(fs);
    bool failWrite=false;Storage storage;
    storage.read=[&]()->std::optional<std::vector<std::uint8_t>>{auto path=files.userPath(DesktopStorage::Saves,"rpg.save");if(!std::filesystem::exists(path))return {};auto raw=readDesktopFile(path,r::snapshotByteLimit);auto*b=reinterpret_cast<const std::uint8_t*>(raw.data());return std::vector<std::uint8_t>(b,b+raw.size());};
    storage.write=[&](auto bytes){if(failWrite)throw std::runtime_error("Injected failed write; previous save retained");files.writeUser(DesktopStorage::Saves,"rpg.save",std::as_bytes(bytes),r::snapshotByteLimit);};
    Controller controller(host.application(),storage,[&](SceneId id,HostScene&scene){DesktopView view{scene.camera};host.setViews(id,{&view,1});});
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
    controller.start();host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"),16));
    auto&panel=host.gui().root().add<Panel>();panel.setPosition({14,14});panel.setSize({280,710});panel.setPadding(10);panel.setLayout(VerticalLayout{4});
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
    auto tick=[&](){require(step(),"Host stopped");require(host.nowSeconds()<deadline,"Smoke timeout");};
    while(host.diagnostics().presentedFrames<10)tick();
    std::ofstream(output/"device.json")<<host.diagnostics().renderState;
    auto capture=[&](const char*name){host.captureFrame(output/name);do{tick();}while(host.capturePending());require(host.captureError().empty(),"Capture failed");};
    auto act=[&](Action a){controller.execute(a);tick();};capture("before.ppm");
    act(Action::Equip);act(Action::Deliver);require(controller.awards()==2,"Reward missing");act(Action::Begin);act(Action::Move);
    auto turn=controller.state().turnEpoch;act(Action::Pause);controller.orbit(.4f);for(int i=0;i<5;++i)tick();require(controller.state().turnEpoch==turn,"Pause advanced turn");
    act(Action::Recover);act(Action::Pause);act(Action::Save);auto saved=storage.read();require(saved.has_value(),"Save missing");
    failWrite=true;try{controller.execute(Action::Save);throw std::logic_error("Write should fail");}catch(const std::runtime_error&){}failWrite=false;require(storage.read()==saved,"Failed write replaced save");
#ifdef _WIN32
    // Exercise the native window event queue and DesktopHost control mapping.
    HWND window=nullptr;
    EnumWindows([](HWND candidate,LPARAM data)->BOOL{DWORD process=0;GetWindowThreadProcessId(candidate,&process);wchar_t title[128]{};GetWindowTextW(candidate,title,128);
        if(process==GetCurrentProcessId()&&std::wstring_view(title)==L"RPG - Training Court"){*reinterpret_cast<HWND*>(data)=candidate;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&window));
    require(window!=nullptr,"Native sample window missing");
    // Hidden smoke windows have no OS keyboard focus. Inject the focus event into
    // this window's queue without taking focus from the user's foreground app.
    require(PostMessageW(window,WM_SETFOCUS,0,0)!=0,"Native focus event failed");tick();
    auto key=[&](WPARAM value){auto scan=MapVirtualKeyW(static_cast<UINT>(value),MAPVK_VK_TO_VSC);auto down=LPARAM(1)|(LPARAM(scan)<<16);
        require(PostMessageW(window,WM_KEYDOWN,value,down)!=0,"Native key down failed");
        require(PostMessageW(window,WM_KEYUP,value,down|LPARAM(0xc0000000u))!=0,"Native key up failed");};
    key(VK_SPACE);for(int i=0;i<5;++i)tick();require(controller.state().turnEpoch==turn+1,"Native keyboard did not end exactly one turn");
#endif
    controller.enqueue(Action::Travel);controller.enqueue(Action::EndTurn);tick();require(controller.discarded>0,"Travel retained input");
    act(Action::Load);require(controller.awards()==2&&controller.state().turnEpoch==turn,"Reload changed receipts/turn");
    auto old=&controller.state();auto corrupt=*saved;corrupt.back()^=1;try{controller.load(corrupt,controller.epoch());throw std::logic_error("Corrupt load should fail");}catch(const std::invalid_argument&){}require(&controller.state()==old,"Corrupt load published");
    capture("after.ppm");std::ofstream report(output/"report.json");report<<"{\"passed\":true,\"backend\":\"D3D12\",\"frames\":"<<host.diagnostics().presentedFrames<<",\"awards\":"<<controller.awards()<<"}\n";
    host.setEventHandler({});host.gui().clear();host.requestClose();host.step();require(host.application().loadedSceneCount()==0,"Shutdown retained scenes");std::cout<<"R9 D3D12 smoke passed\n";return 0;
}catch(const std::exception&e){std::ofstream("rpg-host-error.txt")<<e.what()<<'\n';std::cerr<<e.what()<<'\n';return 1;}}
