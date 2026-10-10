#pragma once
#include "engine/scene.hpp"

namespace engine {
struct alignas(16) ParticleInstance {
    std::array<float,4> positionSize,color,ageRotation,atlas;
};
static_assert(sizeof(ParticleInstance)==64);
struct FxBatch {
    bool particle=false;
    std::uint32_t first=0,count=1; // Particle instance range, or one MeshDraw index.
    BlendMode blend=BlendMode::Alpha;
    std::shared_ptr<const Texture> texture;
};
// Camera-specific order: opaque comes from RenderFrame; this queue mixes smoke and
// transparent meshes back-to-front, then additive FX. Never regroup across alpha order.
class FxFrame {
public:
    void prepare(const SceneFrame& scene,const Mat4& view);
    std::span<const ParticleInstance> instances() const { return particles_; }
    std::span<const FxBatch> batches(RenderLayer layer) const { return batches_.at(static_cast<std::size_t>(layer)); }
    std::size_t storageBytes() const;
    std::size_t storageGrowths() const { return growths_; }
private:
    struct Entry { bool particle;std::uint32_t index,order;RenderLayer layer;BlendMode blend;float depth; };
    std::vector<Entry> entries_;
    std::vector<ParticleInstance> particles_;
    std::array<std::vector<FxBatch>,static_cast<std::size_t>(RenderLayer::Count)> batches_;
    std::size_t growths_=0;
};
}
