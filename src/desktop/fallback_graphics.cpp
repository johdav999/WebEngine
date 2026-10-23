#include "desktop_graphics.hpp"
#include <algorithm>
#include <sstream>

namespace engine {
namespace {
const char* name(DesktopBackend b){switch(b){case DesktopBackend::Dawn:return "dawn-d3d12";case DesktopBackend::DawnVulkan:return "dawn-vulkan";case DesktopBackend::DawnD3D11:return "dawn-d3d11";case DesktopBackend::Direct3D12:return "direct-d3d12";default:return "automatic";}}
std::string jsonString(std::string_view s){std::string r="\"";for(unsigned char c:s){if(c=='"'||c=='\\')r+='\\';r+=c<32?' ':char(c);}return r+'"';}
class FallbackGraphics final:public DesktopGraphics {
    std::vector<DesktopBackend> choices_;
    DesktopGraphicsFactory factory_;
    std::unique_ptr<DesktopGraphics> active_;
    RenderQuality quality_;
    RenderCapabilities empty_;
    WaterInteractions water_;
    Scene* scene_=nullptr; // Released by Application's existing bind(nullptr) boundary.
    std::vector<DesktopView> views_; // Handles, never pointers retained across recovery.
    std::vector<DesktopDiagnostics::Attempt> attempts_;
    std::size_t next_=0;
    DesktopBackend selected_=DesktopBackend::Automatic;
    bool shadows_=true,forcedFailure_=false;
    std::string terminal_,captureError_;
    std::uint64_t frames_=0;
    static std::vector<DesktopView> handles(Scene* scene,std::span<const RenderView> views){
        std::vector<DesktopView> result;result.reserve(views.size());
        for(const auto& v:views){if(!scene||!v.camera||v.camera->scene()!=scene)throw std::invalid_argument("Fallback camera must belong to its scene");result.push_back({v.camera->id(),v.clearColor,v.skyEnabled,v.fogEnabled});}return result;
    }
    std::vector<RenderView> resolve()const{
        std::vector<RenderView> result;if(scene_)for(const auto& v:views_)if(auto* c=dynamic_cast<CameraComponent*>(scene_->findComponent(v.camera)))result.push_back({c,v.clearColor,v.skyEnabled,v.fogEnabled});return result;
    }
    void retire(){if(!active_)return;frames_+=active_->diagnostics().presentedFrames;if(active_->capturePending())captureError_="Framebuffer capture cancelled by graphics fallback";active_->bind(nullptr,{});active_.reset();}
    void fail(std::string reason){if(!attempts_.empty())attempts_.back().error=reason.substr(0,16384);}
    void next(){
        // Destroy the previous surface/device before creating a new swapchain for
        // this HWND. Destructors invalidate outstanding GPU callbacks/readbacks.
        retire();
        while(next_<choices_.size()){
            selected_=choices_[next_++];attempts_.push_back({selected_,{}});
            try{active_=factory_(selected_,quality_);if(!active_)throw std::runtime_error("Graphics factory returned no backend");active_->setShadows(shadows_);if(scene_){const auto views=resolve();active_->bind(scene_,views);active_->setWaterInteractions(water_);}return;}
            catch(const std::exception& e){fail(e.what());retire();}
        }
        std::ostringstream out;out<<"All desktop graphics backends failed";for(const auto& a:attempts_)out<<"; "<<name(a.backend)<<": "<<a.error;terminal_=out.str();
    }
public:
    FallbackGraphics(std::vector<DesktopBackend> choices,DesktopGraphicsFactory factory,const RenderQuality& q):choices_(std::move(choices)),factory_(std::move(factory)),quality_(q){
        if(choices_.empty()||choices_.size()>3||!factory_)throw std::invalid_argument("Invalid desktop fallback policy");q.validate();
        for(std::size_t i=0;i<choices_.size();++i)if(choices_[i]==DesktopBackend::Automatic||std::find(choices_.begin(),choices_.begin()+i,choices_[i])!=choices_.begin()+i)throw std::invalid_argument("Invalid duplicate/recursive fallback backend");next();
    }
    ~FallbackGraphics()override{if(active_)active_->bind(nullptr,{});}
    void bind(Scene* scene,std::span<const RenderView> views)override{
        if(!scene){if(active_)active_->bind(nullptr,{});scene_=nullptr;views_.clear();water_={};return;}
        auto candidate=handles(scene,views);if(active_)active_->bind(scene,views);if(scene_!=scene){water_={};if(active_)active_->setWaterInteractions(water_);}scene_=scene;views_=std::move(candidate);
    }
    void setViews(std::span<const RenderView> views)override{auto candidate=handles(scene_,views);if(active_)active_->setViews(views);views_=std::move(candidate);}
    void service()override{
        if(!active_||!scene_)return;
        try{if(forcedFailure_){forcedFailure_=false;throw std::runtime_error("Diagnostic terminal graphics failure");}active_->service();const auto d=active_->diagnostics();if(d.error.empty())return;fail(d.error);}
        catch(const std::exception& e){fail(e.what());}
        next();
    }
    void render(double seconds,double interval)override{if(active_)active_->render(seconds,interval);}
    void setWaterTime(double seconds)override{if(active_)active_->setWaterTime(seconds);}
    void setQuality(const RenderQuality& q)override{q.validate();if(active_)active_->setQuality(q);quality_=q;}
    const RenderCapabilities& capabilities()const override{return active_?active_->capabilities():empty_;}
    void setShadows(bool v)override{if(active_)active_->setShadows(v);shadows_=v;}
    void setWaterInteractions(const WaterInteractions& values)override{if(!scene_)throw std::logic_error("Water interactions require an active scene");if(active_)active_->setWaterInteractions(values);water_=values;}
    DesktopDiagnostics diagnostics()const override{
        auto d=active_?active_->diagnostics():DesktopDiagnostics{};d.presentedFrames+=frames_;if(!terminal_.empty())d.error=terminal_;d.backend=selected_;d.requestedBackend=DesktopBackend::Automatic;d.graphicsAttempts=attempts_;
        if(!d.renderState.empty()&&d.renderState.back()=='}'){d.renderState.pop_back();d.renderState+=",\"automatic\":true,\"desktopBackend\":"+jsonString(name(selected_))+",\"graphicsAttempts\":[";bool first=true;for(const auto& a:attempts_){if(!first)d.renderState+=',';first=false;d.renderState+="{\"backend\":"+jsonString(name(a.backend))+",\"error\":"+jsonString(a.error)+"}";}d.renderState+="]}";}return d;
    }
    void retry()override{if(terminal_.empty()){if(active_)active_->retry();return;}terminal_.clear();attempts_.clear();next_=0;next();}
    bool simulateLoss()override{return active_&&active_->simulateLoss();}
    std::string gpuProfileTrace()const override{return active_?active_->gpuProfileTrace():"{\"traceEvents\":[]}";}
    bool simulateFailure()override{if(!active_||!active_->diagnostics().ready)return false;forcedFailure_=true;return true;}
    DesktopBackend backend()const override{return selected_;}
    void capture(const std::filesystem::path& path)override{if(!active_)throw std::runtime_error(terminal_);active_->capture(path);captureError_.clear();}
    bool capturePending()const override{return active_&&active_->capturePending();}
    std::string captureError()const override{return captureError_.empty()&&active_?active_->captureError():captureError_;}
};
}
std::unique_ptr<DesktopGraphics> makeFallbackGraphics(std::vector<DesktopBackend> choices,DesktopGraphicsFactory factory,const RenderQuality& q){return std::make_unique<FallbackGraphics>(std::move(choices),std::move(factory),q);}
}
