#include "../examples/rpg/tactics.hpp"
#include "engine/rpg/combat_ai.hpp"
#include <iostream>
#include <stdexcept>

namespace r=engine::rpg;
namespace {
int checks=0;const r::WorldStamp world{10,1};
r::ContentId id(const char*s){return r::ContentId(s);}
void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F>void rejects(r::ErrorCode code,F fn){try{fn();}catch(const r::Error&e){check(e.code()==code,e.what());return;}throw std::runtime_error("Expected rejection");}
r::Results apply(r::Runtime& run,std::vector<r::Operation> ops){auto it=run.state().producers.find(id("test.setup"));auto t=run.prepare(run.command(id("test.setup"),it==run.state().producers.end()?1:it->second+1,std::move(ops)));auto out=t.results();check(run.commit(std::move(t)),"setup commit");return out;}
struct Fixture {
    r::Runtime run;
    std::vector<r::EntityId> actors;
    explicit Fixture(r::EncounterHandler policy=example::tacticalRules()):run(example::encounterCatalog(std::move(policy)),r::SessionId(1),42){for(int i=0;i<2;++i){auto a=apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0];actors.push_back(a);apply(run,{r::SetTacticalPosition{a,{i*5,0,0}},r::ConfigureVitality{a,id("example.reserve")}});}apply(run,{r::BeginEncounter{{id("example.turns"),1},actors}});}
    r::SpatialSnapshot spatial(){r::SpatialSnapshot s{world};for(auto a:actors){s.entities.push_back({a,run.state().positions.at(a),true});s.legalTargets.push_back(a);}return s;}
    r::CombatOption strike(const char* key="test.strike",const char* ability="example.strike"){
        const auto&e=*run.state().encounter;auto a=*e.active;return {id(key),{a,e.epoch,e.turnEpoch,r::UseAbility{a,id(ability),{actors[1]},{}},run.spatialQuote(a,spatial())}};
    }
    r::CombatOption move(bool reachable=true){const auto&e=*run.state().encounter;auto a=*e.active;auto p=run.state().positions.at(a);auto q=p;++q.z;return {id("test.move"),{a,e.epoch,e.turnEpoch,r::MoveAction{},run.spatialQuote(a,spatial(),r::PathQuote{p,q,1,{p,q},reachable,true,true})}};}
    r::CombatPlan plan(std::vector<r::CombatOption> options,r::CombatScorer score=[](const auto&)->std::optional<std::int64_t>{return 1;},r::CombatAiBudget budget={},std::stop_token stop={}){return run.planCombat(id("test.ai"),1,*run.state().encounter->active,options,score,budget,world,stop);}
    void ack(){auto p=*run.state().presentation;apply(run,{r::AcknowledgePresentation{p.actionEpoch,p.encounterEpoch,p.turnEpoch}});}
};
void decisions(){
    Fixture f;auto before=f.run.save();auto random=f.run.state().random;
    auto a=f.strike("test.a"),z=f.strike("test.z");auto move=f.move();
    auto plan=f.plan({z,move,a});check(plan.selected()==id("test.a")&&plan.evaluations().size()==3,"Stable ID tie break");
    check(f.run.save()==before&&f.run.state().random==random,"Planning mutated state/RNG/watermarks");
    auto reordered=f.plan({a,z,move});check(reordered.selected()==plan.selected(),"Enumeration-order dependent");
    auto choice=f.plan({a,move},[](const r::CombatScoreContext& c)->std::optional<std::int64_t>{return std::holds_alternative<r::MoveAction>(c.option.action.action)?20:10;});
    check(choice.selected()==id("test.move"),"Policy score ignored");
    auto declined=f.plan({a},[](const auto&)->std::optional<std::int64_t>{return {};});check(declined.endTurn(),"Declined legal action did not fall back");
    auto tx=f.run.prepareCombat(plan,world);auto preparedRandom=tx.state().random;
    check(preparedRandom!=random&&tx.state().encounter->allowance.ap==1,"Actual action preview missing");
    check(!f.run.commit(std::move(tx),r::WorldStamp{10,2}),"Stale world published");
    check(f.run.commit(std::move(tx),world)&&f.run.state().random==preparedRandom,"AI commit differs from normal action");
    check(!f.run.commit(std::move(tx),world),"Duplicate publication");
    rejects(r::ErrorCode::PresentationPending,[&]{f.plan({});});f.ack();
    rejects(r::ErrorCode::StaleCommand,[&]{f.run.prepareCombat(plan,world);});
    // A fresh producer allows another plan; the strike's cooldown forces fallback.
    auto blocked=f.strike();auto fallback=f.run.planCombat(id("test.next"),1,*f.run.state().encounter->active,std::span(&blocked,1),[](const auto&)->std::optional<std::int64_t>{return 100;},{},world);
    check(fallback.endTurn()&&fallback.evaluations()[0].rejection==r::ErrorCode::InvalidAction,"Cooldown bypassed");
    auto end=f.run.prepareCombat(fallback,world);check(!f.run.commit(std::move(end)),"Fallback ignored world stamp");
    check(f.run.commit(std::move(end),world)&&f.run.state().encounter->active==f.actors[1],"Fallback did not advance turn");
    rejects(r::ErrorCode::StaleCommand,[&]{f.run.prepareCombat(fallback,world);});
}
void rejectionAndBudget(){
    Fixture f;auto snapshot=f.run.save();auto bad=f.move(false);auto missingItem=f.strike("test.heal","example.heal");
    auto plan=f.plan({bad,missingItem});check(plan.endTurn()&&plan.evaluations().size()==2,"Illegal options not filtered");
    for(const auto&e:plan.evaluations())check(e.rejection.has_value()&&!e.score,"Rejected option was scored");
    check(f.run.save()==snapshot,"Illegal options published");
}
void limits(){
    Fixture f;auto option=f.strike();auto before=f.run.save();
    r::CombatAiBudget budget;budget.evaluations=0;
    auto plan=f.plan({option},[](const auto&)->std::optional<std::int64_t>{return 5;},budget);
    check(plan.truncated()&&plan.evaluations().empty()&&plan.endTurn(),"Zero evaluation budget");
    budget.evaluations=1;plan=f.plan({f.strike("test.z"),f.strike("test.a")},[](const auto&)->std::optional<std::int64_t>{return -5;},budget);
    check(plan.selected()==id("test.a")&&plan.truncated(),"Sorted bounded prefix/negative score");
    budget.evaluations=65;rejects(r::ErrorCode::Limit,[&]{f.plan({option},[](const auto&)->std::optional<std::int64_t>{return 1;},budget);});
    rejects(r::ErrorCode::InvalidId,[&]{f.plan({option,option});});
    auto wrong=option;wrong.action.actor=f.actors[1];rejects(r::ErrorCode::InvalidAction,[&]{f.plan({wrong});});
    rejects(r::ErrorCode::MissingHandler,[&]{f.plan({option},{});});
    budget={};budget.action.effects=0;plan=f.plan({option},[](const auto&)->std::optional<std::int64_t>{return 1;},budget);
    check(plan.evaluations()[0].rejection==r::ErrorCode::BudgetExceeded&&plan.endTurn(),"Effect budget did not filter");
    budget.action.operations=0;plan=f.plan({},[](const auto&)->std::optional<std::int64_t>{return 1;},budget);
    check(plan.status()==r::CombatPlanStatus::NoAction&&plan.fallbackRejection()==r::ErrorCode::BudgetExceeded,"Invalid fallback accepted");
    rejects(r::ErrorCode::InvalidAction,[&]{f.run.prepareCombat(plan,world);});
    check(f.run.save()==before,"Failed planning mutated state");
}
void cancellationAndStaleness(){
    Fixture f;auto before=f.run.save();std::stop_source source;source.request_stop();
    auto plan=f.plan({f.strike()},[](const auto&)->std::optional<std::int64_t>{return 1;},{},source.get_token());
    check(plan.status()==r::CombatPlanStatus::Cancelled&&plan.evaluations().empty(),"Pre-cancellation ignored");
    source=std::stop_source{};plan=f.plan({f.strike()},[&](const auto&)->std::optional<std::int64_t>{source.request_stop();return 1;},{},source.get_token());
    check(plan.status()==r::CombatPlanStatus::Cancelled&&!plan.selected(),"Callback cancellation ignored");
    source=std::stop_source{};plan=f.plan({f.strike()},[](const auto&)->std::optional<std::int64_t>{return 1;},{},source.get_token());
    auto tx=f.run.prepareCombat(plan,world);source.request_stop();check(!f.run.commit(std::move(tx),world),"Cancelled transaction published");
    rejects(r::ErrorCode::InvalidAction,[&]{f.run.prepareCombat(plan,world);});
    check(f.run.save()==before,"Cancellation mutated state");
    rejects(r::ErrorCode::Reentrant,[&]{f.plan({f.strike()},[&](const auto&)->std::optional<std::int64_t>{f.run.invalidateCommands();return 1;});});
    check(f.run.save()==before,"Reentrant score published");
    auto fresh=f.plan({f.strike()});rejects(r::ErrorCode::StaleWorld,[&]{f.run.prepareCombat(fresh,{11,1});});
    Fixture other;rejects(r::ErrorCode::StaleCommand,[&]{other.run.prepareCombat(fresh,world);});
    f.run.restore(before);rejects(r::ErrorCode::StaleCommand,[&]{f.run.prepareCombat(fresh,world);});
    auto after=f.plan({f.strike()});check(after.selected()==fresh.selected(),"Restore changed deterministic decision");
    bool cancelFallback=false;std::stop_source fallbackStop;auto policy=example::tacticalRules();auto decide=policy.decide;
    policy.decide=[&](const auto& context,auto kind){if(cancelFallback){fallbackStop.request_stop();throw r::Error(r::ErrorCode::RejectedEncounter,"Cancelled fallback rule");}return decide(context,kind);};
    Fixture fallback(policy);cancelFallback=true;
    auto cancelled=fallback.plan({},[](const auto&)->std::optional<std::int64_t>{return 1;},{},fallbackStop.get_token());
    check(cancelled.status()==r::CombatPlanStatus::Cancelled&&cancelled.fallbackRejection()==r::ErrorCode::RejectedEncounter,"Fallback error masked cancellation");
}
}
int main(){try{decisions();rejectionAndBudget();limits();cancellationAndStaleness();std::cout<<"L5 combat AI checks: "<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
