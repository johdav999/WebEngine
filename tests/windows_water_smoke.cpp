#include "../examples/water_gallery.hpp"
#include "engine/desktop.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
using namespace engine;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected invalid candidate rejection");}}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: windows_water_smoke dawn|d3d12 evidence-directory");const std::string backend=argv[1];check(backend=="dawn"||backend=="d3d12","Unknown backend");
    const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
    DesktopSettings settings;settings.hidden=true;settings.width=960;settings.height=640;settings.backend=backend=="dawn"?DesktopBackend::Dawn:DesktopBackend::Direct3D12;
    DesktopHost host(settings);rejects([&]{host.setWaterInteractions({});});auto g=water_example::makeGallery();
    const auto id=host.application().loadScene(std::move(g.scene));const DesktopView view{g.camera->id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    auto& gui=host.gui().root().add<Button>("");gui.setPosition({12,12});gui.setSize({140,44});gui.style.background={.16f,.32f,.55f,1};
    double time=0;auto step=[&]{check(host.step({},time+=1./60),"Unexpected host close");check(host.diagnostics().error.empty(),host.diagnostics().error.c_str());check(host.captureError().empty(),host.captureError().c_str());};
    auto wait=[&](auto predicate){const auto deadline=host.nowSeconds()+120;while(!predicate()){check(host.nowSeconds()<deadline,"Water acceptance timeout");step();}};
    wait([&]{return host.diagnostics().ready;});std::ostringstream phases;bool first=true;
    auto capture=[&](const char* name){g.world->synchronize();for(int i=0;i<3;i++)step();if(backend=="dawn"){const auto previous=host.diagnostics().performance;wait([&]{return host.diagnostics().performance!=previous;});}
        host.captureFrame(directory/(std::string(name)+".ppm"));wait([&]{return !host.capturePending();});step();
        if(!first)phases<<',';first=false;phases<<'"'<<name<<"\":{\"renderState\":"<<host.diagnostics().renderState<<",\"performance\":"<<host.diagnostics().performance<<",\"waterTime\":"<<host.application().clocks().simulation.seconds<<'}';};
    auto reset=[&]{g.reset();host.setWaterInteractions({});host.setQuality(RenderQuality{});host.setShadowsEnabled(true);};
    capture("combined");
    auto m=g.defaults;m.water.refractionStrength=0;g.apply(m);capture("refraction-off");
    m=g.defaults;m.water.foamStrength=0;g.apply(m);capture("foam-off");
    m=g.defaults;m.water.planarReflectionStrength=0;g.apply(m);capture("planar-off");
    m.water.refractionStrength=m.water.foamStrength=m.water.underwaterStrength=0;g.apply(m);capture("surface-only");
    m.water.amplitude=0;m.water.rippleStrength=0;g.apply(m);capture("flat");
    m=g.defaults;m.normalTexture.reset();g.apply(m);capture("no-normal-map");
    m=g.defaults;m.water.refractionDistortion=0;g.apply(m);capture("no-distortion");
    m=g.defaults;m.water.absorption={2,.08f,.02f};g.apply(m);capture("absorption");
    m=g.defaults;m.water.foamWidth=2;m.water.foamColor={1,.05f,.02f};g.apply(m);capture("foam-color-width");
    reset();g.world->environmentIntensity=1;capture("ibl");g.world->environmentRotation=1.6f;capture("ibl-rotated");
    reset();host.setShadowsEnabled(false);capture("no-shadows");host.setShadowsEnabled(true);
    m=g.defaults;m.alphaMode=AlphaMode::Blend;m.opacity=.65f;g.apply(m);capture("transparent");
    m.alphaMode=AlphaMode::Additive;g.apply(m);g.world->setFog(FogSettings{});capture("additive-fog");
    reset();m=g.defaults;m.baseColorTexture=lighting_example::checker("Water cutout",0);m.alphaMode=AlphaMode::Mask;g.apply(m);g.water->castsShadow=true;capture("masked-caster");
    reset();g.water->transform.scale={-1.1f,1.4f,.85f};g.water->transform.rotation={0,.3f,.08f};capture("transformed");
    reset();g.pillar->enabled=false;capture("reflection-object-off");g.pillar->enabled=true;g.cutout->enabled=false;capture("reflection-cutout-off");
    reset();g.world->setFog(FogSettings{});capture("fog-reflection");g.camera->setOrthographic(22,.1f,90);capture("orthographic");
    reset();g.bed->enabled=false;g.sphere->enabled=false;capture("clear-depth");
    reset();g.pillar->transform.position={-2,2,4};g.pillar->transform.scale={2,4,2};capture("foreground");g.pillar->transform.position={-3,1,-2};g.pillar->transform.scale={1,3,1};
    reset();g.water->layer=RenderLayer::Background;capture("background-water");g.water->layer=RenderLayer::Overlay;capture("overlay-water");
    reset();g.underwater(true);capture("underwater");
    m=g.defaults;m.water.underwaterStrength=0;g.apply(m);capture("underwater-off");
    m=g.defaults;m.water.causticsStrength=0;g.apply(m);capture("caustics-off");
    m.water.underwaterFogDensity=0;g.apply(m);capture("absorption-only");
    m.water.absorption={};m.water.underwaterFogDensity=.35f;g.apply(m);capture("scattering-only");
    m.water.underwaterFogDensity=0;m.water.causticsStrength=2;m.water.causticsSpeed=0;g.apply(m);capture("caustics-only");
    m.water.causticsScale=4;m.water.causticsDepth=.6f;g.apply(m);capture("caustics-detail");
    m=g.defaults;m.water.underwaterMaxDistance=1;g.apply(m);capture("underwater-distance");
    g.apply(g.defaults);g.camera->setOrthographic(10,.1f,90);capture("underwater-ortho");
    reset();g.camera->transform.position={10,-.6f,4};g.camera->lookAt({0,-1,-4});capture("outside-footprint");
    reset();g.water->transform.scale={-1.1f,1.4f,.85f};g.water->transform.rotation={0,.3f,.08f};g.underwater(true);capture("underwater-transformed");
    reset();g.underwater(true);g.water->visibilityMask=2;g.camera->setVisibilityMask(1);capture("masked-region");
    reset();m=g.defaults;m.water.amplitude=0;m.water.rippleStrength=0;m.water.planarReflectionStrength=0;m.water.refractionStrength=m.water.foamStrength=0;g.apply(m);capture("interaction-none");
    WaterInteraction ring;ring.position={0,0,0};ring.startTime=-.25;ring.strength=1;ring.radius=1.2f;ring.wavelength=.45f;ring.speed=2;host.setWaterInteractions({&ring,1});capture("ring");
    auto bad=ring;bad.strength=std::numeric_limits<float>::quiet_NaN();rejects([&]{host.setWaterInteractions({&bad,1});});capture("invalid-contact-retained");
    std::array<WaterInteraction,17> tooMany{};rejects([&]{host.setWaterInteractions(tooMany);});
    auto wake=ring;wake.position={2,0,0};wake.direction={1,0,.3f};wake.wakeLength=6;host.setWaterInteractions({&wake,1});capture("wake");
    wake.direction={-1,0,.3f};host.setWaterInteractions({&wake,1});capture("wake-reversed");
    ring.visibilityMask=2;host.setWaterInteractions({&ring,1});g.camera->setVisibilityMask(1);capture("contact-masked");g.camera->setVisibilityMask(0xffffffffu);
    ring.visibilityMask=0xffffffffu;ring.position.y=4;host.setWaterInteractions({&ring,1});capture("contact-elevated");ring.position.y=0;
    ring.startTime=10;host.setWaterInteractions({&ring,1});capture("contact-future");ring.startTime=-10;host.setWaterInteractions({&ring,1});capture("contact-expired");
    reset();m=g.defaults;m.water.speed=1;g.apply(m);capture("waves-before");const auto simulation=host.application().clocks().simulation.seconds;
    host.setPaused(false);for(int i=0;i<60;i++)step();host.setPaused(true);check(host.application().clocks().simulation.seconds>simulation,"Resume did not advance water clock");capture("waves-after");capture("paused-identical");
    reset();auto animatedAsset=std::make_shared<Mesh>(*FbxImporter::loadFile(std::filesystem::path(DESKTOP_FIXTURE_DIR)/"skeletal_column_binary.fbx"));MorphTarget stretch{"Stretch"};for(const auto& v:animatedAsset->vertices)stretch.positions.push_back({.15f*v.position.y,.2f*v.position.y,0});animatedAsset->morphTargets.push_back(std::move(stretch));
    auto& animated=g.world->spawn("Animated reflected rig").addComponent<SkeletalMeshComponent>("Rig",animatedAsset);animated.color={1,1,1};animated.transform.position={-1,1,2};animated.animation().play(0,false);animated.animation().seek(1);animated.animation().pause();animated.setMorphWeight(0,.8f);capture("animated-reflection");
    animated.setMaterial(0,MaterialInstance{std::make_shared<const Material>(g.defaults)});capture("animated-water");animated.destroy();g.world->synchronize();
    reset();auto four=host.quality();four.msaaSamples=4;host.setQuality(four);capture("msaa-four");
    reset();m=g.defaults;m.water.causticsSpeed=0;g.apply(m);g.underwater(true);auto q=host.quality();q.post.enabled=true;q.post.bloomStrength=.3f;q.msaaSamples=4;host.setQuality(q);g.world->setFog(FogSettings{});capture("hdr-msaa-underwater");
    host.setWindowSize(803,603);capture("resized");host.setWindowSize(960,640);
    reset();m=g.defaults;m.water.causticsSpeed=0;g.apply(m);auto& second=g.world->spawn("Underwater camera").addComponent<CameraComponent>();second.transform.position={0,-.6f,4};second.lookAt({0,-1,-4});second.setViewport({.5f,0,.5f,1});g.camera->setViewport({0,0,.5f,1});
    const DesktopView split[]{view,DesktopView{second.id()}};host.setViews(id,split);capture("split");g.camera->setViewport({});host.setViews(id,{&view,1});second.destroy();
    reset();m=g.defaults;m.water.refractionStrength=m.water.foamStrength=0;m.water.underwaterStrength=0;m.water.planarReflectionStrength=0;g.apply(m);capture("released-targets");
    m=g.defaults;m.water.foamWidth=std::numeric_limits<float>::quiet_NaN();rejects([&]{g.apply(m);});capture("invalid-water-retained");
    g.water->enabled=false;capture("no-water");reset();m=g.defaults;m.water.amplitude=0;m.water.rippleStrength=0;m.water.refractionStrength=m.water.foamStrength=m.water.planarReflectionStrength=0;g.apply(m);capture("handoff-control");
    ring.position={0,0,0};ring.visibilityMask=0xffffffffu;ring.startTime=host.application().clocks().simulation.seconds-.25;host.setWaterInteractions({&ring,1});capture("handoff-contact");
    auto next=water_example::makeGallery();next.apply(m);const auto nextId=host.application().loadScene(std::move(next.scene));const DesktopView nextView{next.camera->id()};host.setViews(nextId,{&nextView,1});
    host.application().requestTransition(nextId);step();capture("handoff-cleared");host.application().requestTransition(id,true);step();capture("handoff-return-cleared");
    reset();capture("restored");
    std::ofstream out(directory/"report.json");out<<"{\"passed\":true,\"requested\":\""<<backend<<"\",\"cases\":{"<<phases.str()<<"}}\n";check(bool(out),"Cannot write water evidence");
    host.requestClose();host.step({},time+=1./60);std::cout<<backend<<" water acceptance passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
