#pragma once

#include "engine/camera_component.hpp"
#include "engine/light_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/particle_emitter_component.hpp"
#include "engine/render_quality.hpp"
#include "engine/scene.hpp"

namespace engine::fog_example {
inline void atmosphere(Scene& scene) {
    SkyBackground sky;
    sky.horizon={.12f,.17f,.25f};
    sky.zenith={.035f,.06f,.12f};
    sky.nadir={.045f,.055f,.075f};
    scene.setSky(sky);
    scene.ambient={.035f,.045f,.06f};
}
inline FogSettings settings(bool volumetric) {
    FogSettings fog;
    fog.color={.12f,.16f,.22f};
    fog.density=.012f;
    fog.heightFalloff=.08f;
    if(volumetric) {
        fog.volume=FogVolume{};
        fog.volume->minimum={-5,0,-.25f};
        fog.volume->maximum={5,4.5f,7};
        fog.volume->scattering={.88f,.93f,1};
        fog.volume->density=.32f;
        fog.volume->edgeFade=.35f;
    }
    return fog;
}

inline MeshComponent& planarWater(Actor& root,float size,bool reflection=true) {
    auto material=std::make_shared<Material>();
    material->shader=MaterialShader::Water;
    material->baseColor={};
    auto& settings=material->water;
    settings.amplitude=settings.rippleStrength=0;
    settings.horizon=settings.zenith={};
    settings.planarReflectionStrength=reflection?1.f:0.f;
    auto& mesh=root.addComponent<MeshComponent>("Planar water",Mesh::plane(size));
    mesh.setMaterial(0,MaterialInstance{material});
    mesh.castsShadow=false;
    return mesh;
}

inline MeshComponent& build(Actor& root,CameraComponent& camera) {
    auto material=std::make_shared<Material>();
    material->roughness=1;
    material->metallic=0;
    auto cube=std::make_shared<Mesh>(*Mesh::cube());
    cube->materials={material};
    auto box=[&](const char* name,Vec3 position,Vec3 size,Vec3 color) {
        auto& mesh=root.addComponent<MeshComponent>(name,cube);
        mesh.transform.position=position;
        mesh.transform.scale=size;
        mesh.color=color;
    };
    box("Left jamb",{-3,1.9f,0},{4,3.8f,.36f},{.27f,.32f,.4f});
    box("Right jamb",{3,1.9f,0},{4,3.8f,.36f},{.27f,.32f,.4f});
    box("Lintel",{0,3.45f,0},{2,.7f,.36f},{.27f,.32f,.4f});
    box("Floor",{0,-.15f,1.8f},{12,.3f,15},{.22f,.26f,.32f});
    box("Far wall",{0,2.4f,-4.6f},{12,4.8f,.25f},{.15f,.19f,.25f});
    box("Foreground post",{1.8f,.75f,4.5f},{.36f,1.5f,.36f},{.3f,.4f,.55f});
    auto glass=std::make_shared<Material>();
    glass->shader=MaterialShader::Unlit;
    glass->baseColor={.3f,.6f,.85f};
    glass->alphaMode=AlphaMode::Blend;
    glass->opacity=.38f;
    auto& panel=root.addComponent<MeshComponent>("Translucent panel",cube);
    panel.transform.position={-2.5f,1.1f,2.7f};
    panel.transform.scale={.65f,.85f,.04f};
    panel.setMaterial(0,MaterialInstance{glass});
    panel.castsShadow=false;
    auto dust=std::make_shared<ParticleEmitterDefinition>();
    dust->spawnRate=0;
    dust->looping=false;
    dust->bursts={{0,12}};
    dust->positionMin={-1.2f,-.3f,-.6f};
    dust->positionMax={1.2f,.3f,.6f};
    dust->velocityMin=dust->velocityMax={};
    dust->lifetimeMin=dust->lifetimeMax=8;
    dust->size={{0,.045f}};
    dust->color={{0,{.65f,.8f,1,.55f}}};
    auto& emitter=root.addChild<ParticleEmitterComponent>("Dust in the beam",dust);
    emitter.transform.position={.1f,1.55f,2.2f};
    auto& spot=root.addComponent<LightComponent>("Doorway spotlight",LightType::Spot);
    spot.transform.position={0,2.1f,-3.5f};
    spot.direction=normalized(Vec3{0,-.14f,1});
    spot.color={1,.81f,.58f};
    spot.intensity=27;
    spot.range=17;
    spot.innerAngle=.5f;
    spot.outerAngle=.69f;
    spot.shadow.enabled=true;
    camera.transform={};
    camera.transform.position={7,3.6f,9};
    camera.lookAt({0,1.5f,2});
    camera.setPerspective(50*pi/180,.1f,40);
    auto& water=planarWater(root,6);
    water.transform.position={0,.02f,3};
    water.enabled=false;
    return water;
}

enum ShadowCase { GridClear, GridShadow, DirectClear, DirectShadow, ReflectionClear,
                  ReflectionShadow, ReflectionOff, FadedClear, FadedShadow, ShadowCaseCount };

inline void configureShadowCheck(Scene& scene,Actor& root,CameraComponent& camera,ShadowCase mode) {
    const bool reflection=mode>=ReflectionClear&&mode<=ReflectionOff;
    const float height=reflection?.75f:2.f, depth=reflection?-2.f:0.f;
    scene.setSky(std::nullopt);
    scene.environment.reset();
    scene.ambient={};
    camera.transform={};
    camera.transform.position=reflection?Vec3{0,3,8}:Vec3{0,height,8};
    camera.lookAt(reflection?Vec3{}:Vec3{0,height,0});
    camera.setOrthographic(6,.1f,40);
    camera.setViewport({});

    auto black=std::make_shared<Material>();
    black->shader=MaterialShader::Unlit;
    black->baseColor={};
    black->doubleSided=true;
    auto& backdrop=root.addComponent<MeshComponent>("Dark receiver",Mesh::plane(12));
    backdrop.setMaterial(0,MaterialInstance{black});
    backdrop.transform.position={0,height,depth-.65f};
    backdrop.transform.rotation.x=pi/2;
    backdrop.castsShadow=false;
    if(reflection)planarWater(root,12,mode!=ReflectionOff);

    auto& spot=root.addComponent<LightComponent>("Horizontal probe spotlight",LightType::Spot);
    spot.transform.position={-5,height,depth};
    spot.direction={1,0,0};
    spot.intensity=85;
    spot.range=30;
    spot.innerAngle=.2f;
    spot.outerAngle=.35f;
    spot.shadow.enabled=true;

    FogSettings fog;
    fog.density=0;
    auto& box=fog.volume.emplace();
    box.minimum={-3,height-.35f,depth-.12f};
    box.maximum={3,height+.35f,depth+.12f};
    box.scattering={1,1,1};
    box.density=.5f;
    box.edgeFade=mode>=FadedClear?.06f:0.f;
    scene.setFog(fog);
    scene.synchronize();
}

inline void shadowCheckQuality(RenderQuality& quality,ShadowCase mode) {
    quality.post.enabled=false;
    quality.msaaSamples=1;
    quality.volumetricFogGrid=mode!=DirectClear&&mode!=DirectShadow;
    quality.volumetricFogSelfShadows=mode==GridShadow||mode==DirectShadow||mode==ReflectionShadow||mode==FadedShadow;
}
} // namespace engine::fog_example
