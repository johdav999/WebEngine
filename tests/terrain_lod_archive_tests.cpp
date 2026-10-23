#include "../examples/hybrid_terrain/scene.hpp"
#include "engine/mesh_optimizer.hpp"
#include <cstring>
#include <iostream>
using namespace engine;
using hybrid_example::check;
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Invalid checkpoint accepted");}
void repair(std::vector<std::byte>& bytes){
    const std::uint64_t size=bytes.size()-24;for(int i=0;i<8;++i)bytes[12+i]=std::byte((size>>(8*i))&255);
    std::uint32_t crc=0xffffffff;for(std::size_t i=24;i<bytes.size();++i){crc^=std::to_integer<unsigned>(bytes[i]);for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    crc=~crc;for(int i=0;i<4;++i)bytes[20+i]=std::byte((crc>>(8*i))&255);
}
void equalMesh(const Mesh& a,const Mesh& b){
    check(a.vertices.size()==b.vertices.size()&&a.indices==b.indices&&a.parts.size()==b.parts.size(),"Base geometry changed during load");
    check(!std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(Vertex)),"Vertex attributes changed during load");
    for(std::size_t i=0;i<a.parts.size();++i)check(a.parts[i].firstIndex==b.parts[i].firstIndex&&a.parts[i].indexCount==b.parts[i].indexCount&&a.parts[i].materialIndex==b.parts[i].materialIndex,"Material partition changed");
    check(a.lods.size()==b.lods.size(),"Derived LOD count changed");
    for(std::size_t i=0;i<a.lods.size();++i){check(a.lods[i].maxPixels==b.lods[i].maxPixels,"LOD threshold changed");equalMesh(*a.lods[i].mesh,*b.lods[i].mesh);}
}
int main(){try{
    hybrid_example::Demo demo;auto world=demo.make();demo.editEntrance({4,12,3.5f});
    demo.setTool(1);demo.beginStroke();demo.stamp({7.5f,16.5f});demo.endStroke();demo.setView(3);demo.detailLevels(3,1);
    const auto source=demo.cavePart->mesh;check(source->lodRecipe&&source->lods.size()==3,"Missing source recipe");
    auto bytes=demo.saveCheckpoint();
    for(int cycle=0;cycle<3;++cycle){auto document=SceneArchive::load(bytes);hybrid_example::Demo restored;restored.bindCheckpoint(document);
        equalMesh(*source,*restored.cavePart->mesh);check(restored.entrance==demo.entrance&&restored.manualHoles==demo.manualHoles&&restored.history.empty(),"Application metadata was not restored");
        check(restored.cavePart->mesh->lodRecipe->lockedVertices==source->lodRecipe->lockedVertices,"Seam locks changed");
        check(restored.cavePart->lodPolicy.cameraLevel==3&&restored.cavePart->lodPolicy.shadowLevel==1,"Detail policy lost");
        check(!terrainGround(*restored.world,-8.5f,4.5f)&&!restored.world->physics().raycast({-8.5f,12,4.5f},{0,-1,0},20),"Manual hole collision was filled");
        restored.verifySeams(*restored.world);restored.walk(true);
        for(int i=0;i<600&&restored.moving;++i){restored.update();restored.world->fixedUpdate(1.f/60);}
        check(!restored.moving&&restored.walker->transform.position.z>9.8f,"Loaded cave is not traversable");
        restored.editEntrance({3,10,4});check(restored.paintedCells()==demo.paintedCells()&&restored.undo(),"Loaded terrain is not editable");
        equalMesh(*source,*restored.cavePart->mesh);bytes=restored.saveCheckpoint();
    }
    AssetLibrary assets;assets.meshes={source,source};auto loaded=SceneArchive::loadAssets(SceneArchive::saveAssets(assets));
    check(loaded.meshes[0]==loaded.meshes[1],"Regeneration broke shared asset identity");equalMesh(*source,*loaded.meshes[0]);
    auto stale=std::make_shared<Mesh>(*source);stale->lods.clear();assets.meshes={stale};
    equalMesh(*source,*SceneArchive::loadAssets(SceneArchive::saveAssets(assets)).meshes[0]);
    auto optout=std::make_shared<Mesh>(*source);optout->lodRecipe.reset();assets.meshes={optout};
    check(SceneArchive::loadAssets(SceneArchive::saveAssets(assets)).meshes[0]->lods.empty(),"Unmarked meshes gained an implicit recipe");
    // A v35 mesh has the same base codec, without the new optional-recipe byte.
    const std::string marker="legacy-mesh-end-marker";optout->warnings={marker};auto legacy=SceneArchive::saveAssets(assets);
    const auto* begin=reinterpret_cast<const std::byte*>(marker.data());auto pos=std::search(legacy.begin(),legacy.end(),begin,begin+marker.size());
    check(pos!=legacy.end()&&*(pos+marker.size())==std::byte{0},"Legacy fixture marker missing");legacy.erase(pos+marker.size());legacy[8]=std::byte{35};repair(legacy);
    auto legacyMesh=SceneArchive::loadAssets(legacy).meshes[0];check(!legacyMesh->lodRecipe&&legacyMesh->lods.empty()&&legacyMesh->indices==source->indices,"Version 35 base mesh compatibility lost");
    // Malformed recipes with a correct archive checksum must fail during decode.
    assets.meshes={source};auto encoded=SceneArchive::saveAssets(assets);std::vector<std::byte> recipe;
    auto integer=[&](std::uint32_t value){for(int i=0;i<4;++i)recipe.push_back(std::byte((value>>(i*8))&255));};
    integer(1);integer(3);recipe.push_back(std::byte{1});integer(std::uint32_t(source->vertices.size()));
    for(auto lock:source->lodRecipe->lockedVertices)recipe.push_back(std::byte(lock));
    auto at=std::search(encoded.begin(),encoded.end(),recipe.begin(),recipe.end());check(at!=encoded.end(),"Recipe fixture missing");const auto offset=at-encoded.begin();
    for(const auto index:{0,4,9,13}){auto corrupt=encoded;corrupt[offset+index]=std::byte{255};repair(corrupt);rejects([&]{SceneArchive::loadAssets(corrupt);});}
    auto bad=std::make_shared<Mesh>(*source);assets.meshes={bad};const auto oldLod=bad->lods[0].mesh;
    for(int which=0;which<5;++which){bad->lodRecipe=source->lodRecipe;
        if(which==0)bad->lodRecipe->version=2;if(which==1)bad->lodRecipe->levels=4;
        if(which==2)bad->lodRecipe->lockedVertices.pop_back();if(which==3)bad->lodRecipe->lockedVertices[0]=2;
        if(which==4)bad->lodRecipe->levels=0;
        rejects([&]{MeshOptimizer::regenerateLods(*bad);});rejects([&]{SceneArchive::saveAssets(assets);});check(bad->lods[0].mesh==oldLod,"Rejected regeneration replaced valid cache");
    }
    bad->lodRecipe=source->lodRecipe;rejects([&]{MeshOptimizer::optimize(*bad);});
    ArchiveLimits limits;limits.maxAllocatedBytes=source->vertices.size()*512;
    rejects([&]{SceneArchive::load(demo.saveCheckpoint(),limits);});
    bytes=demo.saveCheckpoint();bytes.back()^=std::byte{1};rejects([&]{SceneArchive::load(bytes);});check(demo.cavePart->mesh==source,"Failed load touched live mesh");
    auto doc=SceneArchive::load(demo.saveCheckpoint());doc.applicationData.clear();rejects([&]{hybrid_example::Demo candidate;candidate.bindCheckpoint(doc);});
    std::cout<<"PASS automatic LOD archives: exact base/derived geometry, seam locks, shared identity, holes/collision, repeated saves, traversal/editing, opt-out and atomic rejection\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
