#pragma once

#include "engine/dynamic_gi.hpp"
#include "engine/dynamic_reflections.hpp"
#include "lighting_gallery.hpp"
#include <limits>

namespace engine::dynamic_gi_example {
inline constexpr Vec3 samplePosition{1.16f, .78f, .05f};
inline constexpr Vec3 doorway{0, 1.1f, 0};
inline constexpr Vec3 doorScale{.16f, 2.2f, 1.56f};
inline constexpr Vec3 skyRadiance{.028f,.037f,.055f};
inline constexpr Vec3 mirrorPosition{2.8f,1.7f,-.4f};
inline constexpr Vec3 bounceSamplePosition{1.16f,1.10f,.22f};
inline constexpr Vec3 offscreenSourcePosition{-2.35f,1.1f,0};
inline constexpr Vec3 offscreenSourceScale{.12f,2.2f,2.0f};

inline float sourceScreenRight(const CameraComponent& camera,const PixelViewport& viewport) {
    float right=-std::numeric_limits<float>::infinity();
    for(float x:{-.5f,.5f})for(float y:{-.5f,.5f})for(float z:{-.5f,.5f}) {
        const auto point=offscreenSourcePosition+Vec3{x*offscreenSourceScale.x,y*offscreenSourceScale.y,z*offscreenSourceScale.z};
        right=std::max(right,camera.worldToScreen(point,viewport).pixel.x);
    }
    return right;
}

class Doorway {
public:
    Doorway(Scene& scene, CameraComponent& camera) {
        scene.ambient={};
        scene.environment=Environment::studio();
        scene.environmentIntensity=.12f;
        scene.setLightingZones({});
        scene.setSky(SkyBackground{});
        scene.setFog(std::nullopt);

        auto opaqueStage = [&](const char* name, Vec3 position, Vec3 scale, Vec3 color, Vec3 diffuse) {
            auto material=std::make_shared<Material>();
            material->shader=MaterialShader::Unlit;
            material->baseColor=color;
            lighting_example::object(scene,name,Mesh::cube(),position,scale,material);
            appendBox(staticGeometry_,position,scale,diffuse);
        };
        opaqueStage("Workshop floor",{.2f,-.085f,0},{6.6f,.17f,5},{.08f,.1f,.13f},{.075f,.075f,.075f});
        opaqueStage("Rear wall",{.2f,1.45f,-2.4f},{6.6f,2.9f,.15f},{.12f,.14f,.17f},{.05f,.05f,.05f});
        opaqueStage("Left jamb",{0,1.45f,-1.6f},{.19f,2.9f,1.7f},{.17f,.18f,.20f},{.045f,.045f,.045f});
        opaqueStage("Right jamb",{0,1.45f,1.6f},{.19f,2.9f,1.7f},{.17f,.18f,.20f},{.045f,.045f,.045f});
        opaqueStage("Lintel",{0,2.55f,0},{.19f,.7f,1.6f},{.17f,.18f,.20f},{.045f,.045f,.045f});
        opaqueStage("Workshop ceiling",{.2f,3,0},{6.6f,.2f,5},{.08f,.1f,.12f},{.04f,.04f,.04f});
        auto door=std::make_shared<Material>();
        door->shader=MaterialShader::Unlit;
        door->baseColor={.24f,.28f,.33f};
        auto& doorMesh=lighting_example::object(scene,"Sliding workshop door",Mesh::cube(),doorway,doorScale,door);
        doorMesh.setSavedId("dynamic.gi.door");

        constexpr auto panelPosition=offscreenSourcePosition;
        constexpr auto panelScale=offscreenSourceScale;
        constexpr Vec3 red{.95f,.022f,.012f};
        lighting_example::object(scene,"Off-screen red bounce panel",Mesh::cube(),panelPosition,panelScale,
                                 lighting_example::material(red,0,.9f));
        appendBox(staticGeometry_,panelPosition,panelScale,red);
        auto& light=scene.addChild<LightComponent>("Panel key light",LightType::Spot);
        light.transform.position={-.65f,1.25f,0};
        light.direction={-1,0,0};
        light.intensity=15;
        light.range=6;
        light.innerAngle=.38f;
        light.outerAngle=.68f;

        lighting_example::object(scene,"Ceramic bounce sample",Mesh::sphere(),samplePosition,{.92f,.92f,.92f},
                                 lighting_example::material({.84f,.84f,.84f},0,.85f));
        constexpr Vec3 canopyPosition{1.0f,1.88f,.2f},canopyScale{1.6f,.1f,1.3f},canopyColor{.75f,.75f,.75f};
        lighting_example::object(scene,"White bounce canopy",Mesh::cube(),canopyPosition,canopyScale,
                                 lighting_example::material(canopyColor,0,.9f));
        appendBox(staticGeometry_,canopyPosition,canopyScale,canopyColor);
        lighting_example::object(scene,"Polished metal floor",Mesh::cube(),{2.52f,.025f,-.12f},{1.65f,.045f,2.7f},
                                 lighting_example::material({.55f,.56f,.58f},.91f,.14f));
        auto source=std::make_shared<Material>();
        source->shader=MaterialShader::Unlit;
        source->emissive={.12f,2.3f,.48f};
        lighting_example::object(scene,"Green reflection target",Mesh::cube(),{2.52f,.57f,-.36f},{.38f,1.04f,.35f},source);
        auto& mirror=lighting_example::object(scene,"Off-screen reflection mirror",lighting_example::panelMesh(),
                                              mirrorPosition,{.48f,.46f,1},
                                              lighting_example::material({.84f,.84f,.84f},1,.055f));
        mirror.setSavedId("dynamic.gi.mirror");
        mirror.transform.rotation={0,-.84f,0};

        camera.transform={};
        camera.transform.position={1.68f,2.3f,6.8f};
        camera.lookAt({1.68f,1.14f,0});
        camera.setPerspective(.47f,.1f,25);
        DiffuseGiVolume volume;
        volume.minimum={.10f,.12f,-1.66f};
        volume.maximum={3.5f,2.5f,1.66f};
        volume.counts={8,4,4};
        volume.rayDistance=9;
        volume.raysPerProbe=96;
        volume.updatesPerFrame=8;
        gi_.configure(volume);
        gi_.replaceStatic(staticGeometry_);
        ReflectionCaptureSettings reflectionSettings;
        reflectionSettings.position=mirrorPosition+Vec3{std::sin(-.84f),0,std::cos(-.84f)}*.04f;
        reflectionSettings.minimum={.1f,.12f,-1.66f};
        reflectionSettings.maximum={3.5f,2.5f,1.66f};
        reflectionSettings.blendDistance=.2f;
        reflectionSettings.size=16;
        reflectionSettings.workPerUpdate=256;
        reflectionSettings.rayDistance=9;
        reflection_.configure(reflectionSettings,scene.environment);
        scene.synchronize();
        lights_=scene.collect().lights;
        setDoor(scene,true);
    }

