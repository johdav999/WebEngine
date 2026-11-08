#include "engine/material.hpp"

namespace engine {
void MaterialLayers::validate() const {
    auto uv=[](Vec2 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1000000&&std::abs(p.y)<=1000000;};
    if(!uv(weightScale)||!uv(weightOffset)) throw std::invalid_argument("Invalid layer weight UVs");
    auto texture=[](const auto& t) {if(t&&(!t->width||!t->height||t->width>4096||t->height>4096||t->pixels.size()!=std::uint64_t(t->width)*t->height*4)) throw std::invalid_argument("Invalid layer texture");};
    texture(weights);
    if(!std::isfinite(heightBlend)||heightBlend<0||heightBlend>1||!std::isfinite(triplanarScale)||triplanarScale<.0001f||triplanarScale>1000000||!std::isfinite(triplanarSharpness)||triplanarSharpness<1||triplanarSharpness>16)
        throw std::invalid_argument("Invalid layer projection/blending");
    for(const auto& layer:layers) {
        if(!uv(layer.uvScale)||!uv(layer.uvOffset)||!std::isfinite(layer.roughness)||layer.roughness<0||layer.roughness>1||
            !std::isfinite(layer.color.x)||!std::isfinite(layer.color.y)||!std::isfinite(layer.color.z)||layer.color.x<0||layer.color.y<0||layer.color.z<0)
            throw std::invalid_argument("Invalid material layer");
        texture(layer.texture);texture(layer.normalHeightTexture);
        if(!std::isfinite(layer.normalScale)||layer.normalScale<0||layer.normalScale>16||!std::isfinite(layer.heightScale)||layer.heightScale<0||layer.heightScale>16||!std::isfinite(layer.heightBias)||std::abs(layer.heightBias)>16)
            throw std::invalid_argument("Invalid layer normal/height parameters");
    }
}
std::array<float,56> materialLayerParameters(const MaterialLayers& s) {
    std::array<float,56> result{};
    for(std::size_t i=0;i<4;++i) {
        const auto& l=s.layers[i];const auto k=i*4;
        result[k]=l.color.x;result[k+1]=l.color.y;result[k+2]=l.color.z;result[k+3]=l.roughness;
        result[16+k]=l.uvScale.x;result[17+k]=l.uvScale.y;result[18+k]=l.uvOffset.x;result[19+k]=l.uvOffset.y;
    }
    result[32]=s.weightScale.x;result[33]=s.weightScale.y;result[34]=s.weightOffset.x;result[35]=s.weightOffset.y;
    result[36]=(s.enabled ? 1.f : 0.f)+(s.triplanar ? 2.f : 0.f);result[37]=s.heightBlend;result[38]=s.triplanarScale;result[39]=s.triplanarSharpness;
    for(std::size_t i=0;i<4;++i) {const auto& l=s.layers[i];const auto k=40+i*4;result[k]=l.normalScale;result[k+1]=l.heightScale;result[k+2]=l.heightBias;result[k+3]=l.normalHeightTexture ? 1.f : 0.f;}
    return result;
}
std::array<std::shared_ptr<const Texture>,9> materialTextures(const Material& m) {
    if(m.layers.enabled) {
        static const auto fallback=[] {auto t=std::make_shared<Texture>();t->name="Layer zero weights";t->width=t->height=1;t->wrapU=t->wrapV=TextureWrap::Clamp;t->pixels={255,0,0,0};return t;}();
        std::array<std::shared_ptr<const Texture>,9> result;
        for(std::size_t i=0;i<4;++i) result[i]=m.layers.layers[i].texture ? m.layers.layers[i].texture : Texture::white();
        result[4]=m.layers.weights ? m.layers.weights : fallback;
        for(std::size_t i=0;i<4;++i) result[5+i]=m.layers.layers[i].normalHeightTexture ? m.layers.layers[i].normalHeightTexture : Texture::flatNormal();return result;
    }
    return {m.baseColorTexture ? m.baseColorTexture : Texture::white(),m.metallicRoughnessTexture ? m.metallicRoughnessTexture : Texture::white(),
        m.normalTexture ? m.normalTexture : Texture::flatNormal(),m.occlusionTexture ? m.occlusionTexture : Texture::white(),m.emissiveTexture ? m.emissiveTexture : Texture::white(),m.advanced.surfaceTexture?m.advanced.surfaceTexture:Texture::white(),m.advanced.coatNormalTexture?m.advanced.coatNormalTexture:Texture::flatNormal(),
        m.advanced.colorTexture?m.advanced.colorTexture:Texture::white(),m.advanced.anisotropyTexture?m.advanced.anisotropyTexture:Texture::white()};
}
std::array<TextureSemantic,9> materialTextureSemantics(const Material& m) {
    return m.layers.enabled ? std::array{TextureSemantic::Color,TextureSemantic::Color,TextureSemantic::Color,TextureSemantic::Color,TextureSemantic::Data,TextureSemantic::Normal,TextureSemantic::Normal,TextureSemantic::Normal,TextureSemantic::Normal} :
        std::array{TextureSemantic::Color,TextureSemantic::Data,TextureSemantic::Normal,TextureSemantic::Data,TextureSemantic::Color,TextureSemantic::Data,TextureSemantic::Normal,TextureSemantic::Color,TextureSemantic::Data};
}
void MaterialAdvanced::validate() const {
    auto range=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    auto color=[&](Vec3 v){return range(v.x,0,1)&&range(v.y,0,1)&&range(v.z,0,1);};
    if(!range(clearcoat,0,1)||!range(clearcoatRoughness,0,1)||!range(transmission,0,1)||!range(ior,1,3)||
       !range(thickness,0,100)||!range(attenuationDistance,0,1000000)||!range(subsurface,0,1)||!range(sheen,0,1)||
       !color(attenuationColor)||!color(subsurfaceColor)||!color(sheenColor)||!range(sheenRoughness,0,1)||
       !range(anisotropy,-1,1)||!range(anisotropyRotation,-1000000,1000000)||!range(specular,0,1)||
       !range(uvSpeed.x,-100,100)||!range(uvSpeed.y,-100,100)||!range(windAmplitude,0,10)||
       !range(windFrequency,0,100)||!range(windSpeed,-100,100)||!range(clearcoatNormalScale,0,16)||
       !range(windDirection.x,-1000000,1000000)||!range(windDirection.y,-1000000,1000000)||!range(windDirection.z,-1000000,1000000)||
       (windAmplitude>0&&dot(windDirection,windDirection)<1e-12f))
        throw std::invalid_argument("Invalid advanced material parameters");
    for(const auto& texture:{surfaceTexture,coatNormalTexture,colorTexture,anisotropyTexture})
        if(texture&&(!texture->width||!texture->height||texture->width>4096||texture->height>4096||texture->pixels.size()!=std::uint64_t(texture->width)*texture->height*4))
            throw std::invalid_argument("Invalid advanced material texture");
    if(colorTexture&&(colorTexture->height<2||colorTexture->height%2))throw std::invalid_argument("Advanced color atlas needs two equal vertical bands");
}
std::array<float,36> materialAdvancedParameters(const MaterialAdvanced& a) {
    return {a.clearcoat,a.clearcoatRoughness,a.transmission,a.ior,
        a.thickness,a.attenuationDistance,a.subsurface,a.sheen,
        a.attenuationColor.x,a.attenuationColor.y,a.attenuationColor.z,a.sheenRoughness,
        a.subsurfaceColor.x,a.subsurfaceColor.y,a.subsurfaceColor.z,a.anisotropy,
        a.sheenColor.x,a.sheenColor.y,a.sheenColor.z,a.anisotropyRotation,
        a.specular,a.uvSpeed.x,a.uvSpeed.y,a.windAmplitude,
        a.windDirection.x,a.windDirection.y,a.windDirection.z,a.windFrequency,a.windSpeed,a.clearcoatNormalScale,a.colorTexture?float((a.colorTexture->wrapU==TextureWrap::Clamp?1:0)|(a.colorTexture->wrapV==TextureWrap::Clamp?2:0)):0.f,0,
        a.surfaceTexture?1.f:0.f,a.coatNormalTexture?1.f:0.f,a.colorTexture?1.f:0.f,a.anisotropyTexture?1.f:0.f};
}
void Material::validate() const {
    auto unit=[](float v) { return std::isfinite(v)&&v>=0&&v<=1; };
    auto color=[](Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&v.x>=0&&v.y>=0&&v.z>=0; };
    if(!color(baseColor)||!color(emissive)||!unit(opacity)||!unit(roughness)||!unit(metallic)
        ||!unit(occlusionStrength)||!unit(alphaCutoff)||!std::isfinite(normalScale)||normalScale<0
        ||!std::isfinite(uvScale.x)||!std::isfinite(uvScale.y)||!std::isfinite(uvOffset.x)||!std::isfinite(uvOffset.y)
        ||alphaMode<AlphaMode::Opaque||alphaMode>AlphaMode::Additive||shader<MaterialShader::Pbr||shader>MaterialShader::Water)
        throw std::invalid_argument("Invalid material parameters");
    water.validate();
    advanced.validate();
    if(layers.enabled&&(advanced.surfaceTexture||advanced.coatNormalTexture||advanced.colorTexture||advanced.anisotropyTexture))
        throw std::invalid_argument("Layered and advanced material maps use the same texture slots");
    layers.validate();
    if(layers.enabled&&shader==MaterialShader::Water) throw std::invalid_argument("Layer blending is unavailable for water");
    if(baseColorSurface && (shader!=MaterialShader::Unlit || alphaMode!=AlphaMode::Opaque || layers.enabled))
        throw std::invalid_argument("Render surface bindings require an opaque unlit material without layers");
}
std::shared_ptr<const Material> Material::standard() {
    static const auto value=std::make_shared<const Material>();return value;
}
Material Material::parameters() const {
    Material result;
#define COPY(field) result.field=field
    COPY(baseColor);COPY(baseColorTexture);COPY(baseColorSurface);COPY(opacity);COPY(roughness);COPY(metallic);COPY(normalScale);COPY(occlusionStrength);
    COPY(emissive);COPY(metallicRoughnessTexture);COPY(normalTexture);COPY(occlusionTexture);COPY(emissiveTexture);
    COPY(alphaMode);COPY(alphaCutoff);COPY(doubleSided);COPY(shader);COPY(water);COPY(layers);COPY(advanced);COPY(uvScale);COPY(uvOffset);
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
    OVERRIDE(emissive);OVERRIDE(alphaMode);OVERRIDE(doubleSided);OVERRIDE(shader);OVERRIDE(water);OVERRIDE(layers);OVERRIDE(advanced);
    OVERRIDE(baseColorTexture);OVERRIDE(metallicRoughnessTexture);OVERRIDE(normalTexture);OVERRIDE(occlusionTexture);OVERRIDE(emissiveTexture);
    if(baseColorSurface) result.baseColorSurface=baseColorSurface;
#undef OVERRIDE
    result.validate();return result;
}
bool sameMaterialBinding(const Material& a,const Material& b) {
    auto base=[](const Material& m) { return m.baseColorTexture ? m.baseColorTexture.get() : Texture::white().get(); };
    return base(a)==base(b)&&a.baseColorSurface==b.baseColorSurface&&a.metallicRoughnessTexture==b.metallicRoughnessTexture&&a.normalTexture==b.normalTexture
        &&a.occlusionTexture==b.occlusionTexture&&a.emissiveTexture==b.emissiveTexture
        &&a.roughness==b.roughness&&a.metallic==b.metallic&&a.normalScale==b.normalScale&&a.occlusionStrength==b.occlusionStrength
        &&a.emissive.x==b.emissive.x&&a.emissive.y==b.emissive.y&&a.emissive.z==b.emissive.z
        &&a.uvScale.x==b.uvScale.x&&a.uvScale.y==b.uvScale.y&&a.uvOffset.x==b.uvOffset.x&&a.uvOffset.y==b.uvOffset.y
        &&a.alphaMode==b.alphaMode&&a.alphaCutoff==b.alphaCutoff&&a.doubleSided==b.doubleSided&&a.shader==b.shader&&waterParameters(a.water)==waterParameters(b.water)
        &&materialAdvancedParameters(a.advanced)==materialAdvancedParameters(b.advanced)
        &&materialLayerParameters(a.layers)==materialLayerParameters(b.layers)&&materialTextures(a)==materialTextures(b);
}
} // namespace engine
