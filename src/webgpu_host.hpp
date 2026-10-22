#pragma once
// Private renderer/platform boundary. GPU and OS types never enter engine_core.
#include <webgpu/webgpu_cpp.h>
#include <cstdint>

namespace engine {
struct GpuHostSize { double width=0,height=0,pixelRatio=1; };
class WebGpuHost {
public:
    virtual ~WebGpuHost()=default;
    // Host outlives the renderer; all calls and callbacks run on its owner thread.
    virtual double nowMs() const=0;
    virtual bool available() const { return true; }
    virtual bool timingRequested() const { return true; }
    virtual GpuHostSize logicalSize() const=0;
    virtual void drawableSize(int& width,int& height) const=0;
    virtual void resizeDrawable(std::uint32_t width,std::uint32_t height)=0;
    virtual wgpu::Instance createInstance() { return wgpu::CreateInstance(); }
    virtual void adapterOptions(wgpu::RequestAdapterOptions&) {}
    virtual void adapterSelected(const wgpu::Adapter&) {} // Optional host diagnostics only.
    virtual wgpu::Surface createSurface(const wgpu::Instance&)=0;
    virtual wgpu::CallbackMode callbackMode() const=0;
    virtual void processEvents(const wgpu::Instance&) {}
    virtual wgpu::TextureUsage surfaceUsage() const { return wgpu::TextureUsage::RenderAttachment; }
    virtual void beforeSubmit(const wgpu::Device&,const wgpu::CommandEncoder&,const wgpu::Texture&) {}
    virtual void present(const wgpu::Surface&) {}
    virtual void status(const char* text,bool error)=0;
    virtual void stats(int,int,int) {}
    virtual void performance(const char*,const char*) {}
    virtual void renderState(const char*) {}
};
#ifdef __EMSCRIPTEN__
WebGpuHost& browserWebGpuHost();
#endif
}
