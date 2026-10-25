#include "engine/rpg/runtime.hpp"
#include "wire.hpp"
#include <algorithm>
#include <limits>

namespace engine::rpg::wire {
template<> struct Fields<AttributeRequirement> { static constexpr auto members=std::tuple{&AttributeRequirement::attribute,&AttributeRequirement::minimum}; };
template<> struct Fields<BuildRequirements> { static constexpr auto members=std::tuple{&BuildRequirements::minimumLevel,&BuildRequirements::attributes,&BuildRequirements::unlocks}; };
template<> struct Fields<EquipmentRule> { static constexpr auto members=std::tuple{&EquipmentRule::item,&EquipmentRule::requirements,&EquipmentRule::requiredSlots}; };
template<> struct Fields<UnlockChoice> { static constexpr auto members=std::tuple{&UnlockChoice::id,&UnlockChoice::cost,&UnlockChoice::requirements,&UnlockChoice::exclusiveGroup}; };
template<> struct Fields<ProgressionChoices> { static constexpr auto members=std::tuple{&ProgressionChoices::progression,&ProgressionChoices::pointsPerLevel,&ProgressionChoices::choices}; };
template<> struct Fields<VitalityState> { static constexpr auto members=std::tuple{&VitalityState::resource,&VitalityState::condition,&VitalityState::onZero}; };
template<> struct Fields<PartyMember> { static constexpr auto members=std::tuple{&PartyMember::entity,&PartyMember::active,&PartyMember::control}; };
template<> struct Fields<PartyState> { static constexpr auto members=std::tuple{&PartyState::capacity,&PartyState::activeLimit,&PartyState::members,&PartyState::leader}; };
template<> struct Fields<CombatMember> { static constexpr auto members=std::tuple{&CombatMember::team,&CombatMember::control}; };
template<> struct Fields<EntityId> { static constexpr auto members=std::tuple{&EntityId::session,&EntityId::value}; };
template<> struct Fields<ItemId> { static constexpr auto members=std::tuple{&ItemId::session,&ItemId::value}; };
template<> struct Fields<Amount> { static constexpr auto members=std::tuple{&Amount::value,&Amount::unit,&Amount::scale}; };
template<> struct Fields<AttributeDefinition> { static constexpr auto members=std::tuple{&AttributeDefinition::id,&AttributeDefinition::unit,&AttributeDefinition::scale,&AttributeDefinition::minimum,&AttributeDefinition::maximum,&AttributeDefinition::initial}; };
template<> struct Fields<ResourceDefinition> { static constexpr auto members=std::tuple{&ResourceDefinition::id,&ResourceDefinition::unit,&ResourceDefinition::scale,&ResourceDefinition::minimumMaximum,&ResourceDefinition::maximumMaximum,&ResourceDefinition::initialMaximum,&ResourceDefinition::initialCurrent}; };
template<> struct Fields<HandlerReference> { static constexpr auto members=std::tuple{&HandlerReference::id,&HandlerReference::version}; };
template<> struct Fields<ProgressionDefinition> { static constexpr auto members=std::tuple{&ProgressionDefinition::id,&ProgressionDefinition::unit,&ProgressionDefinition::scale,&ProgressionDefinition::maximumExperience,&ProgressionDefinition::thresholds,&ProgressionDefinition::allowedUnlocks,&ProgressionDefinition::handler}; };
template<> struct Fields<CharacterDefinition> { static constexpr auto members=std::tuple{&CharacterDefinition::id,&CharacterDefinition::attributes,&CharacterDefinition::resources,&CharacterDefinition::progression}; };
template<> struct Fields<Modifier> { static constexpr auto members=std::tuple{&Modifier::kind,&Modifier::target,&Modifier::delta}; };
template<> struct Fields<AttributeState> { static constexpr auto members=std::tuple{&AttributeState::base,&AttributeState::value}; };
template<> struct Fields<ResourceState> { static constexpr auto members=std::tuple{&ResourceState::baseMaximum,&ResourceState::maximum,&ResourceState::current}; };
template<> struct Fields<StatusState> { static constexpr auto members=std::tuple{&StatusState::definition,&StatusState::stacks,&StatusState::remaining,&StatusState::skipNext,&StatusState::modifiers}; };
template<> struct Fields<CharacterState> { static constexpr auto members=std::tuple{&CharacterState::definition,&CharacterState::attributes,&CharacterState::resources,&CharacterState::modifiers,&CharacterState::experience,&CharacterState::level,&CharacterState::unlocks,&CharacterState::equipmentModifiers,&CharacterState::statuses}; };
template<> struct Fields<ItemDefinition> { static constexpr auto members=std::tuple{&ItemDefinition::id,&ItemDefinition::maximumStack,&ItemDefinition::slots,&ItemDefinition::modifiers}; };
template<> struct Fields<InventoryDefinition> { static constexpr auto members=std::tuple{&InventoryDefinition::id,&InventoryDefinition::capacity,&InventoryDefinition::slots}; };
template<> struct Fields<InventoryState> { static constexpr auto members=std::tuple{&InventoryState::definition,&InventoryState::character}; };
template<> struct Fields<ItemState> { static constexpr auto members=std::tuple{&ItemState::definition,&ItemState::quantity,&ItemState::inventory,&ItemState::slot}; };
template<> struct Fields<ResourceAmount> { static constexpr auto members=std::tuple{&ResourceAmount::resource,&ResourceAmount::amount}; };
template<> struct Fields<StatusDefinition> { static constexpr auto members=std::tuple{&StatusDefinition::id,&StatusDefinition::phase,&StatusDefinition::duration,&StatusDefinition::maximumStacks,&StatusDefinition::stacking,&StatusDefinition::refreshOnStack,&StatusDefinition::includeApplicationPhase,&StatusDefinition::modifiers,&StatusDefinition::tickEffects}; };
template<> struct Fields<TargetRequirement> { static constexpr auto members=std::tuple{&TargetRequirement::minimum,&TargetRequirement::maximum,&TargetRequirement::allowSelf,&TargetRequirement::allowOthers}; };
template<> struct Fields<ItemCost> { static constexpr auto members=std::tuple{&ItemCost::definition,&ItemCost::quantity}; };
template<> struct Fields<AbilityDefinition> { static constexpr auto members=std::tuple{&AbilityDefinition::id,&AbilityDefinition::handler,&AbilityDefinition::schemaVersion,&AbilityDefinition::parameters,&AbilityDefinition::costs,&AbilityDefinition::targets,&AbilityDefinition::itemCost}; };
template<> struct Fields<TurnAllowance> { static constexpr auto members=std::tuple{&TurnAllowance::ap,&TurnAllowance::movement}; };
template<> struct Fields<InitiativeEntry> { static constexpr auto members=std::tuple{&InitiativeEntry::entity,&InitiativeEntry::score}; };
template<> struct Fields<EncounterState> { static constexpr auto members=std::tuple{&EncounterState::policy,&EncounterState::epoch,&EncounterState::round,&EncounterState::turnEpoch,&EncounterState::roster,&EncounterState::order,&EncounterState::cursor,&EncounterState::active,&EncounterState::allowance,&EncounterState::stage,&EncounterState::outcome}; };
template<> struct Fields<CooldownState> { static constexpr auto members=std::tuple{&CooldownState::phase,&CooldownState::remaining}; };
template<> struct Fields<TacticalPosition> { static constexpr auto members=std::tuple{&TacticalPosition::x,&TacticalPosition::y,&TacticalPosition::z}; };
template<> struct Fields<RandomState> { static constexpr auto members=std::tuple{&RandomState::version,&RandomState::state}; };
template<> struct Fields<AwardId> { static constexpr auto members=std::tuple{&AwardId::source,&AwardId::receipt}; };
template<> struct Fields<Definition> { static constexpr auto members=std::tuple{&Definition::id,&Definition::handler,&Definition::handlerVersion}; };
template<> struct Fields<CatalogDefinition> { static constexpr auto members=std::tuple{&CatalogDefinition::id,&CatalogDefinition::revision,&CatalogDefinition::definitions,&CatalogDefinition::characters,&CatalogDefinition::progressions,&CatalogDefinition::items,&CatalogDefinition::inventories,&CatalogDefinition::abilities,&CatalogDefinition::statuses}; };
template<> struct Codec<State> {
static void put(Writer&w,const State&s){
wire::put(w,s.revision);wire::put(w,s.nextEntity);wire::put(w,s.characters);wire::put(w,s.producers);wire::put(w,s.nextItem);wire::put(w,s.inventories);wire::put(w,s.items);wire::put(w,s.random);wire::put(w,s.phaseEpoch);wire::put(w,s.encounterEpoch);wire::put(w,s.turnEpoch);wire::put(w,s.encounter);wire::put(w,s.positions);wire::put(w,s.actionEpoch);wire::put(w,s.awards);wire::put(w,s.cooldowns);
}
static State get(Reader&r){State s;
s.revision=wire::get<decltype(s.revision)>(r);s.nextEntity=wire::get<decltype(s.nextEntity)>(r);s.characters=wire::get<decltype(s.characters)>(r);s.producers=wire::get<decltype(s.producers)>(r);s.nextItem=wire::get<decltype(s.nextItem)>(r);s.inventories=wire::get<decltype(s.inventories)>(r);s.items=wire::get<decltype(s.items)>(r);s.random=wire::get<decltype(s.random)>(r);s.phaseEpoch=wire::get<decltype(s.phaseEpoch)>(r);s.encounterEpoch=wire::get<decltype(s.encounterEpoch)>(r);s.turnEpoch=wire::get<decltype(s.turnEpoch)>(r);s.encounter=wire::get<decltype(s.encounter)>(r);s.positions=wire::get<decltype(s.positions)>(r);s.actionEpoch=wire::get<decltype(s.actionEpoch)>(r);s.awards=wire::get<decltype(s.awards)>(r);s.cooldowns=wire::get<decltype(s.cooldowns)>(r);
return s;}
};
}

