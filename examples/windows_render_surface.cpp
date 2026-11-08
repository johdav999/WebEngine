#include "render_surface_demo.hpp"
#include "engine/desktop.hpp"
#include <iostream>
#include <string_view>

using namespace engine;
using namespace render_surface_demo;

int main(int argc,char** argv) {
    try {
        DesktopSettings settings;
        settings.title="WebEngine Rear Camera Monitor";
        settings.width=800;
        settings.height=600;
        std::filesystem::path evidence;
        for(int i=1;i<argc;++i) {
            const std::string_view flag=argv[i];
            if(flag=="--backend"&&i+1<argc) {
                const std::string_view backend=argv[++i];
                if(backend=="dawn") settings.backend=DesktopBackend::Dawn;
                else if(backend=="d3d12") settings.backend=DesktopBackend::Direct3D12;
                else throw std::invalid_argument("Use --backend dawn or d3d12");
            } else if(flag=="--smoke"&&i+1<argc) {
                evidence=argv[++i];
            } else throw std::invalid_argument("Expected [--backend dawn|d3d12] [--smoke output-directory]");
        }
        settings.hidden=!evidence.empty();
        settings.allowGraphicsFaultInjection=settings.hidden;
        DesktopHost host(settings);
        auto scene=std::make_unique<Scene>("Rear camera monitor");
        auto& camera=scene->addChild<CameraComponent>("Dashboard camera");
        auto& world=*scene;
        Monitor monitor(scene->spawn("Monitor fixture"),camera);
        const auto sceneId=host.application().loadScene(std::move(scene));
        const auto select=[&](Mode mode) {
            const auto plan=monitor.configure(mode);
            std::array<DesktopView,2> views;
            for(std::size_t i=0;i<views.size();++i) {
                const auto& view=plan[i];
                views[i]={view.camera->id(),view.clearColor,view.skyEnabled,view.fogEnabled,view.target,view.refreshSurface};
            }
            host.setViews(sceneId,{views.data(),mode==Mode::Removed ? 1u : views.size()});
            world.synchronize();
        };
        select(Mode::Live);
        host.application().start(sceneId);
        host.setPaused(true);
        if(evidence.empty()) {
            std::cout<<"L live, F freeze, R refresh, S resize, X remove feed, Escape quit\n";
            host.setEventHandler([&](const DesktopEvent& event) {
                if(event.type!=DesktopEventType::Control||!event.down||event.repeat||event.guiConsumed) return false;
                switch(event.control) {
                case DesktopControl::L: select(Mode::Live);return true;
                case DesktopControl::F: select(Mode::Frozen);return true;
                case DesktopControl::R: select(Mode::Refreshed);return true;
                case DesktopControl::S: select(Mode::Resized);return true;
                case DesktopControl::X: select(Mode::Removed);return true;
                case DesktopControl::Escape: host.requestClose();return true;
                default: return false;
                }
            });
            return host.run();
        }

        std::filesystem::create_directories(evidence);
        const auto take=[&](const char* name,bool waitForRecovery=false) {
            const auto started=host.nowSeconds();
            const auto deadline=started+45;
            const auto first=host.diagnostics().presentedFrames;
            const auto step=[&] {
                if(!host.step()||host.nowSeconds()>deadline)
                    throw std::runtime_error(std::string(name)+" capture timed out: "+host.diagnostics().error);
                return host.diagnostics();
            };
            auto info=host.diagnostics();
            while(!info.ready||info.presentedFrames==0||(waitForRecovery&&info.presentedFrames==first)) info=step();
            // On an already running device, capture the first frame after changing the mode.
            host.captureFrame(evidence/(std::string(name)+".ppm"));
            do { info=step(); } while(host.capturePending());
            if(!host.captureError().empty()) throw std::runtime_error(host.captureError());
            // Dawn publishes performance at 250 ms intervals.
            while(info.presentedFrames==first||host.nowSeconds()-started<.3) info=step();
            std::cout<<name<<' '<<info.performance<<'\n';
        };
        take("live");
        select(Mode::Frozen);take("frozen");
        select(Mode::Refreshed);take("refreshed");
        select(Mode::Resized);take("resized");
        select(Mode::Removed);take("removed");
        select(Mode::Refreshed);take("restored");
        select(Mode::Frozen);
        if(!host.simulateGraphicsLoss()) throw std::runtime_error("Could not inject graphics loss");
        take("recovery-fallback",true);
        select(Mode::Refreshed);take("recovered");
        host.setWindowSize(720,540);take("window-resized");
        monitor.configure(Mode::Refreshed)[1].camera->destroy();
        world.flushPendingDestroy();
        take("camera-destroyed");
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
