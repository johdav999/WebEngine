#pragma once
#include "engine/terrain.hpp"

namespace engine {
// Prepare an immutable mesh in mesh-local metres. The heightfield axes must align
// with the mesh axes; terrainOrigin locates its sample (0,0) in that same space.
// Only nearby boundaries between hole and solid cells are joined. This is not a
// Boolean intersection or a projection that closes arbitrary cave openings.
struct TerrainSeamSettings {
    Vec3 terrainOrigin{};
    float weldDistance=.25f,blendWidth=1.25f,maxEdgeLength=.75f;
    std::uint32_t maxBoundaryEdges=2048,maxOutputTriangles=30000,maxWork=8000000;
    std::uint32_t lodLevels=3; // 0 disables; up to 3 constrained derived levels.
    void validate() const;
};
struct TerrainSeamResult {
    std::shared_ptr<const Mesh> mesh;
    std::uint32_t boundaryEdges=0,weldedVertices=0,insertedSamples=0,outputTriangles=0;
};
// Full-source assets only. Source <=4096 triangles; output <=30000 triangles/90000
// vertices per level. Derived LOD locks blend vertices and open/material borders;
// <=64 parts when LOD is enabled. Input resources remain unchanged on rejection.
// Opaque PBR/unlit solid-color or unweighted single-layer triplanar materials only;
// albedo, roughness and packed layer normal/height maps use existing MaterialLayers.
// UVs become blend weights; conventional UV maps and painted four-layer inputs reject.
// Rebuild explicitly after source/mesh/material edits, then publish at an idle boundary.
TerrainSeamResult buildTerrainSeam(const TerrainAsset&,std::shared_ptr<const Mesh>,TerrainSeamSettings={});
} // namespace engine
