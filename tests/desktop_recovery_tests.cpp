#include "desktop/desktop_graphics.hpp"
#include <iostream>
using namespace engine;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct State {bool alive=false,lost=false,userError=false,pending=false,shadows=true;unsigned views=0,contacts=0;double waterTime=0;float scale=1;std::uint64_t frames=0;};
class Fake final:public DesktopGraphics {
    std::shared_ptr<State> state_;RenderCapabilities caps_;
public:
    Fake(std::shared_ptr<State> s,const RenderQuality& q):state_(s){check(!s->alive,"Old device survived reacquisition");s->alive=true;s->lost=s->pending=false;s->frames=0;s->scale=q.resolution.scale;}
    ~Fake()override{state_->alive=false;}
    void bind(Scene* s,std::span<const RenderView> v)override{state_->views=s?unsigned(v.size()):0;}
    void setViews(std::span<const RenderView> v)override{state_->views=unsigned(v.size());}
    void service()override{if(state_->userError)throw std::runtime_error("User/validation failure");if(state_->lost)throw DesktopDeviceLost("Lost device");}
    void render(double,double)override{++state_->frames;}
    void setWaterTime(double v)override{state_->waterTime=v;}
    void setQuality(const RenderQuality& q)override{state_->scale=q.resolution.scale;}
    const RenderCapabilities& capabilities()const override{return caps_;}
    void setShadows(bool v)override{state_->shadows=v;}
    void setWaterInteractions(const WaterInteractions& v)override{state_->contacts=unsigned(v.values().size());}
    DesktopDiagnostics diagnostics()const override{DesktopDiagnostics d;d.ready=state_->alive&&!state_->lost;d.presentedFrames=state_->frames;return d;}
    void retry()override{}
    bool simulateLoss()override{state_->lost=true;return true;}
    DesktopBackend backend()const override{return DesktopBackend::Direct3D12;}
    void capture(const std::filesystem::path&)override{state_->pending=true;}
    bool capturePending()const override{return state_->pending;}
    std::string captureError()const override{return {};}
};
}
int main(){try{
    double now=0;int calls=0,failures=0;auto state=std::make_shared<State>();
    auto factory=[&](DesktopBackend b,const RenderQuality& q)->std::unique_ptr<DesktopGraphics>{check(b==DesktopBackend::Direct3D12,"Recovery switched backend");++calls;check(!state->alive,"Factory called before old device destruction");if(failures-->0)throw std::runtime_error("Unavailable adapter");return std::make_unique<Fake>(state,q);};
    auto g=makeRecoveringD3D12Graphics(factory,{},[&]{return now;});Scene scene;auto& camera=scene.spawn("Camera").addComponent<CameraComponent>();scene.synchronize();const RenderView view{&camera};g->bind(&scene,{&view,1});
    auto q=RenderQuality{};q.resolution.scale=.75f;g->setQuality(q);g->setShadows(false);g->setWaterTime(4);WaterInteractions water;const WaterInteraction contact;water.set({&contact,1});g->setWaterInteractions(water);
    g->render(0,16);g->capture("cancel.ppm");check(g->simulateLoss(),"Ready diagnostic loss failed");g->service();check(!g->diagnostics().ready&&!state->alive&&!g->capturePending(),"Loss did not retire device and readback");check(!g->captureError().empty(),"Cancelled capture has no reason");
    camera.destroy();scene.synchronize();g->service();check(calls==1,"Recovery ignored backoff");now=1;g->service();check(calls==2&&g->diagnostics().ready,"Reacquisition failed");
    check(state->views==0&&state->scale==.75f&&!state->shadows&&state->waterTime==4&&state->contacts==1,"Recovery lost policy or retained destroyed camera");
    check(g->diagnostics().presentedFrames==1,"Frame count regressed");check(g->diagnostics().renderState.find("\"generation\":2")!=std::string::npos,"Generation did not advance");
    state->userError=true;bool rejected=false;try{g->service();}catch(const std::runtime_error&){rejected=true;}check(rejected&&calls==2&&state->alive,"User exception triggered recovery");state->userError=false;
    // A burst of successful but unstable devices cannot retry forever.
    for(int i=0;i<2;++i){g->simulateLoss();g->service();now+=1;g->service();}
    check(calls==4,"Successful reacquisition count wrong");g->simulateLoss();g->service();check(!g->diagnostics().error.empty(),"Rapid repeated loss did not exhaust");now+=10;g->service();check(calls==4,"Exhausted recovery kept allocating");
    failures=3;g->retry();for(int i=0;i<3;++i){now+=10;g->service();}check(calls==7&&!g->diagnostics().error.empty(),"Failed acquisitions were not bounded");
    failures=0;g->retry();g->service();check(g->diagnostics().ready&&calls==8,"Explicit retry failed");
    for(int i=0;i<120;++i)g->render(0,16);g->service();check(g->diagnostics().renderState.find("\"recoveryAttempts\":0")!=std::string::npos,"Stable frames did not reset retry budget");
    g->simulateLoss();g->service();g->bind(nullptr,{});now+=10;g->service();check(calls==8,"Unbound/shutdown scene acquired a device");g.reset();check(!state->alive,"Shutdown retained device");
    std::cout<<"Recovery policy passed: retirement, backoff, handles, retained policy, capture cancellation, exception classification, exhaustion, explicit retry, stable reset and shutdown\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
