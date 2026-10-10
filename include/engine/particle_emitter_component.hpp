#pragma once
#include "engine/component.hpp"
#include "engine/texture.hpp"
#include "engine/blend_mode.hpp"
#include <span>

namespace engine {
struct Color4 { float r=1,g=1,b=1,a=1; };
struct SizeKey { float time=0,value=1; };
struct ColorKey { float time=0; Color4 value; };
struct ParticleBurst { double time=0; std::uint32_t count=1; };
enum class ParticleSpace { Local, World };
struct Flipbook {
    std::uint32_t columns=1,rows=1,frames=1;
    float cycles=1; // One playback over a particle's lifetime; >1 repeats.
    void validate(const Texture& texture) const;
    std::uint32_t frame(float normalizedAge) const;
};
struct ParticleEmitterDefinition {
    std::string name="Emitter";
    std::shared_ptr<const Texture> texture=Texture::white();
    BlendMode blend=BlendMode::Alpha;
    ParticleSpace space=ParticleSpace::Local;
    std::uint32_t maxParticles=512,seed=1;
    double spawnRate=20,duration=1;
    bool looping=true;
    float lifetimeMin=1,lifetimeMax=2;
    Vec3 positionMin{},positionMax{},velocityMin{0,1,0},velocityMax{0,2,0},gravity{};
    float rotationMin=0,rotationMax=0,angularVelocity=0;
    std::vector<ParticleBurst> bursts;
    std::vector<SizeKey> size{{0,0.2f},{1,0.5f}};
    std::vector<ColorKey> color{{0,{1,1,1,1}},{1,{1,1,1,0}}};
    Flipbook flipbook;
    void validate() const;
    float sizeAt(float age) const;
    Color4 colorAt(float age) const;
};
// Pooled CPU records, independent of scene nodes and GPU allocations.
struct Particle {
    Vec3 position,velocity;
    float age=0,lifetime=1,rotation=0,sizeScale=1;
};
struct ParticleDraw {
    Vec3 position; float size=1;
    Color4 color;
    float normalizedAge=0,rotation=0;
    Flipbook flipbook;
    std::shared_ptr<const Texture> texture;
    BlendMode blend=BlendMode::Alpha;
    std::uint32_t visibilityMask=1;
};
class ParticleEmitterComponent : public Component {
public:
    explicit ParticleEmitterComponent(std::string name="Emitter",
        std::shared_ptr<const ParticleEmitterDefinition> definition=std::make_shared<ParticleEmitterDefinition>());
    std::string_view typeName() const noexcept override { return "ParticleEmitterComponent"; }
    void setDefinition(std::shared_ptr<const ParticleEmitterDefinition> definition); // Validated before replacing/resetting.
    const ParticleEmitterDefinition& definition() const { return *definition_; }
    void play(bool restart=true);
    void pause() { paused_=true; }
    void resume() { paused_=false; }
    void stop(bool clear=false); // Otherwise living particles drain normally.
    bool playing() const { return emitting_ && !paused_; }
    bool paused() const { return paused_; }
    std::span<const Particle> particles() const { return particles_; }
    std::uint64_t droppedParticles() const { return dropped_; }
    std::uint32_t visibilityMask=1;
    void appendDraws(std::vector<ParticleDraw>& output,const Mat4& world) const;
protected:
    void tick(double deltaSeconds) override;
private:
    friend class ArchiveCodec;
    std::shared_ptr<const ParticleEmitterDefinition> definition_;
    std::vector<Particle> particles_;
    std::vector<double> deaths_; // Reused min-heap for exact capacity at subframe spawn times.
    double time_=0,nextRate_=0;
    std::uint64_t burstCycle_=0,dropped_=0;
    std::size_t burstIndex_=0;
    std::uint32_t random_=1;
    bool emitting_=true,paused_=false;
    float randomUnit();
    void spawn(double event,double end);
    void advance(double seconds);
    double nextBurst() const;
    void skipEvents(double end);
    void addDropped(long double count);
};
}
