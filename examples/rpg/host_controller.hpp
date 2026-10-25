#pragma once
#include "host_scene.hpp"
#include "engine/application.hpp"
#include <deque>
#include <limits>

namespace rpg_example {
namespace q=engine::quests;
enum class Action { Begin, Move, Strike, EndTurn, Equip, Deliver, Pause, Travel, Save, Load, Recover, Finish, AiStep };
inline constexpr const char* actionNames[]={"Begin encounter","Move one tile","Strike","End turn","Equip / unequip","Deliver parcel","Pause / resume","Travel","Save","Load","Recover view","Finish / exit","AI step"};
struct Storage {
    std::function<std::optional<std::vector<std::uint8_t>>()> read;
    std::function<void(std::span<const std::uint8_t>)> write;
};
class Controller {
    engine::Application& app_;
    Storage storage_;
    std::function<void(engine::SceneId,HostScene&)> registerView_;
    std::shared_ptr<const r::Catalog> catalog_=example::encounterCatalog(example::tacticalRules());
    std::shared_ptr<const q::Catalog> quests_=example::rewardQuests();
    std::unique_ptr<example::SavedGame> game_=fresh();
    std::shared_ptr<const engine::WaypointGraph> graph_=navigation();
    struct Pending {Action action;std::uint64_t epoch;};
    std::deque<Pending> queue_;
    std::uint64_t epoch_=1,world_=1;
    bool paused_=false,recover_=false;
    float aspect_=1.5f;
    struct Reservation {r::EntityId actor;std::vector<engine::Vec3> route;double elapsed=0;};
    std::optional<Reservation> reservation_;
    std::unique_ptr<example::SavedGame> fresh()const{return std::make_unique<example::SavedGame>(catalog_,quests_);}
    r::ContentId producer()const{return r::ContentId("example.host");}
    std::uint64_t sequence()const{auto i=state().producers.find(producer());if(i==state().producers.end())return 1;if(i->second==UINT64_MAX)throw std::runtime_error("Input sequence exhausted");return i->second+1;}
    void headroom()const{if(epoch_==UINT64_MAX||world_==UINT64_MAX)throw std::runtime_error("Host epoch exhausted");}
    r::Results apply(std::vector<r::Operation> ops){return game_->game().applyRpg(producer(),sequence(),std::move(ops));}
    void initialize(){
        auto player=apply({r::CreateCharacter{r::ContentId("example.scholar")}}).created[0];
        auto partner=apply({r::CreateCharacter{r::ContentId("example.scholar")}}).created[0];
        apply({r::ConfigureVitality{player,r::ContentId("example.reserve")},r::ConfigureVitality{partner,r::ContentId("example.reserve")}});
        apply({r::SetParty{r::ContentId("example.party"),{2,1,{{player,true,r::ControlMode::Player},{partner,false,r::ControlMode::Player}},player}}});
        auto bag=apply({r::CreateInventory{r::ContentId("example.pack"),player}}).inventories[0];
        apply({r::AddItem{bag,r::ContentId("example.blade"),1},r::AddItem{bag,r::ContentId("example.tonic"),2},
               r::SetTacticalPosition{player,{-2,0,0}},r::SetTacticalPosition{partner,{2,0,0}}});
        auto c=game_->game().command();c.batch.commands.push_back({"example.delivery",q::CommandKind::Activate,0,{}});auto t=game_->game().prepare(c);if(!game_->game().commit(std::move(t)))throw std::runtime_error("Quest activation failed");
    }
    static void validateSample(const example::SavedGame& sample){
        const auto&s=sample.game().rpgState();
        if(sample.checkpoint()>1||s.characters.size()!=2||s.positions.size()!=2||s.inventories.size()!=1)throw std::invalid_argument("Incompatible sample save");
        std::set<r::TacticalPosition> occupied;
        for(auto&[id,p]:s.positions)if(p.y||p.x< -4||p.x>4||p.z< -2||p.z>2||(!p.x&&!p.z)||!occupied.insert(p).second)throw std::invalid_argument("Invalid sample position");
    }
    void replaceScene(const r::State&s,unsigned location){
        auto candidate=std::make_unique<HostScene>(s,location);auto*view=candidate.get();auto id=app_.loadScene(std::move(candidate));
        try{registerView_(id,*view);app_.requestTransition(id,true);if(!app_.commitTransition())throw std::runtime_error("Scene not ready");}
        catch(...){app_.unloadScene(id);throw;}
    }
    r::SpatialSnapshot spatial(r::EntityId actor)const{
        r::SpatialSnapshot snap{{world_,1}};
        for(auto id:state().encounter->roster){auto p=state().positions.at(id);snap.entities.push_back({id,p,true});
            const auto a=state().positions.at(actor);if(std::abs(a.x-p.x)+std::abs(a.z-p.z)<=6)snap.legalTargets.push_back(id);}
        return snap;
    }
    void tactical(bool move){
        if(reservation_||state().presentation)throw std::invalid_argument("Wait for presentation");
        if(!state().encounter||!state().encounter->active)throw std::invalid_argument("Begin an encounter first");
        const auto&e=*state().encounter;auto actor=*e.active;auto start=state().positions.at(actor);auto snapshot=spatial(actor);
        std::optional<r::PathQuote> path;std::vector<engine::Vec3> route;
        if(move){
            auto end=start;end.z=end.z==0?1:0;
            engine::WaypointSearchSettings settings;settings.snapDistance=.05f;settings.expansionBudget=128;settings.segmentBudget=256;
            settings.canTraverse=[&](engine::Vec3 from,engine::Vec3 to){
                for(auto&[id,p]:state().positions)if(id!=actor){auto v=point(p);if(engine::dot(v-from,v-from)<.01f||engine::dot(v-to,v-to)<.01f)return false;}return true;};
            auto found=graph_->findPath(point(start),point(end),settings);
            if(found.status!=engine::PathStatus::Complete)throw std::invalid_argument("Path blocked or unavailable");
            r::PathQuote p;p.start=start;p.destination=end;p.reachable=true;p.destinationResident=true;p.destinationFree=true;
            for(auto v:found.points){r::TacticalPosition vq{std::llround(v.x),0,std::llround(v.z)};if(p.route.empty()||p.route.back()!=vq){p.route.push_back(vq);route.push_back(point(vq));}}
            p.cost=static_cast<std::int64_t>(p.route.size()-1);path=std::move(p);
        }else {
            route={point(start),point(start)};
            auto target=e.roster.front()==actor?e.roster.back():e.roster.front();
            snapshot.targeting=r::TargetQuery{r::SphereTargets{state().positions.at(target),1},6,e.teams.empty()?r::TargetTeam::Any:r::TargetTeam::Opponents,false,true};
        }
        auto quote=game_->game().spatialQuote(actor,std::move(snapshot),std::move(path));
        r::TacticalAction action{actor,e.epoch,e.turnEpoch,r::MoveAction{},quote};
        if(!move)action.action=r::UseAbility{actor,r::ContentId("example.strike"),r::selectTargets(state(),quote->snapshot(),actor,*quote->snapshot().targeting),{}};
        // Sole gameplay writer. Reserve all route cells/destination before final observation.
        // No motor, AI or physics body writes these presentation actors.
        reservation_.emplace(Reservation{actor,std::move(route),0});
        try{auto t=game_->game().prepareRpg(producer(),sequence(),{action},{},r::WorldStamp{world_,1});
            if(!game_->game().commitRpg(std::move(t),r::WorldStamp{world_,1}))throw std::runtime_error("World changed before commit");}
        catch(...){reservation_.reset();throw;}
        project();
    }
    void aiStep(){
        if(reservation_||!state().encounter||!state().encounter->active)throw std::invalid_argument("No available combat turn");
        const auto&e=*state().encounter;const auto actor=*e.active;auto snapshot=spatial(actor);
        auto quote=game_->game().spatialQuote(actor,snapshot);std::vector<r::CombatOption> options;
        for(std::size_t i=0;i<e.roster.size();++i){auto target=e.roster[i];
            if(target!=actor&&r::canAct(state(),target)&&(e.teams.empty()||r::teamRelation(state(),actor,target)==r::TeamRelation::Opponent))
                options.push_back({r::ContentId("example.strike."+std::to_string(i)),{actor,e.epoch,e.turnEpoch,r::UseAbility{actor,r::ContentId("example.strike"),{target},{}},quote}});
        }
        // Authored sample policy prefers actual damage; no legal strike ends the turn.
        auto plan=game_->game().planCombat(producer(),sequence(),actor,options,[](const r::CombatScoreContext& c)->std::optional<std::int64_t>{
            std::int64_t value=0;for(const auto& effect:c.results.resources)
                if(effect.entity!=c.option.action.actor&&effect.resource==r::ContentId("example.reserve")&&effect.delta.value<0)value-=effect.delta.value;
            return value>0?std::optional<std::int64_t>(value):std::nullopt;
        },{},r::WorldStamp{world_,1});
        if(plan.status()!=r::CombatPlanStatus::Ready)throw std::invalid_argument("No AI action available");
        auto tx=game_->game().prepareCombat(plan,r::WorldStamp{world_,1});
        if(!plan.endTurn()){auto p=point(state().positions.at(actor));reservation_.emplace(Reservation{actor,{p,p},0});}
        if(!game_->game().commitRpg(std::move(tx),r::WorldStamp{world_,1})){reservation_.reset();throw std::runtime_error("AI plan became stale");}
    }
public:
    std::string message="Ready";
    std::size_t saves=0,loads=0,discarded=0;
    Controller(engine::Application&app,Storage storage,std::function<void(engine::SceneId,HostScene&)> registerView)
        :app_(app),storage_(std::move(storage)),registerView_(std::move(registerView)){initialize();}
    const r::State&state()const{return game_->game().rpgState();}
    r::EntityId player()const{return state().characters.begin()->first;}
    unsigned location()const{return static_cast<unsigned>(game_->checkpoint());}
    std::uint64_t epoch()const{return epoch_;}
    bool paused()const{return paused_;}
    bool reserved()const{return reservation_.has_value();}
    std::size_t awards()const{return state().awards.size();}
    HostScene&scene(){auto*s=dynamic_cast<HostScene*>(app_.activeScene());if(!s)throw std::runtime_error("No RPG scene");return *s;}
    void start(){
        try{if(auto bytes=storage_.read()){auto candidate=fresh();candidate->load(*bytes);validateSample(*candidate);game_.swap(candidate);++loads;message="Loaded save";}}
        catch(const std::exception&e){message=std::string("Load failed: ")+e.what();}
        auto s=std::make_unique<HostScene>(state(),location());auto*v=s.get();auto id=app_.loadScene(std::move(s));registerView_(id,*v);app_.start(id);
    }
    void cancelInput(){headroom();++epoch_;discarded+=queue_.size();queue_.clear();recover_=true;}
    void enqueue(Action action){if(queue_.size()>=32)throw std::runtime_error("Input queue full");queue_.push_back({action,epoch_});}
    void orbit(float yaw,float elevation=0,float zoom=0){scene().inspect(yaw,elevation,zoom);}
    void aspect(float value){if(std::isfinite(value)&&value>0)aspect_=value;}
    void project(){
        std::optional<r::EntityId> moving;engine::Vec3 visual{};
        if(reservation_){auto&r=*reservation_;moving=r.actor;float t=static_cast<float>(std::clamp(r.elapsed/.45,0.,1.));
            float part=t*float(r.route.size()-1);auto i=std::min(static_cast<std::size_t>(part),r.route.size()-2);visual=r.route[i]*(1-(part-float(i)))+r.route[i+1]*(part-float(i));}
        scene().project(state(),moving,visual);
    }
    void recover(r::PresentationCompletion reason=r::PresentationCompletion::Recovered){
        scene().project(state()); // Rebuild first; only then release input/reservations.
        if(state().presentation){auto p=*state().presentation;apply({r::AcknowledgePresentation{p.actionEpoch,p.encounterEpoch,p.turnEpoch,reason}});}
        reservation_.reset();recover_=false;
    }
    std::vector<std::uint8_t> saveBytes()const{return game_->save();}
    void load(std::span<const std::uint8_t>bytes,std::uint64_t expected){
        if(expected!=epoch_)throw std::invalid_argument("Stale load");headroom();auto candidate=fresh();candidate->load(bytes);validateSample(*candidate);
        replaceScene(candidate->game().rpgState(),static_cast<unsigned>(candidate->checkpoint()));
        game_.swap(candidate);reservation_.reset();cancelInput();recover_=false;++world_;app_.resetClocks();++loads;message="Loaded";
    }
    void execute(Action action){
        if(action==Action::Pause){paused_=!paused_;if(paused_)app_.pause();else app_.resume();cancelInput();message=paused_?"Paused":"Running";return;}
        if(action==Action::Recover){recover();return;}
        if(action==Action::Travel){headroom();recover();auto next=1-location();replaceScene(state(),next);game_->checkpoint(next);cancelInput();recover_=false;++world_;message="Travelled";return;}
        if(action==Action::Save){auto bytes=saveBytes();storage_.write(bytes);++saves;message="Saved";return;}
        if(action==Action::Load){auto bytes=storage_.read();if(!bytes)throw std::invalid_argument("No save");load(*bytes,epoch_);return;}
        if(paused_||app_.suspended())throw std::invalid_argument("Resume to play");
        if(state().presentation)throw std::invalid_argument("Wait for presentation");
        switch(action){
            case Action::Begin:{std::vector<r::EntityId> roster;for(auto&[id,c]:state().characters)roster.push_back(id);apply({r::BeginEncounter{{r::ContentId("example.turns"),1},roster,{{roster.front(),{r::ContentId("example.blue"),r::ControlMode::Player}},{roster.back(),{r::ContentId("example.red"),r::ControlMode::Player}}}}});break;}
        case Action::Move:tactical(true);break;
        case Action::Strike:tactical(false);break;
        case Action::AiStep:aiStep();break;
        case Action::EndTurn:{if(!state().encounter||!state().encounter->active)throw std::invalid_argument("No active turn");auto e=*state().encounter;apply({r::EndTurn{*e.active,e.epoch,e.turnEpoch}});break;}
        case Action::Equip:{for(auto&[id,item]:state().items)if(item.definition==r::ContentId("example.blade")){if(item.slot)apply({r::UnequipItem{item.inventory,id}});else apply({r::EquipItem{item.inventory,id,r::ContentId("example.hand")}});break;}break;}
        case Action::Deliver:{auto&g=game_->game();if(g.questState().quests.at("example.delivery").earned)throw std::invalid_argument("Parcel already rewarded");if(!g.pending())g.post(g.event({"example.delivery","example.delivery","example.delivery",1,{{"item",std::string("parcel")}}}));auto tx=g.prepare(g.command());if(!g.commit(std::move(tx)))throw std::runtime_error("Reward rejected");break;}
        case Action::Finish:{if(!state().encounter)throw std::invalid_argument("No encounter");auto e=*state().encounter;if(e.stage==r::EncounterStage::AwaitingCommand)apply({r::FinishEncounter{e.epoch,e.turnEpoch}});apply({r::ExitEncounter{e.epoch,e.turnEpoch}});break;}
        default:throw std::invalid_argument("Unknown action");}
        message="Updated";project();
    }
    void drain(){
        if(recover_)recover();else if(reservation_&&reservation_->elapsed>=.45)recover(r::PresentationCompletion::Completed);
        auto pending=std::move(queue_);queue_.clear();
        for(auto p:pending){if(p.epoch!=epoch_){++discarded;continue;}try{execute(p.action);}catch(const std::exception&e){message=std::string("Action failed: ")+e.what();}}
    }
    engine::ApplicationFrameCallbacks callbacks(){engine::ApplicationFrameCallbacks cb;
        cb.simulate=[this](auto&,const engine::ApplicationFrame&f){if(reservation_)reservation_->elapsed+=f.simulation.delta;};
        cb.present=[this](auto&,const engine::ApplicationFrame&f){project();scene().rig->update(f.presentation.delta,aspect_);};return cb;}
};
} // namespace rpg_example
