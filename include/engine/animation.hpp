#pragma once
#include "engine/math.hpp"
#include <span>
#include <string>
#include <vector>
#include <cstdint>

namespace engine {
struct Quaternion { float x=0,y=0,z=0,w=1; };
struct JointPose {
    Vec3 translation{},scale{1,1,1};Quaternion rotation;
    Mat4 matrix() const;
};
// Rigid movement in the skeletal mesh's local coordinates; scale stays in the pose.
struct RootMotionDelta {
    Vec3 translation{};Quaternion rotation;
    Mat4 matrix() const;
};
JointPose blendPose(const JointPose& a,const JointPose& b,float weight);
struct SkeletonJoint { std::string name;std::int32_t parent=-1;JointPose bindPose; };
struct SkinBinding { std::uint32_t joint=0;Mat4 inverseBind=Mat4::identity(); };
struct AnimationMarker {
    double time=0;std::string name,payload;
};
struct AnimationClip {
    std::string name;double duration=0;std::uint32_t frameCount=0;
    // Uniformly sampled local poses, including both endpoints, frame-major.
    std::vector<JointPose> frames;
    // Authored before publishing the skeleton; ordered by time, ties retain order.
    std::vector<AnimationMarker> markers{};
    void sample(double time,std::span<JointPose> output) const;
    JointPose sampleJoint(double time,std::size_t joint,std::size_t jointCount) const;
};
struct Skeleton {
    std::vector<SkeletonJoint> joints;
    // Slot zero is identity for vertices without skin weights.
    std::vector<SkinBinding> bindings;
    std::vector<AnimationClip> clips;
    void validate() const;
};
struct alignas(16) BoneUniform { Mat4 transform,normal; };
static_assert(sizeof(BoneUniform)==128);
struct SkinPose { std::vector<BoneUniform> bones;std::uint64_t revision=0; };
struct SkinVertex { std::array<std::uint32_t,4> joints{};std::array<float,4> weights{1,0,0,0}; };
static_assert(sizeof(SkinVertex)==32);
} // namespace engine
