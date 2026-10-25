#pragma once
#include "inventory.hpp"

namespace example {
inline r::CatalogDefinition abilityDefinitions() {
    auto def = inventoryDefinitions();
    def.characters[0].resources.push_back({r::ContentId("example.focus"), r::ContentId("example.point"), 1, 0, 20, 20, 5});
    for (const auto& name : {"example.strike", "example.heal", "example.concentrate"}) {
        r::AbilityDefinition ability{r::ContentId(name), {r::ContentId("example.effects"), 1}};
        ability.parameters.emplace(r::ContentId("example.delta"), r::Amount{
            ability.id == r::ContentId("example.strike") ? -4 : 3, r::ContentId("example.point"), 1});
        ability.costs.push_back({r::ContentId("example.reserve"), {1, r::ContentId("example.point"), 1}});
        if (ability.id == r::ContentId("example.heal")) ability.itemCost = r::ItemCost{r::ContentId("example.tonic"), 1};
        def.abilities.push_back(std::move(ability));
    }
    r::StatusDefinition focused{r::ContentId("example.focused")};
    focused.duration = 2; focused.maximumStacks = 3; focused.stacking = r::StatusStacking::Stack;
    focused.modifiers.push_back({r::ModifierTarget::Attribute, r::ContentId("example.insight"), {2, r::ContentId("example.point"), 1}});
    focused.tickEffects.push_back({r::ContentId("example.reserve"), {-1, r::ContentId("example.point"), 1}});
    def.statuses.push_back(focused);
    r::StatusDefinition ward{r::ContentId("example.ward")};
    ward.stacking = r::StatusStacking::Refresh; ward.includeApplicationPhase = false;
    ward.modifiers.push_back({r::ModifierTarget::ResourceMaximum, r::ContentId("example.reserve"), {10, r::ContentId("example.point"), 1}});
    def.statuses.push_back(ward);
    r::StatusDefinition mark{r::ContentId("example.mark")}; mark.phase = r::TurnPhase::RoundEnd;
    def.statuses.push_back(mark);
    return def;
}
inline void validateAbility(const r::AbilityDefinition& def) {
    auto found = def.parameters.find(r::ContentId("example.delta"));
    if (def.parameters.size() != 1 || found == def.parameters.end() ||
        found->second.unit != r::ContentId("example.point") || found->second.scale != 1 ||
        found->second.value < -10 || found->second.value > 10)
        throw r::Error(r::ErrorCode::InvalidDefinition, "Sample ability expects one bounded point delta");
}
inline r::EffectProposal effects(const r::AbilityContext& context, r::Random& random) {
    auto delta = context.definition.parameters.at(r::ContentId("example.delta"));
    if (delta.value < 0) delta.value -= random.uniform(3);
    const r::ContentId resource(context.definition.id == r::ContentId("example.concentrate") ? "example.focus" : "example.reserve");
    r::EffectProposal proposal;
    for (auto target : context.targets) proposal.effects.push_back(r::ResourceEffect{target, resource, delta});
    return proposal;
}
inline std::shared_ptr<const r::Catalog> abilityCatalog(r::CatalogDefinition def = abilityDefinitions(),
    std::function<r::EffectProposal(const r::AbilityContext&, r::Random&)> evaluate = effects,
    std::shared_ptr<void> lifetime = {}) {
    engine::ModuleRegistration<r::Bindings> rules;
    rules.descriptor.id = "example.ability-rules";
    rules.descriptor.dependencies.push_back({"webengine.rpg", {{1, 3, 0}, {2, 0, 0}}});
    rules.install = [evaluate = std::move(evaluate), lifetime = std::move(lifetime)](r::Bindings& bindings) {
        (void)lifetime;
        bindings.addAdvancementHandler(r::ContentId("example.unlocks"), {1, unlocks});
        bindings.addAbilityHandler(r::ContentId("example.effects"), {1, 1, validateAbility, evaluate});
    };
    r::Composition composition;
    const std::array registrations{r::moduleRegistration(), rules};
    return r::Catalog::create(std::move(def), composition.replace(registrations));
}
} // namespace example
