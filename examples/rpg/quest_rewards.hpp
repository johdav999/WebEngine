#pragma once
#include "tactics.hpp"
#include "engine/rpg/quest_bridge.hpp"

namespace example {
namespace q=engine::quests;
namespace bridge=r::quest_bridge;
inline std::shared_ptr<const q::Catalog> rewardQuests(bool facts=false) {
    q::CatalogDefinition def;def.id="example.quests";def.content="example.quests.v1";
    def.facts={{"ready",false}};def.events={{"example.delivery",{{"item",std::string{}}}}};
    q::Stage stage;stage.id="collect";
    if(facts)stage.objectives.push_back({"ready",q::ObjectiveMode::Current,{"quest.fact",1,{{"key",std::string("ready")},{"value",true}}},1});
    else stage.objectives.push_back({"delivery",q::ObjectiveMode::Accumulate,{"quest.event",1,{{"event",std::string("example.delivery")},{"field",std::string("item")},{"value",std::string("parcel")}}},1});
    stage.transitions.push_back({}); // Explicit terminal condition is evaluated within the quest budget.
    q::QuestDefinition quest;quest.id="example.delivery";quest.content="example.delivery.v1";quest.entry=stage.id;quest.stages={stage};
    quest.effects={{"experience","example.xp",{{"amount",std::int64_t{10}}}},
                   {"supplies","example.item",{{"quantity",std::int64_t{1}}}}};
    def.quests.push_back(quest);
    engine::ModuleComposition<q::Bindings> composition;
    const std::array registrations{q::moduleRegistration()};
    return q::Catalog::create(std::move(def),composition.replace(registrations));
}
// This sample chooses an existing first character and its loadout as reward recipients.
// Those choices and amounts are game policy, never library defaults.
inline bridge::Policy rewardPolicy() {
    bridge::Policy policy;
    policy.reward=[](const q::EffectRequest& effect,const r::State& state) {
        if(state.characters.empty())return bridge::RewardProposal{false,{}};
        const auto actor=state.characters.begin()->first;
        if(effect.type=="example.xp")return bridge::RewardProposal{true,{r::AddExperience{actor,{std::get<std::int64_t>(effect.payload.at("amount")),r::ContentId("example.practice"),1}}}};
        if(effect.type=="example.item") {
            for(const auto& [bag,value]:state.inventories)if(value.character==actor)
                return bridge::RewardProposal{true,{r::AddItem{bag,r::ContentId("example.tonic"),std::get<std::int64_t>(effect.payload.at("quantity"))}}};
        }
        return bridge::RewardProposal{false,{}};
    };
    return policy;
}
} // namespace example
