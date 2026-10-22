#pragma once
#include "engine/skeletal_mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/scene.hpp"

// Playground policy: a small authored rig, two sloping pads, fixed contact
// targets, hand reach and head aim. No policy or scene ownership in the solver.
inline engine::SkeletalMeshComponent& populateIkExample(engine::Scene& scene){
    using namespace engine;
    auto s=std::make_shared<Skeleton>();
    const auto joint=[&](const char* name,int parent,Vec3 translation){JointPose p;p.translation=translation;s->joints.push_back({name,parent,p});};
    joint("Root",-1,{});joint("Pelvis",0,{0,2.25f,0});
    joint("Left hip",1,{-.35f,0,0});joint("Left knee",2,{0,-1,0});joint("Left foot",3,{0,-1,0});
    joint("Right hip",1,{.35f,0,0});joint("Right knee",5,{0,-1,0});joint("Right foot",6,{0,-1,0});
    joint("Shoulder",1,{.65f,.85f,0});joint("Elbow",8,{.65f,0,0});joint("Hand",9,{.65f,0,0});
    joint("Head",1,{0,1.4f,0});
    std::vector<JointPose> pose;for(const auto& j:s->joints)pose.push_back(j.bindPose);
    s->bindings.push_back({});
    for(std::size_t j=0;j<pose.size();++j)s->bindings.push_back({std::uint32_t(j),inverseAffine(ikJointTransform(*s,pose,j))});
    AnimationClip clip;clip.name="Weight shift";clip.duration=2;clip.frameCount=3;
    for(int f=0;f<3;++f){auto frame=pose;const float angle=f==1?.06f:-.06f;frame[1].rotation={0,0,std::sin(angle/2),std::cos(angle/2)};clip.frames.insert(clip.frames.end(),frame.begin(),frame.end());}s->clips.push_back(clip);s->validate();
    auto geometry=std::make_shared<Mesh>();geometry->skeleton=s;const auto cube=Mesh::cube();
    for(auto color:{Vec3{.1f,.65f,.72f},Vec3{.92f,.62f,.16f},Vec3{.35f,.45f,.95f},Vec3{.94f,.85f,.68f}}){auto m=std::make_shared<Material>();m->baseColor=color;geometry->materials.push_back(m);}
    const auto box=[&](std::size_t bone,Vec3 center,Vec3 size,std::uint32_t material){
        const auto base=std::uint32_t(geometry->vertices.size()),first=std::uint32_t(geometry->indices.size());
        const auto origin=transformPoint(ikJointTransform(*s,pose,bone),{});
        for(auto v:cube->vertices){v.position=origin+center+Vec3{v.position.x*size.x,v.position.y*size.y,v.position.z*size.z};geometry->vertices.push_back(v);SkinVertex skin;skin.joints[0]=std::uint32_t(bone+1);geometry->skin.push_back(skin);}
        for(auto i:cube->indices)geometry->indices.push_back(base+i);
        geometry->parts.push_back({first,std::uint32_t(cube->indices.size()),material});
    };
    box(1,{0,.4f,0},{1.1f,.9f,.5f},0);box(1,{0,1.05f,0},{.2f,.4f,.2f},0);
    box(1,{-.72f,.45f,0},{.22f,.9f,.22f},2);
    for(auto root:{2u,5u}){box(root,{0,-.5f,0},{.24f,.9f,.24f},0);box(root+1,{0,-.5f,0},{.2f,.9f,.2f},1);box(root+2,{0,.04f,.14f},{.35f,.16f,.6f},1);}
    box(8,{.325f,0,0},{.6f,.22f,.22f},2);box(9,{.325f,0,0},{.6f,.18f,.18f},2);box(10,{.1f,0,0},{.24f,.24f,.24f},3);
    box(11,{0,.12f,0},{.5f,.55f,.5f},3);box(11,{0,.12f,.31f},{.16f,.14f,.2f},2);
    auto& root=scene.spawn("IK demonstration");root.setSavedId("ik.demo");
    for(int side=0;side<2;++side){
        auto& pad=scene.spawn(side?"Raised pad":"Low pad");pad.transform.position={side?.45f:-.45f,side?.45f:.12f,0};pad.transform.rotation.z=side?-.18f:.12f;
        auto& mesh=pad.addComponent<MeshComponent>("Sloping ground",Mesh::cube());mesh.transform.scale={.85f,.3f,1.5f};mesh.color={.25f,.32f,.4f};
        RigidBodySettings body;body.motion=BodyMotion::Static;pad.addComponent<RigidBodyComponent>("Ground body",body);pad.addComponent<ColliderComponent>("Ground collider",CollisionShape::box({.425f,.15f,.75f}));
    }
    scene.synchronize();
    auto d=std::make_shared<AnimationControllerDefinition>();d->states={{"Weight shift",0,true,1,{}}};
    for(auto hip:{2u,5u}){IkConstraint c;c.name=hip==2?"Left foot":"Right foot";c.root=hip;c.middle=hip+1;c.tip=hip+2;c.orientTip=true;c.target.position=transformPoint(ikJointTransform(*s,pose,c.tip),{});c.target.pole={c.target.position.x,1,2};
        IkGroundProbe probe;probe.above=1;probe.below=1;probe.soleOffset=.04f;
        if(auto contact=groundIkTarget(scene.physics(),Mat4::identity(),c.target,probe))c.target=*contact;
        d->ik.push_back(c);
    }
    IkConstraint hand;hand.name="Hand reach";hand.root=8;hand.middle=9;hand.tip=10;hand.target.position={1.4f,2.9f,.7f};hand.target.pole={2,2,0};hand.orientTip=true;d->ik.push_back(hand);
    IkConstraint look;look.name="Look";look.mode=IkMode::LookAt;look.root=11;look.maxAngle=pi/3;look.target.position={2,3.6f,3};d->ik.push_back(look);
    auto& skin=root.addComponent<SkeletalMeshComponent>("IK character",geometry);skin.setSavedId("ik.character");skin.color={1,1,1};skin.animation().setController(d);
    auto& target=root.addComponent<MeshComponent>("Reach object",Mesh::cube());target.transform.position=hand.target.position+Vec3{.15f,0,0};target.transform.scale={.2f,.2f,.2f};target.color={.9f,.35f,.3f};
    return skin;
}
