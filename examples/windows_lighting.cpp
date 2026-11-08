#include "lighting_gallery.hpp"
#include "material_delivery_gallery.hpp"
#include "ranked_lighting.hpp"
#include "fog_doorway.hpp"
#include "lighting_zone_doorway.hpp"
#include "dynamic_gi_doorway.hpp"
#include "fog_milestones.hpp"
#include "fog_profile.hpp"
#include "stone_arch.hpp"
#include "engine/desktop.hpp"
#include "engine/asset_desktop.hpp"
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
using namespace engine;

static int runGallery(DesktopHost& host,const ApplicationFrameCallbacks& callbacks,const std::filesystem::path& capture,
                      double warmupSeconds=0,DesktopDiagnostics* captureDiagnostics=nullptr) {
    if(capture.empty())return host.run(callbacks);
    const auto deadline=host.nowSeconds()+60;
    std::optional<double> firstPresented;
    bool queued=false;
    while(host.step(callbacks)){
        const auto now=host.nowSeconds();
        if(now>deadline)throw std::runtime_error("Gallery capture timed out");
        const auto diagnostics=host.diagnostics();
        if(diagnostics.ready&&diagnostics.presentedFrames&&!firstPresented)firstPresented=now;
        if(diagnostics.presentedFrames>8&&firstPresented&&now-*firstPresented>=warmupSeconds&&!queued){host.captureFrame(capture);queued=true;}
        if(queued&&!host.capturePending()){
            if(!host.captureError().empty())throw std::runtime_error(host.captureError());
            if(captureDiagnostics)*captureDiagnostics=diagnostics;
            host.requestClose();
        }
    }
    return 0;
}

static int runMaterialDelivery(DesktopHost& host,const std::filesystem::path& capture,bool enabled,double time,bool maps) {
    auto gallery=material_delivery_example::build(enabled,maps);const auto id=host.application().loadScene(std::move(gallery.scene));
    const DesktopView view{gallery.camera->id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);host.setWaterTime(time);host.setShadowsEnabled(false);
    host.setEventHandler([&](const DesktopEvent& e){if(e.type==DesktopEventType::Control&&e.down&&e.control==DesktopControl::Escape){host.requestClose();return true;}return false;});
    DesktopDiagnostics d;const auto result=runGallery(host,{},capture,.3,&d);
    if(!capture.empty())std::cout<<"{\"materialDelivery\":true,\"enabled\":"<<(enabled?"true":"false")<<",\"time\":"<<time<<",\"state\":"<<d.renderState<<"}\n";
    return result;
}

static int runLightingZoneDoorway(DesktopHost& host, const std::filesystem::path& capture,
                                 lighting_zone_example::Stop stop, bool local) {
    using namespace lighting_zone_example;
    auto scene = std::make_unique<Scene>("Local lighting doorway");
    auto& world = *scene;
    auto& camera = world.addChild<CameraComponent>("Doorway camera");
    auto& samples = build(world, camera);
    place(samples, stop);
    setLocalLighting(world, local);
    const auto id = host.application().loadScene(std::move(scene));
    const DesktopView view{camera.id()};
    host.setViews(id, {&view, 1});
    host.application().start(id);
    host.setPaused(true);
    host.setShadowsEnabled(false);
    std::cout << "Local lighting doorway: O outside, T threshold, I inside, L toggle local lighting, Esc quit\n";
    host.setEventHandler([&](const DesktopEvent& event) {
        if (event.type != DesktopEventType::Control || !event.down || event.repeat || event.guiConsumed)
            return false;
        switch (event.control) {
        case DesktopControl::O: place(samples, Stop::Outside); break;
        case DesktopControl::T: place(samples, Stop::Threshold); break;
        case DesktopControl::I: place(samples, Stop::Inside); break;
        case DesktopControl::L: local = !local; setLocalLighting(world, local); break;
        case DesktopControl::Escape: host.requestClose(); return true;
        default: return false;
        }
        world.synchronize();
        return true;
    });
    Vec2 ceramic{}, metal{};
    ApplicationFrameCallbacks callbacks;
    if (!capture.empty()) {
        callbacks.present = [&](Scene&, const ApplicationFrame&) {
            const auto info = host.diagnostics();
            if (!info.width || !info.height)
                return;
            const PixelViewport viewport{0, 0, static_cast<float>(info.width), static_cast<float>(info.height)};
            ceramic = camera.worldToScreen(samples.transform.position + ceramicOffset, viewport).pixel;
            metal = camera.worldToScreen(samples.transform.position + metalOffset, viewport).pixel;
        };
    }
    DesktopDiagnostics diagnostics;
    const int result = runGallery(host, callbacks, capture, 0, &diagnostics);
    if (!capture.empty()) {
        std::cout << "{\"ceramic\":[" << ceramic.x << ',' << ceramic.y << "],\"metal\":[" << metal.x << ',' << metal.y << "],\"state\":"
                  << diagnostics.renderState << "}\n";
    }
    host.setEventHandler({});
    return result;
}

