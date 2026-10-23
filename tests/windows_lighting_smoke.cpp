#include "../examples/lighting_gallery.hpp"
#include "engine/desktop.hpp"
#include "engine/particle_emitter_component.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
using namespace engine;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: windows_lighting_smoke dawn|d3d12 evidence-directory");const std::string backend=argv[1];check(backend=="dawn"||backend=="d3d12","Unknown backend");
    const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
    DesktopSettings settings;settings.hidden=true;settings.width=960;settings.height=640;settings.backend=backend=="dawn"?DesktopBackend::Dawn:DesktopBackend::Direct3D12;
    DesktopHost host(settings);auto g=lighting_example::makeGallery();
    const auto id=host.application().loadScene(std::move(g.scene));const DesktopView view{g.camera->id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    double time=0;auto step=[&]{check(host.step({},time+=1./60),"Unexpected host close");check(host.captureError().empty(),host.captureError().c_str());};
    const auto deadline=host.nowSeconds()+120;
    auto wait=[&](auto predicate){while(!predicate()){check(host.nowSeconds()<deadline,"Lighting acceptance timeout");step();}};
    wait([&]{return host.diagnostics().ready;});
    check(host.capabilities().pbr&&host.capabilities().shadowMaps&&host.capabilities().spotLightShadows,"Static lighting capabilities missing");
    std::ostringstream phases;bool firstPhase=true;
    auto capture=[&](const char* name){g.world->synchronize();for(int i=0;i<3;++i)step();host.captureFrame(directory/(std::string(name)+".ppm"));wait([&]{return !host.capturePending();});step();
        if(!firstPhase)phases<<',';firstPhase=false;phases<<'"'<<name<<"\":{\"renderState\":"<<host.diagnostics().renderState<<",\"performance\":"<<host.diagnostics().performance<<'}';};
    auto reset=[&]{g.reset();host.setShadowsEnabled(true);host.setQuality(RenderQuality{});};
    capture("baseline");const auto baseline=host.diagnostics();
    reset();MaterialInstance rough{g.goldMaterial};rough.roughness=.95f;g.gold->setMaterial(0,rough);capture("rough");
    reset();MaterialInstance normal{g.mappedMaterial};normal.normalScale=0.f;g.mapped->setMaterial(0,normal);capture("normal-off");
    reset();MaterialInstance surface{g.mappedMaterial};surface.metallicRoughnessTexture=std::shared_ptr<const Texture>{};g.mapped->setMaterial(0,surface);capture("surface-off");
    reset();MaterialInstance ao{g.mappedMaterial};ao.occlusionStrength=0.f;g.mapped->setMaterial(0,ao);capture("ao-off");
    reset();MaterialInstance emission{g.mappedMaterial};emission.emissive=Vec3{};g.mapped->setMaterial(0,emission);capture("emission-off");
    reset();g.world->environmentIntensity=0;capture("environment-off");
    reset();g.world->environmentRotation=1.5f;capture("environment-rotated");
    reset();g.spot->enabled=false;capture("spot-off");reset();g.point->enabled=false;capture("point-off");
    reset();host.setShadowsEnabled(false);capture("shadows-off");
    reset();g.blocker->castsShadow=false;capture("caster-off");reset();g.floor->receivesShadow=false;capture("receiver-off");
    reset();MaterialInstance opaque{g.maskMaterial};opaque.alphaMode=AlphaMode::Opaque;g.cutout->setMaterial(0,opaque);capture("mask-opaque");
    reset();MaterialInstance glass{g.glassMaterial};glass.alphaMode=AlphaMode::Opaque;g.glass->setMaterial(0,glass);capture("glass-opaque");
    reset();g.cutout->transform.scale.x*=-1;capture("mirrored");
    reset();MaterialInstance winding{g.maskMaterial};winding.shader=MaterialShader::Unlit;winding.baseColorTexture=Texture::white();winding.alphaMode=AlphaMode::Opaque;winding.doubleSided=false;
    g.cutout->setMaterial(0,winding);capture("winding-front");g.cutout->transform.scale.x*=-1;capture("winding-mirrored");
    g.cutout->transform.scale.x*=-1;g.cutout->transform.rotation={0,pi,0};capture("winding-culled");
    winding.doubleSided=true;g.cutout->setMaterial(0,winding);capture("winding-double");g.cutout->transform.rotation={};
    reset();auto q=host.quality();q.directionalCascades=3;q.shadowDistance=40;host.setQuality(q);capture("cascades");
    reset();q=host.quality();q.maxShadowMaps=0;host.setQuality(q);capture("zero-maps");
    reset();g.sun->intensity=0;g.point->enabled=false;capture("spot-shadow");host.setShadowsEnabled(false);capture("spot-no-shadow");
    reset();q=host.quality();q.shadowResolution=512;q.shadowFilterTaps=1;host.setQuality(q);capture("shadow-budget");
    reset();q=host.quality();q.msaaSamples=4;host.setQuality(q);capture("msaa4");check(host.diagnostics().effectiveMsaaSamples==(host.capabilities().msaa4x?4u:1u),"MSAA request was ignored");
    host.setWindowSize(800,600);capture("msaa4-resized");check(host.diagnostics().width==800&&host.diagnostics().height==600,"MSAA resize failed");
    q.msaaSamples=1;host.setQuality(q);capture("msaa1-resized");
    host.setWindowSize(960,640);reset();q=host.quality();q.msaaSamples=4;q.directionalCascades=3;host.setQuality(q);
    auto& second=g.world->spawn("Second camera").addComponent<CameraComponent>();second.transform.position={-11,8,15};second.lookAt({0,1,0});second.setViewport({.5f,0,.5f,1});g.camera->setViewport({0,0,.5f,1});
    const DesktopView split[]{view,DesktopView{second.id()}};host.setViews(id,split);capture("split-msaa4");
    g.camera->setViewport({});host.setViews(id,{&view,1});second.destroy();g.world->synchronize();
    host.setWindowSize(960,640);reset();capture("restored");
    // Particle support composes existing lighting and can be removed cleanly.
    if(backend=="d3d12"){
    auto& particles=g.world->spawn("Supported particles").addComponent<ParticleEmitterComponent>();particles.play();g.world->update(.1);
    step();particles.destroy();
    reset();capture("particles-restored");}
    std::ofstream out(directory/"report.json");out<<"{\"passed\":true,\"requested\":\""<<backend<<"\",\"renderState\":"<<host.diagnostics().renderState<<",\"performance\":"<<baseline.performance<<",\"cases\":{"<<phases.str()<<"}}\n";check(bool(out),"Cannot write lighting evidence");
    host.requestClose();host.step({},time+=1./60);std::cout<<backend<<" static materials/lighting/shadows/MSAA passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
