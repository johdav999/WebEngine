#pragma once
#include "engine/state_machine.hpp"
#include "engine/character_motor_component.hpp"
#include "engine/scene_archive.hpp"
#include <map>
#include <variant>
namespace engine {
enum class AiParameterType { Boolean,Number,Vector,Actor };
using AiValue=std::variant<bool,double,Vec3,std::string>;
struct AiParameter {std::string key;AiParameterType type;AiValue initial;double minimum=-10000,maximum=10000;};
struct AiBinding {std::string key;std::map<std::string,AiValue> parameters;};
class AiControllerComponent;
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
    static std::shared_ptr<const AiBindingRegistry> create(std::vector<AiBehaviorBinding>,std::vector<AiConditionBinding>);
    static std::shared_ptr<const AiBindingRegistry> builtins();
    const auto& behaviors()const{return behaviors_;}const auto& conditions()const{return conditions_;}
    const AiBehaviorBinding& behavior(std::string_view)const;
    const AiConditionBinding& condition(std::string_view)const;
    AiBinding defaults(std::string_view key,bool condition=false)const;
    void validate(const AiBinding&,bool condition=false)const;
private:std::vector<AiBehaviorBinding> behaviors_;std::vector<AiConditionBinding> conditions_;
};
struct AiControllerSettings {
    std::shared_ptr<const StateMachineDefinition> graph;
    std::map<std::uint32_t,AiBinding> states;
    std::map<std::string,AiBinding> conditions;
    static AiControllerSettings defaults(std::shared_ptr<const StateMachineDefinition>,const AiBindingRegistry&);
    void validate(const AiBindingRegistry&)const;
};
struct AiTransitionRecord {std::uint32_t from=0,to=0;double simulationSeconds=0;};
class AiControllerComponent final:public Component,private StateMachineHost {
public:
    AiControllerComponent(std::string name,AiControllerSettings,std::shared_ptr<const AiBindingRegistry> registry=AiBindingRegistry::builtins());
    std::string_view typeName()const noexcept override{return "AiControllerComponent";}
    int fixedTickOrder()const noexcept override{return -100;}
    const AiControllerSettings& settings()const{return settings_;}
    const auto& registry()const{return registry_;}
    void configure(AiControllerSettings); // Validate fully before stopping/replacing.
    void validate()const override;
    void validateReferences(const Scene&)const;
    void stop()noexcept;
    void restart();
    StateMachineStatus status()const{return machine_->status();}
    std::uint32_t currentState()const{return machine_->current();}
    double stateSeconds()const{return machine_->stateSeconds();}
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
    std::string error_;double simulationSeconds_=0;std::uint32_t previous_=0;bool hasPrevious_=false;
    void enter(const AiState&)override;void update(const AiState&,double)override;
    void exit(const AiState&)noexcept override;bool condition(std::string_view)override;
};
struct AiControllerArchive {
    static std::string save(const AiControllerSettings&);
    static AiControllerSettings load(std::string_view,const AiBindingRegistry&);
};
ComponentCodecs aiControllerCodecs(std::shared_ptr<const AiBindingRegistry> registry=AiBindingRegistry::builtins());
} // namespace engine
