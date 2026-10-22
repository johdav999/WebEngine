#include "engine/actor.hpp"
#include "engine/performance_timeline.hpp"
#include "engine/animation_component.hpp"
#include <tuple>

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
    if(controller_&&!controller_->ik.empty()){
        fadeFrom_=ikPose_;
        // The visible IK pose is root-stripped. Restore the base's rigid root
        // transform so evaluate() can strip the frozen outgoing pose exactly once.
        if(rootAsset_==asset_){
            const auto root=compose(jointWorld(*asset_,rootJoint_,[&](std::size_t i){return pose_[i];}),rootBindInverse_);
            for(std::size_t i=0;i<fadeFrom_.size();++i)if(asset_->joints[i].parent<0){
                fadeFrom_[i].translation=root.translation+rotate(root.rotation,fadeFrom_[i].translation);
                fadeFrom_[i].rotation=multiply(root.rotation,fadeFrom_[i].rotation);
            }
        }
    }
    else if(controller_&&!controller_->layers.empty()){composed_=pose_;applyLayers();fadeFrom_=composed_;}
    else fadeFrom_=pose_;
    configure(clip,loop,speed_);clearController();fadeDuration_=duration;evaluate();
}
void AnimationComponent::clearController() { controller_.reset();parameters_.clear();layerTimes_.clear();ikTargets_.clear();ikPose_.clear();ikResult_={};controllerState_=0;lastTransition_.reset();transitionCount_=0; }
void AnimationComponent::swapPlayback(AnimationComponent& other) noexcept {
    const auto fields=[](AnimationComponent& p){return std::tie(p.asset_,p.pose_,p.second_,p.fadeFrom_,p.stripped_,p.rootAsset_,p.rootJoint_,p.rootMotion_,p.rootBindInverse_,p.state_,p.a_,p.b_,p.time_,p.speed_,p.fadeTime_,p.fadeDuration_,p.weight_,p.loop_,p.controller_,p.parameters_,p.controllerState_,p.lastTransition_,p.transitionCount_,p.completed_,p.events_,p.layerTimes_,p.composed_,p.ikPose_,p.ikTargets_,p.ikResult_);};
    auto a=fields(*this),b=fields(other);a.swap(b);
}
void AnimationComponent::setController(std::shared_ptr<const AnimationControllerDefinition> definition) {
    if(!definition) { clearController();return; }
    const auto skeleton=target_.skeleton();
    if(!skeleton) throw std::logic_error("Animation controller requires a skeleton");
    definition->validate(*skeleton);
    // Evaluate on detached storage so even incompatible additive transforms leave
    // the old controller, clocks, pending outputs and visible pose untouched.
    SkeletonComponent candidateTarget;candidateTarget.setSkeleton(skeleton);
    AnimationComponent candidate("Controller candidate",candidateTarget);
    candidate.controller_=std::move(definition);
    for(const auto& parameter:candidate.controller_->parameters)candidate.parameters_.push_back(parameter.initial);
    candidate.layerTimes_.resize(candidate.controller_->layers.size(),0);
    for(const auto& c:candidate.controller_->ik)candidate.ikTargets_.push_back(c.target);
    if(rootAsset_==skeleton)validateIkRootMotion(*skeleton,candidate.controller_->ik,rootJoint_);
    candidate.composed_.resize(skeleton->joints.size());
    if(rootAsset_==skeleton){candidate.rootAsset_=rootAsset_;candidate.rootJoint_=rootJoint_;candidate.rootBindInverse_=rootBindInverse_;candidate.stripped_.resize(skeleton->joints.size());}
    candidate.enterControllerState(candidate.controller_->initialState,0);
    target_.setPose(!candidate.controller_->ik.empty()?candidate.ikPose_:!candidate.controller_->layers.empty()?candidate.composed_:candidate.rootAsset_?candidate.stripped_:candidate.pose_);
    swapPlayback(candidate);
}
void AnimationComponent::setIkTargets(std::span<const IkTarget> targets) {
    if(!controller_||asset_!=target_.skeleton()||targets.size()!=ikTargets_.size())throw std::invalid_argument("IK target count or skeleton mismatch");
    for(const auto& t:targets)t.validate();
    std::array<IkTarget,32> previous;std::copy(ikTargets_.begin(),ikTargets_.end(),previous.begin());
    std::copy(targets.begin(),targets.end(),ikTargets_.begin());
    try{evaluate();}catch(...){std::copy_n(previous.begin(),ikTargets_.size(),ikTargets_.begin());throw;}
}
void AnimationComponent::setIkTarget(std::string_view name,const IkTarget& target) {
    if(!controller_)throw std::logic_error("IK requires a controller");
    for(std::size_t i=0;i<controller_->ik.size();++i)if(controller_->ik[i].name==name){
        std::array<IkTarget,32> next;std::copy(ikTargets_.begin(),ikTargets_.end(),next.begin());next[i]=target;
        setIkTargets(std::span(next).first(ikTargets_.size()));return;
    }
    throw std::invalid_argument("Unknown IK constraint");
}
AnimationComponent::Sources AnimationComponent::motionSources(const AnimationControllerState& state) const {
    Sources sources;
    if(state.tree) {
        const auto& tree=*state.tree;
        const auto weights=animationBlendWeights(tree,std::get<double>(parameters_[tree.parameterX]),tree.parameterY?std::get<double>(parameters_[*tree.parameterY]):0);
        const auto duration=animationTreeDuration(tree,*asset_);
        for(std::size_t i=0;i<weights.count;++i)if(weights.values[i]>0) {
            const auto clip=tree.points[i].clip;
            sources.values[sources.count++]={clip,weights.values[i],tree.synchronizePhase&&duration>0?asset_->clips[clip].duration/duration:1};
        }
    } else if(state.blend) {
        const auto& blend=*state.blend;const auto value=std::get<double>(parameters_[blend.parameter]);
        const float w=value<=blend.minimum?0:value>=blend.maximum?1:float((value-blend.minimum)/(blend.maximum-blend.minimum));
        sources.values[sources.count++]={state.clip,1-w,1};sources.values[sources.count++]={blend.clip,w,1};
    } else sources.values[sources.count++]={state.clip,1,1};
    return sources;
}
AnimationComponent::Sources AnimationComponent::baseSources() const {
    if(controller_&&controller_->states[controllerState_].tree)return motionSources(controller_->states[controllerState_]);
    Sources sources;sources.values[sources.count++]={a_,b_<0?1:1-weight_,1};
    if(b_>=0)sources.values[sources.count++]={std::size_t(b_),weight_,1};
    return sources;
}
double AnimationComponent::playbackDuration() const {
    if(controller_&&controller_->states[controllerState_].tree)return animationTreeDuration(*controller_->states[controllerState_].tree,*asset_);
    return b_>=0?std::max(asset_->clips[a_].duration,asset_->clips[b_].duration):asset_->clips[a_].duration;
}
JointPose AnimationComponent::sampleSources(const Sources& sources,double time,bool loop,std::size_t joint) const {
    JointPose result;result.scale={};result.rotation={0,0,0,0};Quaternion reference;bool first=true;
    for(std::size_t i=0;i<sources.count;++i) {
        const auto& source=sources.values[i];if(source.weight<=0)continue;
        const auto& clip=asset_->clips[source.clip];const auto clock=time*source.rate;
        const auto pose=clip.sampleJoint(loop&&clip.duration>0?std::fmod(clock,clip.duration):std::min(clock,clip.duration),joint,asset_->joints.size());
        if(first){reference=pose.rotation;first=false;}
        const auto sign=reference.x*pose.rotation.x+reference.y*pose.rotation.y+reference.z*pose.rotation.z+reference.w*pose.rotation.w<0?-1.0f:1.0f;
        result.translation=result.translation+pose.translation*source.weight;result.scale=result.scale+pose.scale*source.weight;
        result.rotation.x+=pose.rotation.x*source.weight*sign;result.rotation.y+=pose.rotation.y*source.weight*sign;
        result.rotation.z+=pose.rotation.z*source.weight*sign;result.rotation.w+=pose.rotation.w*source.weight*sign;
    }
    result.rotation=normalizeRotation(result.rotation);return result;
}
double AnimationComponent::layerTime(std::string_view name) const {
    if(controller_)for(std::size_t i=0;i<controller_->layers.size();++i)if(controller_->layers[i].name==name)return layerTimes_[i];
    throw std::out_of_range("Animation layer is unavailable");
}
void AnimationComponent::restartLayer(std::string_view name) {
    if(asset_!=target_.skeleton())throw std::logic_error("Animation layer target changed");
    if(controller_)for(std::size_t i=0;i<controller_->layers.size();++i)if(controller_->layers[i].name==name){layerTimes_[i]=0;evaluate();return;}
    throw std::out_of_range("Animation layer is unavailable");
}
void AnimationComponent::applyLayers() {
    ProfileScope profile(ProfileCategory::Animation,"Animation layers",name);
    if(!controller_||controller_->layers.empty())return;
    // Compose after base root stripping. Extraction protects the locomotion chain.
    for(std::size_t l=0;l<controller_->layers.size();++l) {
        const auto& layer=controller_->layers[l];float weight=layer.weight;
        if(layer.weightParameter)weight*=float(std::clamp(std::get<double>(parameters_[*layer.weightParameter]),0.0,1.0));
        if(weight<=0)continue;
        const auto sources=motionSources(layer.motion);
        for(std::size_t j=0;j<composed_.size();++j) {
            const float w=weight*(layer.mask.empty()?1:layer.mask[j]);if(w<=0)continue;
            bool protectedJoint=false;
            if(rootAsset_)for(auto k=std::int32_t(rootJoint_);k>=0;k=asset_->joints[k].parent)if(std::size_t(k)==j){protectedJoint=true;break;}
            if(protectedJoint)continue;
            const auto sample=sampleSources(sources,layerTimes_[l],layer.motion.loop,j);
            auto& pose=composed_[j];
            if(layer.mode==AnimationLayerMode::Override)pose=blendPose(pose,sample,w);
            else {
                const auto ref=layer.referenceClip?asset_->clips[*layer.referenceClip].sampleJoint(layer.referenceTime,j,asset_->joints.size()):asset_->joints[j].bindPose;
                const auto q=normalizeRotation(ref.rotation);
                const auto delta=multiply({-q.x,-q.y,-q.z,q.w},sample.rotation);
                const auto partial=blendPose(JointPose{},JointPose{{},{1,1,1},delta},w);
                pose.translation=pose.translation+(sample.translation-ref.translation)*w;
                pose.rotation=multiply(pose.rotation,partial.rotation);
                pose.scale={pose.scale.x*(1+(sample.scale.x/ref.scale.x-1)*w),pose.scale.y*(1+(sample.scale.y/ref.scale.y-1)*w),pose.scale.z*(1+(sample.scale.z/ref.scale.z-1)*w)};
            }
        }
    }
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
        const auto duration=playbackDuration();
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
        lastTransition_=std::size_t(transition-controller_->transitions.data());++transitionCount_;
        for(const auto& condition:transition->conditions) if(controller_->parameters[condition.parameter].type==AnimationParameterType::Trigger) parameters_[condition.parameter]=false;
        return;
    }
    if(b_>=0) weight_=controllerWeight(controllerState_);
    evaluate();
}
std::vector<AnimationEvent> AnimationComponent::consumeEvents() { auto result=std::move(events_);events_.clear();return result; }
AnimationOutput AnimationComponent::consumeOutput() { return {consumeRootMotion(),consumeEvents()}; }
std::vector<AnimationEvent> AnimationComponent::collectEvents(double from,double to) const {
    constexpr std::size_t limit=4096;
    std::vector<AnimationEvent> result;
    const auto collect=[&](std::size_t clipIndex,float contribution,double rate) {
        const auto& clip=asset_->clips[clipIndex];
        if(clip.duration<=0 || contribution<=0 || rate<=0) return;
        const auto sourceFrom=from*rate,sourceTo=to*rate;
        for(std::size_t i=0;i<clip.markers.size();++i) {
            const auto& marker=clip.markers[i];
            double first=0,last=0;
            if(loop_) {
                first=std::max(0.0,std::floor((sourceFrom-marker.time)/clip.duration));
                last=std::floor((sourceTo-marker.time)/clip.duration)+1;
                // Beyond this range doubles cannot enumerate individual cycles.
                if(!std::isfinite(first) || !std::isfinite(last) || last>=std::ldexp(1.0,53)) throw std::overflow_error("Animation marker clock overflow");
                // Division near a decimal boundary can round either way. Choose
                // conservative cycle bounds, then compare actual crossing times.
                while(first*clip.duration+marker.time<=sourceFrom) ++first;
                while(last>=0 && last*clip.duration+marker.time>sourceTo) --last;
            } else if(!(marker.time>sourceFrom && marker.time<=std::min(sourceTo,clip.duration))) continue;
            if(last<first) continue;
            if(last-first+1>double(limit-events_.size()-result.size())) throw std::overflow_error("Animation event queue overflow; consume output more frequently");
            for(double cycle=first;cycle<=last;++cycle) {
                const auto crossing=(cycle*clip.duration+marker.time)/rate;
                float weight=contribution;
                if(fadeDuration_>0) weight*=static_cast<float>(std::clamp((fadeTime_+(crossing-from)/speed_)/fadeDuration_,0.0,1.0));
                if(weight>0) result.push_back({clipIndex,i,marker.name,marker.payload,crossing,weight});
            }
        }
    };
    const auto sources=baseSources();
    for(std::size_t i=0;i<sources.count;++i){const auto& s=sources.values[i];collect(s.clip,s.weight,s.rate);}
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b) { return a.time<b.time; });
    return result;
}
void AnimationComponent::enableRootMotion(std::size_t joint) {
    const auto skeleton=target_.skeleton();
    if(!skeleton || joint>=skeleton->joints.size()) throw std::out_of_range("Root motion joint is unavailable");
    if(controller_)validateIkRootMotion(*skeleton,controller_->ik,joint);
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
    const auto start=from;
    const auto delta=[&](const AnimationClip& clip,double rate) {
        const auto from=start*rate;
        const auto end=to*rate;
        if(!loop_ || clip.duration==0) return compose(inverseMotion(rootAt(clip,std::min(from,clip.duration))),rootAt(clip,std::min(end,clip.duration)));
        const auto first=std::floor(from/clip.duration),last=std::floor(end/clip.duration);
        if(!std::isfinite(last) || last>=std::ldexp(1.0,64)) throw std::invalid_argument("Root motion loop count overflow");
        auto result=inverseMotion(rootAt(clip,std::fmod(from,clip.duration)));
        if(last>first) {
            const auto cycle=compose(rootAt(clip,clip.duration),inverseMotion(rootAt(clip,0)));
            result=compose(result,power(cycle,static_cast<std::uint64_t>(last)-static_cast<std::uint64_t>(first)));
        }
        // Cancel complete past cycles algebraically, before using float transforms.
        return compose(result,rootAt(clip,std::fmod(end,clip.duration)));
    };
    RootMotionDelta result;float total=0;const auto sources=baseSources();
    for(std::size_t i=0;i<sources.count;++i) {
        const auto& s=sources.values[i];if(s.weight<=0)continue;
        const auto movement=delta(asset_->clips[s.clip],s.rate);
        result=total==0?movement:blendMotion(result,movement,s.weight/(total+s.weight));total+=s.weight;
    }
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
    ProfileScope profile(ProfileCategory::Animation,"Sample and blend pose",name);
    const auto& clip=asset_->clips[a_];
    auto sampleTime=[&](double duration) { return loop_ && duration>0 ? std::fmod(time_,duration) : std::min(time_,duration); };
    if(controller_&&controller_->states[controllerState_].tree) {
        const auto sources=baseSources();for(std::size_t i=0;i<pose_.size();++i)pose_[i]=sampleSources(sources,time_,loop_,i);
    } else {
    clip.sample(sampleTime(clip.duration),pose_);
    if(b_>=0) {
        const auto& other=asset_->clips[b_];other.sample(sampleTime(other.duration),second_);
        for(std::size_t i=0;i<pose_.size();++i) pose_[i]=blendPose(pose_[i],second_[i],weight_);
    }
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
    }
    if(controller_&&(!controller_->layers.empty()||!controller_->ik.empty())) {
        composed_=rootAsset_==asset_?stripped_:pose_;applyLayers();
        if(!controller_->ik.empty()){ProfileScope ik(ProfileCategory::Animation,"Inverse kinematics",name);ikPose_.resize(composed_.size());const auto result=solveIk(*asset_,composed_,controller_->ik,ikTargets_,ikPose_);target_.setPose(ikPose_);ikResult_=result;}
        else target_.setPose(composed_);
    } else target_.setPose(rootAsset_==asset_?stripped_:pose_);
}
void AnimationComponent::update(double delta) {
    ProfileScope profile(ProfileCategory::Animation,"Animation update",owner()?owner()->name:name);
    if(!std::isfinite(delta) || delta<0) throw std::invalid_argument("Invalid animation delta");
    if(rootAsset_ && rootAsset_!=target_.skeleton()) { rootAsset_.reset();rootMotion_={};stripped_.clear(); }
    if(asset_ && asset_!=target_.skeleton()) { stop();return; }
    const auto* transition=controllerTransition();
    if(state_!=AnimationState::Playing && !transition && !(completed_&&controller_&&!controller_->layers.empty())) return;
    // Stage interval output using candidate playback fields before committing a
    // transition or consuming triggers. No target pose/state is published here.
    const auto oldA=a_;const auto oldB=b_;const auto oldWeight=weight_;const auto oldLoop=loop_;
    const auto oldTime=time_,oldSpeed=speed_,oldFadeTime=fadeTime_,oldFadeDuration=fadeDuration_;
    const auto oldControllerState=controllerState_;
    const auto restore=[&] { a_=oldA;b_=oldB;weight_=oldWeight;loop_=oldLoop;time_=oldTime;speed_=oldSpeed;fadeTime_=oldFadeTime;fadeDuration_=oldFadeDuration;controllerState_=oldControllerState; };
    std::vector<AnimationEvent> markers;RootMotionDelta motion=rootMotion_;double nextTime=0,nextFade=0;
    try {
        if(transition) {
            const auto& target=controller_->states[transition->to];
            a_=target.clip;b_=target.blend ? static_cast<std::int32_t>(target.blend->clip) : -1;
            weight_=controllerWeight(transition->to);loop_=target.loop;speed_=target.speed;time_=fadeTime_=0;fadeDuration_=transition->fadeSeconds;
            controllerState_=transition->to;
        } else if(controller_ && b_>=0) weight_=controllerWeight(controllerState_);
        nextTime=time_+delta*speed_;nextFade=fadeTime_+delta;
        if(!std::isfinite(nextTime) || !std::isfinite(nextFade)) throw std::invalid_argument("Animation clock overflow");
        if(controller_)for(std::size_t i=0;i<layerTimes_.size();++i)
            if(!std::isfinite(layerTimes_[i]+delta*controller_->layers[i].motion.speed))throw std::invalid_argument("Animation layer clock overflow");
        if(delta>0) {
            if(rootAsset_) motion=compose(rootMotion_,extractRootMotion(time_,nextTime,fadeTime_,delta));
            markers=collectEvents(time_,nextTime);
        }
        events_.reserve(events_.size()+markers.size());
    } catch(...) { restore();throw; }
    restore();updateController();
    if(delta==0) { evaluate();return; }
    if(controller_)for(std::size_t i=0;i<layerTimes_.size();++i) {
        const auto& m=controller_->layers[i].motion;
        const auto duration=m.tree?animationTreeDuration(*m.tree,*asset_):m.blend?std::max(asset_->clips[m.clip].duration,asset_->clips[m.blend->clip].duration):asset_->clips[m.clip].duration;
        layerTimes_[i]+=delta*m.speed;if(!m.loop)layerTimes_[i]=std::min(layerTimes_[i],duration);
    }
    time_=nextTime;fadeTime_=nextFade;evaluate();rootMotion_=motion;
    for(auto& marker:markers) events_.push_back(std::move(marker));
    if(fadeDuration_>0 && fadeTime_>=fadeDuration_) fadeDuration_=0;
    const auto duration=playbackDuration();
    if(!loop_ && time_>=duration && fadeDuration_==0) { time_=duration;state_=AnimationState::Paused;completed_=true; }
}
} // namespace engine
