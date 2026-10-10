#include "engine/render_frame.hpp"
#include "engine/mesh_component.hpp"
#include <cstring>
#include <iostream>

using namespace engine;
namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
ObjectUniform object(const RenderFrame& frame,std::size_t i) {
    ObjectUniform data{};std::memcpy(&data,frame.objectData().data()+i*RenderFrame::objectStride,sizeof(data));return data;
}
}
int main() {
    try {
        Scene scene;auto cube=Mesh::cube();
        auto& a=scene.addChild<MeshComponent>("A",cube);
        auto& b=scene.addChild<MeshComponent>("B",cube);
        b.transform.position={2,3,4};b.transform.scale={2,3,4};b.color={1,0,0};
        auto& c=scene.addChild<MeshComponent>("C",cube);c.texture=Texture::white();
        auto& d=scene.addChild<MeshComponent>("Texture break",cube);
        d.texture=std::make_shared<Texture>(*Texture::white());
        scene.addChild<MeshComponent>("Order break",cube);
        auto& overlay=scene.addChild<MeshComponent>("Overlay",cube);overlay.layer=RenderLayer::Overlay;
        scene.addChild<MeshComponent>("World after overlay",cube);
        auto multi=std::make_shared<Mesh>(*cube);multi->materials.resize(2);multi->parts={{0,18,0},{18,18,1}};
        scene.addChild<MeshComponent>("Material ranges",multi);
        RenderFrame frame;frame.collect(scene);frame.prepareObjects();
        auto world=frame.batches(RenderLayer::World);
        check(world.size()==6,"Texture, range, mesh and noncontiguous slot boundaries must split batches");
        check(world[0].firstInstance==0 && world[0].instanceCount==3,"Different transforms and tints share a batch, including explicit white fallback");
        check(world[1].firstInstance==3 && world[2].firstInstance==4,"Do not sort across a texture break");
        check(world[3].firstInstance==6 && world[4].firstInstance==7 && world[5].firstInstance==8,"Keep original object slot and material range order");
        check(frame.batches(RenderLayer::Overlay).size()==1 && frame.batches(RenderLayer::Overlay)[0].firstInstance==5,"Keep layers separate");
        const auto oa=object(frame,0),ob=object(frame,1);
        check(transformPoint(ob.model,{}).x==2 && ob.color[0]==1 && oa.color[0]!=ob.color[0],"Batch retains individual transforms and tints");
        check(std::abs(ob.normal.m[0]-0.5f)<0.0001f && std::abs(ob.normal.m[5]-1.0f/3)<0.0001f,"Each instance retains its nonuniform normal transform");
        frame.collect(scene,false);frame.prepareObjects();
        check(frame.batches(RenderLayer::World).size()==8 && frame.changedObjects()==0,"Disabling batching changes submission without reuploading instance slots");
        frame.collect(scene);frame.prepareObjects();const auto bytes=frame.storageBytes();
        check(frame.changedObjects()==0,"Reenabling batching keeps staging unchanged");
        b.color={0,1,0};frame.collect(scene);frame.prepareObjects();
        check(frame.changedObjects()==1 && frame.objectUploads().size()==1 && frame.objectUploads()[0].offset==256 && frame.objectUploads()[0].bytes==sizeof(ObjectUniform),"Changing an instance uploads only its original slot");
        for(int i=0;i<1000;++i) {
            frame.collect(scene);frame.prepareObjects();
            check(frame.storageGrowths()==0 && frame.storageBytes()==bytes,"Warm batch lists retain capacity");
        }
        d.texture.reset();frame.collect(scene);frame.prepareObjects();
        check(frame.batches(RenderLayer::World)[0].instanceCount==5 && frame.changedObjects()==0,"Resource-only changes rebuild batches without object uploads");
        overlay.enabled=false;frame.collect(scene);frame.prepareObjects();
        check(frame.batches(RenderLayer::Overlay).empty() && frame.batches(RenderLayer::World)[0].instanceCount==6,"Visibility updates instance offsets and removes stale batches");
        scene.enabled=false;frame.collect(scene);frame.prepareObjects();
        check(frame.batches(RenderLayer::World).empty() && frame.objectData().empty(),"Empty scenes clear batches");
        scene.enabled=true;a.enabled=false;frame.collect(scene);frame.prepareObjects();
        check(frame.batches(RenderLayer::World)[0].firstInstance==0 && frame.batches(RenderLayer::World)[0].instanceCount==5,"Restored scene repacks first-instance addresses correctly");
        Scene grouped;
        auto& g1=grouped.addChild<MeshComponent>("Ship one",multi);g1.allowInstanceReordering=true;
        auto& g2=grouped.addChild<MeshComponent>("Ship two",multi);g2.allowInstanceReordering=true;
        g2.transform.position={2,0,0};g2.color={1,0,0};g2.castsShadow=false;
        frame.collect(grouped);frame.prepareObjects();
        check(frame.batches(RenderLayer::World).size()==2,"Nonadjacent matching ranges combine when opted in");
        for(auto batch:frame.batches(RenderLayer::World)) {
            check(batch.instanceCount==2,"Each material range contains both ships");
            for(std::uint32_t i=0;i<batch.instanceCount;++i) {
                const auto slot=batch.firstInstance+i;const auto& draw=frame.instanceDraw(slot);const auto uniform=object(frame,slot);
                check(uniform.model.m==draw.model.m && uniform.color[0]==draw.color.x,"GPU slots retain mapped transforms and tint");
            }
        }
        check(frame.shadowBatches().size()==2 && frame.shadowBatches()[0].instanceCount==1,"Shadow batches split noncasters without changing color batching");
        g2.materialOverrides.resize(2);MaterialInstance override;override.roughness=0.1f;g2.setMaterial(0,override);
        frame.collect(grouped);check(frame.batches(RenderLayer::World).size()==3,"Different material bindings split reordered ranges");
        g2.clearMaterial(0);g2.transform.scale={-1,1,1};frame.collect(grouped);
        check(frame.batches(RenderLayer::World).size()==4,"Mirrored instances split reordered ranges");
        g2.transform.scale={1,1,1};g2.allowInstanceReordering=false;frame.collect(grouped);
        check(frame.batches(RenderLayer::World).size()==4,"Order-sensitive objects terminate reorderable runs");
        std::cout<<"Instancing tests passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
