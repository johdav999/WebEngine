#pragma once
#include "engine/animation.hpp"
#include <optional>
#include <variant>

namespace engine {
enum class AnimationParameterType { Number,Boolean,Trigger };
using AnimationParameterValue=std::variant<double,bool>;
struct AnimationParameter {
    std::string name;
    AnimationParameterType type=AnimationParameterType::Number;
    AnimationParameterValue initial=0.0;
};
struct AnimationBlend {
    std::size_t clip=0,parameter=0;
    double minimum=0,maximum=1;
};
struct AnimationControllerState {
    std::string name;
    std::size_t clip=0;
    bool loop=true;
    double speed=1;
    std::optional<AnimationBlend> blend;
};
enum class AnimationComparison { Equal,NotEqual,Less,LessEqual,Greater,GreaterEqual };
struct AnimationCondition {
    std::size_t parameter=0;
    AnimationComparison comparison=AnimationComparison::Equal;
    AnimationParameterValue value=true;
};
struct AnimationTransition {
    std::size_t from=0,to=0;
    double fadeSeconds=0;
    // Minimum unwrapped normalized time in the source (max duration for blends).
    std::optional<double> exitTime;
    std::vector<AnimationCondition> conditions;
};
// Shared immutable configuration. Gameplay owns the meaning of states/parameters.
struct AnimationControllerDefinition {
    std::vector<AnimationParameter> parameters;
    std::vector<AnimationControllerState> states;
    // First eligible transition wins; at most one transition per variable tick.
    std::vector<AnimationTransition> transitions;
    std::size_t initialState=0;
    void validate(const Skeleton& skeleton) const;
};
} // namespace engine
