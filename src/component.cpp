#include "engine/component.hpp"
#include "engine/actor.hpp"
#include "engine/scene.hpp"

namespace engine {
Component::Component(std::string name) : Node(std::move(name)) {}
Component::~Component() = default;
const Actor* Component::owner() const {
    for(auto* node=parent();node;node=node->parent())
        if(auto* actor=dynamic_cast<const Actor*>(node)) return actor;
    return nullptr;
}
Actor* Component::owner() { return const_cast<Actor*>(std::as_const(*this).owner()); }
void Component::refreshOwner() noexcept {
    auto* current=owner();
    if(current==notifiedOwner_) return;
    auto* previous=notifiedOwner_;notifiedOwner_=current;onOwnerChanged(previous,current);
}
void Component::visitOwnerComponents(const std::function<bool(const Component&)>& visitor) const {
    if(const auto* actor=owner()) {
        auto query=[&](const Node& node) {
            const auto* component=dynamic_cast<const Component*>(&node);
            return !component || visitor(*component);
        };
        Actor::walkComponents(*actor,query);
    }
}
void Component::destroy() {
    if(!scene()) throw std::logic_error("Component destruction requires an owning scene");
    scene()->queueRemoval(*this);
}
bool Component::hasTag(std::string_view tag) const {
    return std::find(tags_.begin(),tags_.end(),tag)!=tags_.end();
}
bool Component::addTag(std::string tag) {
    if(tag.empty()) throw std::invalid_argument("Component tags cannot be empty");
    if(hasTag(tag)) return false;
    tags_.push_back(std::move(tag));return true;
}
bool Component::removeTag(std::string_view tag) {
    const auto found=std::find(tags_.begin(),tags_.end(),tag);
    if(found==tags_.end()) return false;
    tags_.erase(found);return true;
}
void Component::validateSnapshot(const ComponentSnapshot& snapshot) const {
    if(snapshot.schemaVersion!=1) throw std::invalid_argument("Unsupported component snapshot version");
    if(snapshot.type!=typeName()) throw std::invalid_argument("Component snapshot type mismatch");
    Node validator;validator.setLocalTransform(snapshot.transform);
    for(std::size_t i=0;i<snapshot.tags.size();++i) {
        if(snapshot.tags[i].empty()) throw std::invalid_argument("Component tags cannot be empty");
        if(std::find(snapshot.tags.begin(),snapshot.tags.begin()+i,snapshot.tags[i])!=snapshot.tags.begin()+i)
            throw std::invalid_argument("Duplicate component tag");
    }
}
ComponentSnapshot Component::serialize() const {
    ComponentSnapshot snapshot{1,std::string(typeName()),name,transform,enabled,tickEnabled_,fixedTickEnabled_,tags_};
    validateSnapshot(snapshot);return snapshot;
}
void Component::deserialize(const ComponentSnapshot& snapshot) {
    validateSnapshot(snapshot);
    auto next=snapshot; // Finish allocations before committing any settings.
    name.swap(next.name);tags_.swap(next.tags);transform=next.transform;
    enabled=next.enabled;tickEnabled_=next.tickEnabled;fixedTickEnabled_=next.fixedTickEnabled;
}
void Component::validate() const { (void)serialize(); }
} // namespace engine
