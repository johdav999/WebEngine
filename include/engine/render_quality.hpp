#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace engine {
struct PostProcessing {
    bool enabled=false;
    float exposure=0, bloomStrength=.15f, bloomThreshold=1, saturation=1, contrast=1;
    void validate() const {
        if(!std::isfinite(exposure)||exposure< -8||exposure>8||!std::isfinite(bloomStrength)||bloomStrength<0||bloomStrength>2||
           !std::isfinite(bloomThreshold)||bloomThreshold<0||bloomThreshold>32||!std::isfinite(saturation)||saturation<0||saturation>2||
           !std::isfinite(contrast)||contrast<0||contrast>2) throw std::invalid_argument("Invalid post-processing settings");
    }
};
enum class QualityProfile { Low, Balanced, High };
enum class RenderBackend { WebGpu, WebGl2 };
enum class RenderBackendPreference { Automatic, WebGpu, WebGl2 };
struct RenderCapabilities {
    std::uint32_t maxTextureDimension2D=4096, maxTextureArrayLayers=256;
    std::uint64_t maxBufferSize=0;
    bool timestampQuery=false, compressionBC=false, compressionETC2=false, compressionASTC=false;
    RenderBackend backend=RenderBackend::WebGpu;
    bool shadowMaps=true, pbr=true, instancing=true;
    std::uint32_t maxLights=8; // First enabled masked lights per view, shared across types.
    bool spotLights=true,spotLightShadows=true, hdrPostProcessing=false;
    bool msaa4x=false; // Actual color/depth attachment support; false before acquisition.
};
struct ResolutionBudget {
    float scale=1, maxPixelRatio=2;
    std::uint32_t maxDimension=4096;
    std::uint64_t maxPixels=16777216;
    void validate() const {
        if(!std::isfinite(scale)||scale<0.25f||scale>2||!std::isfinite(maxPixelRatio)||maxPixelRatio<0.5f||maxPixelRatio>4||
           !maxDimension||maxDimension>16384||!maxPixels||maxPixels>268435456)
            throw std::invalid_argument("Invalid rendering resolution budget");
    }
};
struct RenderQuality {
    PostProcessing post;
    std::uint32_t directionalCascades=1; // 1 preserves authored fixed volumes; 2..4 fit camera slices.
    float shadowDistance=80, cascadeLambda=.6f;
    ResolutionBudget resolution;
    std::uint32_t shadowResolution=1024, shadowFilterTaps=9, maxShadowMaps=4;
    bool lod=true, shadowLod=true, compressedTextures=true;
    float detailScale=1; // Projected LOD pixels/texels multiplier; .25..1, presentation only.
    std::uint32_t msaaSamples=1; // Presentation only: 1 (off) or 4.
    void validate() const {
        post.validate();
        if(directionalCascades<1||directionalCascades>4||!std::isfinite(shadowDistance)||shadowDistance<=0||
           !std::isfinite(cascadeLambda)||cascadeLambda<0||cascadeLambda>1) throw std::invalid_argument("Invalid shadow cascades");
        resolution.validate();
        if(!std::isfinite(detailScale)||detailScale<.25f||detailScale>1)throw std::invalid_argument("Invalid rendering detail scale");
        if(msaaSamples!=1&&msaaSamples!=4)
            throw std::invalid_argument("MSAA requires 1 or 4 samples");
        if((shadowResolution!=256&&shadowResolution!=512&&shadowResolution!=1024&&shadowResolution!=2048)||
           (shadowFilterTaps!=1&&shadowFilterTaps!=9)||maxShadowMaps>4)
            throw std::invalid_argument("Invalid rendering shadow budget");
    }
    static RenderQuality profile(QualityProfile profile) {
        RenderQuality q;
        switch(profile) {
        case QualityProfile::Low: q.resolution={0.75f,1,2048,2073600};q.shadowResolution=512;q.shadowFilterTaps=1;q.maxShadowMaps=1;break;
        case QualityProfile::Balanced: break;
        case QualityProfile::High: q.resolution={1,2,8192,33554432};q.shadowResolution=2048;break;
        default: throw std::invalid_argument("Unknown rendering quality profile");
        }
        return q;
    }
};
struct RenderExtent { std::uint32_t width=1,height=1; };
inline std::uint32_t resolveMsaaSamples(const RenderQuality& quality,const RenderCapabilities& caps) {
    quality.validate();
    return quality.msaaSamples==4&&caps.msaa4x ? 4u : 1u;
}
// Shrink both axes together, preserving the view's aspect under all budgets.
inline RenderExtent resolveResolution(double cssWidth,double cssHeight,double pixelRatio,
                                      const ResolutionBudget& budget,const RenderCapabilities& caps) {
    budget.validate();
    if(!std::isfinite(cssWidth)||!std::isfinite(cssHeight)||cssWidth<=0||cssHeight<=0||!caps.maxTextureDimension2D) return {};
    if(!std::isfinite(pixelRatio)||pixelRatio<=0) pixelRatio=1;
    double w=cssWidth,h=cssHeight;
    const double factor=std::min(pixelRatio,double(budget.maxPixelRatio))*budget.scale;
    const double dimension=std::min(budget.maxDimension,caps.maxTextureDimension2D);
    const double ratio=std::min({factor,dimension/w,dimension/h,std::sqrt(double(budget.maxPixels)/w/h)});
    return {std::uint32_t(std::max(1.0,std::floor(w*ratio))),std::uint32_t(std::max(1.0,std::floor(h*ratio)))};
}
} // namespace engine
