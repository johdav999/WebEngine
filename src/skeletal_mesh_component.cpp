#include "engine/skeletal_mesh_component.hpp"
#include <tuple>

namespace engine {
SkeletalMeshComponent::SkeletalMeshComponent(std::string name,std::shared_ptr<const Mesh> mesh):MeshComponent(std::move(name),std::move(mesh)) {
    skeleton_=&addChild<SkeletonComponent>();
    animation_=&skeleton_->addChild<AnimationComponent>("Animation",*skeleton_);synchronizeSkeleton();
    protectChild(*skeleton_);protectChild(*animation_);
}
void SkeletalMeshComponent::synchronizeSkeleton() const {
    skeleton_->setSkeleton(mesh ? mesh->skeleton : nullptr);
}
std::shared_ptr<const SkinPose> SkeletalMeshComponent::skinPose() const {
    synchronizeSkeleton();return skeleton_->skeleton() ? skeleton_->skinPose() : nullptr;
}
void SkeletalMeshComponent::swapAnimationState(SkeletalMeshComponent& other) noexcept {
    mesh.swap(other.mesh);
    auto skeletonState=[](SkeletonComponent& s){return std::tie(s.skeleton_,s.skinPose_,s.worlds_,s.pending_,s.bind_);};
    auto a=skeletonState(*skeleton_),b=skeletonState(*other.skeleton_);a.swap(b);
    animation_->swapPlayback(*other.animation_);
}
} // namespace engine
