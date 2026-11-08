#pragma once

#include "lighting_gallery.hpp"

namespace engine::volumetric_water_example {
// The center ray meets flat water at the origin. Its reflected leg reaches
// (0, 2, -2), while the transmitted image comes from (0, -2, -2).
enum Case {
    BedClear, RefractionClear, BedFog, RefractionFog, RefractionOff, BedFrontFog, RefractionFrontFog,
    ReflectionClear, ReflectionFirstLeg, ReflectionSecondLeg, ReflectionVirtualLeg,
    ReflectionDistanceLimit, ReflectionOff, ReflectionBothLegs, ReflectionScatter, Count
};

inline void configure(Scene& scene,Actor& root,CameraComponent& camera,Case mode,bool orthographic) {
    scene.setSky(std::nullopt);
    scene.environment.reset();
    scene.ambient={};
    camera.transform={};
    camera.transform.position={0,6,6};
    camera.lookAt({});
    if(orthographic)camera.setOrthographic(6,.1f,40);
    else camera.setPerspective(pi/4,.1f,40);
    camera.setViewport({});

    auto& water=root.addComponent<MeshComponent>("Flat water",Mesh::plane(12));
    water.enabled=mode!=BedClear&&mode!=BedFog&&mode!=BedFrontFog;
    water.castsShadow=false;
    auto waterMaterial=std::make_shared<Material>();
    waterMaterial->shader=MaterialShader::Water;
    waterMaterial->baseColor={};
    auto& settings=waterMaterial->water;
    settings.amplitude=0;
    settings.rippleStrength=0;
    settings.reflectionStrength=mode>=ReflectionClear?1.f:0.f;
    settings.planarReflectionStrength=mode>=ReflectionClear&&mode!=ReflectionOff?1.f:0.f;
    settings.horizon=settings.zenith={};
    settings.refractionStrength=mode==RefractionClear||mode==RefractionFog||mode==RefractionFrontFog?1.f:0.f;
    settings.refractionDistortion=0;
    settings.absorption={};
    water.setMaterial(0,MaterialInstance{waterMaterial});

    auto target=std::make_shared<Material>();
    target->shader=MaterialShader::Unlit;
    target->baseColor={1,0,0};
    target->doubleSided=true;
    auto& receiver=root.addComponent<MeshComponent>("Red receiver",lighting_example::panelMesh());
    receiver.transform.position={0,mode>=ReflectionClear?2.f:-2.f,-2};
    receiver.setMaterial(0,MaterialInstance{target});
    receiver.castsShadow=false;

    if(mode>=BedFog&&mode!=ReflectionClear) {
        FogSettings fog;
        fog.density=0;
        FogVolume volume;
        volume.scattering={};
        volume.density=.7f;
        volume.edgeFade=0;
        volume.minimum={-1.5f,.5f,-1.85f};
        volume.maximum={1.5f,1.5f,-.25f};
        if(mode<=RefractionOff) {
            volume.minimum.y=-1.5f;
            volume.maximum.y=-.5f;
        } else if(mode==BedFrontFog||mode==RefractionFrontFog||mode==ReflectionFirstLeg||mode==ReflectionVirtualLeg) {
            volume.minimum.z=.25f;
            volume.maximum.z=1.85f;
            if(mode==ReflectionVirtualLeg) {
                volume.minimum.y=-1.5f;
                volume.maximum.y=-.5f;
            }
        }
        if(mode==ReflectionBothLegs)volume.maximum.z=1.85f;
        if(mode==ReflectionScatter) {
            volume.scattering={0,1,0};
            scene.ambient={0,1,0};
        }
        if(mode==ReflectionDistanceLimit)volume.maxDistance=7;
        fog.volume=volume;
        scene.setFog(fog);
    } else scene.setFog(std::nullopt);
    scene.synchronize();
}
} // namespace engine::volumetric_water_example
