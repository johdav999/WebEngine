#include "../examples/lighting_gallery.hpp"
#include "../examples/water_gallery.hpp"
#include "engine/desktop.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace engine;
namespace {
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
std::uint64_t generation(const DesktopHost& host){const auto s=host.diagnostics().renderState;const auto at=s.find("\"generation\":");check(at!=std::string::npos,"Missing GPU generation");return std::stoull(s.substr(at+13));}
std::shared_ptr<Texture> encodedTexture(){
    auto t=std::make_shared<Texture>();t->name="Authored BC7 with magenta RGBA fallback";t->width=8;t->height=4;t->pixels.resize(8*4*4);
    for(std::size_t i=0;i<t->pixels.size();i+=4){t->pixels[i]=t->pixels[i+2]=t->pixels[i+3]=255;}
    for(auto semantic:{TextureSemantic::Color,TextureSemantic::Data,TextureSemantic::Normal}){
        std::vector<std::uint8_t> block(16);unsigned bit=0;
        auto write=[&](unsigned value,unsigned count){for(unsigned i=0;i<count;++i,++bit)if(value&(1u<<i))block[bit/8]|=std::uint8_t(1u<<(bit%8));};
        write(64,7);for(int c=0;c<3;++c)for(int endpoint=0;endpoint<2;++endpoint)write(semantic==TextureSemantic::Color?0:semantic==TextureSemantic::Normal&&c==2?127:63,7);
        write(127,7);write(127,7);write(1,1);write(1,1);
        CompressedTexture v;v.semantic=semantic;
        for(auto [w,h]:{std::pair{8u,4u},{4u,2u},{2u,1u},{1u,1u}}){TextureMip level{w,h,{}};for(unsigned i=0;i<((w+3)/4)*((h+3)/4);++i)level.pixels.insert(level.pixels.end(),block.begin(),block.end());v.levels.push_back(std::move(level));}
        v.validate(t->width,t->height);t->compressed.push_back(std::move(v));
    }
    // A structurally valid unsupported format precedes the supported candidates.
    auto unsupported=t->compressed.front();unsupported.format=TextureCompression::ETC2RGBA;t->compressed.insert(t->compressed.begin(),std::move(unsupported));return t;
}
}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: windows_performance_smoke dawn|d3d12 evidence-directory");const std::string backend=argv[1];check(backend=="dawn"||backend=="d3d12","Unknown backend");
    const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
    DesktopSettings settings;settings.hidden=true;settings.width=960;settings.height=640;settings.allowGraphicsFaultInjection=true;settings.backend=backend=="dawn"?DesktopBackend::Dawn:DesktopBackend::Direct3D12;
    DesktopHost host(settings);auto owned=std::make_unique<Scene>("Instancing and compression");auto* scene=owned.get();scene->ambient={.18f,.18f,.18f};
    auto& camera=scene->spawn("Camera").addComponent<CameraComponent>();camera.transform.position={17,15,22};camera.lookAt({0,0,0});camera.farPlane=80;
    auto& sun=scene->spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);sun.direction={-.7f,-1,-.4f};sun.intensity=2;sun.shadow.enabled=true;sun.shadow.halfExtent=18;sun.shadow.halfDepth=40;
    auto plain=std::make_shared<Material>();plain->roughness=.7f;const auto shared=Mesh::cube();
    std::vector<MeshComponent*> grid;std::vector<std::shared_ptr<const Mesh>> individual;
    for(int z=0;z<16;++z)for(int x=0;x<16;++x){auto& c=lighting_example::object(*scene,"Shared instance",shared,{(x-7.5f)*1.3f,.8f,(z-7.5f)*1.3f},{.65f,.8f,.65f},plain);
        c.allowInstanceReordering=true;c.color={.2f+x*.04f,.2f+z*.035f,.8f};if((x+z)%7==0)c.transform.scale.x=-.65f;c.receivesShadow=(x+z)%3!=0;c.castsShadow=(x+z)%9!=0;
        grid.push_back(&c);individual.push_back(std::make_shared<const Mesh>(*shared));}
    auto& floor=lighting_example::object(*scene,"Floor",Mesh::plane(30),{0,-.1f,0},{1,1,1},lighting_example::material({.25f,.3f,.35f}));floor.castsShadow=false;
    const auto id=host.application().loadScene(std::move(owned));const DesktopView view{camera.id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    DesktopFiles files;host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf",16*1024*1024),17));auto& label=host.gui().root().add<TextLabel>("GPU recovery / shared scene");label.setPosition({12,12});label.setSize({300,40});
    double time=0;auto step=[&]{check(host.step({},time+=1./60),"Unexpected host close");check(host.diagnostics().error.empty(),host.diagnostics().error.c_str());};
    auto wait=[&](auto predicate){const auto deadline=host.nowSeconds()+120;while(!predicate()){check(host.nowSeconds()<deadline,"Performance acceptance timeout");step();}};
    wait([&]{return host.diagnostics().ready;});std::ostringstream phases;bool first=true;
    auto capture=[&](std::string name){host.application().activeScene()->synchronize();for(int i=0;i<4;++i)step();if(backend=="dawn"){const auto previous=host.diagnostics().performance;wait([&]{return host.diagnostics().performance!=previous;});}
        host.captureFrame(directory/(name+".ppm"));wait([&]{return !host.capturePending();});check(host.captureError().empty(),host.captureError().c_str());step();if(!first)phases<<',';first=false;
        phases<<'"'<<name<<"\":{\"renderState\":"<<host.diagnostics().renderState<<",\"performance\":"<<host.diagnostics().performance<<",\"simulationSeconds\":"<<host.application().clocks().simulation.seconds<<'}';};
    auto instances=[&](bool enabled){for(std::size_t i=0;i<grid.size();++i)grid[i]->mesh=enabled?shared:individual[i];};
    auto compare=[&](const char* name){instances(true);capture(std::string(name)+"-instanced");instances(false);capture(std::string(name)+"-individual");};
    host.setShadowsEnabled(false);compare("color");host.setShadowsEnabled(true);compare("shadow");
    auto q=host.quality();q.post.enabled=true;q.msaaSamples=4;host.setQuality(q);compare("hdr-four");q=RenderQuality{};host.setQuality(q);
    instances(true);grid[127]->transform.position.y+=1;capture("changed-object");capture("cached-objects");grid[127]->transform.position.y-=1;
    // Distinct complete bindings cannot merge, although the geometry is shared.
    auto alternate=std::make_shared<Material>(*plain);alternate->roughness=.1f;for(std::size_t i=0;i<grid.size();++i)if(i%2)grid[i]->setMaterial(0,MaterialInstance{alternate});compare("material-split");
    auto source=encodedTexture();auto compressed=std::make_shared<Material>(*plain);compressed->baseColorTexture=source;compressed->normalTexture=source;compressed->metallicRoughnessTexture=source;
    for(auto* c:grid)c->setMaterial(0,MaterialInstance{compressed});instances(true);capture("bc7");q.compressedTextures=false;host.setQuality(q);capture("rgba");q.compressedTextures=true;host.setQuality(q);capture("bc7-restored");
    auto malformed=std::make_shared<Texture>(*source);malformed->compressed.back().levels.back().pixels.pop_back();auto bad=std::make_shared<Material>(*compressed);bad->baseColorTexture=malformed;
    bool rejected=false;try{if(backend=="d3d12"){grid[0]->setMaterial(0,MaterialInstance{bad});host.setViews(id,{&view,1});step();}else malformed->compressed.back().validate(malformed->width,malformed->height);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Malformed compressed mip was accepted");grid[0]->setMaterial(0,MaterialInstance{compressed});capture("invalid-retained");
    // Genuine selected-device removal, with an unsubmitted readback to cancel.
    const auto before=generation(host);const auto clock=host.application().clocks().simulation.seconds;capture("before-recovery");if(backend!="dawn")host.captureFrame(directory/"cancelled.ppm");check(host.simulateGraphicsLoss(),"Real device removal unavailable");
    wait([&]{return host.diagnostics().ready&&generation(host)>before;});if(backend!="dawn")check(!host.capturePending()&&!host.captureError().empty(),"Recovery did not cancel pending readback");check(host.application().clocks().simulation.seconds==clock,"Recovery advanced paused simulation");check(host.application().activeScene()==scene,"Recovery replaced scene");capture("after-recovery");
    host.setWindowSize(803,603);capture("resized");host.setWindowSize(960,640);
    auto& second=scene->spawn("Second camera").addComponent<CameraComponent>();second.transform=camera.transform;second.setViewport({.5f,0,.5f,1});camera.setViewport({0,0,.5f,1});const DesktopView split[]{view,DesktopView{second.id()}};host.setViews(id,split);capture("split");
    const auto nextGeneration=generation(host);check(host.simulateGraphicsLoss(),"Second removal unavailable");second.destroy();scene->synchronize();camera.setViewport({});wait([&]{return host.diagnostics().ready&&generation(host)>nextGeneration;});host.setViews(id,{&view,1});capture("removed-camera");
    // Water inputs, HDR/MSAA, animation palettes and FX caches also rebuild lazily.
    auto water=water_example::makeGallery();
    auto rig=std::make_shared<Mesh>(*FbxImporter::loadFile(std::filesystem::path(DESKTOP_FIXTURE_DIR)/"skeletal_column_binary.fbx"));MorphTarget morph{"Lean"};for(const auto& v:rig->vertices)morph.positions.push_back({.12f*v.position.y,.05f*v.position.y,0});rig->morphTargets.push_back(std::move(morph));
    auto& animated=water.world->spawn("Recovery rig").addComponent<SkeletalMeshComponent>("Rig",rig);animated.transform.position={-1,1,2};animated.animation().play(0,false);animated.animation().seek(1);animated.animation().pause();animated.setMorphWeight(0,.6f);
    auto fx=std::make_shared<ParticleEmitterDefinition>();fx->spawnRate=0;fx->bursts={{0,1}};fx->velocityMin=fx->velocityMax={};fx->softDistance=.5f;fx->size={{0,3}};auto& mist=water.world->spawn("Recovery FX").addComponent<ParticleEmitterComponent>("Mist",fx);mist.transform.position={0,1,2};water.world->update(0);
    const auto waterId=host.application().loadScene(std::move(water.scene));const DesktopView waterView{water.camera->id()};host.setViews(waterId,{&waterView,1});host.application().requestTransition(waterId,true);step();
    WaterInteraction ring;ring.startTime=-.25;host.setWaterInteractions({&ring,1});q.post.enabled=true;q.msaaSamples=4;host.setQuality(q);capture("water-before");const auto waterGeneration=generation(host);check(host.simulateGraphicsLoss(),"Water device removal unavailable");
    wait([&]{return host.diagnostics().ready&&generation(host)>waterGeneration;});capture("water-after");
    for(int i=0;i<125;++i)step();const auto runningGeneration=generation(host);const auto runningClock=host.application().clocks().simulation.seconds;host.setPaused(false);check(host.simulateGraphicsLoss(),"Running removal unavailable");
    wait([&]{return host.diagnostics().ready&&generation(host)>runningGeneration;});check(host.application().state()==ApplicationState::Running,"Recovery changed explicit running state");check(host.application().clocks().simulation.seconds==runningClock,"Recovery caught up unavailable GPU time");host.setPaused(true);capture("running-recovered");
    const auto finalGeneration=generation(host);check(host.simulateGraphicsLoss(),"Shutdown removal unavailable");host.requestClose();host.step({},time+=1./60);check(host.application().state()==ApplicationState::Stopped,"Recovery shutdown revived application");
    std::ofstream out(directory/"report.json");out<<"{\"passed\":true,\"requested\":\""<<backend<<"\",\"finalGeneration\":"<<finalGeneration<<",\"cases\":{"<<phases.str()<<"}}\n";check(bool(out),"Cannot write performance evidence");std::cout<<backend<<" recovery/instancing/compression acceptance passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
