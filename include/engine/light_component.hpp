#pragma once
#include "engine/component.hpp"
#include "engine/shadow.hpp"

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
    // Cone half angles in radians, with a smooth penumbra between them.
    float innerCone=.20f,outerCone=.35f;
    void validate() const override;
    Vec3 direction{0,-1,0}; // Local direction of emitted light, transformed by the hierarchy.
    std::uint32_t visibilityMask=1;
    ShadowSettings shadow; // Supported for Directional lights; opt in per light.
};
} // namespace engine
