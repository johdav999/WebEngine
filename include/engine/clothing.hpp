#pragma once
#include "engine/skeletal_mesh_component.hpp"
#include "engine/scene.hpp"
#include "engine/cloth.hpp"
#include <span>

namespace engine {
// Assets use the exact shared rig, binding table and mesh bind space of the driver.
// A rigid accessory is a mesh with every vertex weighted to one binding.
struct ClothingBodyRegion {
    std::string id;
    std::shared_ptr<const Mesh> mesh;
    std::uint64_t regions=0;
    Vec3 color{1,1,1};
};
struct ClothingItem {
    std::string id,slot;
    std::shared_ptr<const Mesh> mesh;
    std::uint64_t hidesRegions=0;
    std::shared_ptr<const ClothDefinition> cloth; // Optional physical, skinned-target cloth.
};
struct ClothingCatalog {
    std::shared_ptr<const Skeleton> skeleton;
    std::vector<ClothingBodyRegion> body;
    std::vector<ClothingItem> items;
    void validate() const;
};
struct ClothingChoice {
    std::string item;
    Vec3 color{1,1,1};
};
using ClothingSelection=std::vector<ClothingChoice>;
// Bounded, versioned text encoding; no paths, scene handles or asset bytes.
std::string saveClothingSelection(std::span<const ClothingChoice>);
ClothingSelection loadClothingSelection(std::string_view);

// Application-owned projection. Call apply/clear at an idle hierarchy boundary,
// and synchronize in ApplicationFrameCallbacks::present, including paused frames.
// The scene owns generated children; destruction of this controller does not remove them.
// Destroying the driver destroys the children. Explicit clear removes them earlier.
class ClothingInstance {
public:
    ClothingInstance(ComponentId driver,ClothingCatalog catalog);
    ClothingInstance(const ClothingInstance&)=delete;
    ClothingInstance& operator=(const ClothingInstance&)=delete;
    void apply(Scene&,ClothingSelection); // Staged; invalid selections retain the outfit.
    bool synchronize(Scene&); // False hides the projection if driver/rig/root was replaced.
    // Call once after simulation with its delta (zero while paused), never from rendering.
    bool advanceCloth(Scene&,double delta,std::span<const ClothCapsule> worldCapsules={},Vec3 acceleration={0,-9.81f,0},ClothEnvironment environment={});
    bool resetCloth(Scene&); // Rebind to current animated targets after teleport/seek/load.
    ClothStats clothStats(const Scene&) const;
    void clear(Scene&);
    const ClothingSelection& selection() const { return selection_; }
    ComponentId root() const { return root_; }
private:
    ComponentId driver_,root_;
    ClothingCatalog catalog_;
    ClothingSelection selection_;
};
} // namespace engine
