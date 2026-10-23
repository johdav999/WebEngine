#include "engine/terrain_mesh.hpp"
#include "engine/water_region.hpp"
#include "engine/terrain.hpp"
#include "engine/scene_archive.hpp"
#include "engine/physics.hpp"
#include <iostream>
#include <cstring>
#include <limits>
using namespace engine;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,"Invalid lake/region accepted");}
std::shared_ptr<Mesh> floorMesh(){
    auto m=std::make_shared<Mesh>();m->materials={std::make_shared<Material>()};
    m->vertices={{{-4,0,-4},{0,1,0},{0,0}},{{-4,0,4},{0,1,0},{0,1}},{{4,0,4},{0,1,0},{1,1}},{{4,0,-4},{0,1,0},{1,0}},
                 {{-4,3,-4},{0,-1,0},{0,0}},{{-4,3,4},{0,-1,0},{0,1}},{{4,3,4},{0,-1,0},{1,1}},{{4,3,-4},{0,-1,0},{1,0}}};
    m->indices={0,1,2,0,2,3,4,6,5,4,7,6};m->parts={{0,12,0}};m->generateTangents();return m;
}
TerrainMeshLakeSettings settings(){TerrainMeshLakeSettings s;s.outline={{-1,0},{0,-1},{1,0},{0,1}};s.depth=.5f;s.waterLevel=-.08f;s.bankWidth=.3f;s.maxEdgeLength=.15f;return s;}
int main(){try{
    auto source=floorMesh();const auto vertices=source->vertices;const auto indices=source->indices;auto s=settings();auto lake=buildTerrainMeshLake(source,s);
    check(source->indices==indices&&!std::memcmp(vertices.data(),source->vertices.data(),vertices.size()*sizeof(Vertex)),"Source changed during lake preparation");
    check(lake.insertedVertices>0&&lake.terrainMesh->lodRecipe&&lake.terrainMesh->lodRecipe->levels==3&&lake.terrainMesh->lods.size()<=3&&!lake.waterMesh->indices.empty(),"Missing prepared lake geometry/LOD");
    for(unsigned i=4;i<8;++i)check(lake.terrainMesh->vertices[i].position.y==3,"Lake modified the ceiling");
    for(const auto& v:lake.waterMesh->vertices)check(v.position.y==0&&lake.region.contains({v.position.x,-.1f,v.position.z}),"Water spans dry outline");
    Scene scene;auto terrain=makeMeshTerrainActor("Basin",lake.terrainMesh,true);terrain->getComponent<MeshComponent>()->lodPolicy.cameraLevel=3;scene.adoptChild(std::move(terrain));
    auto actor=makeMeshTerrainActor("Pond",lake.waterMesh);actor->transform.position.y=lake.waterLevel;auto* surface=actor->getComponent<MeshComponent>();
    auto material=std::make_shared<Material>(*lake.waterMesh->materials[0]);material->water.underwaterStrength=1;surface->setMaterial(0,MaterialInstance(material));surface->addTag("Keep.Me");setWaterRegion(*surface,lake.region);
    scene.adoptChild(std::move(actor));scene.synchronize();
    auto bed=scene.physics().raycast({0,1,0},{0,-1,0},3),dry=scene.physics().raycast({2,1,0},{0,-1,0},3);
    check(bed&&std::abs(bed->point.y+.58f)<.002f&&dry&&std::abs(dry->point.y)<.001f,"Basin collision or dry bank changed");
    check(!surface->getComponent<ColliderComponent>(),"Water surface is solid");
    check(underwaterView(scene,{0,-.3f,0}).has_value(),"Pond camera not submerged");
    check(!underwaterView(scene,{.8f,-.3f,.8f})&&!underwaterView(scene,{0,-2,0}),"Water leaks into bounding-box corner or lower chamber");
    auto doc=SceneArchive::load(SceneArchive::save(scene));check(underwaterView(*doc.scene,{0,-.3f,0}).has_value()&&!underwaterView(*doc.scene,{0,-2,0}),"Finite region lost during scene load");
    auto restored=doc.scene->physics().raycast({0,1,0},{0,-1,0},3);check(restored&&std::abs(restored->point.y+.58f)<.002f,"Loaded basin collision changed");
    UnderwaterView view{Mat4::identity(),{-2,0,-2},{2,0,2},material->water,WaterRegion{s.outline,-.5f}};
    check(std::abs(underwaterDistance(view,{0,-.2f,0},{4,-.2f,0})-1)<1e-5f,"Polygon ray clipping wrong");
    check(std::abs(underwaterDistance(view,{0,-.2f,0},{0,-2,0})-.3f)<1e-5f,"Finite bottom ray clipping wrong");
    check(underwaterDistance(view,{.8f,-.2f,.8f},{2,-.2f,2})==0,"Dry corner ray fogged");
    surface->transform.rotation.y=.6f;surface->transform.scale={2,1,1.5f};auto transformed=transformPoint(surface->worldMatrix(),{0,-.2f,0});check(underwaterView(scene,transformed).has_value(),"Transformed region rejected");
    check(surface->hasTag("Keep.Me")&&waterRegion(*surface)->outline.size()==4,"Metadata replaced unrelated tags");
    auto invalid=s;invalid.outline={{-1,-1},{1,-1},{0,0},{1,1},{-1,1}};rejects([&]{buildTerrainMeshLake(source,invalid);});invalid=s;std::reverse(invalid.outline.begin(),invalid.outline.end());rejects([&]{buildTerrainMeshLake(source,invalid);});
    invalid=s;invalid.floorAnchor.y=1;rejects([&]{buildTerrainMeshLake(source,invalid);});invalid=s;invalid.maxWork=1;rejects([&]{buildTerrainMeshLake(source,invalid);});invalid=s;invalid.maxOutputTriangles=4;rejects([&]{buildTerrainMeshLake(source,invalid);});
    invalid=s;invalid.maxWaterTriangles=1;rejects([&]{buildTerrainMeshLake(source,invalid);});invalid=s;invalid.depth=std::numeric_limits<float>::quiet_NaN();rejects([&]{buildTerrainMeshLake(source,invalid);});
    auto locked=std::make_shared<Mesh>(*source);locked->lodRecipe=MeshLodRecipe{};locked->lodRecipe->lockedVertices.resize(locked->vertices.size(),1);
    auto lockedLake=buildTerrainMeshLake(locked,s);for(unsigned i=0;i<source->vertices.size();++i)check(!std::memcmp(&lockedLake.terrainMesh->vertices[i],&source->vertices[i],sizeof(Vertex)),"Remote LOD lock attributes changed");
    auto region=lake.region;region.bottom=1;rejects([&]{setWaterRegion(*surface,region);});check(waterRegion(*surface)->bottom==lake.region.bottom,"Rejected metadata replaced valid region");
    surface->addTag("WebEngine.WaterRegion.v99:bad");rejects([&]{waterRegion(*surface);});setWaterRegion(*surface,std::nullopt);check(!waterRegion(*surface)&&surface->hasTag("Keep.Me"),"Region removal changed unrelated metadata");
    auto stacked=floorMesh();for(unsigned i=4;i<8;++i){stacked->vertices[i].position.y=-.7f;stacked->vertices[i].normal={0,1,0};}for(unsigned i=6;i<12;i+=3)std::swap(stacked->indices[i+1],stacked->indices[i+2]);auto isolated=buildTerrainMeshLake(stacked,s);for(unsigned i=4;i<8;++i)check(isolated.terrainMesh->vertices[i].position.y==-.7f,"Lake modified disconnected lower floor");
    check(source->indices==indices&&!std::memcmp(vertices.data(),source->vertices.data(),vertices.size()*sizeof(Vertex)),"Rejected lake touched source");
    std::cout<<"PASS terrain mesh lakes: basin/water geometry, floor/roof isolation, collision, bounded polygon/bottom optics, transforms, metadata/archive, LOD and atomic rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
