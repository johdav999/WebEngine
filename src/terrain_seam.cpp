#include "engine/terrain_seam.hpp"
#include "terrain_surface.hpp"
#include "engine/mesh_optimizer.hpp"
#include <algorithm>
#include <limits>
#include <map>

namespace engine {
namespace {
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float length2(Vec3 p){return dot(p,p);}
Vec3 unit(Vec3 p){const float l=std::sqrt(length2(p));return l>1e-8f?p*(1/l):Vec3{0,1,0};}
struct Edge {Vec3 a,b,na,nb;};
struct Nearest {Vec3 point,normal;float distance2=std::numeric_limits<float>::infinity();};
using terrain_surface::singleLayer;
}
void TerrainSeamSettings::validate() const {
    require(lodLevels<=3,"Terrain seam supports zero through three LOD levels");
    require(finite(terrainOrigin)&&std::isfinite(weldDistance)&&weldDistance>0&&weldDistance<=10&&
        std::isfinite(blendWidth)&&blendWidth>=weldDistance&&blendWidth<=100&&
        std::isfinite(maxEdgeLength)&&maxEdgeLength>=.01f&&maxEdgeLength<=blendWidth,
        "Invalid terrain seam distances");
    require(maxBoundaryEdges>=1&&maxBoundaryEdges<=2048&&maxOutputTriangles>=1&&maxOutputTriangles<=30000&&maxWork>=1&&maxWork<=64000000,
        "Invalid terrain seam work budget");
}
TerrainSeamResult buildTerrainSeam(const TerrainAsset& terrain,std::shared_ptr<const Mesh> source,TerrainSeamSettings settings){
    settings.validate();require(!terrain.patch(),"Terrain seams require a full source asset");
    require(source&&source->indices.size()<=4096*3&&source->lods.empty(),"Terrain seam source must have <=4096 triangles and no authored LOD");
    (void)makeMeshTerrainActor("Validate seam source",source);
    require(!settings.lodLevels||source->parts.size()<=64,"Seam LOD supports at most 64 material parts");
    const auto& d=terrain.data();require(settings.weldDistance<std::min(d.spacing.x,d.spacing.y)*.5f,"Seam weld distance must be less than half a source cell");
    const auto groundLayer=singleLayer(*d.material);
    std::vector<MaterialLayer> sourceLayers;for(const auto& m:source->materials){require(bool(m),"Missing mesh seam material");sourceLayers.push_back(singleLayer(*m));
        require(m->shader==d.material->shader,"Seam shader families must match");
        if(m->layers.enabled&&d.material->layers.enabled)require(m->layers.triplanarSharpness==d.material->layers.triplanarSharpness,"Seam triplanar sharpness must match");}
    std::uint32_t work=0;auto spend=[&]{if(++work>settings.maxWork)throw std::length_error("Terrain seam work budget exceeded");};
    auto point=[&](unsigned x,unsigned z){return settings.terrainOrigin+Vec3{x*d.spacing.x,d.heights[std::size_t(z)*d.samplesX+x],z*d.spacing.y};};
    auto normal=[&](unsigned x,unsigned z){const auto xl=x?x-1:x,xr=std::min(x+1,d.samplesX-1),zl=z?z-1:z,zr=std::min(z+1,d.samplesZ-1);
        return unit(cross(point(x,zr)-point(x,zl),point(xr,z)-point(xl,z)));};
    std::vector<Edge> edges;std::map<std::uint64_t,Vec3> samples;
    auto add=[&](unsigned x,unsigned z,unsigned xx,unsigned zz){
        if(edges.size()==settings.maxBoundaryEdges)throw std::length_error("Terrain seam boundary budget exceeded");
        edges.push_back({point(x,z),point(xx,zz),normal(x,z),normal(xx,zz)});
        samples.emplace(std::uint64_t(z)*d.samplesX+x,point(x,z));samples.emplace(std::uint64_t(zz)*d.samplesX+xx,point(xx,zz));
    };
    for(unsigned z=0;z<d.samplesZ-1;++z)for(unsigned x=0;x<d.samplesX-1;++x)if(d.hole(x,z)){
        if(x>0&&!d.hole(x-1,z))add(x,z,x,z+1);
        if(x+1<d.samplesX-1&&!d.hole(x+1,z))add(x+1,z,x+1,z+1);
        if(z>0&&!d.hole(x,z-1))add(x,z,x+1,z);
        if(z+1<d.samplesZ-1&&!d.hole(x,z+1))add(x,z+1,x+1,z+1);
    }
    TerrainSeamResult result;result.boundaryEdges=std::uint32_t(edges.size());
    if(edges.empty()){result.mesh=std::move(source);result.outputTriangles=std::uint32_t(result.mesh->indices.size()/3);return result;}
    auto nearest=[&](Vec3 p){Nearest n;for(const auto& e:edges){spend();const auto delta=e.b-e.a;const float t=std::clamp(dot(p-e.a,delta)/length2(delta),0.f,1.f);const auto q=e.a+delta*t;const float dd=length2(p-q);
        if(dd<n.distance2)n={q,unit(e.na*(1-t)+e.nb*t),dd};}return n;};
    const float weld2=settings.weldDistance*settings.weldDistance;
    auto snap=[&](Vertex v){auto n=nearest(v.position);if(dot(v.normal,n.normal)<-.01f)return v;float sampleDistance=weld2;bool found=false;Vec3 sample;
        for(const auto& [key,p]:samples){spend();const float dd=length2(v.position-p);if(dd<=sampleDistance){sampleDistance=dd;sample=p;found=true;}}
        if(found){n.point=sample;n.distance2=sampleDistance;}
        if(n.distance2<=weld2){if(n.distance2>1e-12f)++result.weldedVertices;v.position=n.point;}return v;};
    auto weights=std::make_shared<Texture>();weights->name="Terrain seam blend";weights->width=256;weights->height=1;weights->wrapU=weights->wrapV=TextureWrap::Clamp;
    for(unsigned i=0;i<256;++i)weights->pixels.insert(weights->pixels.end(),{std::uint8_t(i),std::uint8_t(255-i),0,0});
    auto out=std::make_shared<Mesh>();out->materials.reserve(source->materials.size());
    for(std::size_t i=0;i<source->materials.size();++i){auto m=std::make_shared<Material>();m->name="Terrain seam / "+source->materials[i]->name;m->shader=source->materials[i]->shader;
        m->doubleSided=source->materials[i]->doubleSided;m->roughness=1;m->layers.enabled=m->layers.triplanar=true;m->layers.triplanarScale=1;
        m->layers.triplanarSharpness=d.material->layers.enabled?d.material->layers.triplanarSharpness:source->materials[i]->layers.triplanarSharpness;
        m->layers.layers[0]=groundLayer;m->layers.layers[1]=sourceLayers[i];m->layers.weights=weights;m->validate();out->materials.push_back(m);}
    auto finishVertex=[&](Vertex v){const auto n=nearest(v.position);const float t=dot(v.normal,n.normal)<-.01f?0:std::clamp(1-std::sqrt(n.distance2)/settings.blendWidth,0.f,1.f),w=t*t*(3-2*t);
        v.uv={w*(255.f/256)+.5f/256,.5f};v.normal=unit(v.normal*(1-w)+n.normal*w);return v;};
    auto midpoint=[](Vertex a,Vertex b){Vertex c;c.position=(a.position+b.position)*.5f;c.normal=unit(a.normal+b.normal);return c;};
    auto emit=[&](auto&& self,Vertex a,Vertex b,Vertex c,unsigned depth)->void{
        spend();const float ab=length2(a.position-b.position),bc=length2(b.position-c.position),ca=length2(c.position-a.position);
        if(std::max({ab,bc,ca})>settings.maxEdgeLength*settings.maxEdgeLength){
            if(depth==24)throw std::length_error("Terrain seam subdivision depth exceeded");
            if(ab>=bc&&ab>=ca){auto m=midpoint(a,b);self(self,a,m,c,depth+1);self(self,m,b,c,depth+1);}
            else if(bc>=ca){auto m=midpoint(b,c);self(self,a,b,m,depth+1);self(self,a,m,c,depth+1);}
            else{auto m=midpoint(c,a);self(self,a,b,m,depth+1);self(self,m,b,c,depth+1);}return;
        }
        if(length2(cross(b.position-a.position,c.position-a.position))<1e-12f)return;
        if(result.outputTriangles==settings.maxOutputTriangles)throw std::length_error("Terrain seam triangle budget exceeded");
        ++result.outputTriangles;const auto index=std::uint32_t(out->vertices.size());out->vertices.insert(out->vertices.end(),{finishVertex(a),finishVertex(b),finishVertex(c)});out->indices.insert(out->indices.end(),{index,index+1,index+2});
    };
    for(const auto& part:source->parts){require(part.firstIndex%3==0&&part.indexCount%3==0&&part.materialIndex<sourceLayers.size(),"Invalid terrain seam part");const auto first=std::uint32_t(out->indices.size());
        for(unsigned i=part.firstIndex;i<part.firstIndex+part.indexCount;i+=3){
            const Vertex original[3]={source->vertices[source->indices[i]],source->vertices[source->indices[i+1]],source->vertices[source->indices[i+2]]};std::vector<Vertex> polygon;
            for(unsigned e=0;e<3;++e){const auto a=original[e],b=original[(e+1)%3];polygon.push_back(snap(a));std::vector<std::pair<float,Vertex>> split;
                const auto delta=b.position-a.position;const float l2=length2(delta);
                for(const auto& [key,p]:samples){spend();const float t=dot(p-a.position,delta)/l2;
                    if(t<=0||t>=1||length2(p-a.position)<=weld2||length2(p-b.position)<=weld2||length2(p-(a.position+delta*t))>weld2)continue;
                    Vertex v;v.position=p;v.normal=unit(a.normal*(1-t)+b.normal*t);
                    if(dot(v.normal,normal(unsigned(key%d.samplesX),unsigned(key/d.samplesX)))<-.01f)continue;
                    split.emplace_back(t,v);
                }
                std::sort(split.begin(),split.end(),[](const auto& a,const auto& b){return a.first<b.first;});
                for(const auto& [t,v]:split){polygon.push_back(v);++result.insertedSamples;}
            }
            if(polygon.size()==3)emit(emit,polygon[0],polygon[1],polygon[2],0);
            else{Vertex center;center.normal={};for(auto v:polygon){center.position=center.position+v.position;center.normal=center.normal+v.normal;}center.position=center.position*(1.f/polygon.size());center.normal=unit(center.normal);
                for(std::size_t e=0;e<polygon.size();++e)emit(emit,center,polygon[e],polygon[(e+1)%polygon.size()],0);}
        }
        const auto count=std::uint32_t(out->indices.size())-first;if(count)out->parts.push_back({first,count,part.materialIndex});
    }
    require(!out->indices.empty(),"Terrain seam welding removed the entire mesh");out->generateTangents();
    if(settings.lodLevels){
        // Exact attribute welding enables simplification of tessellated faces.
        // Every vertex in the blend band is immutable at every detail level.
        MeshOptimizer::optimize(*out);
        std::vector<std::uint8_t> locks;locks.reserve(out->vertices.size());
        for(const auto& v:out->vertices)locks.push_back(v.uv.x>.5f/256+1e-7f);
        out->lodRecipe=MeshLodRecipe{1,settings.lodLevels,true,std::move(locks)};
        MeshOptimizer::regenerateLods(*out);
    }
    (void)makeMeshTerrainActor("Validate prepared seam",out);result.mesh=std::move(out);return result;
}
} // namespace engine
