#include "engine/terrain_mesh.hpp"
#include "engine/terrain.hpp"
#include "engine/mesh_optimizer.hpp"
#include <algorithm>
#include <map>
#include <queue>
#include <tuple>

namespace engine {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float square(Vec3 p){return dot(p,p);}
using Position=std::tuple<float,float,float>;
Position position(Vec3 p){return {p.x,p.y,p.z};}
using Edge=std::pair<Position,Position>;
Edge edge(Vec3 a,Vec3 b){auto x=position(a),y=position(b);if(y<x)std::swap(x,y);return {x,y};}
using IndexEdge=std::pair<unsigned,unsigned>;
IndexEdge indexEdge(unsigned a,unsigned b){return {std::min(a,b),std::max(a,b)};}
// Closest point barycentrics, including edge/vertex regions (Ericson).
Vec3 barycentric(Vec3 p,Vec3 a,Vec3 b,Vec3 c){
    const auto ab=b-a,ac=c-a,ap=p-a;const float d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return {1,0,0};
    const auto bp=p-b;const float d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return {0,1,0};
    const float vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0){const float v=d1/(d1-d3);return {1-v,v,0};}
    const auto cp=p-c;const float d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return {0,0,1};
    const float vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0){const float w=d2/(d2-d6);return {1-w,0,w};}
    const float va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const float w=(d4-d3)/((d4-d3)+(d5-d6));return {0,1-w,w};}
    const float inv=1/(va+vb+vc);return {1-(vb+vc)*inv,vb*inv,vc*inv};
}
Vertex interpolate(const Vertex& a,const Vertex& b,const Vertex& c,Vec3 w){
    Vertex v;v.position=a.position*w.x+b.position*w.y+c.position*w.z;
    v.normal=normalized(a.normal*w.x+b.normal*w.y+c.normal*w.z);
    v.uv={a.uv.x*w.x+b.uv.x*w.y+c.uv.x*w.z,a.uv.y*w.x+b.uv.y*w.y+c.uv.y*w.z};v.tangent={};return v;
}
struct Builder {
    Mesh mesh;TerrainMeshFormationSettings settings;std::uint32_t work=0;
    std::vector<std::uint8_t> locks;
    void spend(){if(++work>settings.maxWork)throw std::length_error("Terrain mesh formation work budget exceeded");}
    void budget(std::size_t triangles)const{if(triangles>settings.maxOutputTriangles||mesh.vertices.size()>90000)
        throw std::length_error("Terrain mesh formation geometry budget exceeded");}
    unsigned add(Vertex v,bool locked=false){spend();if(mesh.vertices.size()==90000)throw std::length_error("Terrain mesh formation vertex budget exceeded");
        mesh.vertices.push_back(v);locks.push_back(locked);return unsigned(mesh.vertices.size()-1);}
    Vec3 closest(unsigned t,Vec3 p)const{const auto a=mesh.vertices[mesh.indices[t*3]].position,b=mesh.vertices[mesh.indices[t*3+1]].position,c=mesh.vertices[mesh.indices[t*3+2]].position;
        const auto w=barycentric(p,a,b,c);return a*w.x+b*w.y+c*w.z;}
    // Geometric adjacency crosses attribute splits without changing their UVs.
    // Only the connected, consistently facing sheet containing the anchor grows.
    std::vector<std::uint8_t> patch(const TerrainMeshFormation& f,Vec3 axis){
        const auto n=mesh.indices.size()/3;std::map<Edge,std::vector<unsigned>> edges;
        std::vector<std::uint8_t> eligible(n),selected(n);unsigned start=unsigned(n);float best=1e-10f;
        for(unsigned t=0;t<n;++t){spend();const auto a=mesh.vertices[mesh.indices[t*3]].position,b=mesh.vertices[mesh.indices[t*3+1]].position,c=mesh.vertices[mesh.indices[t*3+2]].position;
            for(auto e:{edge(a,b),edge(b,c),edge(c,a)})edges[e].push_back(t);
            const float distance=square(closest(t,f.anchor)-f.anchor);
            eligible[t]=dot(normalized(cross(b-a,c-a)),axis)>.35f&&distance<f.baseRadius*f.baseRadius;
            if(eligible[t]&&distance<best){best=distance;start=t;}}
        require(start<n,"Terrain mesh formation anchor is not on a surface facing its direction");
        std::queue<unsigned> pending;pending.push(start);selected[start]=1;
        while(!pending.empty()){const auto t=pending.front();pending.pop();spend();
            for(unsigned j=0;j<3;++j){const auto a=mesh.vertices[mesh.indices[t*3+j]].position,b=mesh.vertices[mesh.indices[t*3+(j+1)%3]].position;const auto& adjacent=edges.at(edge(a,b));
                require(adjacent.size()<=2,"Non-manifold formation patch");
                const auto delta=b-a;const float u=std::clamp(dot(f.anchor-a,delta)/square(delta),0.f,1.f);
                if(square(a+delta*u-f.anchor)<f.baseRadius*f.baseRadius*.9999f)
                    require(adjacent.size()==2,"Terrain mesh formation footprint crosses an open edge");
                for(auto next:adjacent)if(eligible[next]&&!selected[next]){selected[next]=1;pending.push(next);}}}
        return selected;
    }
    void refine(const TerrainMeshFormation& f,Vec3 axis,float target){
        for(unsigned level=0;level<12;++level){const auto selected=patch(f,axis);std::map<Edge,bool> split;
            for(unsigned t=0;t<selected.size();++t)if(selected[t])for(unsigned j=0;j<3;++j){spend();const auto a=mesh.vertices[mesh.indices[t*3+j]].position,b=mesh.vertices[mesh.indices[t*3+(j+1)%3]].position;
                if(square(b-a)>target*target*1.0001f)split.emplace(edge(a,b),true);}
            if(split.empty())return;
            std::map<IndexEdge,unsigned> midpoints;std::vector<unsigned> indices;std::vector<MeshPart> parts;
            auto middle=[&](unsigned a,unsigned b){const auto key=indexEdge(a,b);if(auto it=midpoints.find(key);it!=midpoints.end())return it->second;
                const auto id=add(interpolate(mesh.vertices[a],mesh.vertices[b],mesh.vertices[a],{.5f,.5f,0}));midpoints.emplace(key,id);return id;};
            auto emit=[&](unsigned a,unsigned b,unsigned c){spend();if(indices.size()/3==settings.maxOutputTriangles)throw std::length_error("Terrain mesh formation triangle budget exceeded");indices.insert(indices.end(),{a,b,c});};
            for(const auto& part:mesh.parts){const auto first=unsigned(indices.size());for(unsigned i=part.firstIndex;i<part.firstIndex+part.indexCount;i+=3){spend();const unsigned v[3]={mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]};unsigned mids[3]{},mask=0;
                    for(unsigned j=0;j<3;++j)if(split.contains(edge(mesh.vertices[v[j]].position,mesh.vertices[v[(j+1)%3]].position))){mask|=1<<j;mids[j]=middle(v[j],v[(j+1)%3]);}
                    if(mask==0)emit(v[0],v[1],v[2]);
                    else if(mask==7){emit(v[0],mids[0],mids[2]);emit(mids[0],v[1],mids[1]);emit(mids[2],mids[1],v[2]);emit(mids[0],mids[1],mids[2]);}
                    else if(mask==1||mask==2||mask==4){const unsigned j=mask==1?0:mask==2?1:2;emit(v[j],mids[j],v[(j+2)%3]);emit(mids[j],v[(j+1)%3],v[(j+2)%3]);}
                    else {const unsigned j=mask==3?0:mask==6?1:2;const auto a=v[j],b=v[(j+1)%3],c=v[(j+2)%3],ab=mids[j],bc=mids[(j+1)%3];emit(a,ab,c);emit(ab,bc,c);emit(ab,b,bc);}}
                parts.push_back({first,unsigned(indices.size())-first,part.materialIndex});}
            mesh.indices=std::move(indices);mesh.parts=std::move(parts);budget(mesh.indices.size()/3);
        }
        throw std::length_error("Terrain mesh formation refinement depth exceeded");
    }
    void insertAnchor(const TerrainMeshFormation& f,Vec3 axis){
        const auto selected=patch(f,axis);const float target=settings.maxEdgeLength?settings.maxEdgeLength:f.baseRadius/5;
        unsigned containing=unsigned(selected.size());float best=1e-10f;
        for(unsigned t=0;t<selected.size();++t)if(selected[t]){const float d=square(closest(t,f.anchor)-f.anchor);if(d<best){best=d;containing=t;}}
        require(containing<selected.size(),"Formation anchor cannot be resolved on the refined surface");
        const unsigned tri[3]={mesh.indices[containing*3],mesh.indices[containing*3+1],mesh.indices[containing*3+2]};
        auto attributes=[&](unsigned a,unsigned b,unsigned c){
            const auto& va=mesh.vertices[a];const auto& vb=mesh.vertices[b];const auto& vc=mesh.vertices[c];
            const auto ab=vb.position-va.position,ac=vc.position-va.position,ap=f.anchor-va.position;
            const float aa=dot(ab,ab),bb=dot(ab,ac),cc=dot(ac,ac),d=aa*cc-bb*bb;
            const float u=(cc*dot(ap,ab)-bb*dot(ap,ac))/d,v=(aa*dot(ap,ac)-bb*dot(ap,ab))/d;
            auto out=interpolate(va,vb,vc,{1-u-v,u,v});out.position=f.anchor;return out;
        };
        // A point almost on an edge must split that edge and both faces, rather
        // than introduce a needle-thin triangle inside just one face. Very close
        // existing vertices are relocated to the exact anchor. The small local
        // remesh also interpolates each attribute island's own source UV plane.
        float vertexDistance=target*target*.0144f;unsigned nearby=unsigned(mesh.vertices.size());
        for(auto id:tri){const float d=square(mesh.vertices[id].position-f.anchor);if(d<vertexDistance){vertexDistance=d;nearby=id;}}
        if(nearby<mesh.vertices.size()){
            const auto key=position(mesh.vertices[nearby].position);std::map<unsigned,Vertex> replacement;
            for(unsigned i=0;i<mesh.indices.size();i+=3){spend();for(unsigned j=0;j<3;++j){const auto id=mesh.indices[i+j];
                if(position(mesh.vertices[id].position)==key&&!replacement.contains(id))replacement.emplace(id,attributes(mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]));}}
            for(const auto& [id,v]:replacement){require(!locks[id],"Formation anchor meets a protected vertex");mesh.vertices[id]=v;locks[id]=1;}return;
        }
        bool splitEdge=false;Edge chosen;float edgeDistance=target*target*.0144f;
        for(unsigned j=0;j<3;++j){const auto a=mesh.vertices[tri[j]].position,b=mesh.vertices[tri[(j+1)%3]].position,delta=b-a;
            const float t=std::clamp(dot(f.anchor-a,delta)/square(delta),0.f,1.f),d=square(a+delta*t-f.anchor);
            if(d<edgeDistance){edgeDistance=d;chosen=edge(a,b);splitEdge=true;}}
        std::map<IndexEdge,unsigned> shared;std::vector<unsigned> indices;std::vector<MeshPart> parts;
        auto emit=[&](unsigned a,unsigned b,unsigned c){indices.insert(indices.end(),{a,b,c});budget(indices.size()/3);};
        for(const auto& part:mesh.parts){const auto first=unsigned(indices.size());for(unsigned i=part.firstIndex;i<part.firstIndex+part.indexCount;i+=3){spend();const unsigned ids[3]={mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]};
                int edgeIndex=-1;if(splitEdge)for(unsigned j=0;j<3;++j)if(edge(mesh.vertices[ids[j]].position,mesh.vertices[ids[(j+1)%3]].position)==chosen)edgeIndex=int(j);
                if(edgeIndex>=0){const unsigned a=ids[edgeIndex],b=ids[(edgeIndex+1)%3],c=ids[(edgeIndex+2)%3];const auto key=indexEdge(a,b);auto it=shared.find(key);unsigned anchor;
                    if(it==shared.end()){anchor=add(attributes(a,b,c),true);shared.emplace(key,anchor);}else anchor=it->second;
                    emit(a,anchor,c);emit(anchor,b,c);
                }else if(!splitEdge&&i/3==containing){const auto anchor=add(attributes(ids[0],ids[1],ids[2]),true);for(unsigned j=0;j<3;++j)emit(ids[j],ids[(j+1)%3],anchor);}
                else emit(ids[0],ids[1],ids[2]);
            }parts.push_back({first,unsigned(indices.size())-first,part.materialIndex});}
        mesh.indices=std::move(indices);mesh.parts=std::move(parts);budget(mesh.indices.size()/3);
    }
    void formation(const TerrainMeshFormation& f){
        const auto axis=normalized(f.direction);const float target=settings.maxEdgeLength?settings.maxEdgeLength:f.baseRadius/5;
        require(target<=f.baseRadius*.5f,"Formation edge length must resolve its base radius");
        const auto protectedLocks=locks;
        // Refuse protected regions before refining or marking the new tip lock.
        auto selected=patch(f,axis);
        for(unsigned t=0;t<selected.size();++t)if(selected[t])for(unsigned j=0;j<3;++j){const auto id=mesh.indices[t*3+j];const auto d=mesh.vertices[id].position-f.anchor;const auto radial=d-axis*dot(d,axis);
            require(!locks[id]||square(radial)>=f.baseRadius*f.baseRadius,"Terrain mesh formation overlaps a protected LOD vertex");}
        refine(f,axis,target);insertAnchor(f,axis);selected=patch(f,axis);
        const auto original=mesh.vertices;
        std::vector<std::uint8_t> moving(mesh.vertices.size()),dirty(mesh.vertices.size());
        for(unsigned t=0;t<selected.size();++t)if(selected[t])for(unsigned j=0;j<3;++j)moving[mesh.indices[t*3+j]]=1;
        const auto u=normalized(cross(axis,std::abs(axis.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0})),v=cross(axis,u);
        std::uint32_t hash=f.seed*747796405u+2891336453u;hash=((hash>>((hash>>28)+4))^hash)*277803737u;hash=(hash>>22)^hash;
        const float phase=float(hash&65535)*(2*pi/65536);
        for(unsigned i=0;i<mesh.vertices.size();++i)if(moving[i]){spend();auto& vertex=mesh.vertices[i];const auto d=vertex.position-f.anchor;const auto radial=d-axis*dot(d,axis);const float r=std::sqrt(square(radial))/f.baseRadius;
            if(r>=1)continue;
            // Rounded tip and zero displacement/slope at the broad attachment.
            const float s=(std::sqrt(r*r+.0036f)-.06f)/(std::sqrt(1.0036f)-.06f);
            const float angle=std::atan2(dot(radial,v),dot(radial,u));
            const float h=(1-s)*(1-s)*(1+f.irregularity*4*s*(1-s)*std::sin(3*angle+phase+2*s));
            vertex.position=vertex.position+axis*(f.length*h)+f.bend*(h*h);dirty[i]=1;
        }
        // Include unchanged neighbours of deformed faces for a smooth base join.
        auto affected=dirty;for(unsigned i=0;i<mesh.indices.size();i+=3)if(dirty[mesh.indices[i]]||dirty[mesh.indices[i+1]]||dirty[mesh.indices[i+2]])
            for(unsigned j=0;j<3;++j)affected[mesh.indices[i+j]]=1;
        std::map<Position,Vec3> normals;
        for(unsigned i=0;i<mesh.indices.size();i+=3){spend();const auto a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];const auto n=cross(mesh.vertices[b].position-mesh.vertices[a].position,mesh.vertices[c].position-mesh.vertices[a].position);
            require(finite(n)&&square(n)>1e-16f,"Terrain mesh formation produced a degenerate triangle");
            for(auto id:{a,b,c}){auto& sum=normals[position(original[id].position)];sum=sum+n;}}
        std::map<Position,bool> affectedPositions;
        for(unsigned i=0;i<mesh.vertices.size();++i)if(affected[i])affectedPositions.emplace(position(original[i].position),true);
        for(unsigned i=0;i<mesh.vertices.size();++i)if(affectedPositions.contains(position(original[i].position))&&(i>=protectedLocks.size()||!protectedLocks[i]))
            mesh.vertices[i].normal=normalized(normals.at(position(original[i].position)));
    }
};
}
void TerrainMeshFormation::validate()const{
    require(finite(anchor)&&finite(direction)&&square(direction)>1e-8f&&square(direction)<1e8f&&finite(bend)&&
        std::isfinite(length)&&length>=.02f&&length<=100&&std::isfinite(baseRadius)&&baseRadius>=.02f&&baseRadius<=50&&
        std::isfinite(irregularity)&&irregularity>=0&&irregularity<=.2f&&square(bend)<=baseRadius*baseRadius*.25f&&
        std::abs(dot(bend,normalized(direction)))<=1e-5f,"Invalid terrain mesh formation dimensions/direction");
}
void TerrainMeshFormationSettings::validate()const{
    require(std::isfinite(maxEdgeLength)&&maxEdgeLength>=0&&maxEdgeLength<=50&&maxOutputTriangles>=1&&maxOutputTriangles<=30000&&
        maxWork>=1&&maxWork<=64000000&&lodLevels<=3,"Invalid terrain mesh formation preparation settings");
}
TerrainMeshFormationResult buildTerrainMeshFormations(std::shared_ptr<const Mesh> source,std::span<const TerrainMeshFormation> formations,TerrainMeshFormationSettings settings){
    settings.validate();require(source&&formations.size()<=32,"Terrain mesh formations require a source and at most 32 placements");
    require(source->indices.size()<=settings.maxOutputTriangles*3&&source->vertices.size()<=90000&&source->parts.size()<=64&&
        source->skin.empty()&&!source->skeleton&&source->morphTargets.empty(),"Terrain mesh formation requires a bounded static source");
    for(const auto& f:formations)f.validate();
    // Distinct opposing sheets are allowed; same-facing footprints cannot overlap.
    for(unsigned i=0;i<formations.size();++i)for(unsigned j=0;j<i;++j){const auto& a=formations[i];const auto& b=formations[j];
        if(dot(normalized(a.direction),normalized(b.direction))>.35f)require(square(a.anchor-b.anchor)>=(a.baseRadius+b.baseRadius)*(a.baseRadius+b.baseRadius),"Overlapping terrain mesh formation footprints");}
    Builder b;b.settings=settings;b.mesh=*source;b.mesh.lods.clear();b.mesh.lodRecipe.reset();b.mesh.invalidateBounds();
    (void)makeMeshTerrainActor("Validate terrain mesh formation source",std::make_shared<Mesh>(b.mesh));
    if(source->lodRecipe){MeshOptimizer::validateLodRecipe(*source);b.locks=source->lodRecipe->lockedVertices;}
    if(b.locks.empty())b.locks.resize(b.mesh.vertices.size());
    // Normalize an implicit single material part before topology refinement.
    if(b.mesh.parts.empty())b.mesh.parts={{0,unsigned(b.mesh.indices.size()),0}};
    for(const auto& f:formations)b.formation(f);
    b.mesh.invalidateBounds();b.mesh.generateTangents();
    // Tangent reconstruction can reach an unchanged neighbour of the patch.
    // Explicit source locks protect every original attribute, including tangents.
    if(source->lodRecipe&&!source->lodRecipe->lockedVertices.empty())
        for(unsigned i=0;i<source->vertices.size();++i)if(source->lodRecipe->lockedVertices[i])b.mesh.vertices[i]=source->vertices[i];
    if(settings.lodLevels){MeshLodRecipe recipe;recipe.levels=settings.lodLevels;recipe.lockedVertices=std::move(b.locks);b.mesh.lodRecipe=std::move(recipe);MeshOptimizer::regenerateLods(b.mesh);}
    auto result=std::make_shared<Mesh>(std::move(b.mesh));(void)makeMeshTerrainActor("Validate terrain mesh formation result",result);
    return {result,unsigned(formations.size()),unsigned(result->vertices.size()-source->vertices.size()),unsigned(result->indices.size()/3)};
}

