#include "engine/render_frame.hpp"
#include "engine/camera_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/fx_frame.hpp"
#include <iostream>
#include <limits>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
}
int main() {
    try {
        Scene scene;CameraComponent camera;camera.setOrthographic(4,1,10);
        auto cube=Mesh::cube();
        auto& a=scene.addChild<MeshComponent>("A",cube);a.transform.position={0,0,-5};
        auto& b=scene.addChild<MeshComponent>("B",cube);b.transform.position={20,0,-5};
        auto& c=scene.addChild<MeshComponent>("C",cube);c.transform.position={1,0,-5};
        RenderFrame frame;
        auto collect=[&] { frame.collect(scene,true,~0u,&camera.frustum(1));frame.prepareObjects(); };
        collect();
        check(frame.sceneFrame().culledMeshes==1 && frame.sceneFrame().meshCount==3,"Culling counts mesh components");
        check(frame.batches(RenderLayer::World).size()==1 && frame.batches(RenderLayer::World)[0].instanceCount==3,"Off-screen shadow casters remain available");
        auto batches=frame.cameraBatches(RenderLayer::World);
        check(batches.size()==2 && batches[0].firstInstance==0 && batches[1].firstInstance==2,"Culling splits instancing at hidden slots");
        for(const Vec3 p:{Vec3{-20,0,-5},Vec3{20,0,-5},Vec3{0,-20,-5},Vec3{0,20,-5},Vec3{0,0,1},Vec3{0,0,-20}}) {
            b.transform.position=p;collect();check(!frame.sceneFrame().meshes[1].cameraVisible,"All six clipping planes reject separated meshes");
        }
        b.transform.position={2.5f,0,-5};collect();check(frame.sceneFrame().meshes[1].cameraVisible,"Touching a plane retains the mesh");
        b.transform.position={20,0,-5};b.frustumCulling=false;collect();check(frame.sceneFrame().culledMeshes==0,"Per-mesh opt-out bypasses culling");
        b.frustumCulling=true;
        auto& child=b.addChild<MeshComponent>("Visible child",cube);child.transform.position={-20,0,0};
        collect();check(!frame.sceneFrame().meshes[1].cameraVisible && frame.sceneFrame().meshes[2].cameraVisible,"Culled parent mesh does not suppress child traversal");
        child.enabled=false;
        b.blend=BlendMode::Alpha;collect();FxFrame fx;fx.prepare(frame.sceneFrame(),camera.view());
        check(fx.batches(RenderLayer::World).empty(),"Transparent meshes use the same culling result");
        b.blend=BlendMode::Opaque;
        const auto bytes=frame.storageBytes();
        for(int i=0;i<1000;++i) { collect();check(frame.storageGrowths()==0 && frame.storageBytes()==bytes,"Warm culling retains frame capacities"); }
        camera.transform.position={20,0,0};collect();
        check(frame.sceneFrame().meshes[1].cameraVisible && !frame.sceneFrame().meshes[0].cameraVisible,"Camera movement refreshes visibility");
        check(frame.changedObjects()==0,"Camera culling preserves object slots without uploads");
        camera.transform={};camera.setPerspective(pi/2,1,10);
        b.transform.position={8,0,-5};collect();check(!frame.sceneFrame().meshes[1].cameraVisible,"Perspective sides reject outside bounds");
        frame.collect(scene,true,~0u,&camera.frustum(2));check(frame.sceneFrame().meshes[1].cameraVisible,"Viewport aspect changes visibility");
        camera.setOrthographic(4,1,10);
        a.transform.scale={-2,0.3f,3};a.transform.rotation={0.3f,0.7f,0.2f};collect();
        check(frame.sceneFrame().meshes[0].cameraVisible,"Rotated negative nonuniform scale stays visible");
        Mat4 shear=Mat4::identity();shear.m[4]=2;shear.m[14]=-5;
        check(camera.frustum(1).intersectsTransformedAabb({-0.5f,-0.5f,-0.5f},{0.5f,0.5f,0.5f},shear),"Affine shear is supported");
        b.visibilityMask=2;frame.collect(scene,true,1,&camera.frustum(1));check(frame.sceneFrame().meshCount==2,"Visibility mask is applied before culling");
        frame.collect(scene,true,~0u);check(frame.sceneFrame().culledMeshes==0,"Camera-free collection remains unculled");
        check(frame.cameraBatches(RenderLayer::World).size()==frame.batches(RenderLayer::World).size(),"Disabling culling restores every batch");
        auto edited=std::make_shared<Mesh>(*cube);
        check(edited->localBounds().maximum.x==0.5f,"Copied mesh refreshes its local cache");
        for(auto& v:edited->vertices) v.position.x+=20;
        edited->invalidateBounds();a.mesh=edited;collect();check(!frame.sceneFrame().meshes[0].cameraVisible,"Resource replacement and explicit invalidation refresh bounds");
        edited->vertices[0].position.x=std::numeric_limits<float>::quiet_NaN();edited->invalidateBounds();collect();
        check(frame.sceneFrame().meshes[0].cameraVisible,"Unknown or invalid bounds conservatively bypass rejection");

        auto rig=std::make_shared<Skeleton>();rig->joints.push_back({"Root",-1,{}});rig->bindings={{},{0,Mat4::identity()}};
        auto animated=std::make_shared<Mesh>(*cube);animated->skeleton=rig;animated->skin.resize(animated->vertices.size());
        for(auto& v:animated->skin) v.joints[0]=1;
        Scene moving;auto& skinned=moving.addChild<SkeletalMeshComponent>("Animated",animated);skinned.transform.position={0,0,-5};
        auto pose=std::make_shared<SkinPose>();pose->bones.resize(2,{Mat4::identity(),Mat4::identity()});pose->revision=1;
        auto bounds=skinned.localBounds(pose);check(bounds.valid && bounds.minimum.x==-0.5f,"Skin bounds match bind pose");
        pose->bones[1].transform.m[12]=20;++pose->revision;
        bounds=skinned.localBounds(pose);check(bounds.minimum.x==19.5f,"Pose revision refreshes deformed bounds");
        check(!camera.frustum(1).intersectsTransformedAabb(bounds.minimum,bounds.maximum,skinned.worldMatrix()),"Animated bounds can leave the frustum");
        pose->bones[1].transform.m[12]=0;++pose->revision;bounds=skinned.localBounds(pose);
        check(camera.frustum(1).intersectsTransformedAabb(bounds.minimum,bounds.maximum,skinned.worldMatrix()),"Animated bounds can reenter the frustum");
        frame.collect(moving,true,~0u,&camera.frustum(1));check(frame.sceneFrame().culledMeshes==0,"Skeletal collection uses live bounds");
        std::cout<<"Frustum culling, stable batching, transparency, hierarchy, bounds and animation tests passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
