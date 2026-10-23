#pragma once
#include "engine/mesh.hpp"
#include <cstddef>
#include <span>

namespace engine {
struct MeshOptimizationResult {
    std::size_t inputVertices=0,outputVertices=0;
};
struct MeshLodConstraints {
    bool lockBorders=false;
    // Empty, or one 0/1 byte per source vertex. Locked vertices retain position
    // and attributes. Borrowed only during synchronous generation.
    std::span<const std::uint8_t> lockedVertices{};
};
class MeshOptimizer {
public:
    // Exact full-vertex welding, per-material triangle cache ordering, then
    // vertex fetch ordering. Preserves all attributes, triangles and winding.
    // Parts must partition the index buffer. Invalid input leaves mesh intact.
    static MeshOptimizationResult optimize(Mesh& mesh);
    // Generate shared static LODs once before publication. Preserves original
    // geometry and material slots. Rejects skin/morph assets; atomic on failure.
    static void generateLods(Mesh& mesh,MeshLodConstraints constraints={});
    // Recipe v1: <=90k vertices/30k triangles/64 parts, 1..3 candidates;
    // only strictly reducing levels survive. Neither operation edits base data.
    static void validateLodRecipe(const Mesh& mesh);
    static void regenerateLods(Mesh& mesh); // No recipe: no-op. Atomic on failure.
};
struct PackedMeshIndices {
    bool useUint16=false;
    // Padded to a multiple of four bytes for WebGPU buffer creation/write.
    std::vector<std::byte> bytes;
};
// Retains the public CPU uint32 index representation; packs only for upload.
PackedMeshIndices packMeshIndices(const Mesh& mesh);
} // namespace engine
