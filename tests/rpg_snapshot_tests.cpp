#include "../examples/rpg/tactics.hpp"
#include "engine/rpg/snapshot.hpp"
#include "../src/rpg/wire.hpp" // Malformed-but-checksummed fixtures exercise the decoder.
#include "rpg_r8_fixture.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace r=engine::rpg;
namespace {
int checks=0;
r::ContentId id(const char*s){return r::ContentId(s);}
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
template<class F>void reject(F fn){try{fn();}catch(const r::Error&){++checks;return;}throw std::runtime_error("Expected snapshot rejection");}
r::Results apply(r::Runtime&run,std::vector<r::Operation> ops,std::optional<r::WorldStamp> world={}){
    auto i=run.state().producers.find(id("test.input"));auto seq=i==run.state().producers.end()?1:i->second+1;
    auto tx=run.prepare(run.command(id("test.input"),seq,std::move(ops)),{},world);auto result=tx.results();check(run.commit(std::move(tx),world),"commit");return result;
}
struct Fixture {
    std::shared_ptr<const r::Catalog> catalog=example::encounterCatalog(example::tacticalRules());
    r::Runtime game{catalog,r::SessionId(1),42};
    r::EntityId actor{r::SessionId(1),0},other{r::SessionId(1),0},bag{r::SessionId(1),0};
    Fixture(){
        actor=apply(game,{r::CreateCharacter{id("example.scholar")}}).created[0];
        other=apply(game,{r::CreateCharacter{id("example.scholar")}}).created[0];
        bag=apply(game,{r::CreateInventory{id("example.pack"),actor}}).inventories[0];
        auto blade=apply(game,{r::AddItem{bag,id("example.blade"),1}}).items[0];
        apply(game,{r::EquipItem{bag,blade,id("example.hand")},r::AddItem{bag,id("example.tonic"),3},
            r::AddExperience{actor,{35,id("example.practice"),1}},
            r::ApplyStatus{actor,id("example.focused"),id("test.focus")},
            r::ApplyStatus{actor,id("example.ward"),id("test.ward")},
            r::SetModifiers{actor,id("test.bonus"),{{r::ModifierTarget::Attribute,id("example.insight"),{1,id("example.point"),1}}}},
            r::SetCooldown{actor,id("test.cooldown"),r::TurnPhase::OwnerEnd,3},
            r::SetTacticalPosition{actor,{1,-2,3}},r::SetTacticalPosition{other,{4,5,-6}},
            r::RecordAward{{{id("test.awards"),"receipt:1"},r::AwardDisposition::Granted}},
            r::RecordAward{{{id("test.awards"),"receipt:2"},r::AwardDisposition::ReplaySuppressed}}});
        apply(game,{r::UseAbility{actor,id("example.strike"),{other},{}}});
    }
};
std::string hex(std::span<const std::uint8_t> b){std::ostringstream out;for(auto x:b)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(x);return out.str();}
std::vector<std::uint8_t> fixtureBytes(){Fixture f;apply(f.game,{r::BeginEncounter{{id("example.turns"),1},{f.actor,f.other}}});return f.game.save();}
void roundtrip(){
    Fixture f;auto bytes=f.game.save();auto cat=r::encodeCatalog(*f.catalog);
    auto decoded=r::decodeCatalog(cat,f.catalog->bindings());check(r::encodeCatalog(*decoded)==cat,"catalogue byte roundtrip");
    auto state=r::decodeSnapshot(*decoded,bytes);check(r::encodeSnapshot(*decoded,state)==bytes,"snapshot byte roundtrip");
    auto old=f.game.command(id("test.old"),1,{r::AdjustResource{f.actor,id("example.reserve"),{0,id("example.point"),1}}});auto tx=f.game.prepare(old);
    auto generation=f.game.state().generation;f.game.restore(bytes);check(f.game.state().generation>generation,"generation not rebased");
    reject([&]{f.game.prepare(old);});check(!f.game.commit(std::move(tx)),"preload transaction survived");
    r::Runtime clone(decoded,r::SessionId(1));clone.restore(bytes);
    apply(f.game,{r::UseAbility{f.actor,id("example.strike"),{f.other},{}}});apply(clone,{r::UseAbility{f.actor,id("example.strike"),{f.other},{}}});
    check(f.game.save()==clone.save(),"RNG continuation differs after load");
    auto changed=f.catalog->definition();++changed.characters[0].attributes[0].initial;
    auto incompatible=r::Catalog::create(changed,f.catalog->bindings());reject([&]{r::decodeSnapshot(*incompatible,bytes);});
    auto other=example::characterCatalog(example::characterDefinitions());reject([&]{r::decodeCatalog(cat,other->bindings());});
    auto mismatch=state;mismatch.session=r::SessionId(2);auto before=&f.game.state();reject([&]{f.game.restore(mismatch);});check(before==&f.game.state(),"foreign session replaced state");
}
void invalidState(){
    Fixture f;const auto good=r::decodeSnapshot(*f.catalog,f.game.save());auto before=&f.game.state();
    auto bad=[&](auto edit){auto s=good;edit(s.state);reject([&]{f.game.restore(s);});check(before==&f.game.state(),"invalid restore changed live state");};
    bad([](auto&s){s.nextEntity=1;});bad([](auto&s){s.nextItem=1;});
    bad([](auto&s){s.items.begin()->second.inventory.value=900;});
    bad([](auto&s){s.items.begin()->second.slot=id("test.unknown-slot");});
    bad([](auto&s){s.items.begin()->second.quantity=2;});
    bad([](auto&s){s.characters.begin()->second.equipmentModifiers.clear();});
    bad([](auto&s){s.characters.begin()->second.level=0;});
    bad([](auto&s){s.characters.begin()->second.attributes.begin()->second.value=999;});
    bad([](auto&s){s.characters.begin()->second.statuses.begin()->second.remaining=999;});
    bad([](auto&s){s.random.version=2;});
    bad([](auto&s){s.producers.begin()->second=0;});
    bad([](auto&s){s.awards.begin()->second=static_cast<r::AwardDisposition>(99);});
    bad([](auto&s){s.revision=std::numeric_limits<std::uint64_t>::max();});
    bad([](auto&s){s.generation=std::numeric_limits<std::uint64_t>::max();});
    auto exhausted=good;exhausted.state.nextEntity=0;exhausted.state.nextItem=0;f.game.restore(exhausted);
    before=&f.game.state();reject([&]{f.game.allocateEntityId();});reject([&]{apply(f.game,{r::AddItem{f.bag,id("example.tonic"),1}});});check(before==&f.game.state(),"exhausted IDs changed live state");
}
void encounter(){
    Fixture f;apply(f.game,{r::BeginEncounter{{id("example.turns"),1},{f.actor,f.other}}});auto bytes=f.game.save();auto s=r::decodeSnapshot(*f.catalog,bytes);
    check(r::encodeSnapshot(*f.catalog,s)==bytes,"encounter roundtrip");
    auto before=&f.game.state();auto bad=s;bad.state.encounter->cursor=100;reject([&]{f.game.restore(bad);});
    bad=s;bad.state.encounter->order[0].entity.value=999;reject([&]{f.game.restore(bad);});
    bad=s;bad.state.encounter->active=f.other;reject([&]{f.game.restore(bad);});
    bad=s;bad.state.encounter->turnEpoch++;reject([&]{f.game.restore(bad);});check(before==&f.game.state(),"invalid turn changed state");
    const auto&e=*f.game.state().encounter;auto actor=*e.active;auto start=f.game.state().positions.at(actor);auto end=start;end.x++;
    r::SpatialSnapshot spatial{{1,1},{{f.actor,f.game.state().positions.at(f.actor),true},{f.other,f.game.state().positions.at(f.other),true}},{f.other}};
    auto quote=f.game.spatialQuote(actor,spatial,r::PathQuote{start,end,1,{start,end},true,true,true});
    r::TacticalAction action{actor,e.epoch,e.turnEpoch,r::MoveAction{},quote};
    apply(f.game,{action},r::WorldStamp{1,1});reject([&]{f.game.save();});
    auto pending=*f.game.state().presentation;apply(f.game,{r::AcknowledgePresentation{pending.actionEpoch,pending.encounterEpoch,pending.turnEpoch}});
    f.game.restore(bytes);reject([&]{apply(f.game,{action},r::WorldStamp{1,1});});
}
void malformed(){
    Fixture f;auto bytes=f.game.save();auto before=&f.game.state();
    // Every truncated prefix; bounded deterministic mutation corpus also repairs checksums.
    for(std::size_t n=0;n<bytes.size();++n)reject([&]{f.game.restore(std::span(bytes).first(n));});
    for(std::size_t n=0;n<bytes.size();n+=31){auto b=bytes;b[n]^=0x80;reject([&]{f.game.restore(b);});}
    for(std::size_t n=0;n<256;++n){std::vector<std::uint8_t> p(n,static_cast<std::uint8_t>(n));auto b=r::wire::wrap(2,p);reject([&]{f.game.restore(b);});}
    auto rewrap=[](std::vector<std::uint8_t>&b){auto h=r::wire::checksum(std::span(b).first(b.size()-8));for(unsigned i=0;i<8;++i)b[b.size()-8+i]=static_cast<std::uint8_t>(h>>(8*i));};
    auto b=bytes;b[8]=2;rewrap(b);reject([&]{f.game.restore(b);});
    b=bytes;for(unsigned i=0;i<8;++i)b[32+i]=255;rewrap(b);reject([&]{f.game.restore(b);});
    b.assign(r::snapshotByteLimit+1,0);reject([&]{f.game.restore(b);});
    check(before==&f.game.state(),"malformed bytes changed live state");
    auto cat=r::encodeCatalog(*f.catalog);for(std::size_t n=0;n<cat.size();n+=17)reject([&]{r::decodeCatalog(std::span(cat).first(n),f.catalog->bindings());});
}
}
int main(int argc,char**){try{
    auto bytes=fixtureBytes();if(argc>1){std::cout<<hex(bytes);return 0;}
    check(hex(bytes)==rpgR8Fixture,"cross-platform canonical byte fixture mismatch");
    roundtrip();invalidState();encounter();malformed();
    std::cout<<"R8 snapshot checks: "<<checks<<"; fixture bytes: "<<bytes.size()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
