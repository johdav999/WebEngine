#include "engine/terrain_cliff.hpp"
#include "engine/scene_archive.hpp"
#include "engine/physics.hpp"
#include "engine/character_motor_component.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
using namespace engine;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid cliff accepted");}
std::shared_ptr<const TerrainAsset> source(){TerrainData d;d.samplesX=d.samplesZ=33;d.chunkCells=8;d.heights.resize(1089);d.holes.resize(1024);d.holes[2*32+2]=1;
    for(unsigned z=0;z<33;++z)for(unsigned x=0;x<33;++x){const float t=std::clamp((float(z)-13.5f)/5,0.f,1.f);d.heights[z*33+x]=5*t*t*(3-2*t);}
    return TerrainAsset::create(std::move(d));}
void topology(const Mesh& m,const TerrainCliffResult& r){std::map<std::pair<unsigned,unsigned>,unsigned> edges;
    for(std::size_t i=0;i<m.indices.size();i+=3){const auto a=m.vertices[m.indices[i]].position,b=m.vertices[m.indices[i+1]].position,c=m.vertices[m.indices[i+2]].position;
        check(cross(b-a,c-a).y>0,"Cliff triangle folded or degenerated");
        for(unsigned j=0;j<3;++j){auto a=m.indices[i+j],b=m.indices[i+(j+1)%3];if(a>b)std::swap(a,b);check(++edges[{a,b}]<=2,"Non-manifold cliff edge");}}
    unsigned boundary=0;for(const auto& [e,n]:edges)if(n==1){++boundary;const auto a=m.vertices[e.first].position,b=m.vertices[e.second].position;
        check((a.x==r.cutX&&b.x==r.cutX)||(a.x==r.cutX+r.cellsX&&b.x==r.cutX+r.cellsX)||(a.z==r.cutZ&&b.z==r.cutZ)||(a.z==r.cutZ+r.cellsZ&&b.z==r.cutZ+r.cellsZ),"Cliff has an internal crack");
        check(std::abs(a.x-b.x)+std::abs(a.z-b.z)==1,"Cliff perimeter subdivided or skipped a source edge");}
    check(boundary==2*(r.cellsX+r.cellsZ),"Cliff perimeter does not match source grid");}
