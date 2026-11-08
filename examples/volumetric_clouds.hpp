#pragma once
#include "engine/sky.hpp"
#include "engine/scene.hpp"
#include "engine/render_quality.hpp"
#include "engine/mesh_component.hpp"

namespace engine::cloud_example {
inline constexpr int fixtureCount=34;
inline constexpr const char* names[fixtureCount]={"clear","legacy","volume","wind","translated-legacy","translated-volume",
    "inside","above","zero-extinction","sparse","overcast","quality-fallback","sphere","orthographic-volume",
    "orthographic-legacy","night","sunset","distance-limit","view-disabled","hdr","msaa4","foreground",
    "ground-shadow-off","ground-shadow-on","ground-shadow-wind","point-light-off","point-light-on",
    "above-layer-off","above-layer-on","depth-off","depth-on","depth-zero-extinction",
    "reflection-depth-off","reflection-depth-on"};
inline bool shadowMode(int mode) {return mode==23||mode==24||mode==26||mode==28;}
inline int depthPassExpectation(int mode) {
    if(mode==8||mode==31)return -1; // A clear extinction volume may be optimized away.
    if(mode==32)return 0;
    if(mode==33)return 2; // Main and mirrored camera origins have independent cloud grids.
    return mode!=0&&mode!=1&&mode!=4&&mode!=11&&mode!=14&&mode!=18&&mode!=29?1:0;
}
inline void configure(Scene& scene,CameraComponent& camera,RenderQuality& quality,int mode) {
    if(mode<0||mode>=fixtureCount)throw std::invalid_argument("Unknown volumetric cloud fixture");
    camera.transform={};camera.transform.position={0,300,0};camera.setPerspective(pi/3,.1f,100000);
    camera.setViewport({});camera.setVisibilityMask(~0u);
    Vec3 direction{0,.35f,-1};
    if(mode==4||mode==5)camera.transform.position.x=2500;
    if(mode==6)camera.transform.position.y=1500;
    if(mode==7){camera.transform.position.y=2600;direction.y=-.35f;}
    camera.lookAt(camera.worldPosition()+direction);
    if(mode==13||mode==14)camera.setOrthographic(2200,.1f,100000);
    if(mode>=22&&mode<=28) {
        const float height=mode>=27?2400.f:0.f;
        camera.transform.position={0,height+700,700};
        camera.lookAt({0,height,0});
    }
    if(mode>=29&&mode<=31) {
        camera.transform.position={0,300,0};
        camera.lookAt(camera.worldPosition()+normalized(Vec3{0,.35f,-1}));
        camera.setOrthographic(3000,.1f,100000);
    }
    if(mode>=32) {
        camera.transform.position={0,600,3000};
        camera.lookAt({0,0,0});
    }
    SkyBackground sky;sky.mode=SkyMode::Atmosphere;sky.clouds.enabled=mode!=0;
    sky.clouds.volumetric=mode!=1&&mode!=4&&mode!=14;
    sky.clouds.coverage=.62f;sky.clouds.density=1.2f;sky.clouds.wind={.001f,.0004f};
    sky.atmosphere.sunDirection={.45f,.7f,-.5f};
    if(mode==3)sky.clouds.offset={.13f,.07f};
    if(mode==8)sky.clouds.extinction=0;
    if(mode==9)sky.clouds.coverage=.25f;
    if(mode==10)sky.clouds.coverage=.9f;
    if(mode==12)sky.geometry=SkyGeometry::Sphere;
    if(mode==15)sky.atmosphere.sunDirection={.3f,-.8f,-.5f};
    if(mode==16)sky.atmosphere.sunDirection={-.8f,.06f,-.4f};
    if(mode==17)sky.clouds.maxDistance=1000;
    if(mode==24)sky.clouds.offset={.13f,.07f};
    if(mode>=29)sky.clouds.coverage=.85f;
    if(mode==31)sky.clouds.extinction=0;
    quality.volumetricCloudSteps=mode==11?0:24;
    quality.volumetricFogSteps=0;quality.volumetricFogLightCache=false;
    quality.volumetricCloudDepth=mode!=29&&mode!=32;
    quality.volumetricCloudShadows=shadowMode(mode);
    quality.volumetricCloudShadowDistance=8000;
    quality.volumetricCloudShadowStrength=1;
    quality.post.enabled=mode==19;quality.post.bloomStrength=0;
    quality.msaaSamples=mode==20?4:1;
    quality.resolution={1,1,640,640*360};quality.maxShadowMaps=0;
    quality.shadowResolution=512;quality.webGlPbr=true;
    scene.setFog(std::nullopt);scene.setSky(sky);scene.ambient=mode>=22?Vec3{.015f,.015f,.015f}:Vec3{.2f,.2f,.2f};
    scene.environmentIntensity=0;
}

inline void populate(Scene& scene,Actor& root,CameraComponent& camera,int mode) {
    if(mode<0||mode>=fixtureCount)throw std::invalid_argument("Unknown volumetric cloud fixture");
    const auto greenCube=[&](const char* name,Vec3 position,Vec3 scale) {
        auto material=std::make_shared<Material>();
        material->shader=MaterialShader::Unlit;
        material->baseColor={0,1,0};
        auto& cube=root.addComponent<MeshComponent>(name,Mesh::cube());
        cube.setMaterial(0,MaterialInstance{material});
        cube.transform.position=position;
        cube.transform.scale=scale;
        cube.castsShadow=cube.receivesShadow=false;
    };
    if(mode==21) {
        greenCube("Foreground cloud occluder",camera.worldPosition()+normalized(Vec3{0,.35f,-1})*1000,{350,350,350});
    }else if(mode>=22&&mode<=28) {
        const float height=mode>=27?2400.f:0.f;
        auto material=std::make_shared<Material>();
        material->shader=MaterialShader::Pbr;
        material->baseColor={.65f,.65f,.65f};
        material->roughness=1;
        auto& ground=root.addComponent<MeshComponent>("Lit cloud shadow receiver",Mesh::plane(30000));
        ground.setMaterial(0,MaterialInstance{material});
        ground.transform.position.y=height;
        ground.castsShadow=false;
        const bool point=mode==25||mode==26;
        auto& light=root.addComponent<LightComponent>(point?"Point-only receiver light":"Cloud-aligned sunlight",
                                                      point?LightType::Point:LightType::Directional);
        light.intensity=point?6.f:3.f;
        light.color={1,1,1};
        light.transform.position={0,height+1800,500};
        light.range=12000;
        light.direction=normalized(scene.sky()->atmosphere.sunDirection)*-1;
    }else if(mode>=29&&mode<=31) {
        const auto direction=normalized(Vec3{0,.35f,-1});
        const auto eye=camera.worldPosition();
        greenCube("Before the cloud layer",eye+direction*1000+Vec3{-1200,0,0},{600,600,120});
        greenCube("Inside the cloud layer",eye+direction*3900,{600,600,120});
        greenCube("Beyond the cloud layer",eye+direction*7000+Vec3{1200,0,0},{600,600,120});
    }else if(mode>=32) {
        greenCube("Reflected cloud receiver",{0,2000,-4000},{1200,1200,1200});
        auto material=std::make_shared<Material>();
        material->shader=MaterialShader::Water;
        material->baseColor={};
        material->roughness=.05f;
        material->normalScale=0;
        material->water.amplitude=material->water.rippleStrength=0;
        material->water.horizon=material->water.zenith={};
        material->water.reflectionStrength=material->water.planarReflectionStrength=1;
        auto& water=root.addComponent<MeshComponent>("Cloud reflection plane",Mesh::plane(20000));
        water.setMaterial(0,MaterialInstance{material});
        water.castsShadow=false;
    }
}
} // namespace engine::cloud_example
