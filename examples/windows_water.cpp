#include "water_gallery.hpp"
#include "engine/desktop.hpp"
#include <iostream>
using namespace engine;
int main(int argc,char** argv){try{
    DesktopSettings settings;settings.title="WebEngine | Water";settings.quality.post.enabled=true;std::filesystem::path capture;
    for(int i=1;i<argc;i++){const std::string key=argv[i];if(key=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="d3d12")settings.backend=DesktopBackend::Direct3D12;else if(value=="dawn")settings.backend=DesktopBackend::Dawn;else throw std::invalid_argument("Backend must be dawn or d3d12");}
        else if(key=="--capture"&&i+1<argc){capture=argv[++i];settings.hidden=true;}else{std::cout<<"webengine_water_windows [--backend dawn|d3d12] [--capture image.ppm]\n";return key=="--help"?0:1;}}
    DesktopFiles files;DesktopHost host(settings);auto g=water_example::makeGallery();auto m=g.defaults;m.water.speed=1;g.apply(m);
    auto& boat=lighting_example::object(*g.world,"Moving wake",Mesh::cube(),{},{.5f,.2f,.8f},lighting_example::material({1,.65f,.06f}));boat.castsShadow=false;
    const auto id=host.application().loadScene(std::move(g.scene));const DesktopView view{g.camera->id()};host.setViews(id,{&view,1});host.application().start(id);
    host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024),17));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({16,16});panel.setSize({310,674});panel.setPadding(12);panel.setLayout(VerticalLayout{5});
    panel.add<TextLabel>("WATER AND REFLECTIONS").setSize({286,24});
    auto box=[&](const char* label,bool value,std::function<void(bool)> action)->Checkbox&{auto& item=panel.add<Checkbox>(label);item.setSize({286,30});item.setChecked(value);item.onChange(std::move(action));return item;};
    auto slider=[&](const char* label,float lo,float hi,float value,std::function<void(float)> action){panel.add<TextLabel>(label).setSize({286,24});auto& item=panel.add<Slider>();item.setSize({286,28});item.setAccessibleName(label);item.setRange(lo,hi,.01f);item.setValue(value);item.onChange(std::move(action));};
    box("Animate water",true,[&](bool v){host.setPaused(!v);});
    slider("Wave height",0,.6f,.22f,[&](float v){auto material=g.current;material.water.amplitude=v;g.apply(material);});
    slider("Refraction",0,1,.85f,[&](float v){auto material=g.current;material.water.refractionStrength=v;g.apply(material);});
    slider("Shoreline foam width",.01f,2,.6f,[&](float v){auto material=g.current;material.water.foamWidth=v;g.apply(material);});
    box("Planar reflections",true,[&](bool v){auto material=g.current;material.water.planarReflectionStrength=v?1.f:0.f;g.apply(material);});
    box("Underwater camera",false,[&](bool v){g.underwater(v);});
    box("Underwater fog and absorption",true,[&](bool v){auto material=g.current;material.water.underwaterStrength=v?1.f:0.f;g.apply(material);});
    box("Underwater caustics",true,[&](bool v){auto material=g.current;material.water.causticsStrength=v?1.6f:0.f;g.apply(material);});
    bool interactions=true;box("Contact ripples and moving wake",true,[&](bool v){interactions=v;});
    box("Height fog",false,[&](bool v){g.world->setFog(v?std::optional<FogSettings>{FogSettings{}}:std::nullopt);});
    auto& hdr=box("HDR post-processing",true,[&](bool v){auto q=host.quality();q.post.enabled=v;host.setQuality(q);});
    auto& msaa=box("Smooth edges (4x MSAA)",false,[&](bool v){auto q=host.quality();q.msaaSamples=v?4:1;host.setQuality(q);});
    auto& quit=panel.add<Button>("Quit / Esc");quit.setSize({286,32});quit.onClick([&]{host.requestClose();});
    host.setEventHandler([&](const DesktopEvent& e){if(e.type==DesktopEventType::Control&&e.down&&!e.repeat&&!e.guiConsumed){if(e.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}if(e.control==DesktopControl::Escape){host.requestClose();return true;}}return false;});
    ApplicationFrameCallbacks callbacks;callbacks.present=[&](Scene&,const ApplicationFrame& frame){
        const auto time=frame.simulation.seconds;boat.transform.position={float(std::sin(time*.5)*4),.25f,1};boat.transform.rotation.y=std::cos(time*.5)>=0?pi/2:-pi/2;
        if(interactions){WaterInteraction wake;wake.position=boat.transform.position;wake.position.y=0;wake.direction={std::cos(time*.5)>=0?1.f:-1.f,0,0};wake.startTime=time-.15;wake.wakeLength=5;wake.radius=.5f;wake.strength=.9f;
            WaterInteraction ring;ring.position={-1,0,3};ring.startTime=std::floor(time);ring.radius=.6f;const std::array values{wake,ring};host.setWaterInteractions(values);
        }else host.setWaterInteractions({});hdr.setEnabled(host.capabilities().hdrPostProcessing);msaa.setEnabled(host.capabilities().msaa4x);
    };
    if(capture.empty())return host.run(callbacks);host.setPaused(true);const auto deadline=host.nowSeconds()+120;bool queued=false;
    while(host.step(callbacks)){if(!host.diagnostics().error.empty())throw std::runtime_error(host.diagnostics().error);if(host.nowSeconds()>deadline)throw std::runtime_error("Water gallery capture timeout");
        if(host.diagnostics().presentedFrames>8&&!queued){host.captureFrame(capture);queued=true;}if(queued&&!host.capturePending()){if(!host.captureError().empty())throw std::runtime_error(host.captureError());host.requestClose();}}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
