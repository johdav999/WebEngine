#include "demo.hpp"
#include "engine/desktop.hpp"
#include <fstream>
#include <iostream>
#include <SDL3/SDL.h>

using namespace engine;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv){try{
    DesktopSettings settings;settings.title="WebEngine | Planet flight";settings.width=1280;settings.height=900;settings.backend=DesktopBackend::Dawn;
    std::filesystem::path output;
    for(int i=1;i<argc;++i){const std::string arg=argv[i];
        if(arg=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="dawn")settings.backend=DesktopBackend::Dawn;else if(value=="d3d12")settings.backend=DesktopBackend::Direct3D12;else throw std::invalid_argument("Use dawn or d3d12");}
        else if(arg=="--smoke"&&i+1<argc){output=argv[++i];settings.hidden=true;}else throw std::invalid_argument("Use --backend dawn|d3d12 [--smoke directory]");
    }
    planet_example::Demo demo;planet_example::Controls controls;DesktopHost host(settings);auto scene=demo.make();
    const auto id=host.application().loadScene(std::move(scene));const DesktopView view{demo.camera->id(),std::array<float,4>{.002f,.004f,.009f,1}};
    host.setViews(id,{&view,1});host.setShadowsEnabled(false);
    planet_example::Controls::bind(host.bindings(),{controlId(DesktopControl::W),controlId(DesktopControl::S),controlId(DesktopControl::A),controlId(DesktopControl::D),controlId(DesktopControl::Q),controlId(DesktopControl::E),controlId(DesktopControl::LeftShift),controlId(DesktopControl::RightShift),controlId(DesktopControl::MouseRight),controlId(DesktopControl::Space),controlId(DesktopControl::G)});
    DesktopFiles files;host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"),17));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({22,20});panel.setSize({880,200});panel.setPadding(12);panel.setLayout(VerticalLayout{6});
    panel.add<TextLabel>("WEBENGINE | LAND AND EXPLORE A PLANET").setSize({850,26});
    auto& row=panel.add<Panel>();row.setSize({690,32});row.setLayout(HorizontalLayout{8});
    auto button=[&](const char* title,auto callback){auto& b=row.add<Button>(title);b.setSize({162,30});b.onClick([&,callback]{host.cancelInput();controls.cancel();callback();});};
    button("Orbit bookmark",[&]{demo.request(0);});button("Land on surface",[&]{demo.request(1);});
    button("Take off to space",[&]{demo.request(2);});button("Stop tour",[&]{demo.request(3);});
    auto& second=panel.add<Panel>();second.setSize({850,32});second.setLayout(HorizontalLayout{8});
    auto action=[&](const char* label,int command){auto& b=second.add<Button>(label);b.setSize({162,30});b.onClick([&,command]{host.cancelInput();controls.cancel();demo.request(command);});};
    action("Jump / Space",6);action("Thrusters / G",7);action("20 m hover bookmark",9);action("Terrain detail",8);
    auto& status=panel.add<TextLabel>("Preparing planet");status.setSize({850,26});
    auto& help=host.gui().root().add<TextLabel>("WASD fly / walk | Q/E descend / take off | Space jump | G thrusters | Right-drag look\nStage 2: radial gravity, stable terrain collision, landing and surface movement");help.setPosition({28,835});help.setSize({1200,48});
    host.setEventHandler([&](const DesktopEvent& e){
        if(e.type==DesktopEventType::InputCancelled||e.type==DesktopEventType::Resized||(e.type==DesktopEventType::FocusChanged&&!e.focused))controls.cancel();
        if(e.type==DesktopEventType::Control&&e.down&&!e.repeat&&!e.guiConsumed){if(e.control==DesktopControl::Escape){host.cancelInput();controls.cancel();demo.tour=0;return true;}
            if(e.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}}
        return false;
    });
    ApplicationFrameCallbacks callbacks;
    callbacks.input=[&](Scene&,const InputSystem& input,const ApplicationFrame& frame){controls.update(demo,input,frame.input.delta);};
    callbacks.simulate=[&](Scene&,const ApplicationFrame&){demo.prepareSimulation();};
    callbacks.present=[&](Scene&,const ApplicationFrame& frame){demo.project(frame.presentation.delta,host.size().height,frame.interpolationAlpha);status.setText(demo.status());};
    // Finish synchronous fonts/UI setup before starting asynchronous GPU acquisition.
    // The first host step can then service adapter/device callbacks immediately.
    host.application().start(id);
    if(output.empty())return host.run(callbacks);
    std::filesystem::create_directories(output);double time=host.nowSeconds();const double deadline=time+240;
    auto tick=[&]{time+=.05;check(host.step(callbacks,time),"Native planet host closed");const auto d=host.diagnostics();check(d.error.empty(),d.error.c_str());check(host.nowSeconds()<deadline,"Native planet smoke timed out");};
    auto capture=[&](const char* name){for(int i=0;i<8;++i)tick();host.captureFrame(output/name);do{tick();}while(host.capturePending());check(host.captureError().empty(),host.captureError().c_str());};
    for(int i=0;i<160;++i)tick();check(host.diagnostics().backend==settings.backend,"Native backend mismatch");capture("orbit.ppm");
    demo.beginTour(true);double previous=demo.altitude();std::size_t samples=0;bool air=false,low=false;double biggestRelativeStep=0;
    while(demo.tour){tick();const double h=demo.altitude();check(h<=previous+.001,"Descent reversed");check(h>=2.999,"Source surface clearance failed");
        biggestRelativeStep=std::max(biggestRelativeStep,(previous-h)/std::max(4.,previous));previous=h;++samples;
        if(!air&&h<20000){capture("approach.ppm");previous=demo.altitude();air=true;}
        if(!low&&h<300){capture("low-flight.ppm");previous=demo.altitude();low=true;}}
    for(int i=0;i<200;++i)tick();capture("surface.ppm");const auto surfaceLevel=demo.terrain.stats().deepest;
    check(demo.motor.state().grounded&&demo.altitude()<2&&surfaceLevel>=14,"Collision landing or fine LOD was not reached");check(biggestRelativeStep<.08&&samples>450,"Descent was not continuous");
    check(demo.groundGap()>.6&&demo.groundGap()<1.2,"Landing did not use registered terrain");
    const auto rested=demo.position;for(int i=0;i<80;++i)tick();const double restDrift=(demo.position-rested).length();check(restDrift<.005,"Landed explorer drifted");
    const auto collider=demo.collider->shape();const auto builds=demo.collisionBuilds;const auto supported=demo.position;
    demo.coarseVisual=true;for(int i=0;i<45;++i)tick();demo.coarseVisual=false;for(int i=0;i<45;++i)tick();
    check(demo.collider->shape()==collider&&demo.collisionBuilds==builds&&(demo.position-supported).length()<.005,"Render detail changed physical support");
    check(demo.camera->worldPosition().x==0&&demo.camera->worldPosition().y==0&&demo.camera->worldPosition().z==0,"Camera origin drifted");
    const auto before=demo.position;const auto originalRebases=demo.rebases;host.bindings().control(controlId(DesktopControl::D),1);for(int i=0;i<420;++i){tick();check(demo.motor.state().grounded,"Native walk lost support");}host.bindings().control(controlId(DesktopControl::D),0);tick();
    const double walked=(demo.position-before).length();check(walked>115&&demo.rebases>originalRebases,"Surface travel did not cross a physics rebase");capture("walking.ppm");
    host.bindings().control(controlId(DesktopControl::Space),1);tick();host.bindings().control(controlId(DesktopControl::Space),0);check(!demo.motor.state().grounded,"Native jump stayed grounded");
    for(int i=0;i<80;++i)tick();check(demo.motor.state().grounded&&demo.motor.state().jumps==1,"Native jump did not reland");capture("relanded.ppm");
    host.bindings().control(controlId(DesktopControl::E),1);for(int i=0;i<10;++i)tick();host.cancelInput();tick();const auto cancelled=demo.position;for(int i=0;i<8;++i)tick();
    check((demo.position-cancelled).length()<1e-6,"Cancel left native input held");
    // Release thrusters from a visible hover, then land through gravity/contact.
    demo.request(9);tick();check(demo.thrusters&&demo.altitude()>19,"Hover bookmark failed");
    host.bindings().control(controlId(DesktopControl::G),1);tick();host.bindings().control(controlId(DesktopControl::G),0);
    check(!demo.thrusters,"Gravity control did not release thrusters");const double released=demo.altitude();for(int i=0;i<5;++i)tick();check(demo.altitude()<released-.1,"Planet gravity did not accelerate descent");capture("gravity-drop.ppm");
    for(int i=0;i<100&&!demo.motor.state().grounded;++i)tick();check(demo.motor.state().grounded,"Gravity drop did not land");capture("gravity-landed.ppm");
    const auto landings=demo.motor.state().landings,jumps=demo.motor.state().jumps;const double cargoGap=demo.cargoGap();
    demo.beginTour(false);while(demo.tour)tick();for(int i=0;i<100;++i)tick();capture("return-to-space.ppm");
    const auto& stats=demo.terrain.stats();check(stats.leaves<=demo.terrain.settings().maxLeaves,"Planet leaf budget exceeded");check(stats.resident<=stats.leaves*2,"Planet resident budget exceeded");
    std::ofstream report(output/"report.json");report<<"{\"backend\":\""<<(settings.backend==DesktopBackend::Dawn?"dawn":"d3d12")<<"\",\"presentedFrames\":"<<host.diagnostics().presentedFrames
        <<",\"stage\":2,\"descentSamples\":"<<samples<<",\"maxRelativeDescentStep\":"<<biggestRelativeStep<<",\"surfaceLevel\":"<<surfaceLevel<<",\"walked\":"<<walked<<",\"restDrift\":"<<restDrift<<",\"landings\":"<<landings<<",\"jumpsBeforeDrop\":1,\"cargoGap\":"<<cargoGap
        <<",\"collisionBuilds\":"<<demo.collisionBuilds<<",\"splits\":"<<stats.splits<<",\"merges\":"<<stats.merges<<",\"leaves\":"<<stats.leaves<<",\"resident\":"<<stats.resident<<",\"controls\":true,\"cancel\":true,\"cameraAtOrigin\":true,\"lodCollisionStable\":true,\"gravityDrop\":true,\"takeoff\":true}";
    std::cout<<"Planet native acceptance passed: "<<output<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
