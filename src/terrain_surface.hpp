#pragma once
#include "engine/material.hpp"

namespace engine::terrain_surface {
inline void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
inline MaterialLayer singleLayer(const Material& m){
    m.validate();require(m.alphaMode==AlphaMode::Opaque&&m.opacity==1&&m.shader!=MaterialShader::Water&&m.metallic==0&&dot(m.emissive,m.emissive)==0,
        "Terrain seam requires opaque nonmetallic PBR/unlit surfaces");
    require(!m.baseColorTexture&&!m.normalTexture&&!m.metallicRoughnessTexture&&!m.emissiveTexture&&!m.occlusionTexture,
        "Terrain seam requires solid color or triplanar layer maps, not conventional UV maps");
    MaterialLayer layer;
    if(m.layers.enabled){
        require(m.layers.triplanar&&!m.layers.weights&&m.layers.heightBlend==0,"Terrain seam requires an unweighted single triplanar layer");
        layer=m.layers.layers[0];
        layer.uvOffset={m.uvOffset.x*layer.uvScale.x+layer.uvOffset.x,m.uvOffset.y*layer.uvScale.y+layer.uvOffset.y};
        layer.uvScale={layer.uvScale.x*m.uvScale.x*m.layers.triplanarScale,layer.uvScale.y*m.uvScale.y*m.layers.triplanarScale};
    }
    layer.color={layer.color.x*m.baseColor.x,layer.color.y*m.baseColor.y,layer.color.z*m.baseColor.z};
    layer.roughness*=m.roughness;return layer;
}
inline std::shared_ptr<const Material> blend(const Material& ground,const Material& rock){
    const auto a=singleLayer(ground),b=singleLayer(rock);
    require(ground.shader==rock.shader,"Terrain seam shader families must match");
    if(ground.layers.enabled&&rock.layers.enabled)require(ground.layers.triplanarSharpness==rock.layers.triplanarSharpness,"Terrain seam triplanar sharpness must match");
    auto weights=std::make_shared<Texture>();weights->name="Terrain seam blend";weights->width=256;weights->height=1;
    weights->wrapU=weights->wrapV=TextureWrap::Clamp;
    for(unsigned i=0;i<256;++i)weights->pixels.insert(weights->pixels.end(),{std::uint8_t(i),std::uint8_t(255-i),0,0});
    auto m=std::make_shared<Material>();m->name="Terrain seam / "+rock.name;m->shader=rock.shader;m->doubleSided=rock.doubleSided;
    m->roughness=1;m->layers.enabled=m->layers.triplanar=true;m->layers.triplanarScale=1;
    m->layers.triplanarSharpness=ground.layers.enabled?ground.layers.triplanarSharpness:rock.layers.triplanarSharpness;
    m->layers.layers[0]=a;m->layers.layers[1]=b;m->layers.weights=weights;m->validate();return m;
}
} // namespace engine::terrain_surface
