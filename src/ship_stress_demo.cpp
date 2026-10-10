#include "engine/ship_stress_demo.hpp"
#include "engine/mesh_optimizer.hpp"
#include <cmath>
#include <stdexcept>

namespace engine {
void ShipStressDemo::setAsset(std::shared_ptr<const Mesh> mesh) {
    if(!mesh||mesh->vertices.empty()||mesh->skeleton) throw std::invalid_argument("The ship stress test needs a static ship mesh");
    if(active()) throw std::logic_error("Stop the ship stress test before replacing its asset");
    if(mesh->lods.empty() && mesh->indices.size()>3000 && mesh->morphTargets.empty()) {
        auto detailed=std::make_shared<Mesh>(*mesh);
        MeshOptimizer::generateLods(*detailed);mesh=std::move(detailed);
    }
    mesh_=std::move(mesh);
}
void ShipStressDemo::setCount(int count) {
    if(count<0||count>maxShips) throw std::invalid_argument("Choose a ship count from 0 to 10,000");
    if(!mesh_) throw std::logic_error("Load the Hansa ship before starting the stress test");
    if(active()&&count==count_) return;
    // Build off-scene so a failed allocation keeps the previous fleet intact.
    auto fleet=std::make_unique<Actor>("Hansa ship stress test");
    if(count) {
        const int columns=int(std::ceil(std::sqrt(float(count))));
        const int rows=(count+columns-1)/columns;
        const float spacing=12.0f/std::max(columns,rows);
        const float scale=std::min(1.0f,spacing*0.8f/4.0f);
        for(int i=0;i<count;++i) {
            const int row=i/columns,column=i%columns;
            const int rowSize=std::min(columns,count-row*columns);
            auto& ship=fleet->addChild<MeshComponent>("Hansa Knarr "+std::to_string(i+1),mesh_);
            ship.color={1,1,1};
            ship.allowInstanceReordering=true;
            ship.transform.position={(column-(rowSize-1)*0.5f)*spacing,0,(row-(rows-1)*0.5f)*spacing};
            ship.transform.scale={scale,scale,scale};
            ship.transform.rotation.y=(i%2)*pi;
        }
    }
    const auto previous=root_;
    auto& next=static_cast<Actor&>(scene_.adoptChild(std::move(fleet)));
    root_=next.id();count_=count;
    if(auto* old=scene_.findActor(previous)) old->destroy();
    scene_.flushPendingDestroy();
}
void ShipStressDemo::stop() {
    if(auto* fleet=scene_.findActor(root_)) fleet->destroy();
    root_={};count_=0;scene_.flushPendingDestroy();
}
} // namespace engine
