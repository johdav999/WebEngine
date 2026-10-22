#pragma once
#include "counter/counter.hpp"
#include "engine/camera_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene.hpp"
namespace counter_example {
struct View {
    engine::CameraComponent* camera=nullptr;
    engine::MeshComponent *a=nullptr,*b=nullptr;
    std::unique_ptr<engine::Scene> make() {
        using namespace engine;
        auto scene=std::make_unique<Scene>("Independent plugin sessions");
        camera=&scene->spawn("Camera").addComponent<CameraComponent>();
        camera->transform.position={0,3,9};camera->lookAt({0,1,0});
        auto cube=[&](const char* name,float x,Vec3 color){
            auto& m=scene->spawn(name).addComponent<MeshComponent>(name,Mesh::cube());
            m.transform.position={x,.5f,0};m.transform.scale={1,.7f,1};
            auto material=std::make_shared<Material>();material->shader=MaterialShader::Unlit;material->baseColor=color;
            m.setMaterial(0,MaterialInstance{material});return &m;};
        a=cube("Session A",-1.5f,{.15f,.75f,.85f});b=cube("Session B",1.5f,{.95f,.55f,.2f});return scene;
    }
    void project(const Demo& d) {
        const float ah=.7f+.25f*float(d.first->counter.value%12),bh=.7f+.25f*float(d.second->counter.value%12);
        a->transform.scale.y=ah;a->transform.position.y=ah/2;
        b->transform.scale.y=bh;b->transform.position.y=bh/2;
    }
};
}
