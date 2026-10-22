#include "engine/effect_component.hpp"
#include "engine/scene.hpp"
#include <unordered_set>
#include <unordered_map>
namespace engine {
void EffectLimits::validate() const {
    if(!maxActiveEffects||maxActiveEffects>256||!maxActiveDecals||maxActiveDecals>512||!maxSpawnsPerUpdate||maxSpawnsPerUpdate>1024||maxDepth>8||!maxEventsPerEmitter||maxEventsPerEmitter>4096)
        throw std::invalid_argument("Invalid effect composition limits");
}
void EffectAsset::validate() const {
    std::unordered_set<const EffectAsset*> path;
    std::unordered_map<const EffectAsset*,unsigned> heights;
    auto check=[&](auto&& self,const EffectAsset& a,unsigned depth)->void{
    if(depth>8||path.contains(&a))throw std::invalid_argument("Effect graph cycle or depth exceeds eight");
    if(const auto found=heights.find(&a);found!=heights.end()){if(depth+found->second>8)throw std::invalid_argument("Effect graph depth exceeds eight");return;}
    if(heights.size()+path.size()>=64)throw std::invalid_argument("Effect graph exceeds 64 assets");
    path.insert(&a);a.limits.validate();
    if(a.emitters.empty()||a.emitters.size()>64||a.triggers.size()>64) throw std::invalid_argument("Effect assets require 1 to 64 emitters and at most 64 triggers");
    for(const auto& emitter:a.emitters) {
        if(!emitter.definition) throw std::invalid_argument("Effect emitter is missing its definition");
        emitter.definition->validate();
        for(auto v:emitter.transform.matrix().m) if(!std::isfinite(v)) throw std::invalid_argument("Invalid effect emitter transform");
    }
    unsigned height=0;
    for(const auto& t:a.triggers){
        if(t.emitter>=a.emitters.size()||t.event<ParticleEventType::Birth||t.event>ParticleEventType::Death||(!t.effect&&!t.decal)||!std::isfinite(t.inheritVelocity)||t.inheritVelocity<0||t.inheritVelocity>10)
            throw std::invalid_argument("Invalid effect trigger");
        const auto mode=a.emitters[t.emitter].definition->renderMode;
        if(mode==ParticleRenderMode::Ribbon||mode==ParticleRenderMode::SurfaceTrail)throw std::invalid_argument("Particle triggers require quad or mesh emitters");
        if(t.event==ParticleEventType::Impact&&!a.emitters[t.emitter].definition->collision.enabled)throw std::invalid_argument("Impact trigger requires collision");
        if(t.decal){t.decal->settings.validate();if(t.decal->settings.lifetime<=0)throw std::invalid_argument("Triggered decals require finite lifetime");for(float x:{t.decal->scale.x,t.decal->scale.y,t.decal->scale.z})if(!std::isfinite(x)||x<=0||x>1000)throw std::invalid_argument("Invalid triggered decal scale");}
        if(t.effect){self(self,*t.effect,depth+1);height=std::max(height,1+heights.at(t.effect.get()));}
    }
    path.erase(&a);heights.emplace(&a,height);
    };check(check,*this,0);
}
EffectComponent::EffectComponent(std::string name,std::shared_ptr<const EffectAsset> asset)
    : Component(std::move(name)),asset_(std::move(asset)) {
    if(!asset_) throw std::invalid_argument("Missing effect asset");
    asset_->validate();setTickEnabled(true);emitters_.reserve(asset_->emitters.size());
    for(const auto& entry:asset_->emitters) {
        auto& emitter=addChild<ParticleEmitterComponent>(entry.definition->name,entry.definition);
        emitter.transform=entry.transform;protectChild(emitter);emitters_.push_back(&emitter);
    }
    for(std::size_t i=0;i<emitters_.size();++i){std::uint32_t mask=0;for(const auto& t:asset_->triggers)if(t.emitter==i)mask|=1u<<std::uint32_t(t.event);emitters_[i]->captureEvents(mask,asset_->limits.maxEventsPerEmitter);}
}
void EffectComponent::clearSpawned(){for(std::size_t i=children().size();i-->emitters_.size();)if(auto* c=dynamic_cast<EffectComponent*>(children()[i].get());c&&c->spawned_)removeChild(*c);else if(auto* d=dynamic_cast<DecalComponent*>(children()[i].get());d&&d->effectSpawned_)removeChild(*d);}
void EffectComponent::play(bool restart) { if(restart){clearSpawned();totalSpawned_=totalDropped_=0;}paused_=false;spawning_=true;for(auto* e:emitters_) e->play(restart);if(!restart)for(const auto& c:children())if(auto* e=dynamic_cast<EffectComponent*>(c.get()))e->resume(); }
void EffectComponent::pause() { paused_=true;for(auto* e:emitters_) e->pause();for(const auto& c:children())if(auto* e=dynamic_cast<EffectComponent*>(c.get()))e->pause(); }
void EffectComponent::resume() { paused_=false;for(auto* e:emitters_) e->resume();for(const auto& c:children())if(auto* e=dynamic_cast<EffectComponent*>(c.get()))e->resume(); }
void EffectComponent::stop(bool clear) { spawning_=false;paused_=false;for(auto* e:emitters_) e->stop(clear);for(const auto& c:children())if(auto* e=dynamic_cast<EffectComponent*>(c.get()))e->stop(clear);if(clear)clearSpawned(); }
void EffectComponent::setVisibilityMask(std::uint32_t mask) { for(auto* e:emitters_) e->visibilityMask=mask;for(const auto& c:children())if(auto* e=dynamic_cast<EffectComponent*>(c.get()))e->setVisibilityMask(mask);else if(auto* d=dynamic_cast<DecalComponent*>(c.get()))d->visibilityMask=mask; }
std::size_t EffectComponent::particleCount() const { std::size_t count=0;for(auto* e:emitters_) count+=e->particles().size();for(const auto& c:children())if(auto* e=dynamic_cast<EffectComponent*>(c.get());e&&!e->isPendingRemoval())count+=e->particleCount();return count; }
bool EffectComponent::playing() const { for(auto* e:emitters_) if(e->playing()) return true;return false; }
EffectComponent& EffectComponent::compositionRoot(){auto* root=this;for(auto* p=parent();p;p=p->parent())if(auto* e=dynamic_cast<EffectComponent*>(p))root=e;return *root;}
EffectCompositionStats EffectComponent::compositionStats() const {
    EffectCompositionStats s;s.spawned=totalSpawned_;s.dropped=totalDropped_;
    visit([&](const Node& n,const Mat4&){if(const auto* e=dynamic_cast<const ParticleEmitterComponent*>(&n))s.dropped+=e->droppedEvents();if(&n!=this){if(dynamic_cast<const EffectComponent*>(&n))++s.effects;if(dynamic_cast<const DecalComponent*>(&n))++s.decals;}});return s;
}
void EffectComponent::tick(double){frameSpawns_=0;if(spawned_)try{setWorldTransform(spawnPose_);}catch(const std::invalid_argument&){if(scene())destroy();}}
void EffectComponent::spawn(const EffectTrigger& t,const ParticleEvent& event){
    auto& root=compositionRoot();const auto& limit=root.asset_->limits;
    auto admit=[&](bool decal){
        if(root.frameSpawns_>=limit.maxSpawnsPerUpdate||depth_>=limit.maxDepth){++root.totalDropped_;return false;}
        const auto s=root.compositionStats();if(decal?s.decals>=limit.maxActiveDecals:s.effects>=limit.maxActiveEffects){++root.totalDropped_;return false;}return true;
    };
    Transform pose;pose.position=event.position;
    if(t.alignNormal){const auto n=normalized(event.normal);pose.rotation={std::asin(std::clamp(n.z,-1.f,1.f)),0,std::atan2(-n.x,n.y)};}
    auto publish=[&](std::unique_ptr<Component> candidate,bool decal){
        try{candidate->setWorldTransform(inverseAffine(worldMatrix())*candidate->transform.matrix());}
        catch(const std::invalid_argument&){++root.totalDropped_;return;}
        adoptChild(std::move(candidate));++root.frameSpawns_;++root.totalSpawned_;(void)decal;
    };
    if(t.effect&&admit(false)){auto child=std::make_unique<EffectComponent>(t.effect->name,t.effect);child->transform=pose;child->spawnPose_=pose;child->spawned_=true;child->depth_=depth_+1;child->setVisibilityMask(emitters_[t.emitter]->visibilityMask);for(auto* e:child->emitters_)e->inheritedVelocity_=event.velocity*t.inheritVelocity;publish(std::move(child),false);}
    if(t.decal&&admit(true)){auto child=std::make_unique<DecalComponent>("Triggered decal",t.decal->settings);child->transform=pose;child->transform.scale=t.decal->scale;child->spawnPose_=child->transform;child->effectSpawned_=true;child->visibilityMask=emitters_[t.emitter]->visibilityMask;publish(std::move(child),true);}
}
void EffectComponent::postTick(double){
    if(paused_)return;
    if(spawning_)for(std::size_t i=0;i<emitters_.size();++i)if(emitters_[i]->isActiveInHierarchy()&&emitters_[i]->tickEnabled())for(const auto& event:emitters_[i]->events())for(const auto& t:asset_->triggers)if(t.emitter==i&&t.event==event.type)spawn(t,event);
    if(spawned_&&!playing()&&particleCount()==0){bool busy=false;for(auto* e:emitters_)busy|=e->trailPointCount()>0;for(const auto& c:children())if(!c->isPendingRemoval()&&(dynamic_cast<EffectComponent*>(c.get())||dynamic_cast<DecalComponent*>(c.get())))busy=true;if(!busy&&scene())destroy();}
}
}
