#pragma once
#include "engine/navmesh.hpp"
namespace engine {
struct CrowdSettings {
    NavigationSettings navigation;
    std::size_t maxAgents=32,maxNeighbors=12,obstacleTests=8;
    float neighborDistance=4,timeHorizon=1.5f,separation=.08f;
    CrowdSettings();
    void validate() const;
};
struct CrowdAgentState {
    ComponentId motor;NavigationStatus status=NavigationStatus::Idle;
    Vec3 position{},preferred{},velocity{};
    std::size_t neighbors=0;bool yielding=false;
};
struct CrowdStats {std::size_t agents=0,neighbors=0,candidates=0,obstacleQueries=0,safetyStops=0;};
// One owner-thread producer for registered motors. Snapshot/plan/avoid/submit all
// agents before any motor fixed ticks. Retains only handles and owned navigation.
class CrowdNavigation {
public:
    explicit CrowdNavigation(CrowdSettings settings={});
    void add(CharacterMotorComponent&,std::shared_ptr<const NavigationSource>,Vec3 goal);
    void setGoal(ComponentId,Vec3);
    bool remove(Scene&,ComponentId);
    void clear(Scene&) noexcept; // Explicit handoff/disable cleanup before destruction.
    void step(Scene&,double fixedSeconds);
    std::vector<CrowdAgentState> snapshot() const;
    const CrowdStats& stats() const {return stats_;}
    const CrowdSettings& settings() const {return settings_;}
private:
    struct Entry {
        ComponentId motor;std::shared_ptr<const NavigationSource> source;
        Navigator navigator;CrowdAgentState state;
        std::shared_ptr<const CollisionShape> probe;
        float probeRadius=0,probeHalfHeight=0;
    };
    CrowdSettings settings_;std::vector<Entry> entries_;std::uint64_t scene_=0;
    CrowdStats stats_;
};
}
