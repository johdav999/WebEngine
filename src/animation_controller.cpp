#include "engine/animation_controller.hpp"
#include <stdexcept>
#include <unordered_set>

namespace engine {
void AnimationControllerDefinition::validate(const Skeleton& skeleton) const {
    const auto require=[](bool value) { if(!value) throw std::invalid_argument("Invalid animation controller definition"); };
    require(!states.empty() && states.size()<=1024 && initialState<states.size() && parameters.size()<=1024 && transitions.size()<=4096);
    std::unordered_set<std::string> names;
    for(const auto& parameter:parameters) {
        require(!parameter.name.empty() && parameter.name.size()<=1024 && names.insert(parameter.name).second);
        require(parameter.type>=AnimationParameterType::Number && parameter.type<=AnimationParameterType::Trigger);
        if(parameter.type==AnimationParameterType::Number)
            require(std::holds_alternative<double>(parameter.initial) && std::isfinite(std::get<double>(parameter.initial)));
        else require(std::holds_alternative<bool>(parameter.initial) && (parameter.type!=AnimationParameterType::Trigger || !std::get<bool>(parameter.initial)));
    }
    names.clear();
    for(const auto& state:states) {
        require(!state.name.empty() && state.name.size()<=1024 && names.insert(state.name).second && state.clip<skeleton.clips.size() && std::isfinite(state.speed) && state.speed>0);
        if(state.blend) {
            const auto& blend=*state.blend;
            require(blend.clip<skeleton.clips.size() && blend.parameter<parameters.size() && parameters[blend.parameter].type==AnimationParameterType::Number);
            require(std::isfinite(blend.minimum) && std::isfinite(blend.maximum) && blend.maximum>blend.minimum && std::isfinite(blend.maximum-blend.minimum));
        }
    }
    for(const auto& transition:transitions) {
        require(transition.from<states.size() && transition.to<states.size() && std::isfinite(transition.fadeSeconds) && transition.fadeSeconds>=0 && transition.conditions.size()<=1024);
        if(transition.exitTime) require(std::isfinite(*transition.exitTime) && *transition.exitTime>=0);
        for(const auto& condition:transition.conditions) {
            require(condition.parameter<parameters.size() && condition.comparison>=AnimationComparison::Equal && condition.comparison<=AnimationComparison::GreaterEqual);
            const auto type=parameters[condition.parameter].type;
            if(type==AnimationParameterType::Number) require(std::holds_alternative<double>(condition.value) && std::isfinite(std::get<double>(condition.value)));
            else {
                require(std::holds_alternative<bool>(condition.value) && condition.comparison<=AnimationComparison::NotEqual);
                if(type==AnimationParameterType::Trigger) require(condition.comparison==AnimationComparison::Equal && std::get<bool>(condition.value));
            }
        }
    }
}
} // namespace engine
