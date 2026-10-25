#pragma once
#include "save_game.hpp"
#include "engine/ai.hpp"
#include "engine/camera_controller.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene.hpp"
#include <cmath>

namespace rpg_example {
namespace r=engine::rpg;
inline engine::Vec3 point(r::TacticalPosition p){return {float(p.x),float(p.y),float(p.z)};}
inline std::shared_ptr<const engine::WaypointGraph> navigation(){
    std::vector<engine::Vec3> points;std::vector<engine::WaypointEdge> edges;
    for(int z=-2;z<=2;++z)for(int x=-4;x<=4;++x)if(x||z)points.push_back({float(x),0,float(z)});
    for(std::uint32_t a=0;a<points.size();++a)for(std::uint32_t b=0;b<points.size();++b)
        if(std::abs(points[a].x-points[b].x)+std::abs(points[a].z-points[b].z)==1)edges.push_back({a,b});
    return std::make_shared<engine::WaypointGraph>(std::move(points),std::move(edges));
}
struct HostScene:engine::Scene {
    engine::ComponentId camera;
    std::map<r::EntityId,engine::ActorId> actors;
    std::map<r::EntityId,engine::ComponentId> bodies;
    std::unique_ptr<engine::CameraController> rig;
    engine::OrbitCameraBehaviour* orbit=nullptr;
    explicit HostScene(const r::State&state,unsigned location):Scene(location?"Practice garden":"Training court"){
        using namespace engine;
        auto material=[](Vec3 color){auto m=std::make_shared<Material>();m->shader=MaterialShader::Unlit;m->baseColor=color;return m;};
        auto cube=[&](Actor&a,const char*name,Vec3 p,Vec3 size,Vec3 color)->MeshComponent&{
            auto&m=a.addComponent<MeshComponent>(name,Mesh::cube());m.transform.position=p;m.transform.scale=size;m.setMaterial(0,MaterialInstance{material(color)});return m;};
        cube(spawn("Ground"),"Ground",{0,-.2f,0},{12,.3f,9},location?Vec3{.12f,.22f,.16f}:Vec3{.12f,.17f,.23f});
        for(int z=-2;z<=2;++z)for(int x=-4;x<=4;++x)if(x||z)
            cube(spawn("Path tile"),"Tile",{float(x),-.02f,float(z)},{.92f,.07f,.92f},((x+z)&1)?Vec3{.28f,.35f,.38f}:Vec3{.34f,.41f,.43f});
        cube(spawn("Blocked pillar"),"Pillar",{0,1,0},{.9f,2,.9f},{.65f,.58f,.4f});
        for(auto&[id,ch]:state.characters){
            auto&a=spawn(id==state.characters.begin()->first?"Scholar":"Partner");a.transform.position=point(state.positions.at(id));actors.emplace(id,a.id());
            auto color=id==state.characters.begin()->first?Vec3{.15f,.72f,.88f}:Vec3{.91f,.42f,.23f};
            auto&body=cube(a,"Body",{0,.75f,0},{.55f,1.5f,.55f},color);bodies.emplace(id,body.id());
            cube(a,"Head",{0,1.65f,0},{.4f,.4f,.4f},{.9f,.82f,.65f});
        }
        auto&cam=spawn("Third person camera").addComponent<CameraComponent>();camera=cam.id();
        rig=std::make_unique<CameraController>(*this,camera);orbit=&rig->add<OrbitCameraBehaviour>();
        orbit->target=CameraTarget(actors.begin()->second);orbit->target.offset={0,1,0};orbit->distance=9;orbit->yaw=.5f;orbit->elevation=.55f;
        rig->update(0,1.5f);
    }
    engine::CameraComponent& cameraView(){return *dynamic_cast<engine::CameraComponent*>(findComponent(camera));}
    void project(const r::State&state,std::optional<r::EntityId> moving={},engine::Vec3 visual={}){
        for(auto&[id,handle]:actors){auto*a=findActor(handle);if(!a)throw std::runtime_error("Stale RPG actor binding");a->transform.position=moving==id?visual:point(state.positions.at(id));}
    }
    void inspect(float yaw,float elevation,float zoom){orbit->yaw+=yaw;orbit->elevation=std::clamp(orbit->elevation+elevation,.15f,1.2f);orbit->distance=std::clamp(orbit->distance+zoom,4.f,16.f);}
};
} // namespace rpg_example
