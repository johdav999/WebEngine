#include "engine/glb_importer.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeleton_component.hpp"
#include <fstream>
#include <iostream>
using namespace engine;
namespace {
int failures=0;
void check(bool ok,const char* message) { if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';} }
bool close(float a,float b) {return std::abs(a-b)<0.002f;}
template<class F> void rejects(F f,const char* message) {bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
std::filesystem::path path(const char* name){return std::filesystem::path(GLB_FIXTURE_DIR)/name;}
std::vector<std::byte> read(const char* name){std::ifstream f(path(name),std::ios::binary|std::ios::ate);std::vector<std::byte> b(static_cast<std::size_t>(f.tellg()));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),b.size());return b;}
}
int main() {
 try {
    GlbImportOptions o;o.optimizeMeshes=false;GlbImportMeasurements timing;o.measurements=&timing;
    const auto bytes=read("skeletal_textured.glb");auto mesh=GlbImporter::loadMemory(bytes,o);
    check(mesh->vertices.size()==3&&mesh->indices.size()==3,"indexed geometry");
    check(close(mesh->vertices[0].position.x,2)&&close(mesh->vertices[2].uv.y,1),"baked node transform and top-left UV");
    check(mesh->parts.size()==1&&mesh->materials.size()==1,"material ranges");auto m=mesh->materials[0];
    check(close(m->baseColor.x,0.8f)&&close(m->opacity,0.75f)&&close(m->metallic,0.25f)&&close(m->roughness,0.7f),"PBR factors");
    check(m->normalTexture&&m->metallicRoughnessTexture&&m->occlusionTexture&&m->emissiveTexture,"five PBR texture maps");
    check(m->baseColorTexture==m->normalTexture&&m->baseColorTexture->wrapU==TextureWrap::Clamp,"texture sharing and sampler wrap");
    check(m->alphaMode==AlphaMode::Mask&&m->doubleSided&&close(m->alphaCutoff,0.4f),"alpha and sidedness");
    check(timing.decodedTextures==1&&timing.decodedTextureBytes>0&&timing.totalMs>0,"measurements");
    check(mesh->morphTargets.size()==1&&close(mesh->morphTargets[0].defaultWeight,0.25f)&&close(mesh->morphTargets[0].positions[2].z,0.5f),"sparse morph target");
    check(mesh->skin.size()==3&&mesh->skeleton&&mesh->skeleton->clips.size()==4,"skin and four clips");
    SkeletonComponent skeleton;skeleton.setSkeleton(mesh->skeleton);
    auto skinPosition=[&](int vertex){Vec3 p{};const auto& s=mesh->skin[vertex];for(int k=0;k<4;++k)p=p+transformPoint(skeleton.skinPose()->bones[s.joints[k]].transform,mesh->vertices[vertex].position)*s.weights[k];return p;};
    check(close(skinPosition(0).x,2)&&close(skinPosition(0).y,0),"inverse bind compensates baked mesh transform");
    std::vector<JointPose> poses(mesh->skeleton->joints.size());
    mesh->skeleton->clips[0].sample(1,poses);skeleton.setPose(poses);check(close(skinPosition(0).y,1),"linear animation deforms through runtime palette");
    mesh->skeleton->clips[1].sample(1,poses);skeleton.setPose(poses);check(close(skinPosition(0).x,0)&&close(skinPosition(0).y,2),"quaternion rotation and inverse binds");
    mesh->skeleton->clips[2].sample(0.5,poses);skeleton.setPose(poses);check(close(skinPosition(0).y,0),"STEP clip sampled correctly");
    mesh->skeleton->clips[3].sample(0.5,poses);skeleton.setPose(poses);check(close(skinPosition(0).y,0.75f),"cubic Hermite tangents scaled by interval");
    auto staticMesh=GlbImporter::loadFile(path("static_instances.glb"),o);check(!staticMesh->skeleton&&staticMesh->vertices.size()==6,"selected scene merges only its two instances");
    check(staticMesh->localBounds().maximum.x<5,"unused scene node excluded");
    for(std::size_t i=0;i<staticMesh->indices.size();i+=3) {auto a=staticMesh->vertices[staticMesh->indices[i]],b=staticMesh->vertices[staticMesh->indices[i+1]],c=staticMesh->vertices[staticMesh->indices[i+2]];check(dot(cross(b.position-a.position,c.position-a.position),a.normal)>0,"mirrored winding agrees with generated normals");}
    auto transformed=GlbImporter::loadFile(path("uv_transform.glb"),o);
    check(close(transformed->vertices[1].uv.x,2.25f)&&close(transformed->vertices[2].uv.y,3.5f)&&transformed->materials[0]->normalTexture,"alternate UV set and shared texture transform");
    auto dataUri=GlbImporter::loadFile(path("data_uri_strip.glb"),o);check(dataUri->materials[0]->baseColorTexture&&dataUri->indices.size()==6,"data URI images and non-indexed triangle strips");
    auto matrixMesh=GlbImporter::loadFile(path("matrix_skin.glb"),o);SkeletonComponent matrixSkeleton;matrixSkeleton.setSkeleton(matrixMesh->skeleton);
    check(close(transformPoint(matrixSkeleton.skinPose()->bones[matrixMesh->skin[1].joints[0]].transform,matrixMesh->vertices[1].position).x,3),"matrix mesh reflection compensated by inverse bind");
    auto external=GlbImporter::loadFile(path("external_texture.glb"));check(bool(external->materials[0]->baseColorTexture),"native companion resolver");
    auto missing=GlbImporter::loadMemory(read("external_texture.glb"));check(!missing->materials[0]->baseColorTexture&&!missing->warnings.empty(),"memory import performs no implicit image reads");
    int resolved=0;o.resolveTexture=[&](std::string_view name){++resolved;check(name=="uv-colors.png","resolver filename");return read("uv-colors.png");};
    auto explicitImage=GlbImporter::loadMemory(read("external_texture.glb"),o);check(explicitImage->materials[0]->baseColorTexture&&resolved==1,"explicit resolver cached per texture");
    o.resolveTexture={};o.optimizeMeshes=true;auto optimized=GlbImporter::loadMemory(bytes,o);check(optimized->skeleton&&optimized->skin.size()==optimized->vertices.size()&&optimized->morphTargets[0].positions.size()==optimized->vertices.size(),"optimization preserves deformation streams");
    for(auto name:{"invalid_accessor.glb","invalid_cycle.glb","unsupported_extension.glb","invalid_animation.glb"}) rejects([&]{GlbImporter::loadFile(path(name));},name);
    rejects([&]{GlbImporter::loadMemory(std::span(bytes).first(20));},"truncated GLB rejected");
    for(int limit=0;limit<7;++limit) {GlbImportOptions small;switch(limit){case 0:small.maxFileBytes=20;break;case 1:small.maxVertices=2;break;case 2:small.maxIndices=2;break;case 3:small.parserMemoryBytes=100;break;case 4:small.maxAnimationBytes=20;break;case 5:small.maxTextureBytes=1;break;case 6:small.maxMorphBytes=1;break;}rejects([&]{GlbImporter::loadMemory(bytes,small);},"resource limit enforced");}
    MeshComponent component("GLB",Mesh::cube());auto previous=component.mesh;rejects([&]{component.loadGlb({});},"invalid component import throws");check(component.mesh==previous,"failed import preserves component mesh");component.loadGlb(bytes);check(component.mesh->skeleton!=nullptr,"component memory API");component.loadGlbFile(path("static_instances.glb"));check(!component.mesh->skeleton,"component file API");
 }catch(const std::exception& e){++failures;std::cerr<<"Unexpected: "<<e.what()<<'\n';}
 std::cout<<"GLB failures: "<<failures<<'\n';return failures?1:0;
}
