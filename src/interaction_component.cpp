#include "engine/interaction_component.hpp"

namespace engine {
void InteractionComponent::addAction(InteractionAction action) {
    if(action.id.empty()) throw std::invalid_argument("Interaction action ID must not be empty");
    for(const auto& existing:actions_) if(existing.id==action.id) throw std::invalid_argument("Duplicate interaction action ID");
    if(action.label.empty()) action.label=action.id;
    actions_.push_back(std::move(action));
    ++actionRevision_;
}
bool InteractionComponent::removeAction(std::string_view id) {
    const auto found=std::find_if(actions_.begin(),actions_.end(),[&](const auto& action) { return action.id==id; });
    if(found==actions_.end()) return false;
    actions_.erase(found);++actionRevision_;return true;
}
void InteractionComponent::validate() const {
    Component::validate();
    if(!proxyBounds) return;
    const auto& b=*proxyBounds;
    const auto finite=[](Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); };
    if(!b.valid||!finite(b.minimum)||!finite(b.maximum)||b.minimum.x>b.maximum.x||b.minimum.y>b.maximum.y||b.minimum.z>b.maximum.z)
        throw std::invalid_argument("Invalid interaction proxy bounds");
}
} // namespace engine
