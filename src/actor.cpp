#include "engine/actor.hpp"
#include "engine/scene.hpp"

namespace engine {
Actor::Actor(std::string name) : Node(std::move(name)) {}
Actor::~Actor() = default;
void Actor::destroy() {
    if(!scene()) throw std::logic_error("Actor destruction requires an owning scene");
    scene()->queueRemoval(*this);
}
bool Actor::removeComponent(Component& component) {
    if(component.owner()!=this || !component.parent()) return false;
    return component.parent()->removeChild(component);
}
bool Actor::hasTag(std::string_view tag) const {
    return std::find(tags_.begin(),tags_.end(),tag)!=tags_.end();
}
bool Actor::addTag(std::string tag) {
    if(tag.empty()) throw std::invalid_argument("Actor tags cannot be empty");
    if(hasTag(tag)) return false;
    tags_.push_back(std::move(tag));return true;
}
bool Actor::removeTag(std::string_view tag) {
    const auto found=std::find(tags_.begin(),tags_.end(),tag);
    if(found==tags_.end()) return false;
    tags_.erase(found);return true;
}
} // namespace engine
