#pragma once
#include "engine/mesh_optimizer.hpp"
#include "formations.hpp"
#include <map>
#include <tuple>

namespace hybrid_example {
// Application-owned rock shaping. The engine still receives an ordinary mesh.
inline std::shared_ptr<const engine::Mesh> sculptCave(const engine::TerrainAsset& terrain,
    const engine::Mesh& joined,int halfWidth,int depth,float ceiling,bool addFormations=true) {
    using namespace engine;
    auto mesh=std::make_shared<Mesh>(joined);MeshOptimizer::optimize(*mesh);
    // Refine every edge uniformly, sharing midpoint indices across triangles.
    // Canonical source vertices include blend attributes but not old tangents.
    for(auto& v:mesh->vertices)v.tangent={};
    MeshOptimizer::optimize(*mesh);
    // Reserve triangles for local formations, the pond and the grotto basin.
    for(int level=0;level<2 && mesh->indices.size()*4<=48000;++level){
        std::map<std::pair<unsigned,unsigned>,unsigned> edges;
        auto midpoint=[&](unsigned a,unsigned b){if(a>b)std::swap(a,b);const auto edge=std::pair{a,b};
            if(auto it=edges.find(edge);it!=edges.end())return it->second;
            auto v=mesh->vertices[a];const auto& other=mesh->vertices[b];v.position=(v.position+other.position)*.5f;v.normal=normalized(v.normal+other.normal);v.uv={(v.uv.x+other.uv.x)*.5f,(v.uv.y+other.uv.y)*.5f};
            const auto id=unsigned(mesh->vertices.size());mesh->vertices.push_back(v);edges.emplace(edge,id);return id;};
        std::vector<unsigned> refined;
        for(std::size_t i=0;i<mesh->indices.size();i+=3){const auto a=mesh->indices[i],b=mesh->indices[i+1],c=mesh->indices[i+2],ab=midpoint(a,b),bc=midpoint(b,c),ca=midpoint(c,a);
            refined.insert(refined.end(),{a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca});}
        mesh->indices=std::move(refined);for(auto& part:mesh->parts){part.firstIndex*=4;part.indexCount*=4;}
    }
    if(mesh->indices.size()>90000)throw std::length_error("Cave loft exceeds triangle budget");
    const auto original=mesh->vertices;(void)terrain;(void)depth;
    for(auto& v:mesh->vertices){const auto p=v.position;
        const float blend=std::clamp((v.uv.x-.5f/256)*256/255,0.f,1.f);
        const float envelope=(1-blend)*std::clamp(p.y/.5f,0.f,1.f)*std::clamp((5-p.y)/.5f,0.f,1.f);
        const float side=std::min(1.f,std::abs(p.x)/halfWidth);
        const float ripple=std::sin(2.1f*p.z+1.7f*p.y)*std::sin(1.3f*p.x+1.1f*p.z);
        v.position.x+=.09f*envelope*side*ripple;
        v.position.y+=.07f*envelope*side*std::sin(2.7f*p.z+2.2f*p.x);
    }
    // Vertices split for materials, seam UVs or original hard normals still
    // belong to one sculpted surface. Accumulate normals across those splits.
    using Key=std::tuple<long long,long long,long long>;
    auto key=[](Vec3 p){return Key{std::llround(p.x*100000),std::llround(p.y*100000),std::llround(p.z*100000)};};
    std::map<Key,Vec3> normals;
    for(std::size_t i=0;i<mesh->indices.size();i+=3){const auto a=mesh->indices[i],b=mesh->indices[i+1],c=mesh->indices[i+2];
        const auto normal=cross(mesh->vertices[b].position-mesh->vertices[a].position,mesh->vertices[c].position-mesh->vertices[a].position);
        for(auto index:{a,b,c}){auto& sum=normals[key(original[index].position)];sum=sum+normal;}
    }
    for(std::size_t i=0;i<mesh->vertices.size();++i){auto& v=mesh->vertices[i];
        const float blend=std::clamp((v.uv.x-.5f/256)*256/255,0.f,1.f);
        const auto normal=normals[key(original[i].position)];
        if(dot(normal,normal)>1e-16f)v.normal=normalized(original[i].normal*blend+normalized(normal)*(1-blend));
        v.tangent={};
    }
    // Publish a shared indexed surface, then derive tangents and render LOD.
    mesh->invalidateBounds();MeshOptimizer::optimize(*mesh);mesh->generateTangents();MeshLodRecipe recipe;
    for(const auto& v:mesh->vertices)recipe.lockedVertices.push_back(v.uv.x>.5f/256+1e-7f);
    mesh->lodRecipe=std::move(recipe);
    // Collision is prepared by the caller from the completed full-detail base.
    return addFormations?caveFormations(mesh,halfWidth,depth,ceiling):mesh;
}
}
