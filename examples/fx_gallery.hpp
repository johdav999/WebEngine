#pragma once
// Portable deterministic FX examples, matching the existing browser acceptance scenes.
#include "lighting_gallery.hpp"
#include "engine/effect_presets.hpp"
#include "engine/particle_emitter_component.hpp"
namespace engine::fx_example {
struct Gallery {
    std::unique_ptr<Scene> scene=std::make_unique<Scene>("FX rendering");
    Scene* world=scene.get();CameraComponent* camera=nullptr;Actor* fixture=nullptr;
    std::vector<CameraComponent*> cameras;
    Gallery(){camera=&world->spawn("Camera").addComponent<CameraComponent>();setup(16);}
    void setup(int mode){
    const bool surfaceTrail=mode>=86,occludedSurface=mode==95;
    if(surfaceTrail)mode=occludedSurface?26:mode-60;
    auto& active=*world;
    if(fixture) { fixture->destroy();active.synchronize(); }
    cameras={camera};
    fixture=&active.addChild<Actor>("FX acceptance fixture");camera->enabled=true;
    camera->transform={};camera->transform.position={0,0,5};camera->lookAt({0,0,0});camera->setOrthographic(4,0.1f,20);
    camera->setViewport({});
    active.ambient={1,1,1};active.setFog(std::nullopt);active.setSky(std::nullopt);active.environment.reset();
    auto particle=[&](const char* name,Vec3 position,Color4 color,BlendMode blend=BlendMode::Alpha) -> ParticleEmitterComponent& {
        auto d=std::make_shared<ParticleEmitterDefinition>();d->spawnRate=0;d->looping=false;d->duration=8;d->bursts={{0,1}};
        d->lifetimeMin=d->lifetimeMax=4;d->velocityMin=d->velocityMax={};d->size={{0,2}};d->color={{0,color}};d->blend=blend;
        auto& e=fixture->addChild<ParticleEmitterComponent>(name,d);e.transform.position=position;return e;
    };
    auto cube=[&](const char* name,RenderLayer layer,BlendMode blend=BlendMode::Opaque) {
        auto& m=fixture->addChild<MeshComponent>(name,Mesh::cube());m.layer=layer;m.blend=blend;m.opacity=0.5f;
        m.transform.position={0,0,1};m.transform.scale={3,3,0.2f};m.color={0,1,0};
    };
    if(mode==1||mode==2||mode==8) particle("Red",{0,0,0},{1,0,0,0.5f});
    if(mode==2||mode==3) cube("Occluder",RenderLayer::World);
    if(mode==4||mode==5) {
        if(mode==4) { particle("Near",{0,0,0},{0,0,1,0.5f});particle("Far",{0,0,-1},{1,0,0,0.5f}); }
        else { particle("Far",{0,0,-1},{1,0,0,0.5f});particle("Near",{0,0,0},{0,0,1,0.5f}); }
    }
    if(mode==6||mode==7) {
        particle("Glow",{0,0,0},{1,0,0,0.5f},BlendMode::Additive);
        if(mode==6) particle("Glow 2",{0,0,-1},{1,0,0,0.5f},BlendMode::Additive);
    }
    if(mode==8||mode==9) cube("Overlay",RenderLayer::Overlay);
    if(mode>=10&&mode<=12) {
        auto& e=particle("Atlas",{0,0,0},{1,1,1,1});auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());
        auto t=std::make_shared<Texture>();t->width=t->height=4;t->pixels.resize(64);t->wrapU=t->wrapV=TextureWrap::Clamp;
        const std::array<std::array<std::uint8_t,4>,4> colors={{{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}}};
        for(int y=0;y<4;++y) for(int x=0;x<4;++x) for(int c=0;c<4;++c) t->pixels[(y*4+x)*4+c]=colors[(y/2)*2+x/2][c];
        d->texture=t;d->flipbook={2,2,4,1};e.setDefinition(d);
    }
    if(mode==13||mode==14) cube("Mesh blending",RenderLayer::World,mode==13?BlendMode::Alpha:BlendMode::Opaque);
    if(mode>=15&&mode<=25) {
        const bool stretched=mode==19,clear=mode==18,far=mode==17;
        if(!clear&&mode!=19&&mode!=20) cube("Soft particle receiver",RenderLayer::World);
        auto& e=particle("Milestone particle",{0,0,far?2.1f:1.15f},{1,0,0,.8f},mode>=24 ? BlendMode::Additive : BlendMode::Alpha);
        auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());
        d->softDistance=(mode==15||mode==20||mode==22||mode==25) ? 0 : .5f;
        if(mode==19||mode==20) {d->size={{0,.3f}};d->velocityMin=d->velocityMax={2,0,0};d->alignment=stretched ? ParticleAlignment::Velocity : ParticleAlignment::Camera;d->stretch=.8f;d->softDistance=0;}
        e.setDefinition(d);
        if(mode==21||mode==22) camera->setPerspective(pi/3,.1f,20);
        if(mode==23) {
            e.visibilityMask=3;camera->setViewport({0,0,.5f,1});
            auto& second=fixture->addChild<CameraComponent>("Soft clear view");second.transform=camera->transform;second.setOrthographic(4,.1f,20);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});
            cameras={camera,&second};
        }
    }
    if(mode>=26&&mode<=34){
        if(mode!=29)cube("Trail depth receiver",RenderLayer::World);
        auto d=std::make_shared<ParticleEmitterDefinition>();d->renderMode=surfaceTrail?ParticleRenderMode::SurfaceTrail:ParticleRenderMode::Ribbon;d->space=ParticleSpace::World;
        d->trailLifetime=4;d->trailMinDistance=.1f;d->size={{0,2}};d->color={{0,{1,0,0,.8f}}};
        d->blend=(mode==32||mode==33)?BlendMode::Additive:BlendMode::Alpha;
        d->softDistance=(mode==26||mode==31||mode==33)?0:.5f;
        auto& e=fixture->addChild<ParticleEmitterComponent>("Depth trail",d);
        e.transform.position={-.9f,0,occludedSurface?.9f:mode==28?2.1f:1.15f};
        if(surfaceTrail)e.submitSurfaceContact(e.transform.position,{0,0,1});
        active.update(0);e.transform.position.x=.9f;
        if(surfaceTrail)e.submitSurfaceContact(e.transform.position,{0,0,1});
        active.update(.1);
        if(mode==30||mode==31)camera->setPerspective(pi/3,.1f,20);
        if(mode==34){e.visibilityMask=3;camera->setViewport({0,0,.5f,1});
            auto& second=fixture->addChild<CameraComponent>("Trail clear view");second.transform=camera->transform;second.setOrthographic(4,.1f,20);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});
            cameras={camera,&second};}
    }
    if(mode>=35&&mode<=52){
        if(mode<=43&&mode!=38)cube("Plane depth receiver",RenderLayer::World);
        auto& e=particle("Plane acceptance",{0,0,mode==37?2.1f:1.15f},{1,0,0,.8f},(mode==41||mode==42)?BlendMode::Additive:BlendMode::Alpha);
        auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());d->alignment=ParticleAlignment::Plane;d->planeNormal={0,0,1};
        d->softDistance=(mode==35||mode==40||mode==42||mode>=44)?0:.5f;
        if(mode==39||mode==40)camera->setPerspective(pi/3,.1f,20);
        if(mode==43){e.visibilityMask=3;camera->setViewport({0,0,.5f,1});
            auto& second=fixture->addChild<CameraComponent>("Plane clear view");second.transform=camera->transform;second.setOrthographic(4,.1f,20);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});
            cameras={camera,&second};}
        if(mode>=44&&mode<=47){d->texture=groundRingEffect()->emitters[0].definition->texture;d->size={{0,3}};d->color={{0,{.1f,.7f,1,1}}};
            d->planeNormal=mode==46?Vec3{.5f,1,.4f}:Vec3{0,1,0};d->alignment=mode==45?ParticleAlignment::Camera:ParticleAlignment::Plane;
            e.transform.position={0,.02f,0};camera->transform.position={0,3,5};camera->lookAt({});
            if(mode==47)camera->setPerspective(pi/3,.1f,20);
            auto& floor=fixture->addChild<MeshComponent>("Ground receiver",Mesh::plane(12));floor.color={.04f,.04f,.04f};MaterialInstance m;m.shader=MaterialShader::Unlit;floor.setMaterial(0,m);}
        if(mode==48){cube("Offset receiver",RenderLayer::World);d->surfaceOffset=-.2f;}
        if(mode==49||mode==50){auto t=std::make_shared<Texture>();t->width=4;t->height=2;t->pixels.resize(32);
            for(int y=0;y<2;++y)for(int x=0;x<4;++x){const auto i=(y*4+x)*4;t->pixels[i]=x<2?255:0;t->pixels[i+1]=x<2?0:255;t->pixels[i+3]=255;}
            d->texture=t;d->color={{0,{1,1,1,1}}};d->flipbook={2,1,2,1};}
        if(mode==51){FogSettings f;f.color={0,0,1};f.density=.5f;f.heightFalloff=0;active.setFog(f);}
        e.setDefinition(d);
    }
    if(mode>=53&&mode<=68){
        auto mesh=std::make_shared<Mesh>(*Mesh::cube());auto material=std::make_shared<Material>();material->shader=MaterialShader::Unlit;mesh->materials={material};
        auto make=[&](Vec3 position,Color4 color,BlendMode blend=BlendMode::Alpha)->ParticleEmitterComponent&{
            auto& e=particle("Mesh particle acceptance",position,color,blend);auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());
            d->renderMode=ParticleRenderMode::Mesh;d->particleMesh=mesh;d->meshAngularVelocity={2,1,3};e.setDefinition(d);return e;};
        if(mode==54){auto& m=fixture->addChild<MeshComponent>("Equivalent mesh component",mesh);m.transform.scale={2,2,2};m.color={1,0,0};m.opacity=.8f;m.blend=BlendMode::Alpha;}
        else if(mode==56||mode==57){
            if(mode==56){make({0,0,0},{0,0,1,.5f});make({0,0,-1},{1,0,0,.5f});}
            else {make({0,0,-1},{1,0,0,.5f});make({0,0,0},{0,0,1,.5f});}
        }else {
            auto& e=make({},{1,0,0,.8f},(mode==58||mode==59)?BlendMode::Additive:BlendMode::Alpha);
            auto d=std::make_shared<ParticleEmitterDefinition>(e.definition());
            if(mode==55)cube("Mesh particle occluder",RenderLayer::World);
            if(mode==58)make({0,0,-1},{1,0,0,.8f},BlendMode::Additive);
            if(mode==60){FogSettings f;f.color={0,0,1};f.density=.5f;f.heightFalloff=0;active.setFog(f);}
            if(mode==61||mode==62||mode==68)d->meshRotationMin=d->meshRotationMax={.4f,.7f,.2f};
            if(mode==62){auto blue=std::make_shared<Material>(*material);blue->baseColor={0,0,1};material->baseColor={1,0,0};mesh->materials.push_back(blue);mesh->parts={{0,18,0},{18,18,1}};d->color={{0,{1,1,1,.8f}}};}
            if(mode==63){e.visibilityMask=3;camera->setViewport({0,0,.5f,1});cube("Masked receiver",RenderLayer::World);
                auto& second=fixture->addChild<CameraComponent>("Mesh masked clear view");second.transform=camera->transform;second.setOrthographic(4,.1f,20);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});cameras={camera,&second};}
            if(mode==64){d->blend=BlendMode::Opaque;d->color={{0,{1,.6f,.1f,1}}};d->bursts={{0,128}};d->maxParticles=128;d->positionMin={-1.6f,-1.6f,-.2f};d->positionMax={1.6f,1.6f,.2f};d->size={{0,.1f}};}
            if(mode==65||mode==66){auto blue=std::make_shared<Texture>();blue->width=blue->height=1;blue->pixels={0,0,255,255};material->baseColorTexture=blue;
                auto green=std::make_shared<Texture>();green->width=green->height=1;green->pixels={0,255,0,255};d->texture=green;d->meshTextureOverride=mode==65;d->color={{0,{1,1,1,1}}};}
            if(mode==67){mesh=std::make_shared<Mesh>(*Mesh::plane());mesh->materials={material};material->doubleSided=true;d->particleMesh=mesh;d->meshRotationMin=d->meshRotationMax={pi/2,0,0};}
            e.setDefinition(d);
        }
    }

    if(mode==69||mode==70){
        auto quad=[&]{particle("Near blue quad",{0,0,0},{0,0,1,.5f});};
        auto mesh=[&]{auto& m=fixture->addChild<MeshComponent>("Far red mesh",lighting_example::panelMesh());m.transform.position={0,0,-1};m.color={1,0,0};m.opacity=.5f;m.blend=BlendMode::Alpha;MaterialInstance material;material.shader=MaterialShader::Unlit;m.setMaterial(0,material);};
        if(mode==69){quad();mesh();}else{mesh();quad();}
    }
    if(mode>=71&&mode<=74){particle("Fog billboard",{0,0,0},{1,0,0,.8f},mode==72||mode==73?BlendMode::Additive:BlendMode::Alpha);
        if(mode<=72){FogSettings f;f.color={0,0,1};f.density=.5f;f.heightFalloff=0;active.setFog(f);}}
    active.update(mode==11?1.1:mode==12?3.1:mode==50?2.1:mode==68?.5:0);active.synchronize();
    }
};
}
