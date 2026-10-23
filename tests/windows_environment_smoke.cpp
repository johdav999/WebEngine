#include "../examples/lighting_gallery.hpp"
#include "engine/desktop.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
using namespace engine;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: windows_environment_smoke dawn|d3d12 evidence-directory");
    const std::string backend=argv[1];check(backend=="dawn"||backend=="d3d12","Unknown backend");
    const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
    DesktopSettings settings;settings.hidden=true;settings.width=960;settings.height=640;
    settings.backend=backend=="dawn"?DesktopBackend::Dawn:DesktopBackend::Direct3D12;
    DesktopHost host(settings);auto g=lighting_example::makeGallery();
    auto emissive=std::make_shared<Material>();emissive->shader=MaterialShader::Unlit;emissive->baseColor={.1f,.1f,.1f};emissive->emissive={12,3,.4f};
    lighting_example::object(*g.world,"Bloom source",Mesh::sphere(),{3,3,0},{.6f,.6f,.6f},emissive);
    auto unlit=std::make_shared<Material>();unlit->shader=MaterialShader::Unlit;unlit->baseColor={.15f,.65f,.9f};
    auto& overlay=lighting_example::object(*g.world,"Overlay",Mesh::cube(),{-4,3,2},{.6f,.6f,.6f},unlit);overlay.layer=RenderLayer::Overlay;
    auto additive=std::make_shared<Material>(*unlit);additive->alphaMode=AlphaMode::Additive;additive->opacity=.5f;
    auto& additiveObject=lighting_example::object(*g.world,"Additive",lighting_example::panelMesh(),{4,2,2},{1,1,1},additive);
    const auto id=host.application().loadScene(std::move(g.scene));DesktopView view{g.camera->id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    auto& button=host.gui().root().add<Button>("");button.setPosition({12,12});button.setSize({140,44});button.style.background={.16f,.32f,.55f,1};
    double time=0;auto step=[&]{check(host.step({},time+=1./60),"Unexpected host close");check(host.diagnostics().error.empty(),host.diagnostics().error.c_str());check(host.captureError().empty(),host.captureError().c_str());};
    auto wait=[&](auto predicate){const auto deadline=host.nowSeconds()+120;while(!predicate()){check(host.nowSeconds()<deadline,"Environment acceptance timeout");step();}};
    wait([&]{return host.diagnostics().ready;});check(host.capabilities().hdrPostProcessing,"HDR post capability missing on acceptance adapter");
    std::ostringstream phases;bool first=true;
    auto capture=[&](const char* name){g.world->synchronize();for(int i=0;i<3;++i)step();host.captureFrame(directory/(std::string(name)+".ppm"));wait([&]{return !host.capturePending();});step();
        if(!first)phases<<',';first=false;phases<<'"'<<name<<"\":{\"renderState\":"<<host.diagnostics().renderState<<",\"performance\":"<<host.diagnostics().performance<<'}';};
    SkyBackground sky;FogSettings fog;fog.density=.07f;fog.heightFalloff=.12f;
    capture("baseline");g.world->setSky(sky);capture("gradient");
    sky.geometry=SkyGeometry::Sphere;g.world->setSky(sky);capture("sphere");
    sky.brightness=2;g.world->setSky(sky);capture("bright-sky");
    auto panorama=std::make_shared<Texture>();panorama->width=64;panorama->height=32;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){panorama->pixels.insert(panorama->pixels.end(),{static_cast<unsigned char>(x*4),static_cast<unsigned char>(y*8),static_cast<unsigned char>((x/8)%2?220:30),255});}
    sky={};sky.mode=SkyMode::Panorama;sky.panorama=panorama;g.world->setSky(sky);capture("panorama");
    sky.rotation=1.4f;g.world->setSky(sky);capture("panorama-rotated");
    sky={};sky.mode=SkyMode::Atmosphere;g.world->setSky(sky);capture("atmosphere");
    sky.clouds.enabled=true;sky.clouds.coverage=.8f;g.world->setSky(sky);capture("clouds");
    advanceSky(*g.world,60);capture("clouds-advanced");
    sky=*g.world->sky();sky.dayNight.enabled=true;sky.dayNight.hour=0;g.world->setSky(sky);capture("night");
    g.world->setSky(SkyBackground{});g.world->setFog(fog);capture("fog");
    overlay.layer=RenderLayer::World;capture("overlay-as-world");overlay.layer=RenderLayer::Overlay;
    additiveObject.enabled=false;capture("fog-additive-off");additiveObject.enabled=true;
    auto invalidFog=fog;invalidFog.density=-1;bool rejectedFog=false;
    try{g.world->setFog(invalidFog);}catch(const std::invalid_argument&){rejectedFog=true;}check(rejectedFog,"Invalid fog accepted");capture("invalid-fog-retained");
    auto invalidSky=*g.world->sky();invalidSky.brightness=-1;bool rejectedSky=false;
    try{g.world->setSky(invalidSky);}catch(const std::invalid_argument&){rejectedSky=true;}check(rejectedSky,"Invalid sky accepted");capture("invalid-sky-retained");
    fog.heightFalloff=0;g.world->setFog(fog);capture("uniform-fog");
    fog.startDistance=15;g.world->setFog(fog);capture("fog-start");
    view.fogEnabled=false;host.setViews(id,{&view,1});capture("fog-disabled-view");
    view.skyEnabled=false;host.setViews(id,{&view,1});capture("sky-disabled-view");
    view.skyEnabled=view.fogEnabled=true;host.setViews(id,{&view,1});
    g.camera->setOrthographic(20,.1f,60);capture("orthographic-fog");
    sky={};sky.geometry=SkyGeometry::Sphere;g.world->setSky(sky);capture("orthographic-sphere");
    g.camera->setPerspective(pi/3,.1f,60);g.world->setSky(SkyBackground{});g.world->setFog(std::nullopt);
    auto q=host.quality();q.post.enabled=true;q.post.bloomStrength=0;host.setQuality(q);capture("hdr");
    q.post.bloomStrength=.8f;host.setQuality(q);capture("bloom");
    q.post.bloomThreshold=20;host.setQuality(q);capture("threshold");
    q.post.bloomThreshold=1;q.post.exposure=1;host.setQuality(q);capture("exposure");
    q.post.exposure=0;q.post.saturation=0;host.setQuality(q);capture("grayscale");
    auto invalid=q;invalid.post.exposure=std::numeric_limits<float>::quiet_NaN();bool rejected=false;
    try{host.setQuality(invalid);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid post accepted");capture("invalid-retained");
    q.post.saturation=1;q.post.contrast=1.5f;host.setQuality(q);capture("contrast");
    q.post.contrast=1;q.msaaSamples=4;host.setQuality(q);g.world->setFog(FogSettings{});capture("hdr-msaa-fog");
    host.setWindowSize(803,603);capture("resized");check(host.diagnostics().width==803&&host.diagnostics().height==603,"HDR resize failed");
    q.resolution.scale=.75f;host.setQuality(q);capture("scaled");
    q.resolution.scale=1;host.setQuality(q);host.setWindowSize(960,640);
    auto& second=g.world->spawn("Second camera").addComponent<CameraComponent>();second.transform.position={-11,8,15};second.lookAt({0,1,0});second.setViewport({.5f,0,.5f,1});g.camera->setViewport({0,0,.5f,1});
    const DesktopView split[]{view,DesktopView{second.id(),std::nullopt,false,false}};host.setViews(id,split);capture("split-hdr");
    g.camera->setViewport({});second.setViewport({});second.setVisibilityMask(0);
    const DesktopView preserving{second.id(),std::nullopt,false,false};
    const DesktopView preserved[]{view,preserving};host.setViews(id,preserved);capture("preserved-color");host.setViews(id,{&view,1});capture("single-hdr");second.destroy();g.world->synchronize();
    host.setFullscreen(true);capture("fullscreen");check(host.fullscreen(),"Fullscreen entry failed");host.setFullscreen(false);
    host.setWindowSize(960,640);g.world->setSky(std::nullopt);g.world->setFog(std::nullopt);host.setQuality(RenderQuality{});capture("restored");
    std::ofstream out(directory/"report.json");out<<"{\"passed\":true,\"requested\":\""<<backend<<"\",\"cases\":{"<<phases.str()<<"}}\n";check(bool(out),"Cannot write environment report");
    host.requestClose();host.step({},time+=1./60);std::cout<<backend<<" sky/fog/HDR acceptance passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

