#include "engine/performance_timeline.hpp"
#include "engine/texture.hpp"
#include "engine/math.hpp"
#include <stb_image.h>
#include <limits>
#include <stdexcept>

namespace engine {
void CompressedTexture::validate(std::uint32_t width,std::uint32_t height) const {
    if(!width||!height||width>4096||height>4096||width%4||height%4||levels.empty()||
       (format!=TextureCompression::BC7&&format!=TextureCompression::ETC2RGBA&&format!=TextureCompression::ASTC4x4)||
       (semantic!=TextureSemantic::Color&&semantic!=TextureSemantic::Data&&semantic!=TextureSemantic::Normal))
        throw std::invalid_argument("Invalid compressed texture variant");
    bool complete=false;
    for(const auto& level:levels) {
        const auto bytes=std::uint64_t((width+3)/4)*((height+3)/4)*16;
        if(complete||level.width!=width||level.height!=height||level.pixels.size()!=bytes)
            throw std::invalid_argument("Invalid compressed mip dimensions or block payload");
        complete=width==1&&height==1;
        width=std::max(width/2,1u);height=std::max(height/2,1u);
    }
    if(!complete) throw std::invalid_argument("Compressed texture requires a complete mip chain");
}
std::shared_ptr<const Texture> Texture::loadMemory(std::span<const std::byte> bytes,std::string name,const TextureLimits& limits) {
    ProfileScope profile(ProfileCategory::Loading,"Image decode",name);
    if(!limits.maxFileBytes || !limits.maxDimension || !limits.maxDecodedBytes)
        throw std::invalid_argument("Invalid texture limits");
    if(bytes.empty() || bytes.size()>limits.maxFileBytes || bytes.size()>std::size_t(std::numeric_limits<int>::max()))
        throw std::runtime_error("Texture is empty or exceeds the file size limit: "+name);
    const auto* input=reinterpret_cast<const stbi_uc*>(bytes.data());
    int width=0,height=0,channels=0;
    if(!stbi_info_from_memory(input,static_cast<int>(bytes.size()),&width,&height,&channels))
        throw std::runtime_error("Unsupported or invalid texture (PNG, JPEG, TGA, BMP): "+name);
    const auto size=std::uint64_t(width)*std::uint64_t(height)*4;
    if(width<=0 || height<=0 || std::uint32_t(width)>limits.maxDimension || std::uint32_t(height)>limits.maxDimension || size>limits.maxDecodedBytes)
        throw std::runtime_error("Texture dimensions or decoded size exceed the limit: "+name);
    std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(input,static_cast<int>(bytes.size()),&width,&height,&channels,4),stbi_image_free);
    if(!decoded) throw std::runtime_error("Cannot decode texture: "+name);
    auto texture=std::make_shared<Texture>();
    texture->name=std::move(name); texture->width=width; texture->height=height;
    texture->pixels.assign(decoded.get(),decoded.get()+size);
    return texture;
}
std::shared_ptr<const Texture> Texture::white() {
    static const auto value=[] { auto t=std::make_shared<Texture>();t->name="White fallback";t->width=t->height=1;t->pixels={255,255,255,255};return t; }();
    return value;
}
std::shared_ptr<const Texture> Texture::flatNormal() {
    static const auto value=[] { auto t=std::make_shared<Texture>();t->name="Flat normal";t->width=t->height=1;t->pixels={128,128,255,255};return t; }();return value;
}
std::vector<TextureMip> Texture::mipChain(TextureSemantic semantic) const {
    if(!width||!height||width>4096||height>4096||pixels.size()!=std::uint64_t(width)*height*4) throw std::invalid_argument("Invalid texture pixels");
    std::vector<TextureMip> levels;levels.push_back({width,height,pixels});
    auto linear=[](float v) { return v<=0.04045f ? v/12.92f : std::pow((v+0.055f)/1.055f,2.4f); };
    auto srgb=[](float v) { return v<=0.0031308f ? v*12.92f : 1.055f*std::pow(v,1/2.4f)-0.055f; };
    while(levels.back().width>1||levels.back().height>1) {
        const auto& source=levels.back();TextureMip next{std::max(source.width/2,1u),std::max(source.height/2,1u),{}};
        next.pixels.resize(std::size_t(next.width)*next.height*4);
        for(std::uint32_t y=0;y<next.height;++y) for(std::uint32_t x=0;x<next.width;++x) {
            std::array<float,4> sum{};float weight=0;
            const float x0=float(x)*source.width/next.width,x1=float(x+1)*source.width/next.width;
            const float y0=float(y)*source.height/next.height,y1=float(y+1)*source.height/next.height;
            for(auto sy=std::uint32_t(y0);sy<std::uint32_t(std::ceil(y1));++sy) for(auto sx=std::uint32_t(x0);sx<std::uint32_t(std::ceil(x1));++sx) {
                const float w=(std::min(float(sx+1),x1)-std::max(float(sx),x0))*(std::min(float(sy+1),y1)-std::max(float(sy),y0));weight+=w;
                for(int c=0;c<4;++c) { float v=source.pixels[(std::size_t(sy)*source.width+sx)*4+c]/255.0f;if(c<3&&semantic==TextureSemantic::Color) v=linear(v);sum[c]+=v*w; }
            }
            for(auto& value:sum) value/=weight;
            if(semantic==TextureSemantic::Normal) {
                auto n=normalized(Vec3{sum[0]*2-1,sum[1]*2-1,sum[2]*2-1});if(dot(n,n)<0.5f) n={0,0,1};
                sum[0]=n.x*0.5f+0.5f;sum[1]=n.y*0.5f+0.5f;sum[2]=n.z*0.5f+0.5f;
            }
            for(int c=0;c<4;++c) { auto v=sum[c];if(c<3&&semantic==TextureSemantic::Color) v=srgb(v);next.pixels[(std::size_t(y)*next.width+x)*4+c]=std::uint8_t(std::round(std::clamp(v,0.0f,1.0f)*255)); }
        }
        levels.push_back(std::move(next));
    }
    return levels;
}
} // namespace engine
