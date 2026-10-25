#pragma once
#include "engine/rpg/runtime.hpp"
#include <array>

namespace example {
namespace r = engine::rpg;
inline r::CatalogDefinition characterDefinitions(bool operatorRules = false) {
    r::CatalogDefinition result{r::ContentId("example.characters"), r::ContentRevision(operatorRules ? 2 : 1), {}};
    r::CharacterDefinition character{r::ContentId(operatorRules ? "example.operator" : "example.scholar")};
    if (operatorRules) {
        character.attributes.push_back({r::ContentId("example.precision"), r::ContentId("example.percent"), 100, 0, 10000, 500});
        character.resources.push_back({r::ContentId("example.charge"), r::ContentId("example.joule"), 100, 0, 5000, 2500, 2000});
    } else {
        character.attributes.push_back({r::ContentId("example.insight"), r::ContentId("example.point"), 1, 0, 100, 10});
        character.resources.push_back({r::ContentId("example.reserve"), r::ContentId("example.point"), 1, 0, 100, 20, 20});
    }
    character.progression = r::ContentId("example.training");
    result.characters.push_back(std::move(character));
    result.progressions.push_back({r::ContentId("example.training"), r::ContentId("example.practice"), 1,
        operatorRules ? 30 : 100, operatorRules ? std::vector<std::int64_t>{5, 10} : std::vector<std::int64_t>{10, 30, 60},
        {r::ContentId("example.first"), r::ContentId("example.second"), r::ContentId("example.third")},
        r::HandlerReference{r::ContentId("example.unlocks"), 1}});
    return result;
}
inline r::UnlockProposal unlocks(const r::Advancement& event) {
    const std::array names{"example.first", "example.second", "example.third"};
    return {true, {r::ContentId(names.at(event.toLevel - 1))}};
}
inline std::shared_ptr<const r::Catalog> characterCatalog(
    r::CatalogDefinition definition,
    std::function<r::UnlockProposal(const r::Advancement&)> handler = unlocks) {
    engine::ModuleRegistration<r::Bindings> rules;
    rules.descriptor.id = "example.character-rules";
    rules.descriptor.dependencies.push_back({"webengine.rpg", {{1, 1, 0}, {2, 0, 0}}});
    rules.install = [handler = std::move(handler)](r::Bindings& bindings) {
        bindings.addAdvancementHandler(r::ContentId("example.unlocks"), {1, handler});
    };
    r::Composition composition;
    const std::array registrations{r::moduleRegistration(), rules};
    return r::Catalog::create(std::move(definition), composition.replace(registrations));
}
} // namespace example