namespace engine::rpg {
std::map<ContentId,std::vector<std::uint32_t>> Bindings::compatibility() const {
    std::map<ContentId,std::vector<std::uint32_t>> m;
    for(auto&[id,h]:handlers_)m.emplace(id,std::vector<std::uint32_t>{1,h.version});
    for(auto&[id,h]:advancement_)m.emplace(id,std::vector<std::uint32_t>{2,h.version});
    for(auto&[id,h]:abilities_)m.emplace(id,std::vector<std::uint32_t>{3,h.version,h.schemaVersion});
    for(auto&[id,h]:encounters_)m.emplace(id,std::vector<std::uint32_t>{4,h.version,bool(h.authorize),bool(h.reactions)});
    return m;
}
std::vector<std::uint8_t> encodeCatalog(const Catalog& c){
    wire::Writer w;wire::put(w,c.definition());wire::put(w,c.bindings()->registry().compatibility());
    const bool builds=!c.definition().equipmentRules.empty()||!c.definition().choiceRules.empty();
    if(builds){wire::put(w,c.definition().equipmentRules);wire::put(w,c.definition().choiceRules);}
    return wire::wrap(1,w.bytes,builds?2:1);
}
std::shared_ptr<const Catalog> decodeCatalog(std::span<const std::uint8_t>b,BindingsHandle bindings){
    wire::need(b.size()>=16,"Truncated catalogue version");wire::Reader header{b.subspan(8,8)};const auto version=header.number();
    wire::need(version==1||version==2,"Unknown catalogue version");
    auto r=wire::unwrap(b,1,version);auto d=wire::get<CatalogDefinition>(r);
    auto manifest=wire::get<std::map<ContentId,std::vector<std::uint32_t>>>(r);
    if(version==2){d.equipmentRules=wire::get<decltype(d.equipmentRules)>(r);d.choiceRules=wire::get<decltype(d.choiceRules)>(r);}
    r.end();
    wire::need(bindings && manifest==bindings->registry().compatibility(),"Handler compatibility mismatch");
    auto c=Catalog::create(std::move(d),std::move(bindings));auto canonical=encodeCatalog(*c);
    wire::need(std::ranges::equal(b,canonical),"Noncanonical catalogue");return c;
}
std::vector<std::uint8_t> encodeSnapshot(const Catalog&c,const Snapshot&s){
    validateSnapshot(c,s);wire::Writer w;wire::blob(w,encodeCatalog(c));wire::put(w,s.session);wire::put(w,s.state);
    const bool choices=!s.state.choices.empty();
    const bool vitality=choices||!s.state.vitality.empty();
    const bool extended=vitality||!s.state.parties.empty()||(s.state.encounter&&!s.state.encounter->teams.empty());
    if(extended){wire::put(w,s.state.parties);wire::put(w,s.state.encounter?s.state.encounter->teams:std::map<EntityId,CombatMember>{});}
    if(vitality)wire::put(w,s.state.vitality);
    if(choices)wire::put(w,s.state.choices);
    return wire::wrap(2,w.bytes,choices?4:(vitality?3:(extended?2:1)));
}
Snapshot decodeSnapshot(const Catalog&c,std::span<const std::uint8_t>b){
    wire::need(b.size()>=16,"Truncated snapshot version");wire::Reader header{b.subspan(8,8)};const auto version=header.number();
    wire::need(version>=1&&version<=snapshotVersion,"Unknown snapshot version");
    auto r=wire::unwrap(b,2,version);auto catalog=wire::blob(r);auto expected=encodeCatalog(c);
    wire::need(std::ranges::equal(catalog,expected),"Catalogue/content/handler mismatch");
    auto session=wire::get<SessionId>(r);auto state=wire::get<State>(r);
    if(version>=2){
        state.parties=wire::get<decltype(state.parties)>(r);
        auto teams=wire::get<std::map<EntityId,CombatMember>>(r);
        wire::need(state.encounter||teams.empty(),"Teams without encounter");
        if(state.encounter)state.encounter->teams=std::move(teams);
        if(version==2)wire::need(!state.parties.empty()||(state.encounter&&!state.encounter->teams.empty()),"Empty v2 extension is noncanonical");
    }
    if(version>=3){state.vitality=wire::get<decltype(state.vitality)>(r);if(version==3)wire::need(!state.vitality.empty(),"Empty v3 extension is noncanonical");}
    if(version==4){state.choices=wire::get<decltype(state.choices)>(r);wire::need(!state.choices.empty(),"Empty v4 extension is noncanonical");}
    r.end();
    Snapshot s{session,std::move(state),std::vector<std::uint8_t>(catalog.begin(),catalog.end())};validateSnapshot(c,s);return s;
}
} // namespace engine::rpg
