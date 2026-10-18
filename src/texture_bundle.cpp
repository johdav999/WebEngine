#include "engine/texture_bundle.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace engine {
namespace {
void require(bool value) { if(!value) throw std::invalid_argument("Invalid or unsupported texture bundle/KTX2"); }
std::uint64_t read(std::span<const std::byte> b,std::size_t p,std::size_t n=4) {
    require(p<=b.size()&&n<=b.size()-p);std::uint64_t v=0;
    for(std::size_t i=0;i<n;++i) v|=std::uint64_t(std::to_integer<unsigned char>(b[p+i]))<<(i*8);
    return v;
}
void write(std::vector<std::byte>& b,std::uint64_t v,std::size_t n=4) {
    for(std::size_t i=0;i<n;++i) b.push_back(std::byte((v>>(i*8))&255));
}
void limitsValid(const TextureLimits& l) { require(l.maxFileBytes&&l.maxDimension&&l.maxDecodedBytes); }
constexpr std::array<unsigned char,12> ktxMagic={0xab,0x4b,0x54,0x58,0x20,0x32,0x30,0xbb,0x0d,0x0a,0x1a,0x0a};
constexpr char bundleMagic[]="WETEX001";
}
CompressedTexture loadKtx2(std::span<const std::byte> b,TextureSemantic semantic,const TextureLimits& limits) {
    limitsValid(limits);require(b.size()>=80&&b.size()<=limits.maxFileBytes);
    require(std::memcmp(b.data(),ktxMagic.data(),12)==0);
    const auto format=read(b,12),width=read(b,20),height=read(b,24),count=read(b,40);
    require(read(b,16)==1&&width&&height&&width<=limits.maxDimension&&height<=limits.maxDimension&&
        !read(b,28)&&!read(b,32)&&read(b,36)==1&&count&&count<=13&&!read(b,44)&&!read(b,64,8)&&!read(b,72,8));
    CompressedTexture result;result.semantic=semantic;
    const bool color=semantic==TextureSemantic::Color;
    require(semantic==TextureSemantic::Color||semantic==TextureSemantic::Data||semantic==TextureSemantic::Normal);
    if(format==(color?146u:145u)) result.format=TextureCompression::BC7;
    else if(format==(color?152u:151u)) result.format=TextureCompression::ETC2RGBA;
    else if(format==(color?158u:157u)) result.format=TextureCompression::ASTC4x4;
    else require(false);
    const auto indexEnd=80+count*24;
    const auto dfd=read(b,48),dfdSize=read(b,52),kvd=read(b,56),kvdSize=read(b,60);
    require(dfd>=indexEnd&&dfd%4==0&&dfdSize>=28&&dfd<=b.size()&&dfdSize<=b.size()-dfd);
    require(read(b,dfd)==dfdSize&&read(b,dfd+8,2)==2&&read(b,dfd+10,2)>=40&&read(b,dfd+10,2)==dfdSize-4&&(dfdSize-28)%16==0);
    require(!read(b,dfd+4)&&!read(b,dfd+15,1)&&read(b,dfd+16)==0x0303&&read(b,dfd+20,8)==16);
    const auto model=result.format==TextureCompression::BC7?134u:result.format==TextureCompression::ETC2RGBA?161u:162u;
    require(read(b,dfd+12,1)==model);
    require(read(b,dfd+14,1)==(color?2u:1u)); // sRGB versus linear DFD transfer.
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges={{0,indexEnd},{dfd,dfd+dfdSize}};
    if(kvdSize) {
        require(kvd>=indexEnd&&kvd%4==0&&kvd<=b.size()&&kvdSize<=b.size()-kvd);
        ranges.emplace_back(kvd,kvd+kvdSize);
        for(auto p=kvd;p<kvd+kvdSize;) {
            require(kvd+kvdSize-p>=4);const auto n=read(b,p);p+=4;require(n&&n<=kvd+kvdSize-p);
            auto entry=b.subspan(p,n);auto zero=std::find(entry.begin(),entry.end(),std::byte{0});require(zero!=entry.end());
            const std::string key(reinterpret_cast<const char*>(entry.data()),zero-entry.begin());
            const auto value=entry.subspan(zero-entry.begin()+1);
            if(key=="KTXorientation") require((value.size()==2||(value.size()==3&&value[2]==std::byte{0}))&&value[0]==std::byte{'r'}&&value[1]==std::byte{'d'});
            if(key=="KTXswizzle") require((value.size()==4||(value.size()==5&&value[4]==std::byte{0}))&&std::memcmp(value.data(),"rgba",4)==0);
            p+=(n+3)&~std::uint64_t(3);require(p<=kvd+kvdSize);
        }
    } else require(!kvd);
    std::uint32_t w=width,h=height;std::uint64_t retained=0;
    for(std::uint32_t i=0;i<count;++i) {
        const auto p=80+i*24;const auto offset=read(b,p,8),size=read(b,p+8,8);
        const auto expected=std::uint64_t((w+3)/4)*((h+3)/4)*16;
        require(offset%16==0&&offset>=indexEnd&&offset<=b.size()&&size<=b.size()-offset&&size==expected&&read(b,p+16,8)==size);
        retained+=size;require(retained<=limits.maxDecodedBytes);
        ranges.emplace_back(offset,offset+size);
        TextureMip mip{w,h,{}};mip.pixels.resize(size);std::memcpy(mip.pixels.data(),b.data()+offset,size);
        result.levels.push_back(std::move(mip));w=std::max(w/2,1u);h=std::max(h/2,1u);
    }
    std::sort(ranges.begin(),ranges.end());
    for(std::size_t i=1;i<ranges.size();++i) require(ranges[i-1].second<=ranges[i].first);
    result.validate(width,height);return result;
}
std::shared_ptr<const Texture> loadTextureBundle(std::span<const std::byte> b,std::string name,const TextureLimits& limits) {
    limitsValid(limits);require(b.size()>=16&&b.size()<=limits.maxFileBytes&&std::memcmp(b.data(),bundleMagic,8)==0);
    const auto fallbackSize=read(b,8),count=read(b,12);require(count<=9&&fallbackSize&&fallbackSize<=b.size()-16);
    auto fallback=Texture::loadMemory(b.subspan(16,fallbackSize),std::move(name),limits);
    auto texture=std::make_shared<Texture>(*fallback);std::size_t p=16+fallbackSize,retained=texture->pixels.size();
    for(std::uint32_t i=0;i<count;++i) {
        const auto semantic=read(b,p),size=read(b,p+4);p+=8;require(size<=b.size()-p);
        auto variant=loadKtx2(b.subspan(p,size),static_cast<TextureSemantic>(semantic),limits);
        variant.validate(texture->width,texture->height);
        for(const auto& old:texture->compressed) require(old.format!=variant.format||old.semantic!=variant.semantic);
        for(const auto& mip:variant.levels) { require(mip.pixels.size()<=limits.maxDecodedBytes-retained);retained+=mip.pixels.size(); }
        texture->compressed.push_back(std::move(variant));p+=size;
    }
    require(p==b.size());return texture;
}
std::vector<std::byte> packTextureBundle(std::span<const std::byte> fallback,std::span<const TextureVariantSource> variants,const TextureLimits& limits) {
    limitsValid(limits);require(fallback.size()<=limits.maxFileBytes&&variants.size()<=9);
    std::vector<std::byte> out;for(std::size_t i=0;i<8;++i) out.push_back(std::byte(bundleMagic[i]));
    write(out,fallback.size());write(out,variants.size());out.insert(out.end(),fallback.begin(),fallback.end());
    for(const auto& v:variants) {
        require(out.size()<=limits.maxFileBytes&&8<=limits.maxFileBytes-out.size());
        require(v.ktx2.size()<=limits.maxFileBytes-out.size()-8);
        write(out,std::uint32_t(v.semantic));write(out,v.ktx2.size());out.insert(out.end(),v.ktx2.begin(),v.ktx2.end());
    }
    loadTextureBundle(out,{},limits);return out;
}
}
