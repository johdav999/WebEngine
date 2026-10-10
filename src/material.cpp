#include "engine/material.hpp"

namespace engine {
void Material::validate() const {
    auto unit=[](float v) { return std::isfinite(v)&&v>=0&&v<=1; };
    auto color=[](Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&v.x>=0&&v.y>=0&&v.z>=0; };
    if(!color(baseColor)||!color(emissive)||!unit(opacity)||!unit(roughness)||!unit(metallic)
        ||!unit(occlusionStrength)||!unit(alphaCutoff)||!std::isfinite(normalScale)||normalScale<0
        ||!std::isfinite(uvScale.x)||!std::isfinite(uvScale.y)||!std::isfinite(uvOffset.x)||!std::isfinite(uvOffset.y)
        ||alphaMode<AlphaMode::Opaque||alphaMode>AlphaMode::Additive||shader<MaterialShader::Pbr||shader>MaterialShader::Unlit)
        throw std::invalid_argument("Invalid material parameters");
}
std::shared_ptr<const Material> Material::standard() {
    static const auto value=std::make_shared<const Material>();return value;
}
Material Material::parameters() const {
    Material result;
#define COPY(field) result.field=field
    COPY(baseColor);COPY(baseColorTexture);COPY(opacity);COPY(roughness);COPY(metallic);COPY(normalScale);COPY(occlusionStrength);
    COPY(emissive);COPY(metallicRoughnessTexture);COPY(normalTexture);COPY(occlusionTexture);COPY(emissiveTexture);
    COPY(alphaMode);COPY(alphaCutoff);COPY(doubleSided);COPY(shader);COPY(uvScale);COPY(uvOffset);
#undef COPY
    return result;
}
Material MaterialInstance::resolve(const std::shared_ptr<const Material>& fallback,bool metadata) const {
    const auto source=material ? material : fallback ? fallback : Material::standard();
    Material result=metadata ? *source : source->parameters();
    result.baseColor={result.baseColor.x*tint.x,result.baseColor.y*tint.y,result.baseColor.z*tint.z};
    result.opacity*=opacity;
    if(!std::isfinite(tint.x)||!std::isfinite(tint.y)||!std::isfinite(tint.z)||tint.x<0||tint.y<0||tint.z<0)
        throw std::invalid_argument("Invalid material instance tint");
    result.uvOffset={result.uvOffset.x+uvOffset.x*result.uvScale.x,result.uvOffset.y+uvOffset.y*result.uvScale.y};
    result.uvScale={result.uvScale.x*uvScale.x,result.uvScale.y*uvScale.y};
    if(!std::isfinite(opacity)||opacity<0||opacity>1) throw std::invalid_argument("Invalid material instance opacity");
#define OVERRIDE(field) if(field) result.field=*field
    OVERRIDE(roughness);OVERRIDE(metallic);OVERRIDE(normalScale);OVERRIDE(occlusionStrength);OVERRIDE(alphaCutoff);
    OVERRIDE(emissive);OVERRIDE(alphaMode);OVERRIDE(doubleSided);OVERRIDE(shader);
    OVERRIDE(baseColorTexture);OVERRIDE(metallicRoughnessTexture);OVERRIDE(normalTexture);OVERRIDE(occlusionTexture);OVERRIDE(emissiveTexture);
#undef OVERRIDE
    result.validate();return result;
}
bool sameMaterialBinding(const Material& a,const Material& b) {
    auto base=[](const Material& m) { return m.baseColorTexture ? m.baseColorTexture.get() : Texture::white().get(); };
    return base(a)==base(b)&&a.metallicRoughnessTexture==b.metallicRoughnessTexture&&a.normalTexture==b.normalTexture
        &&a.occlusionTexture==b.occlusionTexture&&a.emissiveTexture==b.emissiveTexture
        &&a.roughness==b.roughness&&a.metallic==b.metallic&&a.normalScale==b.normalScale&&a.occlusionStrength==b.occlusionStrength
        &&a.emissive.x==b.emissive.x&&a.emissive.y==b.emissive.y&&a.emissive.z==b.emissive.z
        &&a.uvScale.x==b.uvScale.x&&a.uvScale.y==b.uvScale.y&&a.uvOffset.x==b.uvOffset.x&&a.uvOffset.y==b.uvOffset.y
        &&a.alphaMode==b.alphaMode&&a.alphaCutoff==b.alphaCutoff&&a.doubleSided==b.doubleSided&&a.shader==b.shader;
}
} // namespace engine
