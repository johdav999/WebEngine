#pragma once
#include "engine/animation_library.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/scene.hpp"

inline std::shared_ptr<engine::Mesh> retargetExampleMesh(bool tall){
    using namespace engine;
    const float height=tall?2.0f:1.3f,upper=tall?.75f:.5f,lower=tall?.9f:.4f;
    auto s=std::make_shared<Skeleton>();
    const auto joint=[&](const char* name,int parent,Vec3 translation){JointPose p;p.translation=translation;s->joints.push_back({name,parent,p});};
    joint("Root",-1,{});joint("Pelvis",0,{0,height,0});joint("Upper arm",1,{.45f,.7f,0});
    joint("Forearm",2,{upper,0,0});joint("Hand",3,{lower,0,0});joint("Head",1,{0,1.15f,0});
    joint("Left leg",0,{-.25f,0,0});joint("Right leg",0,{.25f,0,0});
    if(tall)s->joints[3].bindPose.rotation={0,0,std::sin(.125f),std::cos(.125f)};
    std::vector<JointPose> bind;for(const auto& j:s->joints)bind.push_back(j.bindPose);
    s->bindings.push_back({});for(std::size_t i=0;i<bind.size();++i)s->bindings.push_back({std::uint32_t(i),inverseAffine(ikJointTransform(*s,bind,i))});
    AnimationClip idle;idle.name="Rest";idle.duration=2;idle.frameCount=2;idle.frames=bind;idle.frames.insert(idle.frames.end(),bind.begin(),bind.end());s->clips.push_back(idle);
    if(!tall){
        AnimationClip wave;wave.name="Wave and step";wave.duration=2;wave.frameCount=9;wave.markers={{.5,"Wave","greeting"}};
        for(int f=0;f<9;++f){auto pose=bind;const auto phase=pi*float(f)/8;pose[0].translation.x=.2f*std::sin(2*phase);pose[1].translation.y+=.1f*std::sin(phase);
            const auto a=.7f*std::sin(phase),b=1.1f*std::sin(phase);pose[2].rotation={0,0,std::sin(a/2),std::cos(a/2)};pose[3].rotation={0,std::sin(b/2),0,std::cos(b/2)};wave.frames.insert(wave.frames.end(),pose.begin(),pose.end());}
        s->clips.push_back(wave);
    }s->validate();
    auto mesh=std::make_shared<Mesh>();mesh->skeleton=s;const auto cube=Mesh::cube();
    for(auto color:{tall?Vec3{.95f,.65f,.18f}:Vec3{.12f,.65f,.75f},Vec3{.85f,.87f,.92f},Vec3{.25f,.38f,.7f}}){auto m=std::make_shared<Material>();m->baseColor=color;mesh->materials.push_back(m);}
    const auto box=[&](std::size_t bone,Vec3 center,Vec3 scale,std::uint32_t material){
        const auto first=std::uint32_t(mesh->indices.size()),base=std::uint32_t(mesh->vertices.size());const auto placement=ikJointTransform(*s,bind,bone);
        for(auto v:cube->vertices){v.position=transformPoint(placement,center+Vec3{v.position.x*scale.x,v.position.y*scale.y,v.position.z*scale.z});v.normal=normalized(transformDirection(placement,v.normal));mesh->vertices.push_back(v);SkinVertex skin;skin.joints[0]=std::uint32_t(bone+1);mesh->skin.push_back(skin);}
        for(auto i:cube->indices)mesh->indices.push_back(base+i);mesh->parts.push_back({first,std::uint32_t(cube->indices.size()),material});
    };
    box(1,{0,.35f,0},{.85f,.85f,.4f},0);box(1,{0,.9f,0},{.2f,.3f,.2f},0);
    box(2,{upper/2,0,0},{upper,.18f,.18f},0);box(3,{lower/2,0,0},{lower,.16f,.16f},1);box(4,{.08f,0,0},{.2f,.2f,.2f},1);
    box(5,{0,.1f,0},{.45f,.5f,.45f},1);box(5,{0,.1f,.27f},{.14f,.12f,.15f},2);
    for(auto leg:{6u,7u}){box(leg,{0,height/2,0},{.22f,height,.22f},0);box(leg,{0,.08f,.12f},{.3f,.16f,.5f},2);}
    return mesh;
}
inline void populateRetargetExample(engine::Scene& scene){
    using namespace engine;
    auto source=retargetExampleMesh(false),target=retargetExampleMesh(true);
    auto profile=matchAnimationJoints(*source->skeleton,*target->skeleton);
    profile.translationScale=estimateRetargetScale(*source->skeleton,*target->skeleton,profile);
    for(auto& m:profile.joints)if(m.target=="Pelvis")m.translation=RetargetTranslation::ScaledDelta;
    auto library=AnimationLibrary::fromSkeleton(target->skeleton)->appendRetargeted(*source->skeleton,profile);
    auto data=library->data();data.controller.states[0].name="Retargeted wave";data.controller.states[0].clip=2;library=AnimationLibrary::create(std::move(data));
    auto& donor=scene.spawn("Source proportions");donor.transform.position={-2.0f,0,0};auto& a=donor.addComponent<SkeletalMeshComponent>("Source character",source);a.setSavedId("retarget.source");a.color={1,1,1};a.animation().play(1);
    auto& recipient=scene.spawn("Destination proportions");recipient.transform.position={1.0f,0,0};auto& b=recipient.addComponent<SkeletalMeshComponent>("Retargeted character",target);b.setSavedId("retarget.target");b.color={1,1,1};library->apply(b);
}
