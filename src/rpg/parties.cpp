#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <set>

namespace engine::rpg {
namespace {
void need(bool ok, ErrorCode code, const char* message) { if(!ok)throw Error(code,message); }
}
bool validControl(ControlMode control) noexcept {
    return control==ControlMode::Player||control==ControlMode::AI||control==ControlMode::Scripted;
}
void validateParties(const State& state) {
    need(state.parties.size()<=Limits::parties,ErrorCode::Limit,"Party limit exceeded");
    std::set<EntityId> assigned;
    for(const auto&[id,party]:state.parties){
        need(party.capacity>0&&party.capacity<=Limits::partyMembers&&party.activeLimit>0&&party.activeLimit<=party.capacity,
             ErrorCode::InvalidParty,"Invalid party capacity or active limit");
        need(party.members.size()<=party.capacity,ErrorCode::Capacity,"Party capacity exceeded");
        std::size_t active=0;bool leader=!party.leader;
        for(const auto& member:party.members){
            need(state.characters.contains(member.entity),ErrorCode::MissingEntity,"Unknown party member");
            need(assigned.insert(member.entity).second,ErrorCode::InvalidParty,"Duplicate party membership");
            need(validControl(member.control),ErrorCode::InvalidParty,"Invalid party control mode");
            active+=member.active;
            if(party.leader==member.entity&&member.active)leader=true;
        }
        need(active<=party.activeLimit,ErrorCode::Capacity,"Active party capacity exceeded");
        need(leader,ErrorCode::InvalidParty,"Party leader must be an active member");
    }
}
void validateTeams(const State& state,const EncounterState& encounter) {
    if(encounter.teams.empty())return;
    need(encounter.teams.size()==encounter.roster.size(),ErrorCode::InvalidTeam,"Teams must cover the encounter roster");
    std::set<ContentId> teams;
    for(const auto&[entity,member]:encounter.teams){
        need(state.characters.contains(entity)&&std::binary_search(encounter.roster.begin(),encounter.roster.end(),entity),
             ErrorCode::InvalidTeam,"Team member is not a participant");
        need(validControl(member.control),ErrorCode::InvalidTeam,"Invalid combat control mode");
        teams.insert(member.team);
    }
    need(teams.size()<=Limits::teams,ErrorCode::Limit,"Combat team limit exceeded");
}
void applyParty(State& state,Results& results,const Operation& operation) {
    need(!state.encounter,ErrorCode::InvalidParty,"Party changes require exiting the encounter");
    if(const auto* set=std::get_if<SetParty>(&operation)){
        state.parties.insert_or_assign(set->party,set->state);
        validateParties(state);results.parties.push_back({set->party,false});
    }else if(const auto* remove=std::get_if<RemoveParty>(&operation)){
        need(state.parties.erase(remove->party)!=0,ErrorCode::InvalidParty,"Unknown party");
        results.parties.push_back({remove->party,true});
    }
}
std::optional<ContentId> partyOf(const State& state,EntityId entity) {
    for(const auto&[id,party]:state.parties)
        for(const auto& member:party.members)if(member.entity==entity)return id;
    return {};
}
std::vector<EntityId> activePartyMembers(const State& state,const ContentId& id) {
    const auto it=state.parties.find(id);
    need(it!=state.parties.end(),ErrorCode::InvalidParty,"Unknown party");
    std::vector<EntityId> result;
    for(const auto& member:it->second.members)if(member.active)result.push_back(member.entity);
    return result;
}
TeamRelation teamRelation(const State& state,EntityId a,EntityId b) noexcept {
    if(!state.encounter)return TeamRelation::Unassigned;
    const auto& teams=state.encounter->teams;auto x=teams.find(a),y=teams.find(b);
    if(x==teams.end()||y==teams.end())return TeamRelation::Unassigned;
    return x->second.team==y->second.team?TeamRelation::Ally:TeamRelation::Opponent;
}
bool controlledBy(const State& state,EntityId entity,ControlMode mode) noexcept {
    if(!state.encounter)return false;
    const auto it=state.encounter->teams.find(entity);
    return it!=state.encounter->teams.end()&&it->second.control==mode;
}
} // namespace engine::rpg
