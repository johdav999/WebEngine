#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>

namespace engine::rpg {
void applyAbilities(State&, Results&, const Catalog&, SessionId, const Operation&, std::size_t&, std::size_t&);
namespace {
void need(bool ok, ErrorCode code, const char* message) { if (!ok) throw Error(code, message); }
std::uint64_t next(std::uint64_t value) {
    need(value != std::numeric_limits<std::uint64_t>::max(), ErrorCode::Overflow, "Encounter counter exhausted");
    return value + 1;
}
void spend(std::size_t& work) {
    need(work != 0, ErrorCode::BudgetExceeded, "Encounter work budget exhausted"); --work;
}
bool valid(EncounterOutcome value) {
    return value == EncounterOutcome::Continue || value == EncounterOutcome::Victory ||
        value == EncounterOutcome::Defeat || value == EncounterOutcome::Escape || value == EncounterOutcome::Stalemate;
}
struct Driver {
    State& state; Results& results; const Catalog& catalog; SessionId session;
    std::size_t &effects, &draws, &work;
    EncounterState& encounter() { return *state.encounter; }
    const EncounterHandler& handler() { return *catalog.bindings()->registry().findEncounter(encounter().policy.id); }
    EncounterContext context() { return {state, encounter()}; }
    void record(EncounterEvent event, std::optional<EntityId> entity = {}) {
        need(results.encounters.size() < Limits::results, ErrorCode::Limit, "Encounter result limit exceeded");
        const auto& e = encounter();
        results.encounters.push_back({event,e.epoch,e.round,e.turnEpoch,entity,e.outcome,e.allowance});
    }
    bool eligible(EntityId entity) { spend(work); return handler().eligible(context(), entity); }
    EncounterDecision decision(EncounterCheck check) {
        spend(work); auto result = handler().decide(context(), check);
        need(result.accepted && valid(result.outcome), ErrorCode::RejectedEncounter, "Encounter policy rejected transition");
        return result;
    }
    void finish(EncounterOutcome outcome) {
        auto& e = encounter(); e.stage = EncounterStage::Finished; e.outcome = outcome;
        e.active.reset(); e.allowance = {}; record(EncounterEvent::Finished);
    }
    bool evaluate() {
        auto result = decision(EncounterCheck::Automatic);
        if (result.outcome != EncounterOutcome::Continue) { finish(result.outcome); return true; }
        return false;
    }
    void phase(TurnPhase phase, std::optional<EntityId> owner = {}) {
        spend(work);
        applyAbilities(state,results,catalog,session,AdvancePhase{phase,owner,next(state.phaseEpoch)},effects,draws);
    }
    bool round() {
        auto& e = encounter(); e.round = next(e.round); e.cursor = 0; e.order.clear();
        phase(TurnPhase::RoundStart);
        if (evaluate()) return false;
        // Evaluate in stable roster order, once per participant per round, including ineligible actors.
        std::vector<InitiativeEntry> order;
        for (auto entity : e.roster) {
            spend(work); order.push_back({entity,handler().initiative(context(),entity)});
        }
        e.order = std::move(order);
        std::sort(e.order.begin(),e.order.end(),[](const auto& a,const auto& b) {
            return a.score == b.score ? a.entity < b.entity : a.score > b.score;
        });
        record(EncounterEvent::RoundBegan); return true;
    }
    void closeTurn() {
        auto& e = encounter(); const auto active = *e.active;
        phase(TurnPhase::OwnerEnd,active); record(EncounterEvent::TurnEnded,active);
        e.active.reset(); e.allowance = {}; ++e.cursor;
    }
    void seek() {
        auto& e = encounter();
        bool recheckRoster = true;
        for (;;) {
            spend(work);
            // Do not manufacture empty rounds forever. Games may select a different outcome in decide().
            if (recheckRoster) {
                bool any = false;
                for (auto entity : e.roster) if (eligible(entity)) { any = true; break; }
                if (!any) { finish(EncounterOutcome::Stalemate); return; }
                recheckRoster = false;
            }
            if (e.cursor == e.order.size()) {
                phase(TurnPhase::RoundEnd);
                if (evaluate() || !round()) return;
                recheckRoster = true;
                continue;
            }
            const auto entity = e.order[e.cursor].entity;
            if (!eligible(entity)) { record(EncounterEvent::Skipped,entity); ++e.cursor; continue; }
            e.active = entity; state.turnEpoch = next(state.turnEpoch); e.turnEpoch = state.turnEpoch;
            phase(TurnPhase::OwnerStart,entity);
            if (evaluate()) return;
            if (!eligible(entity)) {
                // An actor incapacitated by its own start phase still gets one matching end phase.
                closeTurn(); if (evaluate()) return; recheckRoster = true; continue;
            }
            spend(work); e.allowance = handler().allowance(context(),entity);
            need(e.allowance.ap >= 0 && e.allowance.movement >= 0, ErrorCode::InvalidAmount, "Negative turn allowance");
            record(EncounterEvent::TurnBegan,entity); return;
        }
    }
    void stamps(std::uint64_t epoch, std::uint64_t turn) {
        need(state.encounter.has_value(), ErrorCode::InvalidEncounter, "No encounter");
        need(epoch == encounter().epoch && turn == encounter().turnEpoch, ErrorCode::StaleTurn, "Stale encounter or turn epoch");
    }
};
}
void Bindings::addEncounterHandler(ContentId id, EncounterHandler handler) {
    need(!handlers_.contains(id) && !advancement_.contains(id) && !abilities_.contains(id) && !encounters_.contains(id),
        ErrorCode::DuplicateHandler, "Duplicate RPG handler");
    need(size() < Limits::handlers, ErrorCode::Limit, "Too many RPG handlers");
    need(handler.version != 0, ErrorCode::InvalidVersion, "Invalid encounter handler version");
    need(handler.eligible && handler.initiative && handler.allowance && handler.decide,
        ErrorCode::MissingValidator, "Encounter policy requires every callback");
    need(bool(handler.reactions) == bool(handler.react) && (!handler.reactions || handler.authorize),
        ErrorCode::MissingValidator, "Reaction discovery/evaluation require tactical authorization");
    encounters_.emplace(std::move(id),std::move(handler));
}
const EncounterHandler* Bindings::findEncounter(const ContentId& id) const noexcept {
    auto it = encounters_.find(id); return it == encounters_.end() ? nullptr : &it->second;
}
void reconcileEncounter(State& state, Results& results, const Catalog& catalog, SessionId session,
    std::size_t& effects, std::size_t& draws, std::size_t& work) {
    if (!state.encounter || state.encounter->stage == EncounterStage::Finished) return;
    Driver d{state,results,catalog,session,effects,draws,work};
    if (d.evaluate()) return;
    if (state.encounter->active && !d.eligible(*state.encounter->active)) {
        d.closeTurn(); if (!d.evaluate()) d.seek();
    }
}
void applyEncounter(State& state, Results& results, const Catalog& catalog, SessionId session,
    const Operation& operation, std::size_t& effects, std::size_t& draws, std::size_t& work) {
    Driver d{state,results,catalog,session,effects,draws,work};
    std::visit([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, BeginEncounter>) {
            need(!state.encounter, ErrorCode::InvalidEncounter, "Exit previous encounter before beginning another");
            need(!op.participants.empty() && op.participants.size() <= Limits::participants, ErrorCode::Limit, "Encounter requires 1-64 participants");
            const auto* handler = catalog.bindings()->registry().findEncounter(op.policy.id);
            need(handler != nullptr, ErrorCode::MissingHandler, "Unknown encounter handler");
            need(handler->version == op.policy.version, ErrorCode::HandlerVersionMismatch, "Encounter handler version mismatch");
            auto roster = op.participants; std::sort(roster.begin(),roster.end());
            need(std::adjacent_find(roster.begin(),roster.end()) == roster.end(), ErrorCode::InvalidEncounter, "Duplicate participant");
            for (auto entity : roster) need(state.characters.contains(entity), ErrorCode::MissingEntity, "Unknown participant");
            state.encounterEpoch = next(state.encounterEpoch);
            state.encounter.emplace(EncounterState{op.policy});
            auto& e = *state.encounter; e.epoch = state.encounterEpoch; e.turnEpoch = state.turnEpoch; e.roster = std::move(roster);
            d.record(EncounterEvent::Began);
            if (!d.evaluate() && d.round()) d.seek();
        } else if constexpr (std::is_same_v<T, EndTurn>) {
            d.stamps(op.encounterEpoch,op.turnEpoch);
            need(d.encounter().stage == EncounterStage::AwaitingCommand && d.encounter().active == op.entity,
                ErrorCode::InvalidEncounter, "EndTurn requires the active participant");
            d.closeTurn(); if (!d.evaluate()) d.seek();
        } else if constexpr (std::is_same_v<T, FinishEncounter>) {
            d.stamps(op.encounterEpoch,op.turnEpoch);
            need(d.encounter().stage == EncounterStage::AwaitingCommand, ErrorCode::InvalidEncounter, "Encounter already finished");
            const auto decision = d.decision(EncounterCheck::Finish);
            need(decision.outcome != EncounterOutcome::Continue, ErrorCode::RejectedEncounter, "Finish requires a terminal outcome");
            d.finish(decision.outcome);
        } else if constexpr (std::is_same_v<T, ExitEncounter>) {
            d.stamps(op.encounterEpoch,op.turnEpoch);
            need(d.encounter().stage == EncounterStage::Finished, ErrorCode::InvalidEncounter, "Finish encounter before exit");
            const auto decision = d.decision(EncounterCheck::Exit);
            need(decision.outcome == d.encounter().outcome, ErrorCode::RejectedEncounter, "Exit cannot rewrite outcome");
            d.record(EncounterEvent::Exited); state.encounter.reset();
        } else if constexpr (std::is_same_v<T, SetCooldown>) {
            need(state.characters.contains(op.entity), ErrorCode::MissingEntity, "Unknown cooldown owner");
            need(op.phase == TurnPhase::OwnerStart || op.phase == TurnPhase::OwnerEnd || op.phase == TurnPhase::RoundStart || op.phase == TurnPhase::RoundEnd,
                ErrorCode::InvalidStatus, "Invalid cooldown phase");
            need(op.remaining <= Limits::statusDuration, ErrorCode::Limit, "Cooldown duration exceeds cap");
            spend(effects);
            auto& entries = state.cooldowns[op.entity];
            if (!op.remaining) entries.erase(op.key);
            else {
                need(entries.contains(op.key) || entries.size() < Limits::cooldowns, ErrorCode::Limit, "Cooldown count exceeds cap");
                entries.insert_or_assign(op.key,CooldownState{op.phase,op.remaining});
            }
            if (entries.empty()) state.cooldowns.erase(op.entity);
            results.cooldowns.push_back({op.entity,op.key,op.remaining});
        }
    },operation);
}
} // namespace engine::rpg
