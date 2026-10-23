#include "engine/desktop.hpp"
#include "engine/mesh_component.hpp"
#include "engine/particle_emitter_component.hpp"
#include <iostream>
// This consumer intentionally has no SDL/Dawn/private source include directories.
using namespace engine;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Function>void rejects(Function function){bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejected candidate");}
void test(DesktopBackend backend) {
    DesktopSettings settings;settings.hidden=true;settings.width=640;settings.height=480;
    settings.backend=backend;
    DesktopHost host(settings);rejects([&]{DesktopHost duplicate(settings);});
    rejects([&]{host.simulateGraphicsLoss();});rejects([&]{host.simulateGraphicsFailure();});
    auto scene=std::make_unique<Scene>("First");auto& camera=scene->spawn("Camera").addComponent<CameraComponent>();
    const auto first=host.application().loadScene(std::move(scene));const DesktopView view{camera.id()};host.setViews(first,{&view,1});
    auto invalid=view;invalid.camera={};rejects([&]{host.setViews(first,{&invalid,1});});
    const auto original=host.quality();auto bad=original;bad.msaaSamples=3;rejects([&]{host.setQuality(bad);});
    check(host.quality().msaaSamples==original.msaaSamples,"Invalid quality changed live settings");
    host.application().start(first);const double startup=host.nowSeconds();double time=0;
    while(!host.diagnostics().ready){check(host.nowSeconds()-startup<15,"Desktop graphics initialization timeout");host.step({},time+=1./60);}
    if(backend==DesktopBackend::Direct3D12){
        auto* active=host.application().activeScene();
        auto& particles=active->spawn("Supported particles").addComponent<ParticleEmitterComponent>();
        particles.play();active->update(.1);
        active->synchronize();host.setViews(first,{&view,1});
        host.step({},time+=1./60);
        particles.destroy();active->synchronize();
        host.step({},time+=1./60);
        check(host.diagnostics().ready,"Particle scene/view replacement failed");
    }
    auto next=std::make_unique<Scene>("Second");auto* secondScene=next.get();auto& secondCamera=next->spawn("Camera").addComponent<CameraComponent>();
    const auto second=host.application().loadScene(std::move(next));
    rejects([&]{host.setViews(second,{&view,1});}); // A camera from a different scene cannot bind.
    const DesktopView nextView{secondCamera.id()};host.setViews(second,{&nextView,1});
    host.application().requestTransition(second,true);host.step({},time+=1./60);
    check(host.application().activeSceneId()==second&&!host.application().findScene(first),"Scene transition/unload failed");
    host.setPaused(true);host.step({},time+=1./60);const auto frozen=host.application().clocks().simulation.seconds;
    for(int i=0;i<3;++i)host.step({},time+=1./60);
    check(host.application().clocks().simulation.seconds==frozen,"Pause advanced simulation");
    host.setPaused(false);host.step({},time+=1./60);host.step({},time+=1./60);
    check(host.application().clocks().simulation.seconds>frozen,"Resume did not advance simulation");
    secondCamera.destroy();secondScene->synchronize(); // Removes the camera without invalid renderer access.
    host.step({},time+=1./60);rejects([&]{host.setViews(second,{&nextView,1});});
    ApplicationFrameCallbacks reentrant;reentrant.present=[&](Scene&,const ApplicationFrame&){rejects([&]{host.step();});};
    host.step(reentrant,time+=1./60);
    bool throwOnce=true;ApplicationFrameCallbacks throwing;throwing.present=[&](Scene&,const ApplicationFrame&){if(throwOnce){throwOnce=false;throw std::runtime_error("User callback");}};
    rejects([&]{host.step(throwing,time+=1./60);});host.step({},time+=1./60);
    ApplicationFrameCallbacks close;close.present=[&](Scene&,const ApplicationFrame&){host.captureFrame("desktop-cancelled.ppm");host.requestClose();};
    check(!host.step(close,time+=1./60),"Callback close did not exit");
    check(host.application().state()==ApplicationState::Stopped&&host.application().loadedSceneCount()==0,"Close retained scene ownership");
    check(host.diagnostics().error.empty(),host.diagnostics().error.c_str());
    check(!host.capturePending()&&!host.captureError().empty(),"Close did not cancel pending readback");
    check(!host.step(),"Stopped host stepped again");
}
}
int main(int argc,char** argv){try{test(argc>1&&std::string(argv[1])=="d3d12"?DesktopBackend::Direct3D12:argc>1&&std::string(argv[1])=="vulkan"?DesktopBackend::DawnVulkan:argc>1&&std::string(argv[1])=="d3d11"?DesktopBackend::DawnD3D11:argc>1&&std::string(argv[1])=="auto"?DesktopBackend::Automatic:DesktopBackend::Dawn);std::cout<<"Desktop public API tests passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
