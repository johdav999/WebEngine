#include "terrain_gallery.hpp"
#include "engine/desktop.hpp"
#include <iostream>
using namespace engine;
int main(int argc,char** argv){try{
    DesktopSettings settings;settings.title="WebEngine | Layered terrain";settings.quality.post.enabled=true;
    std::filesystem::path capture;
    for(int i=1;i<argc;++i){const std::string key=argv[i];
        if(key=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="d3d12")settings.backend=DesktopBackend::Direct3D12;else if(value!="dawn")throw std::invalid_argument("Backend must be dawn or d3d12");}
        else if(key=="--capture"&&i+1<argc){capture=argv[++i];settings.hidden=true;}
        else{std::cout<<"webengine_terrain_windows [--backend dawn|d3d12] [--capture image.ppm]\n";return key=="--help"?0:1;}
    }
    DesktopFiles files;DesktopHost host(settings);auto g=terrain_example::makeGallery();g.world->setSky(SkyBackground{});
    const auto id=host.application().loadScene(std::move(g.scene));const DesktopView view{g.camera->id()};host.setViews(id,{&view,1});host.application().start(id);
    host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024),17));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({16,16});panel.setSize({310,664});panel.setPadding(12);panel.setLayout(VerticalLayout{5});
    panel.add<TextLabel>("LAYERED TERRAIN").setSize({286,24});
    panel.add<TextLabel>("Four materials / independent LOD").setSize({286,24});
    auto checkbox=[&](const char* label,bool value,std::function<void(bool)> action)->Checkbox&{
        auto& item=panel.add<Checkbox>(label);item.setSize({286,30});item.setChecked(value);item.onChange(std::move(action));return item;};
    auto slider=[&](const char* label,float low,float high,float value,std::function<void(float)> action){
        panel.add<TextLabel>(label).setSize({286,24});auto& item=panel.add<Slider>();item.setSize({286,28});item.setAccessibleName(label);item.setRange(low,high,.01f);item.setValue(value);item.onChange(std::move(action));};
    checkbox("World triplanar projection",true,[&](bool v){auto m=g.current;m.layers.triplanar=v;g.apply(m);});
    slider("Normal relief",0,2,.8f,[&](float v){auto m=g.current;for(auto& l:m.layers.layers)l.normalScale=v;g.apply(m);});
    slider("Height blending",0,1,.3f,[&](float v){auto m=g.current;m.layers.heightBlend=v;g.apply(m);});
    unsigned isolated=4;auto& isolate=panel.add<Button>("Blend / isolate next layer");isolate.setSize({286,32});
    isolate.onClick([&]{isolated=(isolated+1)%5;auto m=g.current;if(isolated==4)m.layers.weights=g.defaults.layers.weights;else{std::array<unsigned char,4> w{};w[isolated]=255;m.layers.weights=terrain_example::solid(w);}g.apply(m);});
    int cameraLod=0,shadowLod=0;auto& lodLabel=panel.add<TextLabel>("Camera full / shadow full");lodLabel.setSize({286,24});
    auto updateLod=[&]{g.detail(cameraLod,shadowLod);const char* names[]{"full","LOD 1","LOD 2","LOD 3","mixed"};lodLabel.setText(std::string("Camera ")+names[cameraLod]+" / shadow "+names[shadowLod]);};
    auto& camera=panel.add<Button>("Cycle camera detail");camera.setSize({286,32});camera.onClick([&]{cameraLod=(cameraLod+1)%5;updateLod();});
    auto& shadow=panel.add<Button>("Cycle shadow detail");shadow.setSize({286,32});shadow.onClick([&]{shadowLod=(shadowLod+1)%4;updateLod();});
    checkbox("Overhead view",false,[&](bool v){g.overhead(v);});
    checkbox("Terrain and object shadows",true,[&](bool v){host.setShadowsEnabled(v);});
    checkbox("Height fog",false,[&](bool v){g.world->setFog(v?std::optional<FogSettings>{FogSettings{}}:std::nullopt);});
    auto& hdr=checkbox("HDR post-processing",true,[&](bool v){auto q=host.quality();q.post.enabled=v;host.setQuality(q);});
    auto& msaa=checkbox("Smooth edges (4x MSAA)",false,[&](bool v){auto q=host.quality();q.msaaSamples=v?4:1;host.setQuality(q);});
    auto& quit=panel.add<Button>("Quit / Esc");quit.setSize({286,32});quit.onClick([&]{host.requestClose();});
    host.setEventHandler([&](const DesktopEvent& e){if(e.type==DesktopEventType::Control&&e.down&&!e.repeat&&!e.guiConsumed){
        if(e.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}if(e.control==DesktopControl::Escape){host.requestClose();return true;}}return false;});
    ApplicationFrameCallbacks callbacks;callbacks.present=[&](Scene&,const ApplicationFrame&){hdr.setEnabled(host.capabilities().hdrPostProcessing);msaa.setEnabled(host.capabilities().msaa4x);};
    if(capture.empty())return host.run(callbacks);
    host.setPaused(true);const auto deadline=host.nowSeconds()+120;bool queued=false;
    while(host.step(callbacks)){if(!host.diagnostics().error.empty())throw std::runtime_error(host.diagnostics().error);if(host.nowSeconds()>deadline)throw std::runtime_error("Terrain gallery capture timeout");
        if(host.diagnostics().presentedFrames>8&&!queued){host.captureFrame(capture);queued=true;}
        if(queued&&!host.capturePending()){if(!host.captureError().empty())throw std::runtime_error(host.captureError());host.requestClose();}}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
