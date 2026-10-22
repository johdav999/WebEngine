#pragma once
// Portable acceptance content. All rendering/simulation uses the shared engine;
// SDL, Windows and Dawn belong to the host and acceptance driver.
#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/terrain.hpp"

namespace engine::acceptance {
struct Gallery {
    std::unique_ptr<Scene> scene=std::make_unique<Scene>("Windows feature gallery");
    CameraComponent* camera=nullptr;
    SkeletalMeshComponent* animated=nullptr;
    Node *terrain=nullptr,*flatTerrain=nullptr;
    MeshComponent* water=nullptr;
    ParticleEmitterComponent* particles=nullptr;
    LightComponent *sun=nullptr,*spot=nullptr;
};
inline std::shared_ptr<const Material> material(Vec3 color,float metallic=0,float roughness=.55f,Vec3 emissive={}) {
    auto m=std::make_shared<Material>();m->baseColor=color;m->metallic=metallic;m->roughness=roughness;m->emissive=emissive;return m;
}
inline MeshComponent& object(Scene& scene,const char* name,std::shared_ptr<const Mesh> mesh,Vec3 position,Vec3 scale,std::shared_ptr<const Material> mat) {
    auto& c=scene.spawn(name).addComponent<MeshComponent>(name,std::move(mesh));
    c.transform.position=position;c.transform.scale=scale;c.color={1,1,1};if(mat)c.setMaterial(0,MaterialInstance{std::move(mat)});return c;
}
inline std::shared_ptr<const Mesh> waterMesh() {
    auto m=std::make_shared<Mesh>();constexpr unsigned n=48;constexpr float side=9;
    for(unsigned z=0;z<=n;++z)for(unsigned x=0;x<=n;++x)
        m->vertices.push_back({{side*(float(x)/n-.5f),0,side*(float(z)/n-.5f)},{0,1,0},{float(x)/n,float(z)/n},{1,0,0,-1}});
    for(unsigned z=0;z<n;++z)for(unsigned x=0;x<n;++x){auto a=z*(n+1)+x,b=a+1,c=a+n+1,d=c+1;m->indices.insert(m->indices.end(),{a,c,b,b,c,d});}
    auto mat=std::make_shared<Material>();mat->shader=MaterialShader::Water;mat->baseColor={.015f,.15f,.21f};mat->roughness=.15f;
    mat->doubleSided=true;mat->water.amplitude=.17f;mat->water.wavelength=2.3f;
    mat->water.planarReflectionStrength=.85f;mat->water.refractionStrength=.8f;mat->water.foamStrength=.8f;
    mat->water.underwaterStrength=1;mat->water.causticsStrength=1.5f;
    m->materials.push_back(mat);m->parts.push_back({0,std::uint32_t(m->indices.size()),0});return m;
}
inline Gallery makeGallery(std::shared_ptr<const Mesh> imported) {
    if(!imported||!imported->skeleton)throw std::invalid_argument("Gallery requires an imported skinned mesh");
    Gallery g;auto& s=*g.scene;s.ambient={.07f,.08f,.1f};s.environmentIntensity=.35f;
    g.camera=&s.spawn("Camera").addComponent<CameraComponent>();g.camera->transform.position={13,10,17};g.camera->lookAt({0,1,0});g.camera->farPlane=90;
    auto& sun=s.spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);g.sun=&sun;
    sun.direction={-.4f,-1,-.5f};sun.intensity=2;sun.shadow.enabled=true;sun.shadow.halfExtent=16;sun.shadow.halfDepth=35;
    auto& spot=s.spawn("Spot").addComponent<LightComponent>("Spot",LightType::Spot);g.spot=&spot;
    spot.transform.position={4,7,5};spot.direction={-.3f,-1,-.6f};spot.color={.2f,.55f,1};spot.intensity=12;spot.range=22;spot.outerAngle=.65f;spot.shadow.enabled=true;
    object(s,"Floor",Mesh::cube(),{0,-.7f,0},{22,.3f,20},material({.13f,.17f,.22f}));
    object(s,"Red occluder",Mesh::cube(),{1,1.1f,-1},{1.8f,2.4f,1.8f},material({.65f,.08f,.035f},.2f));
    object(s,"Metal sphere",Mesh::sphere(),{4,1.1f,-1},{1.2f,1.2f,1.2f},material({.7f,.55f,.2f},.85f,.18f));
    object(s,"Bloom source",Mesh::sphere(),{3,2.6f,-4},{.7f,.7f,.7f},material({1,.4f,.04f},0,.4f,{9,2.5f,.15f}));
    g.animated=&s.spawn("Animation").addComponent<SkeletalMeshComponent>("Skinned column",std::move(imported));
    g.animated->transform.position={-4,0,2};g.animated->color={.3f,.85f,.55f};g.animated->animation().play(0);g.animated->animation().seek(1);g.animated->animation().pause();
    for(bool raised:{false,true}) {
        TerrainData data;data.samplesX=data.samplesZ=33;data.spacing={.24f,.24f};data.chunkCells=16;data.lodLevels=3;data.material=material({.25f,.42f,.11f});
        for(unsigned z=0;z<33;++z)for(unsigned x=0;x<33;++x){float dx=(float(x)-16)/8,dz=(float(z)-16)/8;data.heights.push_back(raised?2.7f*std::exp(-.5f*(dx*dx+dz*dz)):-.12f);}
        auto actor=makeTerrainActor(raised?"Raised terrain":"Flat control",TerrainAsset::create(std::move(data)),false);
        actor->transform.position={-10,-.25f,-8};auto* node=actor.get();s.adoptChild(std::move(actor));
        if(raised)g.terrain=node;else{g.flatTerrain=node;node->enabled=false;}
    }
    object(s,"Lake bed",Mesh::cube(),{4,-.4f,5},{9,.2f,9},material({.5f,.37f,.12f}));
    g.water=&object(s,"Water",waterMesh(),{4,.15f,5},{1,1,1},nullptr);g.water->clearMaterial(0);g.water->frustumCulling=false;
    auto def=std::make_shared<ParticleEmitterDefinition>();def->seed=742;def->spawnRate=45;def->maxParticles=256;
    def->lifetimeMin=def->lifetimeMax=4;def->positionMin={-.5f,0,-.5f};def->positionMax={.5f,0,.5f};
    def->velocityMin={-.6f,.8f,-.6f};def->velocityMax={.6f,2.5f,.6f};def->size={{0,.16f},{1,.35f}};
    def->color={{0,{1,.45f,.05f,1}},{1,{.2f,.4f,1,.2f}}};def->softDistance=.3f;
    g.particles=&s.spawn("Particles").addComponent<ParticleEmitterComponent>("Sparks",def);g.particles->transform.position={0,.1f,4};
    // Warm CPU particles deterministically before Application owns the scene.
    s.update(1);g.particles->pause();return g;
}
}
