#include "../examples/hybrid_terrain/scene.hpp"
#include <iostream>
using namespace engine;
using hybrid_example::check;
int main(){try{
    std::vector<hybrid_example::Entrance> cases{{}};for(int w:{2,5})for(int d:{6,14})for(float h:{2.5f,4.5f})cases.push_back({w,d,h});
    for(auto e:cases){std::cout<<"Pond case "<<e.halfWidth<<","<<e.depth<<","<<e.ceiling<<std::endl;hybrid_example::Demo demo;auto world=demo.make();const auto original=demo.pondPart->mesh;
        if(!(e==demo.entrance)){demo.editEntrance(e);const auto edited=demo.pondPart->mesh;check(demo.undo()&&demo.pondPart->mesh==original,"Pond undo did not restore its surface");check(demo.redo()&&demo.pondPart->mesh==edited,"Pond redo did not restore its surface");}
        demo.verifyPond();const auto p=demo.pondCenter();auto dry=world->physics().raycast({demo.routeX(p.z),-2.4f,p.z},{0,-1,0},2);
        check(dry&&std::abs(dry->point.y+3)<.01f,"Pond changed the dry walking route");
        demo.verifySeams(*world);
        demo.setView(8);check(underwaterView(*world,demo.camera->worldPosition()).has_value(),"Pond underwater view is dry");
        demo.setView(9);check(!underwaterView(*world,demo.camera->worldPosition()),"Pond leaks into lower space");demo.setView(7);
        auto loaded=SceneArchive::load(demo.saveCheckpoint());hybrid_example::Demo restored;restored.bindCheckpoint(loaded);restored.verifyPond();check(restored.view==7&&restored.hasPond(),"Pond view/checkpoint lost");
        check(waterRegion(*restored.pondPart)->outline.size()==7,"Pond outline did not survive save/load");
        check(demo.cavePart->mesh->indices.size()/3<=30000,"Pond exceeded cave geometry budget");
    }
    std::cout<<"PASS cave pond: default/eight dimension extrema, dry route, terrain rim, history, finite optics, collision and checkpoint\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
