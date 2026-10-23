#pragma once
// Portable layered-material and terrain content. Hosts own UI and scheduling.
#include "lighting_gallery.hpp"
#include "engine/terrain.hpp"
namespace engine::terrain_example {
inline std::shared_ptr<const Texture> solid(std::array<unsigned char,4> rgba) {
    auto t=std::make_shared<Texture>();t->width=t->height=1;t->wrapU=t->wrapV=TextureWrap::Clamp;t->pixels.assign(rgba.begin(),rgba.end());return t;
}
inline std::shared_ptr<const Texture> tile(unsigned layer,bool relief=false) {
    auto t=std::make_shared<Texture>();t->width=t->height=32;
    const std::array<std::array<float,3>,4> colors{{{.14f,.48f,.16f},{.82f,.63f,.3f},{.52f,.57f,.64f},{.48f,.18f,.09f}}};
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){std::array<unsigned char,4> p;
        const bool bright=((x/(3+layer)+y/(4+layer))&1)!=0;
        if(relief){const float nx=.5f+.22f*std::sin(float(x+layer*3)*.5f),ny=.5f+.2f*std::cos(float(y+layer*2)*.6f);
            p={static_cast<unsigned char>(nx*255),static_cast<unsigned char>(ny*255),240,static_cast<unsigned char>(bright?220:30)};}
        else{for(unsigned k=0;k<3;++k)p[k]=static_cast<unsigned char>(colors[layer][k]*(bright?255:150));p[3]=255;}
        t->pixels.insert(t->pixels.end(),p.begin(),p.end());
    }return t;
}
struct Gallery {
    std::unique_ptr<Scene> scene=std::make_unique<Scene>("Layered terrain");Scene* world=scene.get();
    Actor* terrain=nullptr;CameraComponent* camera=nullptr;LightComponent *sun=nullptr,*spot=nullptr;
    MeshComponent *pillar=nullptr,*sphere=nullptr;std::shared_ptr<const TerrainAsset> asset;
    Material defaults,current;
    void apply(const Material& appearance){
        appearance.validate();auto shared=std::make_shared<const Material>(appearance);
        for(auto* chunk:terrain->getComponents<TerrainComponent>())chunk->setMaterial(0,MaterialInstance{shared});
        pillar->setMaterial(0,MaterialInstance{shared});sphere->setMaterial(0,MaterialInstance{shared});current=appearance;
    }
    void detail(int cameraLevel,int shadowLevel,float cameraScale=1,float shadowScale=1){
        for(auto* chunk:terrain->getComponents<TerrainComponent>()){
            MeshLodPolicy p;p.cameraLevel=cameraLevel==4?(chunk->chunk()%2?3:0):cameraLevel;p.shadowLevel=shadowLevel;p.cameraScale=cameraScale;p.shadowScale=shadowScale;p.validate();chunk->lodPolicy=p;
        }
    }
    void overhead(bool enabled){
        if(enabled){camera->setOrthographic(20,.1f,80);camera->transform.position={0,30,0};camera->lookAt({0,0,0},{0,0,-1});}
        else{camera->setPerspective(pi/3,.1f,80);camera->transform.position={14,11,17};camera->lookAt({0,1,0});}
    }
    void reset(){
        apply(defaults);detail(0,0);overhead(false);pillar->enabled=sphere->enabled=true;
        pillar->transform.scale={2.5f,4,2.5f};pillar->transform.rotation={};pillar->castsShadow=pillar->receivesShadow=true;
        world->setSky(std::nullopt);world->setFog(std::nullopt);
    }
};
inline Gallery makeGallery(){
    Gallery g;auto& s=*g.world;s.ambient={.045f,.05f,.065f};s.environmentIntensity=.5f;
    g.camera=&s.spawn("Camera").addComponent<CameraComponent>();
    g.sun=&s.spawn("Sun").addComponent<LightComponent>("Sun",LightType::Directional);g.sun->direction={-.5f,-1,-.35f};g.sun->intensity=2.5f;g.sun->shadow.enabled=true;g.sun->shadow.halfExtent=15;g.sun->shadow.halfDepth=35;
    g.spot=&s.spawn("Spot").addComponent<LightComponent>("Spot",LightType::Spot);g.spot->transform.position={0,11,7};g.spot->direction={0,-1,-.5f};g.spot->color={.7f,.8f,1};g.spot->range=35;g.spot->intensity=8;g.spot->outerAngle=1;g.spot->shadow.enabled=true;
    g.defaults.roughness=.9f;g.defaults.metallic=.15f;g.defaults.layers.enabled=true;
    for(unsigned i=0;i<4;++i){auto& layer=g.defaults.layers.layers[i];layer.texture=tile(i);layer.normalHeightTexture=tile(i,true);layer.normalScale=.8f;layer.roughness=.25f+.2f*i;layer.heightScale=.3f+.15f*i;layer.uvScale={1.f+.17f*i,1.f+.11f*i};}
    auto weights=std::make_shared<Texture>();weights->width=weights->height=32;weights->wrapU=weights->wrapV=TextureWrap::Clamp;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const float u=float(x)/31,v=float(y)/31;for(float w:{(1-u)*(1-v),u*(1-v),(1-u)*v,u*v})weights->pixels.push_back(static_cast<unsigned char>(std::round(255*w)));}
    g.defaults.layers.weights=weights;g.defaults.layers.weightScale={3.f/16,3.f/16};g.defaults.layers.heightBlend=.3f;g.defaults.layers.triplanar=true;g.defaults.layers.triplanarScale=.6f;
    TerrainData data;data.samplesX=data.samplesZ=33;data.spacing={.5f,.5f};data.chunkCells=16;data.textureMeters=3;data.lodLevels=3;data.material=std::make_shared<const Material>(g.defaults);
    for(unsigned z=0;z<33;++z)for(unsigned x=0;x<33;++x){const float px=float(x)*.5f-8,pz=float(z)*.5f-8;data.heights.push_back(.5f+1.1f*std::sin(px*.36f)*std::cos(pz*.32f)+1.5f*std::exp(-.15f*((px+3)*(px+3)+(pz-2)*(pz-2))));}
    g.asset=TerrainAsset::create(std::move(data));auto terrain=makeTerrainActor("Terrain",g.asset,false);g.terrain=terrain.get();terrain->transform.position={-8,0,-8};s.adoptChild(std::move(terrain));
    auto mat=std::make_shared<const Material>(g.defaults);
    g.pillar=&lighting_example::object(s,"Layered pillar",Mesh::cube(),{-2,3,-1},{2.5f,4,2.5f},mat);
    g.sphere=&lighting_example::object(s,"Layered sphere",Mesh::sphere(),{3,2.4f,2},{1.7f,1.7f,1.7f},mat);
    g.reset();return g;
}
}
