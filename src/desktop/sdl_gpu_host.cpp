#include "sdl_gpu_host.hpp"
#include "sdl_display.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace engine {
struct SdlGpuHost::Capture {
    wgpu::Buffer buffer;
    std::filesystem::path path;
    std::uint32_t width=0,height=0,stride=0;
    bool bgra=false,submitted=false,done=false;
    std::string error;
};
double SdlGpuHost::nowMs() const { return double(SDL_GetTicksNS())/1e6; }
GpuHostSize SdlGpuHost::logicalSize() const {
    int w=0,h=0,pw=0,ph=0;SDL_GetWindowSize(&window_,&w,&h);SDL_GetWindowSizeInPixels(&window_,&pw,&ph);
    const auto scale=desktopCoordinateScale(window_);
    return {double(w)/scale,double(h)/scale,w>0?double(pw)/w*scale:1};
}
void SdlGpuHost::drawableSize(int& w,int& h) const {
    if(SDL_GetWindowFlags(&window_)&SDL_WINDOW_MINIMIZED) { w=h=0;return; }
    w=int(width_);h=int(height_);
}
void SdlGpuHost::resizeDrawable(std::uint32_t w,std::uint32_t h) { width_=w;height_=h; }
void SdlGpuHost::adapterOptions(wgpu::RequestAdapterOptions& options) { options.backendType=wgpu::BackendType::D3D12; }
void SdlGpuHost::adapterSelected(const wgpu::Adapter& adapter) {
    wgpu::AdapterInfo info{};if(adapter.GetInfo(&info)!=wgpu::Status::Success){adapterJson_="null";return;}
    auto quoted=[](wgpu::StringView value){
        const std::string source=value.data?std::string(value.data,value.length==WGPU_STRLEN?std::strlen(value.data):value.length):std::string{};
        std::string result="\"";for(unsigned char c:source){if(c=='"'||c=='\\'){result+='\\';result+=c;}else if(c<32)result+=' ';else result+=c;}return result+'"';
    };
    adapterJson_="{\"vendor\":"+quoted(info.vendor)+",\"architecture\":"+quoted(info.architecture)+",\"device\":"+quoted(info.device)+",\"description\":"+quoted(info.description)+
        ",\"vendorId\":"+std::to_string(info.vendorID)+",\"deviceId\":"+std::to_string(info.deviceID)+",\"backend\":\"D3D12\",\"software\":"+(info.adapterType==wgpu::AdapterType::CPU?"true":"false")+"}";
}
void SdlGpuHost::renderState(const char* json){stateJson=json;if(!stateJson.empty()&&stateJson.back()=='}'){stateJson.pop_back();stateJson+=",\"adapter\":"+adapterJson_+"}";}}
wgpu::Surface SdlGpuHost::createSurface(const wgpu::Instance& instance) {
    auto* handle=SDL_GetPointerProperty(SDL_GetWindowProperties(&window_),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
    if(!handle) throw std::runtime_error("SDL did not provide a Windows HWND");
    wgpu::SurfaceSourceWindowsHWND source{};source.hwnd=handle;source.hinstance=GetModuleHandleW(nullptr);
    wgpu::SurfaceDescriptor descriptor{};descriptor.nextInChain=&source;
    return instance.CreateSurface(&descriptor);
}
wgpu::TextureUsage SdlGpuHost::surfaceUsage() const { return wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::CopySrc; }
void SdlGpuHost::status(const char* text,bool failed) {
    if(failed) error=text;
    (failed?std::cerr:std::cout)<<text<<std::endl;
}
void SdlGpuHost::capture(const std::filesystem::path& path) {
    if(capturePending()) throw std::logic_error("Framebuffer capture already pending");
    capture_.reset();requestedCapture_=path;
}
bool SdlGpuHost::capturePending() const { return !requestedCapture_.empty()||(capture_&&!capture_->done); }
void SdlGpuHost::cancelCapture() {
    if(!capturePending())return;
    requestedCapture_.clear();
    if(!capture_)capture_=std::make_shared<Capture>();
    capture_->error="Framebuffer capture cancelled by shutdown";capture_->done=true;
    if(capture_->buffer)capture_->buffer.Destroy();
}
std::string SdlGpuHost::captureError() const { return capture_?capture_->error:std::string{}; }
void SdlGpuHost::beforeSubmit(const wgpu::Device& device,const wgpu::CommandEncoder& encoder,const wgpu::Texture& texture) {
    if(requestedCapture_.empty()) return;
    auto c=std::make_shared<Capture>();c->path=std::move(requestedCapture_);requestedCapture_.clear();
    c->width=width_;c->height=height_;c->stride=(width_*4+255)&~255u;
    const auto format=texture.GetFormat();
    c->bgra=format==wgpu::TextureFormat::BGRA8Unorm||format==wgpu::TextureFormat::BGRA8UnormSrgb;
    if(!c->bgra&&format!=wgpu::TextureFormat::RGBA8Unorm&&format!=wgpu::TextureFormat::RGBA8UnormSrgb)
        throw std::runtime_error("Framebuffer capture requires an 8-bit color surface");
    wgpu::BufferDescriptor descriptor{};descriptor.size=std::uint64_t(c->stride)*c->height;
    descriptor.usage=wgpu::BufferUsage::CopyDst|wgpu::BufferUsage::MapRead;c->buffer=device.CreateBuffer(&descriptor);
    wgpu::TexelCopyTextureInfo source{};source.texture=texture;
    wgpu::TexelCopyBufferInfo destination{};destination.buffer=c->buffer;destination.layout.bytesPerRow=c->stride;destination.layout.rowsPerImage=c->height;
    const wgpu::Extent3D extent{c->width,c->height,1};encoder.CopyTextureToBuffer(&source,&destination,&extent);
    capture_=std::move(c);
}
void SdlGpuHost::present(const wgpu::Surface& surface) {
    surface.Present();++presentedFrames;
    if(!capture_||capture_->submitted) return;
    auto c=capture_;c->submitted=true;
    c->buffer.MapAsync(wgpu::MapMode::Read,0,std::uint64_t(c->stride)*c->height,callbackMode(),
        [c](wgpu::MapAsyncStatus status,wgpu::StringView) {
            if(c->done)return;
            if(status!=wgpu::MapAsyncStatus::Success) { c->error="Framebuffer readback failed";c->done=true;return; }
            try {
                auto* pixels=static_cast<const unsigned char*>(c->buffer.GetConstMappedRange());
                std::ofstream out(c->path,std::ios::binary);out.exceptions(std::ios::badbit|std::ios::failbit);
                out<<"P6\n"<<c->width<<' '<<c->height<<"\n255\n";
                for(std::uint32_t y=0;y<c->height;++y) for(std::uint32_t x=0;x<c->width;++x) {
                    const auto* p=pixels+y*c->stride+x*4;const char rgb[]={char(p[c->bgra?2:0]),char(p[1]),char(p[c->bgra?0:2])};out.write(rgb,3);
                }
            } catch(const std::exception& e) { c->error=e.what(); }
            c->buffer.Unmap();c->buffer.Destroy();c->done=true;
        });
}
}
