#include "engine/render_frame.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/camera_component.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>

namespace {
bool countAllocations=false;
std::size_t allocations=0;
void* allocate(std::size_t size) {
    if(countAllocations) ++allocations;
    if(auto* data=std::malloc(size?size:1)) return data;
    throw std::bad_alloc();
}
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
}
// Scene/frame vectors and the traversal callback use ordinary operator new.
// Count real allocations rather than inferring allocation behavior from timing.
void* operator new(std::size_t size) {return allocate(size);}
void* operator new[](std::size_t size) {return allocate(size);}
void operator delete(void* data) noexcept {std::free(data);}
void operator delete[](void* data) noexcept {std::free(data);}
void operator delete(void* data,std::size_t) noexcept {std::free(data);}
void operator delete[](void* data,std::size_t) noexcept {std::free(data);}

using namespace engine;
int main() {
    try {
        Scene scene;auto& actor=scene.addChild<Actor>("Moving parent");
        auto& overlay=actor.addChild<MeshComponent>("Overlay",Mesh::cube());overlay.layer=RenderLayer::Overlay;
        auto multi=std::make_shared<Mesh>(*Mesh::cube());multi->materials.resize(2);multi->parts={{0,18,0},{18,18,1}};
        auto mat=std::make_shared<Material>();mat->baseColor={0.5f,0.7f,0.9f};multi->materials[1]=mat;
        auto& world=scene.addChild<MeshComponent>("World",multi);world.color={0.8f,0.6f,0.4f};
        auto& background=scene.addChild<MeshComponent>("Background",Mesh::plane());background.layer=RenderLayer::Background;
        auto& light=actor.addChild<LightComponent>("Light",LightType::Point);
        RenderFrame frame;frame.collect(scene);frame.prepareObjects();
        const auto& collected=frame.sceneFrame();
        check(collected.meshCount==3 && collected.meshes.size()==4,"Parts retain flat traversal order and component counts");
        check(collected.layerDraws[0]==std::vector<std::uint32_t>{3} && collected.layerDraws[1]==std::vector<std::uint32_t>({1,2}) && collected.layerDraws[2]==std::vector<std::uint32_t>{0},"Layer lists preserve material order and original uniform indices");
        const auto* draws=collected.meshes.data();const auto* lights=collected.lights.data();
        const auto* objects=frame.objectData().data();const auto bytes=frame.storageBytes();
        check(frame.storageGrowths()>0,"Initial storage growth is measured");
        allocations=0;countAllocations=true;
        for(int i=0;i<1000;++i) {
            actor.transform.position={float(i),2,3};
            frame.collect(scene);frame.prepareObjects();
            check(frame.storageGrowths()==0 && frame.storageBytes()==bytes,"Steady frames must retain capacity");
        }
        countAllocations=false;
        check(allocations==0,"Warm scene collection and object preparation must perform zero heap allocations");
        check(draws==collected.meshes.data() && lights==collected.lights.data() && objects==frame.objectData().data(),"Steady frames reuse the same buffers");
        ObjectUniform uniform{};std::memcpy(&uniform,frame.objectData().data(),sizeof(uniform));
        check(transformPoint(uniform.model,{}).x==999 && transformPoint(uniform.model,{}).y==2,"Reused uniforms still update transforms");
        check(collected.lights[0].position.x==999,"Reused light list updates parent transforms");
        std::memcpy(&uniform,frame.objectData().data()+2*RenderFrame::objectStride,sizeof(uniform));
        check(std::abs(uniform.color[0]-0.4f)<0.0001f && uniform.color[3]==1,"Reused staging preserves per-material tint");
        for(std::size_t i=0;i<collected.meshes.size();++i)
            for(std::size_t j=sizeof(ObjectUniform);j<RenderFrame::objectStride;++j)
                check(frame.objectData()[i*RenderFrame::objectStride+j]==std::byte{},"Uniform alignment padding remains initialized");

        actor.enabled=false;light.enabled=false;world.enabled=false;background.enabled=false;
        frame.collect(scene);frame.prepareObjects();
        check(frame.objectData().empty() && collected.meshes.empty() && collected.lights.empty(),"Empty frames remove stale draws, lights and upload bytes");
        check(frame.storageBytes()==bytes && frame.storageGrowths()==0,"Empty frame retains reusable capacity");
        for(const auto& layer:collected.layerDraws) check(layer.empty(),"Empty frame clears every layer list");
        actor.enabled=true;world.enabled=true;background.enabled=true;light.enabled=true;
        frame.collect(scene);frame.prepareObjects();check(frame.storageGrowths()==0,"Reenabling components within capacity avoids growth");
        overlay.layer=RenderLayer::World;frame.collect(scene);frame.prepareObjects();
        check(collected.layerDraws[2].empty() && collected.layerDraws[1].front()==0,"Layer changes discard stale layer entries");
        auto& expanded=scene.addChild<Actor>("Larger scene");
        for(int i=0;i<150;++i) expanded.addChild<MeshComponent>("Shared cube",multi).allowInstanceReordering=true;
        frame.collect(scene);frame.prepareObjects();const auto peak=frame.storageBytes();
        check(peak>bytes && frame.storageGrowths()>0,"Scene growth expands draw lists and staging safely");
        check(collected.meshes.size()==304,"Expanded scene includes all material draws");
        frame.collect(scene);frame.prepareObjects();check(frame.storageGrowths()==0,"Larger steady scene reuses grown capacity");
        allocations=0;countAllocations=true;
        for(int i=0;i<100;++i) {frame.collect(scene);frame.prepareObjects();}
        countAllocations=false;
        check(allocations==0,"Warm reordered multi-material batches allocate zero heap blocks");
        expanded.enabled=false;frame.collect(scene);frame.prepareObjects();
        check(frame.storageBytes()==peak && frame.objectData().size()==4*RenderFrame::objectStride,"Smaller scenes upload only active bytes while retaining capacity");
        expanded.enabled=true;frame.collect(scene);frame.prepareObjects();check(frame.storageGrowths()==0,"Regrowth below the high-water mark reuses storage");

        Scene resources;auto owned=std::make_shared<Mesh>(*Mesh::cube());
        auto texture=std::make_shared<Texture>(*Texture::white());owned->materials={std::make_shared<const Material>(Material{"Material",{1,1,1},texture,{}})};owned->parts={{0,36,0}};
        std::weak_ptr<const Mesh> meshWeak=owned;std::weak_ptr<const Texture> textureWeak=texture;
        auto& component=resources.addChild<MeshComponent>("Temporary",owned);
        frame.collect(resources);frame.prepareObjects();owned.reset();texture.reset();component.mesh.reset();
        check(!meshWeak.expired() && !textureWeak.expired(),"Collected frame owns its current resources");
        frame.collect(resources);frame.prepareObjects();
        check(meshWeak.expired() && textureWeak.expired(),"Refilling releases obsolete mesh and material texture references");
        auto& invalid=resources.addChild<MeshComponent>("Invalid layer",Mesh::cube());invalid.layer=RenderLayer::Count;
        bool rejected=false;try {frame.collect(resources);} catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"Invalid layers are rejected before indexing layer storage");
        invalid.enabled=false;frame.collect(resources);frame.prepareObjects();
        check(frame.sceneFrame().meshes.empty(),"Collection recovers cleanly after an invalid frame");
        auto rig=std::make_shared<Skeleton>();rig->joints.push_back({"Root",-1,{}});rig->bindings={{},{0,Mat4::identity()}};
        JointPose rotated;rotated.rotation={0,0,std::sin(0.5f),std::cos(0.5f)};
        rig->clips.push_back({"Turn",2,2,{{},rotated}});
        auto animatedMesh=std::make_shared<Mesh>(*Mesh::cube());animatedMesh->skeleton=rig;animatedMesh->skin.resize(animatedMesh->vertices.size());
        for(auto& vertex:animatedMesh->skin) vertex.joints[0]=1;
        Scene animated;auto& skinned=animated.addChild<SkeletalMeshComponent>("Rig",animatedMesh);skinned.animation().play(0);
        frame.collect(animated);frame.prepareObjects();const auto revision=skinned.skinPose()->revision;
        allocations=0;countAllocations=true;
        for(int i=0;i<1000;++i) {
            animated.updateAnimations(0.016);frame.collect(animated);frame.prepareObjects();
            check(frame.storageGrowths()==0,"Warm animated frames must retain all capacities");
        }
        countAllocations=false;
        check(allocations==0 && skinned.skinPose()->revision>revision,"Warm clip sampling, bone palettes, collection and staging must allocate zero heap blocks");
        check(frame.changedObjects()==0,"Bone motion does not change mesh placement slots");
        CameraComponent camera;camera.transform.position={0,0,5};
        frame.collect(animated,true,~0u,&camera.frustum(1));frame.prepareObjects();
        allocations=0;countAllocations=true;
        for(int i=0;i<1000;++i) {
            animated.updateAnimations(0.016);frame.collect(animated,true,~0u,&camera.frustum(1));frame.prepareObjects();
            check(frame.storageGrowths()==0,"Warm animated culling retains capacities");
        }
        countAllocations=false;
        check(allocations==0,"Animated bounds, frustum collection and visible batches allocate zero heap blocks after warmup");
        std::cout<<"Render frame tests passed: 1000 warm collections/preparations allocated zero heap blocks\n";
    } catch(const std::exception& error) {countAllocations=false;std::cerr<<error.what()<<'\n';return 1;}
}
