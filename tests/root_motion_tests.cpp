#include "engine/animation_component.hpp"
#include "engine/actor.hpp"
#include "engine/scene.hpp"
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
bool countAllocations=false;std::size_t allocations=0;
void* allocate(std::size_t size) {
    if(countAllocations) ++allocations;
    if(auto* value=std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value,std::size_t) noexcept { std::free(value); }
void operator delete[](void* value,std::size_t) noexcept { std::free(value); }

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool near(const Mat4& a,const Mat4& b,float tolerance=0.0005f) {
    for(std::size_t i=0;i<16;++i) if(std::abs(a.m[i]-b.m[i])>tolerance) return false;
    return true;
}
template<class F> void rejects(F action,const char* message) {
    bool rejected=false;try { action(); } catch(const std::exception&) { rejected=true; }
    check(rejected,message);
}
Quaternion yaw(float radians) { return {0,std::sin(radians/2),0,std::cos(radians/2)}; }
Mat4 jointWorld(const Skeleton& skeleton,const AnimationClip* clip,double time,std::size_t selected) {
    std::vector<Mat4> world(skeleton.joints.size());
    for(std::size_t i=0;i<world.size();++i) {
        const auto pose=clip ? clip->sampleJoint(time,i,world.size()) : skeleton.joints[i].bindPose;
        const auto parent=skeleton.joints[i].parent;
        world[i]=(parent<0 ? Mat4::identity() : world[parent])*pose.matrix();
    }
    return world[selected];
}
std::shared_ptr<Skeleton> rig(bool ancestor=false) {
    auto asset=std::make_shared<Skeleton>();
    if(ancestor) asset->joints.push_back({"FBX ancestor",-1,{{7,2,-3},{2,2,2},yaw(pi/2)}});
    const auto root=asset->joints.size();
    asset->joints.push_back({"Locomotion",ancestor ? 0 : -1,{{3,0,0},{1,1,1},yaw(pi/4)}});
    asset->joints.push_back({"Limb",static_cast<std::int32_t>(root),{{0,1,0},{1,1,1},{}}});
    asset->bindings.push_back({});
    for(std::size_t i=0;i<asset->joints.size();++i) asset->bindings.push_back({static_cast<std::uint32_t>(i),inverseAffine(jointWorld(*asset,nullptr,0,i))});
    AnimationClip walk{"Turn and move",1,2,{}};
    for(int frame=0;frame<2;++frame) for(std::size_t i=0;i<asset->joints.size();++i) {
        auto pose=asset->joints[i].bindPose;
        if(i==root) { pose.translation.x+=2+2*frame;pose.rotation=yaw(pi/2+frame*pi/2); }
        if(i==root+1) { pose.translation.y+=frame;pose.scale={1+float(frame),1,1}; }
        walk.frames.push_back(pose);
    }
    asset->clips.push_back(walk);
    auto other=walk;other.name="Double stride";other.duration=2;
    for(std::size_t i=asset->joints.size();i<other.frames.size();++i) if(i%asset->joints.size()==root) other.frames[i].translation.x+=2;
    asset->clips.push_back(other);
    auto zero=walk;zero.name="Zero duration";zero.duration=0;zero.frameCount=1;zero.frames.resize(asset->joints.size());asset->clips.push_back(zero);
    asset->validate();return asset;
}
Mat4 motion(const Skeleton& asset,std::size_t clip,double time,std::size_t root=0) {
    // Uniform ancestor scale belongs to the skeleton, not extracted motion.
    auto world=jointWorld(asset,&asset.clips[clip],time,root),bind=jointWorld(asset,nullptr,0,root);
    const auto scale=root ? 2.0f : 1.0f;
    for(int column=0;column<3;++column) for(int row=0;row<3;++row) { world.m[column*4+row]/=scale;bind.m[column*4+row]/=scale; }
    return world*inverseAffine(bind);
}
void playback() {
    auto asset=rig();SkeletonComponent skeleton;skeleton.setSkeleton(asset);AnimationComponent player("Player",skeleton);
    player.play(0);player.update(0.5);
    check(near(skeleton.skinPose()->bones[1].transform,motion(*asset,0,0.5)),"Extraction is disabled by default");
    player.enableRootMotion();
    check(near(skeleton.skinPose()->bones[1].transform,Mat4::identity()),"Enabling mid-play anchors root to bind pose");
    check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Enabling does not emit teleport movement");
    player.play(0,false,2);player.update(0.25);
    const auto expected=inverseAffine(motion(*asset,0,0))*motion(*asset,0,0.5);
    check(near(player.rootMotionDelta().matrix(),expected),"Extract translation and rotation at playback speed with an offset first frame");
    check(near(skeleton.skinPose()->bones[1].transform,Mat4::identity()),"Extraction removes root movement from the uploaded skin palette");
    check(!near(skeleton.skinPose()->bones[2].transform,Mat4::identity()),"Limb translation and scale remain animated");
    check(near(player.consumeRootMotion().matrix(),expected) && near(player.consumeRootMotion().matrix(),Mat4::identity()),"Consumption resets pending movement");
    player.pause();player.update(1);check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Pause emits no motion");
    player.resume();player.update(4);
    check(player.state()==AnimationState::Paused && player.time()==1,"Non-loop root playback clamps");
    check(near(player.consumeRootMotion().matrix(),inverseAffine(motion(*asset,0,0.5))*motion(*asset,0,1)),"Non-loop overshoot extracts only to the endpoint");
    player.update(1);check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Held endpoint emits no further movement");
    player.play(0);player.update(0.1);player.seek(0.75);
    check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Seek clears pending movement without extracting a teleport");
    player.update(0.1);check(near(player.consumeRootMotion().matrix(),inverseAffine(motion(*asset,0,0.75))*motion(*asset,0,0.85)),"Playback rebases after seek");
    player.disableRootMotion();check(near(skeleton.skinPose()->bones[1].transform,motion(*asset,0,0.85)),"Disabling restores animated root pose");
    player.enableRootMotion();player.stop();check(near(player.consumeRootMotion().matrix(),Mat4::identity()) && near(skeleton.skinPose()->bones[2].transform,Mat4::identity()),"Stop clears pending motion and restores complete bind pose");
    player.play(2);player.update(10);check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Zero-duration clip emits no movement");
    rejects([&] { player.enableRootMotion(99); },"Invalid root index rejected");
    rejects([&] { player.update(-1); },"Invalid update rejected");
    player.play(0);player.seek(std::ldexp(1.0,64));
    rejects([&] { player.update(1); },"Unrepresentable cycle counts rejected");
    skeleton.setSkeleton(rig());player.update(0);
    check(!player.rootMotionEnabled() && player.state()==AnimationState::Stopped && near(player.consumeRootMotion().matrix(),Mat4::identity()),"Rig replacement clears extraction and pending movement");
}
void loops() {
    auto asset=rig();SkeletonComponent skeleton;skeleton.setSkeleton(asset);AnimationComponent player("Player",skeleton);
    player.enableRootMotion();player.play(0);
    const auto start=motion(*asset,0,0),end=motion(*asset,0,1),cycle=end*inverseAffine(start);
    player.update(1);
    check(near(player.consumeRootMotion().matrix(),inverseAffine(start)*end),"Exact loop boundary preserves endpoint movement");
    check(near(skeleton.skinPose()->bones[1].transform,Mat4::identity()),"Wrapped skin pose remains in place");
    player.play(0);player.update(0.75);player.update(2.5);
    const auto expected=inverseAffine(start)*cycle*cycle*cycle*motion(*asset,0,0.25);
    check(near(player.consumeRootMotion().matrix(),expected),"Accumulation composes turning strides through multiple loops");
    player.play(0);Mat4 composed=Mat4::identity();
    for(int i=0;i<13;++i) { player.update(0.25);composed=composed*player.consumeRootMotion().matrix(); }
    check(near(composed,expected),"Single large update and subdivided loop updates agree");
    // A straight clip makes float cancellation in absolute accumulated movement
    // observable: a short step must survive even after a billion completed loops.
    auto straight=rig();straight->clips[0].frames[2].rotation=straight->clips[0].frames[0].rotation;
    skeleton.setSkeleton(straight);player.play(0);player.enableRootMotion();player.seek(1000000000.25);player.update(0.125);
    check(near(player.consumeRootMotion().matrix(),inverseAffine(motion(*straight,0,0.25))*motion(*straight,0,0.375)),"Short strides retain precision after long playback");
}
void blending() {
    auto asset=rig();SkeletonComponent skeleton;skeleton.setSkeleton(asset);AnimationComponent player("Player",skeleton);player.enableRootMotion();
    player.blend(0,1,0);player.update(1.25);
    const auto start=motion(*asset,0,0),cycle=motion(*asset,0,1)*inverseAffine(start);
    check(near(player.consumeRootMotion().matrix(),inverseAffine(start)*cycle*motion(*asset,0,0.25)),"Blend zero extracts only A across its loop");
    player.blend(0,1,1);player.update(1.25);
    check(near(player.consumeRootMotion().matrix(),inverseAffine(motion(*asset,1,0))*motion(*asset,1,1.25)),"Blend one extracts B with independent duration");
    player.blend(0,1,0.5f);player.update(0.5);
    const auto a=inverseAffine(motion(*asset,0,0))*motion(*asset,0,0.5),b=inverseAffine(motion(*asset,1,0))*motion(*asset,1,0.5);
    Transform blended;blended.position={(a.m[12]+b.m[12])/2,(a.m[13]+b.m[13])/2,(a.m[14]+b.m[14])/2};blended.rotation.y=3*pi/16;
    check(near(player.rootMotionDelta().matrix(),blended.matrix()),"Blend combines translation and quaternion movement deltas");
    check(near(skeleton.skinPose()->bones[1].transform,Mat4::identity()),"Blended root pose is stripped");
    player.setBlendWeight(1);check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Weight changes rebase without emitting movement");
    player.crossFade(0,1);check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Crossfade starts without movement");
    player.update(0.5);
    auto targetDelta=inverseAffine(motion(*asset,0,0))*motion(*asset,0,0.5);
    Transform expected;expected.position={targetDelta.m[12]*0.25f,targetDelta.m[13]*0.25f,targetDelta.m[14]*0.25f};expected.rotation.y=pi/8*0.5f;
    check(near(player.consumeRootMotion().matrix(),expected.matrix()),"Crossfade integrates target weight from the frozen outgoing pose");
    player.crossFade(1,1);player.update(2);
    check(near(skeleton.skinPose()->bones[1].transform,Mat4::identity()),"Interrupted crossfade and fade overshoot keep the root in place");
}
void ancestorsAndScene() {
    auto asset=rig(true);Scene scene;auto& actor=scene.spawn<Actor>("Character");
    auto& skeleton=actor.addComponent<SkeletonComponent>();skeleton.setSkeleton(asset);
    auto& player=skeleton.addChild<AnimationComponent>("Player",skeleton);player.enableRootMotion(1);player.play(0);
    scene.update(0.5);const auto expected=inverseAffine(motion(*asset,0,0,1))*motion(*asset,0,0.5,1);
    check(near(player.consumeRootMotion().matrix(),expected),"Selected locomotion joint accounts for translated, rotated and scaled FBX ancestors");
    check(near(skeleton.skinPose()->bones[2].transform,Mat4::identity()),"Nested locomotion joint is anchored in mesh space");
    const auto before=skeleton.skinPose()->bones[3].transform;
    SkeletonComponent raw;raw.setSkeleton(asset);AnimationComponent unstripped("Raw",raw);unstripped.play(0);unstripped.seek(0.5);
    check(near(motion(*asset,0,0.5,1)*before,raw.skinPose()->bones[3].transform),"Extracted transform reconstructs the original limb pose under a scaled ancestor");
    actor.enabled=false;scene.update(2);check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Disabled scene subtree emits no movement");actor.enabled=true;
    scene.fixedUpdate(0.5);check(near(player.consumeRootMotion().matrix(),Mat4::identity()),"Fixed updates do not extract animation movement");
    scene.update(0.01);player.consumeRootMotion();allocations=0;countAllocations=true;
    for(int i=0;i<1000;++i) { scene.update(0.01);player.consumeRootMotion(); }
    countAllocations=false;check(allocations==0,"Warm root motion updates allocate no heap blocks");
}
}
int main() {
    try { playback();loops();blending();ancestorsAndScene();std::cout<<"Root motion tests passed\n"; }
    catch(const std::exception& error) { countAllocations=false;std::cerr<<error.what()<<'\n';return 1; }
}
