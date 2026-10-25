#include "../examples/rpg/tactics.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace r=engine::rpg;
namespace {
int checks=0;
const r::WorldStamp world{10,1};
r::ContentId id(const char*s){return r::ContentId(s);}
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
template<class F>void rejects(r::ErrorCode code,F fn){try{fn();}catch(const r::Error&e){check(e.code()==code,e.what());return;}throw std::runtime_error("Expected rejection");}
r::Transaction prepare(r::Runtime&run,std::vector<r::Operation> ops,r::Budget budget={},std::optional<r::WorldStamp> stamp={}){
    auto i=run.state().producers.find(id("test.input"));return run.prepare(run.command(id("test.input"),i==run.state().producers.end()?1:i->second+1,std::move(ops)),budget,stamp);
}
r::Results apply(r::Runtime&run,std::vector<r::Operation> ops){auto tx=prepare(run,std::move(ops));auto result=tx.results();check(run.commit(std::move(tx)),"commit");return result;}
std::shared_ptr<const r::Catalog> catalog(){auto defs=example::abilityDefinitions();defs.abilities[0].targets.maximum=16;return example::encounterCatalog(example::tacticalRules(),defs);}
struct Fixture {
    r::Runtime run{catalog(),r::SessionId(1),42};std::vector<r::EntityId> actors;
    Fixture(){
        for(auto p:std::vector<r::TacticalPosition>{{0,0,0},{4,0,0},{3,0,1},{3,0,3},{0,0,4},{-2,0,0},{3,4,0},{4,0,1},{5,0,1}}){
            auto a=apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0];actors.push_back(a);apply(run,{r::SetTacticalPosition{a,p}});
        }
        r::BeginEncounter begin{{id("example.turns"),1},actors};
        for(std::size_t i=0;i<actors.size();++i)begin.teams.emplace(actors[i],r::CombatMember{id(i==0||i==3?"test.blue":"test.red")});
        apply(run,{begin});
    }
    r::SpatialSnapshot snapshot()const{r::SpatialSnapshot s{world};for(auto a:actors){s.entities.push_back({a,run.state().positions.at(a),true});s.legalTargets.push_back(a);}return s;}
    r::TargetQuery sphere()const{return {r::SphereTargets{{3,0,0},1},3,r::TargetTeam::Opponents,false,true};}
    r::TacticalAction action(r::SpatialSnapshot s,std::vector<r::EntityId> targets){const auto&e=*run.state().encounter;return {actors[0],e.epoch,e.turnEpoch,r::UseAbility{actors[0],id("example.strike"),targets,{}},run.spatialQuote(actors[0],std::move(s))};}
};
void geometry(){
    Fixture f;auto s=f.snapshot();auto q=f.sphere();auto before=f.run.save();
    auto select=[&](const r::TargetQuery& query){return r::selectTargets(f.run.state(),s,f.actors[0],query);};
    check(select(q)==std::vector{f.actors[1],f.actors[2]},"Sphere inclusive boundary/3D selection");
    std::reverse(s.entities.begin(),s.entities.end());std::reverse(s.legalTargets.begin(),s.legalTargets.end());
    check(select(q)==std::vector{f.actors[1],f.actors[2]},"Input ordering changed target order");
    q={r::ConeTargets{{0,0,0},{1000,0,0},5},0,r::TargetTeam::Any,false,true};
    check(select(q)==std::vector{f.actors[1],f.actors[2],f.actors[3],f.actors[7]},"Cone edge/back/height/range");
    q.team=r::TargetTeam::Opponents;check(select(q)==std::vector{f.actors[1],f.actors[2],f.actors[7]},"Opponent filter");
    q={r::LineTargets{{0,0,0},{1,0,0},4,1},0};
    check(select(q)==std::vector{f.actors[1],f.actors[2],f.actors[7]},"Line caps/radius");
    q={r::ConeTargets{{0,0,0},{1,0,1},5},0};auto diagonal=select(q);check(std::find(diagonal.begin(),diagonal.end(),f.actors[3])!=diagonal.end(),"Diagonal cone");
    q={r::SingleTarget{f.actors[0]},0};check(select(q).empty(),"Self must be explicit");q.includeSelf=true;check(select(q)==std::vector{f.actors[0]},"Self inclusion");
    q={r::SingleTarget{f.actors[3]},5,r::TargetTeam::Allies};check(select(q)==std::vector{f.actors[3]},"Ally single target");
    q=f.sphere();s.legalTargets.erase(std::find(s.legalTargets.begin(),s.legalTargets.end(),f.actors[1]));
    check(select(q)==std::vector{f.actors[2]},"LOS filter");q.requireLineOfSight=false;check(select(q)==std::vector{f.actors[1],f.actors[2]},"Explicit LOS opt-out");
    check(f.run.save()==before,"Preview changed state/RNG/epochs");
    auto state=f.run.state();state.encounter->teams.clear();check(r::selectTargets(state,s,f.actors[0],f.sphere()).empty(),"Unassigned treated as opponent");
}
void invalid(){
    Fixture f;auto s=f.snapshot();auto q=f.sphere();
    auto fail=[&](r::ErrorCode code){rejects(code,[&]{r::selectTargets(f.run.state(),s,f.actors[0],q);});};
    q.range=2;fail(r::ErrorCode::InvalidTarget);q=f.sphere();
    q.shape=r::SphereTargets{{0,0,0},-1};fail(r::ErrorCode::InvalidTarget);
    q.shape=r::ConeTargets{{0,0,0},{0,0,0},1};fail(r::ErrorCode::InvalidTarget);
    q.shape=r::LineTargets{{0,0,0},{1001,0,0},1,1};fail(r::ErrorCode::InvalidSpatial);
    q.shape=r::SphereTargets{{std::numeric_limits<std::int64_t>::min(),0,0},0};fail(r::ErrorCode::InvalidSpatial);
    q=f.sphere();q.range=r::targetingExtentLimit+1;fail(r::ErrorCode::InvalidTarget);
    q=f.sphere();q.team=static_cast<r::TargetTeam>(99);fail(r::ErrorCode::InvalidTarget);q=f.sphere();
    s.entities[0].position.x++;fail(r::ErrorCode::InvalidSpatial);s=f.snapshot();
    s.entities[0].resident=false;fail(r::ErrorCode::InvalidSpatial);s=f.snapshot();
    s.entities[1]=s.entities[0];fail(r::ErrorCode::InvalidSpatial);s=f.snapshot();
    s.legalTargets.push_back(f.actors[0]);fail(r::ErrorCode::InvalidSpatial);s=f.snapshot();
    s.world.revision=0;fail(r::ErrorCode::InvalidSpatial);s=f.snapshot();
    s.entities.pop_back();fail(r::ErrorCode::InvalidSpatial);s=f.snapshot();
    q.shape=r::SingleTarget{{r::SessionId(2),1}};fail(r::ErrorCode::InvalidTarget);
    auto state=f.run.state();for(std::size_t i=0;i<s.entities.size();++i){auto p=i?r::TacticalPosition{500000,500000,500000}:r::TacticalPosition{-500000,-500000,-500000};s.entities[i].position=p;state.positions.at(s.entities[i].entity)=p;}
    q={r::LineTargets{{-500000,-500000,-500000},{1000,1000,1000},1000000,1000000},0};
    check(r::selectTargets(state,s,f.actors[0],q).empty(),"Large exact intermediates overflowed");
}
void atomicArea(){
    Fixture f;auto s=f.snapshot();s.targeting=f.sphere();auto expected=r::selectTargets(f.run.state(),s,f.actors[0],*s.targeting);auto original=f.run.save();
    auto reject=[&](r::TacticalAction action,r::ErrorCode code,r::Budget budget={}){rejects(code,[&]{prepare(f.run,{action},budget,world);});check(f.run.save()==original,"Rejected area action mutated state/RNG");};
    reject(f.action(s,{f.actors[1]}),r::ErrorCode::InvalidTarget);
    reject(f.action(s,{f.actors[1],f.actors[2],f.actors[3]}),r::ErrorCode::InvalidTarget);
    reject(f.action(s,{f.actors[1],f.actors[1]}),r::ErrorCode::InvalidTarget);
    reject(f.action(s,std::vector<r::EntityId>(r::Limits::targets+1,f.actors[1])),r::ErrorCode::Limit);
    auto action=f.action(s,expected);r::Budget budget;budget.effects=1;reject(action,r::ErrorCode::BudgetExceeded,budget);
    auto tx=prepare(f.run,{action},{},world);check(f.run.save()==original,"Area prepare published");
    check(!f.run.commit(std::move(tx),r::WorldStamp{10,2}),"Stale world accepted area");
    check(f.run.save()==original,"Stale publication changed state");
    const auto resource=id("example.reserve");auto first=f.run.state().characters.at(f.actors[1]).resources.at(resource).current;
    check(f.run.commit(std::move(tx),world),"Area commit");
    check(f.run.state().characters.at(f.actors[1]).resources.at(resource).current<first&&f.run.state().characters.at(f.actors[2]).resources.at(resource).current<first,"Area missed a target");
    check(f.run.state().characters.at(f.actors[3]).resources.at(resource).current==first,"Area damaged ally");
    check(f.run.state().encounter->allowance.ap==1,"Area paid AP per target");
    check(f.run.state().characters.at(f.actors[0]).resources.at(resource).current==first-1,"Area paid resource cost per target");
    auto p=*f.run.state().presentation;apply(f.run,{r::AcknowledgePresentation{p.actionEpoch,p.encounterEpoch,p.turnEpoch}});
    rejects(r::ErrorCode::InvalidSpatial,[&]{prepare(f.run,{action},{},world);});
    auto saved=f.run.save();f.run.restore(saved);check(f.run.state().encounter->teams.size()==f.actors.size(),"L3 altered durable team state");
    Fixture through;auto facts=through.snapshot();facts.legalTargets.clear();facts.targeting=through.sphere();facts.targeting->requireLineOfSight=false;
    auto targets=r::selectTargets(through.run.state(),facts,through.actors[0],*facts.targeting);
    auto candidate=prepare(through.run,{through.action(facts,targets)},{},world);
    check(through.run.commit(std::move(candidate),world),"Explicit LOS opt-out preview/execution mismatch");
}
}
int main(){try{geometry();invalid();atomicArea();std::cout<<"L3 targeting checks: "<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