static int runDynamicGiDoorway(DesktopHost& host,const std::filesystem::path& capture,
                               bool open,bool dynamic,unsigned bounces,bool offscreen) {
    auto scene=std::make_unique<Scene>("Dynamic lighting workshop");
    auto& world=*scene;
    auto& camera=world.addChild<CameraComponent>("Workshop camera");
    dynamic_gi_example::Doorway demo(world,camera);
    demo.setDoor(world,open);
    demo.setEnabled(world,dynamic);
    demo.setBounces(bounces);
    demo.setOffscreenReflections(world,offscreen);
    const auto id=host.application().loadScene(std::move(scene));
    const DesktopView view{camera.id()};
    host.setViews(id,{&view,1});
    host.application().start(id);
    host.setPaused(true);
    host.setShadowsEnabled(false);
    host.setEventHandler([&](const DesktopEvent& event) {
        if(event.type!=DesktopEventType::Control||!event.down||event.repeat||event.guiConsumed)return false;
        switch(event.control) {
        case DesktopControl::O: demo.setDoor(world,!demo.open());break;
        case DesktopControl::L: demo.setEnabled(world,!demo.enabled());break;
        case DesktopControl::B: demo.setBounces(demo.bounces()%4+1);break;
        case DesktopControl::F: demo.setOffscreenReflections(world,!demo.offscreenReflections());break;
        case DesktopControl::R: {
            auto quality=host.quality();quality.screenSpaceReflections=!quality.screenSpaceReflections;
            host.setQuality(quality);break;
        }
        case DesktopControl::Escape:host.requestClose();break;
        default:return false;
        }
        return true;
    });
    std::cout<<"Dynamic lighting workshop. O toggles the door, L toggles diffuse lighting, B cycles bounces, F toggles off-screen reflections, R toggles screen-space reflections.\n";
    double peakUpdateMs=0;
    const auto advance=[&] {
        const auto start=host.nowSeconds();
        if(!demo.advance(world))return false;
        peakUpdateMs=std::max(peakUpdateMs,(host.nowSeconds()-start)*1000);
        return true;
    };
    if(!capture.empty())while(advance()){}
    Vec2 sample{},mirror{},bounceSample{};
    float sourceRight=0;
    ApplicationFrameCallbacks callbacks;
    callbacks.present=[&](Scene&,const ApplicationFrame&) {
        advance();
        const auto info=host.diagnostics();
        if(info.width&&info.height) {
            const PixelViewport viewport{0,0,float(info.width),float(info.height)};
            sample=camera.worldToScreen(dynamic_gi_example::samplePosition,viewport).pixel;
            mirror=camera.worldToScreen(dynamic_gi_example::mirrorPosition,viewport).pixel;
            bounceSample=camera.worldToScreen(dynamic_gi_example::bounceSamplePosition,viewport).pixel;
            sourceRight=dynamic_gi_example::sourceScreenRight(camera,viewport);
        }
    };
    DesktopDiagnostics diagnostics;
    const auto result=runGallery(host,callbacks,capture,0,&diagnostics);
    if(!capture.empty())
        std::cout<<"{\"sample\":["<<sample.x<<','<<sample.y<<"],\"revision\":"<<demo.revision()
                 <<",\"peakUpdateMs\":"<<peakUpdateMs<<",\"bounces\":"<<demo.bounces()
                 <<",\"mirror\":["<<mirror.x<<','<<mirror.y<<"],\"bounceSample\":["<<bounceSample.x<<','<<bounceSample.y<<"]"
                 <<",\"sourceRight\":"<<sourceRight<<",\"offscreen\":"<<(demo.offscreenReflections()?"true":"false")
                 <<",\"reflectionRevision\":"<<demo.reflectionRevision()<<",\"state\":"<<diagnostics.renderState<<"}\n";
    return result;
}

static int runRankedLighting(DesktopHost& host,const std::filesystem::path& capture) {
    auto scene=std::make_unique<Scene>("Ranked lights");
    scene->ambient={.025f,.025f,.025f};scene->environment.reset();
    auto& camera=scene->addChild<CameraComponent>();
    camera.transform.position={7,7,11};camera.lookAt({0,0,-1});camera.setOrthographic(15,.1f,60);
    ranked_lighting_example::populate(scene->spawn("Ranked lighting rig"));
    const auto id=host.application().loadScene(std::move(scene));
    const DesktopView view{camera.id()};
    host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    host.setEventHandler([&](const DesktopEvent& event) {
        if(event.type!=DesktopEventType::Control||!event.down||event.guiConsumed)return false;
        if(event.control==DesktopControl::Escape){host.requestClose();return true;}
        if(event.control!=DesktopControl::Left&&event.control!=DesktopControl::Right)return false;
        const auto target=Vec3{0,0,-1};const auto delta=camera.transform.position-target;
        const float step=event.control==DesktopControl::Left?-.08f:.08f;
        camera.transform.position=target+Vec3{delta.x*std::cos(step)+delta.z*std::sin(step),delta.y,delta.z*std::cos(step)-delta.x*std::sin(step)};
        camera.lookAt(target);return true;
    });
    DesktopDiagnostics diagnostics;
    const auto result=runGallery(host,{},capture,.75,&diagnostics);
    host.setEventHandler({});
    if(!capture.empty())std::cout<<"{\"performance\":"<<diagnostics.performance<<",\"state\":"<<diagnostics.renderState<<"}\n";
    return result;
}

static int runDoorway(DesktopHost& host,const std::filesystem::path& capture,int mode,bool shadows,bool orthographic) {
    auto scene=std::make_unique<Scene>("Fog doorway");
    auto& root=scene->spawn("Doorway");
    auto& camera=scene->addChild<CameraComponent>("Camera");
    auto& water=fog_example::build(root,camera);
    if(orthographic)camera.setOrthographic(9,.1f,40);
    fog_example::atmosphere(*scene);
    auto& world=*scene;
    auto initial=host.quality();
    initial.volumetricFogSelfShadows=true;
    host.setQuality(initial);
    const auto fog=[&](int value){
        auto quality=host.quality();
        quality.volumetricFogGrid=value!=3;
        host.setQuality(quality);
        world.setFog(value==0?std::nullopt:std::optional(fog_example::settings(value>=2)));
    };
    fog(mode);
    scene->update(.01f);
    const auto id=host.application().loadScene(std::move(scene));
    const DesktopView view{camera.id()};
    host.setViews(id,{&view,1});
    host.application().start(id);
    host.setPaused(true);
    host.setShadowsEnabled(shadows);
    std::cout<<"Fog doorway controls: O off, A analytic, V volumetric grid, D direct volumetric, S geometry shadows, T fog self-shadowing, W planar water, Esc quit\n";
    host.setEventHandler([&](const DesktopEvent& event){
        if(event.type!=DesktopEventType::Control||!event.down||event.repeat||event.guiConsumed)return false;
        switch(event.control) {
        case DesktopControl::O:fog(0);return true;
        case DesktopControl::A:fog(1);return true;
        case DesktopControl::V:fog(2);return true;
        case DesktopControl::D:fog(3);return true;
        case DesktopControl::S:shadows=!shadows;host.setShadowsEnabled(shadows);return true;
        case DesktopControl::T:{
            auto quality=host.quality();
            quality.volumetricFogSelfShadows=!quality.volumetricFogSelfShadows;
            host.setQuality(quality);
            return true;
        }
        case DesktopControl::W:water.enabled=!water.enabled;world.synchronize();return true;
        case DesktopControl::Escape:host.requestClose();return true;
        default:return false;
        }
    });
    DesktopDiagnostics diagnostics;
    const int result=runGallery(host,{},capture,.75,&diagnostics);
    if(!capture.empty()){
        std::cout<<"{\"performance\":"<<diagnostics.performance<<",\"state\":"<<diagnostics.renderState
                 <<",\"effectiveMsaaSamples\":"<<diagnostics.effectiveMsaaSamples<<"}\n";
    }
    return result;
}

