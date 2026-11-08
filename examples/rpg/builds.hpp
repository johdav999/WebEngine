#pragma once
#include "tactics.hpp"
namespace example {
inline r::CatalogDefinition buildDefinitions() {
    auto d=abilityDefinitions();
    d.progressions[0].allowedUnlocks.insert(d.progressions[0].allowedUnlocks.end(),
        {r::ContentId("example.mastery"),r::ContentId("example.scouting"),r::ContentId("example.expert")});
    d.choiceRules.push_back({r::ContentId("example.training"),1,{
        {r::ContentId("example.mastery"),1,{1,{},{}},r::ContentId("example.specialization")},
        {r::ContentId("example.scouting"),1,{1,{},{}},r::ContentId("example.specialization")},
        {r::ContentId("example.expert"),1,{2,{},{r::ContentId("example.mastery")}},{}},
    }});
    d.inventories[0].slots.push_back(r::ContentId("example.offhand"));
    d.items.push_back({r::ContentId("example.greatblade"),1,{r::ContentId("example.hand")},
        {{r::ModifierTarget::Attribute,r::ContentId("example.insight"),{7,r::ContentId("example.point"),1}}}});
    d.items.push_back({r::ContentId("example.shield"),1,{r::ContentId("example.offhand")},{}});
    d.equipmentRules.push_back({r::ContentId("example.greatblade"),
        {1,{{r::ContentId("example.insight"),{10,r::ContentId("example.point"),1}}},{r::ContentId("example.mastery")}},
        {r::ContentId("example.offhand")}});
    return d;
}
} // namespace example
