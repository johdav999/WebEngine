#include "desktop_graphics.hpp"
namespace engine {
std::unique_ptr<DesktopGraphics> makeDesktopGraphics(DesktopBackend backend,SDL_Window& window,GuiSystem& gui,const RenderQuality& quality) {
    if(backend==DesktopBackend::Automatic){
        std::vector<DesktopBackend> choices;
#if WEBENGINE_HAS_DAWN
        choices={DesktopBackend::Dawn,DesktopBackend::DawnVulkan,DesktopBackend::DawnD3D11};
#endif
#if WEBENGINE_HAS_D3D12 && !WEBENGINE_HAS_DAWN
        choices.push_back(DesktopBackend::Direct3D12);
#endif
        return makeFallbackGraphics(std::move(choices),[&window,&gui](DesktopBackend b,const RenderQuality& q){return makeDesktopGraphics(b,window,gui,q);},quality);
    }
    switch(backend){
#if WEBENGINE_HAS_DAWN
    case DesktopBackend::Dawn:return makeDawnGraphics(window,gui,quality);
    case DesktopBackend::DawnD3D11:
    case DesktopBackend::DawnVulkan:return makeDawnGraphics(window,gui,quality,backend);
#endif
#if WEBENGINE_HAS_D3D12
    case DesktopBackend::Direct3D12:return makeRecoveringD3D12Graphics([&window,&gui](DesktopBackend,const RenderQuality& q){return makeD3D12Graphics(window,gui,q);},quality);
#endif
    default:throw std::invalid_argument("Requested desktop graphics backend is unavailable in this build");
    }
}
}
