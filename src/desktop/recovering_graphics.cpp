#include "desktop_graphics.hpp"
#include <chrono>

namespace engine {
namespace {
std::string recoveryJson(std::string_view s){std::string r="\"";for(unsigned char c:s){if(c=='"'||c=='\\')r+='\\';r+=c<32?' ':char(c);}return r+'"';}
class RecoveringGraphics final:public DesktopGraphics {
    DesktopGraphicsFactory factory_;std::function<double()> now_;
    std::unique_ptr<DesktopGraphics> active_;
    Scene* scene_=nullptr;
    std::vector<DesktopView> views_;
    RenderQuality quality_;RenderCapabilities empty_;
    WaterInteractions water_;double waterTime_=0,nextAttempt_=0;
    bool shadows_=true,pending_=false;
    unsigned attempts_=0;std::uint64_t generation_=0,frames_=0;
    std::string terminal_,reason_,captureError_;
    std::vector<DesktopView> handles(Scene* scene,std::span<const RenderView> views)const{
        RenderViews checked;checked.set(views);if(views.size()>32)throw std::invalid_argument("D3D12 view budget exceeded");
        std::vector<DesktopView> result;for(const auto& v:views){if(!scene||v.camera->scene()!=scene)throw std::invalid_argument("Recovery camera must belong to its scene");result.push_back({v.camera->id(),v.clearColor,v.skyEnabled,v.fogEnabled});}return result;
    }
    std::vector<RenderView> resolve()const{
        std::vector<RenderView> result;if(scene_)for(const auto& v:views_)if(auto* c=dynamic_cast<CameraComponent*>(scene_->findComponent(v.camera)))result.push_back({c,v.clearColor,v.skyEnabled,v.fogEnabled});return result;
    }
    void retire(){
        if(!active_)return;frames_+=active_->diagnostics().presentedFrames;
        if(active_->capturePending())captureError_="Framebuffer capture cancelled by D3D12 device recovery";
        active_->bind(nullptr,{});active_.reset();
    }
    void lost(const DesktopDeviceLost& e){
        reason_=std::string(e.what()).substr(0,16384);retire();
        if(attempts_>=3){pending_=false;terminal_="D3D12 recovery exhausted: "+reason_;return;}
        pending_=true;nextAttempt_=now_()+.05*(1u<<attempts_);
    }
    void acquire(){
        ++attempts_;
        try{
            auto candidate=factory_(DesktopBackend::Direct3D12,quality_);if(!candidate)throw std::runtime_error("D3D12 factory returned no backend");
            candidate->setShadows(shadows_);candidate->setWaterTime(waterTime_);
            if(scene_){const auto views=resolve();candidate->bind(scene_,views);candidate->setWaterInteractions(water_);}
            active_=std::move(candidate);empty_=active_->capabilities();++generation_;pending_=false;terminal_.clear();
        }catch(const std::invalid_argument& e){pending_=false;terminal_=std::string("Invalid D3D12 recovery candidate: ")+e.what();}
        catch(const std::exception& e){reason_=std::string(e.what()).substr(0,16384);pending_=attempts_<3;nextAttempt_=now_()+.05*(1u<<attempts_);if(!pending_)terminal_="D3D12 recovery exhausted: "+reason_;}
    }
public:
    RecoveringGraphics(DesktopGraphicsFactory factory,const RenderQuality& q,std::function<double()> clock):factory_(std::move(factory)),now_(std::move(clock)),quality_(q){
        q.validate();if(!factory_)throw std::invalid_argument("Missing D3D12 recovery factory");
        if(!now_)now_=[] {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();};
        empty_.backend=RenderBackend::Direct3D12;empty_.pbr=empty_.shadowMaps=empty_.instancing=empty_.spotLights=empty_.spotLightShadows=false;
        acquire();if(active_)attempts_=0;
    }
    ~RecoveringGraphics()override{if(active_)active_->bind(nullptr,{});}
    void bind(Scene* scene,std::span<const RenderView> views)override{
        if(!scene){if(active_)active_->bind(nullptr,{});scene_=nullptr;views_.clear();water_={};return;}
        auto candidate=handles(scene,views);if(active_)active_->bind(scene,views);scene_=scene;views_=std::move(candidate);water_={};
    }
    void setViews(std::span<const RenderView> views)override{auto candidate=handles(scene_,views);if(active_)active_->setViews(views);views_=std::move(candidate);}
    void service()override{
        if(active_){try{active_->service();if(active_->diagnostics().presentedFrames>=120)attempts_=0;}catch(const DesktopDeviceLost& e){lost(e);}return;}
        if(pending_&&scene_&&now_()>=nextAttempt_)acquire();
    }
    void render(double seconds,double interval)override{if(active_)try{active_->render(seconds,interval);}catch(const DesktopDeviceLost& e){lost(e);}}
    void setWaterTime(double seconds)override{validateWaterTime(seconds);if(active_)active_->setWaterTime(seconds);waterTime_=seconds;}
    void setQuality(const RenderQuality& q)override{q.validate();if(active_)try{active_->setQuality(q);}catch(const DesktopDeviceLost& e){lost(e);}quality_=q;}
    // Stable host-owned value: replacing the GPU must not dangle capability borrows.
    const RenderCapabilities& capabilities()const override{return empty_;}
    void setShadows(bool v)override{if(active_)active_->setShadows(v);shadows_=v;}
    void setWaterInteractions(const WaterInteractions& v)override{if(!scene_)throw std::logic_error("Water interactions require an active scene");if(active_)active_->setWaterInteractions(v);water_=v;}
    DesktopDiagnostics diagnostics()const override{
        auto d=active_?active_->diagnostics():DesktopDiagnostics{};d.presentedFrames+=frames_;d.backend=DesktopBackend::Direct3D12;
        if(!terminal_.empty())d.error=terminal_;
        if(d.renderState.empty()||d.renderState=="{}")d.renderState="{\"backend\":\"d3d12\",\"state\":"+recoveryJson(active_&&d.ready?"ready":pending_?"recovering":"failed")+",\"generation\":"+std::to_string(generation_)+"}";
        else if(const auto at=d.renderState.find("\"generation\":");at!=std::string::npos){const auto begin=at+13,end=d.renderState.find_first_of(",}",begin);d.renderState.replace(begin,end-begin,std::to_string(generation_));}
        if(d.renderState.back()=='}'){d.renderState.pop_back();d.renderState+=",\"deviceRecovery\":true,\"recoveryAttempts\":"+std::to_string(attempts_)+",\"lastDeviceLoss\":"+recoveryJson(reason_)+"}";}return d;
    }
    void retry()override{if(active_)return;terminal_.clear();attempts_=0;pending_=true;nextAttempt_=now_();}
    std::string gpuProfileTrace()const override{return active_?active_->gpuProfileTrace():"{\"traceEvents\":[]}";}
    bool simulateLoss()override{return active_&&active_->diagnostics().ready&&active_->simulateLoss();}
    DesktopBackend backend()const override{return DesktopBackend::Direct3D12;}
    void capture(const std::filesystem::path& path)override{if(!active_)throw std::logic_error("D3D12 capture requires a ready device");active_->capture(path);captureError_.clear();}
    bool capturePending()const override{return active_&&active_->capturePending();}
    std::string captureError()const override{return !captureError_.empty()?captureError_:active_?active_->captureError():std::string{};}
};
}
std::unique_ptr<DesktopGraphics> makeRecoveringD3D12Graphics(DesktopGraphicsFactory f,const RenderQuality& q,std::function<double()> clock){return std::make_unique<RecoveringGraphics>(std::move(f),q,std::move(clock));}
}
