#include "volumetric_clouds.hpp"
#include "engine/desktop.hpp"
#include "engine/mesh_component.hpp"
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace engine;

int main(int argc,char** argv) {
    try {
        DesktopSettings settings;
        settings.title="WebEngine | Volumetric clouds";
        settings.width=1280;
        settings.height=720;
        std::filesystem::path evidence;
        for(int i=1;i<argc;++i) {
            const std::string_view option=argv[i];
            if(option=="--backend"&&i+1<argc) {
                const std::string_view backend=argv[++i];
                if(backend=="dawn")settings.backend=DesktopBackend::Dawn;
                else if(backend=="d3d12")settings.backend=DesktopBackend::Direct3D12;
                else throw std::invalid_argument("Use --backend dawn or d3d12");
            } else if(option=="--cloud-smoke"&&i+1<argc) {
                evidence=argv[++i];
            } else if(option=="--clouds") {
                // The interactive cloud view is also the default.
            } else if(option=="--help") {
                std::cout<<"webengine_clouds_windows [--backend dawn|d3d12] [--clouds] [--cloud-smoke output-directory]\n";
                return 0;
            } else {
                throw std::invalid_argument("Expected [--backend dawn|d3d12] [--clouds] [--cloud-smoke output-directory]");
            }
        }
        settings.hidden=!evidence.empty();
        if(settings.hidden) {settings.width=640;settings.height=360;}
        settings.quality.post.enabled=false;
        DesktopHost host(settings);
        auto scene=std::make_unique<Scene>("Volumetric cloud layer");
        auto& world=*scene;
        auto& camera=world.addChild<CameraComponent>("Cloud camera");
        const auto sceneId=host.application().loadScene(std::move(scene));
        const auto configure=[&](int mode) {
            if(auto* previous=dynamic_cast<Actor*>(world.findSavedNode("cloud.fixture.root"))) {
                previous->destroy();world.flushPendingDestroy();
            }
            auto quality=host.quality();
            cloud_example::configure(world,camera,quality,mode);
            auto& root=world.spawn("Volumetric cloud fixture");
            root.setSavedId("cloud.fixture.root");
            cloud_example::populate(world,root,camera,mode);
            if(evidence.empty())quality.resolution={1,1,1024,1024*576};
            host.setQuality(quality);
            host.setShadowsEnabled(true);
            DesktopView view{camera.id()};
            view.skyEnabled=mode!=18;
            view.clearColor=std::array<float,4>{.02f,.03f,.04f,1};
            host.setViews(sceneId,{&view,1});
            world.synchronize();
        };
        configure(2);
        host.application().start(sceneId);
        host.setWaterTime(0);

        if(evidence.empty()) {
            int mode=2;
            bool paused=false;
            std::cout<<"Left/Right select cloud fixture, Space pause wind, F11 fullscreen, Escape quit\n";
            host.setEventHandler([&](const DesktopEvent& event) {
                if(event.type!=DesktopEventType::Control||!event.down||event.repeat||event.guiConsumed)return false;
                switch(event.control) {
                case DesktopControl::Space:
                    paused=!paused;host.setPaused(paused);return true;
                case DesktopControl::Left:
                case DesktopControl::Right:
                    mode=(mode+(event.control==DesktopControl::Right?1:cloud_example::fixtureCount-1))%cloud_example::fixtureCount;
                    configure(mode);
                    std::cout<<cloud_example::names[mode]<<'\n';
                    return true;
                case DesktopControl::F11:
                    host.setFullscreen(!host.fullscreen());return true;
                case DesktopControl::Escape:
                    host.requestClose();return true;
                default:
                    return false;
                }
            });
            ApplicationFrameCallbacks callbacks;
            callbacks.simulate=[](Scene& current,const ApplicationFrame& frame) {
                advanceSky(current,frame.simulation.delta);
            };
            const int result=host.run(callbacks);
            host.setEventHandler({});
            return result;
        }

        host.setPaused(true);
        std::filesystem::create_directories(evidence);
        std::ofstream output(evidence/"clouds.jsonl");
        if(!output)throw std::runtime_error("Cannot create cloud fixture diagnostics");
        output.imbue(std::locale::classic());
        for(int mode=0;mode<cloud_example::fixtureCount;++mode) {
            configure(mode);
            const auto firstFrame=host.diagnostics().presentedFrames;
            const auto started=host.nowSeconds();
            const auto deadline=started+90;
            const auto step=[&] {
                // A hidden acceptance window still renders when it loses focus.
                host.application().setSuspended(false);
                if(!host.step())throw std::runtime_error("Cloud fixture window closed");
                const auto info=host.diagnostics();
                if(!info.error.empty())throw std::runtime_error(info.error);
                if(host.nowSeconds()>deadline)
                    throw std::runtime_error(std::string("Cloud fixture timed out: ")+cloud_example::names[mode]);
                return info;
            };
            DesktopDiagnostics info;
            const auto passesReady=[&](const DesktopDiagnostics& diagnostics) {
                if(mode<22)return true;
                const auto counter=[&](const char* name) {
                    const auto field=std::string("\"")+name+"\":";
                    const auto offset=diagnostics.performance.find(field);
                    return offset==std::string::npos?0ull:std::strtoull(diagnostics.performance.c_str()+offset+field.size(),nullptr,10);
                };
                const int depth=cloud_example::depthPassExpectation(mode);
                return (depth<0||counter("cloudGridPasses")==static_cast<unsigned>(depth))&&
                    counter("cloudShadowPasses")==static_cast<unsigned>(cloud_example::shadowMode(mode));
            };
            // Allow lazy sky/noise uploads and Dawn's 250 ms diagnostics update.
            do {info=step();} while(!info.ready||info.presentedFrames<firstFrame+4||host.nowSeconds()-started<.3||!passesReady(info));
            host.captureFrame(evidence/(std::to_string(mode)+".ppm"));
            do {info=step();} while(host.capturePending());
            if(!host.captureError().empty())throw std::runtime_error(host.captureError());
            output<<"{\"mode\":"<<mode<<",\"name\":\""<<cloud_example::names[mode]
                  <<"\",\"backend\":\""<<(host.backend()==DesktopBackend::Direct3D12?"d3d12":"dawn")
                  <<"\",\"width\":"<<info.width<<",\"height\":"<<info.height
                  <<",\"effectiveMsaaSamples\":"<<info.effectiveMsaaSamples
                  <<",\"presentedFrames\":"<<info.presentedFrames
                  <<",\"state\":"<<info.renderState<<",\"performance\":"<<info.performance<<"}\n";
            output.flush();
            if(!output)throw std::runtime_error("Cannot write cloud fixture diagnostics");
            std::cout<<cloud_example::names[mode]<<" captured\n";
        }
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