static int captureFogChecks(DesktopHost& host,const std::filesystem::path& directory,bool milestone) {
    using namespace fog_example;
    std::filesystem::create_directories(directory);
    auto scene=std::make_unique<Scene>("Doorway fog shadow acceptance");
    auto& world=*scene;
    auto& camera=world.addChild<CameraComponent>("Camera");
    const auto id=host.application().loadScene(std::move(scene));
    const DesktopView view{camera.id()};
    host.setViews(id,{&view,1});
    host.application().start(id);
    host.setPaused(true);
    const int count=milestone?int(MilestoneCaseCount):int(ShadowCaseCount);
    for(int mode=0;mode<count;++mode) {
        auto& root=world.spawn("Fog shadow probe");
        auto quality=host.quality();
        if(milestone){
            const auto check=static_cast<MilestoneCase>(mode);
            configureMilestone(world,root,camera,check);
            milestoneQuality(quality,check);
        }else{
            const auto check=static_cast<ShadowCase>(mode);
            configureShadowCheck(world,root,camera,check);
            shadowCheckQuality(quality,check);
        }
        host.setQuality(quality);
        const auto until=host.nowSeconds()+60;
        const auto firstFrame=host.diagnostics().presentedFrames;
        auto step=[&]{
            if(!host.step())throw std::runtime_error("Fog shadow fixture closed during capture");
            const auto info=host.diagnostics();
            if(!info.error.empty())throw std::runtime_error(info.error);
            if(host.nowSeconds()>until)throw std::runtime_error("Fog shadow fixture timed out");
            return info;
        };
        DesktopDiagnostics info;
        do {info=step();} while(!info.ready||info.presentedFrames<firstFrame+3);
        host.captureFrame(directory/(std::to_string(mode)+".ppm"));
        do {info=step();} while(host.capturePending());
        if(!host.captureError().empty())throw std::runtime_error(host.captureError());
        if(mode==count-1)std::cout<<info.renderState<<'\n';
        root.destroy();
        world.flushPendingDestroy();
    }
    return 0;
}

static int profileInteger(const std::string& value,int minimum,int maximum) {
    std::size_t consumed=0;
    const int result=std::stoi(value,&consumed);
    if(consumed!=value.size()||result<minimum||result>maximum)
        throw std::invalid_argument("Invalid fog profile integer: "+value);
    return result;
}

static int profileEnvironment(const char* name,int fallback,int maximum) {
    const auto* value=std::getenv(name);
    return value?profileInteger(value,1,maximum):fallback;
}

static std::vector<int> profileSelection(const char* name,std::vector<int> fallback,int maximum) {
    const auto* value=std::getenv(name);
    if(!value)return fallback;
    std::istringstream input(value);
    std::vector<int> result;
    std::string token;
    while(std::getline(input,token,','))result.push_back(profileInteger(token,0,maximum));
    if(result.empty()||value[std::char_traits<char>::length(value)-1]==',')
        throw std::invalid_argument(std::string("Invalid fog profile selection: ")+name);
    return result;
}

static std::uint64_t profileCounter(const std::string& json,const char* field) {
    const std::string prefix=std::string("\"")+field+"\":";
    const auto offset=json.find(prefix);
    return offset==std::string::npos?0:std::strtoull(json.c_str()+offset+prefix.size(),nullptr,10);
}

struct FogProfileComparison {const char* name;std::array<int,4> variants;};
static std::vector<FogProfileComparison> profileComparisons() {
    const auto* configured=std::getenv("FOG_PROFILE_COMPARISONS");
    std::istringstream input(configured?configured:"extinction,soft");
    std::vector<FogProfileComparison> result;
    std::string token;
    while(std::getline(input,token,',')) {
        if(token=="ext"||token=="extinction")result.push_back({"extinction",{0,1,1,0}});
        else if(token=="soft")result.push_back({"soft",{1,2,2,1}});
        else if(token=="cache")result.push_back({"cache",{1,3,3,1}});
        else throw std::invalid_argument("Invalid fog profile comparison: "+token);
    }
    if(result.empty())throw std::invalid_argument("Empty fog profile comparison selection");
    return result;
}

