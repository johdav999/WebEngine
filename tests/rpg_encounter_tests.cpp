#include "../examples/rpg/encounters.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace r = engine::rpg;
namespace {
int checks = 0;
r::ContentId id(const char* s) { return r::ContentId(s); }
void check(bool b,const char* s) { ++checks; if (!b) throw std::runtime_error(s); }
template<class F> void rejects(r::ErrorCode code,F fn) {
    try { fn(); } catch(const r::Error& e) { check(e.code()==code,e.what()); return; }
    throw std::runtime_error("Expected rejection");
}
r::Transaction prepare(r::Runtime& run,std::vector<r::Operation> ops,r::Budget budget={}) {
    const auto it=run.state().producers.find(id("test.input"));
    return run.prepare(run.command(id("test.input"),it==run.state().producers.end()?1:it->second+1,std::move(ops)),budget);
}
r::Results apply(r::Runtime& run,std::vector<r::Operation> ops) {
    auto tx=prepare(run,std::move(ops));auto result=tx.results();check(run.commit(std::move(tx)),"commit");return result;
}
void reject(r::Runtime& run,r::ErrorCode code,std::vector<r::Operation> ops,r::Budget budget={}) {
    // Live State identity must remain unchanged, preserving every nested field, RNG and watermarks.
    const auto* before=&run.state();rejects(code,[&]{prepare(run,std::move(ops),budget);});
    check(before==&run.state(),"rejected candidate was published");
}
r::EntityId create(r::Runtime& run) { return apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0]; }
r::BeginEncounter begin(std::vector<r::EntityId> actors) { return {{id("example.turns"),1},std::move(actors)}; }
r::EndTurn end(const r::Runtime& run) { const auto& e=*run.state().encounter;return {*e.active,e.epoch,e.turnEpoch}; }
r::AdjustResource delta(r::EntityId actor,std::int64_t value) { return {actor,id("example.reserve"),{value,id("example.point"),1}}; }
void lifecycle() {
    r::Runtime run(example::encounterCatalog(),r::SessionId(1),42);
    const auto a=create(run), b=create(run), c=create(run);
    reject(run,r::ErrorCode::Limit,{begin({})});
    reject(run,r::ErrorCode::InvalidEncounter,{begin({a,a})});
    reject(run,r::ErrorCode::MissingEntity,{begin({{r::SessionId(2),a.value}})});
    auto missing=begin({a});missing.policy.id=id("test.absent");reject(run,r::ErrorCode::MissingHandler,{missing});
    missing=begin({a});missing.policy.version=2;reject(run,r::ErrorCode::HandlerVersionMismatch,{missing});
    apply(run,{begin({c,b,a})});
    check(run.state().encounter->active==a,"stable equal initiative");
    check(run.state().encounter->allowance.ap==2 && run.state().encounter->allowance.movement==6,"allowances");
    auto stale=end(run); const auto* idle=&run.state();
    for(int i=0;i<100;++i) {check(run.state().encounter->turnEpoch==1,"idle turn");check(&run.state()==idle,"idle state");}
    reject(run,r::ErrorCode::InvalidEncounter,{begin({a})});
    reject(run,r::ErrorCode::InvalidEncounter,{r::AdvancePhase{r::TurnPhase::OwnerEnd,a,99}});
    reject(run,r::ErrorCode::InvalidEncounter,{r::ExitEncounter{1,1}});
    auto future=stale;future.entity=b;reject(run,r::ErrorCode::InvalidEncounter,{future});
    apply(run,{delta(b,-20)}); // Future defeated participant keeps position in order but is skipped.
    apply(run,{end(run)}); check(run.state().encounter->active==c,"skip future defeated actor");
    reject(run,r::ErrorCode::StaleTurn,{stale});
    auto same=end(run);reject(run,r::ErrorCode::StaleTurn,{same,same});
    // Score mutation does not reorder the current round; next round rebuilds scores.
    apply(run,{r::SetAttributeBase{c,id("example.insight"),{99,id("example.point"),1}}});
    apply(run,{end(run)}); check(run.state().encounter->round==2 && run.state().encounter->active==c,"next round score refresh");
    apply(run,{delta(c,-20)});check(run.state().encounter->active==a,"defeated active automatically closed");
    apply(run,{delta(a,-20)});check(run.state().encounter->outcome==r::EncounterOutcome::Stalemate,"all ineligible terminates");
    reject(run,r::ErrorCode::InvalidEncounter,{r::FinishEncounter{1,run.state().turnEpoch}});
    apply(run,{r::ExitEncounter{1,run.state().turnEpoch}});check(!run.state().encounter,"exit");
    apply(run,{delta(a,20),begin({a})});check(run.state().encounter->epoch==2,"encounter epoch never reused");
    reject(run,r::ErrorCode::StaleTurn,{stale});
    apply(run,{r::FinishEncounter{2,run.state().turnEpoch}});
    check(run.state().encounter->outcome==r::EncounterOutcome::Escape,"policy escape");
    apply(run,{r::ExitEncounter{2,run.state().turnEpoch}});
    r::Runtime none(example::encounterCatalog(),r::SessionId(2));auto n=create(none);
    apply(none,{delta(n,-20),begin({n})});check(none.state().encounter->stage==r::EncounterStage::Finished,"all-ineligible entry");
}
void phasesAndFailures() {
    auto defs=example::abilityDefinitions();
    r::StatusDefinition start{id("test.start")};start.phase=r::TurnPhase::OwnerStart;
    start.tickEffects.push_back({id("example.reserve"),{-20,id("example.point"),1}});defs.statuses.push_back(start);
    r::Runtime run(example::encounterCatalog(example::encounterRules(),defs),r::SessionId(1));
    auto a=create(run),b=create(run),outside=create(run);
    apply(run,{r::ApplyStatus{a,id("test.start"),id("test.source")},
        r::ApplyStatus{outside,id("example.mark"),id("test.mark")},
        r::SetCooldown{a,id("test.skill"),r::TurnPhase::OwnerStart,2},begin({a,b})});
    check(run.state().encounter->active==b,"start phase incapacitation");
    check(run.state().cooldowns.at(a).at(id("test.skill")).remaining==1,"cooldown start before status");
    apply(run,{r::ApplyStatus{b,id("example.focused"),id("test.focus")},
        r::SetCooldown{b,id("test.skill"),r::TurnPhase::OwnerEnd,1}});
    apply(run,{end(run)});
    check(!run.state().cooldowns.contains(b),"cooldown expiry");
    check(run.state().characters.at(b).resources.at(id("example.reserve")).current==19,"owner end tick");
    check(run.state().characters.at(outside).statuses.size()==1,"round phase excludes nonparticipants");
    r::Budget budget;budget.encounterWork=0;reject(run,r::ErrorCode::BudgetExceeded,{end(run)},budget);
    budget={};budget.effects=0;reject(run,r::ErrorCode::BudgetExceeded,{end(run)},budget);
    budget={};budget.encounterWork=r::Limits::encounterWork+1;reject(run,r::ErrorCode::Limit,{end(run)},budget);
    // R6 requires the tactical wrapper for combat abilities; raw operations cannot bypass it.
    reject(run,r::ErrorCode::InvalidAction,{r::UseAbility{b,id("example.strike"),{b},{}},delta(b,-100)});
    // OwnerEnd failure after a successful RNG action also rolls back the entire command.
    apply(run,{r::ApplyStatus{b,id("example.focused"),id("test.second")}});
    reject(run,r::ErrorCode::InvalidAmount,{delta(b,-19)});
    // Multiple same-key cooldown updates replace, zero clears, invalid phase rejects.
    apply(run,{r::SetCooldown{b,id("test.replace"),r::TurnPhase::RoundEnd,3},r::SetCooldown{b,id("test.replace"),r::TurnPhase::RoundEnd,0}});
    reject(run,r::ErrorCode::InvalidStatus,{r::SetCooldown{b,id("test.bad"),static_cast<r::TurnPhase>(99),1}});
    reject(run,r::ErrorCode::Limit,{r::SetCooldown{b,id("test.bad"),r::TurnPhase::OwnerEnd,65536}});
}
void policies() {
    r::Bindings bindings;bindings.addEncounterHandler(id("test.rules"),example::encounterRules());
    rejects(r::ErrorCode::DuplicateHandler,[&]{bindings.addAbilityHandler(id("test.rules"),{1,1,example::validateAbility,example::effects});});
    rejects(r::ErrorCode::DuplicateHandler,[&]{bindings.addDefinitionHandler(id("test.rules"),{1,[](const auto&){}});});
    rejects(r::ErrorCode::DuplicateHandler,[&]{bindings.addAdvancementHandler(id("test.rules"),{1,example::unlocks});});
    auto bad=example::encounterRules();bad.allowance={};rejects(r::ErrorCode::MissingValidator,[&]{bindings.addEncounterHandler(id("test.bad"),bad);});
    for(auto outcome:{r::EncounterOutcome::Victory,r::EncounterOutcome::Defeat}) {
        auto rules=example::encounterRules();rules.decide=[outcome](const auto&,auto){return r::EncounterDecision{true,outcome};};
        r::Runtime run(example::encounterCatalog(rules),r::SessionId(1));auto a=create(run);
        apply(run,{begin({a})});check(run.state().encounter->outcome==outcome && run.state().phaseEpoch==0,"entry outcome before phases");
    }
    bad=example::encounterRules();bad.allowance=[](const auto&,auto){return r::TurnAllowance{-1,0};};
    r::Runtime invalid(example::encounterCatalog(bad),r::SessionId(1));auto a=create(invalid);
    reject(invalid,r::ErrorCode::InvalidAmount,{begin({a})});
    bad=example::encounterRules();bad.decide=[](const auto&,auto){return r::EncounterDecision{false};};
    r::Runtime denied(example::encounterCatalog(bad),r::SessionId(1));a=create(denied);reject(denied,r::ErrorCode::RejectedEncounter,{begin({a})});
    // Even pathological alternating eligibility cannot loop past a caller-selected budget.
    // Pure eligibility based on transient phase epoch makes every start incapacitate its actor.
    bad=example::encounterRules();bad.eligible=[](const r::EncounterContext& c,auto){return !c.encounter.active.has_value();};
    r::Runtime cyclic(example::encounterCatalog(bad),r::SessionId(1));a=create(cyclic);
    r::Budget budget;budget.encounterWork=40;reject(cyclic,r::ErrorCode::BudgetExceeded,{begin({a})},budget);
    // Exception and reentry preserve the original live state.
    r::Runtime* owner=nullptr;bad=example::encounterRules();bad.initiative=[&](const auto&,auto)->std::int64_t {owner->invalidateCommands();return 0;};
    r::Runtime reentrant(example::encounterCatalog(bad),r::SessionId(1));owner=&reentrant;a=create(reentrant);
    reject(reentrant,r::ErrorCode::Reentrant,{begin({a})});
}
void boundsAndPolicyPhases() {
    auto rules=example::encounterRules();
    auto scores=std::make_shared<int>(0), allowances=std::make_shared<int>(0);
    rules.initiative=[scores](const auto&,auto){++*scores;return 0;};
    rules.allowance=[allowances](const auto&,auto){++*allowances;return r::TurnAllowance{};};
    r::Runtime run(example::encounterCatalog(rules),r::SessionId(1));
    std::vector<r::EntityId> roster;
    for(int i=0;i<65;++i)roster.push_back(create(run));
    reject(run,r::ErrorCode::Limit,{begin(roster)});roster.pop_back();
    apply(run,{begin(roster)});check(*scores==64 && *allowances==1,"one score per participant, one allowance per start");
    for(int i=0;i<64;++i)apply(run,{end(run)});
    check(*scores==128 && *allowances==65 && run.state().encounter->round==2,"one initiative build per round");
    check(run.state().encounter->allowance.ap==0,"zero allowance is valid");
    for(int i=0;i<64;++i) apply(run,{r::SetCooldown{roster[0],r::ContentId("test.cd"+std::to_string(i)),r::TurnPhase::OwnerEnd,2}});
    reject(run,r::ErrorCode::Limit,{r::SetCooldown{roster[0],id("test.too-many"),r::TurnPhase::OwnerEnd,1}});
    // Terminal outcome after an action skips all additional end/round phases.
    rules=example::encounterRules();
    rules.decide=[](const r::EncounterContext& c,r::EncounterCheck check) {
        if(check==r::EncounterCheck::Exit)return r::EncounterDecision{false,c.encounter.outcome};
        if(check==r::EncounterCheck::Finish)return r::EncounterDecision{false};
        for(auto id:c.encounter.roster)
            if(c.state.characters.at(id).resources.at(r::ContentId("example.reserve")).current==0)
                return r::EncounterDecision{true,r::EncounterOutcome::Victory};
        return r::EncounterDecision{};
    };
    r::Runtime win(example::encounterCatalog(rules),r::SessionId(2));auto a=create(win),b=create(win);
    apply(win,{begin({a,b})});reject(win,r::ErrorCode::RejectedEncounter,{r::FinishEncounter{1,1}});
    apply(win,{delta(b,-20)});check(win.state().encounter->outcome==r::EncounterOutcome::Victory && win.state().phaseEpoch==2,"post-action victory without extra ticks");
    reject(win,r::ErrorCode::RejectedEncounter,{r::ExitEncounter{1,1}});
    // Begin fails atomically on a start-phase underflow, preserving entry epochs/cooldowns/statuses.
    auto defs=example::abilityDefinitions();defs.statuses[0].phase=r::TurnPhase::OwnerStart;
    defs.statuses[0].tickEffects[0].amount.value=-21;
    r::Runtime fail(example::encounterCatalog(example::encounterRules(),defs),r::SessionId(3));a=create(fail);
    apply(fail,{r::ApplyStatus{a,id("example.focused"),id("test.fail")}});
    reject(fail,r::ErrorCode::InvalidAmount,{begin({a})});
    // Provider exceptions retain the published candidate boundary as well.
    rules=example::encounterRules();rules.initiative=[](const auto&,auto)->std::int64_t{throw std::runtime_error("provider failure");};
    r::Runtime throwing(example::encounterCatalog(rules),r::SessionId(4));a=create(throwing);
    const auto* before=&throwing.state();bool threw=false;
    try{prepare(throwing,{begin({a})});}catch(const std::runtime_error&){threw=true;}
    check(threw && &throwing.state()==before,"provider exception rollback");
}
std::string trace() {
    r::Runtime run(example::encounterCatalog(),r::SessionId(7),42);auto a=create(run),b=create(run);
    std::ostringstream out;
    auto step=[&](std::vector<r::Operation> ops) {
        auto results=apply(run,std::move(ops)); const auto& s=run.state();
        out<<s.revision<<':'<<s.encounterEpoch<<':'<<s.turnEpoch<<':'<<s.phaseEpoch<<':'<<s.random.state<<'|';
        if(s.encounter) {const auto& e=*s.encounter;out<<e.round<<','<<(e.active?e.active->value:0)<<','<<static_cast<int>(e.outcome)<<','<<e.allowance.ap<<','<<e.allowance.movement;}
        out<<'|';for(const auto& e:results.encounters) out<<static_cast<int>(e.event)<<','<<e.round<<','<<e.turnEpoch<<','<<(e.entity?e.entity->value:0)<<';';
        out<<'\n';
    };
    step({begin({b,a})});step({end(run)});step({end(run)});
    step({r::FinishEncounter{1,3}});step({r::ExitEncounter{1,3}});
    return out.str();
}
}
int main() {
    try {
        lifecycle();phasesAndFailures();policies();boundsAndPolicyPhases();
        const std::string expected =
            "3:1:1:2:1753877967969059832|1,1,0,2,6|0,0,0,0;1,1,0,0;2,1,1,1;\n"
            "4:1:2:4:1753877967969059832|1,2,0,2,6|3,1,1,1;2,1,2,2;\n"
            "5:1:3:8:1753877967969059832|2,1,0,2,6|3,1,2,2;1,2,2,0;2,2,3,1;\n"
            "6:1:3:8:1753877967969059832|2,0,3,0,0|5,2,3,0;\n"
            "7:1:3:8:1753877967969059832||6,2,3,0;\n";
        auto actual=trace();if(actual!=expected)std::cerr<<actual;
        check(actual==expected,"Canonical encounter trace differs");
        std::cout<<actual<<"R5 encounter checks: "<<checks<<'\n';return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
