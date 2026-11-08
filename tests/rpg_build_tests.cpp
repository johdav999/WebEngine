#include "../examples/rpg/builds.hpp"
#include <iostream>
#include <stdexcept>
namespace r=engine::rpg;
namespace {
int checks=0;r::ContentId id(const char*s){return r::ContentId(s);}
void check(bool ok,const char*m){++checks;if(!ok)throw std::runtime_error(m);}
template<class F>void rejects(r::ErrorCode code,F fn){try{fn();}catch(const r::Error&e){check(e.code()==code,e.what());return;}throw std::runtime_error("Expected rejection");}
struct Fixture {
 std::shared_ptr<const r::Catalog> catalog=example::encounterCatalog(example::tacticalRules(),example::buildDefinitions());
 r::Runtime run{catalog,r::SessionId(1),42};r::EntityId actor{r::SessionId(1),0},bag{r::SessionId(1),0};
 Fixture(){actor=apply({r::CreateCharacter{id("example.scholar")}}).created[0];bag=apply({r::CreateInventory{id("example.pack"),actor}}).inventories[0];}
 r::Transaction prepare(std::vector<r::Operation> ops){auto i=run.state().producers.find(id("test.build"));return run.prepare(run.command(id("test.build"),i==run.state().producers.end()?1:i->second+1,std::move(ops)));}
 r::Results apply(std::vector<r::Operation> ops){auto tx=prepare(std::move(ops));auto out=tx.results();check(run.commit(std::move(tx)),"commit");return out;}
 void reject(r::ErrorCode code,std::vector<r::Operation> ops){auto bytes=run.save();rejects(code,[&]{prepare(std::move(ops));});check(bytes==run.save(),"Rejected build mutated state");}
 void xp(int n){apply({r::AddExperience{actor,{n,id("example.practice"),1}}});}
 r::SetProgressionChoices select(std::vector<r::ContentId> v){return {actor,std::move(v)};}
 r::ItemId item(const char* name){return apply({r::AddItem{bag,id(name),1}}).items[0];}
};
void choices(){
 Fixture f;auto mastery=id("example.mastery"),expert=id("example.expert"),scout=id("example.scouting");
 f.reject(r::ErrorCode::InvalidBuild,{f.select({mastery})});f.xp(10);
 f.reject(r::ErrorCode::InvalidBuild,{f.select({mastery,expert})});f.reject(r::ErrorCode::InvalidBuild,{f.select({id("test.unknown")})});f.reject(r::ErrorCode::InvalidBuild,{f.select({mastery,mastery})});
 auto before=f.run.save();auto tx=f.prepare({f.select({mastery})});auto other=f.prepare({f.select({scout})});check(f.run.save()==before&&tx.results().choices[0].remainingPoints==0,"Choice preview");
 check(f.run.commit(std::move(tx))&&!f.run.commit(std::move(other)),"Competing selection");f.xp(20);
 f.reject(r::ErrorCode::InvalidBuild,{f.select({mastery,scout})});f.reject(r::ErrorCode::InvalidBuild,{f.select({expert})});
 f.apply({f.select({expert,mastery})});check(r::remainingChoicePoints(*f.catalog,f.run.state(),f.actor)==0,"Point spend");
 f.reject(r::ErrorCode::InvalidBuild,{f.select({expert})});f.apply({f.select({scout})});
 check(f.run.state().characters.at(f.actor).unlocks.contains(id("example.first"))&&!f.run.state().characters.at(f.actor).unlocks.contains(expert),"Respec changed automatic unlocks");
 check(r::remainingChoicePoints(*f.catalog,f.run.state(),f.actor)==1,"Respec points");
 f.apply({r::BeginEncounter{{id("example.turns"),1},{f.actor}}});
 f.reject(r::ErrorCode::InvalidBuild,{f.select({})});
}
void equipment(){
 Fixture f;auto blade=f.item("example.blade"),shield=f.item("example.shield"),great=f.item("example.greatblade");
 f.reject(r::ErrorCode::InvalidBuild,{r::EquipItem{f.bag,great,id("example.hand")}});f.xp(10);f.apply({f.select({id("example.mastery")})});
 f.apply({r::EquipItem{f.bag,blade,id("example.hand")},r::EquipItem{f.bag,shield,id("example.offhand")}});
 auto extra=f.item("example.tonic");f.item("example.tonic");f.item("example.tonic");
 f.reject(r::ErrorCode::Capacity,{r::EquipItem{f.bag,great,id("example.hand")}});
 f.apply({r::RemoveItem{f.bag,extra,1},r::EquipItem{f.bag,great,id("example.hand")}});
 auto& s=f.run.state();check(!s.items.at(blade).slot&&!s.items.at(shield).slot&&r::occupiedEquipmentSlots(*f.catalog,s.items.at(great)).size()==2,"Multi-slot displacement");
 check(s.characters.at(f.actor).attributes.at(id("example.insight")).value==17,"Multi-slot modifiers applied more than once");
 f.reject(r::ErrorCode::InvalidBuild,{f.select({})});
 f.reject(r::ErrorCode::InvalidBuild,{r::SetAttributeBase{f.actor,id("example.insight"),{9,id("example.point"),1}}});
 f.reject(r::ErrorCode::Capacity,{r::UnequipItem{f.bag,great}});
 f.apply({r::RemoveItem{f.bag,blade,1},r::UnequipItem{f.bag,great},f.select({})});
 check(f.run.state().choices.empty()&&f.run.state().characters.at(f.actor).attributes.at(id("example.insight")).value==10,"Atomic safe respec");
 f.apply({r::SetAttributeBase{f.actor,id("example.insight"),{9,id("example.point"),1}},f.select({id("example.mastery")})});
 f.apply({r::SetModifiers{f.actor,id("test.boost"),{{r::ModifierTarget::Attribute,id("example.insight"),{10,id("example.point"),1}}}}});
 f.reject(r::ErrorCode::InvalidBuild,{r::EquipItem{f.bag,great,id("example.hand")}});
}
void persistence(){
 Fixture f;auto legacy=f.run.save();check(legacy[8]==1,"Empty choices changed old state format");f.xp(30);f.apply({f.select({id("example.mastery"),id("example.expert")})});
 auto bytes=f.run.save();check(bytes[8]==4,"Missing state v4");auto c=r::encodeCatalog(*f.catalog);check(c[8]==2,"Missing catalogue v2");
 auto decoded=r::decodeCatalog(c,f.catalog->bindings());check(r::encodeCatalog(*decoded)==c,"Catalogue roundtrip");
 auto pending=f.prepare({f.select({})});f.run.restore(bytes);check(!f.run.commit(std::move(pending))&&f.run.state().choices.at(f.actor).size()==2,"Restore stale/choices");check(r::encodeSnapshot(*f.catalog,r::decodeSnapshot(*f.catalog,bytes))==bytes,"Snapshot canonical roundtrip");
 auto bad=r::decodeSnapshot(*f.catalog,bytes);bad.state.choices.clear();rejects(r::ErrorCode::InvalidSnapshot,[&]{f.run.restore(bad);});
 bad=r::decodeSnapshot(*f.catalog,bytes);bad.state.characters.at(f.actor).unlocks.erase(id("example.mastery"));rejects(r::ErrorCode::InvalidSnapshot,[&]{f.run.restore(bad);});
 bad=r::decodeSnapshot(*f.catalog,bytes);bad.state.choices[f.actor].insert(id("example.scouting"));bad.state.characters.at(f.actor).unlocks.insert(id("example.scouting"));rejects(r::ErrorCode::InvalidSnapshot,[&]{f.run.restore(bad);});
 auto other=example::buildDefinitions();other.choiceRules[0].pointsPerLevel=2;auto incompatible=example::encounterCatalog(example::tacticalRules(),other);rejects(r::ErrorCode::InvalidSnapshot,[&]{r::decodeSnapshot(*incompatible,bytes);});
 f.run.restore(legacy);check(f.run.state().choices.empty(),"Legacy restore retained choices");
 // Multi-slot overlap in a detached snapshot must also reject.
 f.xp(10);f.apply({f.select({id("example.mastery")})});auto great=f.item("example.greatblade"),shield=f.item("example.shield");f.apply({r::EquipItem{f.bag,great,id("example.hand")}});
 bad=r::decodeSnapshot(*f.catalog,f.run.save());bad.state.items.at(shield).slot=id("example.offhand");rejects(r::ErrorCode::InvalidSnapshot,[&]{f.run.restore(bad);});
}
void definitions(){
 auto fail=[](r::CatalogDefinition d,r::ErrorCode code=r::ErrorCode::InvalidDefinition){rejects(code,[&]{example::encounterCatalog(example::tacticalRules(),std::move(d));});};
 auto d=example::buildDefinitions();d.choiceRules[0].choices[0].requirements.unlocks={id("example.expert")};fail(d);
 d=example::buildDefinitions();d.choiceRules[0].choices[0].requirements.unlocks={id("example.mastery")};fail(d);
 d=example::buildDefinitions();d.choiceRules[0].choices[0].cost=0;fail(d);
 d=example::buildDefinitions();d.choiceRules[0].pointsPerLevel=0;fail(d,r::ErrorCode::Limit);
 d=example::buildDefinitions();while(d.choiceRules[0].choices.size()<257)d.choiceRules[0].choices.push_back(d.choiceRules[0].choices[0]);fail(d,r::ErrorCode::Limit);
 d=example::buildDefinitions();d.choiceRules[0].choices[0].requirements.unlocks={id("test.missing")};fail(d);
 d=example::buildDefinitions();d.choiceRules[0].choices.push_back(d.choiceRules[0].choices[0]);fail(d);
 d=example::buildDefinitions();d.equipmentRules[0].requiredSlots.push_back(id("example.offhand"));fail(d);
 d=example::buildDefinitions();d.equipmentRules[0].item=id("test.missing");fail(d);
 d=example::buildDefinitions();d.equipmentRules[0].requirements.attributes[0].minimum.scale=0;fail(d);
 d=example::buildDefinitions();d.choiceRules[0].choices[0].id=id("example.first");d.choiceRules[0].choices[2].requirements.unlocks.clear();
 auto cat=example::encounterCatalog(example::tacticalRules(),d);r::Runtime run(cat,r::SessionId(1));auto tx=run.prepare(run.command(id("test.input"),1,{r::CreateCharacter{id("example.scholar")}}));auto actor=tx.results().created[0];check(run.commit(std::move(tx)),"setup");auto saved=run.save();
 rejects(r::ErrorCode::RejectedUnlock,[&]{run.prepare(run.command(id("test.input"),2,{r::AddExperience{actor,{10,id("example.practice"),1}}}));});check(run.save()==saved,"Automatic grant bypassed choice ownership");
}
}
int main(){try{choices();equipment();persistence();definitions();std::cout<<"L7 build checks: "<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
