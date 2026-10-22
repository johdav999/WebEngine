#include "engine/animation_controller.hpp"
#include <stdexcept>
#include <unordered_set>

namespace engine {
AnimationBlendWeights animationBlendWeights(const AnimationBlendTree& tree,double x,double y) {
    if(tree.points.empty()||tree.points.size()>64||!std::isfinite(x)||!std::isfinite(y))
        throw std::invalid_argument("Invalid animation blend input");
    AnimationBlendWeights result;result.count=tree.points.size();
    if(!tree.parameterY) {
        std::size_t lo=0,hi=0;
        for(std::size_t i=0;i<result.count;++i) {
            if(tree.points[i].x<tree.points[lo].x)lo=i;
            if(tree.points[i].x>tree.points[hi].x)hi=i;
        }
        x=std::clamp(x,tree.points[lo].x,tree.points[hi].x);
        for(std::size_t i=0;i<result.count;++i) {
            const auto at=tree.points[i].x;
            if(at<=x&&at>tree.points[lo].x)lo=i;
            if(at>=x&&at<tree.points[hi].x)hi=i;
        }
        if(lo==hi)result.values[lo]=1;
        else {const auto w=float((x-tree.points[lo].x)/(tree.points[hi].x-tree.points[lo].x));result.values[lo]=1-w;result.values[hi]=w;}
        return result;
    }
    double minX=tree.points[0].x,maxX=minX,minY=tree.points[0].y,maxY=minY;
    for(const auto& p:tree.points){minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);minY=std::min(minY,p.y);maxY=std::max(maxY,p.y);}
    x=std::clamp(x,minX,maxX);y=std::clamp(y,minY,maxY);
    // Cartesian gradient bands: exact at samples, continuous between them.
    // Normalize the intersected projection bands; no triangulation dependency.
    double sum=0;
    for(std::size_t i=0;i<result.count;++i) {
        double w=1;const auto& a=tree.points[i];
        for(std::size_t j=0;j<result.count;++j)if(i!=j) {
            const auto dx=tree.points[j].x-a.x,dy=tree.points[j].y-a.y;
            const auto distance=dx*dx+dy*dy;
            if(distance<=0)throw std::invalid_argument("Duplicate animation blend points");
            w=std::min(w,std::clamp(1-((x-a.x)*dx+(y-a.y)*dy)/distance,0.0,1.0));
        }
        result.values[i]=float(w);sum+=result.values[i];
    }
    if(sum<=0)throw std::invalid_argument("Degenerate animation blend points");
    for(std::size_t i=0;i<result.count;++i)result.values[i]=float(result.values[i]/sum);
    return result;
}
double animationTreeDuration(const AnimationBlendTree& tree,const Skeleton& skeleton) {
    double duration=0;for(const auto& p:tree.points)duration=std::max(duration,skeleton.clips.at(p.clip).duration);return duration;
}
std::vector<float> animationSubtreeMask(const Skeleton& skeleton,std::size_t joint,float weight) {
    if(joint>=skeleton.joints.size()||!std::isfinite(weight)||weight<0||weight>1)throw std::invalid_argument("Invalid animation mask root/weight");
    skeleton.validate();std::vector<float> result(skeleton.joints.size());
    for(std::size_t i=0;i<result.size();++i)for(auto j=std::int32_t(i);j>=0;j=skeleton.joints[j].parent)
        if(std::size_t(j)==joint){result[i]=weight;break;}
    return result;
}
void AnimationControllerDefinition::validate(const Skeleton& skeleton) const {
    validateIk(skeleton,ik);
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
    const auto number=[&](std::size_t index){require(index<parameters.size()&&parameters[index].type==AnimationParameterType::Number);};
    const auto motion=[&](const AnimationControllerState& state) {
        require(state.clip<skeleton.clips.size() && std::isfinite(state.speed) && state.speed>0);
        if(state.blend) {
            const auto& blend=*state.blend;
            require(blend.clip<skeleton.clips.size() && blend.parameter<parameters.size() && parameters[blend.parameter].type==AnimationParameterType::Number);
            require(std::isfinite(blend.minimum) && std::isfinite(blend.maximum) && blend.maximum>blend.minimum && std::isfinite(blend.maximum-blend.minimum));
        }
        if(state.tree) {
            require(!state.blend);const auto& tree=*state.tree;number(tree.parameterX);if(tree.parameterY)number(*tree.parameterY);
            require(!tree.parameterY||*tree.parameterY!=tree.parameterX);
            require(!tree.points.empty()&&tree.points.size()<=64);
            for(std::size_t i=0;i<tree.points.size();++i) {
                const auto& p=tree.points[i];
                require(p.clip<skeleton.clips.size()&&std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1000000&&std::abs(p.y)<=1000000);
                for(std::size_t j=0;j<i;++j) {
                    const auto dx=p.x-tree.points[j].x,dy=tree.parameterY?p.y-tree.points[j].y:0;
                    require(dx*dx+dy*dy>=1e-12);
                }
            }
        }
    };
    for(const auto& state:states) {
        require(!state.name.empty() && state.name.size()<=1024 && names.insert(state.name).second);
        motion(state);
    }
    require(layers.size()<=16);names.clear();
    for(const auto& layer:layers) {
        require(!layer.name.empty()&&layer.name.size()<=1024&&names.insert(layer.name).second);
        motion(layer.motion);require(layer.mode>=AnimationLayerMode::Override&&layer.mode<=AnimationLayerMode::Additive);
        require(layer.mask.empty()||layer.mask.size()==skeleton.joints.size());
        for(auto w:layer.mask)require(std::isfinite(w)&&w>=0&&w<=1);
        require(std::isfinite(layer.weight)&&layer.weight>=0&&layer.weight<=1);
        if(layer.weightParameter)number(*layer.weightParameter);
        require(std::isfinite(layer.referenceTime)&&layer.referenceTime>=0);
        if(layer.referenceClip)require(*layer.referenceClip<skeleton.clips.size()&&layer.referenceTime<=skeleton.clips[*layer.referenceClip].duration);
        // Additive scale is a ratio, so every reference component must be nonzero.
        if(layer.mode==AnimationLayerMode::Additive)for(std::size_t j=0;j<skeleton.joints.size();++j) {
            const auto pose=layer.referenceClip?skeleton.clips[*layer.referenceClip].sampleJoint(layer.referenceTime,j,skeleton.joints.size()):skeleton.joints[j].bindPose;
            require(std::abs(pose.scale.x)>1e-6f&&std::abs(pose.scale.y)>1e-6f&&std::abs(pose.scale.z)>1e-6f);
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
