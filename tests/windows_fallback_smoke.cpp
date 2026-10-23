#include "../examples/windows_acceptance_scene.hpp"
#include "engine/desktop.hpp"
#include "engine/fbx_importer.hpp"
#include "engine/scene_archive.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
using namespace engine;
namespace {void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}}
int main(int argc,char** argv){try{
    const auto out=std::filesystem::absolute(argc>1?argv[1]:"build/evidence/windows-fallback");std::filesystem::create_directories(out);
    DesktopSettings settings;settings.hidden=true;settings.width=1280;settings.height=800;settings.allowGraphicsFaultInjection=true;settings.quality.post.enabled=true;settings.quality.post.bloomStrength=.3f;
    DesktopHost host(settings);auto gallery=acceptance::makeGallery(FbxImporter::loadFile(std::filesystem::path(DESKTOP_FIXTURE_DIR)/"skeletal_column_binary.fbx"));
    auto* source=gallery.scene.get();const auto sceneId=host.application().loadScene(std::move(gallery.scene));const DesktopView view{gallery.camera->id()};host.setViews(sceneId,{&view,1});host.application().start(sceneId);host.setPaused(true);
    host.gui().setFont(GuiFont::loadTrustedTrueType(readDesktopFile(host.baseDirectory()/"assets/fonts/NotoSans-Regular.ttf",8*1024*1024),17));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({12,12});panel.setSize({380,130});auto& label=panel.add<TextLabel>("Automatic Windows graphics fallback");label.setPosition({10,10});label.setSize({360,28});auto& input=panel.add<TextInput>();input.setPosition({10,48});input.setSize({350,34});input.setText("Retained GUI state");
    double timestamp=0;auto frame=[&]{require(host.step({},timestamp),"Unexpected host close");};
    auto wait=[&](auto predicate){const auto end=host.nowSeconds()+120;while(!predicate()){require(host.nowSeconds()<end,"Windows fallback acceptance timed out");frame();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    wait([&]{return host.diagnostics().ready;});require(host.backend()==DesktopBackend::Dawn,"Automatic preferred backend changed");
    auto capture=[&](const std::string& name){host.captureFrame(out/(name+".ppm"));wait([&]{return !host.capturePending();});require(host.captureError().empty(),"Framebuffer readback failed");};
    const auto before=SceneArchive::save(*source);const auto simulation=host.application().clocks().simulation.seconds;capture("dawn-d3d12");
    std::vector<std::string> states{host.diagnostics().renderState};
    for(const auto [backend,name]:{std::pair{DesktopBackend::DawnVulkan,"vulkan"},std::pair{DesktopBackend::DawnD3D11,"d3d11"}}){
        host.captureFrame(out/(std::string(name)+"-cancelled.ppm"));frame();require(host.capturePending(),"Readback completed before the in-flight cancellation check");
        const auto presentation=host.application().clocks().presentation.seconds;timestamp+=1000;require(host.simulateGraphicsFailure(),"Terminal fault injection was rejected");frame();
        wait([&]{return host.diagnostics().ready&&host.backend()==backend;});
        require(!host.capturePending()&&!host.captureError().empty(),"Fallback retained a stale capture");
        require(host.application().activeSceneId()==sceneId&&host.application().activeScene()==source,"Fallback replaced the application scene");
        require(SceneArchive::save(*source)==before&&host.application().clocks().simulation.seconds==simulation,"Fallback changed source simulation");require(input.text()=="Retained GUI state","Fallback replaced GUI state");
        require(host.application().clocks().presentation.seconds==presentation,"Fallback caught up outage time");capture(name);states.push_back(host.diagnostics().renderState);
    }
    // Native input is still routed to the retained GUI after two real GPU replacements.
    require(host.gui().focusWidget(input.id()),"GUI field cannot focus after fallback");host.gui().editText("After fallback",14,14);require(input.text()=="After fallback","GUI edit failed after fallback");
    require(host.simulateGraphicsFailure(),"Cannot exercise exhaustion");bool exhausted=false;try{frame();}catch(const std::runtime_error&){exhausted=true;}require(exhausted&&!host.diagnostics().error.empty(),"Exhaustion did not report a terminal error");
    require(host.diagnostics().graphicsAttempts.size()==3,"Fallback loop exceeded its candidate budget");
    host.retryGraphics();wait([&]{return host.diagnostics().ready;});require(host.backend()==DesktopBackend::Dawn&&source==host.application().activeScene(),"Explicit retry lost source state");
    require(SceneArchive::save(*source)==before,"Explicit retry changed source state");
    host.requestClose();require(!host.step({},0),"Shutdown did not close");
    std::ofstream report(out/"report.json");report<<"{\"passed\":true,\"diagnosticFaultInjection\":true,\"sourcePreserved\":true,\"guiPreserved\":true,\"captureCancelled\":true,\"boundedExhaustion\":true,\"explicitRetry\":true,\"renderStates\":[";for(std::size_t i=0;i<states.size();++i){if(i)report<<',';report<<states[i];}report<<"]}\n";
    std::cout<<"Native fallback: Dawn D3D12 -> Vulkan -> D3D11 gallery, GUI, state, captures, exhaustion and retry passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
