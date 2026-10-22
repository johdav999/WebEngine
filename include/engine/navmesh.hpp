#pragma once
#include "engine/ai.hpp"
#include "engine/mesh.hpp"

namespace engine {
struct NavMeshBuildSettings {
    float cellSize=.2f,cellHeight=.1f;
    float agentRadius=.35f,agentHeight=1.9f,maxSlopeAngle=pi/4,maxStepHeight=.3f;
    float endpointSnap=.5f,verticalSnap=.5f;
    std::size_t maxCells=262144,maxRasterWork=2000000,maxPolygons=4096;
    void validate() const;
};
struct NavMeshStats { std::size_t inputTriangles=0,gridCells=0,polygons=0,vertices=0,dataBytes=0; };
struct NavMeshLocation { Vec3 position;std::uint32_t polygon=0; };
struct NavMeshObstacle { Vec3 minimum,maximum; }; // Owned world-space solid footprint.
// Immutable static single-tile navigation resource. Recast/Detour types and
// mutable per-request query work remain private. No scene/motor is retained.
class NavMesh final:public NavigationSurface {
public:
    static std::shared_ptr<const NavMesh> bake(const PhysicsGeometry&,NavMeshBuildSettings settings={});
    // Snapshot registered static solid bodies only; synchronize the scene first.
    // Absent physics or empty/over-budget/invalid input rejects atomically.
    static std::shared_ptr<const NavMesh> bake(const Scene&,NavMeshBuildSettings settings={},PhysicsQueryFilter filter={});
    // Carve up to 64 world AABBs from otherwise static collision. Sensor/agent
    // policy and tracked-body exclusions belong to the caller, not the mesh.
    static std::shared_ptr<const NavMesh> bakeWithObstacles(const Scene&,std::span<const NavMeshObstacle>,NavMeshBuildSettings settings={},PhysicsQueryFilter filter={});
    ~NavMesh() override;
    std::optional<NavMeshLocation> project(Vec3 point,float horizontalRange=1) const;
    bool canTraverseSegment(Vec3 from,Vec3 to) const; // Local walkability ray; <=256 polygons.
    std::optional<Vec3> raycast(Vec3 origin,Vec3 direction,float maxDistance=1000) const;
    WaypointPath findPath(Vec3 start,Vec3 goal,const WaypointSearchSettings& settings) const override;
    bool compatible(const CharacterMotorSettings&) const noexcept override;
    bool requiresProjection() const noexcept override { return true; }
    const NavMeshBuildSettings& settings() const { return settings_; }
    const NavMeshStats& stats() const { return stats_; }
    const std::shared_ptr<const Mesh>& debugMesh() const { return debugMesh_; }
private:
    static std::shared_ptr<const NavMesh> bakeGeometry(const PhysicsGeometry&,NavMeshBuildSettings,const std::function<bool(Vec3)>&);
    NavMesh();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    NavMeshBuildSettings settings_;
    NavMeshStats stats_;
    std::shared_ptr<const Mesh> debugMesh_;
};
} // namespace engine
