#pragma once
#include "engine/math.hpp"
#include "engine/texture.hpp"
#include <memory>
#include <optional>

namespace engine {
enum class MaterialShader { Pbr, Unlit };
enum class AlphaMode { Opaque, Mask, Blend, Additive };
// A published asset is immutable; components customize it through MaterialInstance.
struct Material {
    std::string name;
    Vec3 baseColor{1,1,1};
    std::shared_ptr<const Texture> baseColorTexture;
    std::string texturePath;
    float opacity=1,roughness=0.6f,metallic=0,normalScale=1,occlusionStrength=1;
    Vec3 emissive{};
    std::shared_ptr<const Texture> metallicRoughnessTexture,normalTexture,occlusionTexture,emissiveTexture;
    AlphaMode alphaMode=AlphaMode::Opaque;
    float alphaCutoff=0.5f;
    bool doubleSided=false;
    MaterialShader shader=MaterialShader::Pbr;
    Vec2 uvScale{1,1},uvOffset{};
    void validate() const;
    Material parameters() const; // Metadata-free copy for allocation-free frame collection.
    static std::shared_ptr<const Material> standard();
};
struct MaterialInstance {
    MaterialInstance()=default;
    explicit MaterialInstance(std::shared_ptr<const Material> asset):material(std::move(asset)) {}
    std::shared_ptr<const Material> material;
    Vec3 tint{1,1,1};
    float opacity=1;
    Vec2 uvScale{1,1},uvOffset{};
    std::optional<float> roughness,metallic,normalScale,occlusionStrength,alphaCutoff;
    std::optional<Vec3> emissive;
    std::optional<AlphaMode> alphaMode;
    std::optional<bool> doubleSided;
    std::optional<MaterialShader> shader;
    // Engaged null overrides deliberately clear a map; disengaged overrides inherit.
    std::optional<std::shared_ptr<const Texture>> baseColorTexture,metallicRoughnessTexture,normalTexture,occlusionTexture,emissiveTexture;
    Material resolve(const std::shared_ptr<const Material>& fallback=Material::standard(),bool metadata=true) const;
};
// By-value effective binding. Names/import paths are excluded from GPU identity.
bool sameMaterialBinding(const Material& a,const Material& b);
} // namespace engine
