#include "engine/ik.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine {
namespace {
constexpr float epsilon=1e-6f;
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
float magnitude(Vec3 v){return std::sqrt(dot(v,v));}
Vec3 unit(Vec3 v){const auto n=magnitude(v);require(std::isfinite(n)&&n>epsilon,"IK direction is degenerate");return v*(1/n);}
Quaternion normalizedQ(Quaternion q){const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);require(std::isfinite(n)&&n>epsilon,"Invalid IK rotation");return {q.x/n,q.y/n,q.z/n,q.w/n};}
Quaternion product(Quaternion a,Quaternion b){return normalizedQ({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});}
Quaternion conjugate(Quaternion q){q=normalizedQ(q);return {-q.x,-q.y,-q.z,q.w};}
Vec3 rotateQ(Quaternion q,Vec3 v){q=normalizedQ(q);const Vec3 u{q.x,q.y,q.z};return v+cross(u,v)*(2*q.w)+cross(u,cross(u,v))*2;}
Vec3 perpendicular(Vec3 axis){
    const Vec3 ref=std::abs(axis.x)<=std::abs(axis.y)&&std::abs(axis.x)<=std::abs(axis.z)?Vec3{1,0,0}:std::abs(axis.y)<=std::abs(axis.z)?Vec3{0,1,0}:Vec3{0,0,1};
    return unit(cross(axis,ref));
}
Quaternion swing(Vec3 a,Vec3 b,float limit=pi){
    a=unit(a);b=unit(b);const auto d=std::clamp(dot(a,b),-1.0f,1.0f);
    if(d>1-1e-7f)return {};
    auto axis=cross(a,b);if(magnitude(axis)<epsilon)axis=perpendicular(a);else axis=unit(axis);
    const auto half=std::min(std::acos(d),limit)*.5f;const auto s=std::sin(half);
    return {axis.x*s,axis.y*s,axis.z*s,std::cos(half)};
}
struct WorldPose { Mat4 matrix;Quaternion rotation;Vec3 position; };
void worlds(const Skeleton& s,std::span<const JointPose> poses,std::span<WorldPose> output,bool uniform){
    for(std::size_t i=0;i<poses.size();++i){
        const auto& p=poses[i];if(uniform)require(p.scale.x>epsilon&&std::abs(p.scale.x-p.scale.y)<=epsilon*std::max(1.f,p.scale.x)&&std::abs(p.scale.x-p.scale.z)<=epsilon*std::max(1.f,p.scale.x),"IK requires positive uniform joint scales");
        const auto parent=s.joints[i].parent;require(parent>=-1&&parent<std::int32_t(i),"Invalid IK skeleton hierarchy");
        const auto local=p.matrix();auto& w=output[i];w.matrix=parent<0?local:output[parent].matrix*local;
        w.rotation=parent<0?normalizedQ(p.rotation):product(output[parent].rotation,p.rotation);
        for(auto value:w.matrix.m)require(std::isfinite(value),"IK joint transform overflow");
        w.position=transformPoint(w.matrix,{});require(finite(w.position),"IK joint transform overflow");
    }
}
Quaternion parentRotation(const Skeleton& s,std::span<WorldPose> w,std::size_t joint){const auto parent=s.joints[joint].parent;return parent<0?Quaternion{}:w[parent].rotation;}
// Restrict placement to a positive similarity transform; avoids silently solving
// with distorted segment lengths or mirrored bend planes.
Quaternion placementRotation(const Mat4& m){
    for(auto v:m.m)require(std::isfinite(v),"Invalid IK model placement");
    require(m.m[3]==0&&m.m[7]==0&&m.m[11]==0&&m.m[15]==1,"IK placement must be affine");
    const Vec3 x{m.m[0],m.m[1],m.m[2]},y{m.m[4],m.m[5],m.m[6]},z{m.m[8],m.m[9],m.m[10]};
    const auto n=magnitude(x);require(std::isfinite(n)&&n>epsilon,"Invalid IK model placement");
    require(std::abs(magnitude(y)-n)<1e-4f*n&&std::abs(magnitude(z)-n)<1e-4f*n&&
        std::abs(dot(x,y))<1e-4f*n*n&&std::abs(dot(x,z))<1e-4f*n*n&&std::abs(dot(y,z))<1e-4f*n*n&&dot(cross(x,y),z)>0,"IK placement requires rotation and positive uniform scale");
    inverseAffine(m);
    const auto a=x*(1/n),b=y*(1/n),c=z*(1/n);Quaternion q;
    const auto trace=a.x+b.y+c.z;
    if(trace>0){const auto t=std::sqrt(trace+1)*2;q={(b.z-c.y)/t,(c.x-a.z)/t,(a.y-b.x)/t,t/4};}
    else if(a.x>b.y&&a.x>c.z){const auto t=std::sqrt(1+a.x-b.y-c.z)*2;q={t/4,(b.x+a.y)/t,(c.x+a.z)/t,(b.z-c.y)/t};}
    else if(b.y>c.z){const auto t=std::sqrt(1+b.y-a.x-c.z)*2;q={(b.x+a.y)/t,t/4,(c.y+b.z)/t,(c.x-a.z)/t};}
    else{const auto t=std::sqrt(1+c.z-a.x-b.y)*2;q={(c.x+a.z)/t,(c.y+b.z)/t,t/4,(a.y-b.x)/t};}
    return normalizedQ(q);
}
}
void IkTarget::validate() const {
    require(finite(position)&&finite(pole)&&std::isfinite(weight)&&weight>=0&&weight<=1,"Invalid IK target");
    normalizedQ(rotation);
}
void validateIk(const Skeleton& s,std::span<const IkConstraint> constraints){
    require(!s.joints.empty()&&s.joints.size()<=256&&constraints.size()<=32,"IK rig exceeds limits");
    for(std::size_t i=0;i<constraints.size();++i){
        const auto& c=constraints[i];
        require(!c.name.empty()&&c.name.size()<=256,"IK constraint names must be unique");
        for(std::size_t j=0;j<i;++j)require(c.name!=constraints[j].name,"IK constraint names must be unique");
        require(c.mode>=IkMode::TwoBone&&c.mode<=IkMode::LookAt&&c.root<s.joints.size(),"Invalid IK constraint");
        require(std::isfinite(c.weight)&&c.weight>=0&&c.weight<=1&&finite(c.forward)&&magnitude(c.forward)>epsilon&&std::isfinite(c.maxAngle)&&c.maxAngle>=0&&c.maxAngle<=pi,"Invalid IK weight/axis/angle");
        c.target.validate();
        if(c.mode==IkMode::TwoBone)require(c.middle<s.joints.size()&&c.tip<s.joints.size()&&s.joints[c.middle].parent==std::int32_t(c.root)&&s.joints[c.tip].parent==std::int32_t(c.middle),"Two-bone IK requires a direct root-middle-tip chain");
    }
}
void validateIkRootMotion(const Skeleton& s,std::span<const IkConstraint> constraints,std::size_t joint){
    validateIk(s,constraints);
    for(std::size_t i=0;i<s.joints.size();++i)require(s.joints[i].parent>=-1&&s.joints[i].parent<std::int32_t(i),"Invalid IK skeleton hierarchy");
    require(joint<s.joints.size(),"Invalid IK root-motion joint");
    for(const auto& c:constraints)for(auto i=std::int32_t(joint);i>=0;i=s.joints[i].parent)
        require(c.root!=std::size_t(i)&&(c.mode!=IkMode::TwoBone||(c.middle!=std::size_t(i)&&(!c.orientTip||c.tip!=std::size_t(i)))),"IK must not rotate the root-motion joint or its ancestors");
}
Mat4 ikJointTransform(const Skeleton& s,std::span<const JointPose> poses,std::size_t joint){
    require(poses.size()==s.joints.size()&&poses.size()<=256&&joint<poses.size(),"Invalid IK pose/joint");
    std::array<WorldPose,256> w;worlds(s,poses,w,false);return w[joint].matrix;
}
IkTarget ikJointTarget(const Skeleton& s,std::span<const JointPose> poses,std::size_t joint){
    require(poses.size()==s.joints.size()&&poses.size()<=256&&joint<poses.size(),"Invalid IK pose/joint");
    std::array<WorldPose,256> w;worlds(s,poses,w,true);IkTarget result;result.position=w[joint].position;result.rotation=w[joint].rotation;return result;
}
IkResult solveIk(const Skeleton& s,std::span<const JointPose> input,std::span<const IkConstraint> constraints,std::span<const IkTarget> targets,std::span<JointPose> output){
    validateIk(s,constraints);
    require(input.size()==s.joints.size()&&output.size()==input.size()&&targets.size()==constraints.size(),"IK input sizes do not match");
    for(const auto& t:targets)t.validate();
    std::array<JointPose,256> storage;auto pose=std::span(storage).first(input.size());std::copy(input.begin(),input.end(),pose.begin());
    std::array<WorldPose,256> w;IkResult result;result.count=constraints.size();
    worlds(s,pose,w,false);
    for(std::size_t i=0;i<constraints.size();++i){
        const auto& c=constraints[i];const auto& t=targets[i];const auto weight=c.weight*t.weight;
        if(!t.enabled||weight==0)continue;
        worlds(s,pose,w,true);const auto rootBefore=pose[c.root];
        if(c.mode==IkMode::LookAt){
            const auto desired=t.position-w[c.root].position;if(magnitude(desired)<=epsilon)continue;
            const auto forward=rotateQ(w[c.root].rotation,c.forward);
            const auto angle=std::acos(std::clamp(dot(unit(forward),unit(desired)),-1.f,1.f));
            const auto rotation=product(swing(forward,desired,c.maxAngle),w[c.root].rotation);
            pose[c.root].rotation=product(conjugate(parentRotation(s,w,c.root)),rotation);
            pose[c.root]=blendPose(rootBefore,pose[c.root],weight);
            result.constraints[i]={std::max(0.f,angle-std::min(angle,c.maxAngle)*weight),angle>c.maxAngle};
        }else{
            const auto midBefore=pose[c.middle],tipBefore=pose[c.tip];
            const auto p=w[c.root].position,m=w[c.middle].position,e=w[c.tip].position;
            const auto a=magnitude(m-p),b=magnitude(e-m);require(a>epsilon&&b>epsilon,"IK limb segments must have nonzero length");
            auto delta=t.position-p;const auto requested=magnitude(delta);require(std::isfinite(requested),"IK target distance overflow");
            const auto axis=requested>epsilon?delta*(1/requested):magnitude(e-p)>epsilon?unit(e-p):unit(m-p);
            const auto reach=std::clamp(requested,std::max(std::abs(a-b),epsilon),a+b);
            auto bend=t.pole-p-axis*dot(t.pole-p,axis);
            if(magnitude(bend)<=epsilon)bend=m-p-axis*dot(m-p,axis);
            bend=magnitude(bend)>epsilon?unit(bend):perpendicular(axis);
            const auto along=(a*a-b*b+reach*reach)/(2*reach);
            const auto height=std::sqrt(std::max(0.f,a*a-along*along));
            const auto knee=p+axis*along+bend*height,goal=p+axis*reach;
            auto rotation=product(swing(m-p,knee-p),w[c.root].rotation);
            pose[c.root].rotation=product(conjugate(parentRotation(s,w,c.root)),rotation);
            worlds(s,pose,w,true);
            rotation=product(swing(w[c.tip].position-w[c.middle].position,goal-w[c.middle].position),w[c.middle].rotation);
            pose[c.middle].rotation=product(conjugate(parentRotation(s,w,c.middle)),rotation);
            worlds(s,pose,w,true);
            if(c.orientTip)pose[c.tip].rotation=product(conjugate(parentRotation(s,w,c.tip)),t.rotation);
            pose[c.root]=blendPose(rootBefore,pose[c.root],weight);pose[c.middle]=blendPose(midBefore,pose[c.middle],weight);
            if(c.orientTip)pose[c.tip]=blendPose(tipBefore,pose[c.tip],weight);
            worlds(s,pose,w,true);
            result.constraints[i]={magnitude(t.position-w[c.tip].position),requested>a+b||requested<std::abs(a-b)};
        }
    }
    // Validate final matrices before publishing any output, including overlap effects.
    worlds(s,pose,w,false);for(std::size_t j=0;j<pose.size();++j){const auto inverse=inverseAffine(w[j].matrix);for(auto v:inverse.m)require(std::isfinite(v),"IK inverse transform overflow");}
    std::copy(pose.begin(),pose.end(),output.begin());return result;
}
IkTarget ikTargetFromWorld(const Mat4& modelToWorld,const IkTarget& worldTarget){
    worldTarget.validate();const auto rotation=placementRotation(modelToWorld);const auto inverse=inverseAffine(modelToWorld);
    auto target=worldTarget;target.position=transformPoint(inverse,target.position);target.pole=transformPoint(inverse,target.pole);target.rotation=product(conjugate(rotation),target.rotation);target.validate();return target;
}
std::optional<IkTarget> groundIkTarget(const PhysicsWorld& physics,const Mat4& modelToWorld,const IkTarget& animatedFoot,const IkGroundProbe& probe,const PhysicsQueryFilter& filter){
    animatedFoot.validate();const auto placement=placementRotation(modelToWorld);
    require(finite(probe.worldUp)&&finite(probe.soleUp)&&std::isfinite(probe.above)&&probe.above>=0&&std::isfinite(probe.below)&&probe.below>=0&&probe.above+probe.below>epsilon&&std::isfinite(probe.soleOffset)&&probe.soleOffset>=0&&std::isfinite(probe.maxSlope)&&probe.maxSlope>=0&&probe.maxSlope<=pi*.5f,"Invalid IK ground probe");
    const auto up=unit(probe.worldUp),sole=unit(probe.soleUp);const auto origin=transformPoint(modelToWorld,animatedFoot.position)+up*probe.above;
    const auto hit=physics.raycast(origin,up*-1,probe.above+probe.below,filter);
    if(!hit||dot(unit(hit->normal),up)<std::cos(probe.maxSlope))return {};
    auto target=animatedFoot;target.position=transformPoint(inverseAffine(modelToWorld),hit->point+up*probe.soleOffset);
    const auto normal=rotateQ(conjugate(placement),unit(hit->normal));
    target.rotation=product(swing(rotateQ(target.rotation,sole),normal),target.rotation);target.validate();return target;
}
}
