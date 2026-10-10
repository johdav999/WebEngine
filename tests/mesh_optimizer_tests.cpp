#include "engine/mesh_optimizer.hpp"
#include "engine/fbx_importer.hpp"
#include <meshoptimizer.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>

using namespace engine;
namespace {
void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
using Triangle=std::array<std::uint32_t,36>;
std::vector<Triangle> triangles(const Mesh& mesh,std::size_t first,std::size_t count) {
    std::vector<Triangle> result;
    for(std::size_t i=first;i<first+count;i+=3) {
        Triangle triangle{};
        for(std::size_t corner=0;corner<3;++corner) {
            auto vertex=std::bit_cast<std::array<std::uint32_t,12>>(mesh.vertices.at(mesh.indices.at(i+corner)));
            std::copy(vertex.begin(),vertex.end(),triangle.begin()+corner*12);
        }
        result.push_back(triangle);
    }
    std::sort(result.begin(),result.end());return result;
}
void equivalent(const Mesh& before,const Mesh& after) {
    check(before.indices.size()==after.indices.size(),"Optimization must keep every triangle");
    check(before.parts.size()==after.parts.size(),"Optimization must keep draw boundaries");
    if(before.parts.empty()) check(triangles(before,0,before.indices.size())==triangles(after,0,after.indices.size()),"Full vertex attributes and winding must match");
    for(std::size_t i=0;i<before.parts.size();++i) {
        const auto& a=before.parts[i];const auto& b=after.parts[i];
        check(a.firstIndex==b.firstIndex && a.indexCount==b.indexCount && a.materialIndex==b.materialIndex,"Material ranges must remain intact");
        check(triangles(before,a.firstIndex,a.indexCount)==triangles(after,b.firstIndex,b.indexCount),"Every oriented textured triangle must stay in its material batch");
    }
}
double cacheMisses(const Mesh& mesh) {
    double result=0;
    if(mesh.parts.empty()) return meshopt_analyzeVertexCache(mesh.indices.data(),mesh.indices.size(),mesh.vertices.size(),32,0,0).vertices_transformed;
    for(const auto& part:mesh.parts) result+=meshopt_analyzeVertexCache(mesh.indices.data()+part.firstIndex,part.indexCount,mesh.vertices.size(),32,0,0).vertices_transformed;
    return result;
}
template<class F> void rejects(F operation) {
    bool rejected=false;try {operation();} catch(const std::exception&) {rejected=true;}
    check(rejected,"Invalid geometry must throw before changing the mesh");
}
}
int main() {
    try {
        Mesh seam;
        seam.vertices={{{0,0,0},{0,0,1},{0,0}},{{1,0,0},{0,0,1},{1,0}},{{0,1,0},{0,0,1},{0,1}}};
        seam.vertices.push_back(seam.vertices[0]); // Exact duplicate, eligible for welding.
        auto uv=seam.vertices[0];uv.uv.x=1;seam.vertices.push_back(uv);
        auto normal=seam.vertices[0];normal.normal={0,1,0};seam.vertices.push_back(normal);
        seam.vertices.push_back({{9,9,9},{1,0,0},{0,0}}); // Unreferenced.
        seam.indices={0,1,2,3,1,2,4,1,2,5,1,2};
        seam.materials.resize(2);seam.parts={{0,6,0},{6,6,1}};
        const auto before=seam;
        const auto result=MeshOptimizer::optimize(seam);
        check(result.inputVertices==7 && result.outputVertices==5,"Weld full tuples; keep normal and UV seams; remove unused vertices");
        equivalent(before,seam);
        auto bad=before;bad.parts[1].firstIndex=3;
        const auto badIndices=bad.indices;rejects([&]{MeshOptimizer::optimize(bad);});
        check(bad.indices==badIndices && bad.vertices.size()==before.vertices.size(),"Failed optimization preserves buffers");
        bad=before;bad.indices[0]=100;rejects([&]{MeshOptimizer::optimize(bad);});
        bad=before;bad.indices.pop_back();rejects([&]{MeshOptimizer::optimize(bad);});
        bad=before;bad.parts.back().indexCount=3;rejects([&]{MeshOptimizer::optimize(bad);});
        bad=before;bad.parts.back().materialIndex=2;rejects([&]{MeshOptimizer::optimize(bad);});

        Mesh boundary;boundary.vertices.resize(65537);boundary.indices={0,1,65535};
        auto packed=packMeshIndices(boundary);
        check(packed.useUint16 && packed.bytes.size()==8,"Uint16 includes index 65535 and pads an odd triangle to four-byte alignment");
        std::uint16_t last=0;std::memcpy(&last,packed.bytes.data()+4,2);
        check(last==65535 && packed.bytes[6]==std::byte{} && packed.bytes[7]==std::byte{},"Packing is exact with zero padding");
        boundary.indices.back()=65536;packed=packMeshIndices(boundary);
        check(!packed.useUint16 && packed.bytes.size()==12,"Index 65536 retains Uint32");
        std::uint32_t wide=0;std::memcpy(&wide,packed.bytes.data()+8,4);check(wide==65536,"Uint32 index is not truncated");
        boundary.indices.back()=65537;rejects([&]{packMeshIndices(boundary);});

        Mesh grid;constexpr unsigned size=32;
        for(unsigned y=0;y<=size;++y) for(unsigned x=0;x<=size;++x)
            grid.vertices.push_back({{float(x),float(y),0},{0,0,1},{float(x)/size,float(y)/size}});
        std::vector<std::array<unsigned,3>> faces;
        for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
            auto a=y*(size+1)+x;faces.push_back({a,a+1,a+size+1});faces.push_back({a+1,a+size+2,a+size+1});
        }
        std::mt19937 rng(42);std::shuffle(faces.begin(),faces.end(),rng);
        for(const auto& face:faces) grid.indices.insert(grid.indices.end(),face.begin(),face.end());
        const auto unordered=grid;const auto misses=cacheMisses(grid);MeshOptimizer::optimize(grid);equivalent(unordered,grid);
        check(cacheMisses(grid)<misses*0.5,"Cache ordering reduces modeled transforms on unordered geometry");
        unsigned next=0;std::vector<bool> seen(grid.vertices.size());
        for(auto index:grid.indices) if(!seen[index]) {check(index==next++,"Vertex order must follow first indexed use");seen[index]=true;}

        FbxImportOptions options;options.optimizeMeshes=false;FbxImportMeasurements rawTiming;options.measurements=&rawTiming;
        const auto raw=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"Knarr.fbx",options);
        FbxImportMeasurements optimizedTiming;options.optimizeMeshes=true;options.measurements=&optimizedTiming;
        const auto optimized=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"Knarr.fbx",options);
        check(raw->vertices.size()==86544 && optimized->vertices.size()==38621,"Knarr exact welding reduction");
        check(optimizedTiming.inputVertices==86544 && optimizedTiming.outputVertices==38621 && optimizedTiming.optimizeMs>0,"Import reports optimization separately");
        check(rawTiming.optimizeMs==0 && rawTiming.inputVertices==rawTiming.outputVertices,"Comparison option bypasses optimization");
        equivalent(*raw,*optimized);
        check(cacheMisses(*optimized)<cacheMisses(*raw),"Knarr improves modeled cache reuse");
        check(optimized->warnings==raw->warnings && optimized->materials.size()==raw->materials.size(),"Optimization preserves texture warnings and material count");
        for(std::size_t i=0;i<raw->materials.size();++i) {
            const auto& a=*raw->materials[i];const auto& b=*optimized->materials[i];
            check(a.name==b.name && a.texturePath==b.texturePath && a.baseColorTexture->pixels==b.baseColorTexture->pixels,"Imported textures are unchanged");
        }
        const auto rawPacked=packMeshIndices(*raw),optimizedPacked=packMeshIndices(*optimized);
        check(!rawPacked.useUint16 && optimizedPacked.useUint16,"Knarr now uses Uint16 GPU indices");
        const auto oldBytes=raw->vertices.size()*sizeof(Vertex)+rawPacked.bytes.size();
        const auto newBytes=optimized->vertices.size()*sizeof(Vertex)+optimizedPacked.bytes.size();
        check(oldBytes==4645584 && newBytes==2099544,"Knarr GPU geometry payload decreases without deleting triangles");
        std::cout<<"Knarr: "<<raw->vertices.size()<<" -> "<<optimized->vertices.size()<<" vertices, "<<oldBytes<<" -> "<<newBytes<<" GPU geometry bytes; modeled 32-entry cache transforms "<<cacheMisses(*raw)<<" -> "<<cacheMisses(*optimized)<<"\n";
        std::cout<<"Mesh optimizer tests passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
