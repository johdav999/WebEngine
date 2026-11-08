#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <set>

namespace engine::rpg {
namespace {
void need(bool ok,ErrorCode code,const char* message){if(!ok)throw Error(code,message);}
void coordinate(TacticalPosition p,std::int64_t limit=targetingCoordinateLimit){
    need(p.x>=-limit&&p.x<=limit&&p.y>=-limit&&p.y<=limit&&p.z>=-limit&&p.z<=limit,
         ErrorCode::InvalidSpatial,"Targeting coordinate/direction outside exact geometry bounds");
}
void extent(std::int64_t n){need(n>=0&&n<=targetingExtentLimit,ErrorCode::InvalidTarget,"Invalid targeting extent");}
TacticalPosition difference(TacticalPosition a,TacticalPosition b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
std::int64_t dot(TacticalPosition a,TacticalPosition b){return a.x*b.x+a.y*b.y+a.z*b.z;}
std::uint64_t square(std::int64_t n){return static_cast<std::uint64_t>(n*n);}
std::uint64_t norm(TacticalPosition p){return static_cast<std::uint64_t>(dot(p,p));}
}
std::vector<EntityId> selectTargets(const State& state,const SpatialSnapshot& snapshot,EntityId actor,const TargetQuery& query){
    need(state.encounter.has_value(),ErrorCode::InvalidEncounter,"Targeting requires an encounter");
    const auto& roster=state.encounter->roster;
    need(roster.size()<=Limits::participants&&snapshot.entities.size()<=Limits::participants&&snapshot.legalTargets.size()<=Limits::targets,
         ErrorCode::Limit,"Target query exceeds bounds");
    need(snapshot.world.identity&&snapshot.world.revision,ErrorCode::InvalidSpatial,"Targeting world stamp is missing");
    need(std::binary_search(roster.begin(),roster.end(),actor),ErrorCode::InvalidTarget,"Targeting actor outside roster");
    need(snapshot.entities.size()==roster.size(),ErrorCode::InvalidSpatial,"Targeting requires complete roster facts");
    std::map<EntityId,TacticalPosition> positions;
    for(const auto& entry:snapshot.entities){
        coordinate(entry.position);
        auto p=state.positions.find(entry.entity);
        need(std::binary_search(roster.begin(),roster.end(),entry.entity)&&entry.resident&&p!=state.positions.end()&&p->second==entry.position&&positions.emplace(entry.entity,entry.position).second,
             ErrorCode::InvalidSpatial,"Missing, duplicate, nonresident or stale targeting actor");
    }
    std::set<EntityId> visible;
    for(auto id:snapshot.legalTargets)
        need(positions.contains(id)&&visible.insert(id).second,ErrorCode::InvalidSpatial,"Invalid or duplicate LOS target");
    need(query.team==TargetTeam::Any||query.team==TargetTeam::Allies||query.team==TargetTeam::Opponents,
         ErrorCode::InvalidTarget,"Unknown target team filter");
    extent(query.range);
    const auto origin=std::visit([&](const auto& shape)->TacticalPosition{
        using T=std::decay_t<decltype(shape)>;
        if constexpr(std::is_same_v<T,SingleTarget>){
            need(positions.contains(shape.entity),ErrorCode::InvalidTarget,"Selected entity outside roster");return positions.at(shape.entity);
        }else if constexpr(std::is_same_v<T,SphereTargets>){coordinate(shape.center);extent(shape.radius);return shape.center;}
        else {
            coordinate(shape.origin);coordinate(shape.direction,targetingDirectionLimit);extent(shape.length);
            need(norm(shape.direction)!=0,ErrorCode::InvalidTarget,"Targeting direction must be nonzero");
            if constexpr(std::is_same_v<T,LineTargets>)extent(shape.radius);
            return shape.origin;
        }
    },query.shape);
    need(norm(difference(origin,positions.at(actor)))<=square(query.range),ErrorCode::InvalidTarget,"Aim point outside cast range");
    std::vector<EntityId> selected;
    for(const auto&[id,position]:positions){
        if(id==actor&&!query.includeSelf)continue;
        if(query.requireLineOfSight&&!visible.contains(id))continue;
        auto relation=teamRelation(state,actor,id);
        if(query.team==TargetTeam::Allies&&relation!=TeamRelation::Ally)continue;
        if(query.team==TargetTeam::Opponents&&relation!=TeamRelation::Opponent)continue;
        const bool inside=std::visit([&](const auto& shape){
            using T=std::decay_t<decltype(shape)>;
            if constexpr(std::is_same_v<T,SingleTarget>)return id==shape.entity;
            else {
                const auto delta=difference(position,origin);const auto distance=norm(delta);
                if constexpr(std::is_same_v<T,SphereTargets>)return distance<=square(shape.radius);
                else {
                    const auto projection=dot(delta,shape.direction);if(projection<0)return false;
                    const auto axis=norm(shape.direction),along=square(projection);
                    if constexpr(std::is_same_v<T,ConeTargets>)return distance<=square(shape.length)&&2*along>=distance*axis;
                    else return along<=square(shape.length)*axis&&distance*axis-along<=square(shape.radius)*axis;
                }
            }
        },query.shape);
        if(inside)selected.push_back(id);
    }
    return selected;
}
} // namespace engine::rpg
