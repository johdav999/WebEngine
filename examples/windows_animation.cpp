#include "deformation_gallery.hpp"
#include "engine/desktop.hpp"
#include <iostream>
using namespace engine;
int main(int argc,char** argv){try{
    DesktopSettings settings;settings.title="WebEngine | Animated meshes";settings.quality.post.enabled=true;
    std::filesystem::path capture;
    for(int i=1;i<argc;++i){const std::string key=argv[i];
        if(key=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="d3d12")settings.backend=DesktopBackend::Direct3D12;else if(value!="dawn")throw std::invalid_argument("Backend must be dawn or d3d12");}
        else if(key=="--capture"&&i+1<argc){capture=argv[++i];settings.hidden=true;}
        else {std::cout<<"webengine_animation_windows [--backend dawn|d3d12] [--capture image.ppm]\n";return key=="--help"?0:1;}
    }
    DesktopFiles files;const auto bytes=files.readAsset("animation.fbx",16*1024*1024);
    DesktopHost host(settings);auto gallery=deformation_example::makeGallery(FbxImporter::loadMemory(bytes));
    gallery.world->setSky(SkyBackground{});gallery.a->animation().play(0);gallery.b->animation().play(1);
    gallery.a->setMorphWeight(0,.45f);gallery.morph->setMorphWeight(1,.7f);
    const auto id=host.application().loadScene(std::move(gallery.scene));const DesktopView view{gallery.camera->id()};host.setViews(id,{&view,1});host.application().start(id);
    const auto font=files.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024);host.gui().setFont(GuiFont::loadTrustedTrueType(font,17));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({16,16});panel.setSize({324,576});panel.setPadding(12);panel.setLayout(VerticalLayout{6});
    panel.add<TextLabel>("ANIMATED MESHES").setSize({300,24});
    panel.add<TextLabel>("Two players / one shared rig").setSize({300,24});
    auto checkbox=[&](const char* name,bool value,std::function<void(bool)> action)->Checkbox&{
        auto& box=panel.add<Checkbox>(name);box.setSize({300,30});box.setChecked(value);box.onChange(std::move(action));return box;};
    auto& play=checkbox("Play animation",true,[&](bool value){for(auto* mesh:{gallery.a,gallery.b}){if(value)mesh->animation().resume();else mesh->animation().pause();}});
    auto& opposite=panel.add<Button>("Switch first clip");opposite.setSize({300,32});std::size_t clip=0;
    opposite.onClick([&]{clip=1-clip;gallery.a->animation().play(clip);play.setChecked(true);gallery.b->animation().resume();});
    auto& blend=panel.add<Button>("Blend both clips");blend.setSize({300,32});blend.onClick([&]{gallery.a->animation().blend(0,1,.5f);gallery.b->animation().resume();play.setChecked(true);});
    auto slider=[&](const char* name,float low,float high,float value,std::function<void(float)> action)->Slider&{
        panel.add<TextLabel>(name).setSize({300,24});auto& item=panel.add<Slider>();item.setSize({300,30});item.setRange(low,high,.01f);item.setValue(value);item.setAccessibleName(name);item.onChange(std::move(action));return item;};
    auto& phase=slider("Pose (pauses both players)",0,1,0,[&](float value){for(auto* mesh:{gallery.a,gallery.b})mesh->animation().pause();
        gallery.a->animation().seek(gallery.asset->skeleton->clips[clip].duration*value);play.setChecked(false);});
    slider("First player stretch",-.5f,1.5f,.45f,[&](float value){gallery.a->setMorphWeight(0,value);});
    slider("First player lean",-.5f,1.5f,0,[&](float value){gallery.a->setMorphWeight(1,value);});
    checkbox("Animated shadows",true,[&](bool value){host.setShadowsEnabled(value);});
    checkbox("Height fog",false,[&](bool value){gallery.world->setFog(value?std::optional<FogSettings>{FogSettings{}}:std::nullopt);});
    auto& hdr=checkbox("HDR post-processing",true,[&](bool value){auto quality=host.quality();quality.post.enabled=value;host.setQuality(quality);});
    auto& msaa=checkbox("Smooth edges (4x MSAA)",false,[&](bool value){auto quality=host.quality();quality.msaaSamples=value?4:1;host.setQuality(quality);});
    auto& quit=panel.add<Button>("Quit / Esc");quit.setSize({300,32});quit.onClick([&]{host.requestClose();});
    host.setEventHandler([&](const DesktopEvent& event){if(event.type==DesktopEventType::Control&&event.down&&!event.repeat&&!event.guiConsumed){
        if(event.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}if(event.control==DesktopControl::Escape){host.requestClose();return true;}}return false;});
    ApplicationFrameCallbacks callbacks;callbacks.present=[&](Scene&,const ApplicationFrame&){hdr.setEnabled(host.capabilities().hdrPostProcessing);msaa.setEnabled(host.capabilities().msaa4x);};
    if(capture.empty())return host.run(callbacks);
    deformation_example::pose(*gallery.a,0,.5);deformation_example::pose(*gallery.b,1,.75);phase.setValue(.5f);play.setChecked(false);host.setPaused(true);
    const auto deadline=host.nowSeconds()+120;bool queued=false;
    while(host.step(callbacks)){if(host.nowSeconds()>deadline)throw std::runtime_error("Animation gallery capture timeout");
        if(host.diagnostics().presentedFrames>8&&!queued){host.captureFrame(capture);queued=true;}
        if(queued&&!host.capturePending()){if(!host.captureError().empty())throw std::runtime_error(host.captureError());host.requestClose();}}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
