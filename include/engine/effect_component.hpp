#pragma once
#include "engine/particle_emitter_component.hpp"

namespace engine {
struct EffectEmitter {
    std::shared_ptr<const ParticleEmitterDefinition> definition;
    Transform transform;
};
struct EffectAsset {
    std::string name="Effect";
    std::vector<EffectEmitter> emitters;
    void validate() const;
};
class EffectComponent : public Component {
public:
    EffectComponent(std::string name,std::shared_ptr<const EffectAsset> asset);
    std::string_view typeName() const noexcept override { return "EffectComponent"; }
    const EffectAsset& asset() const { return *asset_; }
    std::span<ParticleEmitterComponent* const> emitters() const { return emitters_; }
    void play(bool restart=true);
    void pause();
    void resume();
    void stop(bool clear=false);
    void setVisibilityMask(std::uint32_t mask);
    std::size_t particleCount() const;
    bool playing() const;
private:
    friend class ArchiveCodec;
    std::shared_ptr<const EffectAsset> asset_;
    std::vector<ParticleEmitterComponent*> emitters_;
};
}
