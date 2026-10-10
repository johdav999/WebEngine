#pragma once
#include "engine/camera_component.hpp"
#include <optional>
#include <span>
#include <vector>

namespace engine {
// Caller-owned cameras; order in the list is the compositing order.
// nullopt preserves earlier color, while each view/layer has independent depth.
struct RenderView {
    CameraComponent* camera=nullptr;
    std::optional<std::array<float,4>> clearColor=std::array<float,4>{0.018f,0.026f,0.042f,1};
};
struct ResolvedRenderView {
    std::size_t index=0;
    PixelViewport viewport;
    std::uint32_t x=0,y=0,width=0,height=0; // Scissor; pixel-center rounding.
};
class RenderViews {
public:
    void set(std::span<const RenderView> views); // Validates atomically, accepts an empty list.
    std::span<const RenderView> views() const { return views_; }
    // Reuses output storage; disabled cameras and subpixel-empty views are skipped.
    void resolve(std::uint32_t width,std::uint32_t height,std::vector<ResolvedRenderView>& output) const;
private:
    std::vector<RenderView> views_;
};
} // namespace engine
