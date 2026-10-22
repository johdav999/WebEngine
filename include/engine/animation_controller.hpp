#pragma once
#include "engine/animation.hpp"
#include "engine/ik.hpp"
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
struct AnimationBlendPoint {
    std::size_t clip=0;
    double x=0,y=0;
};
// A bounded 1D or Cartesian 2D blend tree. Coordinates are application units.
struct AnimationBlendTree {
    std::size_t parameterX=0;
    std::optional<std::size_t> parameterY;
    std::vector<AnimationBlendPoint> points;
    // Use the longest clip's cycle for all sources, keeping foot phases aligned.
    bool synchronizePhase=true;
};
struct AnimationBlendWeights {
    std::array<float,64> values{};
    std::size_t count=0;
};
AnimationBlendWeights animationBlendWeights(const AnimationBlendTree&,double x,double y=0);
double animationTreeDuration(const AnimationBlendTree&,const Skeleton&);
struct AnimationControllerState {
    std::string name;
    std::size_t clip=0;
    bool loop=true;
    double speed=1;
    std::optional<AnimationBlend> blend;
    std::optional<AnimationBlendTree> tree{};
};
enum class AnimationLayerMode { Override,Additive };
struct AnimationLayer {
    std::string name;
    AnimationControllerState motion;
    AnimationLayerMode mode=AnimationLayerMode::Override;
    // Empty mask selects every joint. Otherwise one [0,1] weight per joint.
    std::vector<float> mask;
    float weight=1;
    std::optional<std::size_t> weightParameter;
    // Additive reference is bind pose, or a fixed sample of this clip.
    std::optional<std::size_t> referenceClip;
    double referenceTime=0;
};
// Include a joint and all descendants; other joints receive zero weight.
std::vector<float> animationSubtreeMask(const Skeleton&,std::size_t joint,float weight=1);
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
    // Ordered local-space pose overlays. Only the base emits markers/root motion.
    std::vector<AnimationLayer> layers;
    // Ordered model-space IK, applied after blending, root stripping and layers.
    std::vector<IkConstraint> ik;
    void validate(const Skeleton& skeleton) const;
};
} // namespace engine
