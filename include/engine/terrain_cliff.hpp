#pragma once
#include "engine/terrain.hpp"
#include <span>

namespace engine {
struct TerrainCliffSettings {
    // Terrain-local elevations. The left side of the directed X/Z path is high.
    float upperHeight=5,lowerHeight=0,faceAngle=85;
    float shoulderWidth=1.25f,endFade=.75f,blendWidth=.75f,maxEdgeLength=.5f;
    float irregularity=.08f;
    std::uint32_t seed=17,lodLevels=3,maxCells=262144,maxOutputTriangles=30000,maxWork=8000000;
    void validate() const;
};
struct TerrainCliffResult {
    TerrainEdit terrain;
    std::shared_ptr<const Mesh> mesh;
    // Mesh-owned cut only; preserve separately from painted/cave hole ownership.
    std::vector<std::uint8_t> footprint;
    std::uint32_t cutX=0,cutZ=0,cellsX=0,cellsZ=0,outputTriangles=0;
};
// Bounded immutable heightfield-to-mesh replacement along an open path (2..16
// points, gentle bends, no crossings/near folding approaches). Terrain-local metres;
// publish both resources with the same translation and unit physical scale.
// The rectangular grid-aligned collar retains every original perimeter edge,
// height and normal. Interior shared-edge refinement introduces no T-junctions.
// Lips/toes follow the requested steep face; both ends fade into source ground.
// Existing holes/heterogeneous physical surfaces inside the cut reject; unrelated
// holes remain intact. Opaque solid or single-layer triplanar materials only.
// Full-source assets only, <=4096 cut cells, <=30k triangles/90k vertices.
// No scene mutation, automatic subscriptions, overhang or arbitrary mesh Boolean.
// Caller stages full-base collision, then publishes mesh and terrain edit together
// at an idle scene boundary. Base geometry and LOD locks use ordinary archives;
// editable path/settings and footprint ownership remain application metadata.
TerrainCliffResult buildTerrainMeshCliff(const TerrainAsset&,std::span<const Vec2> path,
    std::shared_ptr<const Material> rock,TerrainCliffSettings={});
} // namespace engine
