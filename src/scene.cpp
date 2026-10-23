#include "engine/performance_timeline.hpp"
#include "engine/scene.hpp"
#include "engine/decal.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/physics.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/camera_component.hpp"
#include "engine/selection.hpp"
#include <limits>
#include <atomic>

namespace engine {
namespace { std::atomic<std::uint64_t> nextSceneIdentity{1}; }
Scene::Scene(std::string name) : Node(std::move(name)),identity_(nextSceneIdentity.fetch_add(1)) {
    if(!identity_) throw std::overflow_error("Scene identities exhausted");
    scene_=this;
}
Scene::~Scene() {
    shuttingDown_=true;TraversalGuard guard(*this);detachSubtree(*this);
}
void Scene::reserveSubtreeHandles(const Node& node) {
    std::size_t actorCount=0,componentCount=0;
    auto countNodes=[&](auto&& self,const Node& current)->void {
        actorCount+=dynamic_cast<const Actor*>(&current)!=nullptr;
        componentCount+=dynamic_cast<const Component*>(&current)!=nullptr;
        for(const auto& child:current.children_) self(self,*child);
    };
    countNodes(countNodes,node);
    if(actorCount>std::numeric_limits<std::uint32_t>::max()-actors_.size()) throw std::length_error("Too many actors");
    if(componentCount>std::numeric_limits<std::uint32_t>::max()-components_.size()) throw std::length_error("Too many components");
    actors_.reserve(actors_.size()+actorCount);freeActors_.reserve(actors_.size()+actorCount);
    components_.reserve(components_.size()+componentCount);freeComponents_.reserve(components_.size()+componentCount);
}
void Scene::attachSubtree(Node& node) {
    reserveSubtreeHandles(node);
    auto attach=[&](auto&& self,Node& current)->void {
        current.scene_=this;
        if(auto* actor=dynamic_cast<Actor*>(&current)) {
            std::uint32_t index;
            if(freeActors_.empty()) { index=static_cast<std::uint32_t>(actors_.size());actors_.push_back({}); }
            else { index=freeActors_.back();freeActors_.pop_back(); }
            actors_[index].actor=actor;actor->id_={index,actors_[index].generation,identity_};
            actor->state_=ActorState::Created;
        }
        if(auto* component=dynamic_cast<Component*>(&current)) {
            std::uint32_t index;
            if(freeComponents_.empty()) { index=static_cast<std::uint32_t>(components_.size());components_.push_back({}); }
            else { index=freeComponents_.back();freeComponents_.pop_back(); }
            components_[index].component=component;component->id_={index,components_[index].generation,identity_};
        }
        for(const auto& child:current.children_) self(self,*child);
    };
    attach(attach,node);
}
void Scene::detachSubtree(Node& node) noexcept {
    node.detaching_=true;
    const auto count=node.children_.size();
    for(std::size_t i=0;i<count;++i) detachSubtree(*node.children_[i]);
    if(auto* component=dynamic_cast<Component*>(&node)) {
        if(component->effectiveEnabled_) { component->effectiveEnabled_=false;component->onDisable(); }
        if(component->initialized_) { component->initialized_=false;component->onDetach(); }
        const auto id=component->id_;
        if(id && id.scene==identity_) {
            auto& slot=components_[id.index];slot.component=nullptr;
            if(slot.generation!=std::numeric_limits<std::uint32_t>::max()) {
                ++slot.generation;freeComponents_.push_back(id.index);
            }
        }
        component->id_={};
    }
    if(auto* actor=dynamic_cast<Actor*>(&node)) {
        if(actor->effectiveEnabled_) { actor->effectiveEnabled_=false;actor->onDisable(); }
        if(actor->spawned_) { actor->spawned_=false;actor->onDespawn(); }
        const auto id=actor->id_;
        if(id && id.scene==identity_) {
            auto& slot=actors_[id.index];slot.actor=nullptr;
            if(slot.generation!=std::numeric_limits<std::uint32_t>::max()) {
                ++slot.generation;freeActors_.push_back(id.index);
            }
        }
        actor->id_={};
        if(actor->state_!=ActorState::PendingDestroy) actor->state_=ActorState::Created;
    }
    node.scene_=nullptr;
    node.detaching_=false;
}
const Actor* Scene::findActor(ActorId id) const {
    if(!id || id.scene!=identity_ || id.index>=actors_.size()) return nullptr;
    const auto& slot=actors_[id.index];
    return slot.generation==id.generation && slot.actor && !slot.actor->isPendingDestroy() ? slot.actor : nullptr;
}
Actor* Scene::findActor(ActorId id) { return const_cast<Actor*>(std::as_const(*this).findActor(id)); }
const Component* Scene::findComponent(ComponentId id) const {
    if(!id || id.scene!=identity_ || id.index>=components_.size()) return nullptr;
    const auto& slot=components_[id.index];
    return slot.generation==id.generation && slot.component && !slot.component->isPendingRemoval() ? slot.component : nullptr;
}
Component* Scene::findComponent(ComponentId id) { return const_cast<Component*>(std::as_const(*this).findComponent(id)); }
std::vector<const Actor*> Scene::findActorsWithTag(std::string_view tag) const {
    std::vector<const Actor*> result;
    for(const auto& slot:actors_) if(slot.actor && !slot.actor->isPendingDestroy() && slot.actor->hasTag(tag)) result.push_back(slot.actor);
    return result;
}
std::vector<Actor*> Scene::findActorsWithTag(std::string_view tag) {
    std::vector<Actor*> result;
    for(const auto& slot:actors_) if(slot.actor && !slot.actor->isPendingDestroy() && slot.actor->hasTag(tag)) result.push_back(slot.actor);
    return result;
}
void Scene::queueRemoval(Node& node) {
    if(&node==this || node.scene_!=this) throw std::invalid_argument("Cannot remove this scene node");
    if(node.protectedChild_) throw std::logic_error("Required component children cannot be removed independently");
    if(node.isPendingRemoval()) return;
    node.pendingRemoval_=true;pendingDestroy_=true;
    auto mark=[&](auto&& self,Node& current)->void {
        if(auto* actor=dynamic_cast<Actor*>(&current)) actor->state_=ActorState::PendingDestroy;
        for(const auto& child:current.children_) self(self,*child);
    };
    mark(mark,node);
}
void Scene::flushPendingDestroy() {
    requireIdle();
    TraversalGuard guard(*this);
    auto sweep=[&](auto&& self,Node& node)->void {
        for(std::size_t i=0;i<node.children_.size();) {
            auto& child=*node.children_[i];
            if(child.pendingRemoval_) {
                detachSubtree(child);
                // Cleanup callbacks can append children, but cannot erase/reparent them.
                auto removed=std::move(node.children_[i]);node.children_.erase(node.children_.begin()+i);
                removed->parent_=nullptr;
                removed->notifyOwnerChanges();
            } else { self(self,child);++i; }
        }
    };
    do {
        while(pendingDestroy_) { pendingDestroy_=false;sweep(sweep,*this); }
        if(selection_ && !shuttingDown_) selection_->refresh();
    } while(pendingDestroy_);
}
void Scene::prepareNodes(Node& node) {
    if(node.isPendingRemoval()) return;
    const auto count=node.children_.size();
    if(auto* actor=dynamic_cast<Actor*>(&node)) {
        if(!actor->spawned_) {
            actor->spawned_=true;actor->state_=ActorState::Active;actor->onSpawn();
        }
        const bool active=actor->isActiveInHierarchy();
        if(active!=actor->effectiveEnabled_) {
            actor->effectiveEnabled_=active;
            if(active) actor->onEnable();else actor->onDisable();
        }
    }
    if(auto* component=dynamic_cast<Component*>(&node)) {
        if(!component->initialized_) { component->initialized_=true;component->onAttach(); }
        const bool active=component->isActiveInHierarchy();
        if(active!=component->effectiveEnabled_) {
            component->effectiveEnabled_=active;
            if(active) component->onEnable();else component->onDisable();
        }
    }
    if(node.isPendingRemoval()) return;
    for(std::size_t i=0;i<count;++i) prepareNodes(*node.children_[i]);
}
void Scene::updateNodes(Node& node,double delta,bool fixed) {
    if(!node.isActiveInHierarchy()) return;
    const auto count=node.children_.size();
    if(auto* actor=dynamic_cast<Actor*>(&node)) {
        if(!actor->spawned_) return; // Actors created by a tick begin on the next update.
        if(actor->tickEnabled_) {
            if(fixed) actor->fixedTick(delta);else actor->tick(delta);
        }
    }
    if(!node.isActiveInHierarchy()) return;
    if(!fixed) {
        if(auto* mesh=dynamic_cast<SkeletalMeshComponent*>(&node)) mesh->synchronizeSkeleton();
    }
    if(auto* component=dynamic_cast<Component*>(&node)) {
        if(!component->initialized_) return; // Components created by hooks/ticks start next update.
        if(fixed) { if(component->fixedTickEnabled_) component->fixedTick(delta); }
        else if(component->tickEnabled_) component->tick(delta);
    }
    if(!node.isActiveInHierarchy()) return;
    if(fixed&&std::any_of(node.children_.begin(),node.children_.begin()+count,[](const auto& p){auto* c=dynamic_cast<const Component*>(p.get());return c&&c->fixedTickOrder()!=0;})){
        std::vector<Node*> ordered;ordered.reserve(count);for(std::size_t i=0;i<count;++i)ordered.push_back(node.children_[i].get());
        auto order=[](const Node* n){auto* c=dynamic_cast<const Component*>(n);return c?c->fixedTickOrder():0;};
        std::stable_sort(ordered.begin(),ordered.end(),[&](const Node* a,const Node* b){return order(a)<order(b);});
        for(auto* child:ordered){if(!node.isActiveInHierarchy())break;updateNodes(*child,delta,true);}
    }else for(std::size_t i=0;i<count;++i) updateNodes(*node.children_[i],delta,fixed);
    if(!fixed&&node.isActiveInHierarchy())if(auto* c=dynamic_cast<Component*>(&node);c&&c->initialized_&&c->tickEnabled_)c->postTick(delta);
}
void Scene::update(double delta) {
    ProfileScope profile(ProfileCategory::Simulation,"Scene variable update",name);
    if(!std::isfinite(delta) || delta<0) throw std::invalid_argument("Invalid scene delta");
    requireIdle();
    { TraversalGuard guard(*this);prepareNodes(*this);synchronizePhysics();updateNodes(*this,delta,false); }
    flushPendingDestroy();
}
void Scene::synchronize() {
    requireIdle();
    {
        TraversalGuard guard(*this);prepareNodes(*this);synchronizePhysics();
        auto poses=[&](auto&& self,Node& node)->void {
            if(!node.isActiveInHierarchy()) return;
            if(auto* mesh=dynamic_cast<SkeletalMeshComponent*>(&node)) mesh->synchronizeSkeleton();
            for(const auto& child:node.children_) self(self,*child);
        };
        poses(poses,*this);
    }
    flushPendingDestroy();
}
void Scene::fixedUpdate(double step) {
    ProfileScope profile(ProfileCategory::Simulation,"Scene fixed update",name);
    if(!std::isfinite(step) || step<=0) throw std::invalid_argument("Invalid fixed step");
    requireIdle();
    { TraversalGuard guard(*this);prepareNodes(*this);synchronizePhysics();updateNodes(*this,step,true);if(physics_) physics_->step(step); }
    flushPendingDestroy();
}
PhysicsWorld& Scene::physics() {
    if(shuttingDown_) throw std::logic_error("Cannot create physics during scene destruction");
    if(!physics_) physics_=std::make_unique<PhysicsWorld>(*this);
    return *physics_;
}
SelectionSystem& Scene::selection() {
    if(shuttingDown_) throw std::logic_error("Cannot create selection during scene destruction");
    if(!selection_) selection_=std::make_unique<SelectionSystem>(*this);
    return *selection_;
}
void Scene::synchronizePhysics() {
    ProfileScope profile(ProfileCategory::Physics,"Physics reconciliation",name);
    // Validate free motors too, without creating a physics world for them.
    for(const auto& slot:components_) if(const auto* motor=dynamic_cast<const CharacterMotorComponent*>(slot.component);
        motor&&motor->isInitialized()&&motor->isActiveInHierarchy()) motor->validate();
    if(!physics_) for(const auto& slot:components_) {
        const auto* body=dynamic_cast<const RigidBodyComponent*>(slot.component);
        const auto* motor=dynamic_cast<const CharacterMotorComponent*>(slot.component);
        if((body&&body->isInitialized()&&body->isActiveInHierarchy())
            ||(motor&&motor->isInitialized()&&motor->isActiveInHierarchy()&&motor->settings().collisionAware)) { physics();break; }
    }
    if(physics_) physics_->synchronize();
}
void SceneFrame::clear() {
    meshes.clear();lights.clear();particles.clear();ribbons.clear();
    for(auto& layer:layerDraws) layer.clear();
    meshCount=meshParticleCount=decalCount=storageGrowths=culledMeshes=0;
    transformRebuilds={};
}
std::size_t SceneFrame::storageBytes() const {
    auto bytes=meshes.capacity()*sizeof(MeshDraw)+lights.capacity()*sizeof(SceneLight)+particles.capacity()*sizeof(ParticleDraw)+ribbons.capacity()*sizeof(RibbonDraw);
    for(const auto& layer:layerDraws) bytes+=layer.capacity()*sizeof(std::uint32_t);
    return bytes;
}
SceneFrame Scene::collect(std::uint32_t visibilityMask,const Frustum* frustum) const {
    SceneFrame frame;collect(frame,visibilityMask,frustum);return frame;
}
void Scene::updateAnimations(double delta) {
    update(delta);
}
void Scene::collect(SceneFrame& frame,std::uint32_t visibilityMask,const Frustum* frustum) const {
    ProfileScope profile(ProfileCategory::Render,"Scene collection",name);
    frame.clear();
    // One reference keeps the traversal callback within std::function's small storage.
    struct { SceneFrame& frame;std::uint32_t mask;const Frustum* frustum;const PhysicsWorld* physics; } context{frame,visibilityMask,frustum,physics_.get()};
    visit([&context](const Node& node,const Mat4& authoritativeWorld) {
        const auto world=context.physics ? context.physics->renderMatrix(node,authoritativeWorld) : authoritativeWorld;
        auto& frame=context.frame;const auto visibilityMask=context.mask;
        auto appendDraw=[&frame](MeshDraw draw) {
            const auto layer=static_cast<std::size_t>(draw.layer);
            if(layer>=frame.layerDraws.size()) throw std::invalid_argument("Invalid mesh render layer");
            if(frame.meshes.size()>=std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Too many mesh draws");
            const auto index=static_cast<std::uint32_t>(frame.meshes.size());
            const auto meshCapacity=frame.meshes.capacity(),layerCapacity=frame.layerDraws[layer].capacity();
            frame.meshes.push_back(std::move(draw));frame.layerDraws[layer].push_back(index);
            frame.storageGrowths+=(meshCapacity!=frame.meshes.capacity())+(layerCapacity!=frame.layerDraws[layer].capacity());
        };
        if(const auto* mesh=dynamic_cast<const MeshComponent*>(&node); mesh && mesh->mesh && !mesh->mesh->indices.empty() && (mesh->visibilityMask&visibilityMask)) {
            mesh->lodPolicy.validate();
            if(!std::isfinite(mesh->opacity)||mesh->opacity<0||mesh->opacity>1
                ||(mesh->blend!=BlendMode::Opaque&&mesh->blend!=BlendMode::Alpha&&mesh->blend!=BlendMode::Additive))
                throw std::invalid_argument("Invalid mesh blending or opacity");
            ++frame.meshCount;
            const auto& normal=node.normalMatrix(world);
            const auto* skeletal=dynamic_cast<const SkeletalMeshComponent*>(mesh);
            const auto skin=skeletal ? skeletal->skinPose() : nullptr;
            const auto morph=mesh->morphPose();
            if(skin && mesh->mesh->skin.size()!=mesh->mesh->vertices.size()) throw std::invalid_argument("Skeletal mesh requires skin weights for every vertex");
            bool visible=true;
            const auto bounds=mesh->frustumCulling ? mesh->localBounds(skin) : MeshBounds{};
            if(context.frustum && mesh->frustumCulling) {
                if(bounds.valid) visible=context.frustum->intersectsTransformedAabb(bounds.minimum,bounds.maximum,world);
            }
            frame.culledMeshes+=!visible;
            std::uint32_t partIndex=0;
            auto materialDraw=[&](std::size_t slot,std::uint32_t first,std::uint32_t count) {
                auto material=mesh->resolvedMaterial(slot,false);
                const Vec3 color{mesh->color.x*material.baseColor.x,mesh->color.y*material.baseColor.y,mesh->color.z*material.baseColor.z};
                const auto blend=material.alphaMode==AlphaMode::Blend ? BlendMode::Alpha : material.alphaMode==AlphaMode::Additive ? BlendMode::Additive : BlendMode::Opaque;
                const Vec3 x{world.m[0],world.m[1],world.m[2]},y{world.m[4],world.m[5],world.m[6]},z{world.m[8],world.m[9],world.m[10]};
                appendDraw({mesh->mesh,world,color,mesh->layer,material.baseColorTexture,first,count,normal,skin,blend,mesh->opacity*material.opacity,std::move(material),dot(x,cross(y,z))<0,
                    mesh->castsShadow&&mesh->layer==RenderLayer::World&&blend==BlendMode::Opaque,mesh->receivesShadow&&mesh->layer==RenderLayer::World,visible,morph,mesh->allowInstanceReordering,
                    mesh->mesh,bounds,mesh->id(),partIndex++,mesh->lodPolicy,mesh->receivesDecals,mesh->visibilityMask});
            };
            if(mesh->mesh->parts.empty()) materialDraw(0,0,static_cast<std::uint32_t>(mesh->mesh->indices.size()));
            else for(const auto& part:mesh->mesh->parts) {
                if(part.materialIndex>=mesh->mesh->materials.size() || part.firstIndex>mesh->mesh->indices.size()
                    || part.indexCount>mesh->mesh->indices.size()-part.firstIndex || part.indexCount%3)
                    throw std::invalid_argument("Invalid mesh material range");
                materialDraw(part.materialIndex,part.firstIndex,part.indexCount);
            }
        }
        if(const auto* emitter=dynamic_cast<const ParticleEmitterComponent*>(&node); emitter&&(emitter->visibilityMask&visibilityMask)) {
            emitter->appendMeshes(frame,world,context.frustum);
            const auto capacity=frame.particles.capacity();emitter->appendDraws(frame.particles,world);
            frame.storageGrowths+=capacity!=frame.particles.capacity();
            const auto rc=frame.ribbons.capacity();emitter->appendRibbons(frame.ribbons,world);frame.storageGrowths+=rc!=frame.ribbons.capacity();
        }
        if(const auto* light=dynamic_cast<const LightComponent*>(&node); light && (light->visibilityMask&visibilityMask)) {
            light->validateSettings();
            if(light->shadow.enabled) {
                light->shadow.validate();
                if(light->type==LightType::Directional)
                    directionalShadowMatrix(transformDirection(world,light->direction),transformPoint(world,light->shadow.center),light->shadow,1024);
                else spotShadowMatrix(transformPoint(world,{}),transformDirection(world,light->direction),light->outerAngle,light->range);
            }
            const auto lightDirection=normalized(transformDirection(world,light->direction)),lightPosition=transformPoint(world,{});
            for(float value:{lightDirection.x,lightDirection.y,lightDirection.z,lightPosition.x,lightPosition.y,lightPosition.z})if(!std::isfinite(value))throw std::invalid_argument("Invalid world light pose");
            if(light->type!=LightType::Point&&dot(lightDirection,lightDirection)<.5f)throw std::invalid_argument("Singular world light direction");
            const auto capacity=frame.lights.capacity();
            frame.lights.push_back({light->type,lightPosition,lightDirection,light->color,light->intensity,light->range,light->shadow,transformPoint(world,light->shadow.center),light->innerAngle,light->outerAngle});
            frame.storageGrowths+=capacity!=frame.lights.capacity();
        }
    },Mat4::identity(),&frame.transformRebuilds);
    struct {SceneFrame& frame;const PhysicsWorld* physics;std::uint32_t mask;} decalContext{frame,context.physics,visibilityMask};
    visit([&decalContext](const Node& node,const Mat4& authoritativeWorld){if(const auto* decal=dynamic_cast<const DecalComponent*>(&node);decal&&(decal->visibilityMask&decalContext.mask))decal->appendDraw(decalContext.frame,decalContext.physics?decalContext.physics->renderMatrix(node,authoritativeWorld):authoritativeWorld);});
    if(physics_&&visibilityMask) physics_->appendDebugDraws(frame);
}
} // namespace engine
