#include "engine/performance_timeline.hpp"
#include "engine/ai_controller.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
class WaitTask final:public AiTask {
    double remaining_=0;
public:
    void start(AiControllerComponent&,const AiTaskSpec& s,AiEventScope)override{remaining_=std::get<double>(s.parameters.at("seconds"));}
    AiTaskStatus tick(AiControllerComponent&,double dt)override{remaining_-=dt;return remaining_<=1e-9?AiTaskStatus::Succeeded:AiTaskStatus::Running;}
};
class EventTask final:public AiTask {
    std::string key_;bool received_=false;
public:
    void start(AiControllerComponent&,const AiTaskSpec& s,AiEventScope)override{require(!s.event.empty(),"wait-event requires an observation event");key_=s.event;}
    void event(const AiEvent& e)noexcept override{if(e.key==key_)received_=true;}
    AiTaskStatus tick(AiControllerComponent&,double)override{return received_?AiTaskStatus::Succeeded:AiTaskStatus::Running;}
};
class FaceTask final:public AiTask {
    Vec3 target_{};
public:
    void start(AiControllerComponent&,const AiTaskSpec& s,AiEventScope)override{target_=std::get<Vec3>(s.parameters.at("target"));}
    AiTaskStatus tick(AiControllerComponent& c,double)override{
        auto* a=c.owner();require(a&&a->parent()==c.scene(),"face-target requires a scene-root actor");
        const auto position=transformPoint(a->worldMatrix(),{});auto d=target_-position;
        if(std::hypot(d.x,d.z)>1e-5)a->setWorldTransform(Transform{position,{0,std::atan2(d.x,d.z),0},{1,1,1}});
        return AiTaskStatus::Succeeded;
    }
};
class MoveTask final:public AiTask {
    std::unique_ptr<Navigator> navigator_;
public:
    void start(AiControllerComponent& c,const AiTaskSpec& s,AiEventScope)override{
        require(bool(c.navigationSource()),"move-to requires an application-supplied NavigationSource");
        NavigationSettings settings;settings.following.speed=float(std::get<double>(s.parameters.at("speed")));settings.following.arrivalRadius=float(std::get<double>(s.parameters.at("distance")));settings.maxSeconds=settings.following.maxSeconds=s.timeout;
        navigator_=std::make_unique<Navigator>(settings);navigator_->navigateSource(c.navigationSource(),std::get<Vec3>(s.parameters.at("target")));
    }
    AiTaskStatus tick(AiControllerComponent& c,double dt)override{
        auto* m=c.motor();require(m&&m->isActiveInHierarchy(),"move-to motor unavailable");navigator_->step(*m,dt);
        switch(navigator_->status()){
        case NavigationStatus::Arrived:return AiTaskStatus::Succeeded;
        case NavigationStatus::Unreachable:case NavigationStatus::BudgetExceeded:case NavigationStatus::TimedOut:case NavigationStatus::Cancelled:return AiTaskStatus::Failed;
        default:return AiTaskStatus::Running;}
    }
    std::string_view error()const noexcept override{return "Navigation could not reach the target within its budgets";}
    void cancel(AiControllerComponent& c)noexcept override{if(navigator_)if(auto* m=c.motor())navigator_->cancel(*m);}
};
}
std::vector<AiTaskBinding> builtinAiTasks(){return {
    {"wait",{{"seconds",AiParameterType::Number,1.0,0,86400}},false,[]{return std::make_unique<WaitTask>();}},
    {"wait-event",{},false,[]{return std::make_unique<EventTask>();}},
    {"face-target",{{"target",AiParameterType::Vector,Vec3{0,0,1},-10000,10000}},true,[]{return std::make_unique<FaceTask>();}},
    {"move-to",{{"target",AiParameterType::Vector,Vec3{1,0,0},-10000,10000},{"speed",AiParameterType::Number,2.0,.01,100},{"distance",AiParameterType::Number,.15,.01,10}},true,[]{return std::make_unique<MoveTask>();}}
};}
const AiTaskBinding& AiBindingRegistry::task(std::string_view key)const{for(const auto& t:tasks_)if(t.key==key)return t;throw std::invalid_argument("Missing AI task binding: "+std::string(key));}
AiTaskSpec AiBindingRegistry::taskDefaults(std::string_view key)const{AiTaskSpec s;s.key=key;for(const auto& p:task(key).parameters)s.parameters[p.key]=p.initial;return s;}
void AiBindingRegistry::validateTask(const AiTaskSpec& s,const StateMachineDefinition& graph)const{
    const auto& ps=task(s.key).parameters;require(ps.size()==s.parameters.size(),"Task parameters do not match registry schema");
    for(const auto& p:ps){auto it=s.parameters.find(p.key);require(it!=s.parameters.end(),"Missing task parameter");validateAiValue(p,it->second);}
    for(const auto&[p,k]:s.sources){auto parameter=std::find_if(ps.begin(),ps.end(),[&](const auto& p2){return p2.key==p;});const auto& fields=graph.data().blackboard;auto field=std::find_if(fields.begin(),fields.end(),[&](const auto& f){return f.key==k;});require(parameter!=ps.end()&&field!=fields.end()&&parameter->type==field->type,"Invalid task parameter source");if(field->type==AiParameterType::Number||field->type==AiParameterType::Vector)require(field->minimum>=parameter->minimum&&field->maximum<=parameter->maximum,"Task source bounds exceed parameter schema");}
    require(s.key!="wait-event"||!s.event.empty(),"wait-event requires an observation event");
}
AiControllerComponent::~AiControllerComponent(){stop();}
void AiControllerComponent::setNavigationSource(std::shared_ptr<const NavigationSource> source){require(!machine_->executing(),"Cannot replace navigation from a runtime callback");if(source==navigationSource_)return;stop();navigationSource_=std::move(source);}
void AiControllerComponent::recordTask(const AiTaskRecord& record)noexcept{try{if(taskHistory_.size()==32)taskHistory_.erase(taskHistory_.begin());taskHistory_.push_back(record);}catch(...){} }
std::vector<AiTaskRecord> AiControllerComponent::activeTasks()const{std::vector<AiTaskRecord> result;for(const auto&[id,run]:taskRuns_)if(run.task||run.terminal)result.push_back(run.record);return result;}
void AiControllerComponent::cancelTasks(std::uint32_t state)noexcept{
    auto it=taskRuns_.find(state);if(it==taskRuns_.end())return;auto& run=it->second;
    if(run.task){run.task->cancel(*this);run.record.status=AiTaskStatus::Canceled;recordTask(run.record);}taskRuns_.erase(it);
}
void AiControllerComponent::event(const AiEvent& e,std::optional<AiEventScope> scope){for(auto&[id,run]:taskRuns_)if(run.task&&(!scope||(scope->instance==run.scope.instance&&scope->activation==run.scope.activation)))run.task->event(e);}
void AiControllerComponent::updateTasks(const AiState& s,double dt){
    ProfileScope profile(ProfileCategory::AI,"AI task sequence",s.name);
    if(stopRequested_||!isActiveInHierarchy())return;
    auto it=taskRuns_.find(s.id);if(it==taskRuns_.end())return;auto& run=it->second;
    if(!run.terminal){
        const auto& spec=s.tasks.at(run.index);
        ProfileScope taskProfile(ProfileCategory::AI,"AI task",spec.key);
        try{
            if(!run.task){auto resolved=spec;for(const auto&[p,k]:spec.sources)resolved.parameters.at(p)=blackboard_.at(k);run.record={s.id,run.index,spec.key,AiTaskStatus::Running,0,{},false};run.task=registry_->task(spec.key).create();require(bool(run.task),"AI task factory returned null");run.task->start(*this,resolved,run.scope);recordTask(run.record);}
            if(stopRequested_||!isActiveInHierarchy())return;
            run.seconds+=dt;run.record.seconds=run.seconds;
            // Let a task succeed exactly at its deadline; a running result times out.
            if(run.seconds>spec.timeout+1e-9){run.record.status=AiTaskStatus::Failed;run.record.error="Task timed out";}else run.record.status=run.task->tick(*this,dt);
            require(run.record.status==AiTaskStatus::Running||run.record.status==AiTaskStatus::Succeeded||run.record.status==AiTaskStatus::Failed,"Tasks must return Running, Succeeded or Failed");
            if(run.record.status==AiTaskStatus::Running&&run.seconds>=spec.timeout){run.record.status=AiTaskStatus::Failed;run.record.error="Task timed out";}
            if(run.record.status==AiTaskStatus::Failed&&run.record.error.empty())run.record.error=run.task->error();
        }catch(const std::exception& e){run.record.status=AiTaskStatus::Failed;run.record.error=e.what();}catch(...){run.record.status=AiTaskStatus::Failed;run.record.error="AI task callback failed";}
        if(stopRequested_||!isActiveInHierarchy())return;
        if(run.record.status==AiTaskStatus::Running)return;
        if(run.task){run.task->cancel(*this);run.task.reset();}recordTask(run.record);
        if(run.record.status==AiTaskStatus::Succeeded&&++run.index<s.tasks.size()){run.seconds=0;return;} // At most one task tick per state per fixed step.
        run.terminal=true;run.signal=run.record.status==AiTaskStatus::Succeeded?s.succeededEvent:s.failedEvent;
    }
    run.record.deliveryPending=!run.signal.empty();
    if(!run.signal.empty()&&postEvent({run.signal,{}},run.scope)){run.signal.clear();run.record.deliveryPending=false;}
}
} // namespace engine
