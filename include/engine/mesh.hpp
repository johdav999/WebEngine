#pragma once
#include "engine/math.hpp"
#include "engine/texture.hpp"
#include "engine/material.hpp"
#include "engine/animation.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace engine {
// UV origin is top-left, matching Texture pixel storage and WebGPU sampling.
struct Vertex { Vec3 position; Vec3 normal; Vec2 uv; std::array<float,4> tangent{1,0,0,1}; };
struct MeshPart { std::uint32_t firstIndex=0,indexCount=0,materialIndex=0; };
struct MeshBounds { Vec3 minimum{},maximum{}; bool valid=false; };
struct Mesh;
struct MeshLod { std::shared_ptr<const Mesh> mesh; float maxPixels=0; };
// Additive offsets in mesh-local space, before skeletal skinning. Empty normal or
// tangent streams preserve the base attribute. Position offsets cover every vertex.
struct MorphTarget {
    std::string name;
    std::vector<Vec3> positions,normals,tangents;
    float defaultWeight=0;
};
struct MorphPose {
    std::vector<Vertex> vertices;
    std::uint64_t revision=0;
};
struct Mesh {
    std::vector<MeshLod> lods; // Shared derived static geometry; level zero is this mesh.
    std::vector<Vertex> vertices;
    std::vector<MorphTarget> morphTargets;
    std::vector<SkinVertex> skin; // Empty for static meshes; one entry per vertex otherwise.
    std::shared_ptr<const Skeleton> skeleton;
    std::vector<std::uint32_t> indices;
    std::vector<std::shared_ptr<const Material>> materials;
    std::vector<MeshPart> parts;
    std::vector<std::string> warnings;
    static std::shared_ptr<const Mesh> cube();
    static std::shared_ptr<const Mesh> plane(float size=1);
    static std::shared_ptr<const Mesh> sphere(std::uint32_t segments=32,std::uint32_t rings=16);
    void generateTangents(); // Call after final UV/geometry conversion, before publishing.
    void validateMorphTargets() const;
    const MeshBounds& localBounds() const;
    // Call after editing positions in place; published mesh resources are immutable.
    void invalidateBounds() { boundsData_=nullptr; }
private:
    mutable MeshBounds bounds_;
    mutable const Vertex* boundsData_=nullptr;
    mutable std::size_t boundsSize_=0;
};
} // namespace engine
