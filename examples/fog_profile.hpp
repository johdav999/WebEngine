#pragma once

#include "fog_doorway.hpp"
#include <array>
#include <stdexcept>

namespace engine::fog_example {
inline constexpr int ProfileScenarioCount=10;
inline constexpr int ProfileVariantCount=4;
inline constexpr std::array<const char*,ProfileScenarioCount> profileScenarioNames{
    "single-grid", "four-light-grid", "four-box-direct", "surface-extinction", "planar-reflection",
    "single-grid-faded", "four-light-grid-faded", "four-box-direct-faded", "surface-extinction-faded", "planar-reflection-faded"
};

inline void validateProfile(int scenario,int variant,int steps) {
    if(scenario<0||scenario>=ProfileScenarioCount||variant<0||variant>=ProfileVariantCount||(steps!=16&&steps!=32))
        throw std::invalid_argument("Invalid fog profiling scenario, variant or sample budget");
}

// Scenario IDs 5..9 repeat 0..4 with an edge fade. Variants keep authored content identical.
inline void configureProfile(Scene& scene,Actor& root,CameraComponent& camera,int scenario,int variant) {
    validateProfile(scenario,variant,16);
    const int mode=scenario%5;
    const bool faded=scenario>=5;
    if(mode==4) {
        configureShadowCheck(scene,root,camera,ReflectionClear);
        auto fog=*scene.fog();
        fog.volume->edgeFade=faded?.06f:0.f;
        scene.setFog(fog);
    }else{
        scene.setSky(std::nullopt);
        scene.environment.reset();
        scene.ambient={.015f,.015f,.015f};
        camera.transform={};
        camera.transform.position={0,2,8};
        camera.lookAt({0,2,0});
        camera.setOrthographic(6,.1f,40);
        camera.setViewport({});

        auto receiver=std::make_shared<Material>();
        receiver->shader=mode==3?MaterialShader::Pbr:MaterialShader::Unlit;
        receiver->baseColor=mode==3?Vec3{.72f,.72f,.72f}:Vec3{.03f,.03f,.03f};
        receiver->roughness=1;
        receiver->metallic=0;
        receiver->doubleSided=true;
        auto& backdrop=root.addComponent<MeshComponent>("Fog profile receiver",Mesh::plane(16));
        backdrop.setMaterial(0,MaterialInstance{receiver});
        backdrop.transform.position={0,2,-2};
        backdrop.transform.rotation.x=pi/2;
        backdrop.castsShadow=false;

        constexpr std::array<Vec3,4> beamPositions{{{-5,2,0},{5,2,0},{0,6,0},{0,-2,0}}};
        constexpr std::array<Vec3,4> surfacePositions{{{-5,1,3},{-5,3,3},{-4,1,4},{-4,3,4}}};
        for(int light=0;light<(mode==0?1:4);++light) {
            auto& spot=root.addComponent<LightComponent>("Fog profile spotlight",LightType::Spot);
            spot.transform.position=mode==3?surfacePositions[light]:beamPositions[light];
            spot.direction=normalized(Vec3{0,2,0}-spot.transform.position);
            spot.intensity=mode==3?9.f:28.f;
            spot.range=30;
            spot.innerAngle=.55f;
            spot.outerAngle=.8f;
            spot.shadow.enabled=true;
        }

        FogSettings fog;
        fog.density=0;
        auto& box=fog.volume.emplace();
        box.minimum=mode==3?Vec3{-3.2f,.5f,.2f}:Vec3{-3,.5f,-1};
        box.maximum=mode==3?Vec3{-.65f,3.5f,2}:Vec3{3,3.5f,1};
        box.density=mode>=2?.12f:.35f;
        box.scattering=mode==3?Vec3{}:Vec3{.8f,.88f,1};
        box.edgeFade=faded?.15f:0.f;
        if(mode>=2) {
            for(float offset:std::array{-.3f,.15f,.3f}) {
                auto extra=box;
                extra.minimum.z+=offset;
                extra.maximum.z+=offset;
                fog.additionalVolumes.push_back(extra);
            }
        }
        scene.setFog(fog);
    }

    auto black=std::make_shared<Material>();
    black->shader=MaterialShader::Unlit;
    black->baseColor={};
    auto& blocker=root.addComponent<MeshComponent>("Fog profile geometric occluder",Mesh::cube());
    blocker.setMaterial(0,MaterialInstance{black});
    blocker.transform.position=mode==4?Vec3{-2,.75f,-2}:Vec3{-1.75f,2,0};
    blocker.transform.scale=mode==4?Vec3{.15f,.2f,.3f}:Vec3{.15f,.5f,.6f};
    scene.synchronize();
}

inline void profileQuality(RenderQuality& quality,int scenario,int variant,int steps) {
    validateProfile(scenario,variant,steps);
    quality.post.enabled=false;
    quality.webGlPbr=true;
    quality.msaaSamples=1;
    quality.resolution={1,1,960,960*540};
    quality.shadowResolution=512;
    quality.shadowFilterTaps=9;
    quality.maxShadowMaps=4;
    quality.directionalCascades=1;
    quality.screenSpaceReflections=false;
    quality.volumetricFogSteps=static_cast<std::uint32_t>(steps);
    quality.volumetricFogMaxLights=scenario%5==0||scenario%5==4?1:4;
    quality.volumetricFogGrid=scenario%5!=2;
    quality.volumetricFogSelfShadows=variant!=0&&scenario%5!=3;
    quality.volumetricFogSurfaceShadows=variant!=0&&scenario%5==3;
    quality.volumetricFogSoftShadows=variant==2;
    quality.volumetricFogLightCache=variant==3;
    quality.validate();
}

// Fixed baselines make repeated motion updates independent of callback cadence.
inline void updateProfile(Scene& scene,Actor& root,CameraComponent& camera,int scenario,int operation,float phase) {
    if(scenario<0||scenario>=ProfileScenarioCount||operation<0||operation>2||!std::isfinite(phase))
        throw std::invalid_argument("Invalid fog profiling update");
    const float offset=.15f*std::sin(phase);
    if(operation==0)camera.transform.position.x=offset;
    else if(operation==1) {
        auto* light=root.getComponent<LightComponent>();
        if(!light)throw std::logic_error("Fog profiling spotlight is missing");
        light->transform.position.x=-5+offset;
    }else{
        auto fog=*scene.fog();
        const int mode=scenario%5;
        const float density=(mode==4?.5f:mode>=2?.12f:.35f)*(1+offset);
        fog.volume->density=density;
        for(auto& box:fog.additionalVolumes)box.density=density;
        scene.setFog(fog);
    }
    scene.synchronize();
}
} // namespace engine::fog_example
