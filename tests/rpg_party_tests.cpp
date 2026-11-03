#include "../examples/rpg/encounters.hpp"
#include <iostream>
#include <stdexcept>

namespace r=engine::rpg;
namespace {
int checks=0;
r::ContentId id(const char*s){return r::ContentId(s);}
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
r::Transaction prepare(r::Runtime&run,std::vector<r::Operation> ops){
    auto i=run.state().producers.find(id("test.input"));
    return run.prepare(run.command(id("test.input"),i==run.state().producers.end()?1:i->second+1,std::move(ops)));
}
r::Results apply(r::Runtime&run,std::vector<r::Operation> ops){auto t=prepare(run,std::move(ops));auto result=t.results();check(run.commit(std::move(t)),"commit");return result;}
template<class F>void rejects(r::ErrorCode code,F fn){try{fn();}catch(const r::Error&e){check(e.code()==code,e.what());return;}throw std::runtime_error("Expected rejection");}
void reject(r::Runtime&run,r::ErrorCode code,std::vector<r::Operation> ops){auto before=&run.state();rejects(code,[&]{prepare(run,std::move(ops));});check(before==&run.state(),"Rejected command published state");}
void contracts(){
    auto catalog=example::encounterCatalog();r::Runtime run(catalog,r::SessionId(1),42);
    auto a=apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0];
    auto b=apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0];
    auto c=apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0];
    auto legacy=run.save();check(legacy[8]==1,"Legacy snapshot version changed");
    r::PartyState party{3,2,{{b,false,r::ControlMode::AI},{a,true,r::ControlMode::Player}},a};
    auto result=apply(run,{r::SetParty{id("test.party"),party}});
    check(result.parties.size()==1&&!result.parties[0].removed,"Party result");
    check(r::partyOf(run.state(),b)==id("test.party")&&!r::partyOf(run.state(),c),"Membership query");
    check(r::activePartyMembers(run.state(),id("test.party"))==std::vector{a},"Active authored order");
    auto bad=party;bad.capacity=1;reject(run,r::ErrorCode::InvalidParty,{r::SetParty{id("test.party"),bad}});
    bad=party;bad.members.push_back({a});reject(run,r::ErrorCode::InvalidParty,{r::SetParty{id("test.party"),bad}});
    bad=party;bad.activeLimit=0;reject(run,r::ErrorCode::InvalidParty,{r::SetParty{id("test.party"),bad}});
    bad=party;bad.leader=b;reject(run,r::ErrorCode::InvalidParty,{r::SetParty{id("test.party"),bad}});
    bad=party;bad.members[0].entity={r::SessionId(2),b.value};reject(run,r::ErrorCode::MissingEntity,{r::SetParty{id("test.party"),bad}});
    bad=party;bad.members[0].control=static_cast<r::ControlMode>(99);reject(run,r::ErrorCode::InvalidParty,{r::SetParty{id("test.party"),bad}});
    bad=party;bad.activeLimit=1;bad.members[0].active=true;reject(run,r::ErrorCode::Capacity,{r::SetParty{id("test.party"),bad}});
    reject(run,r::ErrorCode::InvalidParty,{r::SetParty{id("test.other"),party}});
    reject(run,r::ErrorCode::InvalidParty,{r::RemoveParty{id("test.party")},r::RemoveParty{id("test.missing")}});
    check(r::partyOf(run.state(),a).has_value(),"Partial removal escaped rollback");
    auto pending=prepare(run,{r::RemoveParty{id("test.party")}});
    party.members[0].active=true;party.leader=b;
    apply(run,{r::SetParty{id("test.party"),party}});check(!run.commit(std::move(pending)),"Stale party transaction committed");
    check(r::activePartyMembers(run.state(),id("test.party"))==std::vector{b,a},"Roster order lost");
    auto bytes=run.save();check(bytes[8]==2,"Party snapshot must use v2");
    auto snap=r::decodeSnapshot(*catalog,bytes);check(r::encodeSnapshot(*catalog,snap)==bytes,"Party canonical roundtrip");
    auto invalid=snap;invalid.state.parties.at(id("test.party")).leader=c;
    auto before=&run.state();rejects(r::ErrorCode::InvalidSnapshot,[&]{run.restore(invalid);});check(before==&run.state(),"Invalid restore published");
    r::BeginEncounter encounter{{id("example.turns"),1},{c,b,a},{{a,{id("test.blue"),r::ControlMode::Player}},{b,{id("test.blue"),r::ControlMode::AI}},{c,{id("test.red"),r::ControlMode::Scripted}}}};
    auto broken=encounter;broken.teams.erase(c);reject(run,r::ErrorCode::InvalidTeam,{broken});
    broken=encounter;broken.teams.at(c).control=static_cast<r::ControlMode>(99);reject(run,r::ErrorCode::InvalidTeam,{broken});
    broken=encounter;broken.teams.erase(c);broken.teams.emplace(r::EntityId{r::SessionId(9),1},r::CombatMember{id("test.red")});reject(run,r::ErrorCode::InvalidTeam,{broken});
    apply(run,{encounter});
    check(r::teamRelation(run.state(),a,b)==r::TeamRelation::Ally&&r::teamRelation(run.state(),a,c)==r::TeamRelation::Opponent,"Team relation");
    check(r::controlledBy(run.state(),b,r::ControlMode::AI)&&!r::controlledBy(run.state(),b,r::ControlMode::Player),"Control query");
    reject(run,r::ErrorCode::InvalidParty,{r::RemoveParty{id("test.party")}});
    bytes=run.save();snap=r::decodeSnapshot(*catalog,bytes);check(r::encodeSnapshot(*catalog,snap)==bytes,"Teams canonical roundtrip");
    invalid=snap;invalid.state.encounter->teams.erase(a);rejects(r::ErrorCode::InvalidSnapshot,[&]{run.restore(invalid);});
    r::Runtime clone(catalog,r::SessionId(1));clone.restore(bytes);
    check(r::teamRelation(clone.state(),a,c)==r::TeamRelation::Opponent&&r::partyOf(clone.state(),b)==id("test.party"),"Restore lost L2 state");
    apply(run,{r::FinishEncounter{run.state().encounterEpoch,run.state().turnEpoch}});
    reject(run,r::ErrorCode::InvalidParty,{r::SetParty{id("test.party"),party}});
    apply(run,{r::ExitEncounter{run.state().encounterEpoch,run.state().turnEpoch},r::RemoveParty{id("test.party")}});
    check(!r::controlledBy(run.state(),b,r::ControlMode::AI)&&r::teamRelation(run.state(),a,b)==r::TeamRelation::Unassigned,"Exit retained teams");
    check(run.save()[8]==1,"Empty extension should use legacy wire format");
    run.restore(legacy);check(run.state().parties.empty(),"Legacy restore retained parties");
    apply(run,{r::BeginEncounter{{id("example.turns"),1},{a,b}}});check(r::teamRelation(run.state(),a,b)==r::TeamRelation::Unassigned,"Legacy encounter inferred teams");
}
void limits(){
    r::Runtime run(example::encounterCatalog(),r::SessionId(2));
    for(std::size_t i=0;i<r::Limits::parties;++i)apply(run,{r::SetParty{r::ContentId("test.p"+std::to_string(i)),{}}});
    reject(run,r::ErrorCode::Limit,{r::SetParty{id("test.extra"),{}}});
    r::BeginEncounter e{{id("example.turns"),1},{}};
    for(std::size_t i=0;i<=r::Limits::teams;++i){auto a=apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0];e.participants.push_back(a);e.teams.emplace(a,r::CombatMember{r::ContentId("test.t"+std::to_string(i))});}
    reject(run,r::ErrorCode::Limit,{e});
}
}
int main(){try{contracts();limits();std::cout<<"L2 party/team checks: "<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
