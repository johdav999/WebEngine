#pragma once
#include "engine/camera_component.hpp"
#include <optional>

namespace engine {
// Linear display-light color. Distances are world units; density/falloff are inverse units.
struct FogSettings {
    Vec3 color{0.65f,0.75f,0.85f};
    float density=0.02f,startDistance=0,baseHeight=0,heightFalloff=0.15f;
    void validate() const;
};
struct FogUniform {
    std::array<float,4> colorDensity{},height{},eye{},forward{};
};
static_assert(sizeof(FogUniform)==64);
FogUniform prepareFog(const std::optional<FogSettings>& fog,const CameraComponent& camera,bool enabled=true);
// Analytic integral; startDistance removes the beginning of this segment.
float fogTransmission(const FogSettings& fog,Vec3 origin,Vec3 endpoint);
} // namespace engine
