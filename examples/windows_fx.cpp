#include "fx_gallery.hpp"
#include "engine/desktop.hpp"
#include <iostream>
using namespace engine;
int main(int argc,char** argv){try{
    DesktopSettings settings;settings.title="WebEngine | Particle FX";std::filesystem::path capture;
    for(int i=1;i<argc;++i){const std::string key=argv[i];if(key=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="d3d12")settings.backend=DesktopBackend::Direct3D12;else if(value=="dawn")settings.backend=DesktopBackend::Dawn;else throw std::invalid_argument("Backend must be dawn or d3d12");}
        else if(key=="--capture"&&i+1<argc){capture=argv[++i];settings.hidden=true;}else{std::cout<<"webengine_fx_windows [--backend dawn|d3d12] [--capture image.ppm]\n";return key=="--help"?0:1;}}
    DesktopFiles files;DesktopHost host(settings);fx_example::Gallery g;const auto id=host.application().loadScene(std::move(g.scene));
    auto bind=[&]{std::vector<DesktopView> views;for(auto* c:g.cameras)views.push_back(DesktopView{c->id()});host.setViews(id,views);};bind();host.application().start(id);host.setPaused(true);
    host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024),17));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({16,16});panel.setSize({300,664});panel.setPadding(12);panel.setLayout(VerticalLayout{5});
    panel.add<TextLabel>("PARTICLES AND TRAILS").setSize({276,26});int selected=16;
    auto preset=[&](const char* name,int mode){auto& b=panel.add<Button>(name);b.setSize({276,30});b.onClick([&,mode]{selected=mode;g.setup(mode);bind();});};
    preset("Soft billboard",16);preset("Hard billboard",15);preset("Velocity stretch",19);preset("Flipbook red",10);preset("Flipbook green",11);
    preset("Plane / ground ring",44);preset("Ribbon soft depth",27);preset("Surface contact trail",87);preset("Mesh debris / 128 pieces",64);preset("Mesh material slots",62);preset("Two masked cameras",23);
    auto& restart=panel.add<Button>("Restart selected effect");restart.setSize({276,30});restart.onClick([&]{g.setup(selected);bind();});
    auto box=[&](const char* label,std::function<void(bool)> action){auto& item=panel.add<Checkbox>(label);item.setSize({276,30});item.onChange(std::move(action));};
    box("Play simulation",[&](bool value){host.setPaused(!value);});box("Height fog",[&](bool v){g.world->setFog(v?std::optional<FogSettings>{FogSettings{}}:std::nullopt);});
    box("HDR and bloom",[&](bool v){auto q=host.quality();q.post.enabled=v;q.post.bloomStrength=.3f;host.setQuality(q);});box("4x MSAA",[&](bool v){auto q=host.quality();q.msaaSamples=v?4:1;host.setQuality(q);});
    host.setEventHandler([&](const DesktopEvent& e){if(e.type==DesktopEventType::Control&&e.down&&!e.repeat&&!e.guiConsumed){if(e.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}if(e.control==DesktopControl::Escape){host.requestClose();return true;}}return false;});
    if(capture.empty())return host.run();const auto deadline=host.nowSeconds()+120;bool queued=false;
    while(host.step()){if(!host.diagnostics().error.empty())throw std::runtime_error(host.diagnostics().error);if(host.nowSeconds()>deadline)throw std::runtime_error("FX gallery capture timeout");
        if(host.diagnostics().presentedFrames>8&&!queued){host.captureFrame(capture);queued=true;}if(queued&&!host.capturePending()){if(!host.captureError().empty())throw std::runtime_error(host.captureError());host.requestClose();}}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
