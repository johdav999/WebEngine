#pragma once
#include "engine/component.hpp"
#include "engine/shadow.hpp"
#include <vector>

namespace engine {
enum class LightType { Directional, Point, Spot };
class LightComponent : public Component {
public:
    LightComponent(std::string name, LightType type);
    ~LightComponent() override;
    std::string_view typeName() const noexcept override { return "LightComponent"; }
    LightType type;
    Vec3 color{1,1,1};
    float intensity=1, range=10;
    float innerAngle=pi/9,outerAngle=pi/6; // Half angles in radians; 0 <= inner < outer < pi/2.
    Vec3 direction{0,-1,0}; // Local direction of emitted light, transformed by the hierarchy.
    std::uint32_t visibilityMask=1;
    ShadowSettings shadow; // Supported for Directional and Spot lights; opt in per light.
    void validate() const override;
    void validateSettings() const; // Allocation-free light data check, also used during collection.
};
// Portable reference for the same quadratic range and smooth cone law used by both renderers.
float spotlightAttenuation(float distance,float cosine,float range,float innerAngle,float outerAngle);
struct LightSegment { Vec3 begin,end; };
// World-unit cone rings/rays. No retained scene pointers or effect on collection/physics.
std::vector<LightSegment> spotlightOutline(const LightComponent&,unsigned segments=32);
} // namespace engine
