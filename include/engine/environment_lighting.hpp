#pragma once
#include "engine/environment.hpp"
#include <span>

namespace engine {
inline constexpr std::size_t maxLightingZones = 2;

// World-space influence box for an immutable diffuse and specular bake.
// Fade starts at the box faces and reaches full influence blendDistance inside.
struct LightingZone {
    Vec3 minimum{-5, 0, -5};
    Vec3 maximum{5, 4, 5};
    float blendDistance = 1;
    std::shared_ptr<const Environment> environment;
    float intensity = 1;
    float rotation = 0; // Radians about world Y, independent of the global environment.

    void validate() const;
};

struct LightingZoneWeights {
    float global = 1;
    std::array<float, maxLightingZones> local{};
};

// Reference for the same continuous position-based influence used in shaders.
// Takes validated zones. Empty sets and non-finite positions select the global source.
LightingZoneWeights sampleLightingZones(std::span<const LightingZone> zones, Vec3 position);

struct LightingEnvironmentArray {
    std::vector<EnvironmentLevel> specular;
    EnvironmentLevel diffuse;
    std::uint32_t cubeCount = 0;
};

// Same face/mip dimensions as global. Each level contains the global cube followed
// by the local cubes in zone order, each in +X,-X,+Y,-Y,+Z,-Z face order.
// An optional reflection cache follows the zones. Its diffuse cube is packed for
// resource layout consistency but reflection receivers use only its specular cube.
// The BRDF LUT remains shared and comes from global.
LightingEnvironmentArray packLightingEnvironments(const Environment& global,
                                                   std::span<const LightingZone> zones,
                                                   const Environment* reflection=nullptr);
} // namespace engine
