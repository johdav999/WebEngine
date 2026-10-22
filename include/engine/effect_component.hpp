#pragma once
#include "engine/particle_emitter_component.hpp"
#include "engine/decal.hpp"
#include <optional>

namespace engine {
struct EffectEmitter {
    std::shared_ptr<const ParticleEmitterDefinition> definition;
    Transform transform;
};
struct EffectAsset;
struct EffectDecal {
    DecalSettings settings=[] {DecalSettings s;s.lifetime=5;s.fadeOut=1;return s;}();
    Vec3 scale{.5f,.2f,.5f};
};
struct EffectTrigger {
    std::uint32_t emitter=0;
    ParticleEventType event=ParticleEventType::Impact;
    std::shared_ptr<const EffectAsset> effect;
    std::optional<EffectDecal> decal;
    bool alignNormal=true;
    float inheritVelocity=0;
};
struct EffectLimits {
    std::uint32_t maxActiveEffects=64,maxActiveDecals=128,maxSpawnsPerUpdate=64,maxDepth=4,maxEventsPerEmitter=256;
    void validate() const;
};
struct EffectCompositionStats {std::uint32_t effects=0,decals=0;std::uint64_t spawned=0,dropped=0;};
struct EffectAsset {
    std::string name="Effect";
    std::vector<EffectEmitter> emitters;
    std::vector<EffectTrigger> triggers;
    EffectLimits limits;
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
    bool paused() const{return paused_;}
    EffectCompositionStats compositionStats() const;
protected:
    void tick(double seconds) override;
    void postTick(double seconds) override;
private:
    friend class ArchiveCodec;
    std::shared_ptr<const EffectAsset> asset_;
    std::vector<ParticleEmitterComponent*> emitters_;
    bool paused_=false,spawning_=true,spawned_=false;
    std::uint32_t depth_=0,frameSpawns_=0;
    std::uint64_t totalSpawned_=0,totalDropped_=0;
    Transform spawnPose_;
    EffectComponent& compositionRoot();
    void clearSpawned();
    void spawn(const EffectTrigger&,const ParticleEvent&);
};
}
