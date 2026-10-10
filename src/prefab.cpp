#include "engine/prefab.hpp"
#include <set>

namespace engine {
void ComponentFactoryRegistry::registerFactory(std::string type,std::uint32_t version,Factory factory) {
    if(type.empty() || !version || !factory) throw std::invalid_argument("Invalid prefab component factory");
    if(!factories_.emplace(std::make_pair(std::move(type),version),std::move(factory)).second)
        throw std::invalid_argument("Prefab component factory already registered");
}
bool ComponentFactoryRegistry::unregisterFactory(std::string_view type,std::uint32_t version) {
    return factories_.erase({std::string(type),version})!=0;
}
bool ComponentFactoryRegistry::contains(std::string_view type,std::uint32_t version) const {
    return factories_.contains({std::string(type),version});
}
ComponentFactoryRegistry::Factory ComponentFactoryRegistry::resolve(const PrefabComponent& definition) const {
    const auto found=factories_.find({definition.type,definition.version});
    if(found==factories_.end()) throw std::invalid_argument("Unknown prefab component type/version: "+definition.type);
    return found->second;
}
Actor* PrefabInstance::actor(Scene& scene,std::string_view key) const {
    const auto found=actors.find(key);return found==actors.end() ? nullptr : scene.findActor(found->second);
}
Component* PrefabInstance::component(Scene& scene,std::string_view key) const {
    const auto found=components.find(key);return found==components.end() ? nullptr : scene.findComponent(found->second);
}
namespace {
void validateSettings(const PrefabNode& node) {
    Node validator;validator.setLocalTransform(node.transform);
    if(node.kind!=PrefabNodeKind::Actor && node.kind!=PrefabNodeKind::Node && node.kind!=PrefabNodeKind::Component)
        throw std::invalid_argument("Invalid prefab node kind");
    if((node.kind==PrefabNodeKind::Component)!=node.component.has_value())
        throw std::invalid_argument("Only prefab components require a factory definition");
    if(node.component && (node.component->type.empty() || !node.component->version))
        throw std::invalid_argument("Invalid prefab component type/version");
    if(node.kind==PrefabNodeKind::Node && (node.tickEnabled || node.fixedTickEnabled || !node.tags.empty()))
        throw std::invalid_argument("Plain prefab nodes cannot have ticks or tags");
    if(node.kind==PrefabNodeKind::Actor && node.fixedTickEnabled)
        throw std::invalid_argument("Actor tickEnabled controls both tick phases");
    std::set<std::string_view> tags;
    for(const auto& tag:node.tags)
        if(tag.empty() || !tags.insert(tag).second) throw std::invalid_argument("Invalid or duplicate prefab tag");
}
void applyOverride(PrefabNode& node,const PrefabOverride& value) {
    if(value.name) node.name=*value.name;
    if(value.transform) node.transform=*value.transform;
    if(value.enabled) node.enabled=*value.enabled;
    if(value.tickEnabled) node.tickEnabled=*value.tickEnabled;
    if(value.fixedTickEnabled) node.fixedTickEnabled=*value.fixedTickEnabled;
    if(value.tags) node.tags=*value.tags;
    if(value.component) node.component=*value.component;
}
// Iterative preflight bounds recursion before building/copying a hierarchy.
using Index=std::map<std::string,const PrefabNode*,std::less<>>;
Index indexDefinition(const PrefabNode& root,PrefabLimits limits) {
    if(!limits.maxNodes || !limits.maxDepth) throw std::invalid_argument("Prefab limits must be positive");
    if(root.kind!=PrefabNodeKind::Actor) throw std::invalid_argument("Prefab root must be an actor");
    Index index;
    std::vector<std::pair<const PrefabNode*,std::size_t>> pending{{&root,1}};
    while(!pending.empty()) {
        const auto [node,depth]=pending.back();pending.pop_back();
        if(depth>limits.maxDepth || index.size()>=limits.maxNodes) throw std::length_error("Prefab hierarchy exceeds limits");
        if(node->key.empty() || !index.emplace(node->key,node).second) throw std::invalid_argument("Prefab node keys must be unique and nonempty");
        validateSettings(*node);
        for(const auto& child:node->children) pending.emplace_back(&child,depth+1);
    }
    return index;
}
void configure(Node& node,const PrefabNode& definition) {
    node.name=definition.name;node.setLocalTransform(definition.transform);node.enabled=definition.enabled;
    if(auto* actor=dynamic_cast<Actor*>(&node)) {
        if(definition.tickEnabled) actor->setTickEnabled(*definition.tickEnabled);
        for(const auto& tag:definition.tags) actor->addTag(tag);
    }
    if(auto* component=dynamic_cast<Component*>(&node)) {
        // Common definition settings are authoritative; specialized settings
        // and default tick flags come from the factory.
        auto snapshot=component->Component::serialize();
        snapshot.name=definition.name;snapshot.transform=definition.transform;snapshot.enabled=definition.enabled;
        snapshot.tags=definition.tags;
        if(definition.tickEnabled) snapshot.tickEnabled=*definition.tickEnabled;
        if(definition.fixedTickEnabled) snapshot.fixedTickEnabled=*definition.fixedTickEnabled;
        component->Component::deserialize(snapshot);
    }
}
void validateBuiltTree(const Node& root,PrefabLimits limits) {
    std::vector<std::pair<const Node*,std::size_t>> pending{{&root,1}};
    std::size_t count=0;
    while(!pending.empty()) {
        const auto [node,depth]=pending.back();pending.pop_back();
        if(++count>limits.maxNodes || depth>limits.maxDepth) throw std::length_error("Prefab factory hierarchy exceeds limits");
        if(node->scene() || node->isPendingRemoval()) throw std::invalid_argument("Prefab factories must return detached live hierarchies");
        Node validator;validator.setLocalTransform(node->transform);
        if(auto* component=dynamic_cast<const Component*>(node)) component->validate();
        for(const auto& child:node->children()) pending.emplace_back(child.get(),depth+1);
    }
}
}
Prefab::Prefab(PrefabNode root,PrefabLimits limits):root_(std::move(root)),limits_(limits) {
    (void)indexDefinition(root_,limits_);
}
PrefabInstance Prefab::instantiate(Node& parent,const ComponentFactoryRegistry& registry,std::span<const PrefabOverride> overrides) const {
    if(!parent.scene() || parent.isPendingRemoval()) throw std::invalid_argument("Prefab destination must belong to a live scene");
    // A captured callback may request destruction, but cannot flush/detach the
    // destination out from under construction. Publication rechecks pending state.
    Node::TraversalGuard guard(parent);
    const auto index=indexDefinition(root_,limits_);
    std::map<std::string,PrefabNode,std::less<>> changed;
    for(const auto& value:overrides) {
        const auto found=index.find(value.key);
        if(found==index.end()) throw std::invalid_argument("Unknown prefab override key: "+value.key);
        if(changed.contains(value.key)) throw std::invalid_argument("Duplicate prefab override key: "+value.key);
        // Copy settings only; children remain in the immutable definition.
        const auto& source=*found->second;
        PrefabNode settings{source.key,source.kind,source.name,source.transform,source.enabled,
            source.tickEnabled,source.fixedTickEnabled,source.tags,source.component,{}};
        applyOverride(settings,value);validateSettings(settings);
        changed.emplace(value.key,std::move(settings));
    }
    auto effective=[&](const PrefabNode& node)->const PrefabNode& {
        const auto found=changed.find(node.key);return found==changed.end() ? node : found->second;
    };
    // Copy all factories before executing any of them: registry edits from a
    // callback affect subsequent instantiations, never this one.
    std::map<std::string,ComponentFactoryRegistry::Factory,std::less<>> factories;
    PrefabInstance result;
    for(const auto& [key,node]:index) {
        const auto& settings=effective(*node);
        if(settings.kind==PrefabNodeKind::Actor) result.actors.emplace(key,ActorId{});
        if(settings.component) {
            factories.emplace(key,registry.resolve(*settings.component));
            result.components.emplace(key,ComponentId{});
        }
    }
    std::map<std::string,Node*,std::less<>> instances;
    auto build=[&](auto&& self,const PrefabNode& definition)->std::unique_ptr<Node> {
        const auto& settings=effective(definition);
        std::unique_ptr<Node> node;
        if(settings.kind==PrefabNodeKind::Actor) node=std::make_unique<Actor>();
        else if(settings.kind==PrefabNodeKind::Node) node=std::make_unique<Node>();
        else node=factories.at(settings.key)(settings.component->configuration);
        if(!node || node->parent() || node->scene() || node->isPendingRemoval())
            throw std::invalid_argument("Prefab factory returned a null or attached component");
        configure(*node,settings);
        instances.emplace(settings.key,node.get());
        for(const auto& child:definition.children) node->adoptChild(self(self,child));
        return node;
    };
    auto tree=build(build,root_);validateBuiltTree(*tree,limits_);
    auto& root=static_cast<Actor&>(*tree);
    parent.adoptChild(std::move(tree)); // The only publication point; allocations precede registration.
    result.root=root.id();
    for(auto& [key,id]:result.actors) id=static_cast<Actor*>(instances.at(key))->id();
    for(auto& [key,id]:result.components) id=static_cast<Component*>(instances.at(key))->id();
    return result;
}
} // namespace engine
