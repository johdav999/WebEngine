#pragma once
#include "engine/component.hpp"
#include "engine/animation.hpp"
#include <memory>

namespace engine {
class SkeletonComponent : public Component {
public:
    explicit SkeletonComponent(std::string name="Skeleton");
    std::string_view typeName() const noexcept override { return "SkeletonComponent"; }
    void setSkeleton(std::shared_ptr<const Skeleton> skeleton);
    const auto& skeleton() const { return skeleton_; }
    std::shared_ptr<const SkinPose> skinPose() const { return skinPose_; }
    void setPose(std::span<const JointPose> poses);
    void resetPose();
private:
    friend class ArchiveCodec;
    std::shared_ptr<const Skeleton> skeleton_;
    std::shared_ptr<SkinPose> skinPose_=std::make_shared<SkinPose>();
    std::vector<Mat4> worlds_;
    std::vector<BoneUniform> pending_;
    std::vector<JointPose> bind_;
};
} // namespace engine
