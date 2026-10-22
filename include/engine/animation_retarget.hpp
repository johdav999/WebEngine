#pragma once
#include "engine/animation.hpp"
#include <string_view>

namespace engine {
enum class RetargetTranslation { Bind,ScaledDelta };
struct AnimationRetargetJoint {
    std::string source,target;
    RetargetTranslation translation=RetargetTranslation::Bind;
};
// Named, one-to-one correspondence. Unmapped destination joints retain local bind
// poses. Nearest mapped ancestors must correspond; unmapped helpers are allowed.
struct AnimationRetargetProfile {
    std::vector<AnimationRetargetJoint> joints;
    Quaternion modelRotation{}; // Source model axes -> destination model axes.
    float translationScale=1;
    void validate(const Skeleton& source,const Skeleton& target) const;
};
// Exact-name suggestions only; may be empty/incomplete and require author edits.
AnimationRetargetProfile matchAnimationJoints(const Skeleton& source,const Skeleton& target);
// Median nonzero mapped-segment length ratio, in model coordinates; 1 if none.
float estimateRetargetScale(const Skeleton& source,const Skeleton& target,const AnimationRetargetProfile&);
// Offline/owner-thread bake. Preserves clip names, samples, times and markers.
// Fixed positive uniform scales only. Bounds: 64 clips, 256 joints, 32 MiB poses.
// No input mutation, retained pointers, callbacks or per-frame runtime dependency.
std::vector<AnimationClip> retargetAnimations(const Skeleton& source,const Skeleton& target,const AnimationRetargetProfile&);
// Bounded portable named profile format, independent of scene/library versions.
std::string retargetProfileText(const AnimationRetargetProfile&);
AnimationRetargetProfile parseRetargetProfile(std::string_view);
}
