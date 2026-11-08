#include "../examples/rpg/quest_rewards.hpp"
#include "../examples/rpg/builds.hpp"
#include <iostream>
#include <stdexcept>

namespace r=engine::rpg;namespace q=engine::quests;namespace b=r::quest_bridge;
namespace {
int checks=0;
r::ContentId id(const char* s){return r::ContentId(s);}
void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
template<class F>void rejects(r::ErrorCode code,F fn){try{fn();}catch(const r::Error& e){check(e.code()==code,e.what());return;}throw std::runtime_error("Expected RPG rejection");}
template<class F>void questRejects(q::ErrorCode code,F fn){try{fn();}catch(const q::Error& e){check(e.code()==code,e.what());return;}throw std::runtime_error("Expected quest rejection");}
struct Fixture {
    b::Coordinator game;
    r::EntityId actor,bag;
    std::uint64_t input=0;
    explicit Fixture(b::Policy policy=example::rewardPolicy(),bool facts=false)
        :game(example::encounterCatalog(example::tacticalRules()),example::rewardQuests(facts),r::SessionId(1),id("example.quest-awards"),std::move(policy),42),
        actor{r::SessionId(1),0},bag{r::SessionId(1),0} {
        actor=apply({r::CreateCharacter{id("example.scholar")}}).created[0];
        bag=apply({r::CreateInventory{id("example.pack"),actor}}).inventories[0];
    }
    r::Results apply(std::vector<r::Operation> operations){return game.applyRpg(id("test.setup"),++input,std::move(operations));}
    void activate(bool replay=false){auto cmd=game.command();cmd.batch.replay=replay;cmd.batch.commands.push_back({"example.delivery",q::CommandKind::Activate,0,{}});auto tx=game.prepare(cmd);check(game.commit(std::move(tx)),"activate");}
    q::Event event(std::uint64_t seq=1){return game.event({"example.delivery","example.delivery","test.delivery",seq,{{"item",std::string("parcel")}}});}
    void post(std::uint64_t seq=1){game.post(event(seq));}
    std::int64_t xp()const{return game.rpgState().characters.at(actor).experience;}
    std::int64_t tonics()const{std::int64_t result=0;for(const auto& [id,item]:game.rpgState().items)if(item.definition==r::ContentId("example.tonic"))result+=item.quantity;return result;}
};
void atomicRewards() {
    Fixture f;f.activate();f.post();auto cmd=f.game.command();auto tx=f.game.prepare(cmd);
    check(f.xp()==0 && f.tonics()==0 && f.game.pending()==1 && f.game.questState().receipts.empty(),"preview changed live state/queue");
    check(tx.rewards().awards.size()==2 && tx.effects().size()==2 && tx.questState().quests.at("example.delivery").earned,"compound candidate");
    check(tx.rewards().loot.size()==1,"Quest loot was not staged");
    check(f.game.canCommit(tx) && f.game.commit(std::move(tx)),"atomic publish");
    check(f.xp()==10 && f.tonics()==1 && f.game.pending()==0 && f.game.questState().receipts.size()==2 && f.game.rpgState().awards.size()==2,"both rewards and receipts committed");
    check(!f.game.commit(std::move(tx)),"duplicate candidate committed");
    rejects(r::ErrorCode::ConsumedTransaction,[&]{tx.questState();});
    rejects(r::ErrorCode::StaleCommand,[&]{f.game.prepare(cmd);});
    for(int i=0;i<3;++i){auto again=f.game.prepare(f.game.command());check(again.effects().empty() && again.rewards().awards.empty(),"later ticks duplicate reward");check(f.game.commit(std::move(again)),"later tick");}
    auto retry=f.game.command();retry.batch.commands.push_back({"example.delivery",q::CommandKind::Retry,1,{}});auto retryTx=f.game.prepare(retry);check(f.game.commit(std::move(retryTx)),"retry");
    f.post(2);auto complete=f.game.prepare(f.game.command());check(complete.effects().empty(),"retry reemitted effects");check(f.game.commit(std::move(complete)),"practice completion");
    check(f.xp()==10 && f.tonics()==1 && f.game.rpgState().awards.size()==2,"exactly once across retry");
}
void rejectedCompound() {
    Fixture f;for(int i=0;i<4;++i)f.apply({r::AddItem{f.bag,id("example.blade"),1}});
    f.activate();f.post();auto cmd=f.game.command();const auto* rpg=&f.game.rpgState();const auto* quest=&f.game.questState();
    rejects(r::ErrorCode::Capacity,[&]{f.game.prepare(cmd);});
    check(rpg==&f.game.rpgState() && quest==&f.game.questState() && f.xp()==0 && f.game.pending()==1 && f.game.rpgState().awards.empty(),"full bag must roll back XP, quest, queue and receipts");
    auto item=f.game.rpgState().items.begin()->first;f.apply({r::RemoveItem{f.bag,item,1}});
    rejects(r::ErrorCode::StaleCommand,[&]{f.game.prepare(cmd);});
    auto repaired=f.game.prepare(f.game.command());check(f.game.commit(std::move(repaired)),"retry after freeing capacity");
    check(f.xp()==10 && f.tonics()==1 && f.game.pending()==0,"queued event survives rejection");
    auto policy=example::rewardPolicy();auto mapper=policy.reward;
    policy.reward=[mapper](const q::EffectRequest& e,const r::State& s){if(e.type=="example.item")return b::RewardProposal{false,{}};return mapper(e,s);};
    Fixture bad(policy);bad.activate();bad.post();rpg=&bad.game.rpgState();quest=&bad.game.questState();
    rejects(r::ErrorCode::InvalidAward,[&]{bad.game.prepare(bad.game.command());});
    check(rpg==&bad.game.rpgState() && quest==&bad.game.questState() && bad.game.pending()==1,"invalid mapping changed state");
    Fixture budget;budget.activate();budget.post();r::Budget tiny;tiny.operations=2;
    rejects(r::ErrorCode::BudgetExceeded,[&]{budget.game.prepare(budget.game.command(),tiny);});check(budget.game.pending()==1 && budget.xp()==0,"compound operation budget");
    q::Budget questTiny;questTiny.evaluations=1;
    rejects(r::ErrorCode::BudgetExceeded,[&]{budget.game.prepare(budget.game.command(),{},questTiny);});check(budget.game.pending()==1,"quest exhaustion consumed queue");
}
void staleness() {
    Fixture f;f.activate();f.post();auto tx=f.game.prepare(f.game.command());
    f.apply({r::AdjustResource{f.actor,id("example.reserve"),{-1,id("example.point"),1}}});
    check(!f.game.canCommit(tx) && !f.game.commit(std::move(tx)) && f.game.pending()==1 && f.xp()==0,"stale RPG candidate partially published");
    tx=f.game.prepare(f.game.command());check(f.game.canCommit(tx),"initial preflight");f.post(2);
    check(!f.game.commit(std::move(tx)) && f.game.pending()==2 && f.xp()==0,"queue revision invalidates compound commit");
    auto first=f.game.prepare(f.game.command());auto second=f.game.prepare(f.game.command());
    check(f.game.commit(std::move(first)) && !f.game.commit(std::move(second)),"competing quest/RPG candidates");
    check(f.xp()==10 && f.tonics()==1,"competing candidate duplicated rewards");
    Fixture other;auto foreign=other.game.command();rejects(r::ErrorCode::StaleCommand,[&]{f.game.prepare(foreign);});
    auto foreignTx=other.game.prepare(foreign);check(!f.game.commit(std::move(foreignTx)),"foreign coordinated candidate");
    check(other.game.commit(std::move(foreignTx)),"quest-only revision change");
    questRejects(q::ErrorCode::Stale,[&]{other.game.prepare(foreign);});
    auto stale=other.game.command();other.game.invalidateRpgCommands();rejects(r::ErrorCode::StaleCommand,[&]{other.game.prepare(stale);});
}
void projectionsAndReplay() {
    auto policy=example::rewardPolicy();policy.facts=[](const r::State& state){return q::Fields{{"ready",!state.characters.empty()}};};
    Fixture projected(policy,true);projected.activate();check(projected.xp()==10 && projected.tonics()==1 && std::get<bool>(projected.game.questState().facts.at("ready")),"typed fact projection");
    auto cmd=projected.game.command();cmd.batch.facts["ready"]=false;rejects(r::ErrorCode::InvalidAward,[&]{projected.game.prepare(cmd);});
    Fixture events;events.activate();
    check(events.game.projectEvent({},[](const r::State&,const r::Results&){return std::optional<b::ProjectedEvent>{{"example.delivery","example.delivery","test.projected",1,{{"item",std::string("parcel")}}}};}),"explicit event projection");
    questRejects(q::ErrorCode::Duplicate,[&]{events.game.projectEvent({},[](const auto&,const auto&){return std::optional<b::ProjectedEvent>{{"example.delivery","example.delivery","test.projected",1,{{"item",std::string("parcel")}}}};});});
    check(events.game.pending()==1,"duplicate projected event queued");auto tx=events.game.prepare(events.game.command());check(events.game.commit(std::move(tx)),"projected event reward");
    policy=example::rewardPolicy();policy.reward=[](const auto&,const auto&)->b::RewardProposal{throw std::runtime_error("replay invoked reward mapper");};
    Fixture replay(policy);replay.activate();replay.post();cmd=replay.game.command();cmd.batch.replay=true;
    tx=replay.game.prepare(cmd);check(tx.effects().empty() && tx.rewards().awards.size()==2,"replay ledger candidate");check(replay.game.commit(std::move(tx)),"replay commit");
    check(replay.xp()==0 && replay.tonics()==0 && replay.game.rpgState().awards.size()==2,"replay granted rewards");
    for(const auto& [id,kind]:replay.game.rpgState().awards)check(kind==r::AwardDisposition::ReplaySuppressed,"replay disposition");
    cmd=replay.game.command();cmd.batch.commands.push_back({"example.delivery",q::CommandKind::Retry,1,{}});tx=replay.game.prepare(cmd);check(replay.game.commit(std::move(tx)),"replay retry");
    replay.post(2);tx=replay.game.prepare(replay.game.command());check(replay.game.commit(std::move(tx)) && replay.xp()==0,"replayed completion later granted reward");
}
void callbacksAndLedger() {
    b::Coordinator* owner=nullptr;auto policy=example::rewardPolicy();
    policy.reward=[&](const auto&,const auto&)->b::RewardProposal{owner->invalidateRpgCommands();return {};};
    Fixture reentry(policy);owner=&reentry.game;reentry.activate();reentry.post();
    rejects(r::ErrorCode::Reentrant,[&]{reentry.game.prepare(reentry.game.command());});check(reentry.game.pending()==1 && reentry.xp()==0,"mapper reentry");
    policy.reward=[](const auto&,const auto&)->b::RewardProposal{throw std::runtime_error("mapping failure");};
    Fixture failure(policy);failure.activate();failure.post();const auto* before=&failure.game.questState();bool threw=false;
    try{failure.game.prepare(failure.game.command());}catch(const std::runtime_error&){threw=true;}
    check(threw && before==&failure.game.questState() && failure.game.pending()==1 && failure.game.rpgState().awards.empty(),"mapper exception rollback");
    rejects(r::ErrorCode::InvalidAward,[&]{failure.apply({r::RecordAward{{{id("example.quest-awards"),"test/award"},r::AwardDisposition::Granted}}});});
    rejects(r::ErrorCode::InvalidAward,[&]{failure.game.applyRpg(id("webengine.quest-awards"),1,{r::AddExperience{failure.actor,{1,id("example.practice"),1}}});});
    // Generic receipt validation/capacity does not require the quest library.
    r::Runtime standalone(example::characterCatalog(example::characterDefinitions()),r::SessionId(3));
    auto record=[&](std::uint64_t sequence,std::vector<r::Operation> operations){auto tx=standalone.prepare(standalone.command(id("test.ledger"),sequence,std::move(operations)));check(standalone.commit(std::move(tx)),"ledger commit");};
    record(1,{r::RecordAward{{{id("test.source"),"quest/effect"},r::AwardDisposition::Granted}}});
    rejects(r::ErrorCode::DuplicateAward,[&]{record(2,{r::RecordAward{{{id("test.source"),"quest/effect"},r::AwardDisposition::Granted}}});});
    rejects(r::ErrorCode::InvalidAward,[&]{record(2,{r::RecordAward{{{id("test.source"),""},r::AwardDisposition::Granted}}});});
    for(std::size_t at=1,seq=2;at<r::Limits::awards;++seq){std::vector<r::Operation> ops;for(std::size_t n=0;n<256 && at<r::Limits::awards;++n,++at)ops.push_back(r::RecordAward{{{id("test.source"),"quest/"+std::to_string(at)},r::AwardDisposition::Granted}});record(seq,std::move(ops));}
    rejects(r::ErrorCode::Limit,[&]{record(1000,{r::RecordAward{{{id("test.source"),"quest/overflow"},r::AwardDisposition::Granted}}});});
    check(standalone.state().awards.size()==r::Limits::awards,"award ledger truncated");
}
void spatialAndPending() {
    Fixture f;f.apply({r::SetTacticalPosition{f.actor,{0,0,0}},r::BeginEncounter{{id("example.turns"),1},{f.actor}}});
    const r::WorldStamp world{1,1};r::SpatialSnapshot snapshot{world,{{f.actor,{0,0,0},true}},{f.actor}};
    auto quote=f.game.spatialQuote(f.actor,snapshot,r::PathQuote{{0,0,0},{2,0,0},2,{{0,0,0},{2,0,0}},true,true,true});
    r::TacticalAction action{f.actor,1,1,r::MoveAction{},quote};
    rejects(r::ErrorCode::InvalidAction,[&]{f.game.applyRpg(id("test.move"),1,{action},{},world);});
    auto tx=f.game.prepareRpg(id("test.move"),1,{action},{},world);
    check(!f.game.commitRpg(std::move(tx),r::WorldStamp{1,2}),"bridge skipped final external world check");
    check(f.game.commitRpg(std::move(tx),world),"spatial RPG publication");
    rejects(r::ErrorCode::PresentationPending,[&]{f.game.prepare(f.game.command());});
    f.apply({r::AcknowledgePresentation{1,1,1,r::PresentationCompletion::Recovered}});
    auto quest=f.game.prepare(f.game.command());check(f.game.commit(std::move(quest)),"quest work after recovery");
}
}
void combatPlanning(){
    Fixture f;f.apply({r::SetTacticalPosition{f.actor,{0,0,0}},r::BeginEncounter{{id("example.turns"),1},{f.actor}}});
    const r::WorldStamp world{9,1};r::SpatialSnapshot spatial{world,{{f.actor,{0,0,0},true}},{f.actor}};
    auto quote=f.game.spatialQuote(f.actor,spatial,r::PathQuote{{0,0,0},{1,0,0},1,{{0,0,0},{1,0,0}},true,true,true});
    const auto&e=*f.game.rpgState().encounter;
    std::vector<r::CombatOption> options{{id("test.move"),{f.actor,e.epoch,e.turnEpoch,r::MoveAction{},quote}}};
    r::CombatScorer scorer=[](const auto&)->std::optional<std::int64_t>{return 1;};
    auto before=f.game.save();
    rejects(r::ErrorCode::InvalidAward,[&]{f.game.planCombat(id("webengine.quest-awards"),1,f.actor,options,scorer,{},world);});
    rejects(r::ErrorCode::Reentrant,[&]{f.game.planCombat(id("test.ai"),1,f.actor,options,[&](const auto&)->std::optional<std::int64_t>{f.game.invalidateRpgCommands();return 1;},{},world);});
    auto plan=f.game.planCombat(id("test.ai"),1,f.actor,options,scorer,{},world);
    check(plan.selected()==id("test.move")&&f.game.save()==before,"Bridge planner mutated kernels");
    auto tx=f.game.prepareCombat(plan,world);check(!f.game.commitRpg(std::move(tx),r::WorldStamp{9,2}),"Bridge AI stale world");
    check(f.game.commitRpg(std::move(tx),world)&&f.game.rpgState().positions.at(f.actor).x==1,"Bridge AI publication");
    check(f.game.questState().receipts.empty()&&f.game.pending()==0,"AI changed quest kernel");
}
void encounterRewards(){
    Fixture f;f.activate();f.post();auto before=f.game.save();auto random=f.game.rpgState().random;
    r::Budget zero;zero.randomDraws=0;
    rejects(r::ErrorCode::BudgetExceeded,[&]{f.game.prepare(f.game.command(),zero);});
    check(f.game.save()==before&&f.game.pending()==1&&f.game.rpgState().random==random,"Quest loot exhaustion published");
    r::GrantReward reward{{id("example.encounter-awards"),"encounter-1"},{r::AddExperience{f.actor,{5,id("example.practice"),1}}},
        {{id("example.loot"),{{id("example.supplies"),1,{r::AddItem{f.bag,id("example.tonic"),1}}}},1}}};
    auto forbidden=reward;forbidden.id.source=id("example.quest-awards");
    rejects(r::ErrorCode::InvalidAward,[&]{f.apply({forbidden});});
    f.apply({reward});auto tx=f.game.prepare(f.game.command());check(f.game.commit(std::move(tx)),"Quest alongside encounter reward");
    check(f.xp()==15&&f.game.rpgState().awards.size()==3&&f.game.questState().receipts.size()==2,"Ledger namespaces mixed");
    auto bytes=f.game.save();f.game.restore(bytes);
    rejects(r::ErrorCode::DuplicateAward,[&]{f.apply({reward});});
    check(f.xp()==15&&f.tonics()==2,"Reload duplicated reward");
}
void selectableBuilds(){
    auto catalog=example::encounterCatalog(example::tacticalRules(),example::buildDefinitions());
    b::Coordinator game(catalog,example::rewardQuests(),r::SessionId(1),id("example.quest-awards"),example::rewardPolicy());
    auto actor=game.applyRpg(id("test.build"),1,{r::CreateCharacter{id("example.scholar")}}).created[0];
    game.applyRpg(id("test.build"),2,{r::AddExperience{actor,{10,id("example.practice"),1}},r::SetProgressionChoices{actor,{id("example.mastery")}}});
    auto bytes=game.save();auto stale=game.prepareRpg(id("test.build"),3,{r::SetProgressionChoices{actor,{}}});game.restore(bytes);
    check(!game.commitRpg(std::move(stale))&&game.rpgState().choices.at(actor).contains(id("example.mastery")),"Combined choice restore/staleness");
    game.applyRpg(id("test.build"),3,{r::SetProgressionChoices{actor,{}}});
    check(game.rpgState().choices.empty()&&game.rpgState().characters.at(actor).unlocks.contains(id("example.first")),"Bridge respec removed automatic unlock");
}
void economyBridge(){
    Fixture f;auto stock=f.apply({r::CreateInventory{id("example.pack"),{}}}).inventories[0];
    auto item=f.apply({r::AddItem{stock,id("example.tonic"),5}}).items[0];
    r::TradeItems trade{{id("test.market"),"order"},stock,f.bag,item,2,{}};
    auto forbidden=trade;forbidden.id.source=id("example.quest-awards");
    rejects(r::ErrorCode::InvalidAward,[&]{f.apply({forbidden});});
    r::CraftItems craft{{id("test.recipe"),"order"},{{stock,item,1}},{{stock,id("example.blade"),1}},{}};
    auto bad=craft;bad.id.source=id("example.quest-awards");
    rejects(r::ErrorCode::InvalidAward,[&]{f.apply({bad});});
    auto before=f.game.save();auto tx=f.game.prepareRpg(id("test.preview"),1,{trade,craft});
    check(f.game.save()==before&&f.game.commitRpg(std::move(tx)),"Bridge economics preview/publication");
    auto bytes=f.game.save();f.game.restore(bytes);
    rejects(r::ErrorCode::DuplicateAward,[&]{f.apply({trade});});
    rejects(r::ErrorCode::DuplicateAward,[&]{f.apply({craft});});
    f.activate();f.post();auto quest=f.game.prepare(f.game.command());check(f.game.commit(std::move(quest)),"Quest after market");
    check(f.game.rpgState().awards.size()==4&&f.game.questState().receipts.size()==2,"Market polluted quest ledger");
    f.apply({r::BeginEncounter{{id("example.turns"),1},{f.actor},{}}});
    trade.id.receipt="combat";auto combat=f.game.rpgState().revision;
    rejects(r::ErrorCode::InvalidEncounter,[&]{f.apply({trade});});
    check(f.game.rpgState().revision==combat,"Combat economy failure published");
}
int main(){try{economyBridge();atomicRewards();rejectedCompound();staleness();projectionsAndReplay();callbacksAndLedger();spatialAndPending();combatPlanning();encounterRewards();selectableBuilds();std::cout<<"R7 quest bridge checks: "<<checks<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
