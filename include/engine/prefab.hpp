#pragma once
#include "engine/scene.hpp"
#include <any>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>

namespace engine {
// Configuration is copied by value. Use existing shared_ptr<const Asset> types
// inside it for shared resources; never put live components or handles in it.
struct PrefabComponent {
    std::string type;
    std::uint32_t version=1;
    std::any configuration;
};
class ComponentFactoryRegistry {
public:
    using Factory=std::function<std::unique_ptr<Component>(const std::any&)>;
    void registerFactory(std::string type,std::uint32_t version,Factory factory);
    template<class Configuration,class F>
    void registerFactory(std::string type,std::uint32_t version,F factory) {
        registerFactory(std::move(type),version,Factory{
            [factory=std::move(factory)](const std::any& configuration) {
                const auto* value=std::any_cast<Configuration>(&configuration);
                if(!value) throw std::invalid_argument("Prefab factory configuration type mismatch");
                return factory(*value);
            }});
    }
    bool unregisterFactory(std::string_view type,std::uint32_t version=1);
    bool contains(std::string_view type,std::uint32_t version=1) const;
private:
    friend class Prefab;
    Factory resolve(const PrefabComponent& definition) const;
    std::map<std::pair<std::string,std::uint32_t>,Factory> factories_;
};
enum class PrefabNodeKind { Actor, Node, Component };
struct PrefabNode {
    std::string key; // Unique within the definition, independent of display names.
    PrefabNodeKind kind=PrefabNodeKind::Actor;
    std::string name="Actor";
    Transform transform;
    bool enabled=true;
    // Disengaged flags preserve a factory's defaults (e.g. emitter ticking).
    std::optional<bool> tickEnabled,fixedTickEnabled;
    std::vector<std::string> tags;
    std::optional<PrefabComponent> component;
    std::vector<PrefabNode> children;
};
struct PrefabOverride {
    std::string key;
    std::optional<std::string> name;
    std::optional<Transform> transform;
    std::optional<bool> enabled,tickEnabled,fixedTickEnabled;
    std::optional<std::vector<std::string>> tags; // Replace the complete tag list.
    std::optional<PrefabComponent> component; // Replace type/version/configuration.
};
struct PrefabLimits { std::size_t maxNodes=100000,maxDepth=128; };
// Non-owning, scene-scoped handles. These do not keep the scene/objects alive.
struct PrefabInstance {
    ActorId root;
    std::map<std::string,ActorId,std::less<>> actors;
    std::map<std::string,ComponentId,std::less<>> components;
    Actor* actor(Scene& scene,std::string_view key) const;
    Component* component(Scene& scene,std::string_view key) const;
};
class Prefab {
public:
    explicit Prefab(PrefabNode root,PrefabLimits limits={});
    const PrefabNode& definition() const { return root_; }
    // Factories/configurations/overrides are borrowed only for this call. All
    // validation and allocation finish before one subtree is attached. Existing
    // lifecycle rules apply; this does not tick, initialize physics or spawn hooks.
    PrefabInstance instantiate(Node& parent,const ComponentFactoryRegistry& registry,
        std::span<const PrefabOverride> overrides={}) const;
private:
    PrefabNode root_;
    PrefabLimits limits_;
};
} // namespace engine
