#pragma once
#include "engine/math.hpp"
#include <cstdint>
#include <array>
#include <span>

namespace engine {
// Directional shadow volume in world units. Center is local to the light node;
// extent/depth remain world distances so parent scale does not change quality.
struct ShadowSettings {
    bool enabled=false;
    Vec3 center{};
    float halfExtent=10,halfDepth=20;
    float depthBias=0.0005f,normalBias=0.02f;
    void validate() const;
};
// Square orthographic projection with WebGPU [0,1] depth and texel-snapped XY.
Mat4 directionalShadowMatrix(Vec3 direction,Vec3 worldCenter,const ShadowSettings& settings,std::uint32_t resolution);
Mat4 spotShadowMatrix(Vec3 position,Vec3 direction,float outerAngle,float range);
class CameraComponent;
struct SceneLight;
struct RenderQuality;
struct ShadowMapPlan { Mat4 matrix; std::uint32_t light=0; float nearDistance=0,farDistance=0; bool cascade=false; };
struct ShadowPlan { std::array<ShadowMapPlan,4> maps; std::uint32_t count=0; };
// Bounded, owner-thread presentation planning. No scene mutation or retained references.
ShadowPlan planShadows(std::span<const SceneLight> lights,const CameraComponent& camera,float aspect,const RenderQuality& quality,std::uint32_t resolution);
} // namespace engine
