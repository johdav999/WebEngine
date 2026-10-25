#pragma once
#include "engine/rpg/abilities.hpp"
#include <memory>
#include <variant>

namespace engine::rpg {
struct WorldStamp { std::uint64_t identity = 0, revision = 0; auto operator<=>(const WorldStamp&) const = default; };
// Game-defined integer coordinate units. No float/nav/scene representation enters the kernel.
struct TacticalPosition { std::int64_t x = 0, y = 0, z = 0; auto operator<=>(const TacticalPosition&) const = default; };
struct SpatialEntity { EntityId entity; TacticalPosition position; bool resident = true; };
struct SpatialSnapshot {
    WorldStamp world;
    std::vector<SpatialEntity> entities;
    std::vector<EntityId> legalTargets; // Host queries supply range/LOS/target legality.
};
struct PathQuote {
    TacticalPosition start, destination;
    std::int64_t cost = 0;
    std::vector<TacticalPosition> route;
    bool reachable = false, destinationResident = false, destinationFree = false;
};
class SpatialQuote {
public:
    EntityId actor() const noexcept { return actor_; }
    const SpatialSnapshot& snapshot() const noexcept { return snapshot_; }
    const std::optional<PathQuote>& path() const noexcept { return path_; }
private:
    friend class Runtime;
    SpatialQuote(std::shared_ptr<const void> owner, std::uint64_t generation, std::uint64_t revision,
        std::uint64_t encounter, std::uint64_t turn, EntityId actor, SpatialSnapshot snapshot, std::optional<PathQuote> path)
        : owner_(std::move(owner)), generation_(generation), revision_(revision), encounter_(encounter), turn_(turn),
          actor_(actor), snapshot_(std::move(snapshot)), path_(std::move(path)) {}
    std::shared_ptr<const void> owner_;
    std::uint64_t generation_, revision_, encounter_, turn_;
    EntityId actor_;
    SpatialSnapshot snapshot_;
    std::optional<PathQuote> path_;
};
struct MoveAction {};
struct TacticalAction {
    EntityId actor;
    std::uint64_t encounterEpoch, turnEpoch;
    std::variant<UseAbility, MoveAction> action;
    std::shared_ptr<const SpatialQuote> spatial;
};
struct SetTacticalPosition { EntityId entity; TacticalPosition position; };
struct CooldownCost { ContentId key; TurnPhase phase = TurnPhase::OwnerEnd; std::uint32_t duration = 1; };
struct ActionPlan {
    bool accepted = true;
    std::int64_t ap = 0, movement = 0; // Movement is additional to a move's quoted path cost.
    std::vector<ResourceAmount> resources; // Additional costs, not a replacement for ability costs.
    std::optional<CooldownCost> cooldown;
};
struct TacticalContext { const State& state; const TacticalAction& action; };
enum class ReactionPhase { BeforeAction, AfterAction };
struct ReactionToken { EntityId source; ContentId id; auto operator<=>(const ReactionToken&) const = default; };
struct ReactionContext {
    const TacticalContext& root;
    ReactionPhase phase;
    const ReactionToken& token;
    std::uint32_t depth;
};
struct PendingPresentation {
    std::uint64_t actionEpoch, encounterEpoch, turnEpoch;
    EntityId actor;
    WorldStamp world;
    std::optional<TacticalPosition> destination;
};
enum class PresentationCompletion { Completed, Skipped, Recovered };
struct AcknowledgePresentation {
    std::uint64_t actionEpoch, encounterEpoch, turnEpoch;
    PresentationCompletion completion = PresentationCompletion::Completed;
};
struct TacticalResult {
    PendingPresentation presentation;
    std::optional<ContentId> ability;
    std::int64_t apSpent, movementSpent;
    std::vector<TacticalPosition> route;
};
struct ReactionResult { ReactionToken token; ReactionPhase phase; std::uint32_t depth; };
struct PresentationResult { std::uint64_t actionEpoch; PresentationCompletion completion; };
} // namespace engine::rpg