    void setDoor(Scene& scene, bool open) {
        if(open_==open) return;
        auto* mesh=dynamic_cast<MeshComponent*>(scene.findSavedNode("dynamic.gi.door"));
        if(!mesh) return;
        const auto position=doorway+Vec3{0,0,open?-1.58f:0.f};
        std::vector<DiffuseGiTriangle> moving;
        appendBox(moving,position,doorScale,{.015f,.02f,.025f});
        gi_.replaceDynamic(moving,samplePosition);
        mesh->transform.position=position;
        open_=open;
        settled_=false;
        reflection_.invalidate();
    }

    void setEnabled(Scene& scene, bool enabled) {
        if(enabled_==enabled) return;
        scene.setDiffuseGi(enabled?gi_.snapshot():nullptr);
        enabled_=enabled;
    }

    void setBounces(unsigned bounces) {
        auto volume=gi_.snapshot()->volume;
        if(volume.bounces==bounces)return;
        volume.bounces=bounces;
        gi_.configure(volume);
        settled_=false;
        reflection_.invalidate();
    }

    void setOffscreenReflections(Scene& scene,bool enabled) {
        if(offscreen_==enabled)return;
        scene.setReflectionProbe(enabled?reflection_.snapshot():nullptr);
        offscreen_=enabled;
    }

    bool advance(Scene& scene) {
        bool changed=false;
        if(enabled_&&!settled_) {
            if(gi_.update(lights_,skyRadiance)) {
                scene.setDiffuseGi(gi_.snapshot());
                changed=true;
            } else settled_=true;
        }
        if(offscreen_) {
            changed=reflection_.update(gi_,lights_,skyRadiance,enabled_?gi_.completedSnapshot():nullptr)||changed;
            scene.setReflectionProbe(reflection_.snapshot());
        }
        return changed;
    }

    bool open() const {return open_.value_or(true);}
    bool enabled() const {return enabled_;}
    bool settled() const {return (!enabled_ || settled_)&&(!offscreen_||!reflection_.progress().pending);}
    std::uint64_t revision() const {return gi_.revision();}
    unsigned bounces() const {return gi_.snapshot()->volume.bounces;}
    bool offscreenReflections() const {return offscreen_;}
    std::uint64_t reflectionRevision() const {return reflection_.revision();}
    float reflectionProgress() const {
        const auto p=reflection_.progress();
        return p.total?float(p.completed)/float(p.total):0;
    }

private:
    static void appendBox(std::vector<DiffuseGiTriangle>& triangles, Vec3 center, Vec3 size, Vec3 diffuse) {
        const auto mesh=Mesh::cube();
        auto world=[&](std::uint32_t index) {
            const auto point=mesh->vertices[index].position;
            return center+Vec3{point.x*size.x,point.y*size.y,point.z*size.z};
        };
        for(std::size_t i=0;i<mesh->indices.size();i+=3)
            triangles.push_back({world(mesh->indices[i]),world(mesh->indices[i+1]),world(mesh->indices[i+2]),diffuse});
    }

    DynamicDiffuseGi gi_;
    DynamicReflectionCapture reflection_;
    std::vector<DiffuseGiTriangle> staticGeometry_;
    std::vector<SceneLight> lights_;
    std::optional<bool> open_;
    bool enabled_=true,settled_=false;
    bool offscreen_=false;
};
} // namespace engine::dynamic_gi_example
