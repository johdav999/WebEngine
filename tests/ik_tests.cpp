#include "engine/animation_component.hpp"
#include "engine/editor.hpp"
#include "engine/editor_animation.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "../src/ik_example.hpp"
#include <iostream>
#include <limits>
using namespace engine;
namespace {
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
bool near(float a,float b){return std::abs(a-b)<.0002f;}
void near(Vec3 a,Vec3 b){check(near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z),"Unexpected IK position");}
template<class F>void rejects(F f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw,"Expected rejection");}
std::shared_ptr<Skeleton> rig(){
    auto s=std::make_shared<Skeleton>();s->joints={{"Root",-1,{}},{"Shoulder",0,{}},{"Elbow",1,{}},{"Hand",2,{}},{"Head",0,{}}};
    s->joints[1].bindPose.translation={0,1,0};s->joints[2].bindPose.translation={1,0,0};s->joints[3].bindPose.translation={1,0,0};s->joints[4].bindPose.translation={0,2,0};
    s->bindings={{},{0,Mat4::identity()},{1,Mat4::identity()},{2,Mat4::identity()},{3,Mat4::identity()},{4,Mat4::identity()}};
    AnimationClip clip;clip.name="Idle";clip.duration=1;clip.frameCount=2;
    for(int f=0;f<2;++f)for(auto& j:s->joints)clip.frames.push_back(j.bindPose);
    clip.frames[5].translation.x=1;clip.markers={{.25,"Step",""}};s->clips.push_back(clip);s->validate();return s;
}
std::vector<JointPose> bind(const Skeleton& s){std::vector<JointPose> p;for(auto& j:s.joints)p.push_back(j.bindPose);return p;}
Vec3 position(const Skeleton& s,std::span<const JointPose> p,std::size_t i){return transformPoint(ikJointTransform(s,p,i),{});}
std::shared_ptr<AnimationControllerDefinition> graph(){
    auto d=std::make_shared<AnimationControllerDefinition>();d->states={{"Idle",0,true,1,{}}};
    IkConstraint c;c.name="Reach";c.root=1;c.middle=2;c.tip=3;c.target.position={1,2,0};c.target.pole={0,3,0};c.orientTip=true;d->ik={c};
    c={};c.name="Look";c.mode=IkMode::LookAt;c.root=4;c.target.position={1,2,1};c.maxAngle=pi/3;d->ik.push_back(c);return d;
}
void solver(){
    const auto s=rig();const auto input=bind(*s);auto output=input;auto g=graph();std::vector<IkTarget> targets;for(auto& c:g->ik)targets.push_back(c.target);
    auto r=solveIk(*s,input,g->ik,targets,output);near(position(*s,output,3),targets[0].position);
    near(position(*s,output,1),{0,1,0});check(near(r.constraints[0].error,0),"Reach diagnostic");
    near(transformDirection(ikJointTransform(*s,output,3),{0,0,1}),{0,0,1});
    auto forward=transformDirection(ikJointTransform(*s,output,4),{0,0,1});check(near(forward.x,std::sqrt(.5f)),"Head faces target");
    targets[0].position={10,1,0};r=solveIk(*s,input,g->ik,targets,output);near(position(*s,output,3),{2,1,0});check(r.constraints[0].limited&&near(r.constraints[0].error,8),"Unreachable clamps without stretching");
    targets[0].position={0,1,0};targets[0].pole={0,1,0};solveIk(*s,input,g->ik,targets,output);check(std::sqrt(dot(position(*s,output,3)-Vec3{0,1,0},position(*s,output,3)-Vec3{0,1,0}))<.001f,"Coincident target/pole deterministic");
    targets[0]=g->ik[0].target;targets[0].weight=0;targets[1].enabled=false;solveIk(*s,input,g->ik,targets,output);near(position(*s,output,3),{2,1,0});
    targets[0].weight=.5f;solveIk(*s,input,g->ik,targets,output);const auto partial=position(*s,output,3);check(partial.y>1&&partial.x>1,"Partial IK blends with animation");
    targets[1].enabled=true;targets[1].position={0,2,-1};g->ik[1].maxAngle=pi/4;solveIk(*s,input,g->ik,targets,output);
    forward=transformDirection(ikJointTransform(*s,output,4),{0,0,1});check(near(forward.z,std::sqrt(.5f)),"Antiparallel look respects angle bound");
    targets[0].weight=1;const auto before=output;targets[0].position.x=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{solveIk(*s,input,g->ik,targets,output);});check(output[1].rotation.x==before[1].rotation.x&&output[1].rotation.z==before[1].rotation.z,"Invalid target is atomic");
    targets[0]=g->ik[0].target;auto distorted=input;distorted[1].scale={2,1,1};rejects([&]{solveIk(*s,distorted,g->ik,targets,output);});
    auto degenerate=input;degenerate[2].translation={};rejects([&]{solveIk(*s,degenerate,g->ik,targets,output);});
    g->ik[0].middle=4;rejects([&]{validateIk(*s,g->ik);});g=graph();rejects([&]{validateIkRootMotion(*s,g->ik,2);});validateIkRootMotion(*s,g->ik,0);
    // Parent rotation and scale must not change model-space targeting.
    auto transformed=input;transformed[0].rotation={0,0,std::sqrt(.5f),std::sqrt(.5f)};transformed[0].scale={2,2,2};
    targets[0].position={-3,1,0};targets[0].pole={-4,2,0};solveIk(*s,transformed,g->ik,targets,output);near(position(*s,output,3),targets[0].position);
    auto placement=transformed[0].matrix();placement.m[12]=3;IkTarget world;world.position=transformPoint(placement,{1,2,3});world.pole=transformPoint(placement,{2,3,4});auto local=ikTargetFromWorld(placement,world);near(local.position,{1,2,3});near(local.pole,{2,3,4});
    auto incoming=ikJointTarget(*s,transformed,3);near(incoming.position,position(*s,transformed,3));
    near(transformDirection(JointPose{{},{1,1,1},incoming.rotation}.matrix(),{1,0,0}),{0,1,0});
    placement.m[3]=1;rejects([&]{ikTargetFromWorld(placement,world);});
    // Exercise both the inner and outer reach boundaries in several directions.
    auto unequal=input;unequal[3].translation={.5f,0,0};
    for(auto target:{Vec3{0,1,0},Vec3{.25f,1,0},Vec3{-1,1,.5f},Vec3{0,1,4},Vec3{0,0,0}}){
        targets[0].position=target;solveIk(*s,unequal,g->ik,targets,output);
        const auto delta=target-Vec3{0,1,0};const auto distance=std::sqrt(dot(delta,delta));const auto end=position(*s,output,3)-Vec3{0,1,0};
        check(near(std::sqrt(dot(end,end)),std::clamp(distance,.5f,1.5f)),"Limb preserves inner/outer reach");
    }
}
void playbackAndArchive(){
    auto geometry=std::make_shared<Mesh>(*Mesh::cube());geometry->skeleton=rig();Scene scene("IK");auto& mesh=scene.spawn("Character").addComponent<SkeletalMeshComponent>("Skin",geometry);auto& p=mesh.animation();auto d=graph();p.setController(d);p.enableRootMotion();scene.update(.3);
    check(near(p.rootMotionDelta().translation.x,.3f)&&p.events().size()==1,"IK does not alter animation outputs");
    auto target=p.ikTargets()[0];target.position={.5f,2,0};p.pause();p.setIkTarget("Reach",target);
    near(transformPoint(mesh.skeleton().skinPose()->bones[4].transform,{}),target.position);
    check(near(float(p.time()),.3f)&&p.events().size()==1&&near(p.rootMotionDelta().translation.x,.3f),"Target update preserves clocks and queued outputs");
    near(position(*geometry->skeleton,p.poseBeforeIk(),3),{2,1,0});
    check(d->ik[0].target.position.x==1,"Live target does not mutate controller");
    auto invalid=std::make_shared<AnimationControllerDefinition>(*d);invalid->ik[0].root=0;invalid->ik[0].middle=1;invalid->ik[0].tip=2;
    rejects([&]{p.setController(invalid);});check(p.controller()==d&&near(float(p.time()),.3f),"Invalid controller preserves playback");
    auto bad=target;bad.weight=-1;rejects([&]{p.setIkTarget("Reach",bad);});check(p.ikTargets()[0].weight==1,"Target rejection preserves previous value");
    const auto bytes=SceneArchive::save(scene);auto restored=SceneArchive::load(bytes);check(SceneArchive::save(*restored.scene)==bytes,"Scene IK roundtrip exact");
    auto* copy=dynamic_cast<SkeletalMeshComponent*>(restored.scene->findSavedNode(mesh.savedId()));check(copy&&near(copy->animation().ikTargets()[0].position.x,.5f),"Live targets restored");
    p.resume();copy->animation().resume();scene.update(.1);restored.scene->update(.1);near(transformPoint(copy->skeleton().skinPose()->bones[4].transform,{}),target.position);
    auto library=AnimationLibrary::fromSkeleton(geometry->skeleton,d);auto data=library->data();data.rootMotionJoint=0;library=AnimationLibrary::create(data);
    auto loaded=AnimationLibraryArchive::load(AnimationLibraryArchive::save(*library));check(AnimationLibraryArchive::metadata(loaded->data())==AnimationLibraryArchive::metadata(data),"Library retains IK");
    auto reordered=std::make_shared<Skeleton>(*geometry->skeleton);const std::array<std::size_t,5> order={0,4,1,2,3};
    const auto map=[&](std::size_t source){return std::size_t(std::find(order.begin(),order.end(),source)-order.begin());};
    for(std::size_t j=0;j<5;++j){reordered->joints[j]=geometry->skeleton->joints[order[j]];if(reordered->joints[j].parent>=0)reordered->joints[j].parent=std::int32_t(map(reordered->joints[j].parent));}
    for(auto& b:reordered->bindings)b.joint=std::uint32_t(map(b.joint));
    for(std::size_t f=0;f<2;++f)for(std::size_t j=0;j<5;++j)reordered->clips[0].frames[f*5+j]=geometry->skeleton->clips[0].frames[f*5+order[j]];
    auto otherGeometry=std::make_shared<Mesh>(*geometry);otherGeometry->skeleton=reordered;SkeletalMeshComponent other("Reordered",otherGeometry);loaded->apply(other);
    check(other.animation().controller()->ik[0].root==2&&other.animation().controller()->ik[0].tip==4&&other.animation().controller()->ik[1].root==1,"Library remaps all IK joints by identity");
    auto noIk=AnimationLibrary::fromSkeleton(geometry->skeleton);auto legacy=AnimationLibraryArchive::metadata(noIk->data());legacy.replace(5,1,"2");legacy.resize(legacy.size()-2);
    check(AnimationLibrary::create(AnimationLibraryArchive::parseMetadata(legacy,geometry->skeleton))->controller()->ik.empty(),"ANIM2 defaults IK off");
    EditorSession editor(scene);editor.select(mesh.savedId());const auto before=SceneArchive::save(scene);check(editor.setAnimationLibrary(loaded),"Editor apply IK");const auto applied=SceneArchive::save(scene);check(editor.undo()&&SceneArchive::save(scene)==before,"Undo restores live targets");check(editor.redo()&&SceneArchive::save(scene)==applied,"Redo restores IK");
    p.update(.4);const auto outgoing=transformPoint(mesh.skeleton().skinPose()->bones[4].transform,{});
    p.crossFade(0,.2);check(!p.controller()&&p.ikTargets().empty(),"Manual playback clears IK");
    near(transformPoint(mesh.skeleton().skinPose()->bones[4].transform,{}),outgoing);scene.update(.1);
    p.setController(d);mesh.skeleton().setSkeleton(rig());p.update(0);check(!p.controller()&&p.ikTargets().empty(),"Skeleton replacement cancels IK");
}
void ground(){
    Scene scene;auto& floor=scene.spawn("Slope");floor.transform.position={0,-.5f,0};floor.transform.rotation.z=.2f;
    RigidBodySettings settings;settings.motion=BodyMotion::Static;floor.addComponent<RigidBodyComponent>("Body",settings);floor.addComponent<ColliderComponent>("Shape",CollisionShape::box({5,.5f,5}));scene.update(0);
    IkTarget foot;foot.position={0,.2f,0};IkGroundProbe probe;probe.soleOffset=.04f;PhysicsQueryFilter filter;filter.includeSensors=false;
    auto hit=groundIkTarget(scene.physics(),Mat4::identity(),foot,probe,filter);check(hit.has_value(),"Ground query finds slope");
    const auto normal=transformDirection(JointPose{{},{1,1,1},hit->rotation}.matrix(),{0,1,0});check(normal.x<-.1f&&normal.y>.9f,"Sole aligns with surface normal");
    probe.maxSlope=.1f;check(!groundIkTarget(scene.physics(),Mat4::identity(),foot,probe,filter),"Steep hit rejected");
    probe.maxSlope=1;filter.excludedActors.push_back(floor.id());check(!groundIkTarget(scene.physics(),Mat4::identity(),foot,probe,filter),"Owner/body filtering honored");
    Scene demo;auto& skin=populateIkExample(demo);check(skin.animation().ikTargets()[1].position.y>skin.animation().ikTargets()[0].position.y+.2f,"Demo pads have distinct ground targets");
    demo.update(.3);for(std::size_t i=0;i<3;++i)check(skin.animation().ikResult().constraints[i].error<.001f,"Demo limbs reach contacts while shifting weight");
    const auto bytes=SceneArchive::save(demo);auto loaded=SceneArchive::load(bytes);check(SceneArchive::save(*loaded.scene)==bytes,"Demo uses supported archive components");
}
}
int main(){try{solver();playbackAndArchive();ground();std::cout<<"IK solver, playback, archives, history and grounding passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
