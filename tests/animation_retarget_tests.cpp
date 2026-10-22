#include "engine/animation_retarget.hpp"
#include "engine/editor_animation.hpp"
#include "engine/editor.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "../src/retarget_example.hpp"
#include <iostream>
#include <limits>
using namespace engine;
namespace {
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
bool near(float a,float b){return std::abs(a-b)<.0003f;}
void near(Vec3 a,Vec3 b){check(near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z),"Unexpected retarget position");}
template<class F>void rejects(F fn){bool threw=false;try{fn();}catch(const std::exception&){threw=true;}check(threw,"Expected retarget rejection");}
std::shared_ptr<Skeleton> rig(float upper,float lower){
    auto s=std::make_shared<Skeleton>();s->joints={{"Root",-1,{}},{"Arm",0,{}},{"Hand",1,{}}};s->joints[1].bindPose.translation={upper,0,0};s->joints[2].bindPose.translation={lower,0,0};s->bindings={{},{0,Mat4::identity()},{1,Mat4::identity()},{2,Mat4::identity()}};
    AnimationClip c;c.name="Move";c.duration=1;c.frameCount=2;for(int f=0;f<2;++f)for(const auto& j:s->joints)c.frames.push_back(j.bindPose);
    c.frames[3].translation={1,0,0};c.frames[4].rotation={0,0,std::sqrt(.5f),std::sqrt(.5f)};c.markers={{.5,"Footstep","left"}};s->clips.push_back(c);s->validate();return s;
}
void transfer(){
    auto source=rig(1,1),target=rig(2,3);target->joints[1].bindPose.rotation={0,0,std::sin(.2f),std::cos(.2f)};
    auto p=matchAnimationJoints(*source,*target);p.translationScale=2;
    const auto clips=retargetAnimations(*source,*target,p);
    check(clips[0].frameCount==2&&clips[0].duration==1&&clips[0].markers[0].payload=="left","Timing and markers survive bake");
    for(std::size_t j=0;j<3;++j)for(int k=0;k<16;++k)check(near(clips[0].frames[j].matrix().m[k],target->joints[j].bindPose.matrix().m[k]),"Source rest maps to destination rest");
    near(clips[0].frames[3].translation,{2,0,0});near(clips[0].frames[4].translation,{2,0,0});near(clips[0].frames[5].translation,{3,0,0});
    const auto world=ikJointTransform(*target,std::span(clips[0].frames).subspan(3),1);
    near(transformDirection(world,{1,0,0}),{-std::sin(.4f),std::cos(.4f),0});
    check(near(estimateRetargetScale(*source,*rig(2,3),p),2.5f),"Median proportions estimate");
    p.joints[0].translation=RetargetTranslation::Bind;near(retargetAnimations(*source,*target,p)[0].frames[3].translation,{});
    p.joints[0].translation=RetargetTranslation::ScaledDelta;p.modelRotation={0,0,std::sqrt(.5f),std::sqrt(.5f)};
    near(retargetAnimations(*source,*target,p)[0].frames[3].translation,{0,2,0});
    // Root/pelvis offsets in parent bind coordinates do not apply unit scaling twice.
    source=rig(1,1);target=rig(2,2);source->joints[0].bindPose.scale={.01f,.01f,.01f};for(auto f:{0,3})source->clips[0].frames[f].scale={.01f,.01f,.01f};
    source->clips[0].frames[4].translation.y=.5f;p=matchAnimationJoints(*source,*target);p.translationScale=200;p.joints[1].translation=RetargetTranslation::ScaledDelta;
    near(retargetAnimations(*source,*target,p)[0].frames[4].translation,{2,1,0});
}
void mappingAndValidation(){
    auto source=rig(1,1),target=rig(2,2);target->joints[0].name="Hip";target->joints[1].name="Elbow";target->joints[2].name="Wrist";
    auto p=matchAnimationJoints(*source,*target);check(p.joints.empty(),"No guessed semantic names");
    p.joints={{"Root","Hip",RetargetTranslation::ScaledDelta},{"Arm","Elbow"},{"Hand","Wrist"}};p.validate(*source,*target);
    const auto text=retargetProfileText(p);check(retargetProfileText(parseRetargetProfile(text))==text,"Named profile roundtrip");
    p.joints[2].source="Arm";rejects([&]{p.validate(*source,*target);});p=parseRetargetProfile(text);
    p.joints[2].source="Missing";rejects([&]{p.validate(*source,*target);});p=parseRetargetProfile(text);
    std::swap(p.joints[0].source,p.joints[1].source);rejects([&]{p.validate(*source,*target);});p=parseRetargetProfile(text);
    p.translationScale=std::numeric_limits<float>::quiet_NaN();rejects([&]{retargetAnimations(*source,*target,p);});p=parseRetargetProfile(text);
    auto bad=*source;bad.clips[0].frames[4].scale={2,2,2};rejects([&]{retargetAnimations(bad,*target,p);});
    bad=*source;bad.joints[1].bindPose.scale={1,2,1};rejects([&]{p.validate(bad,*target);});
    rejects([&]{parseRetargetProfile(text+" trailing");});rejects([&]{parseRetargetProfile("WERETARGET 2 1 0 0 0 1 0");});
    auto accessory=SkeletonJoint{"Accessory",2,{}};accessory.bindPose.translation={0,1,0};target->joints.push_back(accessory);target->clips.clear();
    auto clips=retargetAnimations(*source,*target,p);near(clips[0].frames[7].translation,{0,1,0});
    // A helper exists only in the source; its animation contributes through model-space delta.
    source=rig(1,1);source->joints[1].name="Helper";source->joints.insert(source->joints.begin()+2,{"Arm",1,{}});source->joints[3].parent=2;
    source->clips.clear();AnimationClip c;c.name="Helper movement";c.frameCount=2;c.duration=1;for(int f=0;f<2;++f)for(auto j:source->joints)c.frames.push_back(j.bindPose);
    c.frames[5].rotation={0,0,std::sqrt(.5f),std::sqrt(.5f)};source->clips.push_back(c);target=rig(2,2);p=matchAnimationJoints(*source,*target);clips=retargetAnimations(*source,*target,p);
    near(transformDirection(ikJointTransform(*target,std::span(clips[0].frames).subspan(3),1),{1,0,0}),{0,1,0});
    // Output expansion is bounded before allocation even with a small source clip.
    source=rig(1,1);source->clips[0].frameCount=4000;source->clips[0].frames.resize(12000);
    target=rig(2,2);target->clips.clear();while(target->joints.size()<256)target->joints.push_back({"Extra"+std::to_string(target->joints.size()),0,{}});
    p=matchAnimationJoints(*source,*target);rejects([&]{retargetAnimations(*source,*target,p);});
}
void integration(){
    auto source=retargetExampleMesh(false),target=retargetExampleMesh(true);auto base=AnimationLibrary::fromSkeleton(target->skeleton);
    rejects([&]{base->append(*source->skeleton);});auto p=matchAnimationJoints(*source->skeleton,*target->skeleton);p.translationScale=estimateRetargetScale(*source->skeleton,*target->skeleton,p);
    auto library=base->appendRetargeted(*source->skeleton,p);check(base->data().clips.size()==1&&library->data().clips.size()==3,"Bake appends without mutating base");
    check(library->data().clips[0].id==base->data().clips[0].id&&library->data().skeleton->bindings[3].inverseBind.m==target->skeleton->bindings[3].inverseBind.m,"Stable clip IDs and inverse binds");
    AnimationLibraryEditor history(base);history.edit(library);check(history.undo()&&history.library()==base&&history.redo()&&history.library()==library,"Bake is one library undo unit");
    auto loaded=AnimationLibraryArchive::load(AnimationLibraryArchive::save(*library));check(loaded->data().skeleton->clips[2].frames[10].matrix().m==library->data().skeleton->clips[2].frames[10].matrix().m,"Baked frames survive library archive");
    Scene scene;auto& mesh=scene.spawn("Character").addComponent<SkeletalMeshComponent>("Skin",target);EditorSession editor(scene);editor.select(mesh.savedId());const auto before=SceneArchive::save(scene);
    check(editor.setAnimationLibrary(loaded),"Apply retargeted library");const auto after=SceneArchive::save(scene);
    check(editor.undo()&&SceneArchive::save(scene)==before&&editor.redo()&&SceneArchive::save(scene)==after,"Scene apply preserves undo/redo");
    mesh.animation().play(2);mesh.animation().enableRootMotion(0);mesh.animation().update(.5);check(near(mesh.animation().rootMotionDelta().translation.x,.2f*p.translationScale)&&mesh.animation().events().size()==1,"Baked root motion and markers use normal playback");
    auto restored=SceneArchive::load(SceneArchive::save(scene));check(SceneArchive::save(*restored.scene)==SceneArchive::save(scene),"Scene roundtrip");
    Scene demo;populateRetargetExample(demo);demo.update(.5);check(demo.findSavedNode("retarget.target")&&demo.findSavedNode("retarget.source"),"Side-by-side demo builds");
}
}
int main(){try{transfer();mappingAndValidation();integration();std::cout<<"Animation retargeting passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
