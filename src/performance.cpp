#include "engine/performance.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace engine {
MetricSummary summarize(std::span<const double> samples) {
    if(samples.empty()) return {};
    if(samples.size()>PerformanceMonitor::capacity) throw std::invalid_argument("Performance window is too large");
    std::array<double,PerformanceMonitor::capacity> values{}; double sum=0;
    for(std::size_t i=0;i<samples.size();++i) {
        if(!std::isfinite(samples[i]) || samples[i]<0) throw std::invalid_argument("Invalid performance timing");
        values[i]=samples[i]; sum+=samples[i];
    }
    std::sort(values.begin(),values.begin()+samples.size());
    return {sum/samples.size(),values[static_cast<std::size_t>(std::ceil(samples.size()*0.95))-1],values[samples.size()-1]};
}
void PerformanceMonitor::recordFrame(const FrameTiming& timing,const FrameCounters& counters) {
    frames_[totalFrames_%capacity]=timing; ++totalFrames_;
    frameCount_=std::min(frameCount_+1,capacity); latest_=counters;
    totalUploadBytes_+=counters.uploadBytes; totalUploadCalls_+=counters.uploadCalls;
    totalFrameStorageGrowths_+=counters.frameStorageGrowths;totalObjectBufferGrowths_+=counters.objectBufferGrowths;
    totalObjectUploadBytes_+=counters.objectUploadBytes;
    totalResourceMs_+=timing.resourceMs;
    if(counters.newMeshes || counters.newTextures) lastResourceMs_=timing.resourceMs;
}
void PerformanceMonitor::recordGpu(const std::array<double,3>& layerMs,double completedAtMs,double shadowMs) {
    for(const auto value:layerMs) if(!std::isfinite(value) || value<0) return;
    if(!std::isfinite(shadowMs)||shadowMs<0) return;
    shadowGpu_[totalGpuSamples_%capacity]=shadowMs;
    gpu_[totalGpuSamples_%capacity]=layerMs; ++totalGpuSamples_;
    gpuCount_=std::min(gpuCount_+1,capacity); lastGpuAt_=completedAtMs;
}
void PerformanceMonitor::reset() {
    frameCount_=gpuCount_=0; totalFrames_=totalGpuSamples_=totalUploadBytes_=totalUploadCalls_=0;
    latest_={}; lastGpuAt_=0; ++epoch_;
    totalResourceMs_=lastResourceMs_=0;
    totalFrameStorageGrowths_=totalObjectBufferGrowths_=0;
    totalObjectUploadBytes_=0;
}
std::string PerformanceMonitor::json(double nowMs,const char* gpuState) const {
    std::ostringstream out; out.imbue(std::locale::classic()); out<<std::fixed<<std::setprecision(6);
    auto metric=[&](std::span<const double> data) {
        const auto s=summarize(data);out<<"{\"mean\":"<<s.mean<<",\"p95\":"<<s.p95<<",\"max\":"<<s.max<<"}";
    };
    out<<"{\"schemaVersion\":1,\"epoch\":"<<epoch_<<",\"windowCapacity\":"<<capacity<<",\"windowSamples\":"<<frameCount_
        <<",\"totalFrames\":"<<totalFrames_<<",\"totalUploadBytes\":"<<totalUploadBytes_<<",\"totalUploadCalls\":"<<totalUploadCalls_<<",\"totalResourceMs\":"<<totalResourceMs_<<",\"lastResourceMs\":"<<lastResourceMs_<<",\"totalFrameStorageGrowths\":"<<totalFrameStorageGrowths_<<",\"totalObjectBufferGrowths\":"<<totalObjectBufferGrowths_<<",\"totalObjectUploadBytes\":"<<totalObjectUploadBytes_<<",\"timings\":{";
    constexpr const char* names[]={"intervalMs","updateMs","collectMs","prepareMs","encodeMs","submitMs","cpuMs","resourceMs"};
    constexpr std::array members={&FrameTiming::intervalMs,&FrameTiming::updateMs,&FrameTiming::collectMs,&FrameTiming::prepareMs,&FrameTiming::encodeMs,&FrameTiming::submitMs,&FrameTiming::cpuMs,&FrameTiming::resourceMs};
    std::array<double,capacity> values{};
    for(std::size_t m=0;m<members.size();++m) {
        if(m) out<<',';
        out<<'"'<<names[m]<<"\":";
        std::size_t count=0;
        for(std::size_t i=0;i<frameCount_;++i) {
            const auto value=frames_[i].*members[m];
            if(m==0 && value<=0) continue; // First frame / visibility resume has no valid interval.
            values[count++]=value;
        }
        metric(std::span(values).first(count));
    }
    out<<"},\"current\":{";
    constexpr const char* counters[]={"meshes","lights","passes","drawCalls","triangles","uploadCalls","uploadBytes","newMeshes","newTextures","cachedMeshes","cachedTextures","cpuAssetBytes","gpuMeshBytes","gpuTextureBytes","gpuBufferBytes","gpuDepthBytes","frameStorageBytes","frameStorageGrowths","objectBufferGrowths","changedObjects","objectUploadCalls","objectUploadBytes","frameUploadCalls","frameUploadBytes","localTransformBuilds","worldTransformBuilds","normalTransformBuilds","submittedInstances","instancedDrawCalls","maxInstancesPerDraw","skinnedInstances","cachedSkeletons","gpuSkinBytes","boneUploadCalls","boneUploadBytes"};
    constexpr std::array countMembers={&FrameCounters::meshes,&FrameCounters::lights,&FrameCounters::passes,&FrameCounters::drawCalls,&FrameCounters::triangles,&FrameCounters::uploadCalls,&FrameCounters::uploadBytes,&FrameCounters::newMeshes,&FrameCounters::newTextures,&FrameCounters::cachedMeshes,&FrameCounters::cachedTextures,&FrameCounters::cpuAssetBytes,&FrameCounters::gpuMeshBytes,&FrameCounters::gpuTextureBytes,&FrameCounters::gpuBufferBytes,&FrameCounters::gpuDepthBytes,&FrameCounters::frameStorageBytes,&FrameCounters::frameStorageGrowths,&FrameCounters::objectBufferGrowths,&FrameCounters::changedObjects,&FrameCounters::objectUploadCalls,&FrameCounters::objectUploadBytes,&FrameCounters::frameUploadCalls,&FrameCounters::frameUploadBytes,&FrameCounters::localTransformBuilds,&FrameCounters::worldTransformBuilds,&FrameCounters::normalTransformBuilds,&FrameCounters::submittedInstances,&FrameCounters::instancedDrawCalls,&FrameCounters::maxInstancesPerDraw,&FrameCounters::skinnedInstances,&FrameCounters::cachedSkeletons,&FrameCounters::gpuSkinBytes,&FrameCounters::boneUploadCalls,&FrameCounters::boneUploadBytes};
    for(std::size_t c=0;c<countMembers.size();++c) { if(c) out<<',';out<<'"'<<counters[c]<<"\":"<<(latest_.*countMembers[c]); }
    out<<",\"visibleMeshes\":"<<latest_.visibleMeshes<<",\"culledMeshes\":"<<latest_.culledMeshes;
    out<<",\"particles\":"<<latest_.particles<<",\"particleDrawCalls\":"<<latest_.particleDrawCalls
       <<",\"transparentMeshDraws\":"<<latest_.transparentMeshDraws<<",\"particleUploadCalls\":"<<latest_.particleUploadCalls
       <<",\"particleUploadBytes\":"<<latest_.particleUploadBytes<<",\"particleBufferGrowths\":"<<latest_.particleBufferGrowths;
    out<<",\"guiDrawCalls\":"<<latest_.guiDrawCalls<<",\"guiTriangles\":"<<latest_.guiTriangles
       <<",\"guiUploadCalls\":"<<latest_.guiUploadCalls<<",\"guiUploadBytes\":"<<latest_.guiUploadBytes
       <<",\"guiTextureBytes\":"<<latest_.guiTextureBytes<<",\"guiBufferBytes\":"<<latest_.guiBufferBytes;
    out<<",\"cachedMaterials\":"<<latest_.cachedMaterials<<",\"cachedMaterialPipelines\":"<<latest_.cachedMaterialPipelines
       <<",\"materialUploadCalls\":"<<latest_.materialUploadCalls<<",\"materialUploadBytes\":"<<latest_.materialUploadBytes;
    out<<",\"shadowPasses\":"<<latest_.shadowPasses<<",\"shadowDrawCalls\":"<<latest_.shadowDrawCalls<<",\"shadowInstances\":"<<latest_.shadowInstances;
    out<<",\"shadowTriangles\":"<<latest_.shadowTriangles<<",\"cameraLodParts\":"<<latest_.cameraLodParts
       <<",\"shadowLodParts\":"<<latest_.shadowLodParts<<",\"shadowCulledParts\":"<<latest_.shadowCulledParts
       <<",\"shadowMapResolution\":"<<latest_.shadowMapResolution<<",\"shadowFilterTaps\":"<<latest_.shadowFilterTaps;
    out<<",\"width\":"<<latest_.width<<",\"height\":"<<latest_.height<<",\"gpuPayloadBytes\":"<<(latest_.gpuMeshBytes+latest_.gpuTextureBytes+latest_.gpuBufferBytes+latest_.gpuDepthBytes);
    out<<"},\"gpu\":{\"state\":\""<<gpuState<<"\",\"windowSamples\":"<<gpuCount_<<",\"totalSamples\":"<<totalGpuSamples_<<",\"ageMs\":";
    if(gpuCount_) out<<std::max(0.0,nowMs-lastGpuAt_); else out<<"null";
    out<<",\"totalMs\":";
    if(gpuCount_) { for(std::size_t i=0;i<gpuCount_;++i) values[i]=gpu_[i][0]+gpu_[i][1]+gpu_[i][2]+shadowGpu_[i];metric(std::span(values).first(gpuCount_)); } else out<<"null";
    out<<",\"shadowMs\":";
    if(gpuCount_) metric(std::span(shadowGpu_).first(gpuCount_)); else out<<"null";
    out<<",\"layersMs\":[";
    for(std::size_t layer=0;layer<3;++layer) { if(layer) out<<',';if(gpuCount_) { for(std::size_t i=0;i<gpuCount_;++i) values[i]=gpu_[i][layer];metric(std::span(values).first(gpuCount_)); } else out<<"null"; }
    out<<"]}}"; return out.str();
}
} // namespace engine