static int runFogProfile(DesktopHost& host,const std::filesystem::path& directory,int frameOverride) {
    using namespace fog_example;
    const int frameCount=frameOverride?frameOverride:profileEnvironment("FOG_PROFILE_FRAMES",180,100000);
    const int warmupCount=profileEnvironment("FOG_PROFILE_WARMUP",120,100000);
    const int repeats=profileEnvironment("FOG_PROFILE_REPEATS",3,100);
    const auto scenarios=profileSelection("FOG_PROFILE_SCENARIOS",{0,1,2,3,4},ProfileScenarioCount-1);
    const auto budgets=profileSelection("FOG_PROFILE_STEPS",{16,32},32);
    const auto comparisons=profileComparisons();
    const bool motion=std::getenv("FOG_PROFILE_MOTION")&&std::string_view(std::getenv("FOG_PROFILE_MOTION"))=="1";
    for(const auto budget:budgets)validateProfile(0,0,budget);
    std::filesystem::create_directories(directory);
    std::ofstream output(directory/"profile.jsonl");
    if(!output)throw std::runtime_error("Cannot create fog profiling JSONL output");
    output.imbue(std::locale::classic());
    output<<std::fixed<<std::setprecision(6);
    output<<"{\"type\":\"config\",\"schemaVersion\":1,\"warmupFrames\":"<<warmupCount
          <<",\"sampleFrames\":"<<frameCount<<",\"repeats\":"<<repeats
          <<",\"minimumWarmupSeconds\":2,\"minimumSampleSeconds\":3,\"snapshotIntervalSeconds\":0.25"
          <<",\"width\":960,\"height\":540,\"paused\":true,\"motion\":"<<(motion?"true":"false")
          <<",\"sequence\":[";
    for(std::size_t comparison=0;comparison<comparisons.size();++comparison)for(std::size_t order=0;order<4;++order) {
        if(comparison||order)output<<',';
        output<<comparisons[comparison].variants[order];
    }
    output<<']'
          <<",\"wallClock\":\"monotonic host step intervals including diagnostics polling\""
          <<",\"performanceWindows\":\"rolling source summaries for this case; snapshots overlap\"}\n";
    output.flush();
    std::ofstream progress(directory/"progress.jsonl");

    auto scene=std::make_unique<Scene>("Fog cost profiling");
    auto& world=*scene;
    auto& camera=world.addChild<CameraComponent>("Profiling camera");
    const auto id=host.application().loadScene(std::move(scene));
    const DesktopView view{camera.id()};
    host.setViews(id,{&view,1});
    host.application().start(id);
    host.setPaused(true);
    host.setWaterTime(0);
    host.setShadowsEnabled(true);
    for(const int scenario:scenarios)for(const int steps:budgets)for(int repeat=0;repeat<repeats;++repeat) {
        for(std::size_t sequence=0;sequence<comparisons.size()*4;++sequence) {
            const auto& comparison=comparisons[sequence/4];
            const int variant=comparison.variants[sequence%4];
            auto& root=world.spawn("Fog profiling probe");
            configureProfile(world,root,camera,scenario,variant);
            const auto lightCount=world.collect().lights.size();
            const auto& fog=*world.fog();
            std::size_t volumeCount=fog.volume&&fog.volume->density>0?1:0;
            for(const auto& box:fog.additionalVolumes)volumeCount+=box.density>0;
            auto quality=host.quality();
            profileQuality(quality,scenario,variant,steps);
            host.setQuality(quality);
            const auto precedingFrame=host.diagnostics().presentedFrames;
            const double caseStart=host.nowSeconds();
            double firstStepWallMs=0;
            std::string firstPerformance;
            const double deadline=host.nowSeconds()+300;
            double nextProgress=0;
            auto step=[&] {
                if(motion)updateProfile(world,root,camera,scenario,1,float(host.nowSeconds()-caseStart));
                const auto before=host.nowSeconds();
                // This hidden, paused fixture must keep presenting when desktop
                // focus changes. Simulation remains paused throughout the probe.
                host.application().setSuspended(false);
                if(!host.step())throw std::runtime_error("Fog profiling window closed");
                auto info=host.diagnostics();
                if(host.nowSeconds()>=nextProgress) {
                    progress<<"{\"scenario\":"<<scenario<<",\"variant\":"<<variant
                            <<",\"ready\":"<<(info.ready?"true":"false")
                            <<",\"suspended\":"<<(host.application().suspended()?"true":"false")
                            <<",\"presentedFrames\":"<<info.presentedFrames
                            <<",\"state\":"<<info.renderState<<"}\n";
                    progress.flush();nextProgress=host.nowSeconds()+1;
                }
                if(firstPerformance.empty()&&info.ready&&info.presentedFrames>precedingFrame) {
                    firstStepWallMs=(host.nowSeconds()-before)*1000;
                    firstPerformance=info.performance;
                }
                if(!info.error.empty())throw std::runtime_error(info.error);
                if(host.nowSeconds()>deadline)throw std::runtime_error("Fog profiling case timed out");
                return info;
            };
            DesktopDiagnostics info;
            do {info=step();} while(!info.ready||info.presentedFrames<=precedingFrame);
            const double warmupStart=host.nowSeconds();
            const auto firstFrame=info.presentedFrames;
            do {info=step();} while(info.presentedFrames-firstFrame<static_cast<std::uint64_t>(warmupCount)||host.nowSeconds()-warmupStart<2||
                (variant==3&&(!profileCounter(info.performance,"fogLightCacheSlots")||
                             (!motion&&profileCounter(info.performance,"fogLightCacheBuilds")>0))));
            const auto warmedFrames=info.presentedFrames-firstFrame;
            const auto warmupPerformance=info.performance;
            const double started=host.nowSeconds(),warmupSeconds=started-warmupStart;
            const auto measuredStart=info.presentedFrames;
            double previous=started,nextSnapshot=started;
            std::vector<double> wallSamples;
            std::ostringstream snapshots;
            snapshots.imbue(std::locale::classic());
            snapshots<<std::fixed<<std::setprecision(6);
            bool firstSnapshot=true;
            do {
                info=step();
                const double now=host.nowSeconds();
                wallSamples.push_back((now-previous)*1000);
                previous=now;
                if(now>=nextSnapshot) {
                    if(!firstSnapshot)snapshots<<',';
                    firstSnapshot=false;
                    snapshots<<"{\"atSeconds\":"<<now-started<<",\"performance\":"<<info.performance<<'}';
                    nextSnapshot=now+.25;
                }
            } while(info.presentedFrames-measuredStart<static_cast<std::uint64_t>(frameCount)||previous-started<3);
            const auto trace=host.gpuProfileTrace();
            output<<"{\"type\":\"case\",\"backend\":\""<<(host.backend()==DesktopBackend::Direct3D12?"d3d12":"webgpu")
                  <<"\",\"scenario\":"<<scenario<<",\"name\":\""<<profileScenarioNames[scenario]
                  <<"\",\"variant\":"<<variant<<",\"steps\":"<<steps<<",\"repeat\":"<<repeat
                  <<",\"sequence\":"<<sequence<<",\"comparison\":\""<<comparison.name
                  <<"\",\"order\":"<<sequence%4<<",\"lightCount\":"<<lightCount<<",\"volumeCount\":"<<volumeCount
                  <<",\"width\":"<<info.width<<",\"height\":"<<info.height
                  <<",\"warmupFrames\":"<<warmedFrames<<",\"warmupSeconds\":"<<warmupSeconds
                  <<",\"warmupPerformance\":"<<warmupPerformance
                  <<",\"motion\":"<<(motion?"true":"false")<<",\"firstStepWallMs\":"<<firstStepWallMs
                  <<",\"firstPerformance\":"<<firstPerformance
                  <<",\"measuredFrames\":"<<info.presentedFrames-measuredStart<<",\"elapsedSeconds\":"<<previous-started
                  <<",\"wallSamplesMs\":[";
            for(std::size_t i=0;i<wallSamples.size();++i){if(i)output<<',';output<<wallSamples[i];}
            output<<"],\"performanceSamples\":["<<snapshots.str()<<"],\"performance\":"<<info.performance
                  <<",\"state\":"<<info.renderState<<",\"gpuTrace\":"<<(trace.empty()?"null":trace)<<"}\n";
            output.flush();
            if(!output)throw std::runtime_error("Cannot write fog profiling JSONL output");
            std::cout<<"{\"scenario\":"<<scenario<<",\"steps\":"<<steps<<",\"variant\":"<<variant
                     <<",\"repeat\":"<<repeat<<",\"sequence\":"<<sequence<<"}\n";
            root.destroy();
            world.flushPendingDestroy();
        }
    }
    return 0;
}

