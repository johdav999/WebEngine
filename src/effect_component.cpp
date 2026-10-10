#include "engine/effect_component.hpp"
namespace engine {
void EffectAsset::validate() const {
    if(emitters.empty()||emitters.size()>64) throw std::invalid_argument("Effect assets require 1 to 64 emitters");
    for(const auto& emitter:emitters) {
        if(!emitter.definition) throw std::invalid_argument("Effect emitter is missing its definition");
        emitter.definition->validate();
        for(auto v:emitter.transform.matrix().m) if(!std::isfinite(v)) throw std::invalid_argument("Invalid effect emitter transform");
    }
}
EffectComponent::EffectComponent(std::string name,std::shared_ptr<const EffectAsset> asset)
    : Component(std::move(name)),asset_(std::move(asset)) {
    if(!asset_) throw std::invalid_argument("Missing effect asset");
    asset_->validate();emitters_.reserve(asset_->emitters.size());
    for(const auto& entry:asset_->emitters) {
        auto& emitter=addChild<ParticleEmitterComponent>(entry.definition->name,entry.definition);
        emitter.transform=entry.transform;protectChild(emitter);emitters_.push_back(&emitter);
    }
}
void EffectComponent::play(bool restart) { for(auto* e:emitters_) e->play(restart); }
void EffectComponent::pause() { for(auto* e:emitters_) e->pause(); }
void EffectComponent::resume() { for(auto* e:emitters_) e->resume(); }
void EffectComponent::stop(bool clear) { for(auto* e:emitters_) e->stop(clear); }
void EffectComponent::setVisibilityMask(std::uint32_t mask) { for(auto* e:emitters_) e->visibilityMask=mask; }
std::size_t EffectComponent::particleCount() const { std::size_t count=0;for(auto* e:emitters_) count+=e->particles().size();return count; }
bool EffectComponent::playing() const { for(auto* e:emitters_) if(e->playing()) return true;return false; }
}
