#pragma once
#include "engine/math.hpp"
#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine {
class Scene;
class Actor;
struct TerrainEdit;
class Actor;
struct TerrainEdit;
struct TransformCacheStats { std::uint64_t localBuilds=0,worldBuilds=0,normalBuilds=0; };
enum class ReparentMode { KeepLocal, KeepWorld };
class Node {
public:
    explicit Node(std::string name = "Node");
    virtual ~Node();
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    std::string name;
    Transform transform;
    bool enabled = true; // Disabling a node disables its subtree during scene collection.
    template<class T, class... Args> T& addChild(Args&&... args) {
        static_assert(std::is_base_of_v<Node,T>);
        auto child=std::make_unique<T>(std::forward<Args>(args)...);
        T& ref=*child;
        adoptChild(std::move(child));
        return ref;
    }
    Node& adoptChild(std::unique_ptr<Node> child);
    // Detach/reparent require an idle hierarchy. Removal in a scene is deferred.
    std::unique_ptr<Node> detachChild(Node& child);
    bool removeChild(Node& child);
    void setChildIndex(Node& child,std::size_t index); // Idle sibling ordering; no lifecycle change.
    void reparent(Node& newParent,ReparentMode mode=ReparentMode::KeepLocal);
    const auto& children() const { return children_; }
    Node* parent() { return parent_; }
    const Node* parent() const { return parent_; }
    Scene* scene() { return scene_; }
    const Scene* scene() const { return scene_; }
    bool isActiveInHierarchy() const;
    bool isPendingRemoval() const;
    bool isProtectedChild() const { return protectedChild_; } // Read-only authoring metadata.
    // Persistent identity, independent of names and scene-scoped runtime handles.
    const std::string& savedId() const { return savedId_; }
    void setSavedId(std::string identity); // Idle hierarchy; rejects duplicates in this root.
    static bool validSavedId(std::string_view identity);
    const Node* findSavedNode(std::string_view identity) const;
    Node* findSavedNode(std::string_view identity) {
        return const_cast<Node*>(std::as_const(*this).findSavedNode(identity));
    }
    void setEnabled(bool value) { enabled=value; }
    void setLocalTransform(const Transform& value);
    void setWorldTransform(const Mat4& value);
    void setWorldTransform(const Transform& value) { setWorldTransform(value.matrix()); }
    Mat4 worldMatrix() const;
    // Cached inverse transpose for the exact traversal world matrix supplied.
    const Mat4& normalMatrix(const Mat4& world) const;
    TransformCacheStats transformCacheStats() const { return cacheStats_; }
    void visit(const std::function<void(const Node&,const Mat4&)>& visitor, const Mat4& parentWorld=Mat4::identity(),TransformCacheStats* stats=nullptr) const;
protected:
    // Required implementation children cannot be removed or reparented independently.
    void protectChild(Node& descendant);
private:
    friend class ArchiveCodec;
    friend class Scene;
    friend class SelectionSystem;
    friend class Prefab;
    friend class RouteComponent;
    friend class TerrainStream;
    friend class TerrainPagedStream;
    friend void applyTerrainEdit(Actor&,const TerrainEdit&);
    std::string savedId_;
    struct TraversalGuard {
        explicit TraversalGuard(Node& node);
        ~TraversalGuard();
        Node& root;
    };
    Node& root();
    void requireIdle() const;
    void notifyOwnerChanges();
    bool subtreePending() const;
    void visitImpl(const std::function<void(const Node&,const Mat4&)>& visitor,const Mat4& parentWorld,TransformCacheStats* stats) const;
    const Mat4& evaluateWorld(const Mat4& parentWorld) const;
    mutable Transform cachedTransform_;
    mutable Mat4 localCache_,parentCache_,worldCache_,normalInput_,normalCache_;
    mutable TransformCacheStats cacheStats_;
    mutable std::uint64_t worldLocalVersion_=0;
    mutable bool localValid_=false,worldValid_=false,normalValid_=false;
    Node* parent_=nullptr;
    Scene* scene_=nullptr;
    bool pendingRemoval_=false,protectedChild_=false,detaching_=false;
    mutable std::size_t traversalDepth_=0;
    std::vector<std::unique_ptr<Node>> children_;
};
} // namespace engine
