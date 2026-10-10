#include "engine/animation_component.hpp"

namespace engine {
namespace {
Quaternion normalizeRotation(Quaternion q) {
    const auto length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!std::isfinite(length) || length<1e-8f) throw std::invalid_argument("Invalid root motion rotation");
    return {q.x/length,q.y/length,q.z/length,q.w/length};
}
Quaternion multiply(Quaternion a,Quaternion b) {
    const Quaternion q{a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
    return normalizeRotation(q);
}
Vec3 rotate(Quaternion q,Vec3 v) { return transformDirection(RootMotionDelta{{},q}.matrix(),v); }
RootMotionDelta compose(const RootMotionDelta& a,const RootMotionDelta& b) {
    RootMotionDelta result{a.translation+rotate(a.rotation,b.translation),multiply(a.rotation,b.rotation)};
    result.matrix(); // Reject overflow before publishing movement.
    return result;
}
RootMotionDelta inverseMotion(const RootMotionDelta& value) {
    const auto q=normalizeRotation(value.rotation);
    const Quaternion inverse{-q.x,-q.y,-q.z,q.w};
    return {rotate(inverse,value.translation*-1),inverse};
}
RootMotionDelta blendMotion(const RootMotionDelta& a,const RootMotionDelta& b,float weight) {
    const auto pose=blendPose({a.translation,{1,1,1},a.rotation},{b.translation,{1,1,1},b.rotation},weight);
    return {pose.translation,pose.rotation};
}
RootMotionDelta power(RootMotionDelta value,std::uint64_t count) {
    RootMotionDelta result;
    while(count) {
        if(count&1) result=compose(result,value);
        count>>=1;if(count) value=compose(value,value);
    }
    return result;
}
// Sample only the locomotion joint and its ancestors. Parent scale affects the
// joint's position, but extracted orientation is the composed local quaternions.
template<class Sample> RootMotionDelta jointWorld(const Skeleton& skeleton,std::size_t joint,Sample sample) {
    std::array<std::size_t,256> ancestors{};std::size_t count=0;
    for(auto index=static_cast<std::int32_t>(joint);index>=0;index=skeleton.joints[index].parent) ancestors[count++]=index;
    auto world=Mat4::identity();Quaternion rotation;
    while(count) { const auto local=sample(ancestors[--count]);world=world*local.matrix();rotation=multiply(rotation,local.rotation); }
    return {transformPoint(world,{}),rotation};
}
}
AnimationComponent::AnimationComponent(std::string name,SkeletonComponent& target):Component(std::move(name)),target_(target) { setTickEnabled(true); }
void AnimationComponent::checkClip(std::size_t clip) const {
    if(!target_.skeleton() || clip>=target_.skeleton()->clips.size()) throw std::out_of_range("Animation clip is unavailable");
}
void AnimationComponent::configure(std::size_t clip,bool loop,double speed) {
    checkClip(clip);if(!std::isfinite(speed) || speed<=0) throw std::invalid_argument("Playback speed must be positive");
    asset_=target_.skeleton();pose_.resize(asset_->joints.size());second_.resize(pose_.size());
    if(rootAsset_ && rootAsset_!=asset_) { rootAsset_.reset();stripped_.clear(); }
    rootMotion_={};events_.clear();completed_=false;
    a_=clip;b_=-1;weight_=0;time_=fadeTime_=fadeDuration_=0;speed_=speed;loop_=loop;state_=AnimationState::Playing;
}
void AnimationComponent::play(std::size_t clip,bool loop,double speed) { configure(clip,loop,speed);clearController();evaluate(); }
void AnimationComponent::stop() { clearController();completed_=false;events_.clear();state_=AnimationState::Stopped;time_=fadeDuration_=fadeTime_=0;b_=-1;rootMotion_={};target_.resetPose();asset_.reset(); }
void AnimationComponent::pause() { completed_=false;if(state_==AnimationState::Playing) state_=AnimationState::Paused; }
void AnimationComponent::resume() { if(state_==AnimationState::Paused) { state_=AnimationState::Playing;completed_=false; } }
void AnimationComponent::seek(double seconds) {
    if(!std::isfinite(seconds) || seconds<0) throw std::invalid_argument("Invalid animation time");
    if(!asset_ || asset_!=target_.skeleton()) throw std::logic_error("Play an animation before seeking");
    time_=seconds;rootMotion_={};events_.clear();completed_=false;evaluate();
}
void AnimationComponent::blend(std::size_t a,std::size_t b,float weight,bool loop,double speed) {
    checkClip(b);if(!std::isfinite(weight) || weight<0 || weight>1) throw std::invalid_argument("Blend weight must be in [0, 1]");
    configure(a,loop,speed);clearController();b_=static_cast<std::int32_t>(b);weight_=weight;evaluate();
}
void AnimationComponent::setBlendWeight(float weight) {
    if(b_<0 || !std::isfinite(weight) || weight<0 || weight>1) throw std::invalid_argument("Invalid animation blend weight");
    clearController();weight_=weight;rootMotion_={};events_.clear();evaluate();
}
void AnimationComponent::crossFade(std::size_t clip,double duration,bool loop) {
    checkClip(clip);if(!std::isfinite(duration) || duration<0) throw std::invalid_argument("Invalid fade duration");
    if(duration==0 || state_==AnimationState::Stopped || asset_!=target_.skeleton()) { play(clip,loop);return; }
    fadeFrom_=pose_;configure(clip,loop,speed_);clearController();fadeDuration_=duration;evaluate();
}
void AnimationComponent::clearController() { controller_.reset();parameters_.clear();controllerState_=0; }
void AnimationComponent::setController(std::shared_ptr<const AnimationControllerDefinition> definition) {
    if(!definition) { clearController();return; }
    const auto skeleton=target_.skeleton();
    if(!skeleton) throw std::logic_error("Animation controller requires a skeleton");
    definition->validate(*skeleton);
    std::vector<AnimationParameterValue> values;
    for(const auto& parameter:definition->parameters) values.push_back(parameter.initial);
    controller_=std::move(definition);parameters_=std::move(values);events_.clear();rootMotion_={};
    enterControllerState(controller_->initialState,0);
}
std::size_t AnimationComponent::parameterIndex(std::string_view name) const {
    if(controller_) for(std::size_t i=0;i<controller_->parameters.size();++i) if(controller_->parameters[i].name==name) return i;
    throw std::out_of_range("Animation parameter is unavailable");
}
void AnimationComponent::setParameter(std::string_view name,AnimationParameterValue value) {
    const auto index=parameterIndex(name);const auto type=controller_->parameters[index].type;
    if(type==AnimationParameterType::Trigger || (type==AnimationParameterType::Number ?
        !std::holds_alternative<double>(value) || !std::isfinite(std::get<double>(value)) : !std::holds_alternative<bool>(value)))
        throw std::invalid_argument("Animation parameter type/value mismatch");
    parameters_[index]=value;
}
AnimationParameterValue AnimationComponent::parameter(std::string_view name) const { return parameters_[parameterIndex(name)]; }
void AnimationComponent::trigger(std::string_view name) {
    const auto index=parameterIndex(name);
    if(controller_->parameters[index].type!=AnimationParameterType::Trigger) throw std::invalid_argument("Parameter is not a trigger");
    parameters_[index]=true;
}
void AnimationComponent::resetTrigger(std::string_view name) {
    const auto index=parameterIndex(name);
    if(controller_->parameters[index].type!=AnimationParameterType::Trigger) throw std::invalid_argument("Parameter is not a trigger");
    parameters_[index]=false;
}
float AnimationComponent::controllerWeight(std::size_t state) const {
    const auto& blend=controller_->states[state].blend;
    if(!blend) return 0;
    const auto value=std::get<double>(parameters_[blend->parameter]);
    // Clamp before subtracting to avoid overflow from extreme finite parameters.
    if(value<=blend->minimum) return 0;
    if(value>=blend->maximum) return 1;
    return static_cast<float>((value-blend->minimum)/(blend->maximum-blend->minimum));
}
void AnimationComponent::enterControllerState(std::size_t index,double duration) {
    const auto& state=controller_->states[index];
    const bool fade=duration>0 && asset_==target_.skeleton() && state_!=AnimationState::Stopped;
    if(fade) fadeFrom_=pose_;
    // Controller transitions preserve already queued output from prior ticks.
    auto pendingEvents=std::move(events_);const auto pendingMotion=rootMotion_;
    configure(state.clip,state.loop,state.speed);
    events_=std::move(pendingEvents);rootMotion_=pendingMotion;
    if(state.blend) { b_=static_cast<std::int32_t>(state.blend->clip);weight_=controllerWeight(index); }
    controllerState_=index;fadeDuration_=fade ? duration : 0;evaluate();
}
const AnimationTransition* AnimationComponent::controllerTransition() const {
    if(!controller_ || (state_!=AnimationState::Playing && !completed_)) return nullptr;
    const auto matches=[&](const AnimationCondition& condition) {
        const auto& value=parameters_[condition.parameter];
        if(std::holds_alternative<bool>(value)) return condition.comparison==AnimationComparison::Equal ? value==condition.value : value!=condition.value;
        const auto a=std::get<double>(value),b=std::get<double>(condition.value);
        switch(condition.comparison) {
        case AnimationComparison::Equal:return a==b;
        case AnimationComparison::NotEqual:return a!=b;
        case AnimationComparison::Less:return a<b;
        case AnimationComparison::LessEqual:return a<=b;
        case AnimationComparison::Greater:return a>b;
        case AnimationComparison::GreaterEqual:return a>=b;
        }
        return false;
    };
    for(const auto& transition:controller_->transitions) {
        if(transition.from!=controllerState_) continue;
        const auto duration=b_>=0 ? std::max(asset_->clips[a_].duration,asset_->clips[b_].duration) : asset_->clips[a_].duration;
        if(transition.exitTime && (duration>0 ? time_/duration : 1)<*transition.exitTime) continue;
        if(!std::all_of(transition.conditions.begin(),transition.conditions.end(),matches)) continue;
        return &transition;
    }
    return nullptr;
}
void AnimationComponent::updateController() {
    if(!controller_ || (state_!=AnimationState::Playing && !completed_)) return;
    if(const auto* transition=controllerTransition()) {
        enterControllerState(transition->to,transition->fadeSeconds);
        for(const auto& condition:transition->conditions) if(controller_->parameters[condition.parameter].type==AnimationParameterType::Trigger) parameters_[condition.parameter]=false;
        return;
    }
    if(b_>=0) { weight_=controllerWeight(controllerState_);evaluate(); }
}
std::vector<AnimationEvent> AnimationComponent::consumeEvents() { auto result=std::move(events_);events_.clear();return result; }
AnimationOutput AnimationComponent::consumeOutput() { return {consumeRootMotion(),consumeEvents()}; }
std::vector<AnimationEvent> AnimationComponent::collectEvents(double from,double to) const {
    constexpr std::size_t limit=4096;
    std::vector<AnimationEvent> result;
    const auto collect=[&](std::size_t clipIndex,float contribution) {
        const auto& clip=asset_->clips[clipIndex];
        if(clip.duration<=0 || contribution<=0) return;
        for(std::size_t i=0;i<clip.markers.size();++i) {
            const auto& marker=clip.markers[i];
            double first=0,last=0;
            if(loop_) {
                first=std::max(0.0,std::floor((from-marker.time)/clip.duration));
                last=std::floor((to-marker.time)/clip.duration)+1;
                // Beyond this range doubles cannot enumerate individual cycles.
                if(!std::isfinite(first) || !std::isfinite(last) || last>=std::ldexp(1.0,53)) throw std::overflow_error("Animation marker clock overflow");
                // Division near a decimal boundary can round either way. Choose
                // conservative cycle bounds, then compare actual crossing times.
                while(first*clip.duration+marker.time<=from) ++first;
                while(last>=0 && last*clip.duration+marker.time>to) --last;
            } else if(!(marker.time>from && marker.time<=std::min(to,clip.duration))) continue;
            if(last<first) continue;
            if(last-first+1>double(limit-events_.size()-result.size())) throw std::overflow_error("Animation event queue overflow; consume output more frequently");
            for(double cycle=first;cycle<=last;++cycle) {
                const auto crossing=cycle*clip.duration+marker.time;
                float weight=contribution;
                if(fadeDuration_>0) weight*=static_cast<float>(std::clamp((fadeTime_+(crossing-from)/speed_)/fadeDuration_,0.0,1.0));
                if(weight>0) result.push_back({clipIndex,i,marker.name,marker.payload,crossing,weight});
            }
        }
    };
    collect(a_,b_<0 ? 1 : 1-weight_);
    if(b_>=0) collect(static_cast<std::size_t>(b_),weight_);
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b) { return a.time<b.time; });
    return result;
}
void AnimationComponent::enableRootMotion(std::size_t joint) {
    const auto skeleton=target_.skeleton();
    if(!skeleton || joint>=skeleton->joints.size()) throw std::out_of_range("Root motion joint is unavailable");
    const auto bind=jointWorld(*skeleton,joint,[&](std::size_t i) { return skeleton->joints[i].bindPose; });
    stripped_.resize(skeleton->joints.size());rootJoint_=joint;rootBindInverse_=inverseMotion(bind);rootAsset_=skeleton;rootMotion_={};
    if(asset_==skeleton) evaluate();
}
void AnimationComponent::disableRootMotion() {
    rootAsset_.reset();rootMotion_={};stripped_.clear();
    if(asset_ && asset_==target_.skeleton()) evaluate();
}
RootMotionDelta AnimationComponent::consumeRootMotion() { const auto result=rootMotion_;rootMotion_={};return result; }
RootMotionDelta AnimationComponent::rootAt(const AnimationClip& clip,double time) const {
    return compose(jointWorld(*asset_,rootJoint_,[&](std::size_t i) { return clip.sampleJoint(time,i,asset_->joints.size()); }),rootBindInverse_);
}
RootMotionDelta AnimationComponent::extractRootMotion(double from,double to,double fadeFrom,double fadeStep) const {
    const auto delta=[&](const AnimationClip& clip) {
        if(!loop_ || clip.duration==0) return compose(inverseMotion(rootAt(clip,std::min(from,clip.duration))),rootAt(clip,std::min(to,clip.duration)));
        const auto first=std::floor(from/clip.duration),last=std::floor(to/clip.duration);
        if(!std::isfinite(last) || last>=std::ldexp(1.0,64)) throw std::invalid_argument("Root motion loop count overflow");
        auto result=inverseMotion(rootAt(clip,std::fmod(from,clip.duration)));
        if(last>first) {
            const auto cycle=compose(rootAt(clip,clip.duration),inverseMotion(rootAt(clip,0)));
            result=compose(result,power(cycle,static_cast<std::uint64_t>(last)-static_cast<std::uint64_t>(first)));
        }
        // Cancel complete past cycles algebraically, before using float transforms.
        return compose(result,rootAt(clip,std::fmod(to,clip.duration)));
    };
    auto result=delta(asset_->clips[a_]);
    if(b_>=0) result=blendMotion(result,delta(asset_->clips[b_]),weight_);
    if(fadeDuration_>0) {
        // Crossfades hold the outgoing pose still, as in evaluate(). Integrate the
        // target weight over this update, including updates past the fade end.
        const auto during=std::min(fadeStep,std::max(0.0,fadeDuration_-fadeFrom));
        const auto fraction=during/fadeStep;
        const auto weight=fadeFrom>=fadeDuration_ ? 1.0 :
            fraction*(fadeFrom/fadeDuration_+(fadeFrom+during)/fadeDuration_)/2+(1-fraction);
        result=blendMotion({},result,static_cast<float>(std::clamp(weight,0.0,1.0)));
    }
    return result;
}
void AnimationComponent::evaluate() {
    const auto& clip=asset_->clips[a_];
    auto sampleTime=[&](double duration) { return loop_ && duration>0 ? std::fmod(time_,duration) : std::min(time_,duration); };
    clip.sample(sampleTime(clip.duration),pose_);
    if(b_>=0) {
        const auto& other=asset_->clips[b_];other.sample(sampleTime(other.duration),second_);
        for(std::size_t i=0;i<pose_.size();++i) pose_[i]=blendPose(pose_[i],second_[i],weight_);
    }
    if(fadeDuration_>0) {
        const auto weight=static_cast<float>(std::clamp(fadeTime_/fadeDuration_,0.0,1.0));
        for(std::size_t i=0;i<pose_.size();++i) pose_[i]=blendPose(fadeFrom_[i],pose_[i],weight);
    }
    if(rootAsset_==asset_) {
        const auto root=jointWorld(*asset_,rootJoint_,[&](std::size_t i) { return pose_[i]; });
        const auto correction=inverseMotion(compose(root,rootBindInverse_));
        std::copy(pose_.begin(),pose_.end(),stripped_.begin());
        // Apply one rigid correction to every hierarchy root, preserving relative
        // limb motion and scale even when the selected joint is below an FBX root.
        for(std::size_t i=0;i<stripped_.size();++i) if(asset_->joints[i].parent<0) {
            stripped_[i].translation=correction.translation+rotate(correction.rotation,pose_[i].translation);
            stripped_[i].rotation=multiply(correction.rotation,pose_[i].rotation);
        }
        target_.setPose(stripped_);
    } else target_.setPose(pose_);
}
void AnimationComponent::update(double delta) {
    if(!std::isfinite(delta) || delta<0) throw std::invalid_argument("Invalid animation delta");
    if(rootAsset_ && rootAsset_!=target_.skeleton()) { rootAsset_.reset();rootMotion_={};stripped_.clear(); }
    if(asset_ && asset_!=target_.skeleton()) { stop();return; }
    const auto* transition=controllerTransition();
    if(state_!=AnimationState::Playing && !transition) return;
    // Stage interval output using candidate playback fields before committing a
    // transition or consuming triggers. No target pose/state is published here.
    const auto oldA=a_;const auto oldB=b_;const auto oldWeight=weight_;const auto oldLoop=loop_;
    const auto oldTime=time_,oldSpeed=speed_,oldFadeTime=fadeTime_,oldFadeDuration=fadeDuration_;
    const auto restore=[&] { a_=oldA;b_=oldB;weight_=oldWeight;loop_=oldLoop;time_=oldTime;speed_=oldSpeed;fadeTime_=oldFadeTime;fadeDuration_=oldFadeDuration; };
    std::vector<AnimationEvent> markers;RootMotionDelta motion=rootMotion_;double nextTime=0,nextFade=0;
    try {
        if(transition) {
            const auto& target=controller_->states[transition->to];
            a_=target.clip;b_=target.blend ? static_cast<std::int32_t>(target.blend->clip) : -1;
            weight_=controllerWeight(transition->to);loop_=target.loop;speed_=target.speed;time_=fadeTime_=0;fadeDuration_=transition->fadeSeconds;
        } else if(controller_ && b_>=0) weight_=controllerWeight(controllerState_);
        nextTime=time_+delta*speed_;nextFade=fadeTime_+delta;
        if(!std::isfinite(nextTime) || !std::isfinite(nextFade)) throw std::invalid_argument("Animation clock overflow");
        if(delta>0) {
            if(rootAsset_) motion=compose(rootMotion_,extractRootMotion(time_,nextTime,fadeTime_,delta));
            markers=collectEvents(time_,nextTime);
        }
        events_.reserve(events_.size()+markers.size());
    } catch(...) { restore();throw; }
    restore();updateController();
    if(delta==0) return;
    time_=nextTime;fadeTime_=nextFade;evaluate();rootMotion_=motion;
    for(auto& marker:markers) events_.push_back(std::move(marker));
    if(fadeDuration_>0 && fadeTime_>=fadeDuration_) fadeDuration_=0;
    const auto duration=b_>=0 ? std::max(asset_->clips[a_].duration,asset_->clips[b_].duration) : asset_->clips[a_].duration;
    if(!loop_ && time_>=duration && fadeDuration_==0) { time_=duration;state_=AnimationState::Paused;completed_=true; }
}
} // namespace engine
