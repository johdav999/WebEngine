#pragma once
#include "engine/node.hpp"
#include "engine/actor.hpp"
#include "engine/mesh.hpp"
#include "engine/light_component.hpp"
#include "engine/render_layer.hpp"
#include "engine/particle_emitter_component.hpp"
#include "engine/environment.hpp"
#include "engine/sky.hpp"
#include "engine/fog.hpp"
#include <optional>
#include <array>

namespace engine {
class PhysicsWorld;
class SelectionSystem;
class Application;
enum class SceneLifecycleState { Detached, Loaded, Active };
struct Frustum;
struct MeshDraw {
    std::shared_ptr<const Mesh> mesh; Mat4 model; Vec3 color; RenderLayer layer;
    std::shared_ptr<const Texture> texture;
    std::uint32_t firstIndex=0,indexCount=0;
    Mat4 normal;
    std::shared_ptr<const SkinPose> skinPose;
    BlendMode blend=BlendMode::Opaque;
    float opacity=1;
    Material material;
    bool mirrored=false;
    bool castsShadow=true,receivesShadow=true;
    bool cameraVisible=true;
    std::shared_ptr<const MorphPose> morphPose;
    bool allowInstanceReordering=false;
    std::shared_ptr<const Mesh> sourceMesh; // Independent shadow detail uses original geometry.
    MeshBounds sourceBounds;
    ComponentId component;
    std::uint32_t part=0;
    MeshLodPolicy lodPolicy;
    bool receivesDecals=true;
    std::uint32_t visibilityMask=1;
};
struct SceneLight { LightType type; Vec3 position, direction, color; float intensity, range; ShadowSettings shadow; Vec3 shadowCenter; };
struct SceneFrame {
    std::vector<MeshDraw> meshes;
    std::vector<SceneLight> lights;
    std::vector<ParticleDraw> particles;
    std::vector<RibbonDraw> ribbons;
    // Indices into meshes, in traversal order within each layer.
    std::array<std::vector<std::uint32_t>,static_cast<std::size_t>(RenderLayer::Count)> layerDraws;
    std::size_t meshCount=0,meshParticleCount=0,decalCount=0,storageGrowths=0,culledMeshes=0;
    TransformCacheStats transformRebuilds;
    void clear(); // Releases draw references, retains all vector capacities.
    std::size_t storageBytes() const; // Reserved vector payload, excludes assets.
};
class Scene : public Node {
public:
    explicit Scene(std::string name = "Node");
    ~Scene() override;
    SceneLifecycleState lifecycleState() const { return lifecycleState_; }
    Vec3 ambient{0.14f,0.17f,0.23f};
    std::shared_ptr<const Environment> environment=Environment::studio();
    float environmentIntensity=1,environmentRotation=0; // Rotation radians about world Y.
    void setFog(std::optional<FogSettings> fog) { if(fog) fog->validate();fog_=std::move(fog); }
    const std::optional<FogSettings>& fog() const { return fog_; }
    void setSky(std::optional<SkyBackground> sky) { if(sky) sky->validate();sky_=std::move(sky); }
    const std::optional<SkyBackground>& sky() const { return sky_; }
    template<class T=Actor,class... Args> T& spawn(Args&&... args) {
        static_assert(std::is_base_of_v<Actor,T>);
        return addChild<T>(std::forward<Args>(args)...);
    }
    Actor* findActor(ActorId id);
    const Actor* findActor(ActorId id) const;
    Component* findComponent(ComponentId id);
    const Component* findComponent(ComponentId id) const;
    std::vector<Actor*> findActorsWithTag(std::string_view tag);
    std::vector<const Actor*> findActorsWithTag(std::string_view tag) const;
    void update(double deltaSeconds); // Lifecycle and opt-in actor/component ticks, once.
    void synchronize(); // Reconcile lifecycle/poses while paused, without simulation ticks.
    void fixedUpdate(double stepSeconds); // Explicit fixed step; does not advance animation.
    PhysicsWorld& physics(); // Lazy; adding an active rigid body also creates it.
    SelectionSystem& selection(); // Lazy; runtime state and action dispatch.
    SelectionSystem* selectionIfCreated() { return selection_.get(); }
    const SelectionSystem* selectionIfCreated() const { return selection_.get(); }
    PhysicsWorld* physicsIfCreated() { return physics_.get(); }
    const PhysicsWorld* physicsIfCreated() const { return physics_.get(); }
    void flushPendingDestroy(); // Idle only; update/fixedUpdate flush automatically.
    SceneFrame collect(std::uint32_t visibilityMask=0xffffffffu,const Frustum* frustum=nullptr) const;
    void updateAnimations(double deltaSeconds); // Compatibility alias for update().
    void collect(SceneFrame& frame,std::uint32_t visibilityMask=0xffffffffu,const Frustum* frustum=nullptr) const; // Refill caller-owned reusable storage.
protected:
    virtual void onLoad() {} // May throw; Application rolls back ownership and calls onUnload.
    virtual void onUnload() noexcept {}
    virtual void onActivate() noexcept {}
    virtual void onDeactivate() noexcept {}
    virtual void onPause() noexcept {}
    virtual void onResume() noexcept {}
private:
    std::optional<SkyBackground> sky_;
    std::optional<FogSettings> fog_;
    friend class Application;
    SceneLifecycleState lifecycleState_=SceneLifecycleState::Detached;
    friend class Node;
    friend class Actor;
    friend class Component;
    friend class PhysicsWorld;
    struct ActorSlot { Actor* actor=nullptr;std::uint32_t generation=1; };
    struct ComponentSlot { Component* component=nullptr;std::uint32_t generation=1; };
    std::vector<ActorSlot> actors_;
    std::vector<std::uint32_t> freeActors_;
    std::vector<ComponentSlot> components_;
    std::vector<std::uint32_t> freeComponents_;
    std::uint64_t identity_;
    bool pendingDestroy_=false,shuttingDown_=false;
    std::unique_ptr<PhysicsWorld> physics_;
    std::unique_ptr<SelectionSystem> selection_;
    void synchronizePhysics();
    void attachSubtree(Node& node);
    void reserveSubtreeHandles(const Node& node);
    void detachSubtree(Node& node) noexcept;
    void queueRemoval(Node& node);
    void prepareNodes(Node& node);
    void updateNodes(Node& node,double delta,bool fixed);
};
} // namespace engine
