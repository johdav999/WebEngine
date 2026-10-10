#pragma once
#include "engine/actor.hpp"
#include "engine/mesh.hpp"
#include <functional>
#include <optional>

namespace engine {
struct InteractionContext {
    Scene& scene;
    ActorId target,instigator; // Empty instigator is allowed for editor/UI actions.
};
struct ActionEligibility { bool allowed=true; std::string reason; };
enum class ActionStatus { Succeeded, Ineligible, Unavailable, Failed };
struct ActionResult {
    ActionStatus status=ActionStatus::Succeeded;
    std::string reason;
    explicit operator bool() const { return status==ActionStatus::Succeeded; }
};
struct InteractionAction {
    std::string id,label;
    std::function<ActionEligibility(const InteractionContext&)> eligibility;
    std::function<ActionResult(const InteractionContext&)> execute;
};
// Opt-in actor picking and runtime action provider. Mesh bounds include owned
// nested meshes, but exclude child actors. Proxy bounds replace mesh picking.
class InteractionComponent : public Component {
public:
    explicit InteractionComponent(std::string name="Interaction"):Component(std::move(name)) {}
    std::string_view typeName() const noexcept override { return "InteractionComponent"; }
    bool selectable=true,focusable=true;
    std::uint32_t selectionMask=0xffffffffu;
    std::optional<MeshBounds> proxyBounds; // In this component's local space.
    void addAction(InteractionAction action); // Unique, nonempty IDs; empty executor is unbound.
    bool removeAction(std::string_view id);
    const std::vector<InteractionAction>& actions() const { return actions_; }
    std::uint64_t actionRevision() const { return actionRevision_; }
    void validate() const override;
private:
    std::vector<InteractionAction> actions_;
    std::uint64_t actionRevision_=0;
};
} // namespace engine