void TerrainMeshLakeSettings::validate()const{
    WaterRegion{outline,-depth}.validate();
    require(finite(floorAnchor)&&std::isfinite(waterLevel)&&std::abs(waterLevel)<=10000&&depth>=.02f&&depth<=100&&
        std::isfinite(bankWidth)&&bankWidth>=.05f&&bankWidth<=10&&std::isfinite(maxEdgeLength)&&maxEdgeLength>=.02f&&maxEdgeLength<=bankWidth&&
        waterLevel<=floorAnchor.y+.001f&&waterLevel>=floorAnchor.y-bankWidth&&maxOutputTriangles>=1&&maxOutputTriangles<=30000&&
        maxWaterTriangles>=1&&maxWaterTriangles<=8192&&maxWork>=1&&maxWork<=64000000&&lodLevels<=3,"Invalid terrain mesh lake settings");
    WaterRegion region{outline,-depth};require(region.contains({floorAnchor.x,-depth*.5f,floorAnchor.z}),"Lake anchor must lie inside its shoreline");
    if(waterMaterial){waterMaterial->validate();require(waterMaterial->shader==MaterialShader::Water,"Lake surface requires a water material");}
}
TerrainMeshLakeResult buildTerrainMeshLake(std::shared_ptr<const Mesh> source,const TerrainMeshLakeSettings& s){
    s.validate();require(source&&source->indices.size()<=s.maxOutputTriangles*3&&source->vertices.size()<=90000&&source->parts.size()<=64&&source->skin.empty()&&!source->skeleton&&source->morphTargets.empty(),"Lake requires a bounded static terrain mesh");
    Builder b;b.mesh=*source;b.mesh.lods.clear();b.mesh.lodRecipe.reset();b.mesh.invalidateBounds();
    b.settings.maxOutputTriangles=s.maxOutputTriangles;b.settings.maxWork=s.maxWork;b.settings.lodLevels=s.lodLevels;
    (void)makeMeshTerrainActor("Validate lake source",std::make_shared<Mesh>(b.mesh));
    if(source->lodRecipe){MeshOptimizer::validateLodRecipe(*source);b.locks=source->lodRecipe->lockedVertices;}if(b.locks.empty())b.locks.resize(b.mesh.vertices.size());
    if(b.mesh.parts.empty())b.mesh.parts={{0,unsigned(b.mesh.indices.size()),0}};
    WaterRegion region{s.outline,-s.depth};
    auto bankDistance=[&](Vec3 p){float distance=1e10f;for(unsigned i=0;i<s.outline.size();++i){const auto e=region.plane(i);distance=std::min(distance,e[0]*p.x+e[1]*p.z+e[2]);}return distance;};
    TerrainMeshFormation patch;patch.anchor=s.floorAnchor;patch.direction={0,1,0};patch.baseRadius=0;
    for(auto p:s.outline)patch.baseRadius=std::max(patch.baseRadius,std::sqrt((p.x-s.floorAnchor.x)*(p.x-s.floorAnchor.x)+(p.y-s.floorAnchor.z)*(p.y-s.floorAnchor.z)));
    patch.baseRadius+=s.bankWidth*2+s.maxEdgeLength;
    auto selected=b.patch(patch,{0,1,0});
    for(unsigned t=0;t<selected.size();++t)if(selected[t])for(unsigned j=0;j<3;++j){const auto id=b.mesh.indices[t*3+j];if(bankDistance(b.mesh.vertices[id].position)>-s.bankWidth)require(!b.locks[id],"Lake footprint overlaps a protected terrain vertex");}
    b.refine(patch,{0,1,0},s.maxEdgeLength);b.settings.maxEdgeLength=s.maxEdgeLength;b.insertAnchor(patch,{0,1,0});selected=b.patch(patch,{0,1,0});
    const auto original=b.mesh.vertices;std::vector<std::uint8_t> moving(original.size()),affected(original.size());
    for(unsigned t=0;t<selected.size();++t)if(selected[t])for(unsigned j=0;j<3;++j)moving[b.mesh.indices[t*3+j]]=1;
    auto smooth=[](float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);};
    for(unsigned i=0;i<original.size();++i)if(moving[i]){b.spend();auto& v=b.mesh.vertices[i];const float distance=bankDistance(v.position);if(distance<=-s.bankWidth)continue;
        const float y=distance>=0?s.waterLevel-s.depth*smooth(distance/s.bankWidth):v.position.y+(s.waterLevel-v.position.y)*smooth(1+distance/s.bankWidth);
        v.position.y=y;affected[i]=1;if(std::abs(distance)<=s.maxEdgeLength*2)b.locks[i]=1;
    }
    auto dirty=affected;for(unsigned i=0;i<b.mesh.indices.size();i+=3)if(dirty[b.mesh.indices[i]]||dirty[b.mesh.indices[i+1]]||dirty[b.mesh.indices[i+2]])for(unsigned j=0;j<3;++j)affected[b.mesh.indices[i+j]]=1;
    std::map<Position,Vec3> normals;std::map<Position,bool> changed;
    for(unsigned i=0;i<original.size();++i)if(affected[i])changed.emplace(position(original[i].position),true);
    for(unsigned i=0;i<b.mesh.indices.size();i+=3){b.spend();const auto a=b.mesh.indices[i],bb=b.mesh.indices[i+1],c=b.mesh.indices[i+2];const auto n=cross(b.mesh.vertices[bb].position-b.mesh.vertices[a].position,b.mesh.vertices[c].position-b.mesh.vertices[a].position);
        for(auto id:{a,bb,c}){auto& sum=normals[position(original[id].position)];sum=sum+n;}}
    for(unsigned i=0;i<original.size();++i)if(changed.contains(position(original[i].position)))b.mesh.vertices[i].normal=normalized(normals.at(position(original[i].position)));
    auto water=std::make_shared<Mesh>();
    if(s.waterMaterial)water->materials={s.waterMaterial};else{auto m=std::make_shared<Material>();m->shader=MaterialShader::Water;m->baseColor={.02f,.13f,.15f};m->water.amplitude=0;m->doubleSided=true;water->materials={m};}
    // Project only the wet portions of the prepared lakebed, clipped to both its
    // water level and authored shoreline. This prevents a rectangular overlay
    // from spanning dry banks, and follows every small contour of the mesh bed.
    auto clip=[](const std::vector<Vec3>& polygon,auto distance){std::vector<Vec3> out;if(polygon.empty())return out;auto a=polygon.back();float da=distance(a);
        for(auto c:polygon){const float dc=distance(c);if((da>=0)!=(dc>=0))out.push_back(a+(c-a)*(da/(da-dc)));if(dc>=0)out.push_back(c);a=c;da=dc;}return out;};
    for(unsigned t=0;t<selected.size();++t)if(selected[t]){b.spend();std::vector<Vec3> polygon;for(unsigned j=0;j<3;++j)polygon.push_back(b.mesh.vertices[b.mesh.indices[t*3+j]].position);
        polygon=clip(polygon,[&](Vec3 p){return s.waterLevel-p.y;});
        for(unsigned e=0;e<s.outline.size();++e){const auto plane=region.plane(e);polygon=clip(polygon,[&](Vec3 p){return plane[0]*p.x+plane[1]*p.z+plane[2];});}
        for(unsigned j=1;j+1<polygon.size();++j){Vec3 a=polygon[0],bb=polygon[j],c=polygon[j+1];a.y=bb.y=c.y=0;
            if(square(cross(bb-a,c-a))<1e-12f)continue;
            if(water->indices.size()/3==s.maxWaterTriangles)throw std::length_error("Lake water geometry budget exceeded");
            const auto index=unsigned(water->vertices.size());for(auto p:{a,bb,c})water->vertices.push_back({p,{0,1,0},{p.x,p.z}});water->indices.insert(water->indices.end(),{index,index+1,index+2});}}
    require(!water->indices.empty(),"Lake basin produced no submerged surface");water->parts={{0,unsigned(water->indices.size()),0}};MeshOptimizer::optimize(*water);water->generateTangents();
    b.mesh.invalidateBounds();b.mesh.generateTangents();if(source->lodRecipe&&!source->lodRecipe->lockedVertices.empty())for(unsigned i=0;i<source->vertices.size();++i)if(source->lodRecipe->lockedVertices[i])b.mesh.vertices[i]=source->vertices[i];
    if(s.lodLevels){MeshLodRecipe recipe;recipe.levels=s.lodLevels;recipe.lockedVertices=std::move(b.locks);b.mesh.lodRecipe=std::move(recipe);MeshOptimizer::regenerateLods(b.mesh);}
    auto terrain=std::make_shared<Mesh>(std::move(b.mesh));(void)makeMeshTerrainActor("Validate lake basin",terrain,true);(void)makeMeshTerrainActor("Validate lake water",water);
    return {terrain,water,region,s.waterLevel,unsigned(terrain->vertices.size()-source->vertices.size()),unsigned(terrain->indices.size()/3)};
}

} // namespace engine
