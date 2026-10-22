#include "engine/animation_library.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/scene_archive.hpp"
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <cstring>

namespace engine {
namespace {
void require(bool v,const char* message){if(!v)throw std::invalid_argument(message);}
void text(const std::string& s,bool empty=false){require((empty||!s.empty())&&s.size()<=256,"Animation names must contain 1..256 bytes");for(unsigned char c:s)require(c>=32&&c!=127,"Animation text cannot contain control characters");}
void budget(const Skeleton& s){
    require(!s.joints.empty()&&s.joints.size()<=256&&s.bindings.size()<=1024&&s.clips.size()<=64,"Animation skeleton exceeds limits");
    std::size_t bytes=0;for(const auto& c:s.clips){require(c.frames.size()<=(32*1024*1024-bytes)/sizeof(JointPose),"Animation poses exceed 32 MiB");bytes+=c.frames.size()*sizeof(JointPose);text(c.name);}
    s.validate();
}
std::vector<std::size_t> mapping(const Skeleton& target,const Skeleton& source){
    require(target.joints.size()==source.joints.size(),"Skeleton joint counts differ");
    std::unordered_set<std::string> names;
    for(const auto& j:source.joints)require((!j.name.empty()||j.parent<0)&&names.insert(j.name).second,"Source joint names must be unique");
    names.clear();std::vector<std::size_t> map;
    for(const auto& j:target.joints){require((!j.name.empty()||j.parent<0)&&names.insert(j.name).second,"Target joint names must be unique");auto it=std::find_if(source.joints.begin(),source.joints.end(),[&](const auto& q){return q.name==j.name;});require(it!=source.joints.end(),"Skeleton joint names differ");map.push_back(std::size_t(it-source.joints.begin()));}
    for(std::size_t i=0;i<map.size();++i){const auto& a=target.joints[i];const auto& b=source.joints[map[i]];
        require((a.parent<0&&b.parent<0)||(a.parent>=0&&b.parent>=0&&map[std::size_t(a.parent)]==std::size_t(b.parent)),"Skeleton hierarchy differs");
        const auto am=a.bindPose.matrix(),bm=b.bindPose.matrix();for(std::size_t k=0;k<16;++k)require(std::abs(am.m[k]-bm.m[k])<=1e-4f,"Skeleton bind poses differ; retargeting is required");
    }return map;
}
ArchiveLimits limits(){ArchiveLimits l;l.maxBytes=64*1024*1024;l.maxAllocatedBytes=128*1024*1024;l.maxNodes=4;l.maxResources=8;l.maxElements=1000000;return l;}
constexpr char magic[]="WEANIM1\n";
}
AnimationLibrary::AnimationLibrary(AnimationLibraryData d):data_(std::move(d)),controller_(std::make_shared<const AnimationControllerDefinition>(data_.controller)){}
std::shared_ptr<const AnimationLibrary> AnimationLibrary::create(AnimationLibraryData d){
    text(d.name);require(bool(d.skeleton),"Animation library needs a skeleton");budget(*d.skeleton);
    require(!d.clips.empty()&&d.clips.size()==d.skeleton->clips.size(),"Animation clip metadata must match the skeleton");
    std::unordered_set<std::string> ids;for(const auto& c:d.clips){text(c.id);text(c.label,true);text(c.folder,true);text(c.tags,true);require(ids.insert(c.id).second,"Duplicate animation clip ID");require(std::isfinite(c.speed)&&c.speed>0&&c.speed<=100,"Clip speed must be within (0,100]");}
    require(d.controller.states.size()<=256&&d.controller.parameters.size()<=64&&d.controller.transitions.size()<=1024,"Animation graph exceeds authoring limits");
    d.controller.validate(*d.skeleton);require(d.positions.size()==d.controller.states.size(),"Every animation state needs a graph position");
    for(auto p:d.positions)require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=10000&&std::abs(p.y)<=10000,"Invalid animation graph position");
    for(const auto& p:d.controller.parameters)text(p.name);
    for(const auto& s:d.controller.states){text(s.name);require(s.speed<=100,"State speed exceeds 100");}
    for(const auto& l:d.controller.layers){text(l.name);require(l.motion.speed<=100,"Layer speed exceeds 100");}
    for(const auto& t:d.controller.transitions)require(t.conditions.size()<=64&&t.fadeSeconds<=3600&&(!t.exitTime||*t.exitTime<=10000),"Animation transition exceeds limits");
    if(d.rootMotionJoint)require(*d.rootMotionJoint<d.skeleton->joints.size(),"Invalid root motion joint");
    for(const auto& c:d.controller.ik)text(c.name);
    if(d.rootMotionJoint)validateIkRootMotion(*d.skeleton,d.controller.ik,*d.rootMotionJoint);
    return std::shared_ptr<const AnimationLibrary>(new AnimationLibrary(std::move(d)));
}
std::shared_ptr<const AnimationLibrary> AnimationLibrary::fromSkeleton(std::shared_ptr<const Skeleton> s,std::shared_ptr<const AnimationControllerDefinition> c){
    require(bool(s)&&!s->clips.empty(),"Select a skeletal mesh with animation clips");AnimationLibraryData d;d.skeleton=std::move(s);
    for(std::size_t i=0;i<d.skeleton->clips.size();++i)d.clips.push_back({"clip-"+std::to_string(i+1),"","",true,1,{}});
    if(c)d.controller=*c;else d.controller.states.push_back({"Idle",0,true,1,{}});
    for(std::size_t i=0;i<d.controller.states.size();++i)d.positions.push_back({float(60+(i%4)*230),float(80+(i/4)*140)});
    return create(std::move(d));
}
std::size_t AnimationLibrary::clipIndex(std::string_view id)const{for(std::size_t i=0;i<data_.clips.size();++i)if(data_.clips[i].id==id)return i;throw std::invalid_argument("Unknown animation clip ID");}
void AnimationLibrary::validateTarget(const Skeleton& target)const{budget(target);mapping(target,*data_.skeleton);}
std::shared_ptr<const AnimationLibrary> AnimationLibrary::append(const Skeleton& source)const{
    budget(source);require(!source.clips.empty(),"Imported file contains no animation clips");const auto map=mapping(*data_.skeleton,source);
    require(data_.clips.size()+source.clips.size()<=64,"Animation library supports at most 64 clips");
    auto d=data_;auto s=std::make_shared<Skeleton>(*d.skeleton);
    std::size_t total=0;for(const auto& c:s->clips)total+=c.frames.size()*sizeof(JointPose);
    for(const auto& original:source.clips){require(original.frames.size()<=(32*1024*1024-total)/sizeof(JointPose),"Animation poses exceed 32 MiB");total+=original.frames.size()*sizeof(JointPose);
        auto clip=original;for(std::size_t f=0;f<clip.frameCount;++f)for(std::size_t j=0;j<map.size();++j)clip.frames[f*map.size()+j]=original.frames[f*map.size()+map[j]];
        s->clips.push_back(std::move(clip));std::size_t id=s->clips.size();std::string key;do{key="clip-"+std::to_string(id++);}while(std::any_of(d.clips.begin(),d.clips.end(),[&](const auto& e){return e.id==key;}));d.clips.push_back({key,"Imported","",true,1,{}});
    }d.skeleton=std::move(s);return create(std::move(d));
}
std::shared_ptr<const AnimationLibrary> AnimationLibrary::appendRetargeted(const Skeleton& source,const AnimationRetargetProfile& profile)const{
    budget(source);require(data_.clips.size()+source.clips.size()<=64,"Animation library supports at most 64 clips");
    std::size_t bytes=0;for(const auto& c:data_.skeleton->clips)bytes+=c.frames.size()*sizeof(JointPose);
    for(const auto& c:source.clips){require(c.frameCount<=(32*1024*1024-bytes)/(sizeof(JointPose)*data_.skeleton->joints.size()),"Retargeted library exceeds 32 MiB");bytes+=std::size_t(c.frameCount)*data_.skeleton->joints.size()*sizeof(JointPose);}
    Skeleton baked;baked.joints=data_.skeleton->joints;baked.bindings=data_.skeleton->bindings;
    baked.clips=retargetAnimations(source,*data_.skeleton,profile);
    return append(baked);
}
void AnimationLibrary::apply(SkeletalMeshComponent& mesh)const{
    mesh.requireIdle();
    require(bool(mesh.mesh)&&bool(mesh.mesh->skeleton),"Target has no skeleton");validateTarget(*mesh.mesh->skeleton);
    // Preserve target inverse binds and joint ordering; map library poses to them.
    const auto map=mapping(*mesh.mesh->skeleton,*data_.skeleton);auto skeleton=std::make_shared<Skeleton>(*mesh.mesh->skeleton);skeleton->clips=data_.skeleton->clips;
    for(std::size_t c=0;c<skeleton->clips.size();++c)for(std::size_t f=0;f<skeleton->clips[c].frameCount;++f)for(std::size_t j=0;j<map.size();++j)skeleton->clips[c].frames[f*map.size()+j]=data_.skeleton->clips[c].frames[f*map.size()+map[j]];
    skeleton->validate();auto geometry=std::make_shared<Mesh>(*mesh.mesh);geometry->skeleton=std::move(skeleton);
    // Validate/evaluate all player setup on a detached candidate before publication.
    auto remapped=std::make_shared<AnimationControllerDefinition>(*controller_);
    for(auto& layer:remapped->layers)if(!layer.mask.empty()){const auto mask=layer.mask;for(std::size_t j=0;j<map.size();++j)layer.mask[j]=mask[map[j]];}
    for(auto& c:remapped->ik){const auto remap=[&](std::size_t i){return std::size_t(std::find(map.begin(),map.end(),i)-map.begin());};c.root=remap(c.root);if(c.mode==IkMode::TwoBone){c.middle=remap(c.middle);c.tip=remap(c.tip);}}
    SkeletalMeshComponent staged("Animation validation",geometry);staged.animation().setController(remapped);
    std::optional<std::size_t> root;if(data_.rootMotionJoint){root=std::size_t(std::find(map.begin(),map.end(),*data_.rootMotionJoint)-map.begin());staged.animation().enableRootMotion(*root);}
    mesh.mesh=std::move(geometry);mesh.animation().setController(remapped);if(root)mesh.animation().enableRootMotion(*root);
}
std::string AnimationLibraryArchive::metadata(const AnimationLibraryData& d){
    std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17)<<"ANIM 3 "<<std::quoted(d.name)<<' '<<(d.rootMotionJoint?int(*d.rootMotionJoint):-1)<<'\n'<<d.clips.size()<<'\n';
    for(const auto& c:d.clips)o<<std::quoted(c.id)<<' '<<std::quoted(c.folder)<<' '<<std::quoted(c.tags)<<' '<<c.loop<<' '<<c.speed<<' '<<std::quoted(c.label)<<'\n';
    const auto& g=d.controller;o<<g.parameters.size()<<'\n';for(const auto& p:g.parameters){o<<std::quoted(p.name)<<' '<<int(p.type)<<' ';std::visit([&](auto v){o<<v;},p.initial);o<<'\n';}
    o<<g.initialState<<' '<<g.states.size()<<'\n';for(std::size_t i=0;i<g.states.size();++i){const auto& s=g.states[i];o<<std::quoted(s.name)<<' '<<s.clip<<' '<<s.loop<<' '<<s.speed<<' '<<d.positions[i].x<<' '<<d.positions[i].y<<' '<<bool(s.blend);if(s.blend)o<<' '<<s.blend->clip<<' '<<s.blend->parameter<<' '<<s.blend->minimum<<' '<<s.blend->maximum;o<<'\n';}
    o<<g.transitions.size()<<'\n';for(const auto& t:g.transitions){o<<t.from<<' '<<t.to<<' '<<t.fadeSeconds<<' '<<bool(t.exitTime);if(t.exitTime)o<<' '<<*t.exitTime;o<<' '<<t.conditions.size();for(const auto& c:t.conditions){o<<' '<<c.parameter<<' '<<int(c.comparison)<<' '<<c.value.index()<<' ';std::visit([&](auto v){o<<v;},c.value);}o<<'\n';}
    const auto tree=[&](const std::optional<AnimationBlendTree>& value){o<<bool(value);if(value){const auto& t=*value;o<<' '<<t.parameterX<<' '<<(t.parameterY?int(*t.parameterY):-1)<<' '<<t.synchronizePhase<<' '<<t.points.size();for(const auto& p:t.points)o<<' '<<p.clip<<' '<<p.x<<' '<<p.y;}o<<'\n';};
    for(const auto& s:g.states)tree(s.tree);
    o<<g.layers.size()<<'\n';for(const auto& l:g.layers){const auto& m=l.motion;o<<std::quoted(l.name)<<' '<<int(l.mode)<<' '<<l.weight<<' '<<(l.weightParameter?int(*l.weightParameter):-1)<<' '<<(l.referenceClip?int(*l.referenceClip):-1)<<' '<<l.referenceTime<<' '<<l.mask.size();for(auto w:l.mask)o<<' '<<w;
        o<<' '<<std::quoted(m.name)<<' '<<m.clip<<' '<<m.loop<<' '<<m.speed<<' '<<bool(m.blend);if(m.blend)o<<' '<<m.blend->clip<<' '<<m.blend->parameter<<' '<<m.blend->minimum<<' '<<m.blend->maximum;o<<'\n';tree(m.tree);}
    o<<g.ik.size()<<'\n';for(const auto& c:g.ik){const auto& t=c.target;
        o<<std::quoted(c.name)<<' '<<int(c.mode)<<' '<<c.root<<' '<<c.middle<<' '<<c.tip<<' '<<c.weight<<' '<<c.orientTip<<' '<<c.forward.x<<' '<<c.forward.y<<' '<<c.forward.z<<' '<<c.maxAngle<<' '
         <<t.position.x<<' '<<t.position.y<<' '<<t.position.z<<' '<<t.pole.x<<' '<<t.pole.y<<' '<<t.pole.z<<' '<<t.rotation.x<<' '<<t.rotation.y<<' '<<t.rotation.z<<' '<<t.rotation.w<<' '<<t.weight<<' '<<t.enabled<<'\n';}
    return o.str();
}
AnimationLibraryData AnimationLibraryArchive::parseMetadata(std::string_view bytes,std::shared_ptr<const Skeleton> skeleton){
    require(bytes.size()<=512*1024,"Animation metadata exceeds 512 KiB");std::istringstream in{std::string(bytes)};in.imbue(std::locale::classic());AnimationLibraryData d;d.skeleton=std::move(skeleton);std::string magic;int version=0,root=-1;in>>magic>>version>>std::quoted(d.name)>>root;require(magic=="ANIM"&&(version>=1&&version<=3)&&root>=-1,"Unsupported animation metadata");if(root>=0)d.rootMotionJoint=std::size_t(root);
    auto count=[&](std::size_t max){std::size_t n=0;require(bool(in>>n)&&n<=max,"Invalid animation collection size");return n;};
    d.clips.resize(count(64));for(auto& c:d.clips)in>>std::quoted(c.id)>>std::quoted(c.folder)>>std::quoted(c.tags)>>c.loop>>c.speed>>std::quoted(c.label);
    auto value=[&](int type)->AnimationParameterValue{require(type==0||type==1,"Invalid animation value type");if(type==0){double v=0;in>>v;return v;}bool v=false;in>>v;return v;};
    auto& g=d.controller;g.parameters.resize(count(64));for(auto& p:g.parameters){int type=0;in>>std::quoted(p.name)>>type;require(type>=0&&type<=2,"Invalid animation parameter type");p.type=AnimationParameterType(type);p.initial=value(type==0?0:1);}
    in>>g.initialState;g.states.resize(count(256));d.positions.resize(g.states.size());for(std::size_t i=0;i<g.states.size();++i){auto& s=g.states[i];bool blend=false;in>>std::quoted(s.name)>>s.clip>>s.loop>>s.speed>>d.positions[i].x>>d.positions[i].y>>blend;if(blend){s.blend.emplace();in>>s.blend->clip>>s.blend->parameter>>s.blend->minimum>>s.blend->maximum;}}
    g.transitions.resize(count(1024));for(auto& t:g.transitions){bool exit=false;in>>t.from>>t.to>>t.fadeSeconds>>exit;if(exit){double v=0;in>>v;t.exitTime=v;}t.conditions.resize(count(64));for(auto& c:t.conditions){int comparison=0,type=0;in>>c.parameter>>comparison>>type;c.comparison=AnimationComparison(comparison);c.value=value(type);}}
    if(version>=2) {
        const auto optionalIndex=[&](){int v=-1;in>>v;require(v>=-1,"Invalid animation index");return v<0?std::optional<std::size_t>{}:std::optional<std::size_t>(std::size_t(v));};
        const auto tree=[&](std::optional<AnimationBlendTree>& result){bool present=false;in>>present;if(present){result.emplace();auto& t=*result;in>>t.parameterX;t.parameterY=optionalIndex();in>>t.synchronizePhase;t.points.resize(count(64));for(auto& p:t.points)in>>p.clip>>p.x>>p.y;}};
        for(auto& s:g.states)tree(s.tree);
        g.layers.resize(count(16));for(auto& l:g.layers){int mode=0;in>>std::quoted(l.name)>>mode>>l.weight;l.mode=AnimationLayerMode(mode);l.weightParameter=optionalIndex();l.referenceClip=optionalIndex();in>>l.referenceTime;l.mask.resize(count(256));for(auto& w:l.mask)in>>w;
            auto& m=l.motion;bool blend=false;in>>std::quoted(m.name)>>m.clip>>m.loop>>m.speed>>blend;if(blend){m.blend.emplace();in>>m.blend->clip>>m.blend->parameter>>m.blend->minimum>>m.blend->maximum;}tree(m.tree);}
    }
    if(version>=3){g.ik.resize(count(32));for(auto& c:g.ik){int mode=0;auto& t=c.target;
        in>>std::quoted(c.name)>>mode>>c.root>>c.middle>>c.tip>>c.weight>>c.orientTip>>c.forward.x>>c.forward.y>>c.forward.z>>c.maxAngle
          >>t.position.x>>t.position.y>>t.position.z>>t.pole.x>>t.pole.y>>t.pole.z>>t.rotation.x>>t.rotation.y>>t.rotation.z>>t.rotation.w>>t.weight>>t.enabled;c.mode=IkMode(mode);}}
    require(bool(in),"Malformed animation metadata");in>>std::ws;require(in.eof(),"Unexpected animation metadata suffix");return d;
}
std::vector<std::byte> AnimationLibraryArchive::save(const AnimationLibrary& library){
    Scene scene("Animation library");scene.setSavedId("animation-library");auto& s=scene.addChild<SkeletonComponent>();s.setSavedId("animation-skeleton");s.setSkeleton(library.data().skeleton);auto& p=s.addChild<AnimationComponent>("Controller",s);p.setSavedId("animation-controller");p.setController(library.controller());
    const auto meta=metadata(library.data());const auto payload=SceneArchive::save(scene,{}, {},std::as_bytes(std::span(meta.data(),meta.size())),limits());std::vector<std::byte> out;out.reserve(sizeof(magic)-1+payload.size());for(char c:std::string_view(magic))out.push_back(std::byte(c));out.insert(out.end(),payload.begin(),payload.end());return out;
}
std::shared_ptr<const AnimationLibrary> AnimationLibraryArchive::load(std::span<const std::byte> bytes){
    require(bytes.size()>=sizeof(magic)-1&&bytes.size()<=64*1024*1024&&std::memcmp(bytes.data(),magic,sizeof(magic)-1)==0,"Invalid animation library file");auto doc=SceneArchive::load(bytes.subspan(sizeof(magic)-1),limits());require(doc.scene->children().size()==1,"Invalid animation library hierarchy");auto* s=dynamic_cast<SkeletonComponent*>(doc.scene->children()[0].get());require(s&&s->children().size()==1,"Invalid animation library skeleton");auto* p=dynamic_cast<AnimationComponent*>(s->children()[0].get());require(p&&p->controller()&&p->children().empty(),"Invalid animation library controller");auto d=parseMetadata(std::string_view(reinterpret_cast<const char*>(doc.applicationData.data()),doc.applicationData.size()),s->skeleton());
    auto next=AnimationLibrary::create(d);d.controller=*p->controller();require(metadata(d)==metadata(next->data()),"Animation graph records disagree");return next;
}
}
