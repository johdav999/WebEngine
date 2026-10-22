#pragma once
#include "engine/state_machine.hpp"
#include "engine/ai.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/scene_archive.hpp"
#include <map>
#include <variant>
namespace engine {
struct AiBinding {std::string key;std::map<std::string,AiValue> parameters;std::map<std::string,std::string> sources;};
class AiControllerComponent;
enum class AiTaskStatus { Running,Succeeded,Failed,Canceled };
// Per-activation work. All calls are synchronous on the scene owner. cancel must
// release persistent commands/subscriptions, including after success or failure.
class AiTask {
public:
    virtual ~AiTask()=default;
    virtual void start(AiControllerComponent&,const AiTaskSpec&,AiEventScope) {}
    virtual AiTaskStatus tick(AiControllerComponent&,double)=0;
    virtual void event(const AiEvent&) noexcept {}
    virtual std::string_view error()const noexcept{return {};}
    virtual void cancel(AiControllerComponent&) noexcept {}
};
struct AiTaskBinding {
    std::string key;std::vector<AiParameter> parameters;bool needsMotor=false;
    std::function<std::unique_ptr<AiTask>()> create;
};
struct AiTaskRecord {std::uint32_t state=0;std::size_t index=0;std::string key;AiTaskStatus status=AiTaskStatus::Running;double seconds=0;std::string error;bool deliveryPending=false;};
struct AiBehaviorBinding {
    std::string key;std::vector<AiParameter> parameters;bool needsMotor=false;
    std::function<void(AiControllerComponent&,const AiBinding&,double)> enter,update,exit;
};
struct AiConditionBinding {
    std::string key;std::vector<AiParameter> parameters;
    std::function<bool(AiControllerComponent&,const AiBinding&)> evaluate;
};
class AiBindingRegistry {
public:
    static std::shared_ptr<const AiBindingRegistry> create(std::vector<AiBehaviorBinding>,std::vector<AiConditionBinding>,std::vector<AiTaskBinding> tasks={});
    static std::shared_ptr<const AiBindingRegistry> builtins();
    const auto& tasks()const{return tasks_;}
    const AiTaskBinding& task(std::string_view)const;
    AiTaskSpec taskDefaults(std::string_view)const;
    void validateTask(const AiTaskSpec&,const StateMachineDefinition&)const;
    const auto& behaviors()const{return behaviors_;}const auto& conditions()const{return conditions_;}
    const AiBehaviorBinding& behavior(std::string_view)const;
    const AiConditionBinding& condition(std::string_view)const;
    AiBinding defaults(std::string_view key,bool condition=false)const;
    void validate(const AiBinding&,bool condition=false)const;
private:std::vector<AiTaskBinding> tasks_;std::vector<AiBehaviorBinding> behaviors_;std::vector<AiConditionBinding> conditions_;
};
struct AiControllerSettings {
    std::shared_ptr<const StateMachineDefinition> graph;
    std::map<std::uint32_t,AiBinding> states;
    std::map<std::string,AiBinding> conditions;
    static AiControllerSettings defaults(std::shared_ptr<const StateMachineDefinition>,const AiBindingRegistry&);
    void validate(const AiBindingRegistry&)const;
};
struct AiTransitionRecord {std::uint32_t from=0,to=0;double simulationSeconds=0;std::string condition;std::uint32_t transitionId=0,source=0,target=0;std::string event;std::uint64_t eventSequence=0;};
class AiControllerComponent final:public Component,private StateMachineHost {
public:
    AiControllerComponent(std::string name,AiControllerSettings,std::shared_ptr<const AiBindingRegistry> registry=AiBindingRegistry::builtins());
    ~AiControllerComponent() override;
    std::string_view typeName()const noexcept override{return "AiControllerComponent";}
    int fixedTickOrder()const noexcept override{return -100;}
    const AiControllerSettings& settings()const{return settings_;}
    const auto& registry()const{return registry_;}
    void configure(AiControllerSettings); // Validate fully before stopping/replacing.
    const std::map<std::string,AiValue>& blackboard()const{return blackboard_;}
    const AiValue& blackboardValue(std::string_view key)const;
    // Owner-thread atomic patch. Does not restart the machine or reset timers.
    void setBlackboard(const std::map<std::string,AiValue>& patch);
    bool postEvent(AiEvent,std::optional<AiEventScope> scope={});
    auto eventScope(std::uint32_t state)const{return machine_->eventScope(state);}
    const AiEvent* currentEvent()const{return machine_->currentEvent();}
    const auto& eventHistory()const{return machine_->eventHistory();}
    std::size_t pendingEvents()const{return machine_->pendingEvents();}
    const auto& taskHistory()const{return taskHistory_;}
    std::vector<AiTaskRecord> activeTasks()const;
    // Runtime integration, never serialized. Replacing the source cancels work.
    void setNavigationSource(std::shared_ptr<const NavigationSource>);
    const auto& navigationSource()const{return navigationSource_;}
    void validate()const override;
    void validateReferences(const Scene&)const;
    void stop()noexcept;
    void restart();
    StateMachineStatus status()const{return machine_->status();}
    std::uint32_t currentState()const{return machine_->current();}
    double stateSeconds()const{return machine_->stateSeconds();}
    auto activePath()const{return machine_->activePath();}
    const auto& transitions()const{return transitions_;}
    const std::string& error()const{return error_;}
    Actor* resolveActor(std::string_view savedId)const;
    CharacterMotorComponent* motor()const;
protected:
    void fixedTick(double)override;
    void onDisable()noexcept override{stop();}void onDetach()noexcept override{stop();}
    void onOwnerChanged(Actor* previous,Actor*)noexcept override{if(previous)stop();}
private:
    AiControllerSettings settings_;std::shared_ptr<const AiBindingRegistry> registry_;
    std::unique_ptr<StateMachine> machine_;std::vector<AiTransitionRecord> transitions_;
    std::string error_;double simulationSeconds_=0;
    std::map<std::string,AiValue> blackboard_;
    struct TaskRun {std::size_t index=0;double seconds=0;AiEventScope scope;std::unique_ptr<AiTask> task;AiTaskRecord record;bool terminal=false;std::string signal;};
    std::map<std::uint32_t,TaskRun> taskRuns_;std::vector<AiTaskRecord> taskHistory_;
    std::shared_ptr<const NavigationSource> navigationSource_;bool stopRequested_=false;
    void updateTasks(const AiState&,double);void cancelTasks(std::uint32_t)noexcept;
    void recordTask(const AiTaskRecord&)noexcept;
    void event(const AiEvent&,std::optional<AiEventScope>)override;
    AiBinding resolved(const AiBinding&,const std::map<std::string,AiValue>&)const;
    void transitioned(const AiTransition&,std::uint32_t,std::uint32_t)override;
    void enter(const AiState&)override;void update(const AiState&,double)override;
    void exit(const AiState&)noexcept override;bool condition(std::string_view)override;
};
struct AiControllerArchive {
    static std::string save(const AiControllerSettings&);
    static AiControllerSettings load(std::string_view,const AiBindingRegistry&);
};
ComponentCodecs aiControllerCodecs(std::shared_ptr<const AiBindingRegistry> registry=AiBindingRegistry::builtins());
} // namespace engine
