#pragma once
#include "engine/navmesh.hpp"
namespace engine {
struct DynamicNavigationSettings {
    NavMeshBuildSettings build;
    PhysicsQueryFilter geometry;
    double rebuildInterval=.25;
    float movementThreshold=.02f;
    void validate() const;
};
struct DynamicNavigationStats {std::size_t obstacles=0;std::uint64_t builds=0,failures=0;bool dirty=true;std::string error;};
// Application-owned owner-thread service. prepare() is explicit idle work, before
// agent fixed ticks. Tracks handles/AABBs, not borrowed scene/body pointers.
class DynamicNavigation {
public:
    explicit DynamicNavigation(DynamicNavigationSettings settings={});
    void track(Scene&,ComponentId body); // Registered solid rigid body, max64.
    bool untrack(ComponentId); // Call before destroying/mutating the removed body.
    void invalidate(); // Before changing untracked static support/topology.
    bool prepare(Scene&,double simulationSeconds); // Synchronizes; <=1 whole bake/call.
    void retry(); // Failed builds stay dirty without automatic retry storms.
    std::shared_ptr<const NavigationSource> source() const {return source_;}
    const DynamicNavigationStats& stats() const {return stats_;}
private:
    struct Entry {ComponentId body;std::optional<PhysicsBounds> bounds;};
    DynamicNavigationSettings settings_;
    std::shared_ptr<NavigationSource> source_=std::make_shared<NavigationSource>();
    std::vector<Entry> entries_;
    std::uint64_t scene_=0;
    double remaining_=0;
    bool failed_=false;
    DynamicNavigationStats stats_;
};
}
