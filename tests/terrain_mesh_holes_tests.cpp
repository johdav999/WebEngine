#include "engine/terrain_mesh.hpp"
#include "engine/scene.hpp"
#include "engine/physics.hpp"
#include <iostream>
#include <limits>
using namespace engine;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid mesh cut accepted");}
Mesh opening(std::initializer_list<Vec2> rim,float y=0){Mesh mesh;mesh.vertices.push_back({{3,-2,3},{},{}});for(auto p:rim)mesh.vertices.push_back({{p.x,y,p.y},{},{}});
    for(unsigned i=1;i<mesh.vertices.size();++i)mesh.indices.insert(mesh.indices.end(),{0,i,i+1<mesh.vertices.size()?i+1:1});return mesh;}
int main(){try{
    TerrainData data;data.samplesX=data.samplesZ=9;data.chunkCells=4;data.heights.resize(81);auto terrain=TerrainAsset::create(data);
    auto l=opening({{1,1},{5,1},{5,3},{3,3},{3,5},{1,5}});auto cut=buildTerrainMeshHoleMask(*terrain,l);
    check(cut.loops==1&&cut.footprintCells==12&&cut.newlyCutCells==12,"Concave cut did not follow mesh rim");
    check(cut.holes[2*8+4]&&!cut.holes[4*8+4]&&!cut.holes[0],"Concave cut became a bounding rectangle");
    auto diamond=opening({{3,1},{5,3},{3,5},{1,3}});auto shaped=buildTerrainMeshHoleMask(*terrain,diamond);
    check(shaped.footprintCells==12&&!shaped.footprint[1*8+1]&&shaped.footprint[1*8+2],"Conservative shaped-cell rasterization failed");
    auto buried=diamond;for(auto& v:buried.vertices)v.position.y-=3;check(buildTerrainMeshHoleMask(*terrain,buried).loops==0,"Buried opening cut the surface");
    Mesh annulus;for(auto p:std::array<Vec2,8>{Vec2{1,1},Vec2{7,1},Vec2{7,7},Vec2{1,7},Vec2{3,3},Vec2{5,3},Vec2{5,5},Vec2{3,5}})annulus.vertices.push_back({{p.x,0,p.y},{},{}});
    for(unsigned i=0;i<4;++i){const auto n=(i+1)%4;annulus.indices.insert(annulus.indices.end(),{i,n,4+n,i,4+n,4+i});}
    auto rings=buildTerrainMeshHoleMask(*terrain,annulus);check(rings.loops==2&&rings.footprintCells==32&&!rings.footprint[3*8+3],"Nested rim island was removed");
    auto existing=data;existing.holes.resize(64);existing.holes[0]=existing.holes[1*8+1]=1;auto retained=TerrainAsset::create(existing);
    auto merged=buildTerrainMeshHoleMask(*retained,l);check(merged.holes[0]&&!merged.footprint[0]&&merged.newlyCutCells==11,"Mesh cut lost pre-existing holes or ownership");
    TerrainMeshHoleSettings settings;settings.meshToTerrain.m[12]=1;settings.meshToTerrain.m[14]=2;
    auto translated=l;for(auto& v:translated.vertices){v.position.x+=1;v.position.z+=2;}
    check(buildTerrainMeshHoleMask(*terrain,l,settings).holes==buildTerrainMeshHoleMask(*terrain,translated).holes,"Mesh-to-terrain translation failed");
    auto split=l;split.vertices.clear();split.indices.clear();for(auto id:l.indices){split.indices.push_back(unsigned(split.vertices.size()));split.vertices.push_back(l.vertices[id]);}
    check(buildTerrainMeshHoleMask(*terrain,split).holes==cut.holes,"Attribute-split rim was not reconciled");
    auto tj=opening({{1,1},{5,1},{5,5},{1,5}});tj.vertices={{{1,0,1},{},{}},{{5,0,1},{},{}},{{5,0,5},{},{}},{{1,0,5},{},{}},{{3,0,3},{},{}}};tj.indices={0,1,2,0,4,3,4,2,3};
    check(buildTerrainMeshHoleMask(*terrain,tj).footprintCells==16,"Boundary T-junction produced an internal hole rim");
    settings={};Transform pose;pose.position={1,0,7};pose.rotation.y=pi/2;settings.meshToTerrain=pose.matrix();
    check(buildTerrainMeshHoleMask(*terrain,l,settings).footprintCells==12,"Rotated footprint has gridline sliver cuts");
    auto empty=TerrainAsset::create(data,false);check(buildTerrainMeshHoleMask(*empty,l).holes==cut.holes,"Source-only cut changed mask");
    auto sloped=data;sloped.spacing={2,3};for(unsigned z=0;z<9;++z)for(unsigned x=0;x<9;++x)sloped.heights[z*9+x]=x*.1f+z*.2f;
    auto slopeMesh=l;for(unsigned i=0;i<slopeMesh.vertices.size();++i){auto& p=slopeMesh.vertices[i].position;p.y=(i?0:-2)+p.x*.1f+p.z*.2f;p.x*=2;p.z*=3;}
    check(buildTerrainMeshHoleMask(*TerrainAsset::create(sloped),slopeMesh).footprint==cut.footprint,"Uncut sloped source or nonuniform cell spacing changed footprint");
    auto patch=TerrainAsset::createPatch(data,{0,0,4,4});rejects([&]{buildTerrainMeshHoleMask(*patch,l);});
    auto disconnected=opening({{1,1},{3,1},{3,3},{1,3}});const auto other=opening({{5,5},{7,5},{7,7},{5,7}});const auto offset=unsigned(disconnected.vertices.size());
    disconnected.vertices.insert(disconnected.vertices.end(),other.vertices.begin(),other.vertices.end());for(auto id:other.indices)disconnected.indices.push_back(offset+id);
    const auto separate=buildTerrainMeshHoleMask(*terrain,disconnected);check(separate.loops==2&&separate.footprintCells==8,"Disjoint openings joined unrelated terrain");
    Scene scene;auto actor=makeTerrainActor("Cut from mesh",terrain,true);auto* live=actor.get();scene.adoptChild(std::move(actor));scene.synchronize();
    auto edit=cutTerrainHolesFromMesh(*terrain,l);check(edit.holesChanged&&edit.before==terrain,"Mesh cut did not return immutable terrain edit");applyTerrainEdit(*live,edit);scene.synchronize();
    check(!terrainGround(scene,2.5f,2.5f)&&!scene.physics().raycast({2.5f,3,2.5f},{0,-1,0},5),"Mesh cut did not remove queries/collision");
    check(terrainGround(scene,6.5f,6.5f).has_value(),"Mesh cut removed unrelated terrain");
    auto bad=l;bad.vertices[1].position.y=2;rejects([&]{buildTerrainMeshHoleMask(*terrain,bad);});
    bad=l;bad.indices[0]=999;rejects([&]{buildTerrainMeshHoleMask(*terrain,bad);});
    bad=l;bad.vertices[0].position.x=std::numeric_limits<float>::quiet_NaN();rejects([&]{buildTerrainMeshHoleMask(*terrain,bad);});
    auto bow=opening({{1,1},{5,5},{1,5},{5,1}});rejects([&]{buildTerrainMeshHoleMask(*terrain,bow);});
    settings={};settings.maxWork=1;rejects([&]{buildTerrainMeshHoleMask(*terrain,l,settings);});
    settings={};settings.maxCells=1;rejects([&]{buildTerrainMeshHoleMask(*terrain,l,settings);});
    settings={};settings.meshToTerrain.m[0]=0;rejects([&]{buildTerrainMeshHoleMask(*terrain,l,settings);});
    check(terrain->data().holes.empty()&&terrain->ground(2.5f,2.5f).has_value(),"Rejected/derived cuts mutated the source");
    std::cout<<"PASS mesh-shaped terrain holes: concavity, conservative cells, islands, ownership, transforms, split vertices, source-only data, collision, invalid inputs and budgets\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
