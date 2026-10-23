#include "lighting_gallery.hpp"
#include "engine/desktop.hpp"
#include <iostream>
using namespace engine;
int main(int argc,char** argv){try{
    DesktopSettings settings;settings.title="WebEngine | Lighting, environment and post-processing";settings.quality.post.enabled=true;
    bool hidden=false;std::filesystem::path capture;
    for(int i=1;i<argc;++i){const std::string key=argv[i];
        if(key=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="d3d12")settings.backend=DesktopBackend::Direct3D12;else if(value!="dawn")throw std::invalid_argument("Backend must be dawn or d3d12");}
        else if(key=="--capture"&&i+1<argc){capture=argv[++i];hidden=true;}
        else {std::cout<<"webengine_lighting_windows [--backend dawn|d3d12] [--capture image.ppm]\n";return key=="--help"?0:1;}
    }
    settings.hidden=hidden;DesktopHost host(settings);auto gallery=lighting_example::makeGallery();
    SkyBackground sky;sky.mode=SkyMode::Atmosphere;gallery.world->setSky(sky);
    auto glow=std::make_shared<Material>();glow->shader=MaterialShader::Unlit;glow->baseColor={.1f,.1f,.1f};glow->emissive={12,3,.4f};
    lighting_example::object(*gallery.world,"Bloom source",Mesh::sphere(),{3,3,0},{.6f,.6f,.6f},glow);
    const auto id=host.application().loadScene(std::move(gallery.scene));const DesktopView view{gallery.camera->id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    DesktopFiles files;const auto font=files.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024);host.gui().setFont(GuiFont::loadTrustedTrueType(font,17));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({16,16});panel.setSize({324,424});panel.setPadding(12);panel.setLayout(VerticalLayout{6});
    panel.add<TextLabel>("STATIC MATERIALS / LIGHTING").setSize({300,24});
    auto checkbox=[&](const char* title,bool initial,std::function<void(bool)> action)->Checkbox&{
        auto& box=panel.add<Checkbox>(title);box.setSize({300,30});box.setChecked(initial);box.onChange(std::move(action));return box;};
    checkbox("Cast shadows",true,[&](bool value){host.setShadowsEnabled(value);});
    auto& msaa=checkbox("Smooth edges (4x MSAA)",false,[&](bool value){auto q=host.quality();q.msaaSamples=value?4:1;host.setQuality(q);});msaa.setEnabled(host.capabilities().msaa4x);
    checkbox("Three sun cascades",false,[&](bool value){auto q=host.quality();q.directionalCascades=value?3:1;q.shadowDistance=40;host.setQuality(q);});
    checkbox("Spotlight",true,[&](bool value){gallery.spot->enabled=value;gallery.world->synchronize();});
    checkbox("Point light",true,[&](bool value){gallery.point->enabled=value;gallery.world->synchronize();});
    checkbox("Environment reflections",true,[&](bool value){gallery.world->environmentIntensity=value?.7f:0;});
    checkbox("Normal map",true,[&](bool value){auto m=*gallery.mapped->materialOverrides[0];m.normalScale=value?1.f:0.f;gallery.mapped->setMaterial(0,m);gallery.world->synchronize();});
    auto& rough=panel.add<Slider>();rough.setSize({300,30});rough.setRange(.045f,1,.01f);rough.setValue(.12f);rough.setAccessibleName("Gold roughness");rough.onChange([&](float value){MaterialInstance m{gallery.goldMaterial};m.roughness=value;gallery.gold->setMaterial(0,m);gallery.world->synchronize();});
    panel.add<TextLabel>("Gold roughness / F11 fullscreen").setSize({300,24});
    auto& quit=panel.add<Button>("Quit");quit.setSize({300,32});quit.onClick([&]{host.requestClose();});
    auto& environment=host.gui().root().add<Panel>();environment.setSize({324,444});environment.setPadding(12);environment.setLayout(VerticalLayout{6});
    environment.add<TextLabel>("ENVIRONMENT / POST").setSize({300,24});
    auto environmentBox=[&](const char* title,bool initial,std::function<void(bool)> action)->Checkbox&{
        auto& box=environment.add<Checkbox>(title);box.setSize({300,30});box.setChecked(initial);box.onChange(std::move(action));return box;};
    bool skyEnabled=true;
    auto publishSky=[&]{gallery.world->setSky(skyEnabled?std::optional<SkyBackground>{sky}:std::nullopt);};
    environmentBox("Sky background",true,[&](bool value){skyEnabled=value;publishSky();});
    environmentBox("Sphere geometry",false,[&](bool value){sky.geometry=value?SkyGeometry::Sphere:SkyGeometry::Fullscreen;publishSky();});
    environmentBox("Atmosphere",true,[&](bool value){sky.mode=value?SkyMode::Atmosphere:SkyMode::Gradient;publishSky();});
    environmentBox("Clouds",false,[&](bool value){sky.clouds.enabled=value;publishSky();});
    environmentBox("Height fog",false,[&](bool value){gallery.world->setFog(value?std::optional<FogSettings>{FogSettings{}}:std::nullopt);});
    auto& hdr=environmentBox("HDR post-processing",true,[&](bool value){auto q=host.quality();q.post.enabled=value;host.setQuality(q);});
    auto& bloom=environmentBox("Bloom",true,[&](bool value){auto q=host.quality();q.post.bloomStrength=value?.8f:0;host.setQuality(q);});
    environmentBox("Grayscale",false,[&](bool value){auto q=host.quality();q.post.saturation=value?0.f:1.f;host.setQuality(q);});
    environment.add<TextLabel>("Exposure (stops)").setSize({300,24});
    auto& exposure=environment.add<Slider>();exposure.setSize({300,30});exposure.setRange(-3,3,.1f);exposure.setValue(0);exposure.setAccessibleName("Exposure");exposure.onChange([&](float value){auto q=host.quality();q.post.exposure=value;host.setQuality(q);});
    environment.add<TextLabel>("Post effects preserve GUI colors").setSize({300,24});
    auto initial=host.quality();initial.post.bloomStrength=.8f;host.setQuality(initial);
    host.setEventHandler([&](const DesktopEvent& event){if(event.type==DesktopEventType::Control&&event.down&&!event.repeat&&!event.guiConsumed){if(event.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}if(event.control==DesktopControl::Escape){host.requestClose();return true;}}return false;});
    ApplicationFrameCallbacks callbacks;callbacks.present=[&](Scene&,const ApplicationFrame&){msaa.setEnabled(host.capabilities().msaa4x);hdr.setEnabled(host.capabilities().hdrPostProcessing);bloom.setEnabled(host.capabilities().hdrPostProcessing);environment.setPosition({std::max(352.f,host.size().width-340),16});};
    if(capture.empty())return host.run(callbacks);
    const auto deadline=host.nowSeconds()+60;bool queued=false;
    while(host.step(callbacks)){if(host.nowSeconds()>deadline)throw std::runtime_error("Gallery capture timed out");
        if(host.diagnostics().presentedFrames>8&&!queued){host.captureFrame(capture);queued=true;}
        if(queued&&!host.capturePending()){if(!host.captureError().empty())throw std::runtime_error(host.captureError());host.requestClose();}}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