static int captureFogCache(DesktopHost& host,const std::filesystem::path& directory) {
    using namespace fog_example;
    std::filesystem::create_directories(directory);
    std::ofstream output(directory/"cache.jsonl");
    if(!output)throw std::runtime_error("Cannot create fog cache evidence");
    output.imbue(std::locale::classic());output<<std::fixed<<std::setprecision(6);
    auto scene=std::make_unique<Scene>("Fog cache acceptance");
    auto& world=*scene;
    auto& camera=world.addChild<CameraComponent>("Camera");
    const auto id=host.application().loadScene(std::move(scene));
    DesktopView view{camera.id()};
    host.setViews(id,{&view,1});host.application().start(id);
    host.setPaused(true);host.setWaterTime(0);host.setShadowsEnabled(true);
    double deadline=host.nowSeconds()+120;
    auto step=[&] {
        if(!host.step())throw std::runtime_error("Fog cache fixture closed");
        auto info=host.diagnostics();
        if(!info.error.empty())throw std::runtime_error(info.error);
        if(host.nowSeconds()>deadline)throw std::runtime_error("Fog cache fixture timed out");
        return info;
    };
    for(int scenario=0;scenario<ProfileScenarioCount;++scenario)for(const int steps:std::array{16,32}) {
        auto& root=world.spawn("Fog cache probe");
        configureProfile(world,root,camera,scenario,1);
        auto quality=host.quality();
        const auto capture=[&](const char* stage,int variant,bool image) {
            deadline=host.nowSeconds()+60;
            host.setQuality(quality); // Also forces a fresh diagnostic report for the first frame.
            const auto precedingFrame=host.diagnostics().presentedFrames;
            const auto firstAt=host.nowSeconds();
            DesktopDiagnostics first;
            do {first=step();} while(!first.ready||first.presentedFrames<=precedingFrame);
            const auto firstWall=(host.nowSeconds()-firstAt)*1000;
            const auto firstFrame=first.presentedFrames;
            const auto warmedAt=host.nowSeconds();
            std::ostringstream initial;
            initial<<'['<<first.performance;
            auto stable=first;
            std::uint64_t observedBuilds=std::max(profileCounter(first.performance,"totalFogLightCacheBuilds"),profileCounter(first.performance,"fogLightCacheBuilds"));
            const std::string_view label=stage;
            const bool needsBuild=label=="cache"||label=="light"||label=="density"||label=="view-on"||label=="steps-on";
            do {
                stable=step();
                observedBuilds=std::max({observedBuilds,profileCounter(stable.performance,"totalFogLightCacheBuilds"),profileCounter(stable.performance,"fogLightCacheBuilds")});
                if(stable.presentedFrames-firstFrame<=3)initial<<','<<stable.performance;
            } while(stable.presentedFrames-firstFrame<4||host.nowSeconds()-warmedAt<.4||
                    profileCounter(stable.performance,"fogLightCacheBuilds")!=0||
                    (needsBuild&&(!observedBuilds||profileCounter(stable.performance,"fogLightCacheBytes")<2097152)));
            initial<<']';
            if(image) {
                const auto filename=std::to_string(scenario)+"-"+std::to_string(steps)+"-"+stage+".ppm";
                host.captureFrame(directory/filename);
                do {stable=step();} while(host.capturePending());
                if(!host.captureError().empty())throw std::runtime_error(host.captureError());
            }
            const auto trace=host.gpuProfileTrace();
            output<<"{\"scenario\":"<<scenario<<",\"steps\":"<<steps<<",\"variant\":"<<variant
                  <<",\"stage\":\""<<stage<<"\",\"firstStepWallMs\":"<<firstWall
                  <<",\"firstPerformance\":"<<first.performance<<",\"initialPerformance\":"<<initial.str()
                  <<",\"performance\":"<<stable.performance<<",\"state\":"<<stable.renderState
                  <<",\"gpuTrace\":"<<(trace.empty()?"null":trace)<<"}\n";
            output.flush();
            if(!output)throw std::runtime_error("Cannot write fog cache evidence");
        };
        profileQuality(quality,scenario,1,steps);capture("direct",1,true);
        profileQuality(quality,scenario,3,steps);capture("cache",3,true);
        if(scenario==0&&steps==16) {
            updateProfile(world,root,camera,scenario,0,1);capture("camera",3,false);
            updateProfile(world,root,camera,scenario,1,1);capture("light",3,false);
            updateProfile(world,root,camera,scenario,2,1);capture("density",3,false);
            view.fogEnabled=false;host.setViews(id,{&view,1});capture("view-off",3,false);
            view.fogEnabled=true;host.setViews(id,{&view,1});capture("view-on",3,false);
            quality.volumetricFogSteps=0;capture("steps-zero",3,false);
            quality.volumetricFogSteps=16;capture("steps-on",3,false);
            quality.volumetricFogLightCache=false;capture("cache-off",1,false);
        }
        root.destroy();world.flushPendingDestroy();
    }
    return 0;
}

