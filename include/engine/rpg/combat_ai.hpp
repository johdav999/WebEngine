#pragma once
#include "engine/rpg/runtime.hpp"

namespace engine::rpg {
// Hosts enumerate meaningful ability/item/movement options using current query facts.
struct CombatOption { ContentId id; TacticalAction action; };
struct CombatEvaluation {
    ContentId id;
    std::optional<ErrorCode> rejection;
    std::optional<std::int64_t> score; // Empty: legal but declined by policy.
};
struct CombatScoreContext {
    const State& before;
    const State& after;
    const Results& results;
    const CombatOption& option;
};
using CombatScorer = std::function<std::optional<std::int64_t>(const CombatScoreContext&)>;
struct CombatAiBudget {
    std::size_t evaluations = 32; // Hard cap 64; sorted prefix, explicit truncation.
    Budget action;
};
enum class CombatPlanStatus { Ready, Cancelled, NoAction };
class CombatPlan {
public:
    CombatPlanStatus status() const noexcept { return status_; }
    std::span<const CombatEvaluation> evaluations() const noexcept { return evaluations_; }
    const std::optional<ContentId>& selected() const noexcept { return selected_; }
    bool endTurn() const noexcept { return status_==CombatPlanStatus::Ready&&!selected_; }
    bool truncated() const noexcept { return truncated_; }
    std::optional<ErrorCode> fallbackRejection() const noexcept { return fallbackRejection_; }
private:
    friend class Runtime;
    CombatPlan() = default;
    CombatPlanStatus status_ = CombatPlanStatus::NoAction;
    std::vector<CombatEvaluation> evaluations_;
    std::optional<ContentId> selected_;
    std::optional<Command> command_;
    WorldStamp world_;
    std::stop_token stop_;
    bool truncated_ = false;
    std::optional<ErrorCode> fallbackRejection_;
};
} // namespace engine::rpg
