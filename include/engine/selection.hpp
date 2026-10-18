#pragma once
#include "engine/camera_component.hpp"
#include "engine/input.hpp"
#include "engine/interaction_component.hpp"
#include "engine/scene.hpp"
#include <deque>
#include <map>

namespace engine {
enum class SelectionGeometry { Bounds, Triangles };
struct SelectionQuery {
    float maxDistance=1000;
    std::uint32_t selectionMask=0xffffffffu,visibilityMask=0xffffffffu;
    float minDistance=0;
    SelectionGeometry geometry=SelectionGeometry::Bounds;
    bool occlusion=false; // Visible mesh triangles block targets, even without providers.
    std::uint32_t occluderMask=0xffffffffu; // Additional visibility-group filter for blockers.
};
enum class SelectionKind { Selection, Focus };
enum class SelectionReason { Explicit, Pointer, Invalidated, Cancelled };
struct SelectionChange {
    SelectionKind kind;
    ActorId previous,current;
    SelectionReason reason;
    std::uint64_t sequence=0;
};
struct SelectionHit {
    ActorId actor;
    ComponentId provider,surface; // surface is proxy provider or mesh component.
    Vec3 point;
    float distance=0; // World units, regardless of ray direction length or object scale.
    std::optional<std::uint32_t> triangle; // Index-buffer triangle offset / 3; empty for bounds/proxies.
    Vec3 normal{},barycentric{}; // World geometric normal; weights for the triangle's three vertices.
    Vec2 uv{};
};
struct ActionHandle {
    ActorId target;
    ComponentId provider;
    std::string id;
    std::uint64_t revision=0; // Reject replaced/removed action definitions.
};
struct AvailableAction {
    ActionHandle handle;
    std::string label;
    ActionEligibility eligibility;
};
// Scene-owned, single-target selection/focus. Callbacks are synchronous, ordered
// and run under the scene traversal guard: destroy() is safe, erase/reparent is not.
class SelectionSystem {
public:
    explicit SelectionSystem(Scene& scene):scene_(scene) {}
    ActorId selected() const;
    ActorId focused() const;
    bool select(ActorId actor,SelectionReason reason=SelectionReason::Explicit);
    bool focus(ActorId actor,SelectionReason reason=SelectionReason::Explicit);
    void clear(SelectionReason reason=SelectionReason::Explicit);
    void refresh(); // Scene calls after update, fixedUpdate and destruction flush.
    std::optional<SelectionHit> resolveRay(Ray ray,const SelectionQuery& query={},SelectionKind kind=SelectionKind::Selection);
    std::optional<SelectionHit> resolvePointer(Vec2 pixel,const CameraComponent& camera,PixelViewport viewport,
        const SelectionQuery& query={},SelectionKind kind=SelectionKind::Selection);
    // Backing-pixel pointer packets + Select action pulses. Drags never select by
    // themselves. Cancellation clears hover, preserving explicit selection.
    void processInput(const InputSystem& input,const CameraComponent& camera,PixelViewport viewport,const SelectionQuery& query={});
    std::vector<AvailableAction> availableActions(ActorId target,ActorId instigator={});
    std::vector<AvailableAction> availableActions() { return availableActions(selected()); }
    ActionResult execute(const ActionHandle& action,ActorId instigator={});
    using ListenerId=std::uint64_t;
    ListenerId subscribe(std::function<void(const SelectionChange&)> listener);
    bool unsubscribe(ListenerId id);
private:
    Scene& scene_;
    ActorId selected_,focused_;
    std::optional<Vec2> pointer_;
    std::int32_t pointerId_=0;
    std::map<ListenerId,std::function<void(const SelectionChange&)>> listeners_;
    std::deque<SelectionChange> changes_;
    std::uint64_t nextListener_=1,sequence_=0;
    bool publishing_=false;
    bool valid(ActorId id,SelectionKind kind) const;
    bool set(ActorId id,SelectionKind kind,SelectionReason reason);
    void publish();
    ActionEligibility eligibility(const InteractionAction& action,const InteractionContext& context) const;
};
} // namespace engine
