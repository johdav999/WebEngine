#include "engine/rpg/combat_ai.hpp"
#include <algorithm>

namespace engine::rpg {
namespace {
void need(bool ok,ErrorCode code,const char* message){if(!ok)throw Error(code,message);}
struct Guard {
    bool& busy;
    explicit Guard(bool& b):busy(b){need(!busy,ErrorCode::Reentrant,"Reentrant combat planning");busy=true;}
    ~Guard(){busy=false;}
};
}
CombatPlan Runtime::planCombat(ContentId producer,std::uint64_t sequence,EntityId actor,
    std::span<const CombatOption> options,const CombatScorer& score,
    const CombatAiBudget& budget,WorldStamp world,std::stop_token stop){
    Guard guard(busy_);
    need(options.size()<=64&&budget.evaluations<=64,ErrorCode::Limit,"Combat planning cap exceeded");
    need(bool(score),ErrorCode::MissingHandler,"Combat planning needs a scorer");
    need(world.identity&&world.revision,ErrorCode::InvalidSpatial,"Combat planning needs a world stamp");
    need(!state_->presentation,ErrorCode::PresentationPending,"Finish presentation before planning");
    need(state_->encounter&&state_->encounter->stage==EncounterStage::AwaitingCommand&&
        state_->encounter->active==actor&&canAct(*state_,actor),ErrorCode::InvalidAction,"Not the active combat actor");
    const auto watermark=state_->producers.find(producer);
    need(sequence&&(watermark==state_->producers.end()||sequence>watermark->second),ErrorCode::InvalidSequence,"Invalid AI command sequence");
    need(watermark!=state_->producers.end()||state_->producers.size()<Limits::producers,ErrorCode::Limit,"Producer limit");
    const auto& b=budget.action;
    need(b.operations<=Limits::operations&&b.advancements<=Limits::advancements&&b.effects<=Limits::effects&&
        b.randomDraws<=Limits::randomDraws&&b.encounterWork<=Limits::encounterWork&&b.reactions<=Limits::reactions&&
        b.reactionDepth<=Limits::reactionDepth,ErrorCode::Limit,"AI action budget exceeds hard cap");
    std::vector<const CombatOption*> ordered;
    for(const auto& option:options){
        need(option.action.actor==actor,ErrorCode::InvalidAction,"AI option has a different actor");
        ordered.push_back(&option);
    }
    std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a->id<b->id;});
    for(std::size_t i=1;i<ordered.size();++i)need(ordered[i-1]->id!=ordered[i]->id,ErrorCode::InvalidId,"Duplicate AI option ID");
    CombatPlan plan;plan.world_=world;plan.stop_=stop;plan.truncated_=options.size()>budget.evaluations;
    auto cancel=[&]{if(!stop.stop_requested())return false;plan.command_.reset();plan.selected_.reset();plan.status_=CombatPlanStatus::Cancelled;return true;};
    if(cancel())return plan;
    std::optional<std::int64_t> best;
    for(std::size_t i=0;i<std::min(ordered.size(),budget.evaluations);++i){
        if(cancel())return plan;
        const auto& option=*ordered[i];auto cmd=command(producer,sequence,{option.action});
        std::optional<Transaction> trial;
        try {trial.emplace(prepareUnlocked(cmd,budget.action,world));}
        catch(const Error& e){
            if(e.code()==ErrorCode::Reentrant)throw;
            plan.evaluations_.push_back({option.id,e.code(),{}});continue;
        }
        if(cancel())return plan;
        auto value=score({*state_,trial->state(),trial->results(),option});
        if(cancel())return plan;
        plan.evaluations_.push_back({option.id,{},value});
        if(value&&(!best||*value>*best)){best=value;plan.selected_=option.id;plan.command_=std::move(cmd);}
    }
    if(cancel())return plan;
    if(plan.command_){plan.status_=CombatPlanStatus::Ready;return plan;}
    const auto& encounter=*state_->encounter;
    auto fallback=command(producer,sequence,{EndTurn{actor,encounter.epoch,encounter.turnEpoch}});
    try {auto trial=prepareUnlocked(fallback,budget.action,world);}
    catch(const Error& e){if(e.code()==ErrorCode::Reentrant)throw;plan.fallbackRejection_=e.code();cancel();return plan;}
    if(cancel())return plan;
    plan.command_=std::move(fallback);plan.status_=CombatPlanStatus::Ready;return plan;
}
Transaction Runtime::prepareCombat(const CombatPlan& plan,WorldStamp world,Budget budget){
    Guard guard(busy_);
    need(plan.status_==CombatPlanStatus::Ready&&plan.command_.has_value(),ErrorCode::InvalidAction,"Combat plan has no action");
    need(!plan.stop_.stop_requested(),ErrorCode::InvalidAction,"Combat plan cancelled");
    need(world==plan.world_,ErrorCode::StaleWorld,"Combat planning world changed");
    auto tx=prepareUnlocked(*plan.command_,budget,world);
    tx.world_=world; // End-turn fallback is also tied to the planning world.
    tx.stop_=plan.stop_;
    return tx;
}
} // namespace engine::rpg
