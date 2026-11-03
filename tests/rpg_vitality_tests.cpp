#include "../examples/rpg/tactics.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace r=engine::rpg;
namespace {
int checks=0;const r::WorldStamp world{10,1};
r::ContentId id(const char*s){return r::ContentId(s);}
r::Amount points(std::int64_t n){return {n,id("example.point"),1};}
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
template<class F>void rejects(r::ErrorCode code,F fn){try{fn();}catch(const r::Error&e){check(e.code()==code,e.what());return;}throw std::runtime_error("Expected rejection");}
r::Transaction prepare(r::Runtime&run,std::vector<r::Operation> ops,r::Budget budget={},std::optional<r::WorldStamp> stamp={}){
    auto it=run.state().producers.find(id("test.input"));return run.prepare(run.command(id("test.input"),it==run.state().producers.end()?1:it->second+1,std::move(ops)),budget,stamp);
}
r::Results apply(r::Runtime&run,std::vector<r::Operation> ops,std::optional<r::WorldStamp> stamp={}){auto tx=prepare(run,std::move(ops),{},stamp);auto result=tx.results();check(run.commit(std::move(tx),stamp),"commit");return result;}
void reject(r::Runtime&run,r::ErrorCode code,std::vector<r::Operation> ops,r::Budget budget={},std::optional<r::WorldStamp> stamp={}){
    auto before=&run.state();auto random=run.state().random;rejects(code,[&]{prepare(run,std::move(ops),budget,stamp);});check(before==&run.state()&&random==run.state().random,"Failure published state/RNG");
}
std::shared_ptr<const r::Catalog> catalog(){
    auto defs=example::abilityDefinitions();
    r::AbilityDefinition revive{id("test.revive"),{id("example.effects"),1}};
    revive.parameters.emplace(id("example.delta"),points(5));revive.costs.push_back({id("example.reserve"),points(2)});defs.abilities.push_back(revive);
    for(auto name:{"test.poison","test.regeneration"}){r::StatusDefinition status{id(name)};status.phase=r::TurnPhase::RoundEnd;status.duration=2;status.tickEffects.push_back({id("example.reserve"),points(std::string(name)=="test.poison"?-100:100)});defs.statuses.push_back(status);}
    auto policy=example::tacticalRules();policy.eligible=[](const auto&,auto){return true;}; // Kernel must still gate incapacitated actors.
    policy.decide=[](const r::EncounterContext& c,r::EncounterCheck check){
        if(check==r::EncounterCheck::Exit)return r::EncounterDecision{true,c.encounter.outcome};
        if(check==r::EncounterCheck::Finish)return r::EncounterDecision{true,r::EncounterOutcome::Escape};
        if(r::teamDefeated(c.state,id("test.blue")))return r::EncounterDecision{true,r::EncounterOutcome::Defeat};
        if(r::teamDefeated(c.state,id("test.red")))return r::EncounterDecision{true,r::EncounterOutcome::Victory};
        return r::EncounterDecision{};
    };
    engine::ModuleRegistration<r::Bindings> module;module.descriptor.id="test.vital-rules";
    module.descriptor.dependencies.push_back({"webengine.rpg",{{1,10,0},{2,0,0}}});
    module.install=[policy](r::Bindings& b){
        b.addAdvancementHandler(id("example.unlocks"),{1,example::unlocks});
        b.addAbilityHandler(id("example.effects"),{1,1,example::validateAbility,[](const r::AbilityContext& c,r::Random& random){
            if(c.definition.id!=id("test.revive"))return example::effects(c,random);
            r::EffectProposal p;for(auto target:c.targets)p.effects.push_back(r::ReviveEffect{target,points(5)});return p;
        }});
        b.addEncounterHandler(id("example.turns"),policy);
    };
    r::Composition composition;const std::array modules{r::moduleRegistration(),module};return r::Catalog::create(defs,composition.replace(modules));
}
struct Fixture {
    std::shared_ptr<const r::Catalog> defs=catalog();r::Runtime run{defs,r::SessionId(1),42};std::vector<r::EntityId> actors;
    explicit Fixture(bool configured=true){for(int i=0;i<3;++i){auto a=apply(run,{r::CreateCharacter{id("example.scholar")}}).created[0];actors.push_back(a);apply(run,{r::SetTacticalPosition{a,{i,0,0}}});if(configured)apply(run,{r::ConfigureVitality{a,id("example.reserve")}});}}
    std::int64_t hp(std::size_t i)const{return run.state().characters.at(actors[i]).resources.at(id("example.reserve")).current;}
    r::LifeState life(std::size_t i)const{return run.state().vitality.at(actors[i]).condition;}
    void begin(){r::BeginEncounter e{{id("example.turns"),1},actors};for(std::size_t i=0;i<actors.size();++i)e.teams.emplace(actors[i],r::CombatMember{id(i?"test.red":"test.blue")});apply(run,{e});}
    void end(){const auto&e=*run.state().encounter;apply(run,{r::EndTurn{*e.active,e.epoch,e.turnEpoch}});}
    r::TacticalAction ability(const char* name,r::EntityId target){const auto&e=*run.state().encounter;r::SpatialSnapshot s{world};for(auto a:actors){s.entities.push_back({a,run.state().positions.at(a),true});s.legalTargets.push_back(a);}return {*e.active,e.epoch,e.turnEpoch,r::UseAbility{*e.active,id(name),{target},{}},run.spatialQuote(*e.active,s)};}
    void ack(){auto p=*run.state().presentation;apply(run,{r::AcknowledgePresentation{p.actionEpoch,p.encounterEpoch,p.turnEpoch}});}
};
void lifecycle(){
    Fixture f;auto a=f.actors[0];const auto full=f.hp(0);
    auto result=apply(f.run,{r::ApplyVitalEffect{r::DamageEffect{a,points(3)}}});check(f.hp(0)==full-3&&result.vitality[0].applied==3,"Damage result");
    result=apply(f.run,{r::ApplyVitalEffect{r::HealEffect{a,points(std::numeric_limits<std::int64_t>::max())}}});check(f.hp(0)==full&&result.vitality[0].applied==3,"Healing cap/overflow");
    result=apply(f.run,{r::ApplyVitalEffect{r::DamageEffect{a,points(std::numeric_limits<std::int64_t>::max())}}});check(f.hp(0)==0&&f.life(0)==r::LifeState::Downed&&result.vitality[0].applied==full,"Overkill/depletion");
    check(!r::canAct(f.run.state(),a)&&!r::canAct(f.run.state(),{r::SessionId(2),1}),"Act eligibility");
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ApplyVitalEffect{r::HealEffect{a,points(1)}}});
    reject(f.run,r::ErrorCode::InvalidVitality,{r::AdjustResource{a,id("example.reserve"),points(1)}});
    reject(f.run,r::ErrorCode::InvalidAction,{r::UseAbility{a,id("example.strike"),{f.actors[1]},{}}});
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ApplyVitalEffect{r::ReviveEffect{a,points(0)}}});
    apply(f.run,{r::ApplyVitalEffect{r::ReviveEffect{a,points(7)}}});check(f.hp(0)==7&&r::canAct(f.run.state(),a),"Revival");
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ApplyVitalEffect{r::ReviveEffect{a,points(1)}}});
    apply(f.run,{r::ApplyVitalEffect{r::DefeatEffect{a,r::LifeState::Dead}}});
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ApplyVitalEffect{r::ReviveEffect{a,points(3)}}});
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ApplyVitalEffect{r::DefeatEffect{a,r::LifeState::Downed}}});
    apply(f.run,{r::ApplyVitalEffect{r::ReviveEffect{a,points(3),true}}});check(f.hp(0)==3&&f.life(0)==r::LifeState::Alive,"Explicit resurrection");
    reject(f.run,r::ErrorCode::InvalidAmount,{r::ApplyVitalEffect{r::DamageEffect{a,points(-1)}}});
    reject(f.run,r::ErrorCode::InvalidAmount,{r::ApplyVitalEffect{r::HealEffect{a,{1,id("test.wrong"),1}}}});
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ApplyVitalEffect{r::DefeatEffect{a,r::LifeState::Alive}}});
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ConfigureVitality{a,id("example.reserve")}});
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ApplyVitalEffect{r::DamageEffect{a,points(100)}},r::ApplyVitalEffect{r::HealEffect{a,points(1)}}});
    check(f.hp(0)==3,"Partial damage escaped rejected command");
    apply(f.run,{r::AdjustResource{a,id("example.reserve"),points(-3)}});check(f.life(0)==r::LifeState::Downed,"Raw zero did not reconcile");
    Fixture legacy(false);auto who=legacy.actors[0];
    reject(legacy.run,r::ErrorCode::InvalidVitality,{r::ConfigureVitality{who,id("test.missing")}});
    reject(legacy.run,r::ErrorCode::InvalidVitality,{r::ConfigureVitality{who,id("example.reserve"),r::LifeState::Alive}});
    apply(legacy.run,{r::ConfigureVitality{who,id("example.reserve"),r::LifeState::Dead},r::ApplyVitalEffect{r::DamageEffect{who,points(100)}}});check(legacy.life(0)==r::LifeState::Dead,"Immediate death policy");
}
void combat(){
    Fixture f;f.begin();auto a=f.actors[0],b=f.actors[1],c=f.actors[2];
    reject(f.run,r::ErrorCode::InvalidVitality,{r::ConfigureVitality{a,id("example.reserve")}});
    auto strike=f.ability("example.strike",b);r::Budget budget;budget.effects=1;
    reject(f.run,r::ErrorCode::BudgetExceeded,{strike},budget,world);
    apply(f.run,{r::ApplyVitalEffect{r::DamageEffect{b,points(100)}}});f.end();check(f.run.state().encounter->active==c,"Permissive policy let downed actor act");
    const auto before=f.hp(2);auto revive=f.ability("test.revive",b);auto tx=prepare(f.run,{revive},{},world);
    check(!f.run.commit(std::move(tx),r::WorldStamp{10,2})&&f.hp(1)==0,"Stale revival published");
    check(f.run.commit(std::move(tx),world),"Revival commit");
    check(f.hp(1)==5&&f.hp(2)==before-2&&f.run.state().encounter->allowance.ap==1,"Revival costs/health");
    f.ack();f.end();f.end();check(f.run.state().encounter->active==b,"Revived actor did not rejoin turn order");
    apply(f.run,{r::ApplyVitalEffect{r::DefeatEffect{a,r::LifeState::Downed}}});
    check(r::teamDefeated(f.run.state(),id("test.blue"))&&!r::teamDefeated(f.run.state(),id("test.absent"))&&f.run.state().encounter->outcome==r::EncounterOutcome::Defeat,"Team defeat policy");
    Fixture victory;victory.begin();apply(victory.run,{r::ApplyVitalEffect{r::DefeatEffect{victory.actors[1]}},r::ApplyVitalEffect{r::DefeatEffect{victory.actors[2]}}});check(victory.run.state().encounter->outcome==r::EncounterOutcome::Victory,"Team victory policy");
}
void periodicAndSaves(){
    Fixture f;auto a=f.actors[0];auto legacy=Fixture(false);check(legacy.run.save()[8]==1,"Legacy v1 changed");
    apply(legacy.run,{r::SetParty{id("test.party"),{}}});check(legacy.run.save()[8]==2,"L2 v2 changed");
    auto old=legacy.run.save();legacy.run.restore(old);check(legacy.run.state().vitality.empty(),"Legacy load invented vitality");
    apply(f.run,{r::ApplyStatus{a,id("test.poison"),id("test.poison-source")},r::AdvancePhase{r::TurnPhase::RoundEnd,{},1}});
    check(f.hp(0)==0&&f.life(0)==r::LifeState::Downed,"Periodic overkill");
    apply(f.run,{r::ApplyStatus{a,id("test.regeneration"),id("test.regen-source")},r::AdvancePhase{r::TurnPhase::RoundEnd,{},2}});
    check(f.hp(0)==0&&f.life(0)==r::LifeState::Downed,"Periodic implicit resurrection");
    auto bytes=f.run.save();check(bytes[8]==3,"Vitality wire version");auto snap=r::decodeSnapshot(*f.defs,bytes);check(r::encodeSnapshot(*f.defs,snap)==bytes,"Vitality canonical roundtrip");
    auto invalid=snap;invalid.state.vitality.at(a).condition=r::LifeState::Alive;
    rejects(r::ErrorCode::InvalidSnapshot,[&]{f.run.restore(invalid);});
    invalid=snap;invalid.state.vitality.at(a).resource=id("test.missing");rejects(r::ErrorCode::InvalidSnapshot,[&]{f.run.restore(invalid);});
    invalid=snap;invalid.state.vitality.at(a).onZero=r::LifeState::Alive;rejects(r::ErrorCode::InvalidSnapshot,[&]{f.run.restore(invalid);});
    f.run.restore(bytes);check(f.life(0)==r::LifeState::Downed&&f.hp(0)==0,"Restore lost defeat state");
    apply(f.run,{r::ApplyVitalEffect{r::ReviveEffect{a,points(4)}}});
    apply(f.run,{r::AdvancePhase{r::TurnPhase::RoundEnd,{},3}});check(f.hp(0)==f.run.state().characters.at(a).resources.at(id("example.reserve")).maximum,"Periodic healing clamp");
}
}
int main(){try{lifecycle();combat();periodicAndSaves();std::cout<<"L4 vitality checks: "<<checks<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
