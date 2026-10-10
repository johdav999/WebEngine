#include "engine/skeletal_mesh_component.hpp"
#include "engine/render_frame.hpp"
#include "engine/mesh_optimizer.hpp"
#include <cstring>
#include <iostream>
#include <fstream>

using namespace engine;
namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
bool near(const Mat4& a,const Mat4& b,float tolerance=0.0001f) {
    for(std::size_t i=0;i<16;++i) if(std::abs(a.m[i]-b.m[i])>tolerance) return false;return true;
}
Vec3 skinned(const Mesh& mesh,const SkinPose& pose,std::size_t vertex) {
    Vec3 p{};const auto& skin=mesh.skin[vertex];
    for(std::size_t i=0;i<4;++i) p=p+transformPoint(pose.bones[skin.joints[i]].transform,mesh.vertices[vertex].position)*skin.weights[i];
    return p;
}
}
int main() {
    try {
        const auto path=std::filesystem::path(FBX_FIXTURE_DIR)/"skeletal_column_ascii.fbx";
        auto mesh=FbxImporter::loadFile(path);
        check(mesh->skeleton && mesh->skin.size()==mesh->vertices.size(),"FBX imports skeleton and weights");
        check(mesh->skeleton->clips.size()==2 && mesh->skeleton->clips[0].duration==2 && mesh->skeleton->clips[0].frameCount==61,"FBX imports both sampled clips and duration");
        check(mesh->skeleton->clips[0].name=="Bend Left" && mesh->skeleton->clips[1].name=="Bend Right","Clip names are preserved");
        const auto binary=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"skeletal_column_binary.fbx");
        check(binary->indices==mesh->indices && binary->vertices.size()==mesh->vertices.size() && binary->skeleton->clips.size()==2,"Binary FBX imports the same geometry and animations");
        check(binary->skeleton->clips[0].name==mesh->skeleton->clips[0].name && binary->skeleton->clips[1].name==mesh->skeleton->clips[1].name,"Binary and ASCII clip names agree");
        check(!std::memcmp(binary->skin.data(),mesh->skin.data(),mesh->skin.size()*sizeof(SkinVertex)),"Binary and ASCII skin weights agree");
        for(std::size_t i=0;i<mesh->vertices.size();++i) check(!std::memcmp(&binary->vertices[i],&mesh->vertices[i],sizeof(Vertex)),"Binary and ASCII vertex attributes agree");
        for(std::size_t i=0;i<mesh->skeleton->bindings.size();++i) check(near(binary->skeleton->bindings[i].inverseBind,mesh->skeleton->bindings[i].inverseBind),"Binary and ASCII skin bind matrices agree");
        for(std::size_t i=0;i<mesh->skeleton->clips[0].frames.size();++i)
            check(near(binary->skeleton->clips[0].frames[i].matrix(),mesh->skeleton->clips[0].frames[i].matrix()),"Binary and ASCII animation poses agree");
        Scene scene;auto& component=scene.addChild<SkeletalMeshComponent>("Animated",mesh);
        SkeletonComponent binarySkeleton;binarySkeleton.setSkeleton(binary->skeleton);
        for(std::size_t i=0;i<component.skinPose()->bones.size();++i) check(near(component.skinPose()->bones[i].transform,binarySkeleton.skinPose()->bones[i].transform),"Binary and ASCII bind palettes agree");
        auto& player=component.animation();auto bind=component.skinPose();
        for(std::size_t i=0;i<bind->bones.size();++i) {
            if(!near(bind->bones[i].transform,Mat4::identity())) {
                std::cerr<<"Binding "<<i<<":";for(auto f:bind->bones[i].transform.m) std::cerr<<' '<<f;std::cerr<<'\n';
            }
            check(near(bind->bones[i].transform,Mat4::identity()),"Bind pose produces identity skin transforms in baked mesh space");
        }
        std::size_t top=0;for(std::size_t i=0;i<mesh->vertices.size();++i) if(mesh->vertices[i].position.y>mesh->vertices[top].position.y) top=i;
        const auto original=skinned(*mesh,*bind,top);
        player.play(0);player.seek(1);const auto left=skinned(*mesh,*component.skinPose(),top);
        check(std::abs(left.x-original.x)>0.5f,"Imported bone animation deforms vertices");
        player.play(1);player.seek(1);const auto right=skinned(*mesh,*component.skinPose(),top);
        check((left.x-original.x)*(right.x-original.x)<0,"Clips bend in opposite directions");
        player.blend(0,1,0.5f);player.seek(1);const auto middle=skinned(*mesh,*component.skinPose(),top);
        check(std::abs(middle.x-original.x)<0.0001f && std::abs(middle.y-original.y)<0.0001f,"Quaternion blend follows the shortest arc and retains the hierarchy");
        player.setBlendWeight(0);check(std::abs(skinned(*mesh,*component.skinPose(),top).x-left.x)<0.0001f,"Blend zero equals clip A");
        player.setBlendWeight(1);check(std::abs(skinned(*mesh,*component.skinPose(),top).x-right.x)<0.0001f,"Blend one equals clip B");
        player.pause();const auto revision=component.skinPose()->revision;scene.updateAnimations(0.5);
        check(player.time()==1 && component.skinPose()->revision==revision,"Pause freezes pose and palette revision");
        player.resume();scene.updateAnimations(0.25);check(player.time()==1.25,"Resume advances playback");
        player.stop();check(player.state()==AnimationState::Stopped && player.time()==0 && near(component.skinPose()->bones[2].transform,Mat4::identity()),"Stop restores bind pose");
        player.play(0,false,2);scene.updateAnimations(1.5);
        check(player.state()==AnimationState::Paused && player.time()==2,"Non-loop playback clamps and holds final pose");
        player.play(0,true);scene.updateAnimations(2.5);const auto looped=skinned(*mesh,*component.skinPose(),top);
        player.seek(0.5);check(std::abs(skinned(*mesh,*component.skinPose(),top).x-looped.x)<0.0001f,"Loop wraps sample time");
        player.seek(1);const auto beforeFade=component.skinPose()->bones;
        player.crossFade(1,0.4);check(near(component.skinPose()->bones[2].transform,beforeFade[2].transform),"Crossfade starts with the current pose without a snap");
        scene.updateAnimations(0.2);const auto halfway=component.skinPose()->bones;
        check(!near(halfway[2].transform,beforeFade[2].transform),"Fade advances toward target");
        player.crossFade(0,0.4);check(near(component.skinPose()->bones[2].transform,halfway[2].transform),"Interrupted fade preserves current blended pose");
        scene.updateAnimations(0.4);check(player.state()==AnimationState::Playing,"Completed fade continues target clip");
        component.enabled=false;const auto time=player.time();scene.updateAnimations(1);check(player.time()==time,"Disabled subtrees do not tick");component.enabled=true;
        bool rejected=false;try { player.play(99); } catch(const std::out_of_range&) { rejected=true; }check(rejected,"Invalid clip selection rejected");
        rejected=false;try { player.blend(0,1,2); } catch(const std::invalid_argument&) { rejected=true; }check(rejected,"Invalid blend weights rejected");
        rejected=false;try { scene.updateAnimations(-1); } catch(const std::invalid_argument&) { rejected=true; }check(rejected,"Invalid delta rejected");
        auto& other=scene.addChild<SkeletalMeshComponent>("Independent",mesh);other.animation().play(1);other.animation().seek(1);
        RenderFrame frame;frame.collect(scene);frame.prepareObjects();
        check(frame.batches(RenderLayer::World).size()==2,"Instances with independent palettes cannot merge into the same batch");
        auto oldPose=std::weak_ptr<const SkinPose>(other.skinPose());other.mesh=Mesh::cube();scene.updateAnimations(0);frame.collect(scene);
        check(!frame.sceneFrame().meshes.back().skinPose && oldPose.expired(),"Replacing a rig releases its pose and restores static rendering");
        auto rawOptions=FbxImportOptions{};rawOptions.optimizeMeshes=false;const auto raw=FbxImporter::loadFile(path,rawOptions);
        SkeletonComponent rawSkeleton;rawSkeleton.setSkeleton(raw->skeleton);AnimationComponent rawPlayer("Raw",rawSkeleton);rawPlayer.play(0);rawPlayer.seek(1);
        player.play(0);player.seek(1);
        for(auto index:mesh->indices) {
            const auto p=skinned(*mesh,*component.skinPose(),index);bool found=false;
            for(std::size_t i=0;i<raw->vertices.size();++i) {const auto q=skinned(*raw,*rawSkeleton.skinPose(),i);if(std::abs(p.x-q.x)+std::abs(p.y-q.y)+std::abs(p.z-q.z)<0.0001f) {found=true;break;}}
            check(found,"Lossless optimization remaps skin weights with geometry");
        }
        auto budget=FbxImportOptions{};budget.maxAnimationBytes=16;rejected=false;
        try {FbxImporter::loadFile(path,budget);} catch(const std::runtime_error&) { rejected=true; }check(rejected,"Animation memory budget enforced");
        std::ifstream sourceFile(path);std::string differentRest((std::istreambuf_iterator<char>(sourceFile)),{});
        const auto translation=differentRest.find("\"A\",0,100,0");check(translation!=std::string::npos,"Fixture contains the expected bone translation");
        differentRest.replace(translation,std::string("\"A\",0,100,0").size(),"\"A\",0,150,0");
        const auto differentMesh=FbxImporter::loadMemory({reinterpret_cast<const std::byte*>(differentRest.data()),differentRest.size()});
        SkeletonComponent realBind;realBind.setSkeleton(differentMesh->skeleton);
        check(near(realBind.skinPose()->bones[2].transform,Mat4::identity()),"Original FBX cluster bind pose is preserved even when default node pose differs");
        const auto saved=component.skinPose()->bones;const auto savedRevision=component.skinPose()->revision;
        std::vector<JointPose> invalidPose;for(const auto& joint:mesh->skeleton->joints) invalidPose.push_back(joint.bindPose);
        invalidPose.back().scale={0,0,0};rejected=false;
        try { component.skeleton().setPose(invalidPose); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected && component.skinPose()->revision==savedRevision && !std::memcmp(saved.data(),component.skinPose()->bones.data(),saved.size()*sizeof(BoneUniform)),"Rejected singular poses leave the complete uploaded palette unchanged");
        auto duplicate=std::make_shared<Mesh>();duplicate->skeleton=mesh->skeleton;
        const Vertex v{{0,1,0},{0,0,1},{0,0}};duplicate->vertices={v,v,v};duplicate->indices={0,1,2};
        duplicate->skin.resize(3);duplicate->skin[1].joints[0]=1;duplicate->skin[2].joints[0]=2;
        MeshOptimizer::optimize(*duplicate);check(duplicate->vertices.size()==3,"Welding must preserve otherwise identical vertices with different skin influences");
        std::cout<<"Animation tests passed: "<<mesh->skeleton->joints.size()<<" joints, "<<mesh->skeleton->bindings.size()<<" bindings, "<<mesh->skeleton->clips.size()<<" clips\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
