#pragma once
#include "engine/camera_component.hpp"
#include "engine/render_surface.hpp"
#include <optional>
#include <span>
#include <vector>

namespace engine {
// Caller-owned cameras; order in the list is the compositing order.
// nullopt preserves earlier color, while each view/layer has independent depth.
struct RenderView {
    CameraComponent* camera=nullptr;
    std::optional<std::array<float,4>> clearColor=std::array<float,4>{0.018f,0.026f,0.042f,1};
    bool skyEnabled=true; // Explicitly disable for views preserving earlier background color.
    bool fogEnabled=true; // World materials, particles and sky; never Overlay/GUI.
    RenderSurfaceHandle target{}; // Empty means window. Surface producers run before window views.
    bool refreshSurface=true; // False keeps the last complete capture while the producer is registered.
};
struct ResolvedRenderView {
    std::size_t index=0;
    PixelViewport viewport;
    std::uint32_t x=0,y=0,width=0,height=0; // Scissor; pixel-center rounding.
};
class RenderViews {
public:
    static constexpr std::size_t maxSurfaces=4;
    static constexpr std::uint64_t maxSurfacePixels=4*1024*1024;
    void set(std::span<const RenderView> views); // Validates atomically, accepts an empty list.
    std::span<const RenderView> views() const { return views_; }
    // Window views only. Both resolve methods skip disabled and subpixel-empty views.
    void resolve(std::uint32_t width,std::uint32_t height,std::vector<ResolvedRenderView>& output) const;
    void resolveSurfaces(std::vector<ResolvedRenderView>& output) const;
    bool hasSurface(const RenderSurface* target) const;
private:
    std::vector<RenderView> views_;
};
} // namespace engine
