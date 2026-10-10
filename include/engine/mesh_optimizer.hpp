#pragma once
#include "engine/mesh.hpp"
#include <cstddef>

namespace engine {
struct MeshOptimizationResult {
    std::size_t inputVertices=0,outputVertices=0;
};
class MeshOptimizer {
public:
    // Exact full-vertex welding, per-material triangle cache ordering, then
    // vertex fetch ordering. Preserves all attributes, triangles and winding.
    // Parts must partition the index buffer. Invalid input leaves mesh intact.
    static MeshOptimizationResult optimize(Mesh& mesh);
    // Generate shared static LODs once before publication. Preserves original
    // geometry and material slots. Rejects skin/morph assets; atomic on failure.
    static void generateLods(Mesh& mesh);
};
struct PackedMeshIndices {
    bool useUint16=false;
    // Padded to a multiple of four bytes for WebGPU buffer creation/write.
    std::vector<std::byte> bytes;
};
// Retains the public CPU uint32 index representation; packs only for upload.
PackedMeshIndices packMeshIndices(const Mesh& mesh);
} // namespace engine
