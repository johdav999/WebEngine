#pragma once
#include "engine/math.hpp"
#include <cstdint>

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
} // namespace engine
