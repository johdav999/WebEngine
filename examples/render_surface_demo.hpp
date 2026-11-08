#pragma once
#include "engine/mesh_component.hpp"
#include "engine/render_view.hpp"
#include "engine/scene.hpp"

namespace render_surface_demo {
using namespace engine;

enum class Mode { Live, Frozen, Refreshed, Resized, Removed };

class Monitor {
public:
    Monitor(Actor& root,CameraComponent& camera)
        : main_(camera),rear_(root.addChild<CameraComponent>("Rear camera")),
          screen_(panel(root,"Dashboard monitor",{0,0,0},{3.4f,1,2.2f},1)),
          feedback_(panel(root,"Monitor visible to rear camera",{12,0,1},{1,1,.6f},2)),
          upper_(panel(root,"Upper rear marker",{12,1,0},{10,1,2},2)),
          original_(std::make_shared<RenderSurface>(256,128)),
          resized_(std::make_shared<RenderSurface>(160,160)) {
        auto& lower=panel(root,"Lower rear marker",{12,-1,0},{10,1,2},2);
        lower.color={0,0,1};
        auto& bezel=panel(root,"Monitor bezel",{0,0,-.1f},{3.65f,1,2.45f},1);
        bezel.color={.075f,.085f,.10f};
        main_.transform={};
        main_.transform.position={0,0,5};
        main_.lookAt({});
        main_.setOrthographic(4,.1f,20);
        main_.setVisibilityMask(1);
        main_.setViewport({});
        rear_.transform.position={12,0,5};
        rear_.lookAt({12,0,0});
        rear_.setOrthographic(4,.1f,20);
        rear_.setVisibilityMask(2);
    }

    std::array<RenderView,2> configure(Mode mode) {
        upper_.color=mode==Mode::Live ? Vec3{1,0,0} : Vec3{0,1,0};
        const auto& target=mode==Mode::Resized ? resized_ : original_;
        MaterialInstance material;
        material.shader=MaterialShader::Unlit;
        material.baseColorSurface=target;
        material.baseColorTexture=fallback();
        screen_.setMaterial(0,material);
        feedback_.setMaterial(0,material);
        std::array views={RenderView{&main_},RenderView{&rear_}};
        for(auto& view:views) view.skyEnabled=view.fogEnabled=false;
        views[1].target=target;
        views[1].refreshSurface=mode!=Mode::Frozen;
        return views;
    }

private:
    static MeshComponent& panel(Actor& root,const char* name,Vec3 position,Vec3 scale,std::uint32_t mask) {
        auto& panel=root.addChild<MeshComponent>(name,Mesh::plane());
        panel.transform.position=position;
        panel.transform.rotation.x=pi/2;
        panel.transform.scale=scale;
        panel.visibilityMask=mask;
        panel.color={1,1,1};
        MaterialInstance material;
        material.shader=MaterialShader::Unlit;
        panel.setMaterial(0,material);
        return panel;
    }

    static std::shared_ptr<const Texture> fallback() {
        static const auto image=[] {
            auto value=std::make_shared<Texture>();
            value->width=value->height=1;
            value->wrapU=value->wrapV=TextureWrap::Clamp;
            value->pixels={255,255,0,255};
            return value;
        }();
        return image;
    }

    CameraComponent& main_;
    CameraComponent& rear_;
    MeshComponent& screen_;
    MeshComponent& feedback_;
    MeshComponent& upper_;
    RenderSurfaceHandle original_,resized_;
};
} // namespace render_surface_demo
