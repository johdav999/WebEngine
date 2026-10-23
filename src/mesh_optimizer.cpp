#include "engine/mesh_optimizer.hpp"
#include <meshoptimizer.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace engine {
namespace {
// Binary equivalence must include all attributes without uninitialized padding.
static_assert(std::is_trivially_copyable_v<Vertex> && sizeof(Vertex)==12*sizeof(float));
void validate(const Mesh& mesh) {
    mesh.validateMorphTargets();
    if(mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size()%3
        || mesh.vertices.size()>std::numeric_limits<std::uint32_t>::max()
        || mesh.indices.size()>std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Mesh must contain bounded indexed triangles");
    for(auto index:mesh.indices) if(index>=mesh.vertices.size())
        throw std::invalid_argument("Mesh index is outside the vertex buffer");
    if(!mesh.skin.empty() && (mesh.skin.size()!=mesh.vertices.size() || !mesh.skeleton))
        throw std::invalid_argument("Skin stream does not match mesh vertices");
    for(const auto& skin:mesh.skin) {
        float total=0;
        for(std::size_t i=0;i<4;++i) {
            if(skin.joints[i]>=mesh.skeleton->bindings.size() || !std::isfinite(skin.weights[i]) || skin.weights[i]<0)
                throw std::invalid_argument("Invalid mesh skin weights");
            total+=skin.weights[i];
        }
        if(std::abs(total-1)>0.0001f) throw std::invalid_argument("Skin weights must be normalized");
    }
}
}
MeshOptimizationResult MeshOptimizer::optimize(Mesh& mesh) {
    validate(mesh);
    if(mesh.lodRecipe) throw std::invalid_argument("Optimize before constructing a vertex-indexed LOD recipe");
    std::size_t end=0;
    for(const auto& part:mesh.parts) {
        if(part.firstIndex!=end || !part.indexCount || part.indexCount%3
            || part.indexCount>mesh.indices.size()-end || part.materialIndex>=mesh.materials.size())
            throw std::invalid_argument("Mesh parts must partition triangles with valid materials");
        end+=part.indexCount;
    }
    if(!mesh.parts.empty() && end!=mesh.indices.size())
        throw std::invalid_argument("Mesh parts must cover the index buffer");
    std::vector<unsigned int> remap(mesh.vertices.size());
    const std::array<meshopt_Stream,2> streams={meshopt_Stream{mesh.vertices.data(),sizeof(Vertex),sizeof(Vertex)},
        meshopt_Stream{mesh.skin.data(),sizeof(SkinVertex),sizeof(SkinVertex)}};
    // The custom comparison has no stream-count cap and includes every morph offset.
    const auto unique=mesh.morphTargets.empty() ? meshopt_generateVertexRemapMulti(remap.data(),mesh.indices.data(),mesh.indices.size(),
        mesh.vertices.size(),streams.data(),mesh.skin.empty()?1:2) :
        meshopt_generateVertexRemapCustom(remap.data(),mesh.indices.data(),mesh.indices.size(),&mesh.vertices[0].position.x,
            mesh.vertices.size(),sizeof(Vertex),[&](unsigned int a,unsigned int b) {
                if(std::memcmp(&mesh.vertices[a],&mesh.vertices[b],sizeof(Vertex))) return false;
                if(!mesh.skin.empty()&&std::memcmp(&mesh.skin[a],&mesh.skin[b],sizeof(SkinVertex))) return false;
                for(const auto& target:mesh.morphTargets) for(const auto* stream:{&target.positions,&target.normals,&target.tangents})
                    if(!stream->empty()&&std::memcmp(&(*stream)[a],&(*stream)[b],sizeof(Vec3))) return false;
                return true;
            });
    auto morphs=mesh.morphTargets;
    for(auto& target:morphs) for(auto* stream:{&target.positions,&target.normals,&target.tangents}) if(!stream->empty()) {
        std::vector<Vec3> next(unique);
        meshopt_remapVertexBuffer(next.data(),stream->data(),stream->size(),sizeof(Vec3),remap.data());stream->swap(next);
    }
    std::vector<Vertex> vertices(unique);
    std::vector<std::uint32_t> indices(mesh.indices.size());
    meshopt_remapVertexBuffer(vertices.data(),mesh.vertices.data(),mesh.vertices.size(),sizeof(Vertex),remap.data());
    std::vector<SkinVertex> skin(mesh.skin.empty()?0:unique);
    if(!skin.empty()) meshopt_remapVertexBuffer(skin.data(),mesh.skin.data(),mesh.skin.size(),sizeof(SkinVertex),remap.data());
    meshopt_remapIndexBuffer(indices.data(),mesh.indices.data(),mesh.indices.size(),remap.data());
    // Never reorder triangles across draw/material boundaries.
    if(mesh.parts.empty()) meshopt_optimizeVertexCache(indices.data(),indices.data(),indices.size(),unique);
    else for(const auto& part:mesh.parts)
        meshopt_optimizeVertexCache(indices.data()+part.firstIndex,indices.data()+part.firstIndex,part.indexCount,unique);
    remap.resize(unique);
    const auto used=meshopt_optimizeVertexFetchRemap(remap.data(),indices.data(),indices.size(),unique);
    for(auto& target:morphs) for(auto* stream:{&target.positions,&target.normals,&target.tangents}) if(!stream->empty()) {
        std::vector<Vec3> next(used);
        meshopt_remapVertexBuffer(next.data(),stream->data(),stream->size(),sizeof(Vec3),remap.data());stream->swap(next);
    }
    meshopt_remapIndexBuffer(indices.data(),indices.data(),indices.size(),remap.data());
    std::vector<Vertex> fetched(used);
    meshopt_remapVertexBuffer(fetched.data(),vertices.data(),unique,sizeof(Vertex),remap.data());vertices.swap(fetched);
    if(!skin.empty()) {
        std::vector<SkinVertex> fetchedSkin(used);meshopt_remapVertexBuffer(fetchedSkin.data(),skin.data(),unique,sizeof(SkinVertex),remap.data());skin.swap(fetchedSkin);
    }
    const MeshOptimizationResult result{mesh.vertices.size(),used};
    mesh.vertices.swap(vertices);mesh.indices.swap(indices);mesh.skin.swap(skin);
    mesh.morphTargets.swap(morphs);mesh.invalidateBounds();
    return result;
}
void MeshOptimizer::generateLods(Mesh& mesh,MeshLodConstraints constraints) {
    validate(mesh);
    if(!constraints.lockedVertices.empty()&&constraints.lockedVertices.size()!=mesh.vertices.size())
        throw std::invalid_argument("LOD lock count must match source vertices");
    for(auto value:constraints.lockedVertices)if(value>1)throw std::invalid_argument("LOD locks must be zero or one");
    for(const auto& v:mesh.vertices) {
        for(float value:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y})
            if(!std::isfinite(value)) throw std::invalid_argument("LOD vertices must be finite");
    }
    if(mesh.skeleton || !mesh.skin.empty() || !mesh.morphTargets.empty())
        throw std::invalid_argument("Generated LODs require a static mesh");
    std::size_t end=0;
    for(const auto& part:mesh.parts) {
        if(part.firstIndex!=end || !part.indexCount || part.indexCount%3 ||
            part.indexCount>mesh.indices.size()-end || part.materialIndex>=mesh.materials.size())
            throw std::invalid_argument("Invalid LOD material partition");
        end+=part.indexCount;
    }
    if(!mesh.parts.empty() && end!=mesh.indices.size()) throw std::invalid_argument("Incomplete LOD partition");
    std::vector<MeshLod> levels;
    constexpr float ratios[]={0.5f,0.2f,0.06f},errors[]={0.002f,0.008f,0.02f},pixels[]={180,80,32};
    constexpr float weights[]={0.1f,0.1f,0.1f,0.1f,0.1f}; // normals + UVs
    for(std::size_t level=0;level<3;++level) {
        auto next=std::make_shared<Mesh>();next->vertices=mesh.vertices;next->materials=mesh.materials;
        auto simplify=[&](std::uint32_t first,std::uint32_t count,std::uint32_t material) {
            std::vector<unsigned int> indices(count);
            const auto target=std::max<std::size_t>(3,std::size_t(count*ratios[level])/3*3);
            auto size=meshopt_simplifyWithAttributes(indices.data(),mesh.indices.data()+first,count,
                &mesh.vertices[0].position.x,mesh.vertices.size(),sizeof(Vertex),&mesh.vertices[0].normal.x,
                sizeof(Vertex),weights,5,constraints.lockedVertices.empty()?nullptr:constraints.lockedVertices.data(),target,errors[level],
                constraints.lockBorders?meshopt_SimplifyLockBorder:0,nullptr);
            if(constraints.lockBorders||!constraints.lockedVertices.empty()){
                // Collapses can leave zero-area faces along collinear locked
                // samples. They contribute no surface and violate static mesh
                // validation; keep the part if no valid faces remain.
                std::size_t kept=0;
                for(std::size_t i=0;i<size;i+=3){const auto a=mesh.vertices[indices[i]].position,b=mesh.vertices[indices[i+1]].position,c=mesh.vertices[indices[i+2]].position;
                    const auto n=cross(b-a,c-a);if(dot(n,n)<1e-12f)continue;
                    indices[kept++]=indices[i];indices[kept++]=indices[i+1];indices[kept++]=indices[i+2];}
                size=kept;
            }
            if(!size) { indices.assign(mesh.indices.begin()+first,mesh.indices.begin()+first+count);size=count; }
            next->parts.push_back({std::uint32_t(next->indices.size()),std::uint32_t(size),material});
            next->indices.insert(next->indices.end(),indices.begin(),indices.begin()+size);
        };
        if(mesh.parts.empty()) { simplify(0,std::uint32_t(mesh.indices.size()),0);next->parts.clear(); }
        else for(const auto& part:mesh.parts) simplify(part.firstIndex,part.indexCount,part.materialIndex);
        optimize(*next);
        if(!levels.empty() && next->indices.size()>levels.back().mesh->indices.size()) next=std::make_shared<Mesh>(*levels.back().mesh);
        levels.push_back({std::move(next),pixels[level]});
    }
    mesh.lods.swap(levels);
    mesh.lodRecipe.reset(); // Explicit new constraints supersede any previous recipe.
}
void MeshOptimizer::validateLodRecipe(const Mesh& mesh) {
    if(!mesh.lodRecipe)return;
    const auto& r=*mesh.lodRecipe;
    if(r.version!=1||r.levels<1||r.levels>3||mesh.vertices.size()>90000||mesh.indices.size()>90000||mesh.parts.size()>64)
        throw std::invalid_argument("Unsupported or oversized LOD regeneration recipe");
    validate(mesh);
    if(mesh.skeleton||!mesh.skin.empty()||!mesh.morphTargets.empty())throw std::invalid_argument("LOD recipes require static meshes");
    if(!r.lockedVertices.empty()&&r.lockedVertices.size()!=mesh.vertices.size())throw std::invalid_argument("Recipe lock count must match base vertices");
    for(auto lock:r.lockedVertices)if(lock>1)throw std::invalid_argument("Invalid recipe vertex lock");
    for(const auto& v:mesh.vertices)for(float f:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y})
        if(!std::isfinite(f))throw std::invalid_argument("Non-finite LOD recipe vertex");
    std::size_t end=0;
    for(const auto& part:mesh.parts){
        if(part.firstIndex!=end||!part.indexCount||part.indexCount%3||part.indexCount>mesh.indices.size()-end||part.materialIndex>=mesh.materials.size())
            throw std::invalid_argument("Invalid LOD recipe partition");
        end+=part.indexCount;
    }
    if(!mesh.parts.empty()&&end!=mesh.indices.size())throw std::invalid_argument("Incomplete LOD recipe partition");
}
void MeshOptimizer::regenerateLods(Mesh& mesh) {
    if(!mesh.lodRecipe)return;
    validateLodRecipe(mesh);
    const auto& r=*mesh.lodRecipe;
    Mesh staged=mesh;staged.lods.clear();staged.lodRecipe.reset();
    generateLods(staged,{r.lockBorders,r.lockedVertices});staged.lods.resize(r.levels);
    std::size_t previous=mesh.indices.size();
    std::erase_if(staged.lods,[&](const MeshLod& lod){if(lod.mesh->indices.size()>=previous)return true;previous=lod.mesh->indices.size();return false;});
    mesh.lods.swap(staged.lods);
}
PackedMeshIndices packMeshIndices(const Mesh& mesh) {
    validate(mesh);
    PackedMeshIndices packed;
    packed.useUint16=*std::max_element(mesh.indices.begin(),mesh.indices.end())<=65535;
    const auto elementBytes=packed.useUint16?sizeof(std::uint16_t):sizeof(std::uint32_t);
    if(mesh.indices.size()>(std::numeric_limits<std::size_t>::max()-3)/elementBytes)
        throw std::length_error("Mesh index buffer exceeds the address space");
    packed.bytes.resize((mesh.indices.size()*elementBytes+3)&~std::size_t(3));
    if(packed.useUint16) {
        for(std::size_t i=0;i<mesh.indices.size();++i) {
            const auto index=static_cast<std::uint16_t>(mesh.indices[i]);
            std::memcpy(packed.bytes.data()+i*elementBytes,&index,elementBytes);
        }
    } else std::memcpy(packed.bytes.data(),mesh.indices.data(),mesh.indices.size()*elementBytes);
    return packed;
}
} // namespace engine
