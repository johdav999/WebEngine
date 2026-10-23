#include "desktop_graphics.hpp"
#include "sdl_gpu_host.hpp"
#include "webgpu_renderer.hpp"

namespace engine {
namespace {
class DawnGraphics final:public DesktopGraphics {
    SdlGpuHost host_;
    GuiSystem& gui_;
    RenderQuality quality_;
    RenderCapabilities empty_;
    bool shadows_=true;
    DesktopBackend backend_;
    std::unique_ptr<WebGpuRenderer> renderer_;
public:
    DawnGraphics(SDL_Window& window,GuiSystem& gui,const RenderQuality& quality,DesktopBackend backend):host_(window,backend==DesktopBackend::DawnVulkan?wgpu::BackendType::Vulkan:backend==DesktopBackend::DawnD3D11?wgpu::BackendType::D3D11:wgpu::BackendType::D3D12),gui_(gui),quality_(quality),backend_(backend){}
    void bind(Scene* scene,std::span<const RenderView> views) override {
        if(!scene){host_.cancelCapture();renderer_.reset();return;}
        if(renderer_){renderer_->setScene(*scene,views);renderer_->setWaterInteractions(WaterInteractions{});return;}
        auto candidate=std::make_unique<WebGpuRenderer>(*scene,host_);candidate->setViews(views);
        candidate->setFallbackAllowed(false);candidate->setGui(&gui_);candidate->setQuality(quality_);candidate->setShadowsEnabled(shadows_);
        renderer_=std::move(candidate);renderer_->initialize();
    }
    void setViews(std::span<const RenderView> views) override {if(renderer_)renderer_->setViews(views);}
    void service() override {if(renderer_)renderer_->serviceRecovery();}
    void setWaterTime(double seconds) override {if(renderer_)renderer_->setWaterTime(seconds);}
    void render(double,double intervalMs) override {if(renderer_)renderer_->render(0,intervalMs);}
    void setQuality(const RenderQuality& q) override {q.validate();if(renderer_)renderer_->setQuality(q);quality_=q;}
    const RenderCapabilities& capabilities() const override {return renderer_?renderer_->capabilities():empty_;}
    void setShadows(bool enabled) override {shadows_=enabled;if(renderer_)renderer_->setShadowsEnabled(enabled);}
    void setWaterInteractions(const WaterInteractions& values) override {if(!renderer_)throw std::logic_error("Water interactions require an active scene");renderer_->setWaterInteractions(values);}
    DesktopDiagnostics diagnostics() const override {
        int w=0,h=0;host_.drawableSize(w,h);
        return {renderer_&&renderer_->ready(),host_.presentedFrames,std::uint32_t(w),std::uint32_t(h),renderer_?renderer_->effectiveMsaaSamples():1,host_.error,host_.stateJson,host_.performanceJson};
    }
    void retry() override {host_.error.clear();if(renderer_)renderer_->retryRecovery();}
    bool simulateLoss() override {if(!renderer_||!renderer_->ready())return false;renderer_->simulateDeviceLoss();return true;}
    void capture(const std::filesystem::path& p) override {host_.capture(p);}
    bool capturePending() const override {return host_.capturePending();}
    std::string captureError() const override {return host_.captureError();}
    DesktopBackend backend() const override {return backend_;}
};
}
std::unique_ptr<DesktopGraphics> makeDawnGraphics(SDL_Window& w,GuiSystem& g,const RenderQuality& q,DesktopBackend b){return std::make_unique<DawnGraphics>(w,g,q,b);}
}
