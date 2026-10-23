#include "../examples/hybrid_terrain/scene.hpp"
#include <iostream>
using namespace engine;
using hybrid_example::check;
int main(int argc,char** argv){try{
    if(argc>1){auto old=SceneArchive::loadFile(argv[1]);hybrid_example::Demo legacy;legacy.bindCheckpoint(old);
        check(legacy.hasSystem()&&!legacy.hasCurves(),"Legacy straight layout not retained");
        legacy.walkSystem();for(int i=0;i<3000&&legacy.moving;++i){legacy.update();old.scene->fixedUpdate(1.f/60);}
        check(!legacy.moving&&std::abs(legacy.walker->transform.position.x)<.1f,"Legacy straight route no longer walks");
    }
    std::vector<hybrid_example::Entrance> cases{{}};
    for(int w:{2,5})for(int d:{6,14})for(float h:{2.5f,4.5f})cases.push_back({w,d,h});
    for(auto e:cases){
        std::cout<<"Case "<<e.halfWidth<<" "<<e.depth<<" "<<e.ceiling<<std::endl;
        hybrid_example::Demo demo;auto world=demo.make();if(!(e==demo.entrance))demo.editEntrance(e);
        const auto footprint=demo.meshHoleFootprint(e);
        check(std::count(footprint.begin(),footprint.end(),std::uint8_t(1))==2*e.halfWidth*(e.depth+3)-4,"Mesh rim cut differs from authored entrance");
        check(!footprint[9*32+16-e.halfWidth]&&footprint[9*32+16],"Mesh-shaped cut erased a retained corner or blocked the mouth");
        std::cout<<"Triangles "<<demo.cavePart->mesh->indices.size()/3<<std::endl;
        check(demo.hasCurves(),"Curved layout not identified");
        auto dimensions=[&](Vec3 centre){const auto eye=centre+Vec3{0,1.2f,0};
            const auto left=world->physics().raycast(eye,{-1,0,0},20),right=world->physics().raycast(eye,{1,0,0},20);
            const auto roof=world->physics().raycast(eye,{0,1,0},14),floor=world->physics().raycast(eye,{0,-1,0},2);
            check(left&&right&&roof&&floor,"Cave cross-section query escaped enclosure");
            return Vec2{right->point.x-left->point.x,roof->point.y-floor->point.y};};
        const auto tunnel=dimensions({demo.routeX(e.depth+3.f),-.8f,e.depth+3.f});
        for(auto room:std::array<Vec3,3>{Vec3{0,0,float(std::min(6,e.depth-2))},Vec3{-6,-3,float(e.depth)+11},Vec3{6,-8,demo.lowerCentreZ()}}){
            const auto size=dimensions(room);check(size.x>tunnel.x*1.2f&&size.y>tunnel.y+.1f,"Chamber is not wider and taller than its passage");
            auto floor=world->physics().raycast(room+Vec3{0,1,0},{0,-1,0},2);
            auto roof=world->physics().raycast(room+Vec3{0,1,0},{0,1,0},14);
            check(floor&&std::abs(floor->point.y-room.y)<.1f&&roof,"Offset chamber enclosure: width="+std::to_string(e.halfWidth)+" depth="+std::to_string(e.depth)+" x="+std::to_string(room.x)+" z="+std::to_string(room.z)+" floor="+(floor?std::to_string(floor->point.y):"none")+" roof="+(roof?std::to_string(roof->point.y):"none"));
        }
        check(demo.hasLargeGrotto(),"Enlarged grotto not identified");demo.verifyBasin();
        const auto large=dimensions({6,-8,demo.lowerCentreZ()});
        check(large.x>e.halfWidth*2*1.1f*hybrid_example::grottoScale*.8f&&large.y>9.8f,"Lower grotto did not enlarge in width and height");
        demo.walkSystem();float minX=0,maxX=0;
        for(int i=0;i<3000&&demo.moving;++i){demo.update();world->fixedUpdate(1.f/60);minX=std::min(minX,demo.walker->transform.position.x);maxX=std::max(maxX,demo.walker->transform.position.x);}
        check(!demo.moving&&minX<-5.5f&&maxX>5.5f&&std::abs(demo.walker->transform.position.y+8)<.1f,"Curved descent failed");
        auto saved=SceneArchive::load(demo.saveCheckpoint());hybrid_example::Demo restored;restored.bindCheckpoint(saved);check(restored.hasCurves()&&restored.hasLargeGrotto(),"Curved checkpoint lost layout");restored.verifyBasin();
        restored.walk(false);for(int i=0;i<3000&&restored.moving;++i){restored.update();saved.scene->fixedUpdate(1.f/60);}
        check(!restored.moving&&restored.walker->transform.position.z<-6.8f&&std::abs(restored.walker->transform.position.x)<.1f,"Restored curved ascent failed");
        check(demo.cavePart->mesh->indices.size()/3<=30000,"Curved caves exceeded bounded mesh size");
    }
    std::cout<<"PASS curved cave layout, extrema, offset rooms, motor descent/ascent and saved route\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
