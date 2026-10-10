#include "engine/animation.hpp"
#include <stdexcept>

namespace engine {
namespace {
Quaternion normalize(Quaternion q) {
    const auto length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!std::isfinite(length) || length<1e-8f) throw std::invalid_argument("Invalid joint rotation");
    return {q.x/length,q.y/length,q.z/length,q.w/length};
}
Quaternion slerp(Quaternion a,Quaternion b,float t) {
    a=normalize(a);b=normalize(b);float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0) { b={-b.x,-b.y,-b.z,-b.w};dot=-dot; }
    float wa=1-t,wb=t;
    if(dot<0.9995f) { const auto angle=std::acos(std::clamp(dot,-1.0f,1.0f)),sine=std::sin(angle);wa=std::sin((1-t)*angle)/sine;wb=std::sin(t*angle)/sine; }
    return normalize({a.x*wa+b.x*wb,a.y*wa+b.y*wb,a.z*wa+b.z*wb,a.w*wa+b.w*wb});
}
}
Mat4 JointPose::matrix() const {
    const auto q=normalize(rotation);const auto x=q.x,y=q.y,z=q.z,w=q.w;
    auto m=Mat4::identity();
    m.m[0]=(1-2*(y*y+z*z))*scale.x;m.m[1]=2*(x*y+z*w)*scale.x;m.m[2]=2*(x*z-y*w)*scale.x;
    m.m[4]=2*(x*y-z*w)*scale.y;m.m[5]=(1-2*(x*x+z*z))*scale.y;m.m[6]=2*(y*z+x*w)*scale.y;
    m.m[8]=2*(x*z+y*w)*scale.z;m.m[9]=2*(y*z-x*w)*scale.z;m.m[10]=(1-2*(x*x+y*y))*scale.z;
    m.m[12]=translation.x;m.m[13]=translation.y;m.m[14]=translation.z;
    for(const auto f:m.m) if(!std::isfinite(f)) throw std::invalid_argument("Non-finite joint transform");
    return m;
}
JointPose blendPose(const JointPose& a,const JointPose& b,float weight) {
    if(!std::isfinite(weight) || weight<0 || weight>1) throw std::invalid_argument("Blend weight must be in [0, 1]");
    return {a.translation*(1-weight)+b.translation*weight,a.scale*(1-weight)+b.scale*weight,slerp(a.rotation,b.rotation,weight)};
}
Mat4 RootMotionDelta::matrix() const { return JointPose{translation,{1,1,1},rotation}.matrix(); }
JointPose AnimationClip::sampleJoint(double time,std::size_t joint,std::size_t jointCount) const {
    if(!std::isfinite(time) || !frameCount || !jointCount || joint>=jointCount || frames.size()!=std::size_t(frameCount)*jointCount)
        throw std::invalid_argument("Invalid animation joint sample");
    const auto frame=duration>0 ? std::clamp(time/duration,0.0,1.0)*(frameCount-1) : 0;
    const auto a=static_cast<std::size_t>(frame),b=std::min(a+1,std::size_t(frameCount-1));
    return blendPose(frames[a*jointCount+joint],frames[b*jointCount+joint],float(frame-a));
}
void AnimationClip::sample(double time,std::span<JointPose> output) const {
    if(!std::isfinite(time) || !frameCount || output.empty() || frames.size()!=std::size_t(frameCount)*output.size())
        throw std::invalid_argument("Invalid animation sample");
    const auto frame=duration>0 ? std::clamp(time/duration,0.0,1.0)*(frameCount-1) : 0;
    const auto a=static_cast<std::size_t>(frame),b=std::min(a+1,std::size_t(frameCount-1));
    for(std::size_t j=0;j<output.size();++j) output[j]=blendPose(frames[a*output.size()+j],frames[b*output.size()+j],float(frame-a));
}
void Skeleton::validate() const {
    if(joints.empty() || joints.size()>256 || bindings.empty() || bindings.size()>1024) throw std::invalid_argument("Invalid skeleton size");
    for(std::size_t i=0;i<joints.size();++i) {
        if(joints[i].parent<-1 || joints[i].parent>=static_cast<std::int32_t>(i)) throw std::invalid_argument("Skeleton parents must precede children");
        inverseAffine(joints[i].bindPose.matrix());
    }
    for(std::size_t i=1;i<bindings.size();++i) {
        if(bindings[i].joint>=joints.size()) throw std::invalid_argument("Invalid skin binding joint");
        for(auto f:bindings[i].inverseBind.m) if(!std::isfinite(f)) throw std::invalid_argument("Invalid inverse bind transform");
        inverseAffine(bindings[i].inverseBind);
    }
    for(const auto& clip:clips) {
        if(!std::isfinite(clip.duration) || clip.duration<0 || !clip.frameCount || clip.frames.size()!=clip.frameCount*joints.size())
            throw std::invalid_argument("Invalid animation clip");
        for(const auto& pose:clip.frames) inverseAffine(pose.matrix());
        if(clip.markers.size()>4096) throw std::invalid_argument("Too many animation markers");
        double previous=-1;
        for(const auto& marker:clip.markers) {
            if(!std::isfinite(marker.time) || marker.time<0 || marker.time>clip.duration || marker.time<previous || marker.name.empty() || marker.name.size()>1024 || marker.payload.size()>4096)
                throw std::invalid_argument("Invalid animation marker");
            previous=marker.time;
        }
    }
}
} // namespace engine
