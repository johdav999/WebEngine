#include "desktop/desktop_graphics.hpp"
#include <iostream>
using namespace engine;
namespace {
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid policy was accepted");}
struct State {bool alive=false,fail=false,ready=true,pending=false,shadows=true;unsigned destroyed=0,services=0;std::size_t views=0,water=0;std::uint64_t frames=0;float scale=0;};
class Fake final:public DesktopGraphics {
    std::shared_ptr<State> s;RenderCapabilities caps;DesktopBackend type;
public:
    Fake(std::shared_ptr<State> state,DesktopBackend b,const RenderQuality& q):s(state),type(b){s->alive=true;s->fail=false;s->pending=false;s->frames=0;s->scale=q.resolution.scale;}
    ~Fake()override{s->alive=false;++s->destroyed;}
    void bind(Scene* scene,std::span<const RenderView> views)override{s->views=scene?views.size():0;}
    void setViews(std::span<const RenderView> views)override{s->views=views.size();}
    void service()override{++s->services;}
    void render(double,double)override{++s->frames;}
    void setQuality(const RenderQuality& q)override{s->scale=q.resolution.scale;}
    const RenderCapabilities& capabilities()const override{return caps;}
    void setShadows(bool v)override{s->shadows=v;}
    void setWaterInteractions(const WaterInteractions& v)override{s->water=v.values().size();}
    DesktopDiagnostics diagnostics()const override{DesktopDiagnostics d;d.ready=s->ready&&!s->fail;d.presentedFrames=s->frames;if(s->fail)d.error="Injected adapter failure";return d;}
    void retry()override{s->fail=false;}
    bool simulateLoss()override{return true;}
    DesktopBackend backend()const override{return type;}
    void capture(const std::filesystem::path&)override{if(s->pending)throw std::logic_error("Duplicate capture");s->pending=true;}
    bool capturePending()const override{return s->pending;}
    std::string captureError()const override{return {};}
};
}
int main(){try{
    auto first=std::make_shared<State>(),second=std::make_shared<State>();int calls=0;
    auto factory=[&](DesktopBackend b,const RenderQuality& q)->std::unique_ptr<DesktopGraphics>{++calls;if(b==DesktopBackend::DawnVulkan)require(!first->alive,"Old surface survived replacement");return std::make_unique<Fake>(b==DesktopBackend::Dawn?first:second,b,q);};
    rejects([&]{makeFallbackGraphics({},factory,{});});rejects([&]{makeFallbackGraphics({DesktopBackend::Automatic},factory,{});});rejects([&]{makeFallbackGraphics({DesktopBackend::Dawn,DesktopBackend::Dawn},factory,{});});
    auto graphics=makeFallbackGraphics({DesktopBackend::Dawn,DesktopBackend::DawnVulkan},factory,{});
    Scene scene;auto& camera=scene.spawn("Camera").addComponent<CameraComponent>();scene.synchronize();const RenderView view{&camera};graphics->bind(&scene,{&view,1});
    auto q=RenderQuality{};q.resolution.scale=.75f;graphics->setQuality(q);graphics->setShadows(false);graphics->render(0,16);graphics->capture("unused.ppm");
    WaterInteractions water;const WaterInteraction ring;water.set({&ring,1});graphics->setWaterInteractions(water);
    camera.destroy();scene.synchronize();first->fail=true;graphics->service();
    require(graphics->backend()==DesktopBackend::DawnVulkan&&first->destroyed==1&&second->alive,"Asynchronous failure did not replace backend");
    require(second->water==1,"Water inputs were lost across fallback");require(second->views==0,"Destroyed camera was retained through fallback");require(second->scale==.75f&&!second->shadows,"Quality/shadow policy was lost");
    require(!graphics->capturePending()&&!graphics->captureError().empty(),"Readback was retained through fallback");require(graphics->diagnostics().presentedFrames==1,"Frame count regressed across fallback");
    require(graphics->diagnostics().graphicsAttempts.size()==2&&!graphics->diagnostics().graphicsAttempts[0].error.empty(),"Failure diagnostics were lost");
    graphics->capture("new.ppm");require(graphics->captureError().empty(),"New capture retained stale failure");
    require(graphics->simulateFailure(),"Diagnostic failure not queued");graphics->service();require(!graphics->diagnostics().error.empty()&&calls==2,"Exhaustion restarted an unbounded fallback loop");
    graphics->service();require(calls==2,"Terminal failure kept recreating devices");graphics->retry();require(calls==3&&graphics->backend()==DesktopBackend::Dawn,"Explicit retry did not start a fresh bounded attempt");
    graphics->bind(nullptr,{});graphics.reset();require(!first->alive&&!second->alive,"Teardown retained a backend");
    int attempts=0;auto startup=makeFallbackGraphics({DesktopBackend::Dawn,DesktopBackend::DawnVulkan},[&](DesktopBackend b,const RenderQuality& quality)->std::unique_ptr<DesktopGraphics>{++attempts;if(b==DesktopBackend::Dawn)throw std::runtime_error("Unavailable device");return std::make_unique<Fake>(second,b,quality);},{});
    require(attempts==2&&startup->backend()==DesktopBackend::DawnVulkan&&startup->diagnostics().error.empty(),"Synchronous startup failure did not fall back");
    std::cout<<"Desktop fallback policy: startup/async failure, retirement, stale handles, quality, captures, exhaustion and retry passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
