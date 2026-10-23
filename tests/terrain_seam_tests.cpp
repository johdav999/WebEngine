#include "engine/terrain_seam.hpp"
#include "engine/scene.hpp"
#include "engine/physics.hpp"
#include "engine/scene_archive.hpp"
#include <iostream>
#include <map>
#include <set>
using namespace engine;
void check(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
template<class F>void reject(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,"Invalid seam accepted");}
auto boundary(const Mesh& mesh){
    using Point=std::array<float,3>;using Edge=std::pair<Point,Point>;std::map<Edge,unsigned> counts;
    for(std::size_t i=0;i<mesh.indices.size();i+=3)for(unsigned e=0;e<3;++e){auto a=mesh.vertices[mesh.indices[i+e]].position,b=mesh.vertices[mesh.indices[i+(e+1)%3]].position;Point p{a.x,a.y,a.z},q{b.x,b.y,b.z};if(q<p)std::swap(p,q);++counts[{p,q}];}
    std::set<Edge> result;for(auto [edge,n]:counts)if(n==1)result.insert(edge);return result;
}
int main(){try{
    TerrainData d;d.samplesX=d.samplesZ=9;d.chunkCells=4;d.heights.resize(81);d.holes.resize(64);auto grass=std::make_shared<Material>();grass->baseColor={.2f,.5f,.1f};d.material=grass;
    for(unsigned z=2;z<6;++z)for(unsigned x=2;x<6;++x)d.holes[z*8+x]=1;
    auto terrain=TerrainAsset::create(d);auto mesh=std::make_shared<Mesh>();auto rock=std::make_shared<Material>();rock->baseColor={.6f,.3f,.1f};rock->roughness=.8f;
    mesh->materials={rock};mesh->vertices={{{2.15f,.08f,2.15f},{0,1,0},{}},{{2.15f,.08f,5.85f},{0,1,0},{}},{{5.85f,.08f,5.85f},{0,1,0},{}},{{5.85f,.08f,2.15f},{0,1,0},{}}};mesh->indices={0,1,2,0,2,3};mesh->parts={{0,6,0}};
    const auto sourceVertices=mesh->vertices;auto result=buildTerrainSeam(*terrain,mesh);check(result.boundaryEdges==16&&result.weldedVertices>0&&result.insertedSamples>0,"Boundary inference/welding/subdivision missing");
    check(mesh->vertices[0].position.x==sourceVertices[0].position.x&&mesh->indices.size()==6,"Builder mutated source");
    for(int z=2;z<=6;++z)for(int x=2;x<=6;++x)if(x==2||x==6||z==2||z==6){bool found=false;for(auto v:result.mesh->vertices)if(dot(v.position-Vec3{float(x),0,float(z)},v.position-Vec3{float(x),0,float(z)})<1e-10f){found=true;check(v.uv.x>.99f&&v.normal.y>.999f,"Outer seam material/normal mismatch");break;}check(found,"Source boundary sample omitted");}
    float low=1,high=0;for(auto v:result.mesh->vertices){low=std::min(low,v.uv.x);high=std::max(high,v.uv.x);check(std::isfinite(v.normal.x)&&std::isfinite(v.normal.y),"Nonfinite seam normal");}
    check(low<.01f&&high>.99f,"Blend has no interior/exterior endpoints");
    check(!result.mesh->lods.empty()&&result.mesh->lods.back().mesh->indices.size()<result.mesh->indices.size(),"Seam interior did not simplify");
    for(const auto& lod:result.mesh->lods)check(boundary(*lod.mesh)==boundary(*result.mesh),"Simplification changed open boundary edge segments");
    for(const auto& lod:result.mesh->lods)for(const auto& v:result.mesh->vertices)if(v.uv.x>.5f/256+1e-7f){
        bool found=false;for(const auto& coarse:lod.mesh->vertices)if(coarse.position.x==v.position.x&&coarse.position.y==v.position.y&&coarse.position.z==v.position.z&&coarse.uv.x==v.uv.x&&coarse.normal.x==v.normal.x&&coarse.normal.y==v.normal.y&&coarse.normal.z==v.normal.z){found=true;break;}
        check(found,"LOD removed a blend-band vertex or changed its attributes");}
    TerrainSeamSettings fullOnly;fullOnly.lodLevels=0;check(buildTerrainSeam(*terrain,mesh,fullOnly).mesh->lods.empty(),"LOD opt-out ignored");
    auto& material=*result.mesh->materials[0];check(material.layers.enabled&&material.layers.triplanar&&material.layers.weights->pixels.front()==0&&material.layers.weights->pixels[255*4]==255,"Portable material blend missing");
    Scene scene;scene.adoptChild(makeTerrainActor("Ground",terrain,true));scene.adoptChild(makeMeshTerrainActor("Joined",result.mesh,true));scene.synchronize();
    for(float z:{2.01f,3.f,4.f,5.99f})for(float x:{1.99f,2.001f,2.04f,2.15f,5.85f,5.96f,5.999f,6.01f}){auto hit=scene.physics().raycast({x,2,z},{0,-1,0},4);check(hit&&std::abs(hit->point.y)<.1f,"Crack in physical seam");}
    auto archive=SceneArchive::load(SceneArchive::save(scene));check(archive.scene->physics().raycast({2.01f,2,4},{0,-1,0},4).has_value(),"Archive lost seam collision");
    auto tiny=TerrainSeamSettings{};tiny.maxOutputTriangles=1;reject([&]{buildTerrainSeam(*terrain,mesh,tiny);});tiny={};tiny.maxWork=1;reject([&]{buildTerrainSeam(*terrain,mesh,tiny);});tiny={};tiny.weldDistance=.6f;reject([&]{buildTerrainSeam(*terrain,mesh,tiny);});
    auto invalid=std::make_shared<Mesh>(*mesh);auto mapped=std::make_shared<Material>(*rock);mapped->baseColorTexture=Texture::white();invalid->materials={mapped};reject([&]{buildTerrainSeam(*terrain,invalid);});
    auto far=std::make_shared<Mesh>(*mesh);for(auto& v:far->vertices)v.position.x+=20;auto unmatched=buildTerrainSeam(*terrain,far);check(unmatched.weldedVertices==0&&unmatched.insertedSamples==0,"Unrelated geometry snapped to terrain");
    auto solid=d;solid.holes.clear();check(buildTerrainSeam(*TerrainAsset::create(solid),mesh).mesh==mesh,"Solid source should share mesh");
    auto moved=std::make_shared<Mesh>(*mesh);const Vec3 origin{10,2,-3};for(auto& v:moved->vertices)v.position=v.position+origin;TerrainSeamSettings settings;settings.terrainOrigin=origin;
    auto translated=buildTerrainSeam(*terrain,moved,settings);check(translated.outputTriangles==result.outputTriangles,"Translation changed seam topology");
    auto underside=std::make_shared<Mesh>(*mesh);for(auto& v:underside->vertices)v.normal={0,-1,0};
    auto below=buildTerrainSeam(*terrain,underside);check(below.weldedVertices==0&&below.insertedSamples==0,"Opposing underside welded to source");
    for(auto v:below.mesh->vertices)check(std::abs(v.position.y-.08f)<1e-6f&&v.normal.y<-.999f&&v.uv.x<.01f,"Underside geometry or shading changed");
    auto tiled=std::make_shared<Material>(*rock);tiled->layers.enabled=tiled->layers.triplanar=true;tiled->layers.triplanarScale=2;tiled->uvScale={3,4};tiled->uvOffset={.1f,.2f};
    tiled->layers.layers[0].uvScale={5,6};tiled->layers.layers[0].uvOffset={.3f,.4f};tiled->layers.layers[0].normalHeightTexture=Texture::flatNormal();tiled->layers.layers[0].normalScale=.7f;
    auto tiledMesh=std::make_shared<Mesh>(*mesh);tiledMesh->materials={tiled};auto mappedResult=buildTerrainSeam(*terrain,tiledMesh);const auto& layer=mappedResult.mesh->materials[0]->layers.layers[1];
    check(layer.uvScale.x==30&&layer.uvScale.y==48&&std::abs(layer.uvOffset.x-.8f)<1e-6f&&std::abs(layer.uvOffset.y-1.6f)<1e-6f&&layer.normalHeightTexture==tiled->layers.layers[0].normalHeightTexture&&layer.normalScale==.7f,"Triplanar phase/detail preservation failed");
    tiny={};tiny.maxBoundaryEdges=1;reject([&]{buildTerrainSeam(*terrain,mesh,tiny);});
    std::cout<<"PASS terrain seams: inferred boundary, welded gaps/corners, source sample insertion, blend/normal endpoints, collision scans, archives, translation, budgets, unsupported-material rejection\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
