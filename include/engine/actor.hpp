#pragma once
#include "engine/component.hpp"
#include <limits>
#include <string_view>

namespace engine {
// Handles are scoped to a scene; recycled slots have a new generation.
struct ActorId {
    std::uint32_t index=std::numeric_limits<std::uint32_t>::max(),generation=0;
    std::uint64_t scene=0;
    explicit operator bool() const { return scene!=0 && generation!=0; }
    bool operator==(const ActorId&) const = default;
};
enum class ActorState { Created, Active, PendingDestroy };
class Actor : public Node {
public:
    explicit Actor(std::string name = "Actor");
    ~Actor() override;
    ActorId id() const { return id_; }
    ActorState state() const { return state_; }
    bool isPendingDestroy() const { return isPendingRemoval(); }
    bool tickEnabled() const { return tickEnabled_; }
    void setTickEnabled(bool value) { tickEnabled_=value; }
    void destroy();
    template<class T,class... Args> T& addComponent(Args&&... args) {
        static_assert(std::is_base_of_v<Component,T>);
        return addChild<T>(std::forward<Args>(args)...);
    }
    // Queries include disabled components and nested components, exclude child actors
    // and subtrees queued for removal. Returned pointers are non-owning.
    template<class T> T* getComponent() {
        return const_cast<T*>(std::as_const(*this).getComponent<T>());
    }
    template<class T> const T* getComponent() const {
        static_assert(std::is_base_of_v<Component,T>);
        const T* found=nullptr;
        auto query=[&](const Node& node) { found=dynamic_cast<const T*>(&node);return found==nullptr; };
        walkComponents(*this,query);return found;
    }
    template<class T> std::vector<T*> getComponents() {
        static_assert(std::is_base_of_v<Component,T>);
        std::vector<T*> result;
        auto query=[&](const Node& node) { if(auto* value=dynamic_cast<const T*>(&node)) result.push_back(const_cast<T*>(value));return true; };
        walkComponents(*this,query);return result;
    }
    template<class T> std::vector<const T*> getComponents() const {
        static_assert(std::is_base_of_v<Component,T>);
        std::vector<const T*> result;
        auto query=[&](const Node& node) { if(auto* value=dynamic_cast<const T*>(&node)) result.push_back(value);return true; };
        walkComponents(*this,query);return result;
    }
    template<class T> bool hasComponent() const { return getComponent<T>()!=nullptr; }
    bool removeComponent(Component& component);
    bool addTag(std::string tag);
    bool removeTag(std::string_view tag);
    bool hasTag(std::string_view tag) const;
    const auto& tags() const { return tags_; }
protected:
    // Hooks run through Scene::update/fixedUpdate; cleanup hooks must not throw.
    virtual void onSpawn() noexcept {}
    virtual void onDespawn() noexcept {}
    virtual void onEnable() noexcept {}
    virtual void onDisable() noexcept {}
    virtual void tick(double deltaSeconds) { (void)deltaSeconds; }
    virtual void fixedTick(double stepSeconds) { (void)stepSeconds; }
private:
    friend class Scene;
    friend class Component;
    template<class F> static bool walkComponents(const Node& root,F& query) {
        if(root.isPendingRemoval()) return true;
        for(const auto& child:root.children()) {
            if(dynamic_cast<const Actor*>(child.get()) || child->isPendingRemoval()) continue;
            if(!query(*child) || !walkComponents(*child,query)) return false;
        }
        return true;
    }
    ActorId id_;
    ActorState state_=ActorState::Created;
    bool tickEnabled_=false,spawned_=false,effectiveEnabled_=false;
    std::vector<std::string> tags_;
};
} // namespace engine
