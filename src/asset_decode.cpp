#include "engine/asset_decode.hpp"
#include "engine/fbx_importer.hpp"
#include "engine/glb_importer.hpp"
#include "engine/texture_bundle.hpp"
#include <cstring>
#include <stdexcept>
#include <limits>

namespace engine {
namespace {
void put(std::vector<std::byte>& b,std::uint32_t v) { for(int i=0;i<4;++i) b.push_back(std::byte((v>>(i*8))&255)); }
std::uint32_t get(std::span<const std::byte> b,std::size_t p) {
    if(p>b.size()||b.size()-p<4) throw std::invalid_argument("Truncated decode transport");
    std::uint32_t v=0;for(int i=0;i<4;++i) v|=std::uint32_t(std::to_integer<unsigned char>(b[p+i]))<<(i*8);return v;
}
}
std::vector<std::byte> decodeAssetBytes(std::span<const std::byte> b,AssetDecodeKind kind,const ArchiveLimits& limits) {
    AssetLibrary assets;
    switch(kind) {
    case AssetDecodeKind::Fbx: assets.meshes.push_back(FbxImporter::loadMemory(b));break;
    case AssetDecodeKind::Glb: assets.meshes.push_back(GlbImporter::loadMemory(b));break;
    case AssetDecodeKind::Image: assets.textures.push_back(Texture::loadMemory(b));break;
    case AssetDecodeKind::TextureBundle: assets.textures.push_back(loadTextureBundle(b));break;
    default: throw std::invalid_argument("Unknown asset decode kind");
    }
    auto archive=SceneArchive::saveAssets(assets,limits);
    if(archive.size()>std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("Decoded archive exceeds transport range");
    std::vector<std::byte> result;put(result,0x31434457);put(result,static_cast<std::uint32_t>(archive.size()));
    // KTX source payloads are copied after the archive because archives omit variants.
    const auto variants=kind==AssetDecodeKind::TextureBundle ? get(b,12) : 0;put(result,variants);
    result.insert(result.end(),archive.begin(),archive.end());
    if(variants) {
        const auto p=16+get(b,8);result.insert(result.end(),b.begin()+p,b.end());
    }
    if(result.size()>limits.maxBytes) throw std::invalid_argument("Decoded transport exceeds limit");
    return result;
}
void completeDecodedAsset(std::span<const std::byte> b,AssetDecodeKind kind,const AssetLoadContext& context,const ArchiveLimits& limits) {
    if(b.size()>limits.maxBytes||get(b,0)!=0x31434457) throw std::invalid_argument("Invalid decode transport");
    const auto size=get(b,4),count=get(b,8);if(size>b.size()-12||count>9) throw std::invalid_argument("Invalid decode transport size");
    auto assets=SceneArchive::loadAssets(b.subspan(12,size),limits);std::size_t p=12+size;
    if(kind==AssetDecodeKind::Fbx||kind==AssetDecodeKind::Glb) {
        if(count||p!=b.size()||assets.meshes.size()!=1) throw std::invalid_argument("Invalid mesh decode result");
        context.complete<Mesh>(assets.meshes.front());return;
    }
    if((kind!=AssetDecodeKind::Image&&kind!=AssetDecodeKind::TextureBundle)||assets.textures.size()!=1)
        throw std::invalid_argument("Invalid texture decode result");
    if(kind==AssetDecodeKind::Image&&count) throw std::invalid_argument("Unexpected image variants");
    auto texture=std::make_shared<Texture>(*assets.textures.front());
    std::size_t retained=texture->pixels.size();TextureLimits textureLimits;
    for(std::uint32_t i=0;i<count;++i) {
        const auto semantic=get(b,p),length=get(b,p+4);p+=8;
        if(length>b.size()-p) throw std::invalid_argument("Invalid variant decode size");
        auto variant=loadKtx2(b.subspan(p,length),static_cast<TextureSemantic>(semantic));variant.validate(texture->width,texture->height);
        for(const auto& old:texture->compressed) if(old.format==variant.format&&old.semantic==variant.semantic) throw std::invalid_argument("Duplicate decoded variant");
        for(const auto& mip:variant.levels) {
            if(retained>textureLimits.maxDecodedBytes||mip.pixels.size()>textureLimits.maxDecodedBytes-retained) throw std::invalid_argument("Decoded texture exceeds limit");
            retained+=mip.pixels.size();
        }
        texture->compressed.push_back(std::move(variant));p+=length;
    }
    if(p!=b.size()) throw std::invalid_argument("Trailing decode data");
    context.complete<Texture>(std::move(texture));
}
}
