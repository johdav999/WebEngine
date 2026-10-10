#pragma once
#include "engine/node.hpp"
#include <limits>
#include <string_view>

namespace engine {
class Actor;
// Runtime handles are scoped to one scene and invalidated when a slot is recycled.
struct ComponentId {
    std::uint32_t index=std::numeric_limits<std::uint32_t>::max(),generation=0;
    std::uint64_t scene=0;
    explicit operator bool() const { return scene!=0 && generation!=0; }
    bool operator==(const ComponentId&) const = default;
};
// Common settings only: assets, child hierarchy and runtime handles are not saved.
struct ComponentSnapshot {
    std::uint32_t schemaVersion=1;
    std::string type="Component",name="Node";
    Transform transform;
    bool enabled=true,tickEnabled=false,fixedTickEnabled=false;
    std::vector<std::string> tags;
};
class Component : public Node {
public:
    explicit Component(std::string name = "Node");
    ~Component() override;
    Actor* owner();
    const Actor* owner() const; // Nearest actor ancestor, or null for scene components.
    ComponentId id() const { return id_; }
    bool isRegistered() const { return static_cast<bool>(id_); }
    bool isInitialized() const { return initialized_; }
    bool tickEnabled() const { return tickEnabled_; }
    bool fixedTickEnabled() const { return fixedTickEnabled_; }
    void setTickEnabled(bool value) { tickEnabled_=value; }
    void setFixedTickEnabled(bool value) { fixedTickEnabled_=value; }
    void destroy(); // Requires a scene; uses deferred removal, including descendants.
    template<class T> const T* getComponent() const {
        static_assert(std::is_base_of_v<Component,T>);
        const T* found=nullptr;
        visitOwnerComponents([&](const Component& candidate) {
            found=dynamic_cast<const T*>(&candidate);return found==nullptr;
        });
        return found;
    }
    template<class T> T* getComponent() {
        return const_cast<T*>(std::as_const(*this).getComponent<T>());
    }
    template<class T> std::vector<const T*> getComponents() const {
        static_assert(std::is_base_of_v<Component,T>);
        std::vector<const T*> result;
        visitOwnerComponents([&](const Component& candidate) {
            if(auto* value=dynamic_cast<const T*>(&candidate)) result.push_back(value);
            return true;
        });
        return result;
    }
    template<class T> std::vector<T*> getComponents() {
        std::vector<T*> result;
        for(const auto* value:std::as_const(*this).getComponents<T>()) result.push_back(const_cast<T*>(value));
        return result;
    }
    template<class T> bool hasComponent() const { return getComponent<T>()!=nullptr; }
    bool addTag(std::string tag);
    bool removeTag(std::string_view tag);
    bool hasTag(std::string_view tag) const;
    const auto& tags() const { return tags_; }
    virtual std::string_view typeName() const noexcept { return "Component"; }
    virtual ComponentSnapshot serialize() const;
    virtual void deserialize(const ComponentSnapshot& snapshot);
    virtual void validate() const; // Throws invalid_argument for invalid common settings.
protected:
    // Attach/enable are reconciled by Scene update/fixedUpdate. Cleanup is immediate
    // on detachment; hooks must not throw or erase/reparent the hierarchy.
    virtual void onAttach() noexcept {}
    virtual void onDetach() noexcept {}
    virtual void onEnable() noexcept {}
    virtual void onDisable() noexcept {}
    virtual void onOwnerChanged(Actor* previous,Actor* current) noexcept { (void)previous;(void)current; }
    virtual void tick(double deltaSeconds) { (void)deltaSeconds; }
    virtual void fixedTick(double stepSeconds) { (void)stepSeconds; }
private:
    friend class Scene;
    friend class Node;
    void refreshOwner() noexcept;
    void visitOwnerComponents(const std::function<bool(const Component&)>& visitor) const;
    void validateSnapshot(const ComponentSnapshot& snapshot) const;
    ComponentId id_;
    Actor* notifiedOwner_=nullptr;
    bool initialized_=false,effectiveEnabled_=false,tickEnabled_=false,fixedTickEnabled_=false;
    std::vector<std::string> tags_;
};
} // namespace engine
