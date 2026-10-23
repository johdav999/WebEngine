#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace engine {
struct MetricSummary { double mean=0,p95=0,max=0; };
MetricSummary summarize(std::span<const double> samples);
struct FrameTiming {
    double intervalMs=0,updateMs=0,collectMs=0,prepareMs=0,encodeMs=0,submitMs=0,cpuMs=0,resourceMs=0;
};
struct FrameCounters {
    std::uint64_t culledMeshes=0,visibleMeshes=0;
    std::uint64_t guiDrawCalls=0,guiTriangles=0,guiUploadCalls=0,guiUploadBytes=0,guiTextureBytes=0,guiBufferBytes=0;
    std::uint64_t trailSegments=0,trailDrawCalls=0,trailUploadBytes=0;
    std::uint64_t meshParticles=0; // Draws/uploads use existing mesh counters.
    std::uint64_t particles=0,particleDrawCalls=0,transparentMeshDraws=0,particleUploadCalls=0,particleUploadBytes=0,particleBufferGrowths=0;
    std::uint64_t meshes=0,lights=0,passes=0,drawCalls=0,triangles=0;
    std::uint64_t shadowPasses=0,shadowDrawCalls=0,shadowInstances=0;
    std::uint64_t shadowTriangles=0,cameraLodParts=0,shadowLodParts=0,shadowCulledParts=0;
    std::uint32_t shadowMapResolution=1024,shadowFilterTaps=9;
    std::uint64_t submittedInstances=0,instancedDrawCalls=0,maxInstancesPerDraw=0;
    std::uint64_t skinnedInstances=0,cachedSkeletons=0,gpuSkinBytes=0,boneUploadCalls=0,boneUploadBytes=0;
    std::uint64_t assetUploadBytes=0,assetUploadResources=0,deferredAssetUploads=0,deferredAssetDraws=0;
    std::uint64_t uploadCalls=0,uploadBytes=0,newMeshes=0,newTextures=0;
    std::uint64_t cachedMaterials=0,cachedMaterialPipelines=0,materialUploadCalls=0,materialUploadBytes=0;
    std::uint64_t frameStorageBytes=0,frameStorageGrowths=0,objectBufferGrowths=0;
    std::uint64_t changedObjects=0,objectUploadCalls=0,objectUploadBytes=0,frameUploadCalls=0,frameUploadBytes=0;
    std::uint64_t localTransformBuilds=0,worldTransformBuilds=0,normalTransformBuilds=0;
    std::uint64_t cachedMeshes=0,cachedTextures=0,cpuAssetBytes=0,gpuMeshBytes=0,gpuTextureBytes=0,gpuBufferBytes=0,gpuDepthBytes=0;
    std::uint32_t width=0,height=0;
};
// Fixed rolling windows. Collection does not allocate or wait for the GPU.
class PerformanceMonitor {
public:
    static constexpr std::size_t capacity=120;
    void recordFrame(const FrameTiming& timing,const FrameCounters& counters);
    void recordGpu(const std::array<double,3>& layerMs,double completedAtMs,double shadowMs=0);
    void reset();
    std::string json(double nowMs,const char* gpuState) const;
    std::string gpuJson(double nowMs,const char* gpuState) const;
private:
    std::array<FrameTiming,capacity> frames_{};
    std::array<std::array<double,3>,capacity> gpu_{};
    std::array<double,capacity> shadowGpu_{};
    FrameCounters latest_{};
    std::size_t frameCount_=0,gpuCount_=0;
    std::uint64_t totalFrames_=0,totalGpuSamples_=0,totalUploadBytes_=0,totalUploadCalls_=0,epoch_=0;
    double lastGpuAt_=0;
    double totalResourceMs_=0,lastResourceMs_=0;
    std::uint64_t totalFrameStorageGrowths_=0,totalObjectBufferGrowths_=0;
    std::uint64_t totalObjectUploadBytes_=0;
};
} // namespace engine
