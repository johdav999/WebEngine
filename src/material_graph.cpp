#include "engine/material_graph.hpp"
#include <bit>

namespace engine {
Material bindMaterialGraph(const Material& source,std::span<const float> v,
    std::span<const std::uint8_t> pixels,std::uint32_t size,std::uint32_t mask) {
    if(v.size()!=19||mask>31||(size!=64&&size!=128&&size!=256)
        ||pixels.size()!=std::size_t(size)*size*4*std::popcount(mask))
        throw std::invalid_argument("Invalid compiled material graph payload");
    for(float value:v) if(!std::isfinite(value))throw std::invalid_argument("Non-finite graph parameter");
    if(v[11]<0||v[11]>3||v[11]!=std::floor(v[11])||v[14]<0||v[14]>1||v[14]!=std::floor(v[14])||(v[13]!=0&&v[13]!=1))
        throw std::invalid_argument("Invalid graph material mode");
    Material result=source;
    result.baseColor={v[0],v[1],v[2]};result.roughness=v[3];result.metallic=v[4];result.normalScale=v[5];result.occlusionStrength=v[6];
    result.emissive={v[7],v[8],v[9]};result.opacity=v[10];result.alphaMode=AlphaMode(int(v[11]));result.alphaCutoff=v[12];
    result.doubleSided=v[13]!=0;result.shader=MaterialShader(int(v[14]));result.uvScale={v[15],v[16]};result.uvOffset={v[17],v[18]};
    result.validate();
    std::shared_ptr<const Texture>* maps[]={&result.baseColorTexture,&result.metallicRoughnessTexture,&result.normalTexture,&result.occlusionTexture,&result.emissiveTexture};
    const std::size_t bytes=std::size_t(size)*size*4;std::size_t offset=0;
    for(unsigned slot=0;slot<5;++slot) if(mask&(1u<<slot)) {
        auto texture=std::make_shared<Texture>();texture->name="Material graph map";texture->width=size;texture->height=size;
        texture->pixels.assign(pixels.begin()+offset,pixels.begin()+offset+bytes);*maps[slot]=std::move(texture);offset+=bytes;
    }
    return result;
}
}
