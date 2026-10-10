#include "engine/node.hpp"
#include "engine/scene.hpp"
#include <cstring>
#include <atomic>
#include <random>
#include <cstdio>

namespace engine {
namespace {
std::string newSavedId() {
    static const std::uint64_t prefix=[] {
        std::random_device random;
        return (std::uint64_t(random())<<32)^random();
    }();
    static std::atomic<std::uint64_t> sequence{1};
    char text[34];
    std::snprintf(text,sizeof(text),"%016llx-%016llx",
        static_cast<unsigned long long>(prefix),static_cast<unsigned long long>(sequence.fetch_add(1)));
    return text;
}
}
Node::Node(std::string name) : name(std::move(name)),savedId_(newSavedId()) {}
Node::~Node() = default;
bool Node::validSavedId(std::string_view identity) {
    return !identity.empty()&&identity.size()<=128&&std::all_of(identity.begin(),identity.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c==':';
    });
}
void Node::setSavedId(std::string identity) {
    requireIdle();
    if(!validSavedId(identity)) throw std::invalid_argument("Invalid saved node identity");
    bool duplicate=false;
    auto walk=[&](auto&& self,const Node& node)->void {
        if(&node!=this&&node.savedId()==identity) duplicate=true;
        for(const auto& child:node.children()) self(self,*child);
    };
    walk(walk,root());
    if(duplicate) throw std::invalid_argument("Duplicate saved node identity");
    savedId_=std::move(identity);
}
const Node* Node::findSavedNode(std::string_view identity) const {
    const Node* found=nullptr;bool duplicate=false;
    // Include disabled nodes, but never pending removals. No competing runtime registry.
    auto walk=[&](auto&& self,const Node& node)->void {
        if(node.isPendingRemoval()) return;
        if(node.savedId()==identity) { if(found) duplicate=true;found=&node; }
        for(const auto& child:node.children()) self(self,*child);
    };
    walk(walk,*this);return duplicate ? nullptr : found;
}
Node& Node::root() { return parent_ ? parent_->root() : *this; }
Node::TraversalGuard::TraversalGuard(Node& node):root(node.root()) { ++root.traversalDepth_; }
Node::TraversalGuard::~TraversalGuard() { --root.traversalDepth_; }
void Node::requireIdle() const {
    if(const_cast<Node*>(this)->root().traversalDepth_)
        throw std::logic_error("Detach/reparent/flush cannot run during hierarchy traversal");
}
void Node::notifyOwnerChanges() {
    TraversalGuard guard(*this);
    auto notify=[&](auto&& self,Node& node)->void {
        const auto count=node.children_.size();
        if(auto* component=dynamic_cast<Component*>(&node)) component->refreshOwner();
        for(std::size_t i=0;i<count;++i) self(self,*node.children_[i]);
    };
    notify(notify,*this);
}
bool Node::isPendingRemoval() const {
    for(auto* node=this;node;node=node->parent_) if(node->pendingRemoval_) return true;
    return false;
}
bool Node::isActiveInHierarchy() const {
    for(auto* node=this;node;node=node->parent_)
        if(!node->enabled || node->pendingRemoval_) return false;
    return true;
}
bool Node::subtreePending() const {
    if(isPendingRemoval()) return true;
    for(const auto& child:children_) if(child->subtreePending()) return true;
    return false;
}
namespace {
Transform decomposeTransform(const Mat4& matrix) {
    for(float value:matrix.m) if(!std::isfinite(value)) throw std::invalid_argument("Transform must be finite");
    if(std::abs(matrix.m[3])+std::abs(matrix.m[7])+std::abs(matrix.m[11])+std::abs(matrix.m[15]-1)>0.0001f)
        throw std::invalid_argument("Transform must be affine");
    Vec3 x{matrix.m[0],matrix.m[1],matrix.m[2]},y{matrix.m[4],matrix.m[5],matrix.m[6]},z{matrix.m[8],matrix.m[9],matrix.m[10]};
    Transform value;value.position={matrix.m[12],matrix.m[13],matrix.m[14]};
    value.scale={std::sqrt(dot(x,x)),std::sqrt(dot(y,y)),std::sqrt(dot(z,z))};
    inverseAffine(matrix); // Reject singular input before dividing by scale.
    if(dot(x,cross(y,z))<0) value.scale.x=-value.scale.x;
    x=x*(1/value.scale.x);y=y*(1/value.scale.y);z=z*(1/value.scale.z);
    if(std::abs(dot(x,y))+std::abs(dot(y,z))+std::abs(dot(z,x))>0.0001f)
        throw std::invalid_argument("Sheared transforms cannot be represented as local TRS");
    value.rotation.y=std::asin(std::clamp(-x.z,-1.0f,1.0f));
    if(std::abs(std::cos(value.rotation.y))>0.00001f) {
        value.rotation.x=std::atan2(y.z,z.z);value.rotation.z=std::atan2(x.y,x.x);
    } else {
        value.rotation.x=0;value.rotation.z=std::atan2(-y.x,y.y);
    }
    const auto rebuilt=value.matrix();
    for(std::size_t i=0;i<16;++i)
        if(std::abs(rebuilt.m[i]-matrix.m[i])>0.0002f*std::max(1.0f,std::abs(matrix.m[i])))
            throw std::invalid_argument("Transform cannot be represented as local TRS");
    return value;
}
}
void Node::setLocalTransform(const Transform& value) {
    decomposeTransform(value.matrix());transform=value;
}
void Node::setWorldTransform(const Mat4& value) {
    const auto local=parent_ ? inverseAffine(parent_->worldMatrix())*value : value;
    transform=decomposeTransform(local);
}
Node& Node::adoptChild(std::unique_ptr<Node> child) {
    for(auto* node=this;node;node=node->parent_)
        if(node->detaching_ || node==child.get()) throw std::logic_error("Cannot attach a cycle or add to a detaching subtree");
    if(!child || child->parent_ || child->scene_ || child->subtreePending() || isPendingRemoval()
        || dynamic_cast<Scene*>(child.get()) || (scene_ && scene_->shuttingDown_))
        throw std::invalid_argument("Cannot attach this child");
    Node& ref=*child;
    children_.push_back(std::move(child));ref.parent_=this;
    try { if(scene_) scene_->attachSubtree(ref); }
    catch(...) { ref.parent_=nullptr;children_.pop_back();throw; }
    ref.notifyOwnerChanges();
    return ref;
}
void Node::protectChild(Node& descendant) {
    for(auto* node=descendant.parent_;node;node=node->parent_)
        if(node==this) { descendant.protectedChild_=true;return; }
    throw std::invalid_argument("Protected child must belong to this subtree");
}
std::unique_ptr<Node> Node::detachChild(Node& child) {
    requireIdle();
    if(child.parent_!=this || child.protectedChild_ || child.subtreePending())
        throw std::invalid_argument("Cannot detach this child");
    TraversalGuard guard(*this);
    if(scene_) scene_->detachSubtree(child);
    // Cleanup hooks may append siblings, so find the child after those hooks.
    const auto found=std::find_if(children_.begin(),children_.end(),[&](const auto& p) { return p.get()==&child; });
    auto result=std::move(*found);children_.erase(found);result->parent_=nullptr;
    result->notifyOwnerChanges();return result;
}
bool Node::removeChild(Node& child) {
    if(child.parent_!=this) return false;
    if(child.protectedChild_) throw std::logic_error("Required component children cannot be removed independently");
    if(child.isPendingRemoval()) return false;
    if(scene_) scene_->queueRemoval(child);
    else { auto removed=detachChild(child); }
    return true;
}
void Node::reparent(Node& newParent,ReparentMode mode) {
    requireIdle();newParent.requireIdle();
    if(!parent_ || protectedChild_ || subtreePending() || newParent.isPendingRemoval())
        throw std::invalid_argument("Cannot reparent this node");
    for(auto* node=&newParent;node;node=node->parent_)
        if(node==this) throw std::invalid_argument("Hierarchy cycles are not allowed");
    if(parent_==&newParent) return;
    if(mode!=ReparentMode::KeepLocal && mode!=ReparentMode::KeepWorld) throw std::invalid_argument("Invalid reparent mode");
    auto next=transform;
    if(mode==ReparentMode::KeepWorld) next=decomposeTransform(inverseAffine(newParent.worldMatrix())*worldMatrix());
    newParent.children_.reserve(newParent.children_.size()+1);
    if(scene_!=newParent.scene_ && newParent.scene_) {
        // Reserve all handle storage before ownership or callbacks change.
        newParent.scene_->reserveSubtreeHandles(*this);
    }
    TraversalGuard oldGuard(*this),newGuard(newParent);
    auto* previous=parent_;
    const bool changeScene=scene_!=newParent.scene_;
    if(changeScene && scene_) scene_->detachSubtree(*this);
    const auto found=std::find_if(previous->children_.begin(),previous->children_.end(),[&](const auto& p) { return p.get()==this; });
    auto owned=std::move(*found);previous->children_.erase(found);
    parent_=&newParent;transform=next;newParent.children_.push_back(std::move(owned));
    if(changeScene && newParent.scene_) newParent.scene_->attachSubtree(*this);
    notifyOwnerChanges();
}
Mat4 Node::worldMatrix() const {
    return evaluateWorld(parent_ ? parent_->worldMatrix() : Mat4::identity());
}
const Mat4& Node::evaluateWorld(const Mat4& parentWorld) const {
    static_assert(sizeof(Transform)==9*sizeof(float)); // No padding in the public-field snapshot.
    if(!localValid_ || std::memcmp(&cachedTransform_,&transform,sizeof(Transform))) {
        localCache_=transform.matrix();cachedTransform_=transform;localValid_=true;++cacheStats_.localBuilds;
    }
    if(!worldValid_ || worldLocalVersion_!=cacheStats_.localBuilds || std::memcmp(parentCache_.m.data(),parentWorld.m.data(),16*sizeof(float))) {
        worldCache_=parentWorld*localCache_;parentCache_=parentWorld;
        worldLocalVersion_=cacheStats_.localBuilds;worldValid_=true;++cacheStats_.worldBuilds;
    }
    return worldCache_;
}
const Mat4& Node::normalMatrix(const Mat4& world) const {
    if(!normalValid_ || std::memcmp(normalInput_.m.data(),world.m.data(),16*sizeof(float))) {
        const auto normal=transpose(inverseAffine(world)); // Commit only after successful inversion.
        normalInput_=world;normalCache_=normal;normalValid_=true;++cacheStats_.normalBuilds;
    }
    return normalCache_;
}
void Node::visit(const std::function<void(const Node&,const Mat4&)>& visitor, const Mat4& parentWorld,TransformCacheStats* stats) const {
    TraversalGuard guard(*const_cast<Node*>(this));
    if(!isActiveInHierarchy()) return;
    visitImpl(visitor,parentWorld,stats);
}
void Node::visitImpl(const std::function<void(const Node&,const Mat4&)>& visitor,const Mat4& parentWorld,TransformCacheStats* stats) const {
    if(!isActiveInHierarchy()) return;
    const auto childCount=children_.size();
    const auto before=cacheStats_;
    const Mat4 world=evaluateWorld(parentWorld);
    visitor(*this,world);
    if(stats) {
        stats->localBuilds+=cacheStats_.localBuilds-before.localBuilds;
        stats->worldBuilds+=cacheStats_.worldBuilds-before.worldBuilds;
        stats->normalBuilds+=cacheStats_.normalBuilds-before.normalBuilds;
    }
    if(isPendingRemoval() || !enabled) return;
    for(std::size_t i=0;i<childCount;++i) children_[i]->visitImpl(visitor,world,stats);
}
} // namespace engine
