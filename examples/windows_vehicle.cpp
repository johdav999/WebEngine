#include "windows_vehicle_app.hpp"
#include "engine/desktop_runtime.hpp"
#include <iostream>

int main(int argc,char** argv) {
    std::unique_ptr<engine::DesktopSessionLog> log;std::filesystem::path probe;
    try {
        engine::DesktopBackend backend=engine::DesktopBackend::Automatic;bool fullscreen=false;
        std::filesystem::path model;auto files=vehicle_example::vehicleFileSettings();engine::DesktopLogSettings logs;logs.application="Vehicle";logs.redirectStandardStreams=true;
        for(int i=1;i<argc;++i) {
            const std::string key=argv[i];
            if(key=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="auto")backend=engine::DesktopBackend::Automatic;else if(value=="vulkan")backend=engine::DesktopBackend::DawnVulkan;else if(value=="d3d11")backend=engine::DesktopBackend::DawnD3D11;else if(value=="dawn")backend=engine::DesktopBackend::Dawn;else if(value=="d3d12")backend=engine::DesktopBackend::Direct3D12;else throw std::invalid_argument("Backend must be auto, dawn, vulkan, d3d11 or d3d12");}
            else if(key=="--fullscreen")fullscreen=true;
            else if((key=="--model"||key=="--assets"||key=="--user-dir"||key=="--log-dir"||key=="--distribution-test")&&i+1<argc) {
                const std::string value=argv[++i];const auto path=std::filesystem::path(std::u8string(value.begin(),value.end()));
                if(key=="--model")model=path;else if(key=="--assets")files.assetDirectory=path;else if(key=="--log-dir")logs.directory=path;else if(key=="--distribution-test")probe=path;else files.userDirectory=path;
            }else {std::cout<<"webengine_windows [--backend auto|dawn|vulkan|d3d11|d3d12] [--fullscreen] [--model file.glb] [--assets directory] [--user-dir directory]\nWASD/arrows drive; P/Escape opens settings/audio/files/checkpoints; R resets; F11 fullscreen.\n";return key=="--help"?0:1;}
        }
        log=std::make_unique<engine::DesktopSessionLog>(logs);
        log->write(engine::DesktopLogLevel::Info,"Native vehicle starting; executable resolved independently of working directory");
        if(!probe.empty()&&files.userDirectory.empty())throw std::invalid_argument("Distribution tests require an explicit isolated --user-dir");
        engine::DesktopSettings settings;settings.title="WebEngine | Windows vehicle";
        settings.hidden=!probe.empty();settings.backend=backend;
        engine::DesktopHost host(settings);if(fullscreen)host.setFullscreen(true);
        vehicle_example::VehicleDesktopDemo demo(host,model,files);
        if(!probe.empty()){
            const auto deadline=host.nowSeconds()+120;double time=0;
            auto step=[&]{if(!host.step(demo.callbacks(),time+=1./60))throw std::runtime_error("Distribution test closed unexpectedly");};
            while(!host.diagnostics().ready){if(host.nowSeconds()>deadline)throw std::runtime_error("Dawn startup timed out");step();}
            for(int i=0;i<90;++i)step();
            if(!host.gui().font())throw std::runtime_error("Bundled GUI font unavailable");
            const auto sound=engine::decodeDesktopWav(demo.files().readAsset("audio/preview.wav",32*1024*1024));
            if(!sound)throw std::runtime_error("Bundled sound unavailable");
            demo.setMenu(true);step();demo.setMenu(false);demo.saveProgress();demo.loadProgress();
            auto capture=probe;capture.replace_extension(".ppm");host.captureFrame(capture);
            while(host.capturePending()){if(host.nowSeconds()>deadline)throw std::runtime_error("Dawn framebuffer test timed out");step();}
            if(!host.captureError().empty())throw std::runtime_error(host.captureError());
            const auto d=host.diagnostics();std::ofstream out(probe);
            out<<"{\"passed\":true,\"frames\":"<<d.presentedFrames<<",\"font\":true,\"wav\":true,\"saveRoundTrip\":true,\"renderState\":"<<d.renderState<<",\"performance\":"<<d.performance<<"}\n";
            if(!out)throw std::runtime_error("Cannot write distribution test report");
            log->write(engine::DesktopLogLevel::Info,"Distribution test passed: GPU/font/WAV/menu/save/readback");return 0;
        }
        auto callbacks=demo.callbacks();const auto present=callbacks.present;std::string loggedGraphics;
        callbacks.present=[&](engine::Scene& scene,const engine::ApplicationFrame& frame){
            if(present)present(scene,frame);const auto state=host.diagnostics().renderState;
            if(state!=loggedGraphics){log->write(engine::DesktopLogLevel::Info,"Graphics: "+state);loggedGraphics=state;}
        };
        return host.run(callbacks);
    } catch(const std::exception& error) {if(log)log->write(engine::DesktopLogLevel::Error,error.what());else std::cerr<<error.what()<<std::endl;
        if(probe.empty())engine::showDesktopError("WebEngine runtime error",error.what(),log?log->file():std::filesystem::path{});return 1;}
}
