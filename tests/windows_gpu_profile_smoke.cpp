#include "../examples/water_gallery.hpp"
#include "engine/desktop.hpp"
#include "engine/performance_timeline.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace engine;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::uint64_t number(const std::string& json,const char* key){const auto at=json.find(key);return at==std::string::npos?0:std::stoull(json.substr(at+std::strlen(key)));}
void write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path);file<<text;check(bool(file),"Cannot write profiling evidence");}
}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: windows_gpu_profile_smoke dawn|d3d12 evidence-directory");const std::string backend=argv[1];check(backend=="dawn"||backend=="d3d12","Unknown backend");
    const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
    const bool direct=backend=="d3d12",disabled=std::getenv("WEBENGINE_GPU_TIMING")&&std::string_view(std::getenv("WEBENGINE_GPU_TIMING"))=="0";
    DesktopSettings settings;settings.hidden=true;settings.width=800;settings.height=600;settings.backend=direct?DesktopBackend::Direct3D12:DesktopBackend::Dawn;settings.allowGraphicsFaultInjection=true;
    settings.quality.post.enabled=true;settings.quality.msaaSamples=4;settings.quality.directionalCascades=3;
    DesktopHost host(settings);auto gallery=water_example::makeGallery();
    auto fx=std::make_shared<ParticleEmitterDefinition>();fx->spawnRate=0;fx->bursts={{0,1}};fx->velocityMin=fx->velocityMax={};fx->softDistance=.5f;fx->size={{0,3}};
    gallery.world->spawn("Profile soft FX").addComponent<ParticleEmitterComponent>("Mist",fx).transform.position={0,1,2};gallery.world->update(0);
    const auto id=host.application().loadScene(std::move(gallery.scene));const DesktopView view{gallery.camera->id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    DesktopFiles files;host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024),17));auto& label=host.gui().root().add<TextLabel>("GPU pass profiling");label.setSize({300,40});
    auto cpu=std::make_unique<FrameTimeline>();ProfileSession session(*cpu);cpu->setRecording(true);double time=0;
    auto step=[&]{ProfileFrame frame;check(host.step({},time+=1./60),"Unexpected host close");const auto d=host.diagnostics();check(d.error.empty(),d.error.c_str());};
    auto wait=[&](auto predicate){const auto end=host.nowSeconds()+90;while(!predicate()){check(host.nowSeconds()<end,"GPU profiling timeout");step();}};
    wait([&]{return host.diagnostics().ready;});check(!direct||host.capabilities().timestampQuery,"Direct queue timestamp support missing");
    auto fresh=[&]{if(!direct||disabled){for(int i=0;i<20;++i)step();return;}const auto before=number(host.diagnostics().performance,"\"frame\":");wait([&]{return number(host.diagnostics().performance,"\"frame\":")>before;});};
    auto save=[&](const char* name){fresh();host.captureFrame(directory/(std::string(name)+".ppm"));wait([&]{return !host.capturePending();});check(host.captureError().empty(),host.captureError().c_str());
        write(directory/(std::string(name)+".json"),host.diagnostics().performance);write(directory/(std::string(name)+"-state.json"),host.diagnostics().renderState);write(directory/(std::string(name)+"-trace.json"),host.gpuProfileTrace());};
    save("above");gallery.underwater(true);save("underwater");gallery.underwater(false);
    auto& second=gallery.world->spawn("Second view").addComponent<CameraComponent>();second.transform=gallery.camera->transform;second.setViewport({.5f,0,.5f,1});gallery.camera->setViewport({0,0,.5f,1});const DesktopView split[]{view,DesktopView{second.id()}};host.setViews(id,split);save("split");
    host.setWindowSize(703,503);save("resized");host.setWindowSize(800,600);gallery.camera->setViewport({});host.setViews(id,{&view,1});
    const auto before=number(host.diagnostics().renderState,"\"generation\":");const auto clock=host.application().clocks().simulation.seconds;check(host.simulateGraphicsLoss(),"Real GPU loss unavailable");
    wait([&]{return host.diagnostics().ready&&number(host.diagnostics().renderState,"\"generation\":")>before;});check(host.application().clocks().simulation.seconds==clock,"Profiling/recovery changed simulation clock");save("recovered");
    auto quality=host.quality();quality.post.bloomStrength=0;host.setQuality(quality);
    if(direct)check(host.gpuProfileTrace().find("\"ph\":\"X\"")==std::string::npos,"Reset retained old trace");save("no-bloom");
    quality.post.enabled=false;quality.msaaSamples=1;host.setQuality(quality);save("scene-copy");
    if(direct&&!disabled){
        // Keep requesting real asynchronous captures until a sampled submission
        // contains the copy. This does not change the renderer's 10 Hz policy.
        for(unsigned i=0;i<60&&host.gpuProfileTrace().find("Framebuffer capture")==std::string::npos;++i){host.captureFrame(directory/"sampled-capture.ppm");wait([&]{return !host.capturePending();});step();}
        check(host.gpuProfileTrace().find("Framebuffer capture")!=std::string::npos,"Missing GPU capture copy timing");
        write(directory/"sampled-capture-trace.json",host.gpuProfileTrace());
        wait([&]{return number(host.diagnostics().performance,"\"totalSamples\":")>=125;});
        write(directory/"bounded.json",host.diagnostics().performance);write(directory/"bounded-trace.json",host.gpuProfileTrace());
    }
    cpu->setRecording(false);write(directory/"cpu-trace.json",cpu->traceJson());host.requestClose();host.step({},time+=1./60);
    check(host.gpuProfileTrace().find("\"ph\":\"X\"")==std::string::npos,"Shutdown retained GPU trace");
    write(directory/"result.json",std::string("{\"passed\":true,\"disabled\":")+(disabled?"true":"false")+",\"backend\":\""+backend+"\"}");
    std::cout<<backend<<" GPU profiling acceptance passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
