#pragma once
// Portable static scene used by the Windows lighting demo and its pixel acceptance.
#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include "engine/mesh_component.hpp"

namespace engine::lighting_example {
inline std::shared_ptr<const Texture> checker(const char* name,int kind) {
    auto t=std::make_shared<Texture>();t->name=name;t->width=t->height=32;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const bool a=((x/4+y/4)&1)!=0;
        std::array<unsigned char,4> p{};
        if(kind==0)p={255,230,190,static_cast<unsigned char>(a?255:0)};
        if(kind==1)p={static_cast<unsigned char>(a?205:65),static_cast<unsigned char>(a?95:170),220,255};
        if(kind==2)p={255,static_cast<unsigned char>(a?40:235),static_cast<unsigned char>(a?240:40),255};
        if(kind==3)p={static_cast<unsigned char>(a?35:255),0,0,255};
        if(kind==4)p={static_cast<unsigned char>(a?255:0),static_cast<unsigned char>(a?80:0),0,255};
        t->pixels.insert(t->pixels.end(),p.begin(),p.end());
    }return t;
}
inline std::shared_ptr<const Material> material(Vec3 color,float metallic=0,float roughness=.55f) {
    auto m=std::make_shared<Material>();m->baseColor=color;m->metallic=metallic;m->roughness=roughness;return m;
}
inline std::shared_ptr<const Mesh> panelMesh() {
    auto m=std::make_shared<Mesh>();m->vertices={{{-1,-1,0},{0,0,1},{0,1},{1,0,0,-1}},{{1,-1,0},{0,0,1},{1,1},{1,0,0,-1}},{{1,1,0},{0,0,1},{1,0},{1,0,0,-1}},{{-1,1,0},{0,0,1},{0,0},{1,0,0,-1}}};
    m->indices={0,1,2,0,2,3};return m;
}
inline MeshComponent& object(Scene& s,const char* name,std::shared_ptr<const Mesh> mesh,Vec3 position,Vec3 scale,std::shared_ptr<const Material> material) {
    auto& c=s.spawn(name).addComponent<MeshComponent>(name,std::move(mesh));c.color={1,1,1};c.transform.position=position;c.transform.scale=scale;c.setMaterial(0,MaterialInstance{std::move(material)});return c;
}
struct Gallery {
    std::unique_ptr<Scene> scene=std::make_unique<Scene>("Static materials and lighting");
    Scene* world=scene.get();CameraComponent* camera=nullptr;
    LightComponent *sun=nullptr,*spot=nullptr,*point=nullptr;
    MeshComponent *floor=nullptr,*gold=nullptr,*mapped=nullptr,*blocker=nullptr,*cutout=nullptr,*glass=nullptr;
    std::shared_ptr<const Material> goldMaterial,mappedMaterial,maskMaterial,glassMaterial;
    void reset() {
        world->environmentIntensity=.7f;world->environmentRotation=0;
        sun->enabled=spot->enabled=point->enabled=true;sun->intensity=2;spot->intensity=12;
        blocker->castsShadow=true;floor->receivesShadow=true;
        gold->setMaterial(0,MaterialInstance{goldMaterial});mapped->setMaterial(0,MaterialInstance{mappedMaterial});
        cutout->setMaterial(0,MaterialInstance{maskMaterial});glass->setMaterial(0,MaterialInstance{glassMaterial});
        mapped->transform.scale={1.2f,1.2f,1.2f};cutout->transform.scale={1.4f,1.4f,1.4f};
    }
};
inline Gallery makeGallery() {
    Gallery g;auto& s=*g.world;s.ambient={.035f,.04f,.05f};
    g.camera=&s.spawn("Camera").addComponent<CameraComponent>();g.camera->transform.position={11,8,15};g.camera->lookAt({0,1,0});g.camera->farPlane=60;
    g.sun=&s.spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);g.sun->direction={-.6f,-1,-.4f};g.sun->shadow.enabled=true;g.sun->shadow.halfExtent=12;g.sun->shadow.halfDepth=30;
    g.spot=&s.spawn("Spot").addComponent<LightComponent>("Spot",LightType::Spot);g.spot->transform.position={-4,7,4};g.spot->direction=normalized(Vec3{4,-7,-4});g.spot->color={1,.75f,.4f};g.spot->range=25;g.spot->innerAngle=.35f;g.spot->outerAngle=.8f;g.spot->shadow.enabled=true;
    g.point=&s.spawn("Point").addComponent<LightComponent>("Point",LightType::Point);g.point->transform.position={5,3,3};g.point->color={.1f,.35f,1};g.point->range=14;g.point->intensity=5;
    g.floor=&object(s,"Floor",Mesh::cube(),{0,-.3f,0},{18,.25f,16},material({.32f,.38f,.42f}));
    g.goldMaterial=material({.9f,.5f,.13f},1,.12f);g.gold=&object(s,"Gold",Mesh::sphere(),{-3,1,1},{1.2f,1.2f,1.2f},g.goldMaterial);
    auto mapped=std::make_shared<Material>();mapped->baseColor={.3f,.65f,.95f};mapped->metallic=.85f;mapped->roughness=.6f;
    mapped->normalTexture=checker("Normals",1);mapped->metallicRoughnessTexture=checker("Surface",2);mapped->occlusionTexture=checker("Occlusion",3);mapped->emissiveTexture=checker("Emission",4);mapped->emissive={.8f,.25f,.08f};
    g.mappedMaterial=mapped;g.mapped=&object(s,"Mapped sphere",Mesh::sphere(),{0,1,1},{1.2f,1.2f,1.2f},mapped);
    g.blocker=&object(s,"Occluder",Mesh::cube(),{3,1,-1},{2,2,2},material({.7f,.1f,.04f},.1f));
    auto mask=std::make_shared<Material>();mask->baseColorTexture=checker("Cutout",0);mask->alphaMode=AlphaMode::Mask;mask->doubleSided=true;
    g.maskMaterial=mask;g.cutout=&object(s,"Cutout",panelMesh(),{-2,2,-3},{1.4f,1.4f,1.4f},mask);
    auto glass=std::make_shared<Material>();glass->baseColor={.15f,.8f,.4f};glass->alphaMode=AlphaMode::Blend;glass->opacity=.4f;glass->doubleSided=true;
    g.glassMaterial=glass;g.glass=&object(s,"Glass",panelMesh(),{2,1.5f,3},{1.3f,1.3f,1.3f},glass);
    g.reset();return g;
}
}
