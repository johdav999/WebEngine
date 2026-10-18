#include "engine/asset_decode.hpp"
#include "engine/texture_bundle.hpp"
#include "engine/upload_budget.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <limits>
#include <iostream>

using namespace engine;
void put(std::vector<std::byte>& b,std::size_t p,std::uint64_t v,int n=4) {
    for(int i=0;i<n;++i)b[p+i]=std::byte((v>>(i*8))&255);
}
std::vector<std::byte> ktx(unsigned format=146) {
    std::vector<std::byte> b(272);const unsigned char magic[]={0xab,0x4b,0x54,0x58,0x20,0x32,0x30,0xbb,13,10,26,10};std::memcpy(b.data(),magic,12);
    put(b,12,format);put(b,16,1);put(b,20,4);put(b,24,4);put(b,36,1);put(b,40,3);put(b,48,152);put(b,52,format==152||format==151?60:44);
    put(b,152,format==152||format==151?60:44);put(b,160,2,2);put(b,162,format==152||format==151?56:40,2);put(b,164,format==146||format==145?134:format==152||format==151?161:162,1);put(b,168,0x0303);put(b,172,16,8);put(b,166,format%2==0?2:1,1);
    for(int i=0;i<3;++i){put(b,80+i*24,224+i*16,8);put(b,88+i*24,16,8);put(b,96+i*24,16,8);}
    return b;
}
std::vector<std::byte> tga() {
    std::vector<std::byte> b(18+64,std::byte{255});std::fill(b.begin(),b.begin()+18,std::byte{0});b[2]=std::byte{2};b[12]=b[14]=std::byte{4};b[16]=std::byte{32};b[17]=std::byte{40};return b;
}
template<class F>void rejects(F f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}assert(threw);}
std::vector<std::byte> file(const char* name) {
    std::ifstream in(std::string(FBX_FIXTURE_DIR)+"/"+name,std::ios::binary|std::ios::ate);assert(in);
    std::vector<std::byte> b(static_cast<std::size_t>(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char*>(b.data()),b.size());assert(in);return b;
}
int main() {
    UploadAdmission budget;budget.begin({100,2,3});assert(budget.admit(60,0));assert(!budget.admit(41,1));assert(budget.admit(40,1));assert(!budget.admit(1,1));
    budget.begin({100,1,1});assert(budget.admit(200,50));assert(!budget.admit(1,0)); // Oversize progress.
    budget.begin({0,0,1});assert(budget.admit(1,0));assert(!budget.admit(1,1));
    budget.begin({});for(int i=0;i<100;++i)assert(budget.admit(100,100));
    rejects([&]{budget.begin({0,0,std::numeric_limits<double>::quiet_NaN()});});
    for(auto format:{146u,152u,158u}) {
        const auto v=loadKtx2(ktx(format),TextureSemantic::Color);assert(v.levels.size()==3&&v.levels.back().width==1);
        assert(loadKtx2(ktx(format-1),TextureSemantic::Normal).semantic==TextureSemantic::Normal);
    }
    rejects([]{loadKtx2(ktx(),TextureSemantic::Normal);});
    for(auto offset:{12u,16u,20u,24u,28u,32u,36u,40u,44u,48u,52u,64u,72u,80u,88u,96u,104u,112u,120u,128u,136u,144u}) {
        auto b=ktx();put(b,offset,0xffffffff);rejects([&]{loadKtx2(b,TextureSemantic::Color);});
    }
    auto b=ktx();put(b,104,224,8);rejects([&]{loadKtx2(b,TextureSemantic::Color);}); // overlap
    b=ktx();b.resize(240);rejects([&]{loadKtx2(b,TextureSemantic::Color);});
    b=ktx();put(b,166,1,1);rejects([&]{loadKtx2(b,TextureSemantic::Color);});
    const auto fallback=tga();const std::vector<TextureVariantSource> variants={{TextureSemantic::Color,ktx()}};
    const auto bundle=packTextureBundle(fallback,variants);const auto texture=loadTextureBundle(bundle);
    assert(texture->width==4&&texture->pixels.size()==64&&texture->compressed.size()==1);
    assert(texture->mipChain(TextureSemantic::Color).size()==3);
    auto invalid=bundle;invalid.push_back(std::byte{0});rejects([&]{loadTextureBundle(invalid);});
    invalid=bundle;put(invalid,8,0xffffffff);rejects([&]{loadTextureBundle(invalid);});
    auto duplicates=variants;duplicates.push_back(variants[0]);rejects([&]{packTextureBundle(fallback,duplicates);});
    auto tight=TextureLimits{};tight.maxDecodedBytes=70;rejects([&]{loadTextureBundle(bundle,{},tight);});
    AssetManager manager;const AssetId id{"texture"};
    manager.registerAsset<Texture>(id,[&](AssetLoadContext context){
        const auto decoded=decodeAssetBytes(bundle,AssetDecodeKind::TextureBundle);completeDecodedAsset(decoded,AssetDecodeKind::TextureBundle,context);return AssetCancel{};
    });
    auto request=manager.load<Texture>(id);manager.update();manager.update();assert(request.get<Texture>()&&request.get<Texture>()->compressed.size()==1);
    for(const auto& pair:{std::pair{"skeletal_column_ascii.fbx",AssetDecodeKind::Fbx},std::pair{"skeletal_textured.glb",AssetDecodeKind::Glb}}) {
        const AssetId key{pair.first};const auto bytes=file(pair.first);const auto decoded=decodeAssetBytes(bytes,pair.second);
        manager.registerAsset<Mesh>(key,[&,decoded,kind=pair.second](AssetLoadContext context){completeDecodedAsset(decoded,kind,context);return AssetCancel{};});
        auto mesh=manager.load<Mesh>(key);manager.update();manager.update();assert(mesh.get<Mesh>()&&!mesh.get<Mesh>()->indices.empty());
    }
    rejects([&]{decodeAssetBytes({},AssetDecodeKind::Image);});
    auto limits=ArchiveLimits{};limits.maxBytes=16;rejects([&]{decodeAssetBytes(fallback,AssetDecodeKind::Image,limits);});
    std::cout<<"Mobile asset contracts passed\n";
}
