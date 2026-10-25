#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace engine::rpg {
void applyAbilities(State&, Results&, const Catalog&, SessionId, const Operation&, std::size_t&, std::size_t&);
void applyEncounter(State&, Results&, const Catalog&, SessionId, const Operation&, std::size_t&, std::size_t&, std::size_t&);
void reconcileEncounter(State&, Results&, const Catalog&, SessionId, std::size_t&, std::size_t&, std::size_t&);
void applyTacticalEffect(State&, Results&, const Catalog&, const Effect&, std::size_t&);
namespace {
void need(bool ok, ErrorCode code, const char* message) { if (!ok) throw Error(code,message); }
void spend(std::size_t& value) { need(value != 0,ErrorCode::BudgetExceeded,"Tactical work budget exhausted"); --value; }
std::int64_t sum(std::int64_t a,std::int64_t b) {
    need(a >= 0 && b >= 0,ErrorCode::InvalidAmount,"Negative tactical cost");
    need(a <= std::numeric_limits<std::int64_t>::max()-b,ErrorCode::Overflow,"Tactical cost overflow");return a+b;
}
bool member(const EncounterState& e,EntityId id) { return std::binary_search(e.roster.begin(),e.roster.end(),id); }
void spatial(const State& state,const TacticalAction& op) {
    const auto& e=*state.encounter;
    const auto& snapshot=op.spatial->snapshot();
    need(snapshot.entities.size()==e.roster.size(),ErrorCode::InvalidSpatial,"Spatial snapshot must cover the complete roster");
    for(std::size_t i=0;i<e.roster.size();++i) {
        const auto& entry=snapshot.entities[i];const auto pos=state.positions.find(entry.entity);
        need(entry.entity==e.roster[i] && entry.resident && pos!=state.positions.end() && pos->second==entry.position,
            ErrorCode::InvalidSpatial,"Missing, nonresident or stale tactical position");
    }
    for(auto target:snapshot.legalTargets) need(member(e,target),ErrorCode::InvalidSpatial,"Spatial target outside roster");
    if(const auto* use=std::get_if<UseAbility>(&op.action)) {
        need(!op.spatial->path(),ErrorCode::InvalidSpatial,"Ability quote must not carry a movement path");
        need(use->entity==op.actor,ErrorCode::InvalidAction,"Ability caster differs from active actor");
        if(snapshot.targeting){
            auto selected=selectTargets(state,snapshot,op.actor,*snapshot.targeting);
            auto supplied=use->targets;std::sort(supplied.begin(),supplied.end());
            need(supplied==selected,ErrorCode::InvalidTarget,"Ability targets differ from complete area selection");
        }
        if(!snapshot.targeting||snapshot.targeting->requireLineOfSight)
            for(auto target:use->targets)
                need(std::binary_search(snapshot.legalTargets.begin(),snapshot.legalTargets.end(),target),
                    ErrorCode::InvalidTarget,"Target failed spatial query");
        const auto cooldowns=state.cooldowns.find(op.actor);
        need(cooldowns==state.cooldowns.end() || !cooldowns->second.contains(use->ability),ErrorCode::InvalidAction,"Ability is cooling down");
    } else {
        need(!snapshot.targeting,ErrorCode::InvalidSpatial,"Movement quote cannot contain ability targeting");
        need(op.spatial->path().has_value(),ErrorCode::InvalidSpatial,"Movement requires a path");
        const auto& path=*op.spatial->path();
        need(path.reachable && path.destinationResident && path.destinationFree && path.cost>=0 &&
            path.route.size()>=2 && path.route.size()<=Limits::pathPoints &&
            path.start==state.positions.at(op.actor) && path.start!=path.destination &&
            path.route.front()==path.start && path.route.back()==path.destination,
            ErrorCode::InvalidSpatial,"Blocked, missing, occupied or malformed path");
        for(const auto& entry:snapshot.entities)
            need(entry.entity==op.actor || entry.position!=path.destination,ErrorCode::InvalidSpatial,"Destination occupied by a participant");
    }
}
}
std::shared_ptr<const SpatialQuote> Runtime::spatialQuote(EntityId actor,SpatialSnapshot snapshot,std::optional<PathQuote> path) const {
    need(!busy_,ErrorCode::Reentrant,"Cannot issue spatial quotes during preparation");
    need(!state_->presentation,ErrorCode::PresentationPending,"Presentation is pending");
    need(state_->encounter && state_->encounter->stage==EncounterStage::AwaitingCommand && state_->encounter->active==actor,
        ErrorCode::InvalidAction,"Spatial quote requires the active encounter actor");
    need(snapshot.world.identity && snapshot.world.revision,ErrorCode::InvalidSpatial,"World identity/revision must be nonzero");
    need(snapshot.entities.size()<=Limits::participants && snapshot.legalTargets.size()<=Limits::targets &&
        (!path || path->route.size()<=Limits::pathPoints),ErrorCode::Limit,"Spatial quote exceeds bounds");
    std::sort(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& a,const auto& b){return a.entity<b.entity;});
    for(std::size_t i=1;i<snapshot.entities.size();++i)
        need(snapshot.entities[i-1].entity!=snapshot.entities[i].entity,ErrorCode::InvalidSpatial,"Duplicate spatial actor");
    std::sort(snapshot.legalTargets.begin(),snapshot.legalTargets.end());
    need(std::adjacent_find(snapshot.legalTargets.begin(),snapshot.legalTargets.end())==snapshot.legalTargets.end(),ErrorCode::InvalidSpatial,"Duplicate spatial target");
    return std::shared_ptr<const SpatialQuote>(new SpatialQuote(owner_,state_->generation,state_->revision,
        state_->encounter->epoch,state_->encounter->turnEpoch,actor,std::move(snapshot),std::move(path)));
}
void applyTactics(State& state,Results& results,const Catalog& catalog,SessionId session,const TacticalAction& op,
    Budget budget,std::size_t& effects,std::size_t& draws,std::size_t& work) {
    need(state.encounter && state.encounter->stage==EncounterStage::AwaitingCommand && state.encounter->active==op.actor,
        ErrorCode::InvalidAction,"Tactical action requires the active actor");
    auto& e=*state.encounter;
    need(e.epoch==op.encounterEpoch && e.turnEpoch==op.turnEpoch,ErrorCode::StaleTurn,"Stale tactical turn");
    const auto& handler=*catalog.bindings()->registry().findEncounter(e.policy.id);
    need(bool(handler.authorize),ErrorCode::MissingHandler,"Encounter policy has no tactical authorization");
    const TacticalContext context{state,op};
    auto eligible=[&](EntityId actor) {spend(work);return handler.eligible({state,e},actor);};
    need(eligible(op.actor),ErrorCode::InvalidAction,"Actor is incapacitated");
    spatial(state,op);
    // Discovery is pure. A token is queued/fired at most once across both phases.
    struct Queued { ReactionToken token; std::uint32_t depth; };
    std::set<ReactionToken> seen;
    std::size_t remaining=budget.reactions;
    auto score=[&](EntityId actor) {
        auto it=std::find_if(e.order.begin(),e.order.end(),[&](const auto& entry){return entry.entity==actor;});
        need(it!=e.order.end(),ErrorCode::InvalidAction,"Reaction source has no initiative entry");return it->score;
    };
    auto reactions=[&](ReactionPhase phase) {
        if(!handler.reactions)return;
        std::vector<Queued> queue;
        auto discover=[&](const std::optional<ReactionToken>& parent,std::uint32_t depth) {
            spend(work);auto proposals=handler.reactions(context,phase,parent);
            need(proposals.size()<=Limits::reactions,ErrorCode::Limit,"Reaction proposal limit exceeded");
            std::sort(proposals.begin(),proposals.end());
            need(std::adjacent_find(proposals.begin(),proposals.end())==proposals.end(),ErrorCode::InvalidAction,"Duplicate reaction token in proposal");
            for(auto& token:proposals) {
                spend(work);
                need(member(e,token.source),ErrorCode::InvalidTarget,"Reaction source outside roster");
                if(seen.contains(token))continue;
                need(depth<=budget.reactionDepth,ErrorCode::BudgetExceeded,"Reaction depth exceeded");
                spend(remaining);seen.insert(token);queue.push_back({std::move(token),depth});
            }
        };
        discover({},1);
        while(!queue.empty()) {
            std::sort(queue.begin(),queue.end(),[&](const auto& a,const auto& b) {
                const auto as=score(a.token.source),bs=score(b.token.source);
                return as==bs?a.token<b.token:as>bs;
            });
            auto current=std::move(queue.front());queue.erase(queue.begin());
            if(!eligible(current.token.source))continue;
            spend(work);Random random(state.random,draws);
            auto proposal=handler.react({context,phase,current.token,current.depth},random);
            need(!random.exhausted(),ErrorCode::BudgetExceeded,"Reaction exhausted random budget");
            need(proposal.accepted,ErrorCode::RejectedEffect,"Reaction rejected");
            need(proposal.effects.size()<=Limits::effects,ErrorCode::Limit,"Reaction effect limit exceeded");
            need(proposal.effects.size()<=effects,ErrorCode::BudgetExceeded,"Reaction effect budget exceeded");
            std::set<std::pair<EntityId,ContentId>> statusSources;
            for(const auto& effect:proposal.effects) {
                std::visit([&](const auto& value) {
                    need(member(e,value.entity),ErrorCode::InvalidTarget,"Reaction effect escaped roster");
                    using T=std::decay_t<decltype(value)>;
                    if constexpr (!std::is_same_v<T,ResourceEffect>)
                        need(statusSources.emplace(value.entity,value.source).second,ErrorCode::InvalidStatus,"Duplicate reaction status source");
                },effect);
                applyTacticalEffect(state,results,catalog,effect,effects);
            }
            results.reactions.push_back({current.token,phase,current.depth});
            discover(current.token,current.depth+1);
        }
    };
    reactions(ReactionPhase::BeforeAction);
    need(eligible(op.actor),ErrorCode::InvalidAction,"Before-action reaction incapacitated actor");
    // Recheck cooldowns and spatial facts after before-action reactions.
    spatial(state,op);
    spend(work);auto plan=handler.authorize(context);
    need(plan.accepted,ErrorCode::InvalidAction,"Policy rejected tactical action");
    need(plan.ap>=0 && plan.movement>=0,ErrorCode::InvalidAmount,"Negative action allowance cost");
    const auto movement=sum(plan.movement,op.spatial->path()?op.spatial->path()->cost:0);
    need(plan.ap<=e.allowance.ap && movement<=e.allowance.movement,ErrorCode::InvalidAmount,"Insufficient turn allowance");
    need(plan.resources.size()<=Limits::effects,ErrorCode::Limit,"Too many tactical resource costs");
    std::sort(plan.resources.begin(),plan.resources.end(),[](const auto& a,const auto& b){return a.resource<b.resource;});
    for(std::size_t i=0;i<plan.resources.size();++i) {
        need(plan.resources[i].amount.value>=0,ErrorCode::InvalidAmount,"Negative resource cost");
        need(!i || plan.resources[i-1].resource!=plan.resources[i].resource,ErrorCode::InvalidAmount,"Duplicate resource cost");
    }
    if(plan.cooldown) {
        const auto it=state.cooldowns.find(op.actor);
        need(it==state.cooldowns.end() || !it->second.contains(plan.cooldown->key),ErrorCode::InvalidAction,"Action is cooling down");
        need(plan.cooldown->duration>0,ErrorCode::InvalidAmount,"Cooldown duration must be positive");
    }
    e.allowance.ap-=plan.ap;e.allowance.movement-=movement;
    for(const auto& cost:plan.resources) {
        auto amount=cost.amount;amount.value=-amount.value;
        applyTacticalEffect(state,results,catalog,ResourceEffect{op.actor,cost.resource,amount},effects);
    }
    std::optional<ContentId> ability;
    std::optional<TacticalPosition> destination;
    std::vector<TacticalPosition> route;
    if(const auto* use=std::get_if<UseAbility>(&op.action)) {
        ability=use->ability;
        applyAbilities(state,results,catalog,session,*use,effects,draws);
    } else {
        const auto& path=*op.spatial->path();destination=path.destination;route=path.route;
        state.positions.at(op.actor)=path.destination;
    }
    if(plan.cooldown)applyEncounter(state,results,catalog,session,
        SetCooldown{op.actor,plan.cooldown->key,plan.cooldown->phase,plan.cooldown->duration},effects,draws,work);
    reactions(ReactionPhase::AfterAction);
    reconcileEncounter(state,results,catalog,session,effects,draws,work);
    need(state.actionEpoch!=std::numeric_limits<std::uint64_t>::max(),ErrorCode::Overflow,"Action epoch exhausted");
    ++state.actionEpoch;
    state.presentation=PendingPresentation{state.actionEpoch,op.encounterEpoch,op.turnEpoch,op.actor,op.spatial->snapshot().world,destination};
    results.actions.push_back({*state.presentation,std::move(ability),plan.ap,movement,std::move(route)});
}
} // namespace engine::rpg
