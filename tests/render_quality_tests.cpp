#include "engine/render_quality.hpp"
#include "engine/texture.hpp"
#include <cassert>
#include <limits>

using namespace engine;
template<class F> void rejects(F f) { bool threw=false;try { f(); } catch(const std::invalid_argument&) { threw=true; }assert(threw); }
int main() {
    RenderCapabilities caps;
    const auto low=RenderQuality::profile(QualityProfile::Low), balanced=RenderQuality::profile(QualityProfile::Balanced);
    const auto high=RenderQuality::profile(QualityProfile::High);
    for(const auto& q:{low,balanced,high}) q.validate();
    assert(low.maxShadowMaps==1&&low.shadowFilterTaps==1&&high.shadowResolution==2048);
    auto extent=resolveResolution(1000,500,3,balanced.resolution,caps);
    assert(extent.width==2000&&extent.height==1000);
    extent=resolveResolution(1000,500,3,low.resolution,caps);assert(extent.width==750&&extent.height==375);
    auto budget=high.resolution;budget.maxPixels=1000000;
    extent=resolveResolution(4000,2000,2,budget,caps);
    assert(std::uint64_t(extent.width)*extent.height<=budget.maxPixels&&extent.width<=caps.maxTextureDimension2D);
    assert(std::abs(double(extent.width)/extent.height-2)<0.005);
    caps.maxTextureDimension2D=512;extent=resolveResolution(2000,1000,4,high.resolution,caps);
    assert(extent.width==512&&extent.height==256);
    extent=resolveResolution(1000,500,0,balanced.resolution,RenderCapabilities{});assert(extent.width==1000);
    assert(resolveResolution(0,20,1,budget,caps).width==1);
    assert(resolveResolution(std::numeric_limits<double>::infinity(),20,1,budget,caps).height==1);
    for(float value:{0.f,-1.f,3.f,std::numeric_limits<float>::quiet_NaN()}) {
        auto invalid=balanced;invalid.resolution.scale=value;rejects([&]{invalid.validate();});
    }
    auto invalid=balanced;invalid.maxShadowMaps=5;rejects([&]{invalid.validate();});
    invalid=balanced;invalid.shadowResolution=123;rejects([&]{invalid.validate();});
    invalid=balanced;invalid.resolution.maxPixels=0;rejects([&]{invalid.validate();});
    rejects([]{RenderQuality::profile(static_cast<QualityProfile>(42));});
    for(auto format:{TextureCompression::BC7,TextureCompression::ETC2RGBA,TextureCompression::ASTC4x4}) {
        CompressedTexture variant;variant.format=format;
        for(auto size:{8u,4u,2u,1u}) variant.levels.push_back({size,size,std::vector<std::uint8_t>(((size+3)/4)*((size+3)/4)*16)});
        variant.validate(8,8);
        auto broken=variant;broken.levels.pop_back();rejects([&]{broken.validate(8,8);});
        broken=variant;broken.levels[1].pixels.pop_back();rejects([&]{broken.validate(8,8);});
        broken=variant;broken.levels[2].width=3;rejects([&]{broken.validate(8,8);});
        broken=variant;broken.levels.push_back(variant.levels.back());rejects([&]{broken.validate(8,8);});
        rejects([&]{variant.validate(7,8);});
    }
    // Adding a variant preserves semantic RGBA fallback and ordinary imports.
    Texture texture;texture.width=texture.height=4;texture.pixels.resize(64,255);
    const auto before=texture.mipChain(TextureSemantic::Color);
    texture.compressed.push_back({TextureCompression::BC7,TextureSemantic::Color,{{4,4,std::vector<std::uint8_t>(16)}, {2,2,std::vector<std::uint8_t>(16)}, {1,1,std::vector<std::uint8_t>(16)}}});
    texture.compressed.front().validate(4,4);
    assert(texture.mipChain(TextureSemantic::Color).back().pixels==before.back().pixels);
}
