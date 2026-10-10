#include "engine/render_frame.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/camera_component.hpp"
#include <cstring>
#include <iostream>

using namespace engine;
namespace {
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
bool exact(const Mat4& a,const Mat4& b) {return std::memcmp(a.m.data(),b.m.data(),16*sizeof(float))==0;}
Mat4 uncached(const Node& node,const Mat4& root=Mat4::identity()) {return (node.parent()?uncached(*node.parent(),root):root)*node.transform.matrix();}
void stage(RenderFrame& frame,const Scene& scene) {frame.collect(scene);frame.prepareObjects();}
ObjectUniform object(const RenderFrame& frame,std::size_t slot) {
    ObjectUniform value{};std::memcpy(&value,frame.objectData().data()+slot*RenderFrame::objectStride,sizeof(value));return value;
}
}
int main() {
    try {
        Scene scene;auto& parent=scene.addChild<Actor>("Parent");
        auto& child=parent.addChild<MeshComponent>("Child",Mesh::cube());child.transform.position={2,1,3};
        auto multipart=std::make_shared<Mesh>(*Mesh::cube());multipart->materials.resize(2);multipart->parts={{0,18,0},{18,18,1}};
        auto& nested=child.addChild<MeshComponent>("Nested",multipart);nested.transform.scale={2,3,0.5f};
        auto& sibling=scene.addChild<MeshComponent>("Static sibling",Mesh::plane());
        auto& camera=scene.addChild<CameraComponent>("Camera");camera.transform.position={0,5,8};
        RenderFrame frame;stage(frame,scene);
        check(frame.changedObjects()==4 && frame.objectUploads().size()==1,"Initial upload covers every slot in one range");
        check(frame.objectUploads()[0].offset==0 && frame.objectUploads()[0].bytes==3*256+sizeof(ObjectUniform),"Coalescing excludes final alignment padding");
        stage(frame,scene);
        check(frame.objectUploads().empty() && frame.changedObjects()==0,"Unchanged objects do not upload");
        const auto hot=frame.sceneFrame().transformRebuilds;
        check(hot.localBuilds==0 && hot.worldBuilds==0 && hot.normalBuilds==0,"Unchanged traversal rebuilds no matrices");
        const auto siblingStats=sibling.transformCacheStats();
        parent.transform.rotation.y=0.7f;parent.transform.position.x=4;stage(frame,scene);
        check(frame.changedObjects()==3 && frame.objectUploads().size()==1 && frame.objectUploads()[0].bytes==2*256+sizeof(ObjectUniform),"Parent changes upload only its three descendant material slots");
        const auto moved=frame.sceneFrame().transformRebuilds;
        check(moved.localBuilds==1 && moved.worldBuilds==3 && moved.normalBuilds==2,"Parent change rebuilds descendants and one normal matrix per mesh component");
        check(sibling.transformCacheStats().worldBuilds==siblingStats.worldBuilds,"Unrelated siblings retain cached transforms");
        check(exact(child.worldMatrix(),uncached(child)) && exact(nested.worldMatrix(),uncached(nested)),"Cached world matrices match original evaluation exactly");
        check(exact(object(frame,1).normal,transpose(inverseAffine(uncached(nested)))),"Nested nonuniform normal matrix matches uncached inverse transpose");
        stage(frame,scene);check(frame.changedObjects()==0,"Repeated assignment of the same transform remains clean");

        child.transform.scale.x=1.5f;stage(frame,scene);check(frame.changedObjects()==3,"Direct scalar scale writes propagate to descendants");
        stage(frame,scene);nested.color.x=0.13f;stage(frame,scene);
        check(frame.changedObjects()==2 && frame.objectUploads().size()==1 && frame.objectUploads()[0].offset==256,"Component tint marks only its material slots");
        check(frame.sceneFrame().transformRebuilds.normalBuilds==0,"Tint changes do not recompute normal matrices");
        auto mat=std::make_shared<Material>();mat->baseColor.y=0.5f;multipart->materials[1]=mat;stage(frame,scene);
        check(frame.changedObjects()==1 && frame.objectUploads()[0].offset==512 && frame.objectUploads()[0].bytes==sizeof(ObjectUniform),"Material tint changes one final shader uniform");
        child.color.z=0.1f;sibling.color.y=0.2f;stage(frame,scene);
        check(frame.changedObjects()==2 && frame.objectUploads().size()==2,"Separated dirty slots upload separately without including clean slots");
        check(frame.objectUploads()[0].offset==0 && frame.objectUploads()[1].offset==768,"Sparse dirty range offsets are correct");
        child.layer=RenderLayer::Overlay;child.texture=Texture::white();stage(frame,scene);
        check(frame.changedObjects()==1 && object(frame,0).shadow[0]==0,"World-to-overlay change updates only the affected receive-shadow flag");
        child.texture.reset();stage(frame,scene);
        check(frame.changedObjects()==0,"Texture-only changes update bindings without unnecessary uniform writes");

        const auto expectedView=inverseAffine(uncached(camera));check(exact(camera.view(),expectedView),"Cached camera inverse matches original view matrix");
        const auto cameraStats=camera.transformCacheStats();camera.view();camera.view();
        check(camera.transformCacheStats().normalBuilds==cameraStats.normalBuilds,"Static camera reuses inverse/normal cache");
        camera.transform.position.z=9;stage(frame,scene);
        check(frame.changedObjects()==0 && exact(camera.view(),inverseAffine(uncached(camera))),"Camera changes refresh its cache without dirtying object uniforms");
        check(exact(camera.projection(1.5f),perspective(camera.fieldOfView,1.5f,camera.nearPlane,camera.farPlane)),"Cached projection matches original calculation");
        camera.fieldOfView=0.8f;camera.nearPlane=0.2f;camera.farPlane=80;
        check(exact(camera.projection(2),perspective(0.8f,2,0.2f,80)),"Direct lens/aspect changes refresh the projection cache");
        bool invalidLens=false;try {camera.projection(0);} catch(const std::invalid_argument&) {invalidLens=true;}
        check(invalidLens && exact(camera.projection(2),perspective(0.8f,2,0.2f,80)),"Invalid projection does not poison a valid cached lens");

        parent.enabled=false;stage(frame,scene);
        check(frame.sceneFrame().meshes.size()==1 && frame.changedObjects()==1 && frame.objectUploads()[0].offset==0,"Disabling an earlier subtree updates the surviving draw slot");
        parent.transform.position.y=6;parent.enabled=true;stage(frame,scene);
        check(frame.changedObjects()==4 && exact(object(frame,0).model,uncached(child)),"Reenabled subtree incorporates transforms changed while disabled");
        stage(frame,scene);frame.invalidateObjects();frame.prepareObjects();
        check(frame.changedObjects()==4 && frame.objectUploads().size()==1 && frame.objectUploads()[0].bytes==3*256+sizeof(ObjectUniform),"GPU buffer recreation forces all active staged slots to upload");
        stage(frame,scene);check(frame.objectUploads().empty(),"Invalidation does not persist after preparation");
        scene.enabled=false;stage(frame,scene);check(frame.objectUploads().empty() && frame.objectData().empty(),"Empty scenes produce no object uploads");
        scene.enabled=true;stage(frame,scene);check(frame.changedObjects()==4,"Repopulated staging uploads every newly exposed slot");

        const auto custom=Transform{{7,8,9},{0.2f,0.3f,0.4f},{1,1,1}}.matrix();
        scene.visit([&](const Node& node,const Mat4& world) {if(&node==&child) check(exact(world,uncached(child,custom)),"Custom traversal root transforms are included");},custom);
        check(exact(child.worldMatrix(),uncached(child)),"World queries recover the actual hierarchy after an overridden traversal root");
        // Invalid inverses must not poison a successful cached normal on recovery.
        child.transform.scale.x=0;bool rejected=false;
        try {stage(frame,scene);} catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"Singular normal matrices remain rejected");
        child.transform.scale.x=1;stage(frame,scene);check(exact(object(frame,0).normal,transpose(inverseAffine(uncached(child)))),"Valid transform recovers after singular inversion");
        std::cout<<"Transform cache and selective upload tests passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
