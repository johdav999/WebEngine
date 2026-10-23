#pragma once
#include "webgpu_host.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <string>

namespace engine {
// Private Windows presentation adapter; owns no scene or simulation clock.
class SdlGpuHost final : public WebGpuHost {
public:
    explicit SdlGpuHost(SDL_Window& window,wgpu::BackendType backend=wgpu::BackendType::D3D12):window_(window),backend_(backend) {}
    double nowMs() const override;
    bool timingRequested() const override;
    GpuHostSize logicalSize() const override;
    void drawableSize(int&,int&) const override;
    void resizeDrawable(std::uint32_t,std::uint32_t) override;
    wgpu::Instance createInstance() override;
    void adapterOptions(wgpu::RequestAdapterOptions&) override;
    void adapterSelected(const wgpu::Adapter&) override;
    wgpu::Surface createSurface(const wgpu::Instance&) override;
    wgpu::CallbackMode callbackMode() const override { return wgpu::CallbackMode::AllowProcessEvents; }
    void processEvents(const wgpu::Instance& instance) override { instance.ProcessEvents(); }
    wgpu::TextureUsage surfaceUsage() const override;
    void beforeSubmit(const wgpu::Device&,const wgpu::CommandEncoder&,const wgpu::Texture&) override;
    void present(const wgpu::Surface&) override;
    void status(const char*,bool) override;
    void performance(const char* json,const char*) override { performanceJson=json; }
    void renderState(const char* json) override;
    // One bounded asynchronous framebuffer readback. Callback owns its data, not this host.
    void capture(const std::filesystem::path&);
    void cancelCapture();
    bool capturePending() const;
    std::string captureError() const;
    std::string error,performanceJson="{}",stateJson="{}";
    std::uint64_t presentedFrames=0;
private:
    struct Capture;
    SDL_Window& window_; // Window must outlive this host and its renderer.
    wgpu::BackendType backend_;
    std::uint32_t width_=1,height_=1;
    std::filesystem::path requestedCapture_;
    std::shared_ptr<Capture> capture_;
    std::string adapterJson_="null";
};
}
