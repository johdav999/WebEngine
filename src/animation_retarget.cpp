#include "engine/animation_retarget.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace engine {
namespace {
constexpr std::size_t poseBudget=32*1024*1024;
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
Quaternion normalizedQ(Quaternion q){
    const auto n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    require(std::isfinite(n)&&n>1e-8f,"Invalid retarget rotation");return {q.x/n,q.y/n,q.z/n,q.w/n};
}
Quaternion multiply(Quaternion a,Quaternion b){return normalizedQ({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});}
Quaternion inverse(Quaternion q){q=normalizedQ(q);return {-q.x,-q.y,-q.z,q.w};}
Vec3 rotate(Quaternion q,Vec3 v){return transformDirection(JointPose{{},{1,1,1},q}.matrix(),v);}
void name(const std::string& value){
    require(value.size()<=256,"Retarget joint name exceeds 256 bytes");
    for(unsigned char c:value)require(c>=32&&c!=127,"Retarget joint name contains control characters");
}
void fields(const AnimationRetargetProfile& p){
    require(p.joints.size()<=256,"Retarget profile exceeds 256 mappings");
    require(std::isfinite(p.translationScale)&&p.translationScale>=.0001f&&p.translationScale<=10000,"Retarget translation scale must be within [0.0001,10000]");
    normalizedQ(p.modelRotation);
    for(std::size_t i=0;i<p.joints.size();++i){
        const auto& m=p.joints[i];name(m.source);name(m.target);
        require(m.translation==RetargetTranslation::Bind||m.translation==RetargetTranslation::ScaledDelta,"Invalid retarget translation mode");
        for(std::size_t j=0;j<i;++j)require(m.source!=p.joints[j].source&&m.target!=p.joints[j].target,"Retarget mappings must be one-to-one");
    }
}
void uniform(Vec3 s){require(std::isfinite(s.x)&&s.x>1e-6f&&std::abs(s.x-s.y)<=1e-5f*s.x&&std::abs(s.x-s.z)<=1e-5f*s.x,"Retargeting requires positive uniform joint scales");}
void rig(const Skeleton& s){
    require(s.joints.size()<=256&&s.clips.size()<=64,"Retarget rig exceeds joint/clip limits");
    std::size_t bytes=0;
    for(const auto& c:s.clips){require(c.frames.size()<=(poseBudget-bytes)/sizeof(JointPose),"Retarget source poses exceed 32 MiB");bytes+=c.frames.size()*sizeof(JointPose);}
    s.validate();
    for(std::size_t i=0;i<s.joints.size();++i){
        const auto& j=s.joints[i];name(j.name);require(!j.name.empty()||j.parent<0,"Only a hierarchy root may have an empty joint name");uniform(j.bindPose.scale);
        for(std::size_t k=0;k<i;++k)require(j.name!=s.joints[k].name,"Retarget joint names must be unique");
    }
}
struct World { Mat4 matrix,inverseMatrix;Quaternion rotation;Vec3 position; };
World world(const JointPose& pose,const World* parent){
    World w;w.matrix=parent?parent->matrix*pose.matrix():pose.matrix();w.inverseMatrix=inverseAffine(w.matrix);
    for(auto v:w.matrix.m)require(std::isfinite(v),"Retarget transform overflow");
    for(auto v:w.inverseMatrix.m)require(std::isfinite(v),"Retarget inverse transform overflow");
    w.rotation=parent?multiply(parent->rotation,pose.rotation):normalizedQ(pose.rotation);
    w.position=transformPoint(w.matrix,{});return w;
}
using Worlds=std::array<World,256>;
Worlds bindWorlds(const Skeleton& s){
    Worlds result;for(std::size_t j=0;j<s.joints.size();++j){const auto& b=s.joints[j];result[j]=world(b.bindPose,b.parent<0?nullptr:&result[b.parent]);}return result;
}
struct Resolved {
    std::array<int,256> source{},parent{},sourceParent{};
    std::array<RetargetTranslation,256> translation{};
};
Resolved resolve(const Skeleton& source,const Skeleton& target,const AnimationRetargetProfile& profile){
    fields(profile);require(!profile.joints.empty(),"Retarget profile needs at least one mapped joint");
    Resolved result;result.source.fill(-1);result.parent.fill(-1);result.sourceParent.fill(-1);
    std::array<int,256> targetOfSource;targetOfSource.fill(-1);
    const auto index=[](const Skeleton& s,const std::string& name){for(std::size_t i=0;i<s.joints.size();++i)if(s.joints[i].name==name)return int(i);throw std::invalid_argument("Retarget mapping references a missing joint: "+name);};
    for(const auto& m:profile.joints){const auto a=index(source,m.source),b=index(target,m.target);result.source[b]=a;result.translation[b]=m.translation;targetOfSource[a]=b;}
    for(std::size_t j=0;j<target.joints.size();++j)if(result.source[j]>=0){
        auto tp=target.joints[j].parent,sp=source.joints[result.source[j]].parent;
        while(tp>=0&&result.source[tp]<0)tp=target.joints[tp].parent;
        while(sp>=0&&targetOfSource[sp]<0)sp=source.joints[sp].parent;
        require((tp<0&&sp<0)||(tp>=0&&sp>=0&&result.source[tp]==sp),"Retarget mappings disagree on nearest mapped ancestors");
        result.parent[j]=tp;result.sourceParent[j]=sp;
    }return result;
}
}
void AnimationRetargetProfile::validate(const Skeleton& source,const Skeleton& target) const {rig(source);rig(target);resolve(source,target,*this);}
AnimationRetargetProfile matchAnimationJoints(const Skeleton& source,const Skeleton& target){
    rig(source);rig(target);AnimationRetargetProfile p;
    for(const auto& t:target.joints)for(const auto& s:source.joints)if(t.name==s.name){p.joints.push_back({s.name,t.name,t.parent<0?RetargetTranslation::ScaledDelta:RetargetTranslation::Bind});break;}
    return p;
}
float estimateRetargetScale(const Skeleton& source,const Skeleton& target,const AnimationRetargetProfile& profile){
    rig(source);rig(target);const auto r=resolve(source,target,profile);const auto sw=bindWorlds(source),tw=bindWorlds(target);
    std::array<float,256> ratios;std::size_t count=0;
    for(std::size_t j=0;j<target.joints.size();++j)if(r.parent[j]>=0){
        const auto a=sw[r.source[j]].position-sw[r.sourceParent[j]].position,b=tw[j].position-tw[r.parent[j]].position;
        const auto al=std::sqrt(dot(a,a)),bl=std::sqrt(dot(b,b));
        if(al>1e-6f&&bl>1e-6f){const auto ratio=bl/al;require(std::isfinite(ratio),"Retarget proportion overflow");ratios[count++]=ratio;}
    }
    if(!count)return 1;
    std::sort(ratios.begin(),ratios.begin()+count);const auto value=count%2?ratios[count/2]:(ratios[count/2-1]+ratios[count/2])*.5f;
    require(value>=.0001f&&value<=10000,"Estimated retarget scale is outside supported bounds");return value;
}
std::vector<AnimationClip> retargetAnimations(const Skeleton& source,const Skeleton& target,const AnimationRetargetProfile& profile){
    rig(source);rig(target);const auto r=resolve(source,target,profile);require(!source.clips.empty(),"Retarget source has no animation clips");
    std::size_t bytes=0;for(const auto& c:source.clips){require(c.frameCount<=(poseBudget-bytes)/(sizeof(JointPose)*target.joints.size()),"Retarget output poses exceed 32 MiB");bytes+=std::size_t(c.frameCount)*target.joints.size()*sizeof(JointPose);}
    const auto sb=bindWorlds(source),tb=bindWorlds(target);Worlds sa,ta;std::vector<AnimationClip> result;result.reserve(source.clips.size());
    const auto alignment=normalizedQ(profile.modelRotation),unalign=inverse(alignment);
    for(const auto& clip:source.clips){
        AnimationClip out;out.name=clip.name;out.duration=clip.duration;out.frameCount=clip.frameCount;out.markers=clip.markers;out.frames.reserve(std::size_t(out.frameCount)*target.joints.size());
        for(std::size_t f=0;f<clip.frameCount;++f){
            for(std::size_t j=0;j<source.joints.size();++j){
                const auto& p=clip.frames[f*source.joints.size()+j];const auto& b=source.joints[j];uniform(p.scale);
                require(std::abs(p.scale.x-b.bindPose.scale.x)<=1e-5f*b.bindPose.scale.x,"Animated joint scale is not supported by retargeting");
                sa[j]=world(p,b.parent<0?nullptr:&sa[b.parent]);
            }
            for(std::size_t j=0;j<target.joints.size();++j){
                const auto& bind=target.joints[j];auto p=bind.bindPose;const auto parent=bind.parent;const auto src=r.source[j];
                if(src>=0){
                    // Transfer source model-space rotation relative to its bind pose,
                    // then solve destination local rotation against its actual parent.
                    const auto delta=multiply(multiply(multiply(alignment,sa[src].rotation),inverse(sb[src].rotation)),unalign);
                    const auto desired=multiply(delta,tb[j].rotation);
                    p.rotation=parent<0?desired:multiply(inverse(ta[parent].rotation),desired);
                    if(r.translation[j]==RetargetTranslation::ScaledDelta){
                        const auto sp=r.sourceParent[j],tp=r.parent[j];
                        auto deltaPosition=sa[src].position-sb[src].position;
                        if(sp>=0)deltaPosition=transformDirection(sb[sp].matrix,transformPoint(sa[sp].inverseMatrix,sa[src].position)-transformPoint(sb[sp].inverseMatrix,sb[src].position));
                        deltaPosition=rotate(alignment,deltaPosition)*profile.translationScale;
                        auto desiredPosition=tb[j].position+deltaPosition;
                        if(tp>=0)desiredPosition=transformPoint(ta[tp].matrix,transformPoint(tb[tp].inverseMatrix,tb[j].position)+transformDirection(tb[tp].inverseMatrix,deltaPosition));
                        p.translation=parent<0?desiredPosition:transformPoint(ta[parent].inverseMatrix,desiredPosition);
                    }
                }
                ta[j]=world(p,parent<0?nullptr:&ta[parent]);out.frames.push_back(p);
            }
        }result.push_back(std::move(out));
    }return result;
}
std::string retargetProfileText(const AnimationRetargetProfile& p){
    fields(p);std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17)<<"WERETARGET 1 "<<p.translationScale<<' '<<p.modelRotation.x<<' '<<p.modelRotation.y<<' '<<p.modelRotation.z<<' '<<p.modelRotation.w<<' '<<p.joints.size()<<'\n';
    for(const auto& j:p.joints)o<<std::quoted(j.source)<<' '<<std::quoted(j.target)<<' '<<int(j.translation)<<'\n';return o.str();
}
AnimationRetargetProfile parseRetargetProfile(std::string_view text){
    require(text.size()<=256*1024,"Retarget profile exceeds 256 KiB");std::istringstream in{std::string(text)};in.imbue(std::locale::classic());
    AnimationRetargetProfile p;std::string magic;int version=0;std::size_t count=0;
    in>>magic>>version>>p.translationScale>>p.modelRotation.x>>p.modelRotation.y>>p.modelRotation.z>>p.modelRotation.w>>count;
    require(bool(in)&&magic=="WERETARGET"&&version==1&&count<=256,"Invalid retarget profile header");p.joints.resize(count);
    for(auto& j:p.joints){int mode=0;in>>std::quoted(j.source)>>std::quoted(j.target)>>mode;j.translation=RetargetTranslation(mode);}
    require(bool(in),"Malformed retarget profile");in>>std::ws;require(in.eof(),"Unexpected retarget profile suffix");fields(p);return p;
}
}
