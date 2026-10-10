#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/mesh_optimizer.hpp"
#include "engine/render_frame.hpp"
#include "engine/camera_component.hpp"
#include "engine/scene_archive.hpp"
#include <fstream>
#include <iostream>
#include <limits>

using namespace engine;
namespace {
int failures=0;
void check(bool ok,const char* message) { if(!ok) {++failures;std::cerr<<"FAIL: "<<message<<'\n';} }
bool near(float a,float b) { return std::abs(a-b)<0.0001f; }
template<class F> void rejects(F operation,const char* message) {
    bool rejected=false;try {operation();} catch(const std::exception&) {rejected=true;}check(rejected,message);
}
std::shared_ptr<Mesh> geometry() {
    auto mesh=std::make_shared<Mesh>(*Mesh::cube());
    MorphTarget stretch{"Stretch"},lean{"Lean"};
    for(const auto& vertex:mesh->vertices) {
        stretch.positions.push_back({0,vertex.position.y>0 ? 2.0f : 0,0});
        lean.positions.push_back({vertex.position.y>0 ? 1.0f : 0,0,0});
    }
    mesh->morphTargets={stretch,lean};return mesh;
}
std::vector<std::byte> fixture(const char* name) {
    std::ifstream file(std::filesystem::path(FBX_FIXTURE_DIR)/name,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("Missing morph fixture");
    std::vector<std::byte> data(std::size_t(file.tellg()));file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size()));return data;
}
std::span<const std::byte> bytes(const std::string& s) { return {reinterpret_cast<const std::byte*>(s.data()),s.size()}; }
}
int main() {
    try {
        auto mesh=geometry();Scene scene;auto& a=scene.addChild<MeshComponent>("A",mesh);auto& b=scene.addChild<MeshComponent>("B",mesh);
        check(!a.morphPose()&&!b.morphPose(),"Zero weights reuse shared static vertices");
        a.setMorphWeight("Stretch",0.5f);a.setMorphWeight(1,-0.25f);
        const auto pose=a.morphPose();check(pose&&pose->revision==1&&!b.morphPose(),"Shared asset keeps independent instance weights");
        for(std::size_t i=0;i<mesh->vertices.size();++i) {
            const auto original=mesh->vertices[i].position,changed=pose->vertices[i].position;
            check(near(changed.y,original.y+(original.y>0?1:0))&&near(changed.x,original.x-(original.y>0?0.25f:0)),"Targets add and support negative weights");
        }
        check(near(a.localBounds().maximum.y,1.5f)&&near(b.localBounds().maximum.y,0.5f),"Bounds follow instance deformation");
        const auto revision=pose->revision;const auto* data=pose->vertices.data();
        a.setMorphWeight(0,0.5f);RenderFrame frame;frame.collect(scene);frame.prepareObjects();
        for(int i=0;i<1000;++i) {frame.collect(scene);frame.prepareObjects();check(a.morphPose()->vertices.data()==data&&pose->revision==revision&&frame.objectUploads().empty(),"Warm unchanged morphs preserve staging and upload state");}
        check(frame.batches(RenderLayer::World).size()==2,"Different morph poses cannot instance together");
        a.setMorphWeight(0,0);a.setMorphWeight(1,0);frame.collect(scene);
        check(frame.batches(RenderLayer::World).size()==1&&!frame.sceneFrame().meshes[0].morphPose,"Zero weights restore static batching");
        a.setMorphWeight(0,1);a.morphPose();a.setMorphWeight(0,0.5f);
        check(a.morphPose()->revision>revision&&near(a.localBounds().maximum.y,1.5f),"Returning from zero rebuilds morph pose and bounds");
        rejects([&]{a.setMorphWeight(5,1);},"Invalid target index rejected");
        rejects([&]{a.setMorphWeight("Missing",1);},"Invalid target name rejected");
        rejects([&]{a.setMorphWeight(0,std::numeric_limits<float>::quiet_NaN());},"Nonfinite weights rejected");
        check(near(a.morphWeight(0),0.5f),"Failed setter preserves previous weight");
        a.mesh=Mesh::plane();check(!a.morphPose(),"Replacing mesh releases deformation");
        a.mesh=mesh;check(near(a.morphWeight(0),0)&&!a.morphPose(),"Replacing mesh resets weights to resource defaults");
        mesh=geometry();mesh->morphTargets[0].defaultWeight=0.25f;a.mesh=mesh;
        check(near(a.morphWeight(0),0.25f)&&near(a.localBounds().maximum.y,1),"Asset defaults initialize weights");
        a.setMorphWeight(0,2);a.resetMorphWeights();check(near(a.morphWeight(0),0.25f),"Reset restores resource defaults");
        auto invalid=geometry();invalid->morphTargets[0].positions.pop_back();
        rejects([&]{invalid->validateMorphTargets();},"Malformed morph stream rejected");
        invalid=geometry();invalid->morphTargets[0].positions[0].x=std::numeric_limits<float>::infinity();
        rejects([&]{MeshOptimizer::optimize(*invalid);},"Nonfinite target data rejected before optimization");
        auto attrs=geometry();attrs->morphTargets[0].normals.resize(attrs->vertices.size(),{0.25f,0.1f,0});
        attrs->morphTargets[0].tangents.resize(attrs->vertices.size(),{0,0.2f,0.1f});a.mesh=attrs;a.setMorphWeight(0,1);
        for(const auto& v:a.morphPose()->vertices) check(near(dot(v.normal,v.normal),1)&&near(dot(v.normal,{v.tangent[0],v.tangent[1],v.tangent[2]}),0),"Morphed normals and tangents form an orthonormal basis");
        MeshComponent temporary("Temporary",geometry());temporary.setMorphWeight(0,1);temporary.morphPose();temporary.mesh.reset();
        check(!temporary.morphPose(),"Clearing the last mesh reference clears stale morph state");
        auto cancelled=geometry();cancelled->morphTargets[0].normals.clear();
        for(const auto& v:cancelled->vertices) cancelled->morphTargets[0].normals.push_back(v.normal*-1);
        temporary.mesh=cancelled;temporary.setMorphWeight(0,1);
        for(const auto& v:temporary.morphPose()->vertices) check(near(dot(v.normal,v.normal),1),"Cancelled normals keep a finite base-normal fallback");
        auto rig=std::make_shared<Skeleton>();rig->joints.push_back({"Root",-1,{}});rig->bindings={{},{0,Mat4::identity()}};
        JointPose moved;moved.translation={0,3,0};rig->clips.push_back({"Move",1,2,{{},moved}});
        auto skinned=geometry();skinned->skeleton=rig;skinned->skin.resize(skinned->vertices.size());for(auto& skin:skinned->skin) skin.joints[0]=1;
        auto& s=scene.addChild<SkeletalMeshComponent>("Skinned",skinned);s.setMorphWeight(0,0.5f);s.animation().play(0,false);s.animation().seek(1);
        check(near(s.localBounds(s.skinPose()).maximum.y,4.5f),"Morph positions are skinned before bounds calculation");
        frame.collect(scene);check(frame.sceneFrame().meshes.back().morphPose&&frame.sceneFrame().meshes.back().skinPose,"Draw retains morph and skeletal resources");
        Mesh weld;const Vertex v{{0,0,0},{0,1,0},{0,0}};weld.vertices={v,v,v};weld.indices={0,1,2};
        for(int i=0;i<20;++i) weld.morphTargets.push_back({"Shape",{{0,0,0},{0,0,0},{0,0,0}}});
        weld.morphTargets.back().positions[2]={1,0,0};MeshOptimizer::optimize(weld);
        check(weld.vertices.size()==2&&weld.morphTargets.back().positions.size()==2,"Welding includes all morphs beyond meshoptimizer's stream limit");
        Mesh seam;seam.vertices={{{0,0,0},{0,0,1},{0,0}},{{1,0,0},{0,0,1},{1,0}},{{0,1,0},{0,0,1},{0,1}},{{-1,0,0},{0,0,1},{1,0}}};seam.indices={0,1,2,0,2,3};
        seam.morphTargets={{"Seam",{{1,0,0},{2,0,0},{3,0,0},{4,0,0}}}};seam.generateTangents();
        check(seam.vertices.size()>4&&seam.morphTargets[0].positions.size()==seam.vertices.size(),"Tangent seams duplicate morph attributes");
        for(std::size_t i=4;i<seam.vertices.size();++i) check(near(seam.morphTargets[0].positions[i].x,seam.vertices[i].position.y==1 ? 3 : 1),"Split vertices retain the correct source target");
        const auto input=fixture("morph_column_ascii.fbx");FbxImportOptions options;options.optimizeMeshes=false;
        const auto raw=FbxImporter::loadMemory(input,options),optimized=FbxImporter::loadMemory(input);
        check(raw->morphTargets.size()==2&&optimized->morphTargets.size()==2,"FBX sparse channels import as named targets");
        MeshComponent imported("Imported",optimized);check(near(imported.morphWeight(0),0.25f),"FBX percentage maps to normalized default weight");
        imported.setMorphWeight("Stretch",1);imported.setMorphWeight("Lean",0.5f);
        check(near(imported.localBounds().maximum.y,3)&&near(imported.localBounds().maximum.x,1),"FBX centimeter offsets convert to meters");
        MeshComponent original("Raw",raw);original.setMorphWeight(0,1);original.setMorphWeight(1,0.5f);
        for(auto p:imported.morphPose()->vertices) {
            bool found=false;for(auto q:original.morphPose()->vertices) found|=near(p.position.x,q.position.x)&&near(p.position.y,q.position.y)&&near(p.position.z,q.position.z);
            check(found,"Optimized target streams preserve raw deformation");
        }
        options={};options.maxMorphTargets=1;rejects([&]{FbxImporter::loadMemory(input,options);},"Target count budget enforced");
        options={};options.maxMorphBytes=12;rejects([&]{FbxImporter::loadMemory(input,options);},"Decoded morph memory budget enforced");
        options={};options.maxMorphBytes=0;rejects([&]{FbxImporter::loadMemory(input,options);},"Zero morph budget rejected");
        std::string text(reinterpret_cast<const char*>(input.data()),input.size());const auto at=text.find("a: 4,5,6,7");text.replace(at,12,"a: 4,5,6,99");
        rejects([&]{FbxImporter::loadMemory(bytes(text));},"Out-of-range source shape indices rejected");
        const auto old=imported.mesh;rejects([&]{imported.loadFbx(bytes(text));},"Malformed morph import rejected");
        check(imported.mesh==old&&near(imported.morphWeight(0),1),"Failed import preserves asset and instance weights");
        CameraComponent camera;camera.setOrthographic(2,0.1f,20);camera.transform.position={0,0,5};camera.lookAt({0,0,0});
        Scene culling;auto& moving=culling.addChild<MeshComponent>("Morph",geometry());moving.transform.position={4,0,0};
        const auto frustum=camera.frustum(1);SceneFrame culled;culling.collect(culled,0xffffffffu,&frustum);
        check(!culled.meshes[0].cameraVisible,"Base mesh outside camera is culled");
        auto entering=geometry();for(auto& offset:entering->morphTargets[0].positions) offset={-4,0,0};moving.mesh=entering;moving.setMorphWeight(0,1);
        culling.collect(culled,0xffffffffu,&frustum);
        check(culled.meshes[0].cameraVisible,"Morph movement into the camera updates culling bounds");
        text.assign(reinterpret_cast<const char*>(input.data()),input.size());
        const std::string model="Model: 2000, \"Model::MorphColumn\", \"Mesh\" { Version: 232 }";
        text.replace(text.find(model),model.size(),"Model: 2000, \"Model::MorphColumn\", \"Mesh\" { Version: 232\nProperties70: {\nP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",300,0,0\nP: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",-2,1,1\n}\n}");
        auto transformed=FbxImporter::loadMemory(bytes(text));MeshComponent transformedInstance("Transformed",transformed);
        transformedInstance.setMorphWeight(1,1);
        check(near(transformedInstance.localBounds().minimum.x,0)&&near(transformedInstance.localBounds().maximum.x,4),"Mirrored scale affects offsets while node translation affects only positions");
        const auto binary=fixture("morph_column_binary.fbx");auto binaryMesh=FbxImporter::loadMemory(binary);MeshComponent binaryInstance("Binary",binaryMesh);
        binaryInstance.setMorphWeight(0,1);binaryInstance.setMorphWeight(1,0.5f);
        check(binaryMesh->morphTargets.size()==optimized->morphTargets.size()&&near(binaryInstance.localBounds().maximum.y,3)&&near(binaryInstance.localBounds().maximum.x,1),"Binary and ASCII blend shapes produce equivalent deformation");
        Scene saved;auto& first=saved.addChild<MeshComponent>("First",optimized);auto& second=saved.addChild<MeshComponent>("Second",optimized);
        first.setMorphWeight(0,1);second.setMorphWeight(1,-0.5f);
        auto loaded=SceneArchive::load(SceneArchive::save(saved));SceneFrame restored;loaded.scene->collect(restored);
        check(restored.meshes.size()==2&&restored.meshes[0].mesh==restored.meshes[1].mesh&&restored.meshes[0].mesh->morphTargets.size()==2,"Scene archive retains shared morph assets");
        auto* restoredFirst=dynamic_cast<MeshComponent*>(loaded.scene->children().front().get());
        check(restoredFirst&&near(restoredFirst->morphWeight(0),1),"Scene archive retains per-instance morph weights");
    } catch(const std::exception& error) {check(false,error.what());}
    if(failures) return 1;std::cout<<"Morph tests passed\n";return 0;
}
