#include "engine/effect_presets.hpp"
namespace engine {
namespace {
std::shared_ptr<const Texture> radialAtlas(bool flame) {
    auto texture=std::make_shared<Texture>();texture->name=flame ? "Procedural flame flipbook" : "Soft smoke / spark";
    const unsigned cell=32;texture->width=flame ? cell*4 : cell;texture->height=flame ? cell*2 : cell;
    texture->wrapU=texture->wrapV=TextureWrap::Clamp;texture->pixels.resize(texture->width*texture->height*4);
    for(unsigned y=0;y<texture->height;++y) for(unsigned x=0;x<texture->width;++x) {
        const unsigned frame=(y/cell)*4+x/cell;
        const float u=(float(x%cell)+0.5f)/cell*2-1,v=(float(y%cell)+0.5f)/cell*2-1;
        const float bend=flame ? std::sin(v*5+frame*pi/4)*0.13f : 0;
        const float distance=std::sqrt((u+bend)*(u+bend)*(flame?2.0f:1.0f)+v*v);
        const float alpha=std::pow(std::max(0.0f,1-distance),flame?1.0f:2.0f);
        const auto i=(y*texture->width+x)*4;
        texture->pixels[i]=255;texture->pixels[i+1]=flame ? static_cast<std::uint8_t>(140+90*(1-distance)) : 255;
        texture->pixels[i+2]=flame ? 45 : 255;texture->pixels[i+3]=static_cast<std::uint8_t>(alpha*255);
    }
    return texture;
}
std::shared_ptr<ParticleEmitterDefinition> flameDefinition() {
    auto e=std::make_shared<ParticleEmitterDefinition>();e->name="Flame";e->texture=radialAtlas(true);e->blend=BlendMode::Additive;
    e->flipbook={4,2,8,2};e->spawnRate=30;e->lifetimeMin=0.6f;e->lifetimeMax=1.2f;
    e->positionMin={-0.12f,0,-0.12f};e->positionMax={0.12f,0.1f,0.12f};
    e->velocityMin={-0.1f,0.5f,-0.1f};e->velocityMax={0.1f,1.0f,0.1f};
    e->size={{0,0.55f},{0.25f,0.75f},{1,0.08f}};e->color={{0,{2,1.1f,0.4f,0.8f}},{1,{1,0.2f,0.05f,0}}};return e;
}
}
std::shared_ptr<const EffectAsset> campfireEffect() {
    static const auto asset=[] {
        auto a=std::make_shared<EffectAsset>();a->name="Campfire";
        auto fire=flameDefinition();a->emitters.push_back({fire,{}});
        auto smoke=std::make_shared<ParticleEmitterDefinition>();smoke->name="Smoke";smoke->texture=radialAtlas(false);smoke->seed=7;
        smoke->spawnRate=8;smoke->lifetimeMin=2;smoke->lifetimeMax=3.2f;
        smoke->positionMin={-0.1f,0.4f,-0.1f};smoke->positionMax={0.1f,0.6f,0.1f};
        smoke->velocityMin={0.05f,0.4f,-0.04f};smoke->velocityMax={0.2f,0.7f,0.04f};
        smoke->size={{0,0.4f},{1,1.7f}};smoke->color={{0,{0.3f,0.32f,0.36f,0}},{0.15f,{0.3f,0.32f,0.36f,0.6f}},{1,{0.3f,0.32f,0.36f,0}}};
        a->emitters.push_back({smoke,{}});
        auto sparks=std::make_shared<ParticleEmitterDefinition>();sparks->name="Sparks";sparks->texture=smoke->texture;sparks->blend=BlendMode::Additive;sparks->seed=17;
        sparks->spawnRate=14;sparks->lifetimeMin=0.5f;sparks->lifetimeMax=1.4f;sparks->velocityMin={-0.6f,1,-0.6f};sparks->velocityMax={0.6f,2.5f,0.6f};
        sparks->gravity={0,-0.8f,0};sparks->size={{0,0.08f},{1,0.015f}};sparks->color={{0,{3,1.2f,0.2f,1}},{1,{1,0.1f,0,0}}};
        a->emitters.push_back({sparks,{}});a->validate();return a;
    }();return asset;
}
std::shared_ptr<const EffectAsset> explosionEffect() {
    static const auto asset=[] {
        auto a=std::make_shared<EffectAsset>();a->name="Explosion";
        auto fire=flameDefinition();fire->looping=false;fire->spawnRate=0;fire->bursts={{0,12}};
        fire->velocityMin={-1.2f,-0.1f,-1.2f};fire->velocityMax={1.2f,1.2f,1.2f};
        fire->size={{0,0.3f},{0.2f,1.8f},{1,0.2f}};a->emitters.push_back({fire,{}});
        for(std::size_t i=1;i<campfireEffect()->emitters.size();++i) {
            auto e=std::make_shared<ParticleEmitterDefinition>(*campfireEffect()->emitters[i].definition);
            e->looping=false;e->spawnRate=0;e->bursts={{i==1?0.05:0.0,i==1?16u:40u}};
            e->velocityMin={-1.5f,0.4f,-1.5f};e->velocityMax={1.5f,2.5f,1.5f};
            if(i==2) e->gravity={0,-2.5f,0};
            a->emitters.push_back({e,{}});
        }
        a->validate();return a;
    }();return asset;
}
}
