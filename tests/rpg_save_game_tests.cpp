#include "../examples/rpg/save_game.hpp"
#include "../src/rpg/wire.hpp"
#include <iostream>
#include <stdexcept>

namespace r=engine::rpg;namespace q=engine::quests;namespace b=r::quest_bridge;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F>void reject(F fn){try{fn();}catch(const std::invalid_argument&){++checks;return;}throw std::runtime_error("Expected rejection");}
r::ContentId id(const char*s){return r::ContentId(s);}
struct Fixture {
    std::shared_ptr<const r::Catalog> catalog=example::encounterCatalog(example::tacticalRules());
    std::shared_ptr<const q::Catalog> quests=example::rewardQuests();
    example::SavedGame sample{catalog,quests};
    r::EntityId actor{r::SessionId(1),0};
    Fixture(){auto&g=sample.game();actor=g.applyRpg(id("test.setup"),1,{r::CreateCharacter{id("example.scholar")}}).created[0];g.applyRpg(id("test.setup"),2,{r::CreateInventory{id("example.pack"),actor}});}
    void activate(){auto c=sample.game().command();c.batch.commands.push_back({"example.delivery",q::CommandKind::Activate,0,{}});auto t=sample.game().prepare(c);check(sample.game().commit(std::move(t)),"activation");}
    q::Event event(std::uint64_t seq=1){return sample.game().event({"example.delivery","example.delivery","test.delivery",seq,{{"item",std::string("parcel")}}});}
    void complete(bool replay=false){auto&g=sample.game();g.post(event());auto c=g.command();c.batch.replay=replay;auto t=g.prepare(c);check(g.commit(std::move(t)),"completion");}
    void retry(){auto c=sample.game().command();c.batch.commands.push_back({"example.delivery",q::CommandKind::Retry,1,{}});auto t=sample.game().prepare(c);check(sample.game().commit(std::move(t)),"retry setup");}
    std::int64_t xp(){return sample.game().rpgState().characters.at(actor).experience;}
};
void reload(bool replay){
    Fixture f;f.activate();f.complete(replay);f.sample.checkpoint(37);auto saved=f.sample.save();auto bridge=f.sample.game().save();
    f.retry();
    auto old=f.sample.game().command();auto oldTx=f.sample.game().prepare(old);auto oldEvent=f.event(2);
    auto oldRpg=f.sample.game().prepareRpg(id("test.change"),1,{r::AdjustResource{f.actor,id("example.reserve"),{0,id("example.point"),1}}});
    f.sample.game().post(oldEvent);auto generation=f.sample.game().questState().generation;
    f.sample.game().restore(bridge);
    check(f.sample.game().pending()==0&&f.sample.game().questState().generation>generation,"restore retained queue/generation");
    check(!f.sample.game().commit(std::move(oldTx))&&!f.sample.game().commitRpg(std::move(oldRpg)),"preload candidates survived");
    reject([&]{f.sample.game().prepare(old);});reject([&]{f.sample.game().post(oldEvent);});
    check(f.xp()==(replay?0:10)&&f.sample.game().rpgState().awards.size()==2,"restore changed awards");
    for(auto&[award,disposition]:f.sample.game().rpgState().awards)check(disposition==(replay?r::AwardDisposition::ReplaySuppressed:r::AwardDisposition::Granted),"award disposition lost");
    auto retry=f.sample.game().command();retry.batch.commands.push_back({"example.delivery",q::CommandKind::Retry,1,{}});auto tx=f.sample.game().prepare(retry);check(f.sample.game().commit(std::move(tx)),"retry");
    f.sample.game().post(f.event(2));tx=f.sample.game().prepare(f.sample.game().command());check(tx.effects().empty()&&tx.rewards().awards.empty(),"reloaded receipt awarded twice");check(f.sample.game().commit(std::move(tx)),"repeat completion");
    check(f.xp()==(replay?0:10),"duplicate XP after reload");
    f.sample.checkpoint(99);f.sample.load(saved);check(f.sample.checkpoint()==37&&f.xp()==(replay?0:10),"outer envelope load");
    auto before=&f.sample.game();saved.back()^=1;reject([&]{f.sample.load(saved);});check(before==&f.sample.game()&&f.sample.checkpoint()==37,"failed outer load changed sample");
}
void atomicFailure(){
    Fixture f;f.activate();f.complete();auto saved=f.sample.game().save();f.retry();f.sample.game().post(f.event(2));
    const auto*before=&f.sample.game().rpgState();const auto*qbefore=&f.sample.game().questState();
    // Keep valid RPG bytes, damage only the quest member and rebuild outer checksum.
    auto reader=r::wire::unwrap(saved,3);auto source=r::wire::get<r::ContentId>(reader);auto seq=reader.number();auto rb=r::wire::blob(reader);auto qb=r::wire::blob(reader);
    std::vector<std::uint8_t> badQuest(qb.begin(),qb.end());badQuest.back()^=1;
    r::wire::Writer w;r::wire::put(w,source);w.number(seq);r::wire::blob(w,rb);r::wire::blob(w,badQuest);auto bad=r::wire::wrap(3,w.bytes);
    reject([&]{f.sample.game().restore(bad);});
    check(before==&f.sample.game().rpgState()&&qbefore==&f.sample.game().questState()&&f.sample.game().pending()==1,"second kernel failure partially loaded");
    auto rs=r::decodeSnapshot(*f.catalog,rb);rs.state.awards.erase(rs.state.awards.begin());auto missing=r::encodeSnapshot(*f.catalog,rs);
    w={};r::wire::put(w,source);w.number(seq);r::wire::blob(w,missing);r::wire::blob(w,qb);bad=r::wire::wrap(3,w.bytes);reject([&]{f.sample.game().restore(bad);});
    check(before==&f.sample.game().rpgState()&&qbefore==&f.sample.game().questState()&&f.sample.game().pending()==1,"receipt disagreement changed live state");
    for(std::size_t n=0;n<saved.size();n+=37)reject([&]{f.sample.game().restore(std::span(saved).first(n));});
    check(f.sample.game().pending()==1,"truncation consumed queue");
    f.sample.game().restore(saved);f.retry();reject([&]{f.sample.game().post(f.event(1));}); // Saved source sequence survives reload.
}
void callbacks(){
    auto cat=example::encounterCatalog();auto quests=example::rewardQuests();b::Coordinator*live=nullptr;std::vector<std::uint8_t> bytes;
    auto policy=example::rewardPolicy();policy.facts=[&](const r::State&){if(live)live->restore(bytes);return q::Fields{};};
    b::Coordinator game(cat,quests,r::SessionId(1),id("example.quest-awards"),policy);bytes=game.save();live=&game;
    const auto*before=&game.rpgState();reject([&]{game.prepare(game.command());});check(before==&game.rpgState(),"callback restore changed live state");
}
}
int main(){try{reload(false);reload(true);atomicFailure();callbacks();std::cout<<"R8 combined save checks: "<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
