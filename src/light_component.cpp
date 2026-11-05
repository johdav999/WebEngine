#include "engine/light_component.hpp"

namespace engine {
LightComponent::LightComponent(std::string name, LightType type)
    : Component(std::move(name)), type(type) {}
LightComponent::~LightComponent() = default;
void LightComponent::validate() const {
    Component::validate();
    if(type<LightType::Directional||type>LightType::Spot||!std::isfinite(intensity)||intensity<0||!std::isfinite(range)||range<=0||
       !std::isfinite(innerCone)||!std::isfinite(outerCone)||innerCone<0||outerCone<=innerCone||outerCone>=pi/2||
       !std::isfinite(dot(direction,direction))||dot(direction,direction)<1e-12f)
        throw std::invalid_argument("Invalid light settings");
    for(float c:{color.x,color.y,color.z})if(!std::isfinite(c)||c<0)throw std::invalid_argument("Invalid light color");
}
} // namespace engine