void seams(Scene& scene,const TerrainAsset& source,const TerrainCliffResult& r){
    for(float off:{-.03f,-.001f,.001f,.03f}){
        for(unsigned z=r.cutZ;z<r.cutZ+r.cellsZ;++z)for(float x:{float(r.cutX)+off,float(r.cutX+r.cellsX)+off}){
            const float pz=z+.37f;auto hit=scene.physics().raycast({x,12,pz},{0,-1,0},20);const auto original=source.ground(x,pz);
            check(hit&&original&&std::abs(hit->point.y-original->position.y)<.002f,"Side collision seam is not continuous");}
        for(unsigned x=r.cutX;x<r.cutX+r.cellsX;++x)for(float z:{float(r.cutZ)+off,float(r.cutZ+r.cellsZ)+off}){
            const float px=x+.37f;auto hit=scene.physics().raycast({px,12,z},{0,-1,0},20);const auto original=source.ground(px,z);
            check(hit&&original&&std::abs(hit->point.y-original->position.y)<.002f,"Upper/lower collision seam is not continuous");}}
}
int main(){try{
    const auto a=source();const auto heights=a->data().heights;const auto oldHoles=a->data().holes;const std::array<Vec2,2> path{{{8,16},{24,16}}};
    auto rock=std::make_shared<Material>();rock->baseColor={.55f,.48f,.36f};
    auto r=buildTerrainMeshCliff(*a,path,rock);check(r.terrain.before==a&&r.terrain.holesChanged&&r.outputTriangles>100,"Cliff edit missing");
    check(r.terrain.after->data().hole(2,2)&&a->data().heights==heights&&a->data().holes==oldHoles,"Cliff changed its source or lost unrelated holes");
    topology(*r.mesh,r);check(r.mesh->lodRecipe&&r.mesh->lods.size()>0,"Cliff LOD missing");
    for(const auto& v:r.mesh->vertices){check(std::isfinite(v.normal.y)&&std::abs(dot(v.normal,v.normal)-1)<.001f,"Invalid cliff normal");
        const bool edge=v.position.x==r.cutX||v.position.x==r.cutX+r.cellsX||v.position.z==r.cutZ||v.position.z==r.cutZ+r.cellsZ;
        if(edge){const auto hit=a->ground(v.position.x,v.position.z);check(hit&&std::abs(v.position.y-hit->position.y)<1e-6f&&([&]{for(const auto& chunk:a->chunks())for(const auto& q:chunk->vertices)if(dot(q.position-v.position,q.position-v.position)<1e-10f)return dot(v.normal-q.normal,v.normal-q.normal)<1e-9f;return false;})(),"Boundary height/normal mismatch");check(v.uv.x>.99f,"Boundary material does not match ground");}}
    for(const auto& lod:r.mesh->lods){
        for(unsigned i=0;i<r.mesh->vertices.size();++i)if(r.mesh->lodRecipe->lockedVertices[i]){bool found=false;const auto& v=r.mesh->vertices[i];for(const auto& q:lod.mesh->vertices)if(!std::memcmp(&v,&q,sizeof(Vertex))){found=true;break;}check(found,"Cliff LOD lost a protected seam or silhouette vertex");}}
    Scene scene;auto ground=makeTerrainActor("Ground",a,true);auto* live=ground.get();scene.adoptChild(std::move(ground));
    auto cliff=makeMeshTerrainActor("Cliff",r.mesh,true);auto* part=cliff->getComponent<MeshComponent>();part->lodPolicy.cameraLevel=3;scene.adoptChild(std::move(cliff));
    applyTerrainEdit(*live,r.terrain);scene.synchronize();seams(scene,*a,r);
    auto face=scene.physics().raycast({16,2.5f,12},{0,0,1},8);check(face&&std::abs(face->point.z-16)<.025f&&std::abs(face->normal.y)<.15f,"Requested steep cliff collision missing");
    check(!terrainGround(scene,16,16)&&scene.physics().raycast({16,10,16},{0,-1,0},15),"Heightfield was not replaced by cliff collision");
    // Walking along the upper shoulder crosses BOTH ends of the replacement.
    auto& walker=scene.spawn("Walker");walker.transform.position={float(r.cutX)-.75f,5.02f,float(r.cutZ+r.cellsZ)-.25f};
    auto& motor=walker.addComponent<CharacterMotorComponent>();scene.synchronize();motor.teleport(walker.transform.position);
    for(unsigned i=0;i<1000&&walker.transform.position.x<float(r.cutX+r.cellsX)+.5f;++i){motor.submitMovement({{2,0,0},false});scene.fixedUpdate(1.f/60);}
    check(walker.transform.position.x>r.cutX+r.cellsX+.4f&&std::abs(walker.transform.position.y-5)<.08f,"Character snagged at cliff/terrain collar");
    auto saved=SceneArchive::load(SceneArchive::save(scene));seams(*saved.scene,*a,r);
    auto restored=saved.scene->physics().raycast({16,2.5f,12},{0,0,1},8);check(restored&&std::abs(restored->point.z-face->point.z)<.001f,"Saved cliff collision changed");
    auto repeat=buildTerrainMeshCliff(*a,path,rock);check(r.mesh->indices==repeat.mesh->indices&&r.mesh->vertices.size()==repeat.mesh->vertices.size()&&!std::memcmp(r.mesh->vertices.data(),repeat.mesh->vertices.data(),r.mesh->vertices.size()*sizeof(Vertex)),"Cliff generation is not deterministic");
    TerrainCliffSettings flat;flat.lodLevels=0;const std::array<Vec2,3> bend{{{8,16},{16,17},{24,16}}};auto bent=buildTerrainMeshCliff(*a,bend,rock,flat);topology(*bent.mesh,bent);
    // Extra authoring samples on a forward path are not self-approaches.
    const std::array<Vec2,5> sampled{{{8,16},{12,16},{16,16},{20,16},{24,16}}};
    auto sampledResult=buildTerrainMeshCliff(*a,sampled,rock,flat);topology(*sampledResult.mesh,sampledResult);
    std::array<Vec2,2> bad=path;bad[1]=bad[0];rejects([&]{buildTerrainMeshCliff(*a,bad,rock);});bad=path;bad[0].x=1;rejects([&]{buildTerrainMeshCliff(*a,bad,rock);});
    bad=path;bad[1].y=std::numeric_limits<float>::quiet_NaN();rejects([&]{buildTerrainMeshCliff(*a,bad,rock);});
    const std::array<Vec2,4> crossing{{{8,8},{24,24},{8,24},{24,8}}};rejects([&]{buildTerrainMeshCliff(*a,crossing,rock);});
    auto invalid=flat;invalid.faceAngle=90;rejects([&]{buildTerrainMeshCliff(*a,path,rock,invalid);});invalid=flat;invalid.endFade=10;rejects([&]{buildTerrainMeshCliff(*a,path,rock,invalid);});
    invalid=flat;invalid.maxWork=1;rejects([&]{buildTerrainMeshCliff(*a,path,rock,invalid);});invalid=flat;invalid.maxOutputTriangles=10;rejects([&]{buildTerrainMeshCliff(*a,path,rock,invalid);});
    auto textured=std::make_shared<Material>();textured->baseColorTexture=std::make_shared<Texture>();rejects([&]{buildTerrainMeshCliff(*a,path,textured);});
    rejects([&]{buildTerrainMeshCliff(*r.terrain.after,path,rock);});check(a->data().holes==oldHoles&&a->data().heights==heights,"Rejected preparation modified source");
    std::cout<<"PASS cliff: open path, exact perimeter topology/heights/normals/material, steep face, LOD locks, collision, motor seam crossing, archive, determinism and atomic rejection ("<<r.outputTriangles<<" triangles)\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
