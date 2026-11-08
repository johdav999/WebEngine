#pragma once
#include "engine/math.hpp"
#include "engine/render_surface.hpp"
#include "engine/texture.hpp"
#include "engine/water.hpp"
#include <memory>
#include <optional>

namespace engine {
enum class MaterialShader { Pbr, Unlit, Water };
enum class AlphaMode { Opaque, Mask, Blend, Additive };
// Four tiled albedo/roughness layers. RGBA weights are linear data, normalized
// per fragment; an all-zero texel selects layer zero. Weights use mesh UVs; tiles optionally use world planes.
struct MaterialLayer {
    std::shared_ptr<const Texture> texture;
    Vec3 color{1,1,1};
    float roughness=1;
    Vec2 uvScale{1,1},uvOffset{};
    // Linear RGB tangent normal (OpenGL +Y), alpha height. Optional; no displacement.
    std::shared_ptr<const Texture> normalHeightTexture;
    float normalScale=1,heightScale=1,heightBias=0;
};
struct MaterialLayers {
    bool enabled=false;
    std::array<MaterialLayer,4> layers;
    std::shared_ptr<const Texture> weights;
    Vec2 weightScale{1,1},weightOffset{};
    float heightBlend=0; // Zero preserves linear weight blending; positive transition width <=1.
    bool triplanar=false;
    float triplanarScale=1,triplanarSharpness=4; // World units^-1; axis blend exponent [1,16].
    void validate() const;
};
// Portable advanced surface approximations. Neutral defaults retain ordinary PBR.
// Effects use the host-controlled renderer clock and never modify simulation data.
struct MaterialAdvanced {
    float clearcoat=0,clearcoatRoughness=.1f,transmission=0,ior=1.5f;
    float thickness=0,attenuationDistance=0,subsurface=0,sheen=0;
    Vec3 attenuationColor{1,1,1};
    float sheenRoughness=.5f;
    Vec3 subsurfaceColor{1,1,1};
    float anisotropy=0;
    Vec3 sheenColor{1,1,1};
    float anisotropyRotation=0,specular=.5f;
    Vec2 uvSpeed{};
    Vec3 windDirection{1,0,0};
    float windAmplitude=0,windFrequency=1,windSpeed=1,clearcoatNormalScale=1;
    // Packed map contract: surface RGBA=coat/coat roughness/transmission/thickness;
    // color top half=sheen RGB/roughness A, bottom=subsurface RGB/strength A.
    // Anisotropy RG=encoded direction, B=strength, A=specular strength.
    std::shared_ptr<const Texture> surfaceTexture,coatNormalTexture,colorTexture,anisotropyTexture;
    void validate() const;
    bool animated() const {return uvSpeed.x!=0||uvSpeed.y!=0||windAmplitude>0;}
    bool usesSceneInputs() const {return transmission>0;}
};
std::array<float,36> materialAdvancedParameters(const MaterialAdvanced& material);
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
    WaterSettings water;
    MaterialLayers layers;
    MaterialAdvanced advanced;
    Vec2 uvScale{1,1},uvOffset{};
    void validate() const;
    Material parameters() const; // Metadata-free copy for allocation-free frame collection.
    static std::shared_ptr<const Material> standard();
    RenderSurfaceHandle baseColorSurface; // Runtime only; baseColorTexture supplies the fallback.
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
    std::optional<WaterSettings> water;
    std::optional<MaterialLayers> layers;
    std::optional<MaterialAdvanced> advanced;
    // Engaged null overrides deliberately clear a map; disengaged overrides inherit.
    std::optional<std::shared_ptr<const Texture>> baseColorTexture,metallicRoughnessTexture,normalTexture,occlusionTexture,emissiveTexture;
    RenderSurfaceHandle baseColorSurface; // Runtime instance override; empty inherits the asset binding.
    Material resolve(const std::shared_ptr<const Material>& fallback=Material::standard(),bool metadata=true) const;
};
// By-value effective binding. Names/import paths are excluded from GPU identity.
bool sameMaterialBinding(const Material& a,const Material& b);
std::array<float,56> materialLayerParameters(const MaterialLayers& layers);
// Five conventional slots plus four optional packed layer normal/height maps.
std::array<std::shared_ptr<const Texture>,9> materialTextures(const Material& material);
std::array<TextureSemantic,9> materialTextureSemantics(const Material& material);
} // namespace engine
