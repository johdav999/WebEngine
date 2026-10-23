#include "../examples/terrain_gallery.hpp"
#include "engine/desktop.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
using namespace engine;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: windows_terrain_smoke dawn|d3d12 evidence-directory");const std::string backend=argv[1];check(backend=="dawn"||backend=="d3d12","Unknown backend");
    const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
    DesktopSettings settings;settings.hidden=true;settings.width=960;settings.height=640;settings.backend=backend=="dawn"?DesktopBackend::Dawn:DesktopBackend::Direct3D12;
    DesktopHost host(settings);auto g=terrain_example::makeGallery();
    const auto id=host.application().loadScene(std::move(g.scene));const DesktopView view{g.camera->id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    auto& gui=host.gui().root().add<Button>("");gui.setPosition({12,12});gui.setSize({140,44});gui.style.background={.16f,.32f,.55f,1};
    double time=0;auto step=[&]{check(host.step({},time+=1./60),"Unexpected host close");check(host.diagnostics().error.empty(),host.diagnostics().error.c_str());check(host.captureError().empty(),host.captureError().c_str());};
    auto wait=[&](auto predicate){const auto deadline=host.nowSeconds()+120;while(!predicate()){check(host.nowSeconds()<deadline,"Terrain acceptance timeout");step();}};
    wait([&]{return host.diagnostics().ready;});std::ostringstream phases;bool first=true;
    auto capture=[&](const char* name){g.world->synchronize();for(int i=0;i<3;++i)step();
        // Dawn publishes metrics periodically. Require a report from this scene state.
        if(backend=="dawn"){const auto previous=host.diagnostics().performance;wait([&]{return host.diagnostics().performance!=previous;});}
        host.captureFrame(directory/(std::string(name)+".ppm"));wait([&]{return !host.capturePending();});step();
        if(!first)phases<<',';first=false;phases<<'"'<<name<<"\":{\"renderState\":"<<host.diagnostics().renderState<<",\"performance\":"<<host.diagnostics().performance<<'}';};
    auto reset=[&]{g.reset();host.setQuality(RenderQuality{});host.setShadowsEnabled(true);};
    capture("combined");const auto ground=terrainGround(*g.world,0,0);check(bool(ground),"Missing terrain query");
    auto m=g.defaults;m.layers.heightBlend=0;m.layers.triplanar=false;for(auto& l:m.layers.layers)l.normalScale=0;g.apply(m);capture("linear");
    for(auto& l:m.layers.layers)l.normalScale=.8f;g.apply(m);capture("normals");m.layers.heightBlend=.3f;g.apply(m);capture("height");
    m.layers.heightBlend=0;m.layers.triplanar=true;g.apply(m);capture("triplanar");
    for(unsigned i=0;i<4;++i){m=g.defaults;std::array<unsigned char,4> w{};w[i]=255;m.layers.weights=terrain_example::solid(w);g.apply(m);capture(("layer-"+std::to_string(i)).c_str());}
    m.layers.weights=terrain_example::solid({0,0,0,0});g.apply(m);capture("zero-weights");
    m.layers.weights.reset();g.apply(m);capture("missing-weights");
    m.layers.weights=terrain_example::solid({64,64,64,64});g.apply(m);capture("equal-weights");
    m.layers.layers[0].heightBias=1;g.apply(m);capture("height-bias");
    m=g.defaults;for(auto& l:m.layers.layers){l.normalHeightTexture.reset();l.texture.reset();}g.apply(m);capture("missing-maps");
    m=g.defaults;for(auto& l:m.layers.layers){l.uvScale={-2.f,1.4f};l.uvOffset={.2f,.3f};}m.uvScale={-.8f,1.2f};m.uvOffset={.1f,-.2f};g.apply(m);capture("uv-transform");
    auto clamped=std::make_shared<Texture>(*m.layers.layers[2].texture);clamped->wrapU=clamped->wrapV=TextureWrap::Clamp;m.layers.layers[2].texture=clamped;
    auto clampedDetail=std::make_shared<Texture>(*m.layers.layers[2].normalHeightTexture);clampedDetail->wrapU=clampedDetail->wrapV=TextureWrap::Clamp;m.layers.layers[2].normalHeightTexture=clampedDetail;g.apply(m);capture("clamp");
    m=g.defaults;m.layers.triplanarSharpness=12;m.layers.triplanarScale=1.1f;g.apply(m);capture("projection");
    m=g.defaults;for(auto& l:m.layers.layers)l.roughness=.045f;g.apply(m);capture("roughness");
    m=g.defaults;m.shader=MaterialShader::Unlit;g.apply(m);capture("unlit");
    m=g.defaults;m.layers.enabled=false;g.apply(m);capture("layers-disabled");
    reset();m=g.defaults;m.metallicRoughnessTexture=lighting_example::checker("Inactive surface",2);m.normalTexture=lighting_example::checker("Inactive normals",1);m.emissiveTexture=lighting_example::checker("Inactive emissive",4);m.occlusionTexture=lighting_example::checker("Inactive AO",3);g.apply(m);capture("inactive-conventional-maps");
    reset();g.pillar->transform.scale={-2.5f,4,1.8f};g.pillar->transform.rotation={.15f,.4f,.1f};capture("mirrored");
    m=g.defaults;m.alphaMode=AlphaMode::Blend;m.opacity=.45f;m.doubleSided=true;g.apply(m);capture("transparent");
    m.alphaMode=AlphaMode::Additive;g.world->setFog(FogSettings{});g.apply(m);capture("additive-fog");
    reset();m=g.defaults;m.alphaMode=AlphaMode::Mask;m.doubleSided=true;for(auto& l:m.layers.layers)l.texture=lighting_example::checker("Cutout",0);
    g.pillar->setMaterial(0,MaterialInstance{std::make_shared<const Material>(m)});capture("masked");g.pillar->castsShadow=false;capture("mask-caster-off");
    reset();capture("shadowed");host.setShadowsEnabled(false);capture("no-shadows");host.setShadowsEnabled(true);
    auto q=host.quality();q.directionalCascades=3;q.shadowDistance=45;host.setQuality(q);capture("cascades");
    reset();g.detail(3,0);capture("camera-coarse");g.detail(0,3);capture("shadow-coarse");
    g.detail(-1,0,.1f);capture("auto-coarse");g.detail(-1,0,8);capture("auto-fine");
    const auto after=terrainGround(*g.world,0,0);check(after&&std::abs(after->position.y-ground->position.y)<1e-6f,"Presentation changed terrain ground");
    // Shared boundaries preserve exact unlit coverage for arbitrary adjacent LODs.
    reset();m=g.defaults;m.shader=MaterialShader::Unlit;m.baseColor={1,1,1};for(auto& l:m.layers.layers){l.texture=Texture::white();l.color={1,1,1};l.normalHeightTexture.reset();}g.apply(m);g.pillar->enabled=g.sphere->enabled=false;g.overhead(true);host.setShadowsEnabled(false);
    capture("coverage-full");g.detail(3,0);capture("coverage-coarse");g.detail(4,0);capture("coverage-mixed");
    reset();m=g.defaults;m.layers.triplanarScale=std::numeric_limits<float>::quiet_NaN();bool rejected=false;
    try{g.pillar->setMaterial(0,MaterialInstance{std::make_shared<const Material>(m)});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid layer parameters accepted");capture("invalid-retained");
    m=g.defaults;auto invalid=std::make_shared<Texture>();invalid->width=invalid->height=4;m.layers.layers[0].normalHeightTexture=invalid;rejected=false;
    try{g.pillar->setMaterial(0,MaterialInstance{std::make_shared<const Material>(m)});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Malformed layer pixels accepted");capture("invalid-texture-retained");
    // One CPU texture used in conventional and layered roles must keep semantic views distinct.
    auto shared=lighting_example::checker("Semantic alias",1);auto conventional=std::make_shared<Material>();conventional->baseColorTexture=conventional->metallicRoughnessTexture=conventional->normalTexture=conventional->occlusionTexture=conventional->emissiveTexture=shared;conventional->emissive={.3f,.1f,.05f};
    auto& a=lighting_example::object(*g.world,"Conventional alias",Mesh::cube(),{-4,3,3},{2,2,2},conventional);
    auto layered=std::make_shared<Material>();layered->layers.enabled=true;for(auto& l:layered->layers.layers)l.texture=shared;layered->layers.weights=shared;
    auto& b=lighting_example::object(*g.world,"Layered alias",Mesh::cube(),{4,4,-3},{2,2,2},layered);capture("semantic-alias");a.destroy();b.destroy();g.world->synchronize();
    auto imported=std::make_shared<Mesh>(*FbxImporter::loadFile(std::filesystem::path(DESKTOP_FIXTURE_DIR)/"skeletal_column_binary.fbx"));MorphTarget stretch{"Stretch"};for(const auto& v:imported->vertices)stretch.positions.push_back({.2f*v.position.y,.2f*v.position.y,0});imported->morphTargets.push_back(std::move(stretch));
    auto& animated=g.world->spawn("Animated layers").addComponent<SkeletalMeshComponent>("Animated layers",imported);animated.color={1,1,1};animated.transform.position={-4,2,0};animated.animation().play(0,false);animated.animation().seek(1);animated.animation().pause();animated.setMorphWeight(0,.8f);animated.setMaterial(0,MaterialInstance{std::make_shared<const Material>(g.defaults)});capture("animated-layered");
    m=g.defaults;m.layers.enabled=false;animated.setMaterial(0,MaterialInstance{std::make_shared<const Material>(m)});capture("animated-conventional");
    m=g.defaults;m.alphaMode=AlphaMode::Mask;m.doubleSided=true;for(auto& l:m.layers.layers)l.texture=lighting_example::checker("Animated cutout",0);animated.setMaterial(0,MaterialInstance{std::make_shared<const Material>(m)});capture("animated-mask");animated.destroy();g.world->synchronize();
    reset();q=host.quality();q.post.enabled=true;q.post.bloomStrength=.35f;q.msaaSamples=4;host.setQuality(q);g.world->setSky(SkyBackground{});g.world->setFog(FogSettings{});capture("hdr-msaa-fog");
    host.setWindowSize(803,603);capture("resized");host.setWindowSize(960,640);
    auto& second=g.world->spawn("Second camera").addComponent<CameraComponent>();second.transform.position={-14,11,17};second.lookAt({0,1,0});second.setViewport({.5f,0,.5f,1});g.camera->setViewport({0,0,.5f,1});
    const DesktopView split[]{view,DesktopView{second.id()}};host.setViews(id,split);capture("split");g.camera->setViewport({});host.setViews(id,{&view,1});second.destroy();
    reset();capture("restored");
    std::ofstream out(directory/"report.json");out<<"{\"passed\":true,\"requested\":\""<<backend<<"\",\"cases\":{"<<phases.str()<<"}}\n";check(bool(out),"Cannot write terrain evidence");
    host.requestClose();host.step({},time+=1./60);std::cout<<backend<<" layered terrain acceptance passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
