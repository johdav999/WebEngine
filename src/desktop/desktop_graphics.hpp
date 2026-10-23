#pragma once
#include "engine/desktop.hpp"
#include "sdl_display.hpp"

namespace engine {
inline DesktopSize desktopLogicalSize(SDL_Window& window) {
    int w=0,h=0,pw=0,ph=0;SDL_GetWindowSize(&window,&w,&h);SDL_GetWindowSizeInPixels(&window,&pw,&ph);
    const auto scale=desktopCoordinateScale(window);
    return {float(w)/scale,float(h)/scale,w>0?float(pw)/w*scale:1};
}
// Private owner-thread graphics boundary. Window/GUI outlive the implementation.
// bind(nullptr) releases every scene/view borrow before scene shutdown.
class DesktopGraphics {
public:
    virtual ~DesktopGraphics()=default;
    virtual void bind(Scene*,std::span<const RenderView>)=0;
    virtual void setViews(std::span<const RenderView>)=0;
    virtual void service()=0;
    virtual void render(double seconds,double intervalMs)=0;
    virtual void setWaterTime(double) {} // Host simulation clock, independent of GUI presentation.
    virtual void setQuality(const RenderQuality&)=0;
    virtual const RenderCapabilities& capabilities() const=0;
    virtual void setShadows(bool)=0;
    virtual void setWaterInteractions(const WaterInteractions&)=0;
    virtual DesktopDiagnostics diagnostics() const=0;
    virtual std::string gpuProfileTrace() const {return "{\"traceEvents\":[]}";}
    virtual void retry()=0;
    virtual bool simulateLoss()=0;
    virtual void capture(const std::filesystem::path&)=0;
    virtual bool capturePending() const=0;
    virtual std::string captureError() const=0;
    virtual DesktopBackend backend() const {return DesktopBackend::Dawn;}
    virtual bool simulateFailure() {return false;}
};
using DesktopGraphicsFactory=std::function<std::unique_ptr<DesktopGraphics>(DesktopBackend,const RenderQuality&)>;
// Only a lost device enters reacquisition. Invalid content/user exceptions retain
// their normal failure paths rather than becoming recovery/fallback signals.
class DesktopDeviceLost : public std::runtime_error {public:using std::runtime_error::runtime_error;};
std::unique_ptr<DesktopGraphics> makeRecoveringD3D12Graphics(DesktopGraphicsFactory,const RenderQuality&,std::function<double()> clock={});
// Private injectable composition seam for bounded fallback/lifetime tests.
std::unique_ptr<DesktopGraphics> makeFallbackGraphics(std::vector<DesktopBackend>,DesktopGraphicsFactory,const RenderQuality&);
std::unique_ptr<DesktopGraphics> makeDesktopGraphics(DesktopBackend,SDL_Window&,GuiSystem&,const RenderQuality&);
std::unique_ptr<DesktopGraphics> makeDawnGraphics(SDL_Window&,GuiSystem&,const RenderQuality&,DesktopBackend=DesktopBackend::Dawn);
std::unique_ptr<DesktopGraphics> makeD3D12Graphics(SDL_Window&,GuiSystem&,const RenderQuality&);
}
