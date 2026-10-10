#include "engine/ship_stress_demo.hpp"
#include "engine/render_frame.hpp"
#include <iostream>
#include <set>

using namespace engine;
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int main() {
    try {
        Scene scene;ShipStressDemo demo(scene);
        auto mesh=Mesh::cube();demo.setAsset(mesh);
        for(int count:{1,2,7,100,ShipStressDemo::maxShips,3,0}) {
            demo.setCount(count);scene.update(0);
            check(demo.active()&&demo.count()==count,"Fleet count/state mismatch");
            check(scene.children().size()==1,"Old fleets were not released");
            check(demo.root()->children().size()==std::size_t(count),"Wrong number of ship meshes");
            std::set<std::pair<float,float>> positions;
            for(const auto& node:demo.root()->children()) {
                auto* ship=dynamic_cast<const MeshComponent*>(node.get());
                check(ship&&ship->mesh==mesh,"Ships must share one mesh asset");
                const auto p=ship->transform.position;
                check(std::abs(p.x)<6&&std::abs(p.z)<6&&p.y==0,"Ship outside scene ground");
                check(ship->transform.scale.x>0,"Ship scale must remain positive");
                check(positions.emplace(p.x,p.z).second,"Overlapping ship centers");
            }
        }
        demo.setCount(7);const auto previous=demo.root()->id();
        for(int invalid:{-1,ShipStressDemo::maxShips+1}) {
            bool rejected=false;try { demo.setCount(invalid); } catch(const std::invalid_argument&) { rejected=true; }
            check(rejected&&demo.count()==7&&demo.root()->id()==previous,"Invalid count changed the fleet");
        }
        demo.stop();scene.update(0);
        check(!demo.active()&&scene.children().empty()&&!scene.findActor(previous),"Stopping must release all ships");
        check(demo.hasAsset(),"Stopping should preserve the reusable asset");
        demo.setCount(1);check(demo.count()==1,"Restart failed");
        demo.stop();
        auto knarr=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"Knarr.fbx");
        demo.setAsset(knarr);RenderFrame frame;
        check(knarr->parts.size()==7,"Knarr has seven material ranges");
        for(int count:{100,1000}) {
            demo.setCount(count);scene.update(0);frame.collect(scene);frame.prepareObjects();
            check(frame.cameraBatches(RenderLayer::World).size()==7 && frame.shadowBatches().size()==7,"Fleet batches all seven ranges in camera and shadow passes");
            for(auto batch:frame.batches(RenderLayer::World)) {
                check(batch.instanceCount==std::uint32_t(count),"Each part batch includes every ship");
                const auto& first=frame.instanceDraw(batch.firstInstance);
                for(std::uint32_t i=0;i<batch.instanceCount;++i) {
                    const auto& draw=frame.instanceDraw(batch.firstInstance+i);
                    check(draw.mesh==first.mesh && draw.firstIndex==first.firstIndex && sameMaterialBinding(draw.material,first.material),"Grouped instances retain compatible geometry and materials");
                }
            }
            frame.collect(scene);frame.prepareObjects();
            check(frame.changedObjects()==0 && frame.storageGrowths()==0,"Warm grouped fleets retain storage and skip object uploads");
            frame.collect(scene,false);frame.prepareObjects();
            check(frame.cameraBatches(RenderLayer::World).size()==std::size_t(count)*7 && frame.shadowBatches().size()==std::size_t(count)*7,"Disabling instancing restores every ship part draw");
        }
        std::cout<<"Ship stress demo tests passed\n";return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
