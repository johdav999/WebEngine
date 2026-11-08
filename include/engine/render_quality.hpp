#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace engine {
enum class ToneMapper { Reinhard, PbrNeutral };
struct PostProcessing {
    bool enabled=false;
    float exposure=0, bloomStrength=.15f, bloomThreshold=1, saturation=1, contrast=1;
    ToneMapper toneMapper=ToneMapper::Reinhard;
    void validate() const {
        if(!std::isfinite(exposure)||exposure< -8||exposure>8||!std::isfinite(bloomStrength)||bloomStrength<0||bloomStrength>2||
           !std::isfinite(bloomThreshold)||bloomThreshold<0||bloomThreshold>32||!std::isfinite(saturation)||saturation<0||saturation>2||
           !std::isfinite(contrast)||contrast<0||contrast>2||
           (toneMapper!=ToneMapper::Reinhard&&toneMapper!=ToneMapper::PbrNeutral))
            throw std::invalid_argument("Invalid post-processing settings");
    }
};
enum class QualityProfile { Low, Balanced, High };
enum class RenderBackend { WebGpu, WebGl2, Direct3D12 };
enum class RenderBackendPreference { Automatic, WebGpu, WebGl2 };
struct RenderCapabilities {
    std::uint32_t maxTextureDimension2D=4096, maxTextureArrayLayers=256;
    std::uint64_t maxBufferSize=0;
    bool timestampQuery=false, compressionBC=false, compressionETC2=false, compressionASTC=false;
    RenderBackend backend=RenderBackend::WebGpu;
    bool shadowMaps=true, pbr=true, instancing=true;
    std::uint32_t maxLights=8; // Shared across light types per view.
    bool spotLights=true,spotLightShadows=true, hdrPostProcessing=false;
    bool msaa4x=false; // Actual color/depth attachment support; false before acquisition.
    bool compactPbr=false; // Optional reduced WebGL2 metallic/roughness and baked environment tier.
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
    bool gpuClusterCulling=false; // WebGPU/Dawn World color with a single-sample depth attachment.
    bool gpuClusterHierarchy=false; // GPU camera LOD cut and streaming feedback when GPU submission is enabled.
    float detailScale=1; // Projected LOD pixels/texels multiplier; .25..1, presentation only.
    std::uint32_t msaaSamples=1; // Presentation only: 1 (off) or 4.
    std::uint32_t volumetricFogSteps=16; // Zero selects analytic fog.
    std::uint32_t volumetricCloudSteps=0; // Zero keeps legacy 2D sky clouds; maximum 64.
    bool volumetricCloudDepth=true,volumetricCloudShadows=false;
    float volumetricCloudShadowDistance=8000,volumetricCloudShadowStrength=.8f;
    bool volumetricFogGrid=true; // Reuse per-view fog integration when supported.
    bool volumetricFogSelfShadows=false; // Attenuate selected spotlights through active fog boxes.
    bool volumetricFogLightCache=false; // Reuse spotlight optical depth in a bounded light-space cache.
    bool webGlPbr=false; // Opt in to the WebGL2 compact PBR path where available.
    bool screenSpaceReflections=false; // Perspective World receivers; requires effective HDR.
    bool volumetricFogSurfaceShadows=false; // Attenuate direct World surface lighting through fog.
    bool volumetricFogSoftShadows=false; // Filter geometric visibility for volume samples.
    std::uint32_t volumetricFogMaxLights=1;
    bool rankedLights=false; // Opt in to view-dependent selection with authored light priorities.
    std::uint32_t localShadowReserve=0; // Maximum maps to reserve for selected shadowed spotlights.
    void validate() const {
        post.validate();
        if(volumetricFogSteps>32) throw std::invalid_argument("Invalid volumetric fog sample budget");
        if(volumetricCloudSteps>64) throw std::invalid_argument("Invalid volumetric cloud sample budget");
        if(!std::isfinite(volumetricCloudShadowDistance)||volumetricCloudShadowDistance<128||volumetricCloudShadowDistance>100000||
           !std::isfinite(volumetricCloudShadowStrength)||volumetricCloudShadowStrength<0||volumetricCloudShadowStrength>1)
            throw std::invalid_argument("Invalid volumetric cloud shadow policy");
        if(volumetricFogMaxLights<1||volumetricFogMaxLights>4) throw std::invalid_argument("Invalid volumetric fog light budget");
        if(directionalCascades<1||directionalCascades>4||!std::isfinite(shadowDistance)||shadowDistance<=0||
           !std::isfinite(cascadeLambda)||cascadeLambda<0||cascadeLambda>1) throw std::invalid_argument("Invalid shadow cascades");
        resolution.validate();
        if(!std::isfinite(detailScale)||detailScale<.25f||detailScale>1)throw std::invalid_argument("Invalid rendering detail scale");
        if(msaaSamples!=1&&msaaSamples!=4)
            throw std::invalid_argument("MSAA requires 1 or 4 samples");
        if((shadowResolution!=256&&shadowResolution!=512&&shadowResolution!=1024&&shadowResolution!=2048)||
           (shadowFilterTaps!=1&&shadowFilterTaps!=9)||maxShadowMaps>4||localShadowReserve>4)
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
