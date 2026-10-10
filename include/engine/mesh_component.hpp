#pragma once
#include "engine/component.hpp"
#include "engine/mesh.hpp"
#include "engine/fbx_importer.hpp"
#include "engine/glb_importer.hpp"
#include "engine/render_layer.hpp"
#include "engine/blend_mode.hpp"

namespace engine {
class MeshComponent : public Component {
public:
    MeshComponent(std::string name, std::shared_ptr<const Mesh> mesh);
    ~MeshComponent() override;
    std::string_view typeName() const noexcept override { return "MeshComponent"; }
    void loadGlb(std::span<const std::byte> data,const GlbImportOptions& options={});
    void loadFbx(std::span<const std::byte> data,const FbxImportOptions& options={});
#ifndef __EMSCRIPTEN__
    void loadGlbFile(const std::filesystem::path& path,const GlbImportOptions& options={});
    void loadFbxFile(const std::filesystem::path& path,const FbxImportOptions& options={});
#endif
    std::shared_ptr<const Mesh> mesh; // CPU/GPU mesh resources are shared across instances.
    std::shared_ptr<const Texture> texture; // Optional base-color override for every material.
    void loadTexture(std::span<const std::byte> bytes,const TextureLimits& limits={});
    Vec3 color{0.3f,0.65f,0.9f};
    std::vector<std::optional<MaterialInstance>> materialOverrides;
    // Slot zero also applies to primitive meshes without explicit material ranges.
    void setMaterial(std::size_t slot,MaterialInstance instance);
    void clearMaterial(std::size_t slot);
    Material resolvedMaterial(std::size_t slot,bool metadata=true) const;
    BlendMode blend=BlendMode::Opaque;
    float opacity=1; // Alpha/Additive meshes depth-test, without writing depth.
    RenderLayer layer=RenderLayer::World;
    std::uint32_t visibilityMask=1; // Object groups, independent of ordered render layers.
    bool castsShadow=true,receivesShadow=true; // World layer only; blended meshes do not cast.
    bool frustumCulling=true; // Opt out for custom vertex deformation without CPU bounds.
    bool allowInstanceReordering=false; // Runtime batching hint: opaque parts may change equal-depth precedence.
    // Weights are independent per component; finite negative/extrapolated weights are allowed.
    void setMorphWeight(std::size_t target,float weight);
    void setMorphWeight(std::string_view target,float weight);
    float morphWeight(std::size_t target) const;
    void resetMorphWeights(); // Restore asset defaults. Mesh replacement also resets weights.
    std::shared_ptr<const MorphPose> morphPose() const; // Null when all weights are zero.
    MeshBounds localBounds(const std::shared_ptr<const SkinPose>& pose={}) const;
private:
    void synchronizeMorphs() const;
    mutable std::weak_ptr<const Mesh> morphMesh_;
    mutable std::vector<float> morphWeights_;
    mutable std::shared_ptr<MorphPose> morphPose_;
    mutable bool morphDirty_=true;
    mutable std::weak_ptr<const Mesh> boundsMesh_;
    mutable std::weak_ptr<const SkinPose> boundsPose_;
    mutable std::weak_ptr<const MorphPose> boundsMorph_;
    mutable std::uint64_t boundsRevision_=0;
    mutable std::uint64_t boundsMorphRevision_=0;
    mutable MeshBounds animatedBounds_;
};
} // namespace engine
