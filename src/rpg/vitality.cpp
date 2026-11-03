#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <type_traits>

namespace engine::rpg {
namespace {
void need(bool ok,const char* message){if(!ok)throw Error(ErrorCode::InvalidVitality,message);}
bool defeated(LifeState value){return value==LifeState::Downed||value==LifeState::Dead;}
void record(Results& results,EntityId entity,VitalEvent event,std::int64_t requested,std::int64_t applied,LifeState before,LifeState after){
    if(results.vitality.size()>=Limits::results)throw Error(ErrorCode::Limit,"Vitality result limit exceeded");
    results.vitality.push_back({entity,event,requested,applied,before,after});
}
}
bool canAct(const State& state,EntityId entity) noexcept {
    if(!state.characters.contains(entity))return false;
    auto v=state.vitality.find(entity);return v==state.vitality.end()||v->second.condition==LifeState::Alive;
}
bool teamDefeated(const State& state,const ContentId& team){
    if(!state.encounter)return false;bool found=false;
    for(const auto&[entity,member]:state.encounter->teams)if(member.team==team){found=true;if(canAct(state,entity))return false;}
    return found;
}
void validateVitality(const State& state){
    need(state.vitality.size()<=Limits::characters,"Too many vitality profiles");
    for(const auto&[entity,v]:state.vitality){
        auto ch=state.characters.find(entity);need(ch!=state.characters.end(),"Missing vitality character");
        auto hp=ch->second.resources.find(v.resource);need(hp!=ch->second.resources.end(),"Missing vitality resource");
        need(defeated(v.onZero),"Invalid depletion policy");
        need(v.condition==LifeState::Alive||defeated(v.condition),"Invalid life state");
        need(v.condition==LifeState::Alive?hp->second.current>0:hp->second.current==0,"Vitality/resource state mismatch");
    }
    if(state.encounter&&state.encounter->active)need(canAct(state,*state.encounter->active),"Incapacitated active actor");
}
void reconcileVitality(State& state,Results& results,EntityId entity){
    auto it=state.vitality.find(entity);if(it==state.vitality.end())return;
    auto& v=it->second;const auto hp=state.characters.at(entity).resources.at(v.resource).current;
    if(v.condition==LifeState::Alive&&hp==0){v.condition=v.onZero;record(results,entity,VitalEvent::Depleted,0,0,LifeState::Alive,v.condition);}
    else need(v.condition==LifeState::Alive||hp==0,"Incapacitated characters require explicit revival");
}
void configureVitality(State& state,Results& results,const ConfigureVitality& op){
    need(!state.encounter,"Configure vitality outside encounters");
    need(!state.vitality.contains(op.entity),"Vitality is already configured");
    need(defeated(op.onZero),"Invalid depletion policy");
    auto ch=state.characters.find(op.entity);need(ch!=state.characters.end(),"Unknown vitality character");
    auto hp=ch->second.resources.find(op.resource);need(hp!=ch->second.resources.end(),"Unknown vitality resource");
    auto condition=hp->second.current>0?LifeState::Alive:op.onZero;
    state.vitality.emplace(op.entity,VitalityState{op.resource,condition,op.onZero});
    record(results,op.entity,VitalEvent::Configured,0,0,condition,condition);
}
void applyVitalEffect(State& state,Results& results,const Catalog& catalog,const VitalEffect& effect,std::size_t& budget){
    if(!budget)throw Error(ErrorCode::BudgetExceeded,"Vitality effect budget exhausted");--budget;
    std::visit([&](const auto& op){
        using T=std::decay_t<decltype(op)>;
        auto it=state.vitality.find(op.entity);need(it!=state.vitality.end(),"Effect requires configured vitality");
        auto& v=it->second;auto& ch=state.characters.at(op.entity);auto& hp=ch.resources.at(v.resource);
        const auto before=v.condition;const auto previous=hp.current;std::int64_t requested=0;VitalEvent event;
        if constexpr(std::is_same_v<T,DefeatEffect>){
            need(defeated(op.condition),"Defeat must select Downed or Dead");
            need(v.condition!=LifeState::Dead||op.condition==LifeState::Dead,"Defeat cannot undo death");
            v.condition=op.condition;hp.current=0;event=VitalEvent::Defeated;
        }else {
            const auto& defs=catalog.character(ch.definition)->resources;
            auto def=std::find_if(defs.begin(),defs.end(),[&](const auto& d){return d.id==v.resource;});
            need(def!=defs.end(),"Missing vitality definition");
            if(op.amount.value<0||op.amount.unit!=def->unit||op.amount.scale!=def->scale)
                throw Error(ErrorCode::InvalidAmount,"Invalid vitality magnitude/unit/scale");
            requested=op.amount.value;
            if constexpr(std::is_same_v<T,DamageEffect>){
                hp.current-=std::min(hp.current,requested);event=VitalEvent::Damaged;
                if(v.condition==LifeState::Alive&&hp.current==0)v.condition=v.onZero;
            }else if constexpr(std::is_same_v<T,HealEffect>){
                need(v.condition==LifeState::Alive,"Healing does not revive");
                hp.current+=std::min(hp.maximum-hp.current,requested);event=VitalEvent::Healed;
            }else {
                need(defeated(v.condition)&&(v.condition!=LifeState::Dead||op.allowDead),"Revival condition rejected");
                need(requested>0&&hp.maximum>0,"Revival needs positive health");
                hp.current=std::min(hp.maximum,requested);v.condition=LifeState::Alive;event=VitalEvent::Revived;
            }
        }
        const auto delta=hp.current-previous;
        const auto& defs=catalog.character(ch.definition)->resources;
        auto def=std::find_if(defs.begin(),defs.end(),[&](const auto& d){return d.id==v.resource;});
        results.resources.push_back({op.entity,v.resource,{delta,def->unit,def->scale}});
        record(results,op.entity,event,requested,delta<0?-delta:delta,before,v.condition);
    },effect);
}
} // namespace engine::rpg
