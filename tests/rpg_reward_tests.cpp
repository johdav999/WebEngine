#include "../examples/rpg/inventory.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace r=engine::rpg;
namespace {
int checks=0;
r::ContentId id(const char* s){return r::ContentId(s);}
void check(bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);}
struct Fixture {
 r::Runtime run{example::characterCatalog(example::inventoryDefinitions()),r::SessionId(1),42};
 r::EntityId actor{r::SessionId(1),0},bag{r::SessionId(1),0};
 Fixture(){actor=apply({r::CreateCharacter{id("example.scholar")}}).created[0];bag=apply({r::CreateInventory{id("example.pack"),actor}}).inventories[0];}
 r::Transaction prepare(std::vector<r::Operation> ops,r::Budget budget={}){
  auto it=run.state().producers.find(id("test.rewards"));
  return run.prepare(run.command(id("test.rewards"),it==run.state().producers.end()?1:it->second+1,std::move(ops)),budget);
 }
 r::Results apply(std::vector<r::Operation> ops){auto tx=prepare(std::move(ops));auto result=tx.results();check(run.commit(std::move(tx)),"commit");return result;}
 r::RollLoot table(){return {id("test.loot"),{{id("test.common"),3,{r::AddItem{bag,id("example.tonic"),1}}},{id("test.rare"),1,{r::AddItem{bag,id("example.tonic"),2}}}},1};}
 r::GrantReward reward(){return {{id("test.encounter"),"encounter-1"},{r::AddExperience{actor,{10,id("example.practice"),1}}},{table()}};}
 void rejects(r::ErrorCode code,std::vector<r::Operation> ops,r::Budget budget={}){
  auto before=run.save();bool rejected=false;
  try{prepare(std::move(ops),budget);}catch(const r::Error&e){check(e.code()==code,e.what());rejected=true;}
  check(rejected,"Expected rejection");check(run.save()==before,"Rejection changed state/RNG/receipt/watermark");
 }
};
void atomic(){
 Fixture f,other;auto before=f.run.save();auto a=f.prepare({f.reward()}),b=f.prepare({f.reward()});
 check(f.run.save()==before,"Preview changed state");
 check(a.results().loot.size()==1&&a.results().loot[0].entry==b.results().loot[0].entry,"Preview determinism");
 check(a.results().awards.size()==1&&a.state().characters.at(f.actor).experience==10,"Bundle candidate");
 check(!other.run.commit(std::move(a)),"Foreign commit");
 check(f.run.commit(std::move(a))&&!f.run.commit(std::move(b)),"Competing publication");
 check(f.run.state().random!=other.run.state().random,"Loot did not draw");
 f.rejects(r::ErrorCode::DuplicateAward,{f.reward()});
 auto saved=f.run.save();other.run.restore(saved);other.rejects(r::ErrorCode::DuplicateAward,{other.reward()});
 f.run.invalidateCommands();f.rejects(r::ErrorCode::DuplicateAward,{f.reward()});
 Fixture suppressed;auto reward=suppressed.reward();suppressed.apply({r::RecordAward{{reward.id,r::AwardDisposition::ReplaySuppressed}}});
 suppressed.rejects(r::ErrorCode::DuplicateAward,{reward});
 Fixture twice;twice.rejects(r::ErrorCode::DuplicateAward,{twice.reward(),twice.reward()});
 Fixture stale;auto tx=stale.prepare({stale.reward()});stale.run.restore(stale.run.save());check(!stale.run.commit(std::move(tx)),"Restore accepted stale reward");
}
void fullBag(){
 Fixture f,control;for(int i=0;i<4;++i)f.apply({r::AddItem{f.bag,id("example.blade"),1}});
 f.rejects(r::ErrorCode::Capacity,{f.reward()});
 auto item=f.run.state().items.begin()->first;f.apply({r::RemoveItem{f.bag,item,1}});
 auto expected=control.prepare({control.reward()});auto tx=f.prepare({f.reward()});
 check(tx.results().loot[0].entry==expected.results().loot[0].entry,"Failed award advanced RNG");
 check(f.run.commit(std::move(tx))&&f.run.state().characters.at(f.actor).experience==10,"Retry failed");
 f.rejects(r::ErrorCode::DuplicateAward,{f.reward()});
 Fixture order;for(int i=0;i<4;++i)order.apply({r::AddItem{order.bag,id("example.blade"),1}});
 auto removed=order.run.state().items.begin()->first;
 order.apply({r::RemoveItem{order.bag,removed,1},order.reward()});
 check(order.run.state().awards.size()==1,"Expansion reordered command");
}
void limits(){
 Fixture f;auto reward=f.reward();r::Budget tiny;tiny.randomDraws=0;
 f.rejects(r::ErrorCode::BudgetExceeded,{reward},tiny);tiny={};tiny.operations=4;
 f.rejects(r::ErrorCode::BudgetExceeded,{reward},tiny);
 auto bad=f.table();bad.entries.clear();f.rejects(r::ErrorCode::Limit,{bad});
 bad=f.table();bad.rolls=0;f.rejects(r::ErrorCode::Limit,{bad});
 bad=f.table();bad.rolls=33;f.rejects(r::ErrorCode::Limit,{bad});
 bad=f.table();bad.entries[0].weight=0;f.rejects(r::ErrorCode::InvalidDefinition,{bad});
 bad=f.table();bad.entries[1].id=bad.entries[0].id;f.rejects(r::ErrorCode::InvalidDefinition,{bad});
 bad=f.table();bad.entries[0].weight=std::numeric_limits<std::uint32_t>::max();f.rejects(r::ErrorCode::Overflow,{bad});
 bad=f.table();while(bad.entries.size()<65)bad.entries.push_back({r::ContentId("test.entry"+std::to_string(bad.entries.size())),1,{}});
 f.rejects(r::ErrorCode::Limit,{bad});
 reward=f.reward();reward.id.receipt="invalid receipt";f.rejects(r::ErrorCode::InvalidAward,{reward});
 reward=f.reward();while(reward.loot.size()<17)reward.loot.push_back(f.table());f.rejects(r::ErrorCode::Limit,{reward});
 bad=f.table();bad.entries.erase(bad.entries.begin()+1,bad.entries.end());bad.entries[0].grants={r::AddItem{f.bag,id("test.missing"),1}};
 f.rejects(r::ErrorCode::MissingDefinition,{bad});
 bad.entries[0].grants={r::AddItem{f.bag,id("example.tonic"),0}};f.rejects(r::ErrorCode::InvalidQuantity,{bad});
 r::RollLoot empty{id("test.empty"),{{id("test.none"),1,{}}},32};
 tiny={};tiny.randomDraws=31;f.rejects(r::ErrorCode::BudgetExceeded,{empty},tiny);
 reward=f.reward();reward.loot.push_back(f.table());tiny={};tiny.randomDraws=1;
 f.rejects(r::ErrorCode::BudgetExceeded,{reward},tiny);
 reward=f.reward();reward.grants.push_back(r::AdjustResource{f.actor,id("test.missing"),{1,id("example.point"),1}});
 f.rejects(r::ErrorCode::MissingValue,{reward});
 auto result=f.apply({empty});check(result.loot.size()==32&&f.run.state().items.empty(),"Explicit no-drop");
 Fixture unit;auto one=unit.table();one.entries.erase(one.entries.begin()+1,one.entries.end());one.entries[0].weight=1;
 auto out=unit.apply({one});check(out.loot[0].entry==id("test.common"),"Single weighted entry");
}
void deterministic(){
 Fixture a,b;r::RollLoot table{id("test.many"),{{id("test.a"),1,{}},{id("test.b"),3,{}}},32};
 auto left=a.apply({table}),right=b.apply({table});bool different=false;
 for(std::size_t i=0;i<left.loot.size();++i){check(left.loot[i].entry==right.loot[i].entry,"Seed mismatch");different|=left.loot[i].entry!=left.loot[0].entry;}
 check(different&&a.run.state().random==b.run.state().random,"Weighted selection/RNG mismatch");
}
}
int main(){try{atomic();fullBag();limits();deterministic();std::cout<<"L6 reward checks: "<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
