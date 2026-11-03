#pragma once
#include "encounters.hpp"

namespace example {
inline r::EncounterHandler tacticalRules() {
    auto rules=encounterRules();
    rules.authorize=[](const r::TacticalContext& c) {
        r::ActionPlan plan;plan.ap=1;
        if(const auto* use=std::get_if<r::UseAbility>(&c.action.action))
            if(use->ability==r::ContentId("example.strike"))
                plan.cooldown=r::CooldownCost{use->ability,r::TurnPhase::OwnerEnd,1};
        return plan;
    };
    return rules;
}
} // namespace example
