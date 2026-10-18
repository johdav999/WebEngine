#pragma once
// Application/example content, deliberately outside the portable engine API.
#include "engine/vehicle_component.hpp"
#include "engine/scene.hpp"
#include "engine/terrain.hpp"
#include "engine/mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
namespace engine {
inline std::unique_ptr<Actor> makeVehicleExample(Vec3 position={0,.95f,0}) {
    auto car=std::make_unique<Actor>("Vehicle chassis");car->transform.position=position;
    RigidBodySettings settings;settings.mass=1200;settings.centerOfMassOffset={0,-.25f,0};settings.continuousCollision=true;
    car->addComponent<RigidBodyComponent>("Chassis body",settings);
    car->addComponent<ColliderComponent>("Chassis collider",CollisionShape::box({.8f,.25f,1.75f}));
    auto& visual=car->addComponent<MeshComponent>("Chassis visual",Mesh::cube());visual.transform.scale={1.6f,.5f,3.5f};visual.color={.1f,.45f,.95f};
    auto& vehicle=car->addComponent<VehicleComponent>();std::array<std::string,4> ids;
    for(int i=0;i<4;++i){auto& wheel=car->addComponent<MeshComponent>("Wheel "+std::to_string(i),Mesh::cube());wheel.transform.scale={.2f,.7f,.7f};wheel.color={.08f,.08f,.08f};ids[i]=wheel.savedId();}
    vehicle.setVisuals(ids);return car;
}
inline void populateVehicleCourse(Scene& scene) {
    auto solid=[&](std::string name,Vec3 position,Vec3 half,Vec3 rotation,Vec3 color){auto& a=scene.spawn(std::move(name));a.transform.position=position;a.transform.rotation=rotation;
        RigidBodySettings s;s.motion=BodyMotion::Static;a.addComponent<RigidBodyComponent>("Static body",s);a.addComponent<ColliderComponent>("Surface collider",CollisionShape::box(half));
        auto& mesh=a.addComponent<MeshComponent>("Surface visual",Mesh::cube());mesh.transform.scale=half*2;mesh.color=color;};
    solid("Flat lane",{0,-.25f,30},{30,.25f,90},{},{.35f,.4f,.35f});
    solid("Hill",{-12,.8f,20},{4,.3f,8},{-.1f,0,0},{.5f,.5f,.55f});
    solid("Bank",{12,.5f,20},{4,.3f,8},{0,0,.12f},{.55f,.45f,.35f});
    solid("Kerb",{-4,.12f,10},{.2f,.12f,4},{},{.9f,.8f,.2f});
    solid("Ramp",{0,.7f,40},{3,.3f,6},{-.18f,0,0},{.6f,.4f,.2f});
    solid("Wall",{0,1,70},{8,1,.5f},{},{.75f,.3f,.25f});
    scene.adoptChild(makeVehicleExample());
}
inline void populatePhysicalSurfaceCourse(Scene& scene) {
    const char* names[]={"asphalt","gravel","ice"};const float friction[]={1,.6f,.04f},grip[]={1,.65f,.15f};
    const Vec3 colors[]={{.25f,.28f,.32f},{.6f,.45f,.25f},{.5f,.8f,.95f}};
    for(int i=0;i<3;++i){auto surface=std::make_shared<PhysicalSurface>();surface->id=names[i];surface->friction=friction[i];surface->tyreGrip=grip[i];
        auto& lane=scene.spawn(std::string(names[i])+" lane");lane.setSavedId("surface.lane."+std::to_string(i));lane.transform.position={float((i-1)*14),-.5f,60};
        RigidBodySettings body;body.motion=BodyMotion::Static;lane.addComponent<RigidBodyComponent>("Lane body",body);
        lane.addComponent<ColliderComponent>("Lane collider",CollisionShape::box({6,.5f,180})).setSurface(surface);
        auto& mesh=lane.addComponent<MeshComponent>("Lane visual",Mesh::cube());mesh.transform.scale={12,1,360};mesh.color=colors[i];
    }
    auto car=makeVehicleExample({-14,.95f,0});const CompoundChild parts[]={{CollisionShape::box({.8f,.25f,1.75f}),{},{}},{CollisionShape::box({.7f,.18f,.7f}),{{0,.4f,-.2f}},{}}};
    car->getComponent<ColliderComponent>()->setShape(CollisionShape::compound(parts));scene.adoptChild(std::move(car));
    TerrainData data;data.samplesX=9;data.samplesZ=5;data.chunkCells=4;data.heights.resize(45);auto ice=std::make_shared<PhysicalSurface>();ice->id="terrain.ice";ice->friction=.04f;ice->tyreGrip=.15f;data.cellSurfaces.resize(32);for(int z=0;z<4;++z)for(int x=4;x<8;++x)data.cellSurfaces[z*8+x]=ice;
    auto terrain=makeTerrainActor("Physical terrain",TerrainAsset::create(data),true);terrain->transform.position={22,0,0};scene.adoptChild(std::move(terrain));
}

}
