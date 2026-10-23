#pragma once
#include "engine/terrain_mesh.hpp"

namespace hybrid_example {
// Application-owned placement; the engine owns only the generic mesh operation.
inline std::shared_ptr<const engine::Mesh> caveFormations(std::shared_ptr<const engine::Mesh> mesh,
    int halfWidth,int depth,float ceiling){
    using namespace engine;
    // Example placement policy; reusable refinement/extrusion belongs to engine_core.
    // Sample the actual sculpted floor/arch so anchors remain valid at all sizes.
    auto sample=[&](float x,float z,bool ceilingSurface){
        float best=ceilingSurface?-10000.f:10000.f;Vec3 point;
        for(std::size_t i=0;i<mesh->indices.size();i+=3){
            const auto a=mesh->vertices[mesh->indices[i]].position,b=mesh->vertices[mesh->indices[i+1]].position,c=mesh->vertices[mesh->indices[i+2]].position;
            const auto n=cross(b-a,c-a);if((ceilingSurface?n.y>=-.00001f:n.y<=.00001f))continue;
            const float y=a.y-(n.x*(x-a.x)+n.z*(z-a.z))/n.y;const Vec3 p{x,y,z};
            const auto ab=b-a,ac=c-a,ap=p-a;const float aa=dot(ab,ab),bb=dot(ab,ac),cc=dot(ac,ac),d=aa*cc-bb*bb;
            const float u=(cc*dot(ap,ab)-bb*dot(ap,ac))/d,v=(aa*dot(ap,ac)-bb*dot(ap,ab))/d;
            if(u<-.00001f||v<-.00001f||u+v>1.00001f)continue;
            // First chamber only; deeper rooms may overlap in projection.
            if(y<-.2f||y>ceiling+.2f)continue;
            if(ceilingSurface?y>best:y<best){best=y;point=p;}
        }
        if(std::abs(best)>9999)throw std::invalid_argument("Formation attachment surface missing");return point;
    };
    std::array<TerrainMeshFormation,4> formations;
    for(unsigned i=0;i<formations.size();++i){const bool hanging=i>=2;const float side=i%2?-1.f:1.f;
        auto& f=formations[i];f.anchor=sample(side*halfWidth*.55f,2.9f+(depth-4.f)*(i%2?.65f:.25f),hanging);
        f.direction={0,hanging?-1.f:1.f,0};f.length=std::min(ceiling*.36f,hanging?1.5f:1.25f);
        f.baseRadius=std::min(.55f,halfWidth*.20f);f.bend={side*.04f,0,.025f};f.seed=71+i*137;
    }
    TerrainMeshFormationSettings formationSettings;formationSettings.maxWork=32000000;
    return buildTerrainMeshFormations(mesh,formations,formationSettings).mesh;
 }
} // namespace hybrid_example
