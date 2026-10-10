#include "engine/skeleton_component.hpp"
#include <cstring>

namespace engine {
SkeletonComponent::SkeletonComponent(std::string name):Component(std::move(name)) {}
void SkeletonComponent::setSkeleton(std::shared_ptr<const Skeleton> skeleton) {
    if(skeleton==skeleton_) return;
    if(skeleton) skeleton->validate();
    std::vector<JointPose> bind;std::vector<Mat4> worlds;
    auto next=std::make_shared<SkinPose>();next->revision=1;
    if(skeleton) {
        for(const auto& joint:skeleton->joints) bind.push_back(joint.bindPose);
        worlds.resize(bind.size());next->bones.resize(skeleton->bindings.size());
        for(std::size_t i=0;i<bind.size();++i) {
            const auto parent=skeleton->joints[i].parent;
            worlds[i]=(parent<0 ? Mat4::identity() : worlds[parent])*bind[i].matrix();
        }
        for(std::size_t i=0;i<next->bones.size();++i) {
            const auto& binding=skeleton->bindings[i];const auto matrix=i ? worlds[binding.joint]*binding.inverseBind : Mat4::identity();
            next->bones[i]={matrix,transpose(inverseAffine(matrix))};
        }
    }
    auto pending=next->bones;
    skeleton_=std::move(skeleton);bind_.swap(bind);worlds_.swap(worlds);pending_.swap(pending);skinPose_=std::move(next);
}
void SkeletonComponent::resetPose() { if(skeleton_) setPose(bind_); }
void SkeletonComponent::setPose(std::span<const JointPose> poses) {
    if(!skeleton_ || poses.size()!=skeleton_->joints.size()) throw std::invalid_argument("Pose does not match skeleton");
    for(std::size_t i=0;i<poses.size();++i) {
        const auto parent=skeleton_->joints[i].parent;
        worlds_[i]=(parent<0 ? Mat4::identity() : worlds_[parent])*poses[i].matrix();
    }
    for(std::size_t i=0;i<skeleton_->bindings.size();++i) {
        const auto& binding=skeleton_->bindings[i];
        const auto matrix=i ? worlds_[binding.joint]*binding.inverseBind : Mat4::identity();
        pending_[i]={matrix,transpose(inverseAffine(matrix))};
    }
    bool changed=false;
    for(std::size_t i=0;i<pending_.size();++i) {
        const auto& value=pending_[i];
        auto& previous=skinPose_->bones[i];
        if(std::memcmp(&previous,&value,sizeof(value))) { previous=value;changed=true; }
    }
    skinPose_->revision+=changed;
}
} // namespace engine
