#pragma once
#include "engine/camera_component.hpp"
#include <cstddef>
#include <optional>
#include <vector>

namespace engine {
inline constexpr unsigned maxAdditionalFogVolumes=3;
struct FogVolume {
    Vec3 minimum{-5,0,-5},maximum{5,4,5}; // World-space box.
    Vec3 scattering{.8f,.8f,.8f};
    float density=.12f,edgeFade=.25f,maxDistance=60;
    void validate() const;
};
// Linear display-light color. Distances are world units; density/falloff are inverse units.
struct FogSettings {
    Vec3 color{0.65f,0.75f,0.85f};
    float density=0.02f,startDistance=0,baseHeight=0,heightFalloff=0.15f;
    std::optional<FogVolume> volume;
    std::vector<FogVolume> additionalVolumes;
    void validate() const;
};
struct FogVolumeUniform {
    std::array<float,4> minimumFade{},maximumDistance{},colorDensity{};
};
static_assert(sizeof(FogVolumeUniform)==48);
struct FogLightCacheUniform {
    std::array<float,4> minimumIndex{},maximum{};
};
static_assert(sizeof(FogLightCacheUniform)==32);
struct FogUniform {
    std::array<float,4> colorDensity{},height{},eye{},forward{};
    std::array<float,4> volumeMinSteps{},volumeMaxDistance{},volumeColorDensity{},volumeOptions{};
    std::array<FogVolumeUniform,maxAdditionalFogVolumes> additionalVolumes{};
    std::array<float,4> volumeLighting{}; // Active boxes, scattering light budget, cache slots, cache dimension.
    std::array<FogLightCacheUniform,4> lightCache{};
};
static_assert(sizeof(FogUniform)==416 && offsetof(FogUniform,additionalVolumes)==128 &&
              offsetof(FogUniform,volumeLighting)==272 && offsetof(FogUniform,lightCache)==288);
bool hasActiveFogVolume(const std::optional<FogSettings>& fog);
FogUniform prepareFog(const std::optional<FogSettings>& fog,const CameraComponent& camera,
                      bool enabled=true,unsigned steps=0,bool selfShadows=false);
// Analytic integral; startDistance removes the beginning of this segment.
float fogTransmission(const FogSettings& fog,Vec3 origin,Vec3 endpoint);
} // namespace engine
