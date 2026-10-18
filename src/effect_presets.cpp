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
std::shared_ptr<const EffectAsset> fountainEffect() {
    static const auto asset=[] {
        auto a=std::make_shared<EffectAsset>();a->name="Fountain";
        auto drops=std::make_shared<ParticleEmitterDefinition>();drops->name="Fountain drops";drops->texture=radialAtlas(false);
        drops->shape=ParticleShape::Cone;drops->radius=.12f;drops->coneAngle=.3f;drops->speedMin=3;drops->speedMax=5;
        drops->velocityMin=drops->velocityMax={};drops->gravity={0,-3,0};drops->drag=.12f;drops->wind={.2f,0,0};
        drops->space=ParticleSpace::World;drops->spawnRate=80;drops->lifetimeMin=1.5f;drops->lifetimeMax=2.5f;
        drops->alignment=ParticleAlignment::Velocity;drops->stretch=.075f;drops->softDistance=.18f;
        drops->size={{0,.07f},{1,.025f}};drops->color={{0,{.2f,.7f,1,.9f}},{.8f,{.3f,.6f,1,.7f}},{1,{.3f,.6f,1,0}}};
        a->emitters.push_back({drops,{}});
        auto mist=std::make_shared<ParticleEmitterDefinition>();mist->name="Drifting mist";mist->texture=drops->texture;mist->seed=31;
        mist->shape=ParticleShape::Disc;mist->radius=.45f;mist->drag=.7f;mist->wind={.35f,.1f,0};mist->turbulence={.15f,.08f,.15f};
        mist->velocityMin={-.1f,.1f,-.1f};mist->velocityMax={.1f,.3f,.1f};mist->spawnRate=12;mist->lifetimeMin=2;mist->lifetimeMax=3;
        mist->size={{0,.2f},{1,1.1f}};mist->softDistance=.5f;mist->color={{0,{.5f,.7f,.85f,0}},{.2f,{.5f,.7f,.85f,.35f}},{1,{.5f,.7f,.85f,0}}};
        a->emitters.push_back({mist,{}});a->validate();return a;
    }();return asset;
}

std::shared_ptr<const EffectAsset> ribbonEffect(){
    static const auto asset=[] {auto a=std::make_shared<EffectAsset>();a->name="Ribbon trail";
        auto d=std::make_shared<ParticleEmitterDefinition>();d->name="Ribbon";d->renderMode=ParticleRenderMode::Ribbon;
        d->space=ParticleSpace::World;d->trailLifetime=2;d->trailMinDistance=.04f;d->trailBreakDistance=3;d->trailMaxPoints=256;
        d->size={{0,.3f},{1,.02f}};d->color={{0,{.2f,1.3f,1.5f,1}},{.5f,{.8f,.3f,1,.7f}},{1,{.4f,.1f,1,0}}};
        a->emitters.push_back({d,{}});a->validate();return a;
    }();return asset;
}

std::shared_ptr<const EffectAsset> groundRingEffect(){
    static const auto asset=[] {auto a=std::make_shared<EffectAsset>();a->name="Ground ring";
        auto texture=std::make_shared<Texture>();texture->name="Procedural ground ring";texture->width=texture->height=64;texture->pixels.resize(64*64*4);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){const float dx=(x+.5f)/32-1,dy=(y+.5f)/32-1;
            const float alpha=std::clamp(1-std::abs(std::sqrt(dx*dx+dy*dy)-.75f)/.08f,0.f,1.f);const auto i=(y*64+x)*4;
            texture->pixels[i]=texture->pixels[i+1]=texture->pixels[i+2]=255;texture->pixels[i+3]=std::uint8_t(alpha*255);}
        auto d=std::make_shared<ParticleEmitterDefinition>();d->name="Ground ring";d->texture=texture;d->alignment=ParticleAlignment::Plane;
        d->space=ParticleSpace::World;d->surfaceOffset=.02f;d->spawnRate=1;d->duration=2;d->lifetimeMin=d->lifetimeMax=1.5f;d->velocityMin=d->velocityMax={};
        d->size={{0,.2f},{1,3}};d->color={{0,{.2f,.8f,1,1}},{1,{.3f,.4f,1,0}}};
        a->emitters.push_back({d,{}});a->validate();return a;
    }();return asset;
}

std::shared_ptr<const EffectAsset> debrisEffect(){
    static const auto asset=[] {auto a=std::make_shared<EffectAsset>();a->name="Mesh debris";
        auto mesh=std::make_shared<Mesh>(*Mesh::cube());auto material=std::make_shared<Material>();material->name="Debris material";material->roughness=.85f;mesh->materials={material};
        auto d=std::make_shared<ParticleEmitterDefinition>();d->name="Fragments";d->renderMode=ParticleRenderMode::Mesh;d->particleMesh=mesh;
        d->space=ParticleSpace::World;d->blend=BlendMode::Opaque;d->spawnRate=12;d->maxParticles=256;d->duration=2;d->lifetimeMin=1.5f;d->lifetimeMax=2;
        d->bursts={{0,12}};d->velocityMin={-1,2,-1};d->velocityMax={1,3,1};d->gravity={0,-3,0};
        d->meshRotationMax={2*pi,2*pi,2*pi};d->meshAngularVelocity={2,3,1};d->size={{0,.15f},{1,.06f}};
        d->color={{0,{1,.65f,.2f,1}},{1,{.6f,.2f,.1f,1}}};a->emitters.push_back({d,{}});a->validate();return a;
    }();return asset;
}

}