static int captureStreamedArch(DesktopHost& host,ClusterPageStream& stream,const std::filesystem::path& capture) {
    auto coarse=capture.parent_path()/(capture.stem().wstring()+L"-coarse"+capture.extension().wstring());
    const auto deadline=host.nowSeconds()+180;
    const auto step=[&](const ApplicationFrameCallbacks& callbacks) {
        if(!host.step(callbacks))throw std::runtime_error("Stone arch closed during capture");
        const auto info=host.diagnostics();
        if(!info.error.empty())throw std::runtime_error(info.error);
        if(host.nowSeconds()>deadline)throw std::runtime_error("Streamed stone arch capture timed out");
        return info;
    };
    const auto take=[&](const std::filesystem::path& path,const ApplicationFrameCallbacks& callbacks) {
        host.captureFrame(path);
        do { step(callbacks); } while(host.capturePending());
        if(!host.captureError().empty())throw std::runtime_error(host.captureError());
    };
    DesktopDiagnostics cold;
    do { cold=step({}); } while(cold.presentedFrames<3);
    take(coarse,{});
    ApplicationFrameCallbacks callbacks;
    callbacks.present=[&](Scene&,const ApplicationFrame&){stream.update();};
    unsigned stable=0;
    std::uint64_t reads=0;
    double stableSince=host.nowSeconds();
    DesktopDiagnostics refined;
    do {
        refined=step(callbacks);
        const auto stats=stream.stats();
        if(stats.failedPages)throw std::runtime_error("Stone arch fine page failed to load");
        if(stats.residentPages&&!stats.pendingPages&&stats.reads==reads)++stable;
        else { stable=0;stableSince=host.nowSeconds(); }
        reads=stats.reads;
    } while(stable<5||host.nowSeconds()-stableSince<.5);
    take(capture,callbacks);
    refined=host.diagnostics();
    const auto stats=stream.stats();
    std::cout<<"{\"coarse\":"<<cold.performance<<",\"refined\":"<<refined.performance
             <<",\"pages\":"<<stats.residentPages<<",\"total\":"<<stream.asset().pages().size()-1
             <<",\"bytes\":"<<stats.residentBytes<<",\"reads\":"<<stats.reads<<"}\n";
    return 0;
}

static int runStoneArch(DesktopHost& host,const std::filesystem::path& capture,int mode,int shot,bool occluder) {
    auto scene=std::make_unique<Scene>("Stone arch LOD comparison");
    auto& root=scene->spawn("Courtyard");
    auto& camera=scene->addChild<CameraComponent>("Camera");
    auto& world=*scene;
    stone_arch_example::build(root,camera,world,mode!=3);
    std::unique_ptr<DesktopAssetExecutor> executor;
    std::shared_ptr<ClusterPageStream> stream;
    const auto apply=[&] {
        if(mode==3&&!stream) {
            executor=std::make_unique<DesktopAssetExecutor>();
            const DesktopFiles files;
            const std::filesystem::path directory=stone_arch_example::directory;
            const auto asset=ClusterPageAsset::open(files.readAsset(directory/"arch.wecl",ClusterPageAsset::maxManifestBytes));
            stream=std::make_shared<ClusterPageStream>(asset,[&](auto source,std::uint32_t page) {
                return executor->assetLoader<Mesh>(files,directory/stone_arch_example::pageName(page),
                    [source,page](auto bytes){return source->decode(page,bytes);},{source->pages()[page].bytes,15000});
            });
            stone_arch_example::bindStream(world,stream);
        }
        stone_arch_example::pose(camera,shot);
        stone_arch_example::select(world,mode);
        std::cout<<"Stone arch: "<<std::array{"Full mesh","Whole-mesh LOD","Clustered LOD","Streamed LOD"}[mode]
                 <<" / "<<std::array{"near","middle","far"}[shot]<<'\n';
    };
    apply();
    stone_arch_example::showOccluder(world,occluder);
    const auto id=host.application().loadScene(std::move(scene));
    const DesktopView view{camera.id()};
    host.setViews(id,{&view,1});
    host.application().start(id);
    host.setPaused(true);
    std::cout<<"F full mesh, W whole-mesh LOD, C clustered LOD, L streamed LOD, R clear fine pages, G GPU submission, H GPU LOD, O wall, Left/Right camera, Esc quit\n";
    if(mode==3&&!capture.empty())return captureStreamedArch(host,*stream,capture);
    host.setEventHandler([&](const DesktopEvent& event){
        if(event.type!=DesktopEventType::Control||!event.down||event.repeat||event.guiConsumed)return false;
        switch(event.control) {
        case DesktopControl::F:mode=0;break;
        case DesktopControl::W:mode=1;break;
        case DesktopControl::C:mode=2;break;
        case DesktopControl::L:mode=3;break;
        case DesktopControl::R:if(stream)stream->clear();return true;
        case DesktopControl::G:{auto q=host.quality();q.gpuClusterCulling=!q.gpuClusterCulling;host.setQuality(q);return true;}
        case DesktopControl::H:{auto q=host.quality();q.gpuClusterHierarchy=!q.gpuClusterHierarchy;if(q.gpuClusterHierarchy)q.gpuClusterCulling=true;host.setQuality(q);return true;}
        case DesktopControl::O:occluder=!occluder;stone_arch_example::showOccluder(world,occluder);return true;
        case DesktopControl::Left:shot=(shot+2)%3;break;
        case DesktopControl::Right:shot=(shot+1)%3;break;
        case DesktopControl::Escape:host.requestClose();return true;
        default:return false;
        }
        apply();
        return true;
    });
    DesktopDiagnostics diagnostics;
    ApplicationFrameCallbacks callbacks;
    callbacks.present=[&](Scene&,const ApplicationFrame&){if(stream)stream->update();};
    const int result=runGallery(host,callbacks,capture,.75,&diagnostics);
    if(!capture.empty())std::cout<<"{\"performance\":"<<diagnostics.performance<<",\"state\":"<<diagnostics.renderState<<"}\n";
    return result;
}

