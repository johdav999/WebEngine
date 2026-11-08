#pragma once
#include "engine/scene.hpp"

namespace engine {
struct RenderQuality;
struct ViewLighting {
    std::array<SceneLight,8> lights{};
    std::uint32_t lightCount=0;
    std::size_t eligibleCount=0;
    ShadowPlan shadows;
    std::string diagnosticsJson() const; // Light IDs are scene-local runtime handles.
};

// Keep one history per view. Stores handles only, and resets across camera/scene changes.
class ViewLightSelection {
public:
    ViewLighting prepare(std::span<const SceneLight> lights,const CameraComponent& camera,float aspect,
                         const RenderQuality& quality,std::uint32_t shadowResolution,bool shadowsEnabled);
private:
    ComponentId camera_;
    std::array<ComponentId,8> previous_{};
};
} // namespace engine
