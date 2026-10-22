#include "webgpu_host.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>

EM_JS(void, host_status, (const char* text,int error), { window.engineStatus(UTF8ToString(text),!!error); });
EM_JS(void, host_stats, (int meshes,int lights,int layers), { window.engineStats(meshes,lights,layers); });
EM_JS(void, host_performance, (const char* json,const char* build), { const snapshot=JSON.parse(UTF8ToString(json));snapshot.build=UTF8ToString(build);window.enginePerformance(snapshot); });
EM_JS(int, host_timing_requested, (), { return new URLSearchParams(location.search).get('gpuTiming') !== 'off'; });
EM_JS(int, host_available, (), { return isSecureContext && !!navigator.gpu; });
EM_JS(void, host_render_state, (const char* json), { window.EngineQuality?.report(JSON.parse(UTF8ToString(json))); });
EM_JS(double, host_css_width, (), { return document.querySelector('#viewport').clientWidth; });
EM_JS(double, host_css_height, (), { return document.querySelector('#viewport').clientHeight; });
EM_JS(double, host_pixel_ratio, (), { return window.devicePixelRatio || 1; });

namespace engine {
namespace {
class BrowserWebGpuHost final : public WebGpuHost {
public:
    double nowMs() const override { return emscripten_get_now(); }
    bool available() const override { return host_available(); }
    bool timingRequested() const override { return host_timing_requested(); }
    GpuHostSize logicalSize() const override { return {host_css_width(),host_css_height(),host_pixel_ratio()}; }
    void drawableSize(int& w,int& h) const override { emscripten_get_canvas_element_size("#viewport",&w,&h); }
    void resizeDrawable(std::uint32_t w,std::uint32_t h) override { emscripten_set_canvas_element_size("#viewport",w,h); }
    wgpu::Surface createSurface(const wgpu::Instance& instance) override {
        wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas{};canvas.selector="#viewport";
        wgpu::SurfaceDescriptor descriptor{};descriptor.nextInChain=&canvas;
        return instance.CreateSurface(&descriptor);
    }
    wgpu::CallbackMode callbackMode() const override { return wgpu::CallbackMode::AllowSpontaneous; }
    void status(const char* text,bool error) override { host_status(text,error); }
    void stats(int meshes,int lights,int layers) override { host_stats(meshes,lights,layers); }
    void performance(const char* json,const char* build) override { host_performance(json,build); }
    void renderState(const char* json) override { host_render_state(json); }
};
}
WebGpuHost& browserWebGpuHost() { static BrowserWebGpuHost host;return host; }
}
