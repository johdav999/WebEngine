#pragma once
#include "abilities.hpp"

namespace example {
inline r::EncounterHandler encounterRules() {
    return {1,
        [](const r::EncounterContext& c,r::EntityId id) {
            return c.state.characters.at(id).resources.at(r::ContentId("example.reserve")).current > 0;
        },
        [](const r::EncounterContext& c,r::EntityId id) {
            return c.state.characters.at(id).attributes.at(r::ContentId("example.insight")).value;
        },
        [](const r::EncounterContext&,r::EntityId) { return r::TurnAllowance{2,6}; },
        [](const r::EncounterContext& c,r::EncounterCheck check) {
            if (check == r::EncounterCheck::Exit) return r::EncounterDecision{true,c.encounter.outcome};
            if (check == r::EncounterCheck::Finish) return r::EncounterDecision{true,r::EncounterOutcome::Escape};
            // No teams in this neutral example. Automatic victory is selected by game-specific rules.
            return r::EncounterDecision{};
        }};
}
inline std::shared_ptr<const r::Catalog> encounterCatalog(r::EncounterHandler policy = encounterRules(),
    r::CatalogDefinition def = abilityDefinitions()) {
    engine::ModuleRegistration<r::Bindings> rules;
    rules.descriptor.id = "example.encounter-rules";
    rules.descriptor.dependencies.push_back({"webengine.rpg",{{1,4,0},{2,0,0}}});
    rules.install = [policy = std::move(policy)](r::Bindings& b) {
        b.addAdvancementHandler(r::ContentId("example.unlocks"),{1,unlocks});
        b.addAbilityHandler(r::ContentId("example.effects"),{1,1,validateAbility,effects});
        b.addEncounterHandler(r::ContentId("example.turns"),policy);
    };
    r::Composition composition;
    const std::array registrations{r::moduleRegistration(),rules};
    return r::Catalog::create(std::move(def),composition.replace(registrations));
}
} // namespace example
