#pragma once
#include "engine/camera_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/render_quality.hpp"
#include "engine/scene.hpp"

namespace engine::ranked_lighting_example {
inline void populate(Actor& root) {
    auto& floor=root.addComponent<MeshComponent>("Shadow receiver",Mesh::plane(20));
    floor.color={.6f,.6f,.6f};
    auto& chassis=root.addComponent<MeshComponent>("Chassis",Mesh::cube());
    chassis.transform.position={0,.6f,4.4f};chassis.transform.scale={2,.8f,3};chassis.color={.05f,.2f,.5f};
    for(int i=0;i<3;++i) {
        auto& pillar=root.addComponent<MeshComponent>("Shadow pillar",Mesh::cube());
        pillar.transform.position={float(i-1)*2,.8f,float(-i)};
        pillar.transform.scale={.65f,1.6f,.65f};pillar.color={.3f,.4f,.55f};
    }
    for(int i=0;i<9;++i) {
        auto& lamp=root.addComponent<LightComponent>("Fill lamp "+std::to_string(i+1),LightType::Point);
        lamp.transform.position={float(i%3-1)*3,2,float(i/3-1)*3};
        lamp.color={.25f,.45f,1};lamp.intensity=.2f;lamp.range=6;
    }
    auto& sun=root.addComponent<LightComponent>("Sun",LightType::Directional);
    sun.direction={-.4f,-1,-.35f};sun.color={1,.93f,.8f};sun.intensity=.8f;
    sun.shadow.enabled=true;sun.shadow.halfDepth=20;sun.priority=100;
    for(int i=0;i<2;++i) {
        auto& headlight=root.addComponent<LightComponent>(i==0?"Left headlight":"Right headlight",LightType::Spot);
        headlight.transform.position={i==0?-.8f:.8f,1.1f,3};headlight.direction={0,-.22f,-1};
        headlight.color={1,.9f,.7f};headlight.range=18;headlight.intensity=15;
        headlight.shadow.enabled=true;headlight.priority=99-i;
    }
}

inline void configure(RenderQuality& quality) {
    quality.rankedLights=true;quality.localShadowReserve=2;
    quality.directionalCascades=2;quality.shadowDistance=40;quality.maxShadowMaps=4;
}
} // namespace engine::ranked_lighting_example
