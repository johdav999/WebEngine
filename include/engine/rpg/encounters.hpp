#pragma once
#include "engine/rpg/abilities.hpp"
#include <functional>
#include "engine/rpg/tactics.hpp"

namespace engine::rpg {
enum class EncounterOutcome { Continue, Victory, Defeat, Escape, Stalemate };
enum class EncounterStage { AwaitingCommand, Finished };
enum class EncounterCheck { Automatic, Finish, Exit };
struct TurnAllowance { std::int64_t ap = 0, movement = 0; };
struct InitiativeEntry { EntityId entity; std::int64_t score; };
struct EncounterState {
    HandlerReference policy;
    std::uint64_t epoch = 0, round = 0, turnEpoch = 0;
    std::vector<EntityId> roster; // Sorted, unique, fixed until exit.
    std::vector<InitiativeEntry> order;
    std::size_t cursor = 0;
    std::optional<EntityId> active;
    TurnAllowance allowance;
    EncounterStage stage = EncounterStage::AwaitingCommand;
    EncounterOutcome outcome = EncounterOutcome::Continue;
};
struct EncounterContext { const State& state; const EncounterState& encounter; };
struct EncounterDecision { bool accepted = true; EncounterOutcome outcome = EncounterOutcome::Continue; };
// Pure, bounded callbacks; borrowed context expires on return. Capture provider ownership.
struct EncounterHandler {
    std::uint32_t version = 1;
    std::function<bool(const EncounterContext&, EntityId)> eligible;
    std::function<std::int64_t(const EncounterContext&, EntityId)> initiative;
    std::function<TurnAllowance(const EncounterContext&, EntityId)> allowance;
    std::function<EncounterDecision(const EncounterContext&, EncounterCheck)> decide;
    std::function<ActionPlan(const TacticalContext&)> authorize;
    std::function<std::vector<ReactionToken>(const TacticalContext&, ReactionPhase, const std::optional<ReactionToken>&)> reactions;
    std::function<EffectProposal(const ReactionContext&, Random&)> react;
};
struct BeginEncounter { HandlerReference policy; std::vector<EntityId> participants; };
struct EndTurn { EntityId entity; std::uint64_t encounterEpoch, turnEpoch; };
struct FinishEncounter { std::uint64_t encounterEpoch, turnEpoch; };
struct ExitEncounter { std::uint64_t encounterEpoch, turnEpoch; };
struct CooldownState { TurnPhase phase; std::uint32_t remaining; };
// Zero clears; positive replaces. Game keys/ability association remain game policy.
struct SetCooldown { EntityId entity; ContentId key; TurnPhase phase; std::uint32_t remaining; };
struct CooldownResult { EntityId entity; ContentId key; std::uint32_t remaining; };
enum class EncounterEvent { Began, RoundBegan, TurnBegan, TurnEnded, Skipped, Finished, Exited };
struct EncounterResult {
    EncounterEvent event;
    std::uint64_t encounterEpoch, round, turnEpoch;
    std::optional<EntityId> entity;
    EncounterOutcome outcome;
    TurnAllowance allowance;
};
} // namespace engine::rpg
