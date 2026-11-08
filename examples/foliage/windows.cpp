#include "scene.hpp"
#include "engine/desktop.hpp"
#include "engine/performance.hpp"
#include <fstream>
int main(int argc,char** argv){try{
    using namespace engine;DesktopSettings settings;settings.title="WebEngine foliage clearing";settings.width=1100;settings.height=750;
    bool smoke=false,cpu=false,benchmark=false;std::filesystem::path output=".";
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--d3d12")settings.backend=DesktopBackend::Direct3D12;
        else if(arg=="--cpu")cpu=true;else if(arg=="--benchmark")benchmark=true;
        else if(arg=="--smoke"&&i+1<argc){smoke=true;output=argv[++i];settings.hidden=true;}else throw std::invalid_argument("Use --d3d12, --cpu, --benchmark or --smoke output-directory");}
    settings.allowGraphicsFaultInjection=smoke;
    foliage_example::Demo demo;DesktopHost host(settings);const auto id=host.application().loadScene(demo.make(false,cpu,benchmark));
    DesktopView view{demo.camera->id()};host.setViews(id,{&view,1});host.application().start(id);
    DesktopFileSettings fs;fs.application="FoliageDemo";if(smoke)fs.userDirectory=std::filesystem::absolute(output);DesktopFiles files(fs);
    host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"),18));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({12,12});panel.setSize({250,345});panel.setPadding(8);panel.setLayout(VerticalLayout{4});
    panel.add<TextLabel>("Foliage clearing").setSize({230,25});auto& status=panel.add<TextLabel>("Ready");status.setSize({230,25});
    auto command=[&](int n){try{
        if(n==6)host.setPaused(host.application().state()!=ApplicationState::Paused);
        else if(n==4){demo.command(4);files.writeUser(DesktopStorage::Saves,"removed.wefo",std::as_bytes(std::span(demo.saved)),3000000);}
        else if(n==5){auto bytes=files.readUser(DesktopStorage::Saves,"removed.wefo",3000000);demo.foliage->loadRemoved(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));demo.advance(0);}
        else demo.command(n);status.setText("Ready");return true;
    }catch(const std::exception& e){status.setText(e.what());return false;}};
    const std::array<const char*,7> labels={"Toggle wind","Harvest nearest plant","Move visual focus","Regrow plants","Save removals","Restore removals","Pause / resume"};
    for(int i=0;i<7;++i){auto& button=panel.add<Button>(labels[i]);button.setSize({230,30});button.onClick([&,i]{command(i);});}
    std::vector<double> windTimes;ApplicationFrameCallbacks callbacks;callbacks.afterSimulation=[&](Scene&,const ApplicationFrame& f){const auto start=host.nowSeconds();demo.advance(f.simulation.delta);if(benchmark)windTimes.push_back((host.nowSeconds()-start)*1000);};
    if(!smoke)return host.run(callbacks);
    std::filesystem::create_directories(output);const auto deadline=host.nowSeconds()+180;
    auto tick=[&]{
        // Hidden acceptance runs deliberately exercise rendering without native
        // focus; ordinary interactive hosts retain their lifecycle suspension.
        if(host.application().suspended())host.application().setSuspended(false);
        if(!host.step(callbacks))throw std::runtime_error("Foliage smoke closed");
        const auto diagnostics=host.diagnostics();if(!diagnostics.error.empty())throw std::runtime_error(diagnostics.error);
        if(host.nowSeconds()>deadline)throw std::runtime_error("Foliage smoke timeout: frames="+std::to_string(diagnostics.presentedFrames)+", ready="+std::to_string(diagnostics.ready)+", suspended="+std::to_string(host.application().suspended())+", state="+diagnostics.renderState);
    };
    while(host.diagnostics().presentedFrames<8)tick();
    auto capture=[&](const char* name){host.captureFrame(output/name);do{tick();}while(host.capturePending());if(!host.captureError().empty())throw std::runtime_error(host.captureError());};
    if(benchmark) {
        if(demo.foliage->stats().instances!=10000)throw std::runtime_error("Benchmark must contain exactly 10000 clumps");
        windTimes.clear();for(int i=0;i<60;++i)tick();const auto timing=summarize(windTimes);const auto warm=host.diagnostics();
        host.setPaused(true);demo.command(7);for(int i=0;i<4;++i)tick();capture("fixed.ppm");
        std::ofstream report(output/"benchmark.json");report<<"{\"result\":\"PASS\",\"cpuReference\":"<<(cpu?"true":"false")<<",\"instances\":10000,\"bulkComponents\":"<<demo.foliage->stats().bulkComponents<<",\"cpuDeformedVertices\":"<<demo.foliage->stats().deformedVertices<<",\"windCpuMs\":{\"mean\":"<<timing.mean<<",\"p95\":"<<timing.p95<<"},\"performance\":"<<warm.performance<<",\"renderState\":"<<warm.renderState<<",\"fixedRenderState\":"<<host.diagnostics().renderState<<"}\n";
        demo.foliage->clear();host.requestClose();host.step(callbacks);std::cout<<"Windows dense foliage benchmark PASS\n";return 0;
    }
    if(demo.foliage->stats().instances<30||demo.foliage->stats().colliders==0)throw std::runtime_error("Missing foliage/collision");
    command(6);tick();const auto frozen=demo.motion();const auto time=demo.time;capture("before.ppm");for(int i=0;i<4;++i)tick();
    if(demo.time!=time||demo.motion()!=frozen)throw std::runtime_error("Paused wind moved");
    const auto scene=host.application().activeScene();
    if(!host.simulateGraphicsLoss())throw std::runtime_error("Foliage device-loss injection unavailable");
    do{tick();}while(!host.diagnostics().ready||host.diagnostics().renderState.find("\"generation\":2")==std::string::npos);
    for(int i=0;i<4;++i)tick();capture("recovered.ppm");
    if(host.application().activeScene()!=scene||demo.time!=time||demo.motion()!=frozen)throw std::runtime_error("Recovery replaced scene or advanced paused wind");
    const auto count=demo.foliage->stats().instances;if(!command(1)||demo.foliage->stats().instances!=count-1)throw std::runtime_error("Harvest failed");
    if(!command(4))throw std::runtime_error("Save failed");const auto saved=demo.foliage->saveRemoved();command(3);if(!command(5)||demo.foliage->saveRemoved()!=saved)throw std::runtime_error("Restore failed");
    const auto colliders=demo.foliage->stats().colliders;command(2);capture("streamed.ppm");if(demo.foliage->stats().colliders!=colliders)throw std::runtime_error("Visual streaming changed collision");command(2);
    command(0);capture("wind-off.ppm");command(0);const auto pausedMotion=demo.motion();command(6);for(int i=0;i<5;++i)tick();if(demo.motion()==pausedMotion||demo.time<=time)throw std::runtime_error("Wind did not resume");
    capture("after.ppm");std::ofstream report(output/"result.json");report<<"{\"result\":\"PASS\",\"instances\":"<<demo.foliage->stats().instances<<",\"colliders\":"<<demo.foliage->stats().colliders<<",\"removed\":"<<demo.foliage->removed().size()<<",\"recovery\":true,\"renderState\":"<<host.diagnostics().renderState<<"}\n";
    demo.foliage->clear();host.requestClose();host.step(callbacks);std::cout<<"Windows foliage smoke PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
