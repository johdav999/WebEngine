#include "engine/light_component.hpp"

namespace engine {
LightComponent::LightComponent(std::string name, LightType type)
    : Component(std::move(name)), type(type) {}
LightComponent::~LightComponent() = default;
} // namespace engine
