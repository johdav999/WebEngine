#include "../examples/rpg/abilities.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace r = engine::rpg;
namespace {
int checks = 0;
r::ContentId id(const char* value) { return r::ContentId(value); }
r::Amount points(std::int64_t value) { return {value,id("example.point"),1}; }
void check(bool ok, const char* text) { ++checks; if (!ok) throw std::runtime_error(text); }
template<class F> void rejects(r::ErrorCode code, F fn) {
    try { fn(); } catch(const r::Error& e) { check(e.code()==code,e.what()); return; }
    throw std::runtime_error("Expected RPG rejection");
}
// Full relevant R4/R3/character projection: every rollback assertion includes RNG,
// clocks, allocation cursors, watermarks, modifiers, unlocks and inventory ownership.
std::string canonical(const r::State& s) {
    std::ostringstream out;
    out<<s.generation<<':'<<s.revision<<':'<<s.nextEntity<<':'<<s.nextItem<<':'<<s.phaseEpoch<<':'<<s.random.version<<':'<<s.random.state<<'\n';
    for(const auto& [key,seq]:s.producers) out<<key.value()<<':'<<seq<<'\n';
    auto mods=[&](const auto& entries) { for(const auto& m:entries) out<<static_cast<int>(m.kind)<<':'<<m.target.value()<<':'<<m.delta.value<<':'<<m.delta.unit.value()<<':'<<m.delta.scale<<'\n'; };
    for(const auto& [key,c]:s.characters) {
        out<<key.session.value()<<':'<<key.value<<':'<<c.definition.value()<<':'<<c.experience<<':'<<c.level<<'\n';
        for(const auto& [key,a]:c.attributes) out<<key.value()<<':'<<a.base<<':'<<a.value<<'\n';
        for(const auto& [key,v]:c.resources) out<<key.value()<<':'<<v.baseMaximum<<':'<<v.maximum<<':'<<v.current<<'\n';
        for(const auto& [key,m]:c.modifiers) { out<<key.value()<<'\n'; mods(m); }
        for(const auto& [key,m]:c.equipmentModifiers) { out<<key.value<<'\n'; mods(m); }
        for(const auto& [key,v]:c.statuses) { out<<key.value()<<':'<<v.definition.value()<<':'<<v.stacks<<':'<<v.remaining<<':'<<v.skipNext<<'\n'; mods(v.modifiers); }
        for(const auto& key:c.unlocks) out<<key.value()<<'\n';
    }
    for(const auto& [key,v]:s.inventories) out<<key.value<<':'<<v.definition.value()<<':'<<(v.character?v.character->value:0)<<'\n';
    for(const auto& [key,v]:s.items) out<<key.session.value()<<':'<<key.value<<':'<<v.definition.value()<<':'<<v.quantity<<':'<<v.inventory.value<<':'<<(v.slot?v.slot->value():"-")<<'\n';
    return out.str();
}
r::Transaction prepare(r::Runtime& run,std::vector<r::Operation> ops,r::Budget budget={}) {
    auto found=run.state().producers.find(id("test.input"));
    return run.prepare(run.command(id("test.input"),found==run.state().producers.end()?1:found->second+1,std::move(ops)),budget);
}
r::Results apply(r::Runtime& run,std::vector<r::Operation> ops) {
    auto tx=prepare(run,std::move(ops)); auto result=tx.results(); check(run.commit(std::move(tx)),"Commit failed"); return result;
}
void rejected(r::Runtime& run,r::ErrorCode code,std::vector<r::Operation> ops,r::Budget budget={}) {
    const auto before=canonical(run.state()); rejects(code,[&]{prepare(run,std::move(ops),budget);});
    check(before==canonical(run.state()),"Rejection mutated complete live state");
}
r::EntityId create(r::Runtime& run) { return apply(run,{r::CreateCharacter{id("example.scholar")}}).created.at(0); }
r::UseAbility use(r::EntityId caster,r::EntityId target,const char* ability="example.strike") { return {caster,id(ability),{target},{}}; }
std::int64_t reserve(const r::Runtime& run,r::EntityId entity) { return run.state().characters.at(entity).resources.at(id("example.reserve")).current; }
void rng() {
    auto state=r::seedRandom(42); std::size_t budget=20; r::Random random(state,budget);
    for(auto expected:{0xa15c02b7u,0x7b47f409u,0xba1d3330u,0x83d2f293u,0xbfa4784bu,0xcbed606eu})
        check(random.next()==expected,"PCG published vector mismatch");
    check(budget==14,"Raw draw budget");
    const auto bytes=r::encodeRandom(state); auto decoded=r::decodeRandom(bytes);
    const std::array<std::uint8_t,16> expectedBytes{82,78,71,49,1,0,0,0,74,151,219,63,183,208,182,190};
    check(bytes==expectedBytes,"Canonical random byte fixture mismatch");
    check(decoded==state,"Canonical random round trip");
    std::size_t left=20; r::Random continuation(decoded,left);
    for(int i=0;i<10;++i) check(random.next()==continuation.next(),"Random continuation mismatch");
    auto invalid=bytes; invalid[4]=2; rejects(r::ErrorCode::InvalidVersion,[&]{r::decodeRandom(invalid);});
    invalid=bytes; invalid[0]=0; rejects(r::ErrorCode::InvalidDefinition,[&]{r::decodeRandom(invalid);});
    rejects(r::ErrorCode::InvalidDefinition,[&]{r::decodeRandom(std::span(bytes).first(15));});
    state=r::seedRandom(0); budget=4096; r::Random bounded(state,budget);
    check(bounded.uniform(1)==0,"Bound one");
    rejects(r::ErrorCode::InvalidAmount,[&]{bounded.uniform(0);});
    for(int i=0;i<1000;++i) check(bounded.uniform(7)<7,"Bounded sample range");
    // A raw zero is rejected for this bound; exhaustion is latched even if caught.
    state={1,0}; budget=1; r::Random rejection(state,budget);
    rejects(r::ErrorCode::BudgetExceeded,[&]{rejection.uniform(0x80000001u);});
    check(budget==0 && rejection.exhausted(),"Rejection-sampling draw accounting");
    state.version=2; rejects(r::ErrorCode::InvalidVersion,[&]{r::Random invalid(state,budget);});
}
void definitions() {
    auto bad=[](auto def,r::ErrorCode code) { rejects(code,[&]{example::abilityCatalog(std::move(def));}); };
    auto d=example::abilityDefinitions(); d.abilities[0].handler.id=id("test.missing"); bad(d,r::ErrorCode::MissingHandler);
    d=example::abilityDefinitions(); d.abilities[0].handler.version=2; bad(d,r::ErrorCode::HandlerVersionMismatch);
    d=example::abilityDefinitions(); d.abilities[0].schemaVersion=2; bad(d,r::ErrorCode::InvalidVersion);
    d=example::abilityDefinitions(); d.abilities[0].parameters.clear(); bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.abilities[0].parameters.at(id("example.delta")).value=99; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.abilities[0].costs[0].amount.value=-1; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.abilities[0].costs.push_back(d.abilities[0].costs[0]); bad(d,r::ErrorCode::DuplicateDefinition);
    d=example::abilityDefinitions(); d.abilities[0].targets.maximum=65; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.abilities[0].targets={2,1,true,true}; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.abilities[1].itemCost->definition=id("test.missing"); bad(d,r::ErrorCode::MissingDefinition);
    d=example::abilityDefinitions(); d.abilities[1].itemCost->quantity=0; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.statuses[0].duration=0; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.statuses[0].maximumStacks=33; bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.statuses[0].phase=static_cast<r::TurnPhase>(9); bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.statuses[0].modifiers.push_back(d.statuses[0].modifiers[0]); bad(d,r::ErrorCode::InvalidDefinition);
    d=example::abilityDefinitions(); d.statuses[0].id=d.abilities[0].id; bad(d,r::ErrorCode::DuplicateDefinition);
    r::Bindings bindings;
    bindings.addAbilityHandler(id("test.handler"),{1,1,example::validateAbility,example::effects});
    rejects(r::ErrorCode::DuplicateHandler,[&]{bindings.addDefinitionHandler(id("test.handler"),{1,[](const auto&){}});});
    rejects(r::ErrorCode::DuplicateHandler,[&]{bindings.addAdvancementHandler(id("test.handler"),{1,example::unlocks});});
    rejects(r::ErrorCode::DuplicateHandler,[&]{bindings.addAbilityHandler(id("test.handler"),{1,1,example::validateAbility,example::effects});});
    rejects(r::ErrorCode::MissingValidator,[&]{bindings.addAbilityHandler(id("test.empty"),{});});
    rejects(r::ErrorCode::InvalidVersion,[&]{bindings.addAbilityHandler(id("test.empty"),{0,1,example::validateAbility,example::effects});});
}
void actions() {
    r::Runtime run(example::abilityCatalog(),r::SessionId(42),42); auto caster=create(run), target=create(run);
    const auto original=canonical(run.state());
    auto first=prepare(run,{use(caster,target)}); auto second=prepare(run,{use(caster,target)});
    check(canonical(first.state())==canonical(second.state()) && canonical(run.state())==original,"Preview consumed live RNG or produced different outcome");
    check(run.commit(std::move(first)) && !run.commit(std::move(second)),"Competing ability candidates");
    check(reserve(run,caster)==19 && reserve(run,target)==16,"Fixed strike fixture");
    check(run.state().random.state==3118741472915405573ULL,"Fixed RNG state fixture");
    auto pack=apply(run,{r::CreateInventory{id("example.pack"),caster}}).inventories.at(0);
    auto tonic=apply(run,{r::AddItem{pack,id("example.tonic"),2}}).items.at(0);
    auto healing=use(caster,target,"example.heal"); healing.item=tonic;
    apply(run,{healing}); check(reserve(run,target)==19 && run.state().items.at(tonic).quantity==1,"Healing item effect");
    rejected(run,r::ErrorCode::InvalidAmount,{healing}); // over-cap heal must refund item/cost
    auto concentration=use(caster,target,"example.concentrate"); apply(run,{concentration});
    check(run.state().characters.at(target).resources.at(id("example.focus")).current==8,"Non-damage effect");
    auto invalid=use(caster,target); invalid.targets={}; rejected(run,r::ErrorCode::InvalidTarget,{invalid});
    invalid.targets={target,target}; rejected(run,r::ErrorCode::InvalidTarget,{invalid});
    invalid.targets={{r::SessionId(1),1}}; rejected(run,r::ErrorCode::MissingEntity,{invalid});
    invalid=use(caster,target); invalid.item=tonic; rejected(run,r::ErrorCode::InvalidOwnership,{invalid});
    auto wrong=use(target,caster,"example.heal"); wrong.item=tonic; rejected(run,r::ErrorCode::InvalidOwnership,{wrong});
    rejected(run,r::ErrorCode::BudgetExceeded,{use(caster,target)},r::Budget{256,256,256,0});
    rejected(run,r::ErrorCode::BudgetExceeded,{use(caster,target)},r::Budget{256,256,1,4096});
    apply(run,{r::AdjustResource{caster,id("example.reserve"),points(-reserve(run,caster))}});
    rejected(run,r::ErrorCode::InvalidAmount,{use(caster,target)});
}
void evaluatorFailures() {
    auto exercise=[](auto evaluator,r::ErrorCode code) {
        r::Runtime run(example::abilityCatalog(example::abilityDefinitions(),evaluator),r::SessionId(1),42);
        auto a=create(run),b=create(run); rejected(run,code,{use(a,b)});
    };
    exercise([](const auto&,auto& random){random.next();return r::EffectProposal{false,{}};},r::ErrorCode::RejectedEffect);
    exercise([](const auto& ctx,auto& random){random.next();return r::EffectProposal{true,{
        r::ResourceEffect{ctx.targets[0],id("example.reserve"),points(-2)},
        r::ResourceEffect{ctx.entity,id("example.reserve"),points(std::numeric_limits<std::int64_t>::max())}}};},r::ErrorCode::Overflow);
    exercise([](const auto& ctx,auto&){return r::EffectProposal{true,{r::ResourceEffect{{ctx.entity.session,999},id("example.reserve"),points(-1)}}};},r::ErrorCode::InvalidTarget);
    exercise([](const auto& ctx,auto&){return r::EffectProposal{true,{
        r::ApplyStatus{ctx.entity,id("example.ward"),id("test.source")},
        r::ApplyStatus{ctx.entity,id("example.ward"),id("test.source")}}};},r::ErrorCode::InvalidStatus);
    exercise([](const auto& ctx,auto&){return r::EffectProposal{true,{r::ApplyStatus{ctx.entity,id("test.unknown"),id("test.source")}}};},r::ErrorCode::MissingDefinition);
    exercise([](const auto& ctx,auto&){return r::EffectProposal{true,{r::ResourceEffect{ctx.entity,id("example.reserve"),{1,id("test.wrong"),1}}}};},r::ErrorCode::InvalidAmount);
    exercise([](const auto&,auto& random){try {for(int i=0;i<4097;++i)random.next();}catch(const r::Error&){}return r::EffectProposal{};},r::ErrorCode::BudgetExceeded);
    exercise([](const auto& ctx,auto&){r::EffectProposal p;for(int i=0;i<257;++i)p.effects.push_back(r::ResourceEffect{ctx.entity,id("example.reserve"),points(0)});return p;},r::ErrorCode::Limit);
    r::Runtime* owner=nullptr; bool reenter=true;
    auto catalog=example::abilityCatalog(example::abilityDefinitions(),[&](const auto&,auto& random)->r::EffectProposal {
        random.next(); if(reenter)owner->invalidateCommands(); throw std::runtime_error("Provider error");
    });
    r::Runtime run(catalog,r::SessionId(1));owner=&run;auto a=create(run);
    rejected(run,r::ErrorCode::Reentrant,{use(a,a)});
    reenter=false;auto before=canonical(run.state());
    try {prepare(run,{use(a,a)});throw std::logic_error("Provider did not throw");}
    catch(const std::runtime_error& e) {check(std::string(e.what())=="Provider error","Exception propagation");}
    check(canonical(run.state())==before,"Provider failure changed state");
    apply(run,{r::AdjustResource{a,id("example.reserve"),points(-1)}}); // guard released
    auto lifetime=std::make_shared<int>(1);std::weak_ptr<int> weak=lifetime;
    auto retained=example::abilityCatalog(example::abilityDefinitions(),example::effects,lifetime);lifetime.reset();
    {r::Runtime kept(retained,r::SessionId(7));retained.reset();auto c=create(kept);apply(kept,{use(c,c)});check(!weak.expired(),"Provider lifetime lost");}
    check(weak.expired(),"Provider lifetime leaked");
}
void statuses() {
    r::Runtime run(example::abilityCatalog(),r::SessionId(4));auto a=create(run),b=create(run);
    r::ApplyStatus focused{a,id("example.focused"),id("test.focus")};
    apply(run,{focused,focused});
    check(run.state().characters.at(a).attributes.at(id("example.insight")).value==14,"Stack modifiers");
    apply(run,{r::AdvancePhase{r::TurnPhase::OwnerEnd,b,1}});check(reserve(run,a)==20,"Wrong owner's phase ticked status");
    apply(run,{r::AdvancePhase{r::TurnPhase::OwnerStart,a,2}});check(reserve(run,a)==20,"Wrong phase ticked status");
    apply(run,{r::AdvancePhase{r::TurnPhase::OwnerEnd,a,3}});check(reserve(run,a)==18,"Stacked tick effect");
    apply(run,{focused});check(run.state().characters.at(a).statuses.at(id("test.focus")).remaining==1,"Stack unexpectedly refreshed duration");
    rejected(run,r::ErrorCode::Limit,{focused});
    rejected(run,r::ErrorCode::StalePhase,{r::AdvancePhase{r::TurnPhase::OwnerEnd,a,3}});
    rejected(run,r::ErrorCode::BudgetExceeded,{r::AdvancePhase{r::TurnPhase::OwnerEnd,a,4}},r::Budget{256,256,2,4096});
    apply(run,{r::AdvancePhase{r::TurnPhase::OwnerEnd,a,4}});
    check(reserve(run,a)==15 && run.state().characters.at(a).statuses.empty(),"Expiry/ticks");
    check(run.state().characters.at(a).attributes.at(id("example.insight")).value==10,"Expired modifiers remained");
    r::ApplyStatus ward{a,id("example.ward"),id("test.ward")};apply(run,{ward});
    apply(run,{r::AdvancePhase{r::TurnPhase::OwnerEnd,a,5}});
    check(run.state().characters.at(a).statuses.at(id("test.ward")).remaining==1,"Excluded application phase decremented");
    apply(run,{ward});check(run.state().characters.at(a).statuses.at(id("test.ward")).skipNext,"Refresh did not reset phase inclusion");
    rejected(run,r::ErrorCode::InvalidStatus,{r::ApplyStatus{a,id("example.focused"),id("test.ward")}});
    apply(run,{r::DispelStatus{a,id("test.ward")}});
    rejected(run,r::ErrorCode::InvalidStatus,{r::DispelStatus{a,id("test.ward")}});
    r::ApplyStatus mark{a,id("example.mark"),id("test.mark")};apply(run,{mark});
    rejected(run,r::ErrorCode::InvalidStatus,{mark});
    rejected(run,r::ErrorCode::InvalidTarget,{r::AdvancePhase{r::TurnPhase::RoundEnd,a,6}});
    apply(run,{r::AdvancePhase{r::TurnPhase::RoundEnd,{},6}});check(run.state().characters.at(a).statuses.empty(),"Round expiry");
    apply(run,{focused,r::AdjustResource{a,id("example.reserve"),points(-reserve(run,a))}});
    rejected(run,r::ErrorCode::InvalidAmount,{r::AdvancePhase{r::TurnPhase::OwnerEnd,a,7}});
    // Status modifiers cannot bypass the combined limit through ordinary sources.
    apply(run,{r::DispelStatus{a,id("test.focus")}});
    std::vector<r::Operation> batch;
    for(int i=0;i<128;++i) batch.push_back(r::ApplyStatus{a,id("example.mark"),r::ContentId("test.s"+std::to_string(i))});
    apply(run,std::move(batch));
    rejected(run,r::ErrorCode::Limit,{r::ApplyStatus{a,id("example.mark"),id("test.overflow")}});
    apply(run,{r::AdvancePhase{r::TurnPhase::RoundEnd,{},7}});
    check(run.state().characters.at(a).statuses.empty(),"Bounded bulk expiry");
}
void statusPoliciesAndLimits() {
    for(auto phase:{r::TurnPhase::OwnerStart,r::TurnPhase::OwnerEnd,r::TurnPhase::RoundStart,r::TurnPhase::RoundEnd}) {
        auto d=example::abilityDefinitions(); d.statuses[0].phase=phase; d.statuses[0].refreshOnStack=true;
        r::Runtime run(example::abilityCatalog(d),r::SessionId(2));auto a=create(run);
        const auto owner=(phase==r::TurnPhase::OwnerStart || phase==r::TurnPhase::OwnerEnd)?std::optional(a):std::nullopt;
        r::ApplyStatus s{a,id("example.focused"),id("test.source")};
        apply(run,{s,r::AdvancePhase{phase,owner,1},s});
        check(run.state().characters.at(a).statuses.at(id("test.source")).remaining==2,"Stack refresh duration");
        apply(run,{r::AdvancePhase{phase,owner,2},r::AdvancePhase{phase,owner,3}});
        check(run.state().characters.at(a).statuses.empty() && reserve(run,a)==15,"Four-phase duration fixture");
    }
    auto d=example::abilityDefinitions(); d.statuses[0].modifiers[0].delta=points(std::numeric_limits<std::int64_t>::max());
    r::Runtime overflow(example::abilityCatalog(d),r::SessionId(3));auto a=create(overflow);
    rejected(overflow,r::ErrorCode::Overflow,{r::ApplyStatus{a,id("example.focused"),id("test.source")}});
    d=example::abilityDefinitions();d.statuses[0].tickEffects[0].amount.unit=id("test.wrong");
    r::Runtime units(example::abilityCatalog(d),r::SessionId(4));a=create(units);
    rejected(units,r::ErrorCode::InvalidAmount,{r::ApplyStatus{a,id("example.focused"),id("test.source")}});
    d=example::abilityDefinitions();d.characters[0].attributes.push_back({id("example.other"),id("example.point"),1,0,100,0});
    r::Runtime combined(example::abilityCatalog(d),r::SessionId(5));a=create(combined);
    std::vector<r::Modifier> mods{
        {r::ModifierTarget::Attribute,id("example.insight"),points(0)},
        {r::ModifierTarget::Attribute,id("example.other"),points(0)},
        {r::ModifierTarget::ResourceMaximum,id("example.reserve"),points(0)},
        {r::ModifierTarget::ResourceMaximum,id("example.focus"),points(0)}};
    std::vector<r::Operation> ops{r::ApplyStatus{a,id("example.focused"),id("test.status")}};
    for(int i=0;i<63;++i) ops.push_back(r::SetModifiers{a,r::ContentId("test.m"+std::to_string(i)),mods});
    auto three=mods;three.pop_back();ops.push_back(r::SetModifiers{a,id("test.final"),three});apply(combined,std::move(ops));
    rejected(combined,r::ErrorCode::Limit,{r::SetModifiers{a,id("test.final"),mods}});
    auto pack=apply(combined,{r::CreateInventory{id("example.pack"),a}}).inventories.at(0);
    auto blade=apply(combined,{r::AddItem{pack,id("example.blade"),1}}).items.at(0);
    rejected(combined,r::ErrorCode::Limit,{r::EquipItem{pack,blade,id("example.hand")}});
    rejected(combined,r::ErrorCode::Limit,{r::ApplyStatus{a,id("example.focused"),id("test.extra")}});
    // Global limit and a phase whose work exceeds budget must remain atomic.
    r::Runtime bulk(example::abilityCatalog(),r::SessionId(6));ops.clear();
    for(int i=0;i<129;++i)ops.push_back(r::CreateCharacter{id("example.scholar")});
    auto characters=apply(bulk,std::move(ops)).created;
    for(int i=0;i<128;++i){ops.clear();for(int j=0;j<128;++j)ops.push_back(r::ApplyStatus{characters[i],id("example.mark"),r::ContentId("test.s"+std::to_string(j))});apply(bulk,std::move(ops));}
    rejected(bulk,r::ErrorCode::Limit,{r::ApplyStatus{characters[128],id("example.mark"),id("test.extra")}});
    rejected(bulk,r::ErrorCode::BudgetExceeded,{r::AdvancePhase{r::TurnPhase::RoundEnd,{},1}});
}
void targetingAndStatusProposals() {
    auto d=example::abilityDefinitions();d.abilities[0].targets={1,2,false,true};
    r::Runtime run(example::abilityCatalog(d,[](const auto& ctx,auto& random) {
        random.next();
        check(ctx.targets.size()==2 && ctx.targets[0]<ctx.targets[1],"Targets must be canonical before evaluation");
        return r::EffectProposal{true,{r::ApplyStatus{ctx.targets[0],id("example.focused"),id("test.effect")},
            r::ApplyStatus{ctx.targets[1],id("example.ward"),id("test.effect")}}};
    }),r::SessionId(8),42);
    auto a=create(run),b=create(run),c=create(run);
    auto action=use(a,b);action.targets={b,b};rejected(run,r::ErrorCode::InvalidTarget,{action});
    action.targets={a,b};rejected(run,r::ErrorCode::InvalidTarget,{action});
    action.targets={c,b};apply(run,{action});
    check(run.state().characters.at(b).statuses.contains(id("test.effect")) &&
        run.state().characters.at(c).statuses.contains(id("test.effect")),"Ability status effects not published");
    check(reserve(run,a)==19 && run.state().random.state==3118741472915405573ULL,"Cost/RNG status fixture");
    // New command with an already-used sequence must not replay costs or draws.
    auto before=canonical(run.state());
    rejects(r::ErrorCode::InvalidSequence,[&]{run.prepare(run.command(id("test.input"),4,{action}));});
    check(canonical(run.state())==before,"Duplicate sequence changed effects");
}
}
int main(){try{rng();definitions();actions();evaluatorFailures();statuses();statusPoliciesAndLimits();targetingAndStatusProposals();std::cout<<checks<<" R4 checks passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
