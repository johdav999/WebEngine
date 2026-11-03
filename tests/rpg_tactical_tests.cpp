#include "../examples/rpg/tactics.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace r=engine::rpg;
namespace {
int checks=0;
r::ContentId id(const char* s){return r::ContentId(s);}
void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F>void rejects(r::ErrorCode code,F fn){try{fn();}catch(const r::Error& e){check(e.code()==code,e.what());return;}throw std::runtime_error("Expected RPG rejection");}
const r::WorldStamp world{10,1};
r::Transaction prepare(r::Runtime& run,std::vector<r::Operation> ops,r::Budget budget={},std::optional<r::WorldStamp> stamp={}) {
    auto it=run.state().producers.find(id("test.input"));
    return run.prepare(run.command(id("test.input"),it==run.state().producers.end()?1:it->second+1,std::move(ops)),budget,stamp);
}
r::Results apply(r::Runtime& run,std::vector<r::Operation> ops,std::optional<r::WorldStamp> stamp={}) {
    auto tx=prepare(run,std::move(ops),{},stamp);auto result=tx.results();check(run.commit(std::move(tx),stamp),"commit failed");return result;
}
void rejected(r::Runtime& run,r::ErrorCode code,std::vector<r::Operation> ops,r::Budget budget={},std::optional<r::WorldStamp> stamp=world) {
    const auto* before=&run.state();const auto rng=run.state().random;
    rejects(code,[&]{prepare(run,std::move(ops),budget,stamp);});
    check(before==&run.state() && rng==run.state().random,"Rejected work changed published state/RNG");
}
struct Fixture {
    r::Runtime run;
    std::vector<r::EntityId> actors;
    explicit Fixture(r::EncounterHandler policy=example::tacticalRules()):run(example::encounterCatalog(std::move(policy)),r::SessionId(1),42) {
        for(int i=0;i<3;++i){auto a=apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0];actors.push_back(a);apply(run,{r::SetTacticalPosition{a,{5*i,0,0}}});}
        apply(run,{r::BeginEncounter{{id("example.turns"),1},actors}});
    }
    r::SpatialSnapshot snapshot() const {
        r::SpatialSnapshot result{world};for(auto a:actors){result.entities.push_back({a,run.state().positions.at(a),true});result.legalTargets.push_back(a);}return result;
    }
    r::TacticalAction move(std::optional<r::PathQuote> path={}) {
        const auto& e=*run.state().encounter;auto a=*e.active;auto start=run.state().positions.at(a);
        if(!path)path=r::PathQuote{start,{start.x+2,0,0},2,{start,{start.x+1,0,0},{start.x+2,0,0}},true,true,true};
        return {a,e.epoch,e.turnEpoch,r::MoveAction{},run.spatialQuote(a,snapshot(),std::move(path))};
    }
    r::TacticalAction ability(const char* name="example.strike",std::optional<r::ItemId> item={}) {
        const auto& e=*run.state().encounter;auto a=*e.active;
        return {a,e.epoch,e.turnEpoch,r::UseAbility{a,id(name),{actors[1]},item},run.spatialQuote(a,snapshot())};
    }
    r::AcknowledgePresentation ack(r::PresentationCompletion completion=r::PresentationCompletion::Completed)const {
        const auto& p=*run.state().presentation;return {p.actionEpoch,p.encounterEpoch,p.turnEpoch,completion};
    }
};
void movementAndPresentation() {
    Fixture f;auto action=f.move();auto tx=prepare(f.run,{action},{},world);
    check(f.run.state().positions.at(f.actors[0]).x==0 && !f.run.state().presentation,"preview mutated live state");
    check(tx.state().positions.at(f.actors[0]).x==2 && tx.results().actions[0].route.size()==3,"prepared destination/result");
    check(!f.run.canCommit(tx) && !f.run.commit(std::move(tx)),"world stamp must be supplied at publication");
    check(!f.run.canCommit(tx,r::WorldStamp{10,2}) && !f.run.commit(std::move(tx),r::WorldStamp{10,2}),"stale world committed");
    check(!f.run.canCommit(tx,r::WorldStamp{11,1}),"world identity mismatch");
    check(f.run.commit(std::move(tx),world),"valid retry after rejected publication");
    check(!f.run.commit(std::move(tx),world),"duplicate publication");
    check(f.run.state().positions.at(f.actors[0]).x==2 && f.run.state().encounter->allowance.movement==4 && f.run.state().encounter->allowance.ap==1,"move costs apply once");
    rejected(f.run,r::ErrorCode::PresentationPending,{action});
    rejected(f.run,r::ErrorCode::PresentationPending,{r::EndTurn{f.actors[0],1,1}});
    rejected(f.run,r::ErrorCode::PresentationPending,{r::AdjustResource{f.actors[0],id("example.reserve"),{1,id("example.point"),1}}});
    rejects(r::ErrorCode::PresentationPending,[&]{f.run.allocateEntityId();});
    rejects(r::ErrorCode::PresentationPending,[&]{f.move();});
    auto ack=f.ack();auto wrong=ack;wrong.actionEpoch++;
    rejected(f.run,r::ErrorCode::StalePresentation,{wrong});wrong=ack;wrong.turnEpoch++;rejected(f.run,r::ErrorCode::StalePresentation,{wrong});
    rejected(f.run,r::ErrorCode::InvalidAction,{ack,ack});
    auto old=f.run.command(id("test.old"),1,{ack});f.run.invalidateCommands();
    rejects(r::ErrorCode::StaleCommand,[&]{f.run.prepare(old);});
    const auto phase=f.run.state().phaseEpoch,turn=f.run.state().turnEpoch;const auto rng=f.run.state().random;
    apply(f.run,{f.ack(r::PresentationCompletion::Recovered)});
    check(!f.run.state().presentation && f.run.state().positions.at(f.actors[0]).x==2 && f.run.state().phaseEpoch==phase && f.run.state().turnEpoch==turn && f.run.state().random==rng,"recovery acknowledgement must only unlock");
    rejected(f.run,r::ErrorCode::StalePresentation,{ack});
    rejected(f.run,r::ErrorCode::InvalidSpatial,{action});
    action=f.move();apply(f.run,{action},world);apply(f.run,{f.ack(r::PresentationCompletion::Skipped)});
    check(f.run.state().encounter->allowance.ap==0 && f.run.state().positions.at(f.actors[0]).x==4,"second move");
    rejected(f.run,r::ErrorCode::InvalidAmount,{f.move()});
}
void quotes() {
    Fixture f;
    auto action=f.move();rejected(f.run,r::ErrorCode::StaleWorld,{action},{},r::WorldStamp{10,2});
    rejected(f.run,r::ErrorCode::StaleWorld,{action},{},{});
    Fixture foreign;rejected(foreign.run,r::ErrorCode::InvalidSpatial,{action});
    auto wrong=action;wrong.actor=f.actors[1];rejected(f.run,r::ErrorCode::InvalidSpatial,{wrong});
    wrong=action;wrong.turnEpoch++;rejected(f.run,r::ErrorCode::InvalidSpatial,{wrong});
    wrong=action;wrong.spatial.reset();rejected(f.run,r::ErrorCode::InvalidSpatial,{wrong});
    rejected(f.run,r::ErrorCode::InvalidAction,{action,r::EndTurn{f.actors[0],1,1}});
    rejected(f.run,r::ErrorCode::InvalidAction,{r::SetTacticalPosition{f.actors[0],{1,0,0}}});
    auto quote=action.spatial;apply(f.run,{r::SetCooldown{f.actors[0],id("test.noop"),r::TurnPhase::OwnerEnd,0}});
    rejected(f.run,r::ErrorCode::InvalidSpatial,{action});
    const auto a=f.actors[0];
    for(int which=0;which<8;++which){
        auto path=*f.move().spatial->path();
        if(which==0)path.reachable=false;if(which==1)path.destinationFree=false;if(which==2)path.destinationResident=false;
        if(which==3)path.route.clear();if(which==4)path.start.x=1;if(which==5)path.route.back().x=3;
        if(which==6){path.destination={5,0,0};path.route.back()=path.destination;}if(which==7)path.cost=-1;
        rejected(f.run,r::ErrorCode::InvalidSpatial,{f.move(path)});
    }
    action=f.move();action.spatial=f.run.spatialQuote(a,f.snapshot());rejected(f.run,r::ErrorCode::InvalidSpatial,{action});
    auto snapshot=f.snapshot();snapshot.entities.pop_back();action=f.move();action.spatial=f.run.spatialQuote(a,snapshot,action.spatial->path());rejected(f.run,r::ErrorCode::InvalidSpatial,{action});
    snapshot=f.snapshot();snapshot.entities[1].resident=false;action.spatial=f.run.spatialQuote(a,snapshot,action.spatial->path());rejected(f.run,r::ErrorCode::InvalidSpatial,{action});
    snapshot=f.snapshot();snapshot.entities[0].position.x++;action.spatial=f.run.spatialQuote(a,snapshot,action.spatial->path());rejected(f.run,r::ErrorCode::InvalidSpatial,{action});
    snapshot=f.snapshot();snapshot.entities.push_back(snapshot.entities[0]);rejects(r::ErrorCode::InvalidSpatial,[&]{f.run.spatialQuote(a,snapshot);});
    snapshot=f.snapshot();snapshot.world={};rejects(r::ErrorCode::InvalidSpatial,[&]{f.run.spatialQuote(a,snapshot);});
    auto path=*f.move().spatial->path();path.route.resize(257);rejects(r::ErrorCode::Limit,[&]{f.run.spatialQuote(a,f.snapshot(),path);});
    action=f.ability();snapshot=f.snapshot();snapshot.legalTargets.clear();action.spatial=f.run.spatialQuote(a,snapshot);rejected(f.run,r::ErrorCode::InvalidTarget,{action});
}
void abilitiesAndCosts() {
    Fixture f;auto action=f.ability();const auto rng=f.run.state().random;
    apply(f.run,{action},world);check(f.run.state().characters.at(f.actors[0]).resources.at(id("example.reserve")).current==19,"ability resource cost");
    check(f.run.state().random!=rng && f.run.state().cooldowns.at(f.actors[0]).contains(id("example.strike")),"ability RNG/cooldown");
    apply(f.run,{f.ack()});rejected(f.run,r::ErrorCode::InvalidAction,{f.ability()});
    rejected(f.run,r::ErrorCode::InvalidAction,{r::UseAbility{f.actors[0],id("example.concentrate"),{f.actors[0]}, {}}});
    apply(f.run,{f.ability("example.concentrate")},world);apply(f.run,{f.ack()});
    rejected(f.run,r::ErrorCode::InvalidAmount,{f.ability("example.concentrate")});
    auto rules=example::tacticalRules();rules.authorize=[](const auto&){r::ActionPlan p;p.ap=-1;return p;};
    Fixture negative(rules);rejected(negative.run,r::ErrorCode::InvalidAmount,{negative.move()});
    rules.authorize=[](const auto&){r::ActionPlan p;p.movement=std::numeric_limits<std::int64_t>::max();return p;};
    Fixture overflow(rules);rejected(overflow.run,r::ErrorCode::Overflow,{overflow.move()});
    rules.authorize=[](const auto&){r::ActionPlan p;p.resources.push_back({id("example.reserve"),{21,id("example.point"),1}});return p;};
    Fixture poor(rules);rejected(poor.run,r::ErrorCode::InvalidAmount,{poor.move()});
    // Item action consumes through the original owned-inventory path.
    Fixture item;apply(item.run,{r::CreateInventory{id("example.pack"),item.actors[0]}});
    auto inventory=item.run.state().inventories.begin()->first;
    auto items=apply(item.run,{r::AddItem{inventory,id("example.tonic"),1}}).items;
    apply(item.run,{r::AdjustResource{item.actors[1],id("example.reserve"),{-3,id("example.point"),1}}});
    auto use=item.ability("example.heal",items[0]);apply(item.run,{use},world);
    check(!item.run.state().items.contains(items[0]) && item.run.state().characters.at(item.actors[1]).resources.at(id("example.reserve")).current==20,"item consumption and healing");
    apply(item.run,{item.ack()});rejected(item.run,r::ErrorCode::MissingItem,{item.ability("example.heal",items[0])});
}
void reactions() {
    auto rules=example::tacticalRules();
    rules.reactions=[](const r::TacticalContext& c,r::ReactionPhase phase,const std::optional<r::ReactionToken>& parent){
        auto roster=c.state.encounter->roster;
        if(parent)return std::vector<r::ReactionToken>{*parent}; // Self-cycle is ignored, never re-fired.
        if(phase==r::ReactionPhase::BeforeAction)return std::vector<r::ReactionToken>{{roster[1],id("test.b")},{roster[0],id("test.a")}};
        return std::vector<r::ReactionToken>{{roster[0],id("test.a")},{roster[2],id("test.c")}};
    };
    rules.react=[](const r::ReactionContext& c,r::Random& random){random.next();return r::EffectProposal{true,{r::ResourceEffect{c.token.source,id("example.focus"),{-1,id("example.point"),1}}}};};
    Fixture f(rules);auto result=apply(f.run,{f.move()},world);
    check(result.reactions.size()==3 && result.reactions[0].token.source==f.actors[0] && result.reactions[1].token.source==f.actors[1] && result.reactions[2].phase==r::ReactionPhase::AfterAction,"phase/initiative/stable ID order and once per token");
    Fixture budget(rules);r::Budget small;small.reactions=1;rejected(budget.run,r::ErrorCode::BudgetExceeded,{budget.move()},small);
    small={};small.randomDraws=1;rejected(budget.run,r::ErrorCode::BudgetExceeded,{budget.move()},small);
    small={};small.effects=1;rejected(budget.run,r::ErrorCode::BudgetExceeded,{budget.move()},small);
    // Child chains are prepared in the same candidate, never a live interrupt stack.
    rules.reactions=[](const r::TacticalContext& c,r::ReactionPhase phase,const std::optional<r::ReactionToken>& parent){
        if(phase==r::ReactionPhase::BeforeAction)return std::vector<r::ReactionToken>{};
        int depth=parent?std::stoi(parent->id.value().substr(6))+1:1;
        return std::vector<r::ReactionToken>{{c.action.actor,r::ContentId("test.r"+std::to_string(depth))}};
    };
    Fixture nested(rules);small={};small.reactionDepth=2;rejected(nested.run,r::ErrorCode::BudgetExceeded,{nested.ability()},small);
    // An invalid after-action effect rolls back the root ability's cost, damage, cooldown and RNG.
    rules.react=[](const r::ReactionContext& c,r::Random& random){random.next();return r::EffectProposal{true,{r::ResourceEffect{c.root.action.actor,id("example.reserve"),{-100,id("example.point"),1}}}};};
    Fixture failure(rules);rejected(failure.run,r::ErrorCode::InvalidAmount,{failure.ability()});
    rules.react=[](const r::ReactionContext& c,r::Random& random){random.next();return r::EffectProposal{true,{r::ResourceEffect{{r::SessionId(99),1},id("example.reserve"),{-1,id("example.point"),1}}}};};
    Fixture escaped(rules);rejected(escaped.run,r::ErrorCode::InvalidTarget,{escaped.move()});
    rules.reactions=[](const r::TacticalContext& c,auto,const auto&){return std::vector<r::ReactionToken>{{c.action.actor,id("test.dup")},{c.action.actor,id("test.dup")}};};
    Fixture duplicate(rules);rejected(duplicate.run,r::ErrorCode::InvalidAction,{duplicate.move()});
}
void pendingOutcome() {
    auto rules=example::tacticalRules();
    rules.decide=[](const r::EncounterContext& c,r::EncounterCheck check){
        if(check==r::EncounterCheck::Exit)return r::EncounterDecision{true,c.encounter.outcome};
        return r::EncounterDecision{true,c.state.positions.at(c.encounter.roster[0]).x==2?r::EncounterOutcome::Victory:r::EncounterOutcome::Continue};
    };
    Fixture f(rules);apply(f.run,{f.move()},world);
    check(f.run.state().encounter->stage==r::EncounterStage::Finished && f.run.state().presentation.has_value(),"terminal outcome remains presentation gated");
    rejected(f.run,r::ErrorCode::PresentationPending,{r::ExitEncounter{1,1}});
    apply(f.run,{f.ack()});apply(f.run,{r::ExitEncounter{1,1}});check(!f.run.state().encounter,"exit after acknowledgement");
}
void rollbackAndOrdering() {
    auto rules=example::tacticalRules();
    rules.reactions=[](const r::TacticalContext& c,r::ReactionPhase phase,const std::optional<r::ReactionToken>& parent){
        if(parent || phase==r::ReactionPhase::BeforeAction)return std::vector<r::ReactionToken>{};
        return std::vector<r::ReactionToken>{{c.action.actor,id("test.after")}};
    };
    rules.react=[](const r::ReactionContext& c,r::Random& random){random.next();return r::EffectProposal{true,{r::ResourceEffect{c.root.action.actor,id("example.reserve"),{-100,id("example.point"),1}}}};};
    Fixture item(rules);auto bag=apply(item.run,{r::CreateInventory{id("example.pack"),item.actors[0]}}).inventories[0];
    auto tonic=apply(item.run,{r::AddItem{bag,id("example.tonic"),1}}).items[0];
    apply(item.run,{r::AdjustResource{item.actors[1],id("example.reserve"),{-3,id("example.point"),1}}});
    rejected(item.run,r::ErrorCode::InvalidAmount,{item.ability("example.heal",tonic)});
    check(item.run.state().items.at(tonic).quantity==1 && item.run.state().encounter->allowance.ap==2 && item.run.state().actionEpoch==0,"item/action receipt rollback");
    // Before reactions use round initiative, not discovery order or changed attributes mid-round.
    rules=example::tacticalRules();rules.initiative=[](const auto&,r::EntityId actor){return static_cast<std::int64_t>(actor.value);};
    rules.reactions=[](const r::TacticalContext& c,auto,const auto& parent){
        if(parent)return std::vector<r::ReactionToken>{};
        return std::vector<r::ReactionToken>{{c.state.encounter->roster[0],id("test.z")},{c.state.encounter->roster[1],id("test.z")},{c.state.encounter->roster[1],id("test.a")}};
    };
    rules.react=[](const auto&,r::Random&){return r::EffectProposal{};};
    Fixture order(rules);auto result=apply(order.run,{order.move()},world);
    check(result.reactions.size()==3 && result.reactions[0].token.source==order.actors[1] && result.reactions[0].token.id==id("test.a") && result.reactions[2].token.source==order.actors[0],"initiative and lexical source-token ordering");
    auto bad=order.ack();bad.completion=static_cast<r::PresentationCompletion>(99);rejected(order.run,r::ErrorCode::InvalidAction,{bad});
    // Complete a bounded child chain; each callback observes prior staged effects.
    rules=example::tacticalRules();
    rules.reactions=[](const r::TacticalContext& c,r::ReactionPhase phase,const std::optional<r::ReactionToken>& parent){
        if(phase==r::ReactionPhase::BeforeAction)return std::vector<r::ReactionToken>{};
        if(!parent)return std::vector<r::ReactionToken>{{c.action.actor,id("test.parent")}};
        if(parent->id==id("test.parent"))return std::vector<r::ReactionToken>{{c.action.actor,id("test.child")}};
        return std::vector<r::ReactionToken>{};
    };
    rules.react=[](const r::ReactionContext& c,r::Random&){
        if(c.token.id==id("test.child"))check(c.root.state.characters.at(c.token.source).resources.at(id("example.focus")).current==4,"child must observe parent effect");
        return r::EffectProposal{true,{r::ResourceEffect{c.token.source,id("example.focus"),{-1,id("example.point"),1}}}};
    };
    Fixture nested(rules);result=apply(nested.run,{nested.move()},world);
    check(result.reactions.size()==2 && result.reactions[1].depth==2 && nested.run.state().characters.at(nested.actors[0]).resources.at(id("example.focus")).current==3,"bounded nested reaction success");
    // A callback cannot invalidate the live runtime during tactical preparation.
    r::Runtime* owner=nullptr;rules.react=[&](const auto&,r::Random&)->r::EffectProposal{owner->invalidateCommands();return {};};
    Fixture reentry(rules);owner=&reentry.run;rejected(reentry.run,r::ErrorCode::Reentrant,{reentry.move()});
    rules.react=[](const auto&,r::Random&)->r::EffectProposal{throw std::runtime_error("provider failed");};
    Fixture exception(rules);auto* before=&exception.run.state();bool threw=false;
    try{prepare(exception.run,{exception.move()},{},world);}catch(const std::runtime_error&){threw=true;}
    check(threw && before==&exception.run.state(),"reaction exception rollback");
}
void trace() {
    Fixture f;std::ostringstream out;
    auto record=[&](const r::Results& result){const auto& s=f.run.state();out<<s.revision<<':'<<s.actionEpoch<<':'<<s.phaseEpoch<<':'<<s.random.state<<':'<<s.positions.at(f.actors[0]).x<<':'<<s.encounter->allowance.ap<<':'<<s.encounter->allowance.movement<<':'<<s.presentation.has_value()<<'|';
        for(const auto& action:result.actions)out<<action.presentation.actionEpoch<<','<<action.apSpent<<','<<action.movementSpent<<','<<action.route.size()<<';';
        out<<'|';for(const auto& change:result.resources)out<<change.entity.value<<','<<change.delta.value<<';';out<<'\n';};
    record(apply(f.run,{f.move()},world));record(apply(f.run,{f.ack()}));
    record(apply(f.run,{f.ability()},world));record(apply(f.run,{f.ack(r::PresentationCompletion::Skipped)}));
    const std::string expected="8:1:2:1753877967969059832:2:1:4:1|1,1,2,3;|\n9:1:2:1753877967969059832:2:1:4:0||\n10:2:2:3118741472915405573:2:0:4:1|2,1,0,0;|1,-1;2,-4;\n11:2:2:3118741472915405573:2:0:4:0||\n";
    if(out.str()!=expected)std::cerr<<out.str();check(out.str()==expected,"canonical tactical trace differs");std::cout<<out.str();
}
}
int main(){try{movementAndPresentation();quotes();abilitiesAndCosts();reactions();pendingOutcome();rollbackAndOrdering();trace();std::cout<<"R6 tactical checks: "<<checks<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
