#include "../examples/rpg/inventory.hpp"
#include "engine/rpg/replay.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace r=engine::rpg;
namespace {
int checks=0;
r::ContentId id(const char* s){return r::ContentId(s);}
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Fixture {
 std::shared_ptr<const r::Catalog> catalog=example::characterCatalog(example::inventoryDefinitions());
 r::Runtime run{catalog,r::SessionId(1),42};
 r::EntityId buyer{r::SessionId(1),0},seller=buyer,bag=buyer,stock=buyer;
 r::ItemId item{r::SessionId(1),0};
 Fixture(){
  auto people=apply({r::CreateCharacter{id("example.scholar")},r::CreateCharacter{id("example.scholar")}}).created;
  buyer=people[0];seller=people[1];
  auto bags=apply({r::CreateInventory{id("example.pack"),buyer},r::CreateInventory{id("example.pack"),seller}}).inventories;
  bag=bags[0];stock=bags[1];
  item=apply({r::AddItem{stock,id("example.tonic"),5},r::AdjustResource{seller,id("example.reserve"),{-15,id("example.point"),1}}}).items[0];
 }
 std::uint64_t sequence()const{auto i=run.state().producers.find(id("test.economy"));return i==run.state().producers.end()?1:i->second+1;}
 r::Transaction prepare(std::vector<r::Operation> ops,r::Budget budget={}){return run.prepare(run.command(id("test.economy"),sequence(),std::move(ops)),budget);}
 r::Results apply(std::vector<r::Operation> ops){auto tx=prepare(std::move(ops));auto out=tx.results();check(run.commit(std::move(tx)),"commit");return out;}
 r::TransferResource payment(std::int64_t value=3){return {buyer,seller,id("example.reserve"),{value,id("example.point"),1}};}
 r::TradeItems trade(){return {{id("test.orders"),"one"},stock,bag,item,2,payment()};}
 r::CraftItems craft(){return {{id("test.recipes"),"one"},{{stock,item,5}},{{stock,id("example.blade"),1}},{payment()}};}
 std::int64_t funds(r::EntityId entity)const{return run.state().characters.at(entity).resources.at(id("example.reserve")).current;}
 void rejects(r::ErrorCode code,std::vector<r::Operation> ops,r::Budget budget={}){
  auto before=run.save();bool rejected=false;
  try{prepare(std::move(ops),budget);}catch(const r::Error&e){check(e.code()==code,e.what());rejected=true;}
  check(rejected,"Expected rejection");check(run.save()==before,"Failure changed state, receipt, cursor, RNG or producer");
 }
};
void trade(){
 Fixture f,other;auto before=f.run.save();auto a=f.prepare({f.trade()}),b=f.prepare({f.trade()});
 check(before==f.run.save(),"Preview changed live state");check(!other.run.commit(std::move(a)),"Foreign commit");
 check(f.run.commit(std::move(a))&&!f.run.commit(std::move(b)),"Competing commits");
 check(f.funds(f.buyer)==17&&f.funds(f.seller)==8,"Currency not conserved");
 check(f.run.state().items.at(f.item).quantity==3,"Partial stock loss");
 auto purchased=f.run.state().items.rbegin()->first;
 check(f.run.state().items.at(purchased).quantity==2&&f.run.state().items.at(purchased).inventory==f.bag,"Purchase missing");
 f.rejects(r::ErrorCode::DuplicateAward,{f.trade()});
 other.run.restore(f.run.save());other.rejects(r::ErrorCode::DuplicateAward,{other.trade()});
 f.apply({r::TradeItems{{id("test.orders"),"sale"},f.bag,f.stock,purchased,2,r::TransferResource{f.seller,f.buyer,id("example.reserve"),{1,id("example.point"),1}}}});
 check(f.funds(f.buyer)==18&&f.funds(f.seller)==7&&!f.run.state().items.contains(purchased),"Sale conservation/identity");
 Fixture stale;auto tx=stale.prepare({stale.trade()});stale.run.restore(stale.run.save());check(!stale.run.commit(std::move(tx)),"Restore accepted stale trade");
 Fixture twice;twice.rejects(r::ErrorCode::DuplicateAward,{twice.trade(),twice.trade()});
 Fixture suppressed;suppressed.apply({r::RecordAward{{suppressed.trade().id,r::AwardDisposition::ReplaySuppressed}}});suppressed.rejects(r::ErrorCode::DuplicateAward,{suppressed.trade()});
}
void failures(){
 Fixture f;
 f.rejects(r::ErrorCode::InvalidAmount,{f.payment(21)});
 f.rejects(r::ErrorCode::Capacity,{f.payment(16)});
 f.rejects(r::ErrorCode::InvalidAmount,{f.payment(0)});
 f.rejects(r::ErrorCode::InvalidAmount,{f.payment(std::numeric_limits<std::int64_t>::min())});
 auto p=f.payment();p.payee=p.payer;f.rejects(r::ErrorCode::InvalidAmount,{p});
 p=f.payment();p.amount.unit=id("test.wrong");f.rejects(r::ErrorCode::InvalidAmount,{p});
 p=f.payment();p.amount.scale=2;f.rejects(r::ErrorCode::InvalidAmount,{p});
 auto t=f.trade();t.source=f.bag;f.rejects(r::ErrorCode::InvalidOwnership,{t});
 t=f.trade();t.quantity=6;f.rejects(r::ErrorCode::InvalidQuantity,{t});
 t=f.trade();t.quantity=0;f.rejects(r::ErrorCode::InvalidQuantity,{t});
 t=f.trade();t.destination=t.source;f.rejects(r::ErrorCode::InvalidOwnership,{t});
 t=f.trade();t.id.receipt="bad receipt";f.rejects(r::ErrorCode::InvalidAward,{t});
 for(int i=0;i<4;++i)f.apply({r::AddItem{f.bag,id("example.blade"),1}});
 f.rejects(r::ErrorCode::Capacity,{f.trade()});
 auto removed=f.run.state().items.rbegin()->first;f.apply({r::RemoveItem{f.bag,removed,1},f.trade()});
 check(f.run.state().awards.size()==1,"Retry did not succeed");
 Fixture budget;r::Budget tiny;tiny.operations=6;budget.rejects(r::ErrorCode::BudgetExceeded,{budget.trade()},tiny);
 auto blade=budget.apply({r::AddItem{budget.stock,id("example.blade"),1}}).items[0];budget.apply({r::EquipItem{budget.stock,blade,id("example.hand")}});
 auto equipped=budget.trade();equipped.item=blade;equipped.quantity=1;
 budget.rejects(r::ErrorCode::InvalidOwnership,{equipped});
}
void crafting(){
 Fixture f;for(int i=0;i<3;++i)f.apply({r::AddItem{f.stock,id("example.blade"),1}});
 auto partial=f.craft();partial.ingredients[0].quantity=1;f.rejects(r::ErrorCode::Capacity,{partial});
 auto bad=f.craft();bad.ingredients.push_back(bad.ingredients[0]);f.rejects(r::ErrorCode::InvalidQuantity,{bad});
 bad=f.craft();bad.products.clear();f.rejects(r::ErrorCode::Limit,{bad});
 bad=f.craft();bad.ingredients.clear();f.rejects(r::ErrorCode::Limit,{bad});
 bad=f.craft();bad.products[0].quantity=2;f.rejects(r::ErrorCode::InvalidQuantity,{bad});
 bad=f.craft();bad.payments.assign(17,f.payment());f.rejects(r::ErrorCode::Limit,{bad});
 bad=f.craft();bad.payments={f.payment(10),f.payment(10)};f.rejects(r::ErrorCode::Capacity,{bad});
 f.apply({f.craft()});check(!f.run.state().items.contains(f.item)&&f.run.state().items.size()==4,"Recipe did not free consumed slot");
 check(f.funds(f.buyer)==17&&f.funds(f.seller)==8,"Craft fee not conserved");
 auto bytes=f.run.save();f.run.restore(bytes);f.rejects(r::ErrorCode::DuplicateAward,{f.craft()});
}
void diagnostics(){
 Fixture f;const auto initial=f.run.save();
 std::vector<r::ReplayStep> steps{{id("test.economy"),f.sequence(),{f.trade()}},{id("test.economy"),f.sequence()+1,{f.trade()}},{id("test.economy"),f.sequence()+2,{f.payment()}}};
 auto a=r::replay(f.catalog,r::SessionId(1),initial,steps),b=r::replay(f.catalog,r::SessionId(1),initial,steps);
 check(a.completed==1&&a.failure&&a.failure->step==1&&a.failure->code==r::ErrorCode::DuplicateAward,"Replay failure location");
 check(a.snapshot==b.snapshot&&f.run.save()==initial,"Replay nondeterminism or live mutation");
 Fixture control;control.run.restore(initial);control.apply({control.trade()});check(control.run.save()==a.snapshot,"Replay prefix diverged");
 steps.clear();auto empty=r::replay(f.catalog,r::SessionId(1),initial,steps);check(empty.completed==0&&!empty.failure,"Empty replay");
 steps={{id("test.replay"),1,{r::BeginEncounter{{id("test.policy"),1},{},{}}}}};
 auto transient=r::replay(f.catalog,r::SessionId(1),initial,steps);
 check(transient.completed==0&&transient.failure->code==r::ErrorCode::InvalidAction,"Transient replay accepted");
 steps[0].operations.assign(257,f.payment());
 auto oversized=r::replay(f.catalog,r::SessionId(1),initial,steps);
 check(oversized.completed==0&&oversized.failure->code==r::ErrorCode::Limit,"Replay operation bounds");
 steps.assign(65,{id("test.replay"),1,{}});bool limited=false;try{r::replay(f.catalog,r::SessionId(1),initial,steps);}catch(const r::Error&e){limited=e.code()==r::ErrorCode::Limit;}check(limited,"Replay not bounded");
}
}
int main(){try{trade();failures();crafting();diagnostics();std::cout<<"L8 economy checks: "<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
