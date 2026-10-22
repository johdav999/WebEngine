#pragma once
#include "engine/animation_controller.hpp"
#include "engine/animation_retarget.hpp"
#include <memory>

namespace engine {
class SkeletalMeshComponent;
// Entries correspond to append-only skeleton clip slots. IDs survive renaming,
// sorting/filtering and additional imports; published assets must stay immutable.
struct AnimationClipEntry {
    std::string id,folder,tags;
    bool loop=true;
    double speed=1;
    std::string label;
};
struct AnimationLibraryData {
    std::string name="Animations";
    std::shared_ptr<const Skeleton> skeleton;
    std::vector<AnimationClipEntry> clips;
    AnimationControllerDefinition controller;
    std::vector<Vec2> positions;
    std::optional<std::size_t> rootMotionJoint;
};
class AnimationLibrary {
public:
    static std::shared_ptr<const AnimationLibrary> create(AnimationLibraryData);
    static std::shared_ptr<const AnimationLibrary> fromSkeleton(std::shared_ptr<const Skeleton>,
        std::shared_ptr<const AnimationControllerDefinition> controller={});
    const AnimationLibraryData& data() const { return data_; }
    // Unique names, matching hierarchy and local bind transforms required. Reorders
    // source joint samples into target order; does not retarget different bind poses.
    std::shared_ptr<const AnimationLibrary> append(const Skeleton& source) const;
    // Explicit bake into this library's rig. Graph, IK and clip metadata from the
    // source are not copied; destination controller references remain stable.
    std::shared_ptr<const AnimationLibrary> appendRetargeted(const Skeleton& source,const AnimationRetargetProfile&) const;
    std::shared_ptr<const AnimationControllerDefinition> controller() const { return controller_; }
    std::size_t clipIndex(std::string_view id) const;
    void validateTarget(const Skeleton&) const;
    // Configure a detached/preview mesh. EditorSession supplies scene history.
    void apply(SkeletalMeshComponent&) const;
private:
    explicit AnimationLibrary(AnimationLibraryData);
    AnimationLibraryData data_;
    std::shared_ptr<const AnimationControllerDefinition> controller_;
};
class AnimationLibraryArchive {
public:
    // Bounded authoring text, also used by browser forms. Parse before publication.
    static std::string metadata(const AnimationLibraryData&);
    static AnimationLibraryData parseMetadata(std::string_view,std::shared_ptr<const Skeleton>);
    // WEANIM1 envelope with checksummed scene archive carrying skeleton/controller.
    static std::vector<std::byte> save(const AnimationLibrary&);
    static std::shared_ptr<const AnimationLibrary> load(std::span<const std::byte>);
};
}
