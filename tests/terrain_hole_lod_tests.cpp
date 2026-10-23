#include "engine/terrain.hpp"
#include "engine/scene_archive.hpp"
#include "engine/physics.hpp"
#include "engine/mesh_optimizer.hpp"
#include <map>
#include <set>
#include <iostream>
using namespace engine;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
using Point=std::array<float,3>;
using Edge=std::pair<Point,Point>;
std::set<Edge> borders(const Mesh& mesh){
    std::map<Edge,unsigned> counts;
    for(std::size_t i=0;i<mesh.indices.size();i+=3)for(unsigned e=0;e<3;++e){
        auto a=mesh.vertices[mesh.indices[i+e]].position,b=mesh.vertices[mesh.indices[i+(e+1)%3]].position;
        Point p{a.x,a.y,a.z},q{b.x,b.y,b.z};if(q<p)std::swap(p,q);++counts[{p,q}];}
    std::set<Edge> result;for(auto [edge,count]:counts){check(count<=2,"Nonmanifold terrain edge");if(count==1)result.insert(edge);}return result;
}
bool covers(const Mesh& mesh,float x,float z){
    auto cross2=[](Vec3 a,Vec3 b,float x,float z){return (b.x-a.x)*(z-a.z)-(b.z-a.z)*(x-a.x);};
    for(std::size_t i=0;i<mesh.indices.size();i+=3){const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
        if(cross2(a,b,x,z)<=1e-5f&&cross2(b,c,x,z)<=1e-5f&&cross2(c,a,x,z)<=1e-5f)return true;}return false;
}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid prepared topology accepted");}
int main(){try{
    TerrainData data;data.samplesX=34;data.samplesZ=30;data.chunkCells=16;data.heights.resize(34*30);data.holes.resize(33*29);
    for(unsigned z=0;z<30;++z)for(unsigned x=0;x<34;++x)data.heights[z*34+x]=.03f*x+.02f*z;
    // An L-shaped cut crossing chunks, a separate island hole and an edge opening.
    for(unsigned z=9;z<21;++z)for(unsigned x=12;x<19;++x)if(x<16||z<14)data.holes[z*33+x]=1;
    data.holes[3*33+3]=1;for(unsigned z=25;z<29;++z)data.holes[z*33]=1;
    auto asset=TerrainAsset::create(data);std::size_t source=0,coarse=0;
    for(unsigned chunk=0;chunk<asset->chunks().size();++chunk){const auto& mesh=*asset->chunks()[chunk];source+=mesh.indices.size()/3;coarse+=(mesh.lods.empty()?mesh.indices.size():mesh.lods.back().mesh->indices.size())/3;
        const auto boundary=borders(mesh);auto previous=mesh.indices.size();
        for(const auto& lod:mesh.lods){check(lod.mesh->indices.size()<previous,"LOD failed to reduce geometry");previous=lod.mesh->indices.size();check(borders(*lod.mesh)==boundary,"Hole or chunk boundary changed");
            const unsigned x0=(chunk%3)*16,z0=(chunk/3)*16;
            for(unsigned z=z0;z<std::min(z0+16,29u);++z)for(unsigned x=x0;x<std::min(x0+16,33u);++x)
                for(auto offset:{.17f,.53f,.89f})check(covers(*lod.mesh,x+offset,z+.37f)==!data.hole(x,z),"LOD filled a hole or removed solid ground");
        }
    }
    check(coarse<source*.7,"Hole-aware terrain did not simplify sufficiently");
    Scene scene;scene.adoptChild(makeTerrainActor("Cut",asset,true));scene.synchronize();
    auto saved=SceneArchive::load(SceneArchive::save(scene));
    for(unsigned z=0;z<29;++z)for(unsigned x=0;x<33;++x){const bool expected=!data.hole(x,z);
        check(bool(scene.physics().raycast({x+.4f,10,z+.6f},{0,-1,0},20))==expected,"Source collision mismatch");
        check(bool(saved.scene->physics().raycast({x+.4f,10,z+.6f},{0,-1,0},20))==expected,"Archive collision mismatch");}
    TerrainPatch patch{0,0,16,16};auto prepared=TerrainAsset::createPatch(data,patch);check(!prepared->chunks()[0]->lods.empty(),"Cut patch did not generate LOD");
    TerrainAsset::createPatch(data,patch,true,prepared->chunks()[0]);
    auto bad=std::make_shared<Mesh>(*prepared->chunks()[0]);auto badLod=std::make_shared<Mesh>(*bad->lods[0].mesh);badLod->indices[0]=badLod->indices[1];bad->lods[0].mesh=badLod;
    rejects([&]{TerrainAsset::createPatch(data,patch,true,bad);});
    auto mesh=std::make_shared<Mesh>(*Mesh::plane());const auto before=mesh->lods;std::vector<std::uint8_t> invalid(1,1);
    rejects([&]{MeshOptimizer::generateLods(*mesh,{true,invalid});});check(mesh->lods.size()==before.size(),"Failed LOD publication changed source");
    auto curved=data;for(unsigned z=0;z<30;++z)for(unsigned x=0;x<34;++x)curved.heights[z*34+x]=z<7?.02f*z*z:.98f;
    auto curvedAsset=TerrainAsset::create(curved);check(!curvedAsset->chunks()[0]->lods.empty(),"Flat regions beside curved cuts should still simplify");
    for(const auto& lod:curvedAsset->chunks()[0]->lods)for(std::size_t i=0;i<lod.mesh->indices.size();i+=3){
        const auto a=lod.mesh->vertices[lod.mesh->indices[i]].position,b=lod.mesh->vertices[lod.mesh->indices[i+1]].position,c=lod.mesh->vertices[lod.mesh->indices[i+2]].position;
        const auto center=(a+b+c)*(1.f/3);auto ground=curvedAsset->ground(center.x,center.z);
        check(ground&&std::abs(ground->position.y-center.y)<1e-5f,"Curved cut chunk displaced source surface");}
    auto dense=data;for(unsigned z=0;z<29;++z)for(unsigned x=0;x<33;++x)dense.holes[z*33+x]=(x+z)%2;
    auto denseAsset=TerrainAsset::create(dense);for(auto m:denseAsset->chunks())check(m->lods.empty(),"Checkerboard holes should refuse unsafe reduction");
    std::fill(data.holes.begin(),data.holes.end(),1);auto empty=TerrainAsset::create(data);for(auto m:empty->chunks())check(m->indices.empty()&&m->lods.empty(),"Empty chunk gained geometry");
    std::cout<<"PASS hole LOD: "<<source<<" -> "<<coarse<<" triangles; exact boundary edges, disjoint/concave/edge cuts, partial chunks, coverage, source/archive collision, prepared topology rejection\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