int main(int argc,char** argv){try{
    DesktopSettings settings;settings.title="WebEngine | Lighting, environment and post-processing";settings.quality.post.enabled=true;
    std::optional<lighting_zone_example::Stop> lightingZoneStop;
    std::string dynamicGiMode;
    unsigned dynamicGiBounces=1;
    bool dynamicOffscreen=false;
    bool localLighting = true;
    bool hidden=false,doorwayShadows=true,doorwayHdr=false,doorwayMsaa=false,doorwayOrtho=false;
    int doorway=-1,arch=-1,archShot=0,fogProfileFrames=0;std::filesystem::path capture,fogShadowSmoke,fogProfile,fogCacheSmoke;
    bool fogMilestones=false,rankedLighting=false,archGpu=false,archOccluder=false,archHierarchy=false,archShadows=true;
    int materialDelivery=-1;double materialTime=0;bool materialMaps=false;
    for(int i=1;i<argc;++i){const std::string key=argv[i];
        if(key=="--backend"&&i+1<argc){const std::string value=argv[++i];if(value=="d3d12")settings.backend=DesktopBackend::Direct3D12;else if(value=="dawn")settings.backend=DesktopBackend::Dawn;else throw std::invalid_argument("Backend must be dawn or d3d12");}
        else if(key=="--capture"&&i+1<argc){capture=argv[++i];hidden=true;}
        else if (key == "--lighting-zone" && i + 1 < argc) {
            using lighting_zone_example::Stop;
            const std::string value = argv[++i];
            if (value == "outside") lightingZoneStop = Stop::Outside;
            else if (value == "threshold") lightingZoneStop = Stop::Threshold;
            else if (value == "inside") lightingZoneStop = Stop::Inside;
            else throw std::invalid_argument("Lighting zone stop must be outside, threshold or inside");
        }
        else if (key == "--global-lighting") localLighting = false;
        else if(key=="--dynamic-gi"&&i+1<argc) {
            dynamicGiMode=argv[++i];
            if(dynamicGiMode!="open"&&dynamicGiMode!="closed"&&dynamicGiMode!="baked")
                throw std::invalid_argument("Dynamic GI mode must be open, closed or baked");
        }
        else if(key=="--screen-space-reflections")settings.quality.screenSpaceReflections=true;
        else if(key=="--offscreen-reflections")dynamicOffscreen=true;
        else if(key=="--bounces"&&i+1<argc) {
            const std::string value=argv[++i];
            if(value.size()!=1||value[0]<'1'||value[0]>'4')throw std::invalid_argument("Diffuse bounces must be between 1 and 4");
            dynamicGiBounces=unsigned(value[0]-'0');
        }
        else if(key=="--fog-shadow-smoke"&&i+1<argc){fogShadowSmoke=argv[++i];hidden=true;}
        else if(key=="--fog-milestone-smoke"&&i+1<argc){fogShadowSmoke=argv[++i];hidden=true;fogMilestones=true;}
        else if(key=="--fog-profile"&&i+1<argc){fogProfile=argv[++i];hidden=true;}
        else if(key=="--fog-profile-frames"&&i+1<argc)fogProfileFrames=profileInteger(argv[++i],1,100000);
        else if(key=="--fog-cache-smoke"&&i+1<argc){fogCacheSmoke=argv[++i];hidden=true;}
        else if(key=="--material-delivery"&&i+1<argc){const std::string value=argv[++i];if(value!="enabled"&&value!="disabled")throw std::invalid_argument("Material delivery must be enabled or disabled");materialDelivery=value=="enabled"?1:0;}
        else if(key=="--material-maps")materialMaps=true;
        else if(key=="--material-time"&&i+1<argc){std::size_t used=0;const std::string value=argv[++i];materialTime=std::stod(value,&used);if(used!=value.size()||!std::isfinite(materialTime)||std::abs(materialTime)>1000000)throw std::invalid_argument("Invalid material time");}
        else if(key=="--ranked-lighting")rankedLighting=true;
        else if(key=="--fog-doorway"&&i+1<argc) {
            const std::string value=argv[++i];
            if(value=="off")doorway=0;
            else if(value=="analytic")doorway=1;
            else if(value=="volumetric")doorway=2;
            else if(value=="direct")doorway=3;
            else throw std::invalid_argument("Fog must be off, analytic, volumetric or direct");
        }
        else if(key=="--no-doorway-shadows")doorwayShadows=false;
        else if(key=="--doorway-hdr")doorwayHdr=true;
        else if(key=="--doorway-msaa")doorwayMsaa=true;
        else if(key=="--doorway-ortho")doorwayOrtho=true;
        else if(key=="--stone-arch"&&i+1<argc) {
            const std::string value=argv[++i];
            if(value=="full")arch=0;
            else if(value=="whole")arch=1;
            else if(value=="clustered")arch=2;
            else if(value=="streamed")arch=3;
            else throw std::invalid_argument("Stone arch mode must be full, whole, clustered or streamed");
        }
        else if(key=="--arch-camera"&&i+1<argc) {
            const std::string value=argv[++i];
            if(value=="near")archShot=0;
            else if(value=="middle")archShot=1;
            else if(value=="far")archShot=2;
            else throw std::invalid_argument("Stone arch camera must be near, middle or far");
        }
        else if(key=="--arch-gpu")archGpu=true;
        else if(key=="--arch-gpu-hierarchy"){archHierarchy=true;archGpu=true;}
        else if(key=="--arch-no-shadows")archShadows=false;
        else if(key=="--arch-occluder")archOccluder=true;
        else {std::cout<<"webengine_lighting_windows [--backend dawn|d3d12] [--capture image.ppm] [--ranked-lighting] [--dynamic-gi open|closed|baked] [--screen-space-reflections] [--bounces 1|2|3|4] [--offscreen-reflections] [--lighting-zone outside|threshold|inside] [--global-lighting] [--fog-doorway off|analytic|volumetric|direct] [--fog-shadow-smoke output-directory] [--fog-milestone-smoke output-directory] [--fog-profile output-directory] [--fog-profile-frames count] [--fog-cache-smoke output-directory] [--no-doorway-shadows] [--doorway-hdr] [--doorway-msaa] [--doorway-ortho] [--stone-arch full|whole|clustered|streamed] [--arch-camera near|middle|far] [--arch-gpu] [--arch-occluder]\n";return key=="--help"?0:1;}
    }
    settings.hidden=hidden;
    if(materialDelivery>=0) {
        settings.title="WebEngine | Material delivery";settings.width=1024;settings.height=640;settings.quality.post.enabled=false;
        DesktopHost host(settings);return runMaterialDelivery(host,capture,materialDelivery!=0,materialTime,materialMaps);
    }
    if(!fogCacheSmoke.empty()) {
        settings.title="WebEngine | Fog cache acceptance";
        settings.width=960;settings.height=540;
        settings.quality.post.enabled=false;
        DesktopHost host(settings);
        return captureFogCache(host,fogCacheSmoke);
    }
    if(!fogProfile.empty()) {
        settings.title="WebEngine | Fog cost profiling";
        settings.width=960;settings.height=540;
        settings.quality.post.enabled=false;
        DesktopHost host(settings);
        return runFogProfile(host,fogProfile,fogProfileFrames);
    }
    if(!dynamicGiMode.empty()) {
        settings.title="WebEngine | Dynamic lighting workshop";
        settings.width=800;settings.height=450;
        settings.quality.post.bloomStrength=0;
        DesktopHost host(settings);
        return runDynamicGiDoorway(host,capture,dynamicGiMode!="closed",dynamicGiMode!="baked",dynamicGiBounces,dynamicOffscreen);
    }
    if (lightingZoneStop) {
        settings.title = "WebEngine | Local lighting doorway";
        settings.width = 800;
        settings.height = 450;
        settings.quality.post.enabled = false;
        DesktopHost host(settings);
        return runLightingZoneDoorway(host, capture, *lightingZoneStop, localLighting);
    }
    if(!fogShadowSmoke.empty()){
        settings.width=640;
        settings.height=480;
        settings.quality.post.enabled=false;
        DesktopHost host(settings);
        return captureFogChecks(host,fogShadowSmoke,fogMilestones);
    }
    if(doorway>=0){settings.title="WebEngine | Fog doorway";settings.quality.post.enabled=doorwayHdr;settings.quality.msaaSamples=doorwayMsaa?4:1;}
    if(arch>=0){settings.title="WebEngine | Stone arch LOD";settings.quality.post.enabled=false;settings.quality.lod=settings.quality.shadowLod=true;settings.quality.gpuClusterCulling=archGpu;settings.quality.gpuClusterHierarchy=archHierarchy;}
    if(rankedLighting)ranked_lighting_example::configure(settings.quality);
    DesktopHost host(settings);
    if(arch>=0)host.setShadowsEnabled(archShadows);
    if(rankedLighting)return runRankedLighting(host,capture);
    if(arch>=0)return runStoneArch(host,capture,arch,archShot,archOccluder);
    if(doorway>=0)return runDoorway(host,capture,doorway,doorwayShadows,doorwayOrtho);
    auto gallery=lighting_example::makeGallery();
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
    auto& environment=host.gui().root().add<Panel>();environment.setSize({324,480});environment.setPadding(12);environment.setLayout(VerticalLayout{6});
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
    auto& toneMapper=environmentBox("PBR Neutral tone mapping",false,[&](bool value){auto q=host.quality();q.post.toneMapper=value?ToneMapper::PbrNeutral:ToneMapper::Reinhard;host.setQuality(q);});
    auto& bloom=environmentBox("Bloom",true,[&](bool value){auto q=host.quality();q.post.bloomStrength=value?.8f:0;host.setQuality(q);});
    environmentBox("Grayscale",false,[&](bool value){auto q=host.quality();q.post.saturation=value?0.f:1.f;host.setQuality(q);});
    environment.add<TextLabel>("Exposure (stops)").setSize({300,24});
    auto& exposure=environment.add<Slider>();exposure.setSize({300,30});exposure.setRange(-3,3,.1f);exposure.setValue(0);exposure.setAccessibleName("Exposure");exposure.onChange([&](float value){auto q=host.quality();q.post.exposure=value;host.setQuality(q);});
    environment.add<TextLabel>("Post effects preserve GUI colors").setSize({300,24});
    auto initial=host.quality();initial.post.bloomStrength=.8f;host.setQuality(initial);
    host.setEventHandler([&](const DesktopEvent& event){if(event.type==DesktopEventType::Control&&event.down&&!event.repeat&&!event.guiConsumed){if(event.control==DesktopControl::F11){host.setFullscreen(!host.fullscreen());return true;}if(event.control==DesktopControl::Escape){host.requestClose();return true;}}return false;});
    ApplicationFrameCallbacks callbacks;callbacks.present=[&](Scene&,const ApplicationFrame&){msaa.setEnabled(host.capabilities().msaa4x);hdr.setEnabled(host.capabilities().hdrPostProcessing);toneMapper.setEnabled(host.capabilities().hdrPostProcessing);bloom.setEnabled(host.capabilities().hdrPostProcessing);environment.setPosition({std::max(352.f,host.size().width-340),16});};
    return runGallery(host,callbacks,capture);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
