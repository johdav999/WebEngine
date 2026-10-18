#include "fx_contact_fixture.hpp"
#include "engine/decal.hpp"
#include "engine/mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/physics.hpp"
#include <emscripten.h>
using namespace engine;
EM_JS(void, contact_state,(int alive,int impacts,int queries,double skipped,int settled,int triangles,int limited),{
    window.fxContactState={alive,impacts,queries,skipped,settled:!!settled,triangles,limited:!!limited};
});
void fxContactFixture(Scene& scene,Actor& root,CameraComponent& camera,Renderer& renderer,int mode){
    camera.transform.position={0,3,5};camera.lookAt({});camera.setOrthographic(4,.1f,30);
    auto& ground=root.addChild<MeshComponent>("Decal receiver",Mesh::plane(6));ground.color={.15f,.15f,.15f};ground.visibilityMask=mode==80?3:1;
    MaterialInstance material;material.shader=MaterialShader::Unlit;ground.setMaterial(0,material);
    DecalComponent* decal=nullptr;ParticleEmitterComponent* emitter=nullptr;
    DecalSettings settings;settings.color={1,0,0};settings.opacity=.8f;
    if(mode>=81){
        // Fixture/application policy: one contact source, one optional impact mark.
        auto& collider=root.addComponent<ColliderComponent>("Floor",CollisionShape::box({5,.1f,5}));collider.transform.position={0,-.1f,0};RigidBodySettings body;body.motion=BodyMotion::Static;root.addComponent<RigidBodyComponent>("Static",body);
        auto d=std::make_shared<ParticleEmitterDefinition>();d->space=ParticleSpace::World;d->spawnRate=0;d->bursts={{0,mode==85?8u:1u}};d->looping=false;d->lifetimeMin=d->lifetimeMax=3;d->velocityMin=d->velocityMax={1,-10,0};d->size={{0,.2f}};d->color={{0,{0,0,1,1}}};d->collision.enabled=true;
        d->collision.response=mode==82?ParticleCollisionResponse::Kill:mode==83?ParticleCollisionResponse::Slide:mode==84?ParticleCollisionResponse::Settle:ParticleCollisionResponse::Bounce;
        if(mode==85)d->collision.maxQueries=1;
        d->collision.excludeOwner=false; // The fixture owns both its floor and its emitter.
        emitter=&root.addChild<ParticleEmitterComponent>("Contact particles",d);emitter->transform.position={0,.5f,0};scene.update(0);scene.update(.1);
        if(mode==82&&!emitter->impacts().empty()){decal=&root.addChild<DecalComponent>("Impact mark",settings);decal->transform.position=emitter->impacts()[0].point;decal->transform.scale={1,1,1};}
    } else if(mode!=69){
        if(mode==72||mode==73)ground.transform.rotation.z=.35f;
        if(mode==73)settings.normalThreshold=.99f;
        if(mode==74)ground.receivesDecals=false;
        if(mode==75)ground.blend=BlendMode::Alpha;
        if(mode==76)settings.maxSourceTriangles=1;
        if(mode==77)settings.receiverMask=2;
        if(mode==78){auto texture=std::make_shared<Texture>();texture->width=texture->height=32;texture->pixels.resize(4096);for(int y=0;y<32;++y)for(int x=0;x<32;++x){const auto i=(y*32+x)*4;const float dx=(x+.5f)/16-1,dy=(y+.5f)/16-1;texture->pixels[i]=texture->pixels[i+1]=texture->pixels[i+2]=255;texture->pixels[i+3]=dx*dx+dy*dy<.7f?255:0;}settings.texture=texture;}
        decal=&root.addChild<DecalComponent>("Projected mark",settings);decal->transform.scale=mode==71?Vec3{1,1,1}:Vec3{2,2,2};
        if(mode==79){FogSettings fog;fog.color={0,0,1};fog.density=.5f;fog.heightFalloff=0;scene.setFog(fog);}
        if(mode==80){camera.setViewport({0,0,.5f,1});auto& second=root.addChild<CameraComponent>("No decal view");second.transform=camera.transform;second.setOrthographic(4,.1f,30);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});renderer.setViews(std::array{RenderView{&camera},RenderView{&second}});}
    }
    scene.collect();
    const auto alive=emitter?emitter->particles().size():0,impacts=emitter?emitter->impacts().size():0;
    const bool settled=alive&&emitter->particles()[0].settled;
    contact_state(int(alive),int(impacts),emitter?emitter->collisionQueries():0,emitter?double(emitter->skippedCollisionSteps()):0,settled,decal?int(decal->triangleCount()):0,decal?decal->projectionLimited():false);
}
