#include "../examples/fx_gallery.hpp"
#include "../examples/water_gallery.hpp"
#include "engine/desktop.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace engine;
namespace {void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: windows_fx_smoke dawn|d3d12 evidence-directory");const std::string backend=argv[1];check(backend=="dawn"||backend=="d3d12","Unknown backend");
    const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
    DesktopSettings settings;settings.hidden=true;settings.width=960;settings.height=640;settings.backend=backend=="dawn"?DesktopBackend::Dawn:DesktopBackend::Direct3D12;
    DesktopHost host(settings);fx_example::Gallery g;const auto id=host.application().loadScene(std::move(g.scene));
    auto bind=[&]{std::vector<DesktopView> views;for(auto* camera:g.cameras)views.push_back(DesktopView{camera->id()});host.setViews(id,views);};bind();host.application().start(id);host.setPaused(true);
    auto& button=host.gui().root().add<Button>("");button.setPosition({12,12});button.setSize({140,44});button.style.background={.16f,.32f,.55f,1};
    double time=0;auto step=[&]{check(host.step({},time+=1./60),"Unexpected host close");check(host.diagnostics().error.empty(),host.diagnostics().error.c_str());check(host.captureError().empty(),host.captureError().c_str());};
    auto wait=[&](auto predicate){const auto deadline=host.nowSeconds()+120;while(!predicate()){check(host.nowSeconds()<deadline,"FX acceptance timeout");step();}};
    wait([&]{return host.diagnostics().ready;});std::ostringstream phases;bool first=true;
    auto capture=[&](std::string name){host.application().activeScene()->synchronize();for(int i=0;i<3;++i)step();if(backend=="dawn"){const auto previous=host.diagnostics().performance;wait([&]{return host.diagnostics().performance!=previous;});}
        host.captureFrame(directory/(name+".ppm"));wait([&]{return !host.capturePending();});step();if(!first)phases<<',';first=false;
        phases<<'"'<<name<<"\":{\"renderState\":"<<host.diagnostics().renderState<<",\"performance\":"<<host.diagnostics().performance<<'}';};
    auto fixture=[&](int mode,std::string name=""){g.setup(mode);bind();capture(name.empty()?"mode-"+std::to_string(mode):name);};
    for(int mode=0;mode<=74;++mode)fixture(mode);
    for(int mode=86;mode<=95;++mode)fixture(mode);
    fixture(16,"cached");capture("cached-repeat");
    auto q=host.quality();q.msaaSamples=4;host.setQuality(q);fixture(16,"soft-four");fixture(27,"trail-four");fixture(36,"plane-four");fixture(64,"mesh-four");
    q.post.enabled=true;q.post.bloomStrength=.3f;host.setQuality(q);fixture(24,"hdr-additive");fixture(27,"hdr-trail");fixture(64,"hdr-mesh");fixture(23,"hdr-split");
    host.setWindowSize(803,603);capture("resized");host.setWindowSize(960,640);host.setQuality(RenderQuality{});
    fixture(16,"restored");
    // Definition validation must retain the last valid effect and its live pool.
    auto& emitter=g.fixture->addChild<ParticleEmitterComponent>();auto invalid=std::make_shared<ParticleEmitterDefinition>();invalid->softDistance=-1;
    bool rejected=false;try{emitter.setDefinition(invalid);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid FX definition accepted");emitter.destroy();g.world->synchronize();capture("invalid-retained");
    auto water=water_example::makeGallery();auto d=std::make_shared<ParticleEmitterDefinition>();d->spawnRate=0;d->bursts={{0,1}};d->velocityMin=d->velocityMax={};d->size={{0,3}};d->color={{0,{.1f,.2f,1,.8f}}};d->softDistance=.5f;
    auto& mist=water.world->spawn("Water mist").addComponent<ParticleEmitterComponent>("Mist",d);mist.transform.position={0,1,2};water.world->update(0);
    const auto waterId=host.application().loadScene(std::move(water.scene));const DesktopView waterView{water.camera->id()};host.setViews(waterId,{&waterView,1});host.application().requestTransition(waterId,true);step();capture("water-fx");
    mist.enabled=false;capture("water-only");mist.enabled=true;mist.transform.position={0,-1,1};water.underwater(true);capture("underwater-fx");
    // Scene handoff clears GPU streams without retaining scene-owned components.
    auto next=std::make_unique<Scene>("Empty replacement");auto& camera=next->spawn("Camera").addComponent<CameraComponent>();const auto nextId=host.application().loadScene(std::move(next));const DesktopView view{camera.id()};host.setViews(nextId,{&view,1});host.application().requestTransition(nextId,true);step();
    // The old g.world borrow is now invalid; capture through the host only.
    for(int i=0;i<3;++i)step();host.captureFrame(directory/"handoff.ppm");wait([&]{return !host.capturePending();});
    std::ofstream out(directory/"report.json");out<<"{\"passed\":true,\"requested\":\""<<backend<<"\",\"cases\":{"<<phases.str()<<"}}\n";check(bool(out),"Cannot write FX evidence");
    host.requestClose();host.step({},time+=1./60);std::cout<<backend<<" FX pixels captured\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
