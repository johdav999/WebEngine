#pragma once
#include "engine/skeleton_component.hpp"
#include "engine/animation_controller.hpp"

namespace engine {
enum class AnimationState { Stopped,Playing,Paused };
struct AnimationEvent {
    std::size_t clip=0,marker=0;
    std::string name,payload;
    // Unwrapped playback seconds; weight is the source contribution at crossing.
    double time=0;float weight=1;
};
struct AnimationOutput {
    RootMotionDelta rootMotion;
    std::vector<AnimationEvent> events;
};
class AnimationComponent : public Component {
public:
    AnimationComponent(std::string name,SkeletonComponent& target);
    void play(std::size_t clip,bool loop=true,double speed=1);
    void stop(); // Reset to the original bind pose.
    void pause();void resume();
    void seek(double seconds);
    void blend(std::size_t a,std::size_t b,float weight,bool loop=true,double speed=1);
    void setBlendWeight(float weight);
    void crossFade(std::size_t target,double duration,bool loop=true);
    void setController(std::shared_ptr<const AnimationControllerDefinition> definition);
    void clearController(); // Keep the current pose/playback; discard control state.
    const auto& controller() const { return controller_; }
    std::size_t controllerState() const { return controllerState_; }
    void setParameter(std::string_view name,AnimationParameterValue value);
    AnimationParameterValue parameter(std::string_view name) const;
    void trigger(std::string_view name);
    void resetTrigger(std::string_view name);
    // Owned copies; no callbacks or borrowed scene pointers. Controls/seek clear events.
    std::span<const AnimationEvent> events() const { return events_; }
    std::vector<AnimationEvent> consumeEvents();
    AnimationOutput consumeOutput(); // Drain events and root motion together.
    // Opt-in: anchor the selected joint to its bind translation/rotation in mesh space.
    // Select the locomotion joint explicitly for rigs with static FBX ancestors.
    void enableRootMotion(std::size_t joint=0);
    void disableRootMotion();
    bool rootMotionEnabled() const { return static_cast<bool>(rootAsset_); }
    std::size_t rootMotionJoint() const { return rootJoint_; }
    // Accumulated since consumption, in chronological transform order.
    // Playback controls/seek clear pending motion; only update() extracts movement.
    RootMotionDelta rootMotionDelta() const { return rootMotion_; }
    RootMotionDelta consumeRootMotion();
    void update(double deltaSeconds);
    AnimationState state() const { return state_; }
    double time() const { return time_; }
    float blendWeight() const { return weight_; }
    bool blending() const { return b_>=0; }
    const SkeletonComponent& target() const { return target_; }
    std::string_view typeName() const noexcept override { return "AnimationComponent"; }
protected:
    void tick(double deltaSeconds) override { update(deltaSeconds); }
private:
    friend class ArchiveCodec;
    SkeletonComponent& target_;
    std::shared_ptr<const Skeleton> asset_;
    std::vector<JointPose> pose_,second_,fadeFrom_;
    std::vector<JointPose> stripped_;
    std::shared_ptr<const Skeleton> rootAsset_;
    std::size_t rootJoint_=0;
    RootMotionDelta rootMotion_,rootBindInverse_;
    AnimationState state_=AnimationState::Stopped;
    std::size_t a_=0;std::int32_t b_=-1;
    double time_=0,speed_=1,fadeTime_=0,fadeDuration_=0;
    float weight_=0;bool loop_=true;
    std::shared_ptr<const AnimationControllerDefinition> controller_;
    std::vector<AnimationParameterValue> parameters_;
    std::size_t controllerState_=0;
    bool completed_=false;
    std::vector<AnimationEvent> events_;
    std::size_t parameterIndex(std::string_view name) const;
    float controllerWeight(std::size_t state) const;
    void enterControllerState(std::size_t state,double fadeSeconds);
    const AnimationTransition* controllerTransition() const;
    void updateController();
    std::vector<AnimationEvent> collectEvents(double from,double to) const;
    void checkClip(std::size_t index) const;
    void configure(std::size_t clip,bool loop,double speed);
    void evaluate();
    RootMotionDelta rootAt(const AnimationClip& clip,double time) const;
    RootMotionDelta extractRootMotion(double from,double to,double fadeFrom,double fadeStep) const;
};
} // namespace engine
