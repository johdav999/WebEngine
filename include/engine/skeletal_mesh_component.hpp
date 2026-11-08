#pragma once
#include "engine/mesh_component.hpp"
#include "engine/animation_component.hpp"

namespace engine {
class SkeletalMeshComponent : public MeshComponent {
public:
    SkeletalMeshComponent(std::string name,std::shared_ptr<const Mesh> mesh=nullptr);
    std::string_view typeName() const noexcept override { return "SkeletalMeshComponent"; }
    SkeletonComponent& skeleton() { synchronizeSkeleton();return *skeleton_; }
    AnimationComponent& animation() { synchronizeSkeleton();return *animation_; }
    // Final mesh-space skin palette. Overrides must preserve this mesh's binding
    // table and retain the palette through collection/rendering (e.g. clothing).
    virtual std::shared_ptr<const SkinPose> skinPose() const;
    void synchronizeSkeleton() const;
private:
    friend class EditorSession;
    // Validated staged configuration exchange; node identity and target references stay put.
    void swapAnimationState(SkeletalMeshComponent&) noexcept;
    SkeletonComponent* skeleton_;
    AnimationComponent* animation_;
};
} // namespace engine
