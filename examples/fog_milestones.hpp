#pragma once

#include "fog_doorway.hpp"

namespace engine::fog_example {
enum MilestoneCase {
    SurfaceClear, SurfaceShadow, SurfaceReference,
    OverlapClear, OverlapSingle, OverlapPair,
    LightRed, LightGreen, LightBoth, LightBudget,
    FilterClear, FilterSoft, FilterMovedClear, FilterMovedSoft,
    CascadeClear, CascadeSoft, CascadeMovedClear, CascadeMovedSoft, MilestoneCaseCount
};

inline void configureMilestone(Scene& scene,Actor& root,CameraComponent& camera,MilestoneCase mode) {
    scene.setSky(std::nullopt);
    scene.environment.reset();
    scene.ambient={};
    const bool cascades=mode>=CascadeClear;
    const bool moved=mode==FilterMovedClear||mode==FilterMovedSoft||mode>=CascadeMovedClear;
    const float eyeHeight=moved?2.07f:2.f;
    camera.transform={};
    camera.transform.position={0,eyeHeight,8};
    camera.lookAt({0,eyeHeight,0});
    camera.setOrthographic(6,.1f,40);
    camera.setViewport({});

    auto receiver=std::make_shared<Material>();
    receiver->shader=mode<=SurfaceReference?MaterialShader::Pbr:MaterialShader::Unlit;
    receiver->baseColor=mode<LightRed?Vec3{.72f,.72f,.72f}:Vec3{};
    receiver->roughness=1;
    receiver->metallic=0;
    receiver->doubleSided=true;
    auto& backdrop=root.addComponent<MeshComponent>("Fog test receiver",Mesh::plane(12));
    backdrop.setMaterial(0,MaterialInstance{receiver});
    backdrop.transform.position={0,2,0};
    backdrop.transform.rotation.x=pi/2;
    backdrop.castsShadow=false;

    auto addSpot=[&](const char* name,Vec3 position,Vec3 target,Vec3 color,float intensity) -> LightComponent& {
        auto& light=root.addComponent<LightComponent>(name,LightType::Spot);
        light.transform.position=position;
        light.direction=normalized(target-position);
        light.color=color;
        light.intensity=intensity;
        light.range=30;
        light.innerAngle=.24f;
        light.outerAngle=.42f;
        return light;
    };

    FogSettings fog;
    fog.density=0;
    auto& box=fog.volume.emplace();
    box.edgeFade=0;
    box.density=.4f;
    box.scattering={};
    box.minimum={-2,1,-.6f};
    box.maximum={2,3,-.2f};
    if(mode<=SurfaceReference) {
        const float transmission=std::exp(-.5f*std::sqrt(5.f));
        addSpot("Surface spotlight",{-4,2,2},{0,2,0},{1,1,1},6*(mode==SurfaceReference?transmission:1));
        box.minimum={-3,1,.25f};
        box.maximum={-1,3,1.75f};
        box.density=.5f;
        if(mode==SurfaceReference)fog.volume.reset();
    } else if(mode<=OverlapPair) {
        backdrop.transform.position.z=-1;
        if(mode==OverlapClear)fog.volume.reset();
        else if(mode==OverlapSingle)box.density=.8f;
        else {
            auto second=box;
            box.maximum.x=.5f;
            second.minimum.x=-.5f;
            fog.additionalVolumes.push_back(second);
        }
    } else {
        backdrop.transform.position.z=-1;
        box.minimum={-3,1.25f,-.24f};
        box.maximum={3,2.75f,.24f};
        box.scattering={1,1,1};
        if(mode<FilterClear) {
            if(mode!=LightGreen)addSpot("Red fog light",{-5,2,0},{0,2,0},{1,0,0},36);
            if(mode!=LightRed)addSpot("Green fog light",{5,2,0},{0,2,0},{0,1,0},36);
        } else {
            if(cascades){
                box.minimum.z=-3;
                box.maximum.z=3;
                backdrop.transform.position.z=-4;
                auto& sun=root.addComponent<LightComponent>("Cascaded beam",LightType::Directional);
                sun.direction=normalized(Vec3{1,-.18f,-.12f});
                sun.intensity=7;
                sun.shadow.enabled=true;
                sun.shadow.halfDepth=12;
            }else addSpot("Filtered beam",{-5,2,0},{0,2,0},{1,1,1},52).shadow.enabled=true;
            auto black=std::make_shared<Material>(*receiver);
            auto& blocker=root.addComponent<MeshComponent>("Moving fog occluder",Mesh::cube());
            blocker.setMaterial(0,MaterialInstance{black});
            blocker.transform.position={-2,moved?2.055f:2.f,0};
            blocker.transform.scale={.14f,.24f,cascades?2.f:.34f};
        }
    }
    scene.setFog(fog);
    scene.synchronize();
}

inline void milestoneQuality(RenderQuality& quality,MilestoneCase mode) {
    quality.post.enabled=false;
    quality.webGlPbr=true;
    quality.msaaSamples=1;
    quality.shadowResolution=256;
    quality.volumetricFogGrid=true;
    quality.volumetricFogSurfaceShadows=mode==SurfaceShadow||mode==SurfaceReference;
    quality.volumetricFogSoftShadows=mode==FilterSoft||mode==FilterMovedSoft||mode==CascadeSoft||mode==CascadeMovedSoft;
    quality.volumetricFogMaxLights=mode==LightBoth?2:1;
    quality.directionalCascades=mode>=CascadeClear?2:1;
    quality.shadowDistance=18;
    quality.cascadeLambda=0;
}
} // namespace engine::fog_example
