#pragma once
#include "engine/mesh.hpp"
#include "engine/water.hpp"
#include "engine/terrain.hpp"
#include <span>

namespace engine {
struct TerrainMeshHoleSettings {
    Mat4 meshToTerrain=Mat4::identity(); // Mesh-local to heightfield-local metres.
    float contactTolerance=.05f;
    std::uint32_t maxBoundaryEdges=2048,maxCells=262144,maxWork=16000000;
    void validate() const;
};
struct TerrainMeshHoleResult {
    // Full row-major cell masks. holes preserves existing cuts; footprint is
    // mesh-owned only, so applications can replace old cuts without losing paint.
    std::vector<std::uint8_t> holes,footprint;
    std::uint32_t loops=0,footprintCells=0,newlyCutCells=0;
};
// Derive a conservative cell-resolution cut from closed mesh boundary loops on
// the UNCUT source heightfield. Uses base triangles, never presentation LOD.
// Attribute splits and collinear boundary T-junctions are reconciled. Projected
// loops use even/odd filling (concave outlines, disjoint openings and islands).
// Buried/closed meshes produce no cut. Partial contact rims, self-intersections,
// invalid static geometry/transforms or exceeded budgets reject atomically.
// No Boolean excavation, scene mutation or automatic collider replacement.
TerrainMeshHoleResult buildTerrainMeshHoleMask(const TerrainAsset&,const Mesh&,
    TerrainMeshHoleSettings={});
// Convenience immutable edit, preserving all existing holes. Publish through
// applyTerrainEdit/TerrainStream::apply at an idle scene boundary.
TerrainEdit cutTerrainHolesFromMesh(const TerrainAsset&,const Mesh&,TerrainMeshHoleSettings={});

// Mesh-local metres. Direction points out of the oriented surface into free
// space: up for a floor stalagmite, down for a ceiling stalactite. The anchor
// must lie on that surface. This is a local surface extrusion, not a Boolean.
struct TerrainMeshFormation {
    Vec3 anchor{},direction{0,1,0};
    float length=1,baseRadius=.5f;
    Vec3 bend{}; // Tangential tip offset, at most half the base radius.
    float irregularity=.08f; // 0..0.2; deterministic shape variation.
    std::uint32_t seed=0;
    void validate() const;
};
struct TerrainMeshFormationSettings {
    // Zero chooses baseRadius/5 separately for each formation.
    float maxEdgeLength=0;
    std::uint32_t maxOutputTriangles=30000,maxWork=8000000,lodLevels=3;
    void validate() const;
};
struct TerrainMeshFormationResult {
    std::shared_ptr<const Mesh> mesh;
    std::uint32_t formations=0,insertedVertices=0,outputTriangles=0;
};
// Refines shared edges near each anchor, inserts the exact tip sample, and
// smoothly extrudes the connected surface patch. Preserves material partitions,
// interpolates UVs, reconstructs affected normals/tangents, and derives LOD with
// inherited locks plus fixed tips. Base geometry owns collision and persistence.
// Up to 32 non-overlapping footprints; static, finite, oriented triangle meshes
// only, <=30k triangles/90k vertices/64 parts. Open edges and existing LOD locks
// must stay outside each footprint. Nearby disconnected sheets stay untouched.
// Input remains unchanged on failure. Caller publishes the returned ordinary
// Mesh and replaces its collider at an idle scene boundary. No scene ownership,
// random placement policy, volumetric union or collision-clearance guarantee.
TerrainMeshFormationResult buildTerrainMeshFormations(std::shared_ptr<const Mesh>,
    std::span<const TerrainMeshFormation>,TerrainMeshFormationSettings={});

// Convex, counterclockwise XZ shoreline in terrain mesh-local metres. floorAnchor
// selects the connected upward-facing floor, avoiding roofs/stacked chambers.
struct TerrainMeshLakeSettings {
    Vec3 floorAnchor{};
    std::vector<Vec2> outline;
    float waterLevel=-.08f,depth=.45f,bankWidth=.3f,maxEdgeLength=.12f;
    std::uint32_t maxOutputTriangles=30000,maxWaterTriangles=8192,maxWork=8000000,lodLevels=3;
    std::shared_ptr<const Material> waterMaterial;
    void validate()const;
};
struct TerrainMeshLakeResult {
    std::shared_ptr<const Mesh> terrainMesh,waterMesh;
    WaterRegion region; // Surface local Y=0, bottom=-depth.
    float waterLevel=0; // Translate the water surface/region by this mesh-local Y.
    std::uint32_t insertedVertices=0,outputTriangles=0;
};
// Immutable bounded floor preparation. Refines only the connected basin/bank,
// preserves material/UV partitions and inherited locks, locks the shoreline LOD
// band, and clips water geometry to the actual submerged bed. Static meshes only.
// Publish terrain+collider+water+region together at an idle boundary. The water
// is non-solid; lakebed collision, depth queries and gameplay remain caller-owned.
TerrainMeshLakeResult buildTerrainMeshLake(std::shared_ptr<const Mesh>,const TerrainMeshLakeSettings&);

} // namespace engine
