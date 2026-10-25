#pragma once
#include "engine/rpg/characters.hpp"
#include "engine/rpg/random.hpp"
#include <variant>

namespace engine::rpg {
enum class TurnPhase { OwnerStart, OwnerEnd, RoundStart, RoundEnd };
enum class StatusStacking { Reject, Refresh, Stack };
struct ResourceAmount { ContentId resource; Amount amount; };
struct StatusDefinition {
    ContentId id;
    TurnPhase phase = TurnPhase::OwnerEnd;
    std::uint32_t duration = 1, maximumStacks = 1;
    StatusStacking stacking = StatusStacking::Reject;
    bool refreshOnStack = false, includeApplicationPhase = true;
    std::vector<Modifier> modifiers;
    std::vector<ResourceAmount> tickEffects; // Once per stack on each counted phase, before expiry.
};
struct TargetRequirement {
    std::size_t minimum = 1, maximum = 1;
    bool allowSelf = true, allowOthers = true;
};
struct ItemCost { ContentId definition; std::int64_t quantity = 1; };
struct AbilityDefinition {
    ContentId id;
    HandlerReference handler;
    std::uint32_t schemaVersion = 1;
    std::map<ContentId, Amount> parameters;
    std::vector<ResourceAmount> costs;
    TargetRequirement targets;
    std::optional<ItemCost> itemCost;
};
struct ResourceEffect { EntityId entity; ContentId resource; Amount delta; };
struct ApplyStatus { EntityId entity; ContentId definition, source; };
struct DispelStatus { EntityId entity; ContentId source; };
using Effect = std::variant<ResourceEffect, ApplyStatus, DispelStatus>;
struct EffectProposal { bool accepted = true; std::vector<Effect> effects; };
struct UseAbility {
    EntityId entity;
    ContentId ability;
    std::vector<EntityId> targets;
    std::optional<ItemId> item;
};
// R5 supplies an increasing global phase epoch and the encounter-authorized owner.
// Owner phases require an owner; round phases require no owner.
struct AdvancePhase { TurnPhase phase; std::optional<EntityId> owner; std::uint64_t epoch; };
struct State;
struct AbilityContext {
    const State& state; // Candidate after costs, before proposed effects.
    const AbilityDefinition& definition;
    EntityId entity;
    std::span<const EntityId> targets; // Sorted, unique, validated.
};
enum class StatusChangeKind { Applied, Refreshed, Stacked, Skipped, Ticked, Expired, Dispelled };
struct StatusChange { EntityId entity; ContentId source; StatusChangeKind kind; };
struct AbilityResult { EntityId entity; ContentId definition; };
} // namespace engine::rpg
