#pragma once
#include "engine/cloth.hpp"
#include <limits>
#include "cloth_collision_cases.hpp"
#include "cloth_surface_cases.hpp"
#include "cloth_edge_cases.hpp"
#include "cloth_mass_cases.hpp"
#include "cloth_wind_cases.hpp"
#include "cloth_motion_cases.hpp"
inline void clothCases() {
    clothCollisionCases();
    clothSurfaceCases();
    clothEdgeCases();
    clothMassCases();
    clothWindCases();
    clothMotionCases();
    auto wrongCatalog=clothing_example::catalog();
    auto& wrongCape=*std::find_if(wrongCatalog.items.begin(),wrongCatalog.items.end(),[](const auto& item){return item.id=="cape";});
    auto wrongSurface=std::make_shared<engine::ClothDefinition>(*wrongCape.cloth);wrongSurface->surfaceTriangles={{0,2,18}};wrongCape.cloth=wrongSurface;
    rejects([&]{wrongCatalog.validate();});
    using namespace engine;
    std::vector<Vec3> rest={{-.5f,1,0},{.5f,1,0},{-.5f,0,0},{.5f,0,0}};
    ClothDefinition definition;definition.maxDistance={0,0,2,2};
    definition.links={{0,1,0},{0,2,0},{1,3,0},{2,3,0},{0,3,1e-6f},{1,2,1e-6f}};
    ClothSimulation a(definition,rest),b(definition,rest);
    for(int i=0;i<120;++i)a.advance(1.0/120,rest,{}, {0,-9.81f,15});
    for(int i=0;i<60;++i)b.advance(1.0/60,rest,{}, {0,-9.81f,15});
    check(a.stats().steps==120&&b.stats().steps==120,"Fixed step count changed with frame partition");
    for(std::size_t i=0;i<rest.size();++i) {
        auto difference=a.positions()[i]-b.positions()[i];check(dot(difference,difference)<1e-10f,"Static targets should produce frame-partition-independent cloth");
    }
    check(a.positions()[0].y==1&&a.positions()[0].z==0&&a.positions()[1].z==0,"Pinned cloth drifted");
    check(a.positions()[2].z>.1f,"Wind did not move free cloth");
    for(auto link:definition.links){auto d=a.positions()[link.b]-a.positions()[link.a],r=rest[link.b]-rest[link.a];check(std::abs(std::sqrt(dot(d,d))-std::sqrt(dot(r,r)))<.03f,"Stretch constraint diverged");}
    auto frozen=std::vector<Vec3>(a.positions().begin(),a.positions().end());auto steps=a.stats().steps;
    a.advance(0,rest);check(a.stats().steps==steps&&a.positions()[2].z==frozen[2].z,"Zero delta changed cloth");
    rejects([&]{a.advance(.5,rest);});
    rejects([&]{a.advance(.01,rest,{}, {std::numeric_limits<float>::quiet_NaN(),0,0});});
    std::array<ClothCapsule,1> bad={ClothCapsule{{},{},-1}};rejects([&]{a.advance(.01,rest,bad);});
    check(a.stats().steps==steps&&a.positions()[2].z==frozen[2].z,"Invalid update partially committed");
    auto teleported=rest;for(auto& p:teleported)p.x+=5;a.advance(.01,teleported);
    check(a.stats().resets==1&&a.positions()[2].x==teleported[2].x,"Teleport failed to rebind cloth");
    a.advance(.25,teleported);check(a.stats().steps==steps+8&&a.stats().droppedSeconds>.18,"Catch-up budget not enforced");
    ClothSimulation collision(definition,rest);
    std::array<ClothCapsule,1> capsule={ClothCapsule{{0,-.5f,0},{0,.5f,0},.6f}};
    for(int i=0;i<120;++i)collision.advance(1.0/120,rest,capsule,{0,-9.81f,0});
    for(auto i:{2,3}) {auto p=collision.positions()[i];auto center=Vec3{0,std::clamp(p.y,-.5f,.5f),0};auto d=p-center;check(std::sqrt(dot(d,d))>=.614f,"Free particle penetrated capsule");}
    auto invalid=definition;invalid.maxDistance[0]=invalid.maxDistance[1]=1;rejects([&]{ClothSimulation rejected(invalid,rest);});
    invalid=definition;invalid.links.push_back(invalid.links[0]);rejects([&]{ClothSimulation rejected(invalid,rest);});

    clothing_example::Demo d;auto scene=d.make();d.command(8);
    auto findCape=[&]()->MeshComponent*{for(auto& child:scene->findComponent(d.clothing->root())->children())if(child->name=="cape")return dynamic_cast<MeshComponent*>(child.get());return nullptr;};
    auto* cape=findCape();check(cape&&!dynamic_cast<SkeletalMeshComponent*>(cape),"Physical cloth must not be skinned twice");
    auto initial=cape->morphPose();auto initialZ=initial->vertices.back().position.z;auto initialRevision=initial->revision;
    for(int i=0;i<120;++i){scene->update(1.0/120);d.advance(1.0/120);}
    auto result=cape->morphPose();check(result==initial&&result->revision>initialRevision,"Cloth must reuse its stream identity and publish new revisions");
    check(d.clothing->clothStats(*scene).environmentContacts>0,"Demo cape did not collide with obstacle");
    check(d.clothing->clothStats(*scene).surfaceContacts>0,"Demo cape did not collide with obstacle triangles");
    check(std::abs(result->vertices.back().position.z-initialZ)>.01f,"Cape did not deform");
    auto bounds=cape->localBounds();check(bounds.valid,"Cloth bounds invalid");
    for(auto v:result->vertices) {
        check(v.position.x>=bounds.minimum.x&&v.position.x<=bounds.maximum.x&&v.position.z>=bounds.minimum.z&&v.position.z<=bounds.maximum.z,"Cloth escaped draw bounds");
        check(std::isfinite(v.normal.x)&&std::abs(dot(v.normal,v.normal)-1)<.001f,"Cloth normal invalid");
        check(std::abs(dot(v.normal,{v.tangent[0],v.tangent[1],v.tangent[2]}))<.001f,"Cloth tangent invalid");
    }
    bool projected=false;for(auto& draw:scene->collect().meshes)if(draw.component==cape->id()){projected=true;check(draw.morphPose==result&&!draw.skinPose,"Cloth draw lost dynamic vertices or double-skinned");}
    check(projected,"Cloth not collected");
    auto stats=d.clothing->clothStats(*scene);auto revision=result->revision;d.advance(0);d.clothing->synchronize(*scene);
    check(cape->morphPose()->revision==revision&&d.clothing->clothStats(*scene).steps==stats.steps,"Paused presentation changed solver/vertex revision");
    rejects([&]{d.clothing->advanceCloth(*scene,.01,bad);});check(cape->morphPose()->revision==revision,"Invalid capsule changed rendered cloth");
    const std::array<ClothBox,1> badEnvironment={ClothBox{{1,1,1},{0,0,0}}};
    rejects([&]{d.clothing->advanceCloth(*scene,.01,{}, {},{{},badEnvironment});});
    check(cape->morphPose()->revision==revision,"Invalid environment changed rendered cloth");
    const std::array<ClothTriangle,1> badSurface={ClothTriangle{{},{},{}}};
    rejects([&]{d.clothing->advanceCloth(*scene,.01,{}, {},{{},{},badSurface});});
    check(cape->morphPose()->revision==revision,"Invalid surface changed rendered cloth");
    rejects([&]{scene->visit([&](const Node&,const Mat4&){d.advance(.01);});});
    check(cape->morphPose()->revision==revision,"Traversal-time cloth update was published");
    auto saved=d.save();d.command(8);check(!findCape(),"Unequipped cloth survived");d.load(saved);check(findCape(),"Saved cape selection not restored");
    d.driver->transform.position.x=10;d.advance(.01);check(d.clothing->clothStats(*scene).resets==2,"World teleport did not reset clothing");
    auto oldRevision=findCape()->morphPose()->revision;d.driver->transform.scale={2,2,2};rejects([&]{d.advance(.01);});check(findCape()->morphPose()->revision==oldRevision,"Invalid driver scale published cloth");
    d.driver->transform.scale={1,1,1};d.clothing->resetCloth(*scene);
    d.driver->destroy();scene->flushPendingDestroy();check(!d.clothing->advanceCloth(*scene,.01),"Removed character retained cloth simulation");
    std::cout<<"Physical cloth PASS: fixed steps, pin/stretch constraints, wind, capsule clearance, bounds, pause, teleport, atomic validation and lifetime\n";
}
