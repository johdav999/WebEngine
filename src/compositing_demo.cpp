#include "compositing_demo.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/particle_emitter_component.hpp"
using namespace engine;
void setCompositingDemo(Scene& scene,CameraComponent& camera,Renderer& renderer,int mode) {
    if(mode<0 || mode>3) throw std::invalid_argument("Unknown camera layout");
    static CameraComponent* overview=nullptr;
    if(!overview) overview=&scene.addChild<CameraComponent>("Overview camera");
    camera.setViewport(mode==1 ? Viewport{0,0,0.5f,1} : Viewport{});
    overview->setViewport(mode==1 ? Viewport{0.5f,0,0.5f,1} : Viewport{0.68f,0.05f,0.28f,0.32f});
    overview->transform.position={0,14,0};overview->lookAt({0,0,0},{0,0,-1});overview->setOrthographic(18,0.1f,100);
    if(mode==3) {overview->transform.position={10,5,12};overview->lookAt({0,0,0});}
    overview->enabled=mode!=0;
    const std::array views={RenderView{&camera},RenderView{overview,std::array<float,4>{0.04f,0.07f,0.1f,1}}};
    renderer.setViews({views.data(),mode==0 ? 1u : 2u});
}
void setCompositingFixture(Scene& scene,CameraComponent& camera,Renderer& renderer,int mode) {
    scene.setSky(std::nullopt); // Isolate compositor clear/load acceptance from playground weather.
    if(mode<0 || mode>11) throw std::invalid_argument("Unknown compositing fixture");
    static Actor* root=nullptr;
    static CameraComponent* other=nullptr;
    if(!root) {
        root=&scene.addChild<Actor>("Compositing fixture");
        other=&root->addChild<CameraComponent>("Second fixture camera");
        auto add=[&](const char* name,Vec3 color,std::uint32_t mask,float z) {
            auto& mesh=root->addChild<MeshComponent>(name,Mesh::cube());mesh.color=color;mesh.visibilityMask=mask;
            mesh.transform.position={0,0,z};mesh.transform.scale={1.5f,1.5f,0.1f};
            MaterialInstance material;material.shader=MaterialShader::Unlit;mesh.setMaterial(0,material);
        };
        add("First camera red",{1,0,0},1,0);add("Second camera green",{0,1,0},2,-1);
        auto d=std::make_shared<ParticleEmitterDefinition>();d->spawnRate=0;d->looping=false;d->duration=10;
        d->bursts={{0,1}};d->lifetimeMin=d->lifetimeMax=10;d->velocityMin=d->velocityMax={};
        d->size={{0,1}};d->color={{0,{0,0,1,1}}};
        auto& particle=root->addChild<ParticleEmitterComponent>("Blue fixture particle",d);
        particle.visibilityMask=4;particle.transform.position={0,0,1};
    }
    for(const auto& child:scene.children()) child->enabled=false;
    root->enabled=true;other->enabled=mode!=4&&mode!=11;camera.enabled=mode!=5;
    for(const auto& child:root->children()) {
        if(child->name=="First camera red") child->transform.rotation.z=mode==11 ? 0.37f : 0;
    }
    camera.transform={};camera.transform.position={0,0,5};camera.lookAt({});camera.setOrthographic(4,0.1f,20);
    other->transform=camera.transform;other->setOrthographic(4,0.1f,20);
    camera.setVisibilityMask(1);other->setVisibilityMask(mode==7?4:mode==8?8:2);
    camera.setViewport(mode==0 ? Viewport{0,0,0.5f,1} : Viewport{});
    other->setViewport(mode==0 ? Viewport{0.5f,0,0.5f,1} : mode==6 ? Viewport{0.8f,0.2f,0.15f,0.6f} : Viewport{0.55f,0.1f,0.4f,0.4f});
    if(mode==6) other->setOrthographic(2,0.1f,20);
    if(mode==10) other->transform.position.x=1; // Only the later view moves.
    std::array views={RenderView{&camera,std::array<float,4>{0.1f,0,0,1}},RenderView{other,std::array<float,4>{0,0,0.2f,1}}};
    // Acceptance fixtures use their authored clear colors, independent of scene sky.
    views[0].skyEnabled=views[1].skyEnabled=false;
    if(mode==2 || mode==7) views[1].clearColor=std::nullopt;
    if(mode==3) std::swap(views[0],views[1]);
    renderer.setViews(mode==9 ? std::span<const RenderView>{} : std::span<const RenderView>{views});
    scene.update(0);renderer.resetMeasurements();
}
