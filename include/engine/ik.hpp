#pragma once
#include "engine/animation.hpp"
#include "engine/physics.hpp"

namespace engine {
enum class IkMode { TwoBone,LookAt };
// All positions/orientations are in skeletal model space, not actor/world space.
struct IkTarget {
    Vec3 position{},pole{0,0,1};
    Quaternion rotation{};
    float weight=1;
    bool enabled=true;
    void validate() const;
};
struct IkConstraint {
    std::string name;
    IkMode mode=IkMode::TwoBone;
    std::size_t root=0,middle=0,tip=0;
    float weight=1;
    bool orientTip=false;
    Vec3 forward{0,0,1};
    float maxAngle=3.14159265f; // Look-at swing from the incoming animated pose.
    IkTarget target{};        // Instance defaults, copied on controller installation.
};
struct IkConstraintResult {
    float error=0;
    bool limited=false;
};
struct IkResult {
    std::array<IkConstraintResult,32> constraints{};
    std::size_t count=0;
};
void validateIk(const Skeleton&,std::span<const IkConstraint>);
void validateIkRootMotion(const Skeleton&,std::span<const IkConstraint>,std::size_t rootMotionJoint);
// Deterministic ordered local-pose modifiers. No frame history, stretching or IO.
// Supports positive uniform local scales. Output may alias input; failure leaves it intact.
IkResult solveIk(const Skeleton&,std::span<const JointPose> input,
    std::span<const IkConstraint>,std::span<const IkTarget>,std::span<JointPose> output);
Mat4 ikJointTransform(const Skeleton&,std::span<const JointPose>,std::size_t joint);
IkTarget ikJointTarget(const Skeleton&,std::span<const JointPose>,std::size_t joint);
IkTarget ikTargetFromWorld(const Mat4& modelToWorld,const IkTarget& worldTarget);

struct IkGroundProbe {
    Vec3 worldUp{0,1,0},soleUp{0,1,0};
    float above=.5f,below=.8f,soleOffset=.03f,maxSlope=1.04719755f;
};
// Borrows physics only during the call. Queries existing collision, excludes no owner
// implicitly. A miss/steep surface returns nullopt; gameplay decides target release.
std::optional<IkTarget> groundIkTarget(const PhysicsWorld&,const Mat4& modelToWorld,
    const IkTarget& animatedFoot,const IkGroundProbe& ={},const PhysicsQueryFilter& =PhysicsQueryFilter{.includeSensors=false,.excludedActors={},.excludedBodies={}});
}
