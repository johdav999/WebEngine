#include "engine/terrain_mesh.hpp"
#include "engine/terrain.hpp"
#include "engine/scene_archive.hpp"
#include "engine/physics.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <tuple>
using namespace engine;
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Invalid formation accepted");}
std::shared_ptr<Mesh> sheets(){
    auto m=std::make_shared<Mesh>();m->materials={std::make_shared<Material>(),std::make_shared<Material>()};
    for(unsigned layer=0;layer<2;++layer){const auto base=unsigned(m->vertices.size()),first=unsigned(m->indices.size());
        for(unsigned z=0;z<=12;++z)for(unsigned x=0;x<=12;++x)m->vertices.push_back({{float(x)*.5f-3,float(layer)*4,float(z)*.5f-3},{0,layer?-1.f:1.f,0},{x/12.f,z/12.f}});
        for(unsigned z=0;z<12;++z)for(unsigned x=0;x<12;++x){const auto a=base+z*13+x,b=a+1,c=a+13,d=c+1;
            if(layer)m->indices.insert(m->indices.end(),{a,b,c,b,d,c});else m->indices.insert(m->indices.end(),{a,c,b,b,c,d});}
        m->parts.push_back({first,unsigned(m->indices.size())-first,layer});}
    m->generateTangents();return m;
}
void topology(const Mesh& m){
    using Key=std::tuple<float,float,float>;using Edge=std::pair<Key,Key>;std::map<Edge,unsigned> edges;
    for(unsigned i=0;i<m.indices.size();i+=3){const auto a=m.vertices[m.indices[i]].position,b=m.vertices[m.indices[i+1]].position,c=m.vertices[m.indices[i+2]].position;
        check(dot(cross(b-a,c-a),cross(b-a,c-a))>1e-16f,"Degenerate output triangle");
        for(unsigned j=0;j<3;++j){const auto p=m.vertices[m.indices[i+j]].position,q=m.vertices[m.indices[i+(j+1)%3]].position;Key a{p.x,p.y,p.z},b{q.x,q.y,q.z};if(b<a)std::swap(a,b);++edges[{a,b}];}}
    unsigned boundary=0;for(const auto& [edge,count]:edges){check(count<=2,"Non-manifold extrusion");if(count==1)++boundary;}
    check(boundary==96,"Formation introduced a crack or changed the outer boundary");
}
void tips(const Mesh& m){for(const auto tip:{Vec3{-.8f,1.4f,.13f},Vec3{.8f,2.6f,-.17f}}){bool found=false;for(const auto& v:m.vertices)if(dot(v.position-tip,v.position-tip)<1e-9f)found=true;check(found,"Formation tip lost from base/LOD");}}
int main(){try{
    auto source=sheets();const auto originalVertices=source->vertices;const auto originalIndices=source->indices;
    const std::array<TerrainMeshFormation,2> formations{{{{-.8f,0,.13f},{0,1,0},1.4f,.65f,{},.1f,17},{{.8f,4,-.17f},{0,-1,0},1.4f,.65f,{},.1f,83}}};
    auto built=buildTerrainMeshFormations(source,formations);check(built.formations==2&&built.insertedVertices>0,"Missing refined formations");
    check(source->indices==originalIndices&&!std::memcmp(source->vertices.data(),originalVertices.data(),source->vertices.size()*sizeof(Vertex)),"Source mesh mutated");
    topology(*built.mesh);tips(*built.mesh);check(built.mesh->parts.size()==2&&built.mesh->parts[1].materialIndex==1,"Material partitions lost");
    check(built.mesh->lodRecipe&&built.mesh->lods.size()==3,"Formation LOD missing");for(const auto& lod:built.mesh->lods)tips(*lod.mesh);
    for(const auto& v:built.mesh->vertices){check(std::isfinite(v.normal.x)&&std::abs(dot(v.normal,v.normal)-1)<.001f,"Invalid normals");
        check(std::abs(v.uv.x-(v.position.x+3)/6)<1e-5f&&std::abs(v.uv.y-(v.position.z+3)/6)<1e-5f,"UV interpolation changed");}
    Scene scene;auto actor=makeMeshTerrainActor("Formations",built.mesh,true);actor->getComponent<MeshComponent>()->lodPolicy.cameraLevel=3;scene.adoptChild(std::move(actor));scene.synchronize();
    auto floor=scene.physics().raycast({-.8f,3,.13f},{0,-1,0},4),roof=scene.physics().raycast({.8f,1,-.17f},{0,1,0},4);
    check(floor&&std::abs(floor->point.y-1.4f)<.001f&&roof&&std::abs(roof->point.y-2.6f)<.001f,"Full-detail formation collision missing");
    auto archive=SceneArchive::load(SceneArchive::save(scene));auto assets=SceneArchive::collectAssets(*archive.scene);check(assets.meshes.size()==1,"Archived formation mesh missing");
    check(assets.meshes[0]->indices==built.mesh->indices&&assets.meshes[0]->lods.size()==3,"Archive changed topology/LOD");tips(*assets.meshes[0]);
    auto restored=archive.scene->physics().raycast({-.8f,3,.13f},{0,-1,0},4);check(restored&&std::abs(restored->point.y-1.4f)<.001f,"Archive lost formation collision");
    auto repeat=buildTerrainMeshFormations(source,formations);check(repeat.mesh->indices==built.mesh->indices&&!std::memcmp(repeat.mesh->vertices.data(),built.mesh->vertices.data(),built.mesh->vertices.size()*sizeof(Vertex)),"Seeded generation is not deterministic");
    // A nearby disconnected, same-facing sheet lies inside the support sphere.
    auto stacked=sheets();for(unsigned i=169;i<stacked->vertices.size();++i){stacked->vertices[i].position.y=.1f;stacked->vertices[i].normal={0,1,0};}
    for(unsigned i=stacked->parts[1].firstIndex;i<stacked->indices.size();i+=3)std::swap(stacked->indices[i+1],stacked->indices[i+2]);
    TerrainMeshFormationSettings noLod;noLod.lodLevels=0;auto isolated=buildTerrainMeshFormations(stacked,{formations.data(),1},noLod);
    for(unsigned i=169;i<338;++i)check(isolated.mesh->vertices[i].position.y==.1f,"Disconnected sheet was deformed");
    auto bend=formations[0];bend.bend={.15f,0,.05f};auto bent=buildTerrainMeshFormations(source,{&bend,1},noLod);bool bentTip=false;
    for(const auto& v:bent.mesh->vertices)if(dot(v.position-Vec3{-.65f,1.4f,.18f},v.position-Vec3{-.65f,1.4f,.18f})<1e-9f)bentTip=true;check(bentTip,"Bend was not applied to tip");
    auto changed=formations;changed[0].seed++;auto variant=buildTerrainMeshFormations(source,changed,noLod);check(std::memcmp(variant.mesh->vertices.data(),built.mesh->vertices.data(),std::min(variant.mesh->vertices.size(),built.mesh->vertices.size())*sizeof(Vertex))!=0,"Seed has no shape effect");
    // Exact edge/vertex anchors and arbitrary surface orientation.
    for(const auto anchor:{Vec3{0,0,0},Vec3{.25f,0,.25f}}){auto f=formations[0];f.anchor=anchor;auto formed=buildTerrainMeshFormations(source,{&f,1},noLod);topology(*formed.mesh);}
    auto almostEdge=formations[0];almostEdge.anchor={-.499999f,0,.03125f};
    auto stable=buildTerrainMeshFormations(source,{&almostEdge,1},noLod);topology(*stable.mesh);
    Scene edgeScene;edgeScene.adoptChild(makeMeshTerrainActor("Near edge",stable.mesh,true));edgeScene.synchronize();
    auto edgeHit=edgeScene.physics().raycast({almostEdge.anchor.x,2,almostEdge.anchor.z},{0,-1,0},3);
    check(edgeHit&&std::abs(edgeHit->point.y-almostEdge.length)<.002f,"Near-edge anchor produced unstable tip collision");
    auto rotated=sheets();for(auto& v:rotated->vertices){v.position={v.position.y,v.position.z,v.position.x};v.normal={v.normal.y,v.normal.z,v.normal.x};}
    auto sideways=formations[0];sideways.anchor={0,.13f,-.8f};sideways.direction={1,0,0};auto wall=buildTerrainMeshFormations(rotated,{&sideways,1},noLod);
    bool sidewaysTip=false;for(const auto& v:wall.mesh->vertices)if(dot(v.position-Vec3{1.4f,.13f,-.8f},v.position-Vec3{1.4f,.13f,-.8f})<1e-9f)sidewaysTip=true;check(sidewaysTip,"Arbitrary formation direction failed");
    // Material/UV attribute splits must refine without cracks or normal seams.
    auto split=std::make_shared<Mesh>();split->materials=source->materials;split->parts=source->parts;
    for(auto id:source->indices){split->indices.push_back(unsigned(split->vertices.size()));split->vertices.push_back(source->vertices[id]);}
    auto splitResult=buildTerrainMeshFormations(split,formations,noLod);topology(*splitResult.mesh);
    std::map<std::tuple<float,float,float>,Vec3> joinedNormals;
    for(const auto& vertex:splitResult.mesh->vertices){const auto p=vertex.position;auto [it,inserted]=joinedNormals.emplace(std::tuple{p.x,p.y,p.z},vertex.normal);
        if(!inserted)check(dot(it->second-vertex.normal,it->second-vertex.normal)<1e-8f,"Attribute split introduced a shading seam");}

    auto invalid=formations[0];invalid.direction={};rejects([&]{buildTerrainMeshFormations(source,{&invalid,1});});invalid=formations[0];invalid.length=std::numeric_limits<float>::quiet_NaN();rejects([&]{buildTerrainMeshFormations(source,{&invalid,1});});
    invalid=formations[0];invalid.anchor.y=1;rejects([&]{buildTerrainMeshFormations(source,{&invalid,1});});invalid=formations[0];invalid.direction={0,-1,0};rejects([&]{buildTerrainMeshFormations(source,{&invalid,1});});
    invalid=formations[0];invalid.anchor.x=2.9f;rejects([&]{buildTerrainMeshFormations(source,{&invalid,1});});
    auto overlap=formations;overlap[1]=overlap[0];rejects([&]{buildTerrainMeshFormations(source,overlap);});
    TerrainMeshFormationSettings budget;budget.maxWork=1;rejects([&]{buildTerrainMeshFormations(source,formations,budget);});budget={};budget.maxOutputTriangles=unsigned(source->indices.size()/3);rejects([&]{buildTerrainMeshFormations(source,formations,budget);});
    auto seamSource=std::make_shared<Mesh>(*source);seamSource->lodRecipe=MeshLodRecipe{};seamSource->lodRecipe->lockedVertices.resize(source->vertices.size());
    const unsigned seamVertex=6*13+6;seamSource->lodRecipe->lockedVertices[seamVertex]=1;
    auto seamResult=buildTerrainMeshFormations(seamSource,{formations.data(),1});
    check(!std::memcmp(&seamResult.mesh->vertices[seamVertex],&source->vertices[seamVertex],sizeof(Vertex)),"Protected seam attributes changed");
    check(seamResult.mesh->lodRecipe->lockedVertices[seamVertex]==1,"Inherited seam lock lost");
    auto protectedSource=std::make_shared<Mesh>(*source);protectedSource->lodRecipe=MeshLodRecipe{};protectedSource->lodRecipe->lockedVertices.resize(source->vertices.size(),1);rejects([&]{buildTerrainMeshFormations(protectedSource,formations);});
    auto skin=std::make_shared<Mesh>(*source);skin->skin.resize(skin->vertices.size());rejects([&]{buildTerrainMeshFormations(skin,formations);});
    check(source->indices==originalIndices&&!std::memcmp(source->vertices.data(),originalVertices.data(),source->vertices.size()*sizeof(Vertex)),"Rejected preparation touched source");
    std::cout<<"PASS terrain mesh formations: continuous attachment, topology, materials/UVs, exact tips/LOD, collision/archive, disconnected sheets, bend/seed and atomic rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
