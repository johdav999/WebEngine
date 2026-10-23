#include "underwater_render.hpp"
#include "engine/performance_timeline.hpp"
#include "webgpu_renderer.hpp"
#include "engine/mesh_optimizer.hpp"
#include "shaders.hpp"
#include "hdr_shader.hpp"
#include "gui_renderer.hpp"
#include <cstddef>
#include <cstring>
#include <limits>

namespace engine {
namespace { struct UploadDeferred {}; }
namespace {
std::string messageText(wgpu::StringView message) {
    return message.data ? std::string(message.data, message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length) : std::string{};
}
struct alignas(16) Float4 { float x=0,y=0,z=0,w=0; };
struct GpuLight { Float4 positionType,directionRange,colorIntensity,cone; };
static_assert(sizeof(GpuLight)==64);
struct GpuShadow { Mat4 viewProjection; Float4 options,selection; };
struct FrameUniform { Mat4 viewProjection; Float4 ambientCount; std::array<GpuLight,WebGpuRenderer::maxLights> lights; Float4 cameraEnvironment,environmentOptions; std::array<GpuShadow,WebGpuRenderer::maxShadowLights> shadows; Float4 waterClock; Mat4 inverseViewProjection; Float4 viewport,extent,viewForward; Mat4 reflectionViewProjection; Float4 reflectionPlane,reflectionOptions; WaterInteractionParameters interactions; FogUniform fog; };
struct ParticleCamera { Mat4 viewProjection;Float4 right,up; FogUniform fog; Mat4 inverseVp;Float4 viewport,forward; };
static_assert(sizeof(ParticleCamera)==256);
static_assert(sizeof(FrameUniform)==2336 && sizeof(ObjectUniform)==160);
constexpr std::uint32_t objectStride=RenderFrame::objectStride;
wgpu::Buffer makeBuffer(const wgpu::Device& device,std::uint64_t size,wgpu::BufferUsage usage,const char* label) {
    wgpu::BufferDescriptor desc{}; desc.label=label; desc.size=size; desc.usage=usage|wgpu::BufferUsage::CopyDst;
    return device.CreateBuffer(&desc);
}
wgpu::BlendState fxBlend(bool additive) {
    wgpu::BlendState blend{};
    blend.color.srcFactor=wgpu::BlendFactor::One;
    blend.color.dstFactor=additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.color.operation=wgpu::BlendOperation::Add;
    blend.alpha.srcFactor=wgpu::BlendFactor::One;
    blend.alpha.dstFactor=wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation=wgpu::BlendOperation::Add;return blend;
}
}
void WebGpuRenderer::reportState(const char* state) {
    const auto& c=capabilities_;const auto& q=quality_;
    const auto compressedCount=std::count_if(textures_.begin(),textures_.end(),[](const auto& entry){return entry.second.compressed;});
    const auto json=std::string("{\"spotLights\":true,\"spotLightShadows\":true,\"maxLights\":8,\"backend\":\"webgpu\",\"shadows\":true,\"pbr\":true,\"instancing\":true,\"state\":\"")+state+"\",\"generation\":"+std::to_string(deviceGeneration_)+
        ",\"recoveryAttempts\":"+std::to_string(recoveryAttempts_)+",\"maxTextureDimension2D\":"+std::to_string(c.maxTextureDimension2D)+
        ",\"maxTextureArrayLayers\":"+std::to_string(c.maxTextureArrayLayers)+",\"maxBufferSize\":"+std::to_string(c.maxBufferSize)+
        ",\"timestampQuery\":"+(c.timestampQuery?"true":"false")+",\"compressionBC\":"+(c.compressionBC?"true":"false")+
        ",\"compressionETC2\":"+(c.compressionETC2?"true":"false")+",\"compressionASTC\":"+(c.compressionASTC?"true":"false")+
        ",\"compressedTextureCount\":"+std::to_string(compressedCount)+",\"width\":"+std::to_string(width_)+",\"height\":"+std::to_string(height_)+
        ",\"resolutionScale\":"+std::to_string(q.resolution.scale)+",\"maxPixelRatio\":"+std::to_string(q.resolution.maxPixelRatio)+
        ",\"maxDimension\":"+std::to_string(q.resolution.maxDimension)+",\"maxPixels\":"+std::to_string(q.resolution.maxPixels)+
        ",\"shadowResolution\":"+std::to_string(shadowResolution_)+",\"shadowFilterTaps\":"+std::to_string(shadowFilterTaps_)+
        ",\"msaaSamples\":"+std::to_string(q.msaaSamples)+",\"effectiveMsaaSamples\":"+std::to_string(effectiveMsaaSamples())+",\"msaa4x\":"+(c.msaa4x?"true":"false")+
        ",\"detailScale\":"+std::to_string(q.detailScale)+",\"maxShadowMaps\":"+std::to_string(q.maxShadowMaps)+",\"lod\":"+(lodEnabled_?"true":"false")+
        ",\"shadowLod\":"+(shadowLodEnabled_?"true":"false")+",\"compressedTextures\":"+(q.compressedTextures?"true":"false")+"}";
    auto report=json;report.pop_back();report+=",\"postEnabled\":"+std::string(q.post.enabled?"true":"false")+",\"hdrPostProcessing\":true,\"effectivePost\":"+(hdr_?"true":"false")+",\"directionalCascades\":"+std::to_string(q.directionalCascades)+",\"shadowDistance\":"+std::to_string(q.shadowDistance)+",\"postExposure\":"+std::to_string(q.post.exposure)+",\"postBloom\":"+std::to_string(q.post.bloomStrength)+",\"postThreshold\":"+std::to_string(q.post.bloomThreshold)+",\"postSaturation\":"+std::to_string(q.post.saturation)+",\"postContrast\":"+std::to_string(q.post.contrast)+"}";
    host_.renderState(report.c_str());
}
void WebGpuRenderer::fail(const std::string& message) {
    ready_=false;initializing_=false;
    if(recoveryAttempts_ && recoveryAttempts_<3) {
        recoveryPending_=true;retryAtMs_=host_.nowMs()+250*recoveryAttempts_;reportState("recovering");
        host_.status(message.c_str(),0);
    } else { recoveryPending_=false;failed_=true;reportState("failed");host_.status(message.c_str(),canvasAcquired_||!fallbackAllowed_?1:0); }
}
void WebGpuRenderer::deviceLost(const std::string& message) {
    ready_=false;initializing_=false;
    if(recoveryAttempts_>=3) { fail("GPU recovery exhausted: "+message);return; }
    recoveryPending_=true;retryAtMs_=host_.nowMs()+250;reportState("recovering");
    host_.status("GPU disconnected. Reconnecting…",0);
}
void WebGpuRenderer::serviceRecovery() {
    if(instance_) host_.processEvents(instance_);
    if(initializing_&&!canvasAcquired_&&host_.nowMs()-initializationStartedMs_>=10000) {
        callbacks_->owner=nullptr; // Late acquisition must not revive a timed-out backend.
        fail("WebGPU acquisition timed out");
    }
    if(recoveryPending_&&!initializing_&&host_.nowMs()>=retryAtMs_) {
        recoveryPending_=false;++recoveryAttempts_;releaseGpu();initialize();
    }
}
void WebGpuRenderer::retryRecovery() {
    if(!failed_) return;
    failed_=false;recoveryAttempts_=0;deviceLost("Explicit retry");
}
void WebGpuRenderer::simulateDeviceLoss() { if(ready_&&device_) device_.Destroy(); }
void WebGpuRenderer::resizeCanvas() {
    const auto size=host_.logicalSize();
    const auto extent=resolveResolution(size.width,size.height,size.pixelRatio,quality_.resolution,capabilities_);
    int w=0,h=0;host_.drawableSize(w,h);
    if(w!=int(extent.width)||h!=int(extent.height)) host_.resizeDrawable(extent.width,extent.height);
}
void WebGpuRenderer::setQuality(const RenderQuality& quality) {
    quality.validate(); // Validate the entire candidate before publishing any setting.
    if(quality.maxShadowMaps!=quality_.maxShadowMaps) shadowResourcesDirty_=true;
    if(quality.compressedTextures!=quality_.compressedTextures) { materials_.clear();textures_.clear(); }
    quality_=quality;setShadowQuality(std::min(quality.shadowResolution,capabilities_.maxTextureDimension2D),quality.shadowFilterTaps);
    lodEnabled_=quality.lod;shadowLodEnabled_=quality.shadowLod;shadowOverflowReported_=false;
    resizeCanvas();resetMeasurements();reportState(ready_?"ready":failed_?"failed":"initializing");
}
WebGpuRenderer::WebGpuRenderer(Scene& scene,WebGpuHost& host) : host_(host),scene_(&scene) {}
#ifdef __EMSCRIPTEN__
WebGpuRenderer::WebGpuRenderer(Scene& scene) : WebGpuRenderer(scene,browserWebGpuHost()) {}
WebGpuRenderer::WebGpuRenderer(Scene& scene,CameraComponent& camera) : WebGpuRenderer(scene) {
    const RenderView view{&camera};setViews({&view,1});
}
#endif
void WebGpuRenderer::setViews(std::span<const RenderView> views) {
    views_.set(views);
    // Release removed views, retain caches for unchanged slots.
    if(viewResources_.size()>views.size()) viewResources_.resize(views.size());
}
void WebGpuRenderer::setScene(Scene& scene,std::span<const RenderView> views) {
    for(const auto& view:views) if(!view.camera||view.camera->scene()!=&scene)
        throw std::invalid_argument("Render camera does not belong to the target scene");
    setViews(views);
    scene_=&scene;
}
WebGpuRenderer::~WebGpuRenderer() { releaseGpu(); }
void WebGpuRenderer::releaseGpu() {
    bloomTexture_=nullptr;bloomView_=nullptr;postLayout_=nullptr;bloomPipeline_=nullptr;postPipeline_=nullptr;postBuffer_=nullptr;
    postWidth_=postHeight_=0;postValues_={};hdr_=false;

    // Invalidate every outstanding adapter/device/timestamp callback before destroying resources.
    callbacks_->owner=nullptr;
    ready_=false;initializing_=false;guiRenderer_.reset();
    if(surface_&&width_) surface_.Unconfigure();
    viewResources_.clear();activeView_=nullptr;activeCamera_=nullptr;
    meshes_.clear();textures_.clear();materials_.clear();skeletons_.clear();morphs_.clear();
    materialPipelines_.clear();shadowPipelines_.clear();particlePipelines_={};ribbonPipelines_={};timingSlots_={};
    meshShader_=nullptr;meshPipelineLayout_=nullptr;skinPipelineLayout_=nullptr;
    shadowPipelineLayout_=nullptr;shadowSkinPipelineLayout_=nullptr;shadowLayout_=nullptr;
    shadowTexture_=nullptr;shadowView_=nullptr;shadowLayerViews_={};shadowSampler_=nullptr;
    particleLayout_=nullptr;bindLayout_=nullptr;textureLayout_=nullptr;materialLayout_=nullptr;skinLayout_=nullptr;
    clearPipeline_=nullptr;clearLayout_=nullptr;depth_=nullptr;depthView_=nullptr;
    sceneColor_=nullptr;waterColor_=nullptr;waterDepth_=nullptr;
    reflectionColor_=nullptr;reflectionDepth_=nullptr;reflectionColorView_=nullptr;reflectionDepthView_=nullptr;reflectionWidth_=reflectionHeight_=0;
    sceneColorView_=nullptr;waterColorView_=nullptr;waterDepthView_=nullptr;waterSampler_=nullptr;
    underwaterLayout_=nullptr;underwaterPipeline_=nullptr;underwaterSamples_=0;underwaterViews_.clear();
    captureLayouts_={};capturePipelines_={};captureBinding_=nullptr;waterPresentPipeline_=nullptr;
    waterCapture_=false;waterWidth_=waterHeight_=waterSamples_=0;++waterTargetRevision_;
    msaaColor_=nullptr;msaaColorView_=nullptr;sampleCount_=1;capabilities_.msaa4x=false;
    skyPipeline_=nullptr;skySpherePipeline_=nullptr;skyLayout_=nullptr;skySampler_=nullptr;
    specularEnvironment_=nullptr;diffuseEnvironment_=nullptr;brdfLut_=nullptr;
    specularView_=nullptr;diffuseView_=nullptr;brdfView_=nullptr;environmentSampler_=nullptr;environment_.reset();environmentBytes_=0;
    if(device_) device_.Destroy();
    device_=nullptr;adapter_=nullptr;surface_=nullptr;instance_=nullptr;
    width_=height_=0;gpuTimingFailed_=false;stableFrames_=0;resetMeasurements();
}
void WebGpuRenderer::initialize() {
    if(ready_||initializing_||failed_||recoveryPending_) return;
    if(!host_.available()) { fail("WebGPU is unavailable. Automatic mode can use WebGL 2.");return; }
    callbacks_=std::make_shared<CallbackState>(CallbackState{this});
    initializing_=true;initializationStartedMs_=host_.nowMs();reportState(recoveryAttempts_?"recovering":"initializing");
    instance_=host_.createInstance();
    wgpu::RequestAdapterOptions options{}; options.powerPreference=wgpu::PowerPreference::HighPerformance;host_.adapterOptions(options);
    instance_.RequestAdapter(&options,host_.callbackMode(),
        [callbacks=callbacks_](wgpu::RequestAdapterStatus status,wgpu::Adapter adapter,wgpu::StringView message) {
            auto* self=callbacks->owner; if(!self) return;
            if(status!=wgpu::RequestAdapterStatus::Success) { self->fail("No WebGPU adapter: "+messageText(message)); return; }
            self->adapter_=std::move(adapter);
            self->host_.adapterSelected(self->adapter_);
            wgpu::DeviceDescriptor desc{};
            self->gpuTimingSupported_=self->adapter_.HasFeature(wgpu::FeatureName::TimestampQuery);
            self->gpuTimingEnabled_=self->gpuTimingSupported_ && self->host_.timingRequested();
            std::vector<wgpu::FeatureName> features;
            if(self->gpuTimingEnabled_) features.push_back(wgpu::FeatureName::TimestampQuery);
            for(auto feature:{wgpu::FeatureName::TextureCompressionBC,wgpu::FeatureName::TextureCompressionETC2,wgpu::FeatureName::TextureCompressionASTC})
                if(self->adapter_.HasFeature(feature)) features.push_back(feature);
            desc.requiredFeatureCount=features.size();desc.requiredFeatures=features.data();
            // Device-lost callback retains the context for the repeating error callback's lifetime.
            desc.SetUncapturedErrorCallback([](const wgpu::Device&,wgpu::ErrorType,wgpu::StringView message,CallbackState* state) {
                if(state->owner) state->owner->fail("WebGPU error: "+messageText(message));
            },callbacks.get());
            desc.SetDeviceLostCallback(self->host_.callbackMode(),[callbacks](const wgpu::Device&,wgpu::DeviceLostReason,wgpu::StringView message) {
                if(callbacks->owner) callbacks->owner->deviceLost(messageText(message));
            });
            self->adapter_.RequestDevice(&desc,self->host_.callbackMode(),
                [callbacks](wgpu::RequestDeviceStatus status,wgpu::Device device,wgpu::StringView message) {
                    auto* self=callbacks->owner; if(!self) return;
                    if(status!=wgpu::RequestDeviceStatus::Success) { self->fail("Cannot create WebGPU device: "+messageText(message)); return; }
                    self->device_=std::move(device);
                    wgpu::Limits limits{};
                    if(self->device_.GetLimits(&limits)!=wgpu::Status::Success) { self->fail("Cannot query WebGPU limits");return; }
                    auto& c=self->capabilities_;c.maxTextureDimension2D=limits.maxTextureDimension2D;
                    c.maxTextureArrayLayers=limits.maxTextureArrayLayers;c.maxBufferSize=limits.maxBufferSize;
                    c.timestampQuery=self->gpuTimingSupported_;
                    c.compressionBC=self->device_.HasFeature(wgpu::FeatureName::TextureCompressionBC);
                    c.compressionETC2=self->device_.HasFeature(wgpu::FeatureName::TextureCompressionETC2);
                    c.compressionASTC=self->device_.HasFeature(wgpu::FeatureName::TextureCompressionASTC);
                    self->shadowResolution_=std::min(self->quality_.shadowResolution,c.maxTextureDimension2D);
                    ++self->deviceGeneration_;self->resizeCanvas();
                    self->canvasAcquired_=true;
                    self->surface_=self->host_.createSurface(self->instance_);
                    wgpu::SurfaceCapabilities capabilities{};
                    self->surface_.GetCapabilities(self->adapter_,&capabilities);
                    if(capabilities.formatCount==0) { self->fail("No supported canvas format"); return; }
                    self->format_=capabilities.formats[0];c.hdrPostProcessing=true;
                    // Standard 8-bit canvas color formats and Depth24Plus support 4x MSAA.
                    c.msaa4x=self->format_==wgpu::TextureFormat::BGRA8Unorm||self->format_==wgpu::TextureFormat::BGRA8UnormSrgb||
                        self->format_==wgpu::TextureFormat::RGBA8Unorm||self->format_==wgpu::TextureFormat::RGBA8UnormSrgb;
                    self->sampleCount_=resolveMsaaSamples(self->quality_,c);
                    self->createPipeline();
                    self->createClearPipeline();
                    self->createSkyPipeline();
                    self->initializeTiming();
                    self->ready_=true;self->initializing_=false;self->failed_=false;self->reportState("ready");
                    self->host_.status("WebGpuRenderer ready",0);
                });
        });
}
void WebGpuRenderer::configureTimingSlot(TimingSlot& slot,std::uint32_t queryCount) {
    slot.queryCount=queryCount;
    if(slot.resolve) slot.resolve.Destroy();
    if(slot.readback) slot.readback.Destroy();
    if(slot.queries) slot.queries.Destroy();
    wgpu::QuerySetDescriptor query{};query.label="View / layer timestamps";query.type=wgpu::QueryType::Timestamp;query.count=queryCount;
    slot.queries=device_.CreateQuerySet(&query);
    slot.resolve=makeBuffer(device_,queryCount*8,wgpu::BufferUsage::QueryResolve|wgpu::BufferUsage::CopySrc,"Timestamp resolve");
    wgpu::BufferDescriptor readback{};readback.label="Timestamp readback";readback.size=queryCount*8;readback.usage=wgpu::BufferUsage::MapRead|wgpu::BufferUsage::CopyDst;
    slot.readback=device_.CreateBuffer(&readback);
}
void WebGpuRenderer::initializeTiming() {
    if(gpuTimingEnabled_) for(auto& slot:timingSlots_) configureTimingSlot(slot,24);
}
int WebGpuRenderer::beginGpuSample(double nowMs) {
    if(!gpuTimingEnabled_ || gpuTimingFailed_ || resolvedViews_.empty() || nowMs-lastGpuSampleMs_<100) return -1;
    // WebGPU caps timestamp query sets at 8192 entries. Larger view lists still render.
    if(resolvedViews_.size()>8192/24) return -1;
    for(std::size_t i=0;i<timingSlots_.size();++i) {
        auto& slot=timingSlots_[i];if(slot.busy) continue;
        const auto queries=static_cast<std::uint32_t>(resolvedViews_.size()*24);
        if(slot.queryCount!=queries) configureTimingSlot(slot,queries);
        slot.shadowCounts.assign(resolvedViews_.size(),0);slot.waterPasses.assign(resolvedViews_.size(),false);slot.reflectionPasses.assign(resolvedViews_.size(),false);slot.underwaterPasses.assign(resolvedViews_.size(),false);
        slot.busy=true;slot.epoch=measurementEpoch_;lastGpuSampleMs_=nowMs;return static_cast<int>(i);
    }
    return -1; // Never block rendering on a timestamp readback.
}
void WebGpuRenderer::readGpuSample(int index) {
    auto& slot=timingSlots_[index];
    slot.readback.MapAsync(wgpu::MapMode::Read,0,slot.queryCount*8,host_.callbackMode(),
        [callbacks=callbacks_,index](wgpu::MapAsyncStatus status,wgpu::StringView) {
            auto* self=callbacks->owner;if(!self) return;
            auto& slot=self->timingSlots_[index];slot.busy=false;
            if(status!=wgpu::MapAsyncStatus::Success) { self->gpuTimingFailed_=true;return; }
            const auto* times=static_cast<const std::uint64_t*>(slot.readback.GetConstMappedRange(0,slot.queryCount*8));
            std::array<double,3> durations{};double shadowMs=0;bool valid=times!=nullptr;
            if(times) for(std::uint32_t pair=0;pair<slot.queryCount/2;++pair) {
                const auto local=pair%12,view=pair/12;
                if(local>=7&&local<9&&!slot.waterPasses[view]) continue;
                if(local==9&&!slot.reflectionPasses[view]) continue;
                if(local>=10&&!slot.underwaterPasses[view]) continue;
                if(local>=3&&local<7&&local>=3+slot.shadowCounts[view]) continue;
                if(times[pair*2+1]<times[pair*2]) valid=false;
                else if(local>=3&&local<7) shadowMs+=double(times[pair*2+1]-times[pair*2])*1e-6;
                else durations[local>=7 ? 1 : local]+=static_cast<double>(times[pair*2+1]-times[pair*2])*1e-6; // WebGPU timestamps are nanoseconds.
            }
            slot.readback.Unmap();
            if(valid && slot.epoch==self->measurementEpoch_) self->measurements_.recordGpu(durations,self->host_.nowMs(),shadowMs);
        });
}
void WebGpuRenderer::resetMeasurements() {
    measurements_.reset();++measurementEpoch_;lastReportMs_=-1000;lastGpuSampleMs_=-1000;
}
void WebGpuRenderer::reportMeasurements(double nowMs) {
    if(nowMs-lastReportMs_<250) return;
    lastReportMs_=nowMs;reportState("ready");
    const char* gpuState=!gpuTimingSupported_ ? "unsupported" : !gpuTimingEnabled_ ? "disabled" : gpuTimingFailed_ ? "readback-failed" : "enabled";
    const auto json=measurements_.json(nowMs,gpuState);
#ifdef NDEBUG
    host_.performance(json.c_str(),"Release");
#else
    host_.performance(json.c_str(),"Debug");
#endif
}
void WebGpuRenderer::createPipeline() {
    wgpu::ShaderSourceWGSL code{};const auto source=hdrShader(mesh_shader,hdr_);code.code=source.c_str();
    wgpu::ShaderModuleDescriptor shader{};shader.nextInChain=&code;shader.label="PBR / Unlit mesh materials";
    meshShader_=device_.CreateShaderModule(&shader);
    std::array<wgpu::BindGroupLayoutEntry,11> frameBindings{};
    frameBindings[0].binding=0;frameBindings[0].visibility=wgpu::ShaderStage::Vertex|wgpu::ShaderStage::Fragment;
    frameBindings[0].buffer.type=wgpu::BufferBindingType::Uniform;frameBindings[0].buffer.minBindingSize=sizeof(FrameUniform);
    for(std::uint32_t i=1;i<4;++i) {
        frameBindings[i].binding=i;frameBindings[i].visibility=wgpu::ShaderStage::Fragment;
        frameBindings[i].texture.sampleType=wgpu::TextureSampleType::Float;
        frameBindings[i].texture.viewDimension=i==3 ? wgpu::TextureViewDimension::e2D : wgpu::TextureViewDimension::Cube;
    }
    frameBindings[4].binding=4;frameBindings[4].visibility=wgpu::ShaderStage::Fragment;frameBindings[4].sampler.type=wgpu::SamplerBindingType::Filtering;
    frameBindings[5].binding=5;frameBindings[5].visibility=wgpu::ShaderStage::Fragment;
    frameBindings[5].texture.sampleType=wgpu::TextureSampleType::Depth;frameBindings[5].texture.viewDimension=wgpu::TextureViewDimension::e2DArray;
    frameBindings[6].binding=6;frameBindings[6].visibility=wgpu::ShaderStage::Fragment;frameBindings[6].sampler.type=wgpu::SamplerBindingType::Comparison;
    frameBindings[7].binding=7;frameBindings[7].visibility=wgpu::ShaderStage::Fragment;
    frameBindings[7].texture.sampleType=wgpu::TextureSampleType::Float;frameBindings[7].texture.viewDimension=wgpu::TextureViewDimension::e2D;
    frameBindings[8].binding=8;frameBindings[8].visibility=wgpu::ShaderStage::Fragment;
    frameBindings[8].texture.sampleType=wgpu::TextureSampleType::UnfilterableFloat;frameBindings[8].texture.viewDimension=wgpu::TextureViewDimension::e2D;
    frameBindings[9].binding=9;frameBindings[9].visibility=wgpu::ShaderStage::Fragment;frameBindings[9].sampler.type=wgpu::SamplerBindingType::Filtering;
    frameBindings[10]=frameBindings[7];frameBindings[10].binding=10;
    wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=frameBindings.size();layout.entries=frameBindings.data();bindLayout_=device_.CreateBindGroupLayout(&layout);
    // Dedicated particle shaders retain their compact color-only binding layout.
    std::array<wgpu::BindGroupLayoutEntry,2> textureBindings{};
    textureBindings[0].binding=0;textureBindings[0].visibility=wgpu::ShaderStage::Fragment;textureBindings[0].sampler.type=wgpu::SamplerBindingType::Filtering;
    textureBindings[1].binding=1;textureBindings[1].visibility=wgpu::ShaderStage::Fragment|wgpu::ShaderStage::Vertex;
    textureBindings[1].texture.sampleType=wgpu::TextureSampleType::Float;textureBindings[1].texture.viewDimension=wgpu::TextureViewDimension::e2D;
    layout.entryCount=textureBindings.size();layout.entries=textureBindings.data();textureLayout_=device_.CreateBindGroupLayout(&layout);
    std::array<wgpu::BindGroupLayoutEntry,19> materialBindings{};
    materialBindings[0].binding=0;materialBindings[0].visibility=wgpu::ShaderStage::Vertex|wgpu::ShaderStage::Fragment;
    materialBindings[0].buffer.type=wgpu::BufferBindingType::Uniform;materialBindings[0].buffer.minBindingSize=400;
    for(std::uint32_t i=0;i<9;++i) {
        auto& sampler=materialBindings[1+i*2];sampler.binding=1+i*2;sampler.visibility=wgpu::ShaderStage::Fragment;sampler.sampler.type=wgpu::SamplerBindingType::Filtering;
        auto& texture=materialBindings[2+i*2];texture.binding=2+i*2;texture.visibility=wgpu::ShaderStage::Fragment;
        texture.texture.sampleType=wgpu::TextureSampleType::Float;texture.texture.viewDimension=wgpu::TextureViewDimension::e2D;
    }
    layout.entryCount=materialBindings.size();layout.entries=materialBindings.data();materialLayout_=device_.CreateBindGroupLayout(&layout);
    wgpu::BindGroupLayoutEntry skin{};skin.binding=0;skin.visibility=wgpu::ShaderStage::Vertex;skin.buffer.type=wgpu::BufferBindingType::ReadOnlyStorage;skin.buffer.minBindingSize=sizeof(BoneUniform);
    layout.entryCount=1;layout.entries=&skin;skinLayout_=device_.CreateBindGroupLayout(&layout);
    const std::array layouts={bindLayout_,materialLayout_,skinLayout_};
    wgpu::PipelineLayoutDescriptor pl{};pl.bindGroupLayoutCount=2;pl.bindGroupLayouts=layouts.data();meshPipelineLayout_=device_.CreatePipelineLayout(&pl);
    pl.bindGroupLayoutCount=3;skinPipelineLayout_=device_.CreatePipelineLayout(&pl);
    // The depth pass must not bind the texture that it is currently writing.
    layout.entryCount=1;layout.entries=frameBindings.data();shadowLayout_=device_.CreateBindGroupLayout(&layout);
    const std::array shadowLayouts={shadowLayout_,materialLayout_,skinLayout_};
    pl.bindGroupLayouts=shadowLayouts.data();pl.bindGroupLayoutCount=2;shadowPipelineLayout_=device_.CreatePipelineLayout(&pl);
    pl.bindGroupLayoutCount=3;shadowSkinPipelineLayout_=device_.CreatePipelineLayout(&pl);
    createShadowResources();
    createFxPipelines();
    createWaterPipelines();
}
void WebGpuRenderer::setShadowQuality(std::uint32_t resolution,std::uint32_t taps) {
    if((resolution!=256 && resolution!=512 && resolution!=1024 && resolution!=2048) || (taps!=1 && taps!=9))
        throw std::invalid_argument("Shadow quality requires 256/512/1024/2448 resolution and 1/9 taps");
    if(shadowResolution_!=resolution) {
        shadowResourcesDirty_=true;
        for(auto& view:viewResources_) view->frame.resetShadowDetailHistory();
    }
    shadowResolution_=resolution;shadowFilterTaps_=taps;quality_.shadowResolution=resolution;quality_.shadowFilterTaps=taps;
}
void WebGpuRenderer::createShadowResources() {
    if(shadowTexture_) shadowTexture_.Destroy();
    wgpu::TextureDescriptor shadowTexture{};shadowTexture.label="Directional shadow depth array";
    shadowTexture.size={shadowResolution_,shadowResolution_,std::max(1u,quality_.maxShadowMaps)};shadowTexture.format=wgpu::TextureFormat::Depth32Float;
    shadowTexture.usage=wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::TextureBinding;
    shadowTexture_=device_.CreateTexture(&shadowTexture);
    wgpu::TextureViewDescriptor shadowView{};shadowView.dimension=wgpu::TextureViewDimension::e2DArray;shadowView.arrayLayerCount=std::max(1u,quality_.maxShadowMaps);
    shadowView_=shadowTexture_.CreateView(&shadowView);
    wgpu::SamplerDescriptor comparison{};comparison.label="Shadow PCF comparison";comparison.compare=wgpu::CompareFunction::LessEqual;
    comparison.minFilter=comparison.magFilter=wgpu::FilterMode::Linear;comparison.addressModeU=comparison.addressModeV=wgpu::AddressMode::ClampToEdge;
    shadowSampler_=device_.CreateSampler(&comparison);
    shadowLayerViews_={};
    for(std::size_t i=0;i<std::max(1u,quality_.maxShadowMaps);++i) {
        shadowView.dimension=wgpu::TextureViewDimension::e2D;shadowView.baseArrayLayer=i;shadowView.arrayLayerCount=1;
        shadowLayerViews_[i]=shadowTexture_.CreateView(&shadowView);
    }
    for(auto& view:viewResources_) view->boundEnvironment.reset();
    shadowResourcesDirty_=false;
}
wgpu::RenderPipeline WebGpuRenderer::materialPipeline(const MeshDraw& draw,bool shadow,bool reflection) {
    const auto& m=draw.material;const bool skin=bool(draw.skinPose),transparent=draw.blend!=BlendMode::Opaque;
    const bool layered=m.layers.enabled,normalMap=bool(m.normalTexture)&&!layered;
    const bool surfaceMap=bool(m.metallicRoughnessTexture)&&!layered;
    const bool occlusionMap=bool(m.occlusionTexture)&&!layered,emissiveMap=bool(m.emissiveTexture)&&!layered;
    const auto key=std::uint32_t(m.alphaMode)|(std::uint32_t(m.shader)<<2)|(std::uint32_t(m.doubleSided)<<4)|(std::uint32_t(skin)<<5)|(std::uint32_t(draw.mirrored)<<6)|(std::uint32_t(reflection)<<7)|(std::uint32_t(layered)<<8)|(std::uint32_t(normalMap)<<9)
        |(std::uint32_t(surfaceMap)<<10)|(std::uint32_t(occlusionMap)<<11)|(std::uint32_t(emissiveMap)<<12);
    auto& pipelines=shadow ? shadowPipelines_ : materialPipelines_;
    if(auto it=pipelines.find(key);it!=pipelines.end()) return it->second;
    std::array<wgpu::VertexAttribute,4> attributes{};
    attributes[0]={nullptr,wgpu::VertexFormat::Float32x3,offsetof(Vertex,position),0};
    attributes[1]={nullptr,wgpu::VertexFormat::Float32x3,offsetof(Vertex,normal),1};
    attributes[2]={nullptr,wgpu::VertexFormat::Float32x2,offsetof(Vertex,uv),2};
    attributes[3]={nullptr,wgpu::VertexFormat::Float32x4,offsetof(Vertex,tangent),14};
    std::array<wgpu::VertexAttribute,10> instance{};
    for(std::uint32_t i=0;i<9;++i) instance[i]={nullptr,wgpu::VertexFormat::Float32x4,i*16,i+3};
    instance[9]={nullptr,wgpu::VertexFormat::Float32x4,144,15};
    std::array<wgpu::VertexAttribute,2> weights={wgpu::VertexAttribute{nullptr,wgpu::VertexFormat::Uint32x4,0,12},wgpu::VertexAttribute{nullptr,wgpu::VertexFormat::Float32x4,offsetof(SkinVertex,weights),13}};
    std::array<wgpu::VertexBufferLayout,3> buffers{};
    buffers[0].arrayStride=sizeof(Vertex);buffers[0].attributeCount=attributes.size();buffers[0].attributes=attributes.data();
    buffers[1].arrayStride=objectStride;buffers[1].stepMode=wgpu::VertexStepMode::Instance;buffers[1].attributeCount=instance.size();buffers[1].attributes=instance.data();
    buffers[2].arrayStride=sizeof(SkinVertex);buffers[2].attributeCount=weights.size();buffers[2].attributes=weights.data();
    const auto blend=fxBlend(draw.blend==BlendMode::Additive);
    wgpu::ColorTargetState target{};target.format=colorFormat();if(transparent) target.blend=&blend;
    const std::array<wgpu::ConstantEntry,8> materialConstants={{{nullptr,"materialAlpha",double(m.alphaMode)},
        {nullptr,"materialLayered",double(layered)},{nullptr,"materialShader",double(m.shader)},
        {nullptr,"materialNormalMap",double(normalMap)},{nullptr,"materialSurfaceMap",double(surfaceMap)},
        {nullptr,"materialOcclusionMap",double(occlusionMap)},{nullptr,"materialEmissiveMap",double(emissiveMap)},
        {nullptr,"materialReflectionPass",double(reflection)}}};
    const wgpu::ConstantEntry vertexShader{nullptr,"materialShader",double(m.shader)};
    wgpu::FragmentState fragment{};fragment.constantCount=shadow?2:materialConstants.size();fragment.constants=materialConstants.data();fragment.module=meshShader_;fragment.entryPoint=transparent ? "transparentMain" : "fragmentMain";fragment.targetCount=1;fragment.targets=&target;
    if(shadow) { fragment.entryPoint="shadowFragment";fragment.targetCount=0;fragment.targets=nullptr; }
    wgpu::DepthStencilState depth{};depth.format=wgpu::TextureFormat::Depth24Plus;depth.depthWriteEnabled=!transparent;depth.depthCompare=transparent ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Less;
    if(shadow) { depth.format=wgpu::TextureFormat::Depth32Float;depth.depthWriteEnabled=true;depth.depthCompare=wgpu::CompareFunction::Less;depth.depthBias=1;depth.depthBiasSlopeScale=1; }
    wgpu::RenderPipelineDescriptor desc{};desc.label="Cached material pipeline";desc.layout=skin ? skinPipelineLayout_ : meshPipelineLayout_;
    if(shadow) { desc.label="Directional shadow caster";desc.layout=skin ? shadowSkinPipelineLayout_ : shadowPipelineLayout_; }
    desc.vertex.constantCount=1;desc.vertex.constants=&vertexShader;
    desc.vertex.module=meshShader_;desc.vertex.entryPoint=skin ? "skinnedVertexMain" : "vertexMain";desc.vertex.bufferCount=skin ? 3 : 2;desc.vertex.buffers=buffers.data();
    desc.fragment=shadow && m.alphaMode!=AlphaMode::Mask ? nullptr : &fragment;desc.depthStencil=&depth;desc.primitive.topology=wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode=m.doubleSided ? wgpu::CullMode::None : wgpu::CullMode::Back;desc.primitive.frontFace=(draw.mirrored!=reflection) ? wgpu::FrontFace::CW : wgpu::FrontFace::CCW;
    desc.multisample.count=shadow||reflection ? 1u : sampleCount_;
    const auto pipeline=device_.CreateRenderPipeline(&desc);pipelines.emplace(key,pipeline);return pipeline;
}
void WebGpuRenderer::createFxPipelines() {
    wgpu::BindGroupLayoutEntry entry{};entry.binding=0;entry.visibility=wgpu::ShaderStage::Vertex|wgpu::ShaderStage::Fragment;
    entry.buffer.type=wgpu::BufferBindingType::Uniform;entry.buffer.minBindingSize=sizeof(ParticleCamera);
    std::array<wgpu::BindGroupLayoutEntry,2> entries{entry,{}};
    entries[1].binding=1;entries[1].visibility=wgpu::ShaderStage::Fragment;
    entries[1].texture.viewDimension=wgpu::TextureViewDimension::e2D;entries[1].texture.sampleType=wgpu::TextureSampleType::UnfilterableFloat;
    wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=entries.size();layout.entries=entries.data();
    if(!particleLayout_) particleLayout_=device_.CreateBindGroupLayout(&layout);
    const std::array layouts={particleLayout_,textureLayout_};
    wgpu::PipelineLayoutDescriptor pipelineLayout{};pipelineLayout.bindGroupLayoutCount=layouts.size();pipelineLayout.bindGroupLayouts=layouts.data();
    const auto explicitLayout=device_.CreatePipelineLayout(&pipelineLayout);
    wgpu::ShaderSourceWGSL code{};const auto source=hdrShader(particle_shader,hdr_);code.code=source.c_str();
    wgpu::ShaderModuleDescriptor shaderDesc{};shaderDesc.nextInChain=&code;shaderDesc.label="Particle billboards / flipbooks";
    const auto shader=device_.CreateShaderModule(&shaderDesc);
    std::array<wgpu::VertexAttribute,7> attributes{};
    for(std::size_t i=0;i<attributes.size();++i) { attributes[i].format=wgpu::VertexFormat::Float32x4;attributes[i].offset=i*16;attributes[i].shaderLocation=static_cast<std::uint32_t>(i); }
    wgpu::VertexBufferLayout buffer{};buffer.arrayStride=sizeof(ParticleInstance);buffer.stepMode=wgpu::VertexStepMode::Instance;
    buffer.attributeCount=attributes.size();buffer.attributes=attributes.data();
    wgpu::ColorTargetState target{};target.format=colorFormat();
    wgpu::FragmentState fragment{};fragment.module=shader;fragment.entryPoint="particleFragment";fragment.targetCount=1;fragment.targets=&target;
    wgpu::DepthStencilState depth{};depth.format=wgpu::TextureFormat::Depth24Plus;depth.depthWriteEnabled=false;depth.depthCompare=wgpu::CompareFunction::LessEqual;
    wgpu::RenderPipelineDescriptor desc{};desc.label="Instanced particle FX";desc.layout=explicitLayout;
    desc.vertex.module=shader;desc.vertex.entryPoint="particleVertex";desc.vertex.bufferCount=1;desc.vertex.buffers=&buffer;
    desc.fragment=&fragment;desc.depthStencil=&depth;desc.primitive.topology=wgpu::PrimitiveTopology::TriangleList;
    desc.multisample.count=sampleCount_;
    for(std::size_t i=0;i<2;++i) { const auto blend=fxBlend(i==1);target.blend=&blend;particlePipelines_[i]=device_.CreateRenderPipeline(&desc); }
    buffer.arrayStride=sizeof(RibbonVertex);buffer.stepMode=wgpu::VertexStepMode::Vertex;buffer.attributeCount=3;
    desc.vertex.entryPoint="ribbonVertex";
    for(std::size_t i=0;i<2;++i){const auto blend=fxBlend(i==1);target.blend=&blend;ribbonPipelines_[i]=device_.CreateRenderPipeline(&desc);}

}
void WebGpuRenderer::createClearPipeline() {
    // Attachment clears ignore viewport/scissor. A triangle clears just this view.
    constexpr const char* source=R"(
        @group(0) @binding(0) var<uniform> color: vec4f;
        @vertex fn vertexMain(@builtin(vertex_index) i:u32) -> @builtin(position) vec4f {
            let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));
            return vec4f(p[i],0,1);
        }
        @fragment fn fragmentMain() -> @location(0) vec4f { return color; }
    )";
    wgpu::ShaderSourceWGSL code{};code.code=source;
    wgpu::ShaderModuleDescriptor shaderDesc{};shaderDesc.nextInChain=&code;
    const auto shader=device_.CreateShaderModule(&shaderDesc);
    wgpu::BindGroupLayoutEntry entry{};entry.binding=0;entry.visibility=wgpu::ShaderStage::Fragment;
    entry.buffer.type=wgpu::BufferBindingType::Uniform;entry.buffer.minBindingSize=16;
    wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=1;layout.entries=&entry;
    if(!clearLayout_) clearLayout_=device_.CreateBindGroupLayout(&layout);
    wgpu::PipelineLayoutDescriptor pipelineLayout{};pipelineLayout.bindGroupLayoutCount=1;pipelineLayout.bindGroupLayouts=&clearLayout_;
    const auto explicitLayout=device_.CreatePipelineLayout(&pipelineLayout);
    wgpu::ColorTargetState target{};target.format=colorFormat();
    wgpu::FragmentState fragment{};fragment.module=shader;fragment.entryPoint="fragmentMain";fragment.targetCount=1;fragment.targets=&target;
    wgpu::DepthStencilState depth{};depth.format=wgpu::TextureFormat::Depth24Plus;depth.depthWriteEnabled=false;depth.depthCompare=wgpu::CompareFunction::Always;
    wgpu::RenderPipelineDescriptor desc{};desc.label="Viewport color clear";desc.layout=explicitLayout;
    desc.vertex.module=shader;desc.vertex.entryPoint="vertexMain";desc.fragment=&fragment;desc.depthStencil=&depth;
    desc.multisample.count=sampleCount_;
    clearPipeline_=device_.CreateRenderPipeline(&desc);
}
void WebGpuRenderer::createSkyPipeline() {
    static_assert(sizeof(SkyUniform)==320);
    wgpu::ShaderSourceWGSL code{};const auto source=hdrShader(sky_shader,hdr_);code.code=source.c_str();
    wgpu::ShaderModuleDescriptor shaderDesc{};shaderDesc.nextInChain=&code;shaderDesc.label="Sky background";
    const auto shader=device_.CreateShaderModule(&shaderDesc);
    wgpu::BindGroupLayoutEntry entry{};entry.binding=0;entry.visibility=wgpu::ShaderStage::Vertex|wgpu::ShaderStage::Fragment;
    entry.buffer.type=wgpu::BufferBindingType::Uniform;entry.buffer.minBindingSize=sizeof(SkyUniform);
    wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=1;layout.entries=&entry;
    if(!skyLayout_) skyLayout_=device_.CreateBindGroupLayout(&layout);
    const std::array layouts={skyLayout_,textureLayout_};
    wgpu::PipelineLayoutDescriptor pipelineLayout{};pipelineLayout.bindGroupLayoutCount=layouts.size();pipelineLayout.bindGroupLayouts=layouts.data();
    const auto explicitLayout=device_.CreatePipelineLayout(&pipelineLayout);
    wgpu::ColorTargetState target{};target.format=colorFormat();
    wgpu::FragmentState fragment{};fragment.module=shader;fragment.entryPoint="fragmentMain";fragment.targetCount=1;fragment.targets=&target;
    wgpu::DepthStencilState depth{};depth.format=wgpu::TextureFormat::Depth24Plus;depth.depthWriteEnabled=false;depth.depthCompare=wgpu::CompareFunction::Always;
    wgpu::RenderPipelineDescriptor desc{};desc.label="Sky background";desc.layout=explicitLayout;
    desc.vertex.module=shader;desc.vertex.entryPoint="vertexMain";desc.fragment=&fragment;desc.depthStencil=&depth;
    desc.multisample.count=sampleCount_;
    skyPipeline_=device_.CreateRenderPipeline(&desc);
    wgpu::VertexAttribute attribute{};attribute.format=wgpu::VertexFormat::Float32x3;attribute.shaderLocation=0;
    wgpu::VertexBufferLayout buffer{};buffer.arrayStride=sizeof(Vertex);buffer.attributeCount=1;buffer.attributes=&attribute;
    desc.label="Inward unlit sky sphere";desc.vertex.entryPoint="sphereVertex";desc.vertex.bufferCount=1;desc.vertex.buffers=&buffer;
    skySpherePipeline_=device_.CreateRenderPipeline(&desc);
    wgpu::SamplerDescriptor sampler{};sampler.addressModeU=wgpu::AddressMode::Repeat;sampler.addressModeV=wgpu::AddressMode::ClampToEdge;
    sampler.minFilter=sampler.magFilter=wgpu::FilterMode::Linear;skySampler_=device_.CreateSampler(&sampler);
}
bool WebGpuRenderer::prepareSkyBindings(const RenderView& view,float aspect) {
    auto& v=*activeView_;
    if(!view.skyEnabled||!scene_->sky()) { v.skyTexture.reset();v.skyTextureBinding=nullptr;return false; }
    const auto& sky=*scene_->sky();auto payload=prepareSky(sky,*view.camera,aspect);payload.fog=prepareFog(scene_->fog(),*view.camera,view.fogEnabled);
    const auto source=sky.mode==SkyMode::Panorama ? sky.panorama : Texture::white();
    GpuTexture* texture=nullptr;
    try {
        texture=&uploadTexture(source);
        if(sky.geometry==SkyGeometry::Sphere) upload(skySphereMesh());
    } catch(const UploadDeferred&) { ++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return false; }
    if(!v.skyBuffer) {
        v.skyBuffer=makeBuffer(device_,sizeof(SkyUniform),wgpu::BufferUsage::Uniform,"View sky");
        wgpu::BindGroupEntry entry{};entry.binding=0;entry.buffer=v.skyBuffer;entry.size=sizeof(SkyUniform);
        wgpu::BindGroupDescriptor group{};group.layout=skyLayout_;group.entryCount=1;group.entries=&entry;
        v.skyBinding=device_.CreateBindGroup(&group);
    }
    if(!v.skyValid||std::memcmp(&v.skyShadow,&payload,sizeof(payload))) {
        device_.GetQueue().WriteBuffer(v.skyBuffer,0,&payload,sizeof(payload));v.skyShadow=payload;v.skyValid=true;
        ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(payload);
        ++counters_.frameUploadCalls;counters_.frameUploadBytes+=sizeof(payload);
    }
    if(v.skyTexture!=source) {
        std::array<wgpu::BindGroupEntry,2> entries{};entries[0].binding=0;entries[0].sampler=skySampler_;entries[1].binding=1;entries[1].textureView=texture->view;
        wgpu::BindGroupDescriptor group{};group.layout=textureLayout_;group.entryCount=2;group.entries=entries.data();
        v.skyTextureBinding=device_.CreateBindGroup(&group);v.skyTexture=source;
    }
    return true;
}
void WebGpuRenderer::prepareViewBindings(const RenderView& view) {
    auto& v=*activeView_;
    if(!v.frameBuffer) {
        v.reflectionBuffer=makeBuffer(device_,sizeof(FrameUniform),wgpu::BufferUsage::Uniform,"Mirrored camera / lights");
        v.frameBuffer=makeBuffer(device_,sizeof(FrameUniform),wgpu::BufferUsage::Uniform,"View camera / lights");
        v.particleCameraBuffer=makeBuffer(device_,sizeof(ParticleCamera),wgpu::BufferUsage::Uniform,"View particle camera");
        v.clearBuffer=makeBuffer(device_,16,wgpu::BufferUsage::Uniform,"View clear color");
        wgpu::BindGroupEntry entry{};entry.binding=0;entry.buffer=v.particleCameraBuffer;entry.size=sizeof(ParticleCamera);
        wgpu::BindGroupDescriptor group{};group.layout=particleLayout_;group.entryCount=1;group.entries=&entry;
        entry.buffer=v.clearBuffer;entry.size=16;group.layout=clearLayout_;v.clearBinding=device_.CreateBindGroup(&group);
        for(std::size_t i=0;i<maxShadowLights;++i) {
            v.shadowBuffers[i]=makeBuffer(device_,sizeof(FrameUniform),wgpu::BufferUsage::Uniform,"View shadow camera");
            entry.buffer=v.shadowBuffers[i];entry.size=sizeof(FrameUniform);group.layout=shadowLayout_;
            v.shadowBindings[i]=device_.CreateBindGroup(&group);
        }
    }
    if(v.boundEnvironment!=environment_||v.waterRevision!=waterTargetRevision_) {
        std::array<wgpu::BindGroupEntry,11> entries{};entries[0].binding=0;entries[0].buffer=v.frameBuffer;entries[0].size=sizeof(FrameUniform);
        entries[1].binding=1;entries[1].textureView=specularView_;entries[2].binding=2;entries[2].textureView=diffuseView_;entries[3].binding=3;entries[3].textureView=brdfView_;
        entries[4].binding=4;entries[4].sampler=environmentSampler_;
        entries[5].binding=5;entries[5].textureView=shadowView_;entries[6].binding=6;entries[6].sampler=shadowSampler_;
        wgpu::BindGroupDescriptor group{};group.layout=bindLayout_;group.entryCount=entries.size();group.entries=entries.data();
        entries[7].binding=7;entries[7].textureView=waterColorView_;
        entries[8].binding=8;entries[8].textureView=waterDepthView_;
        entries[9].binding=9;entries[9].sampler=waterSampler_;
        entries[10].binding=10;entries[10].textureView=reflectionColorView_;
        v.binding=device_.CreateBindGroup(&group);
        entries[0].buffer=v.reflectionBuffer;entries[10].textureView=waterColorView_;
        v.reflectionBinding=device_.CreateBindGroup(&group);
        std::array<wgpu::BindGroupEntry,2> particleEntries{};
        particleEntries[0].binding=0;particleEntries[0].buffer=v.particleCameraBuffer;particleEntries[0].size=sizeof(ParticleCamera);
        particleEntries[1].binding=1;particleEntries[1].textureView=waterDepthView_;
        group.layout=particleLayout_;group.entryCount=particleEntries.size();group.entries=particleEntries.data();v.particleBinding=device_.CreateBindGroup(&group);
        v.boundEnvironment=environment_;v.waterRevision=waterTargetRevision_;
    }
    const auto& color=view.clearColor;
    if(color && (!v.clearValid || v.clearShadow!=*color)) {
        device_.GetQueue().WriteBuffer(v.clearBuffer,0,color->data(),16);v.clearShadow=*color;v.clearValid=true;
        ++counters_.uploadCalls;counters_.uploadBytes+=16;
    }
}
void WebGpuRenderer::prepareFx() {
    const auto instances=activeView_->fxFrame.instances();
    for(const auto& draw:activeView_->frame.sceneFrame().particles) {
        try { uploadTexture(draw.texture); } catch(const UploadDeferred&) { ++counters_.deferredAssetUploads; }
    }
    for(const auto& draw:activeView_->frame.sceneFrame().ribbons){try{uploadTexture(draw.texture);}catch(const UploadDeferred&){++counters_.deferredAssetUploads;}}
    const auto vertices=activeView_->fxFrame.ribbonVertices();
    if(vertices.empty())activeView_->ribbonShadow.clear();
    else {
        bool grow=vertices.size()>activeView_->ribbonCapacity;
        if(grow){wgpu::Limits limits{};if(device_.GetLimits(&limits)!=wgpu::Status::Success||vertices.size()>limits.maxBufferSize/sizeof(RibbonVertex))throw std::length_error("Ribbon buffer exceeds GPU limits");
            activeView_->ribbonCapacity=std::min<std::uint64_t>(std::max(vertices.size(),activeView_->ribbonCapacity*2+16),limits.maxBufferSize/sizeof(RibbonVertex));
            if(activeView_->ribbonBuffer)activeView_->ribbonBuffer.Destroy();
            activeView_->ribbonBuffer=makeBuffer(device_,activeView_->ribbonCapacity*sizeof(RibbonVertex),wgpu::BufferUsage::Vertex,"Ribbon vertices");}
        if(grow||activeView_->ribbonShadow.size()!=vertices.size()||std::memcmp(vertices.data(),activeView_->ribbonShadow.data(),vertices.size_bytes())){
            device_.GetQueue().WriteBuffer(activeView_->ribbonBuffer,0,vertices.data(),vertices.size_bytes());activeView_->ribbonShadow.assign(vertices.begin(),vertices.end());
            ++counters_.uploadCalls;counters_.uploadBytes+=vertices.size_bytes();counters_.trailUploadBytes+=vertices.size_bytes();}
    }
    if(instances.empty()) activeView_->particleShadow.clear();
    if(instances.empty()&&vertices.empty())return;
    if(!instances.empty()) {
    bool recreate=instances.size()>activeView_->particleCapacity;
    if(recreate) {
        wgpu::Limits limits{};if(device_.GetLimits(&limits)!=wgpu::Status::Success) throw std::runtime_error("Cannot query particle GPU limits");
        const auto max=limits.maxBufferSize/sizeof(ParticleInstance);
        if(instances.size()>max) throw std::length_error("Particle data exceeds GPU limits");
        activeView_->particleCapacity=static_cast<std::size_t>(std::min<std::uint64_t>(std::max(instances.size(),activeView_->particleCapacity*2+16),max));
        if(activeView_->particleBuffer) activeView_->particleBuffer.Destroy();
        activeView_->particleBuffer=makeBuffer(device_,activeView_->particleCapacity*sizeof(ParticleInstance),wgpu::BufferUsage::Vertex,"Pooled particle instances");
        ++counters_.particleBufferGrowths;
    }
    const auto bytes=instances.size_bytes();
    if(recreate||activeView_->particleShadow.size()!=instances.size()||std::memcmp(activeView_->particleShadow.data(),instances.data(),bytes)) {
        device_.GetQueue().WriteBuffer(activeView_->particleBuffer,0,instances.data(),bytes);
        activeView_->particleShadow.assign(instances.begin(),instances.end());
        ++counters_.uploadCalls;counters_.uploadBytes+=bytes;++counters_.particleUploadCalls;counters_.particleUploadBytes+=bytes;
    }
    }
    const auto viewport=activeCamera_->pixelViewport(float(width_),float(height_));
    const auto right=activeCamera_->right();const auto up=normalized(cross(right,activeCamera_->forward()));
    const ParticleCamera camera{activeCamera_->viewProjection(viewport.width/viewport.height),{right.x,right.y,right.z,0},{up.x,up.y,up.z,0},activeView_->fog,
        activeCamera_->inverseViewProjection(viewport.width/viewport.height),{viewport.x,viewport.y,viewport.width,viewport.height},
        {activeCamera_->forward().x,activeCamera_->forward().y,activeCamera_->forward().z,0}};
    if(!activeView_->particleCameraValid||std::memcmp(activeView_->particleCameraShadow.data(),&camera,sizeof(camera))) {
        device_.GetQueue().WriteBuffer(activeView_->particleCameraBuffer,0,&camera,sizeof(camera));
        std::memcpy(activeView_->particleCameraShadow.data(),&camera,sizeof(camera));activeView_->particleCameraValid=true;
        ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(camera);
    }
}
void WebGpuRenderer::resize(std::uint32_t width,std::uint32_t height) {
    if(hdr_!=quality_.post.enabled) {
        hdr_=quality_.post.enabled;materialPipelines_.clear();
        wgpu::ShaderSourceWGSL code{};const auto source=hdrShader(mesh_shader,hdr_);code.code=source.c_str();
        wgpu::ShaderModuleDescriptor shader{};shader.nextInChain=&code;meshShader_=device_.CreateShaderModule(&shader);
        createFxPipelines();createClearPipeline();createSkyPipeline();createWaterPipelines();
        underwaterPipeline_=nullptr;reflectionWidth_=reflectionHeight_=0;
    }
    if(bloomTexture_)bloomTexture_.Destroy();bloomTexture_=nullptr;bloomView_=nullptr;postWidth_=postHeight_=0;

    const auto samples=resolveMsaaSamples(quality_,capabilities_);
    if(samples!=sampleCount_) {
        sampleCount_=samples;materialPipelines_.clear();
        // Keep binding layouts stable so retained per-view bindings stay compatible.
        createFxPipelines();createClearPipeline();createSkyPipeline();
    }
    waterSamples_=0;captureBinding_=nullptr;
    width_=width; height_=height;
    wgpu::SurfaceConfiguration config{}; config.device=device_; config.format=format_;
    config.width=width; config.height=height; config.usage=host_.surfaceUsage();
    config.presentMode=wgpu::PresentMode::Fifo;
    config.alphaMode=wgpu::CompositeAlphaMode::Opaque; surface_.Configure(&config);
    if(depth_) depth_.Destroy();
    if(msaaColor_) msaaColor_.Destroy();
    msaaColor_=nullptr;msaaColorView_=nullptr;
    wgpu::TextureDescriptor desc{}; desc.label="Layer depth buffer"; desc.size={width,height,1};
    desc.format=wgpu::TextureFormat::Depth24Plus; desc.usage=wgpu::TextureUsage::RenderAttachment|wgpu::TextureUsage::TextureBinding;
    desc.sampleCount=sampleCount_;
    depth_=device_.CreateTexture(&desc); depthView_=depth_.CreateView();
    if(sampleCount_>1) {
        desc.label="Multisampled compositor color";desc.format=colorFormat();
        msaaColor_=device_.CreateTexture(&desc);msaaColorView_=msaaColor_.CreateView();
    }
}
void WebGpuRenderer::reserveObjects(std::size_t count) {
    if(count<=activeView_->objectCapacity) return;
    wgpu::Limits limits{};
    if(device_.GetLimits(&limits)!=wgpu::Status::Success) throw std::runtime_error("Cannot query GPU instance-buffer limit");
    const auto maxCapacity=std::min<std::uint64_t>(limits.maxBufferSize/objectStride,std::numeric_limits<std::size_t>::max()/objectStride);
    if(count>maxCapacity) throw std::length_error("Instance data exceeds the GPU buffer-size limit");
    activeView_->objectCapacity=static_cast<std::size_t>(std::min<std::uint64_t>(std::max<std::uint64_t>(count,std::uint64_t(activeView_->objectCapacity)*2+16),maxCapacity));
    if(activeView_->objectBuffer) activeView_->objectBuffer.Destroy();
    activeView_->objectBuffer=makeBuffer(device_,activeView_->objectCapacity*objectStride,wgpu::BufferUsage::Vertex,"Per-instance transforms / normals / tint");
    ++counters_.objectBufferGrowths;
    activeView_->frame.invalidateObjects();
}
void WebGpuRenderer::admitUpload(std::uint64_t bytes) {
    if(!uploadAdmission_.admit(bytes,host_.nowMs()-uploadStarted_)) throw UploadDeferred{};
}
WebGpuRenderer::MaterialKey WebGpuRenderer::materialKey(const Material& m) const {
    const auto w=waterParameters(m.water);
    const auto textures=materialTextures(m);
    MaterialKey result{{textures[0].get(),textures[1].get(),textures[2].get(),textures[3].get(),textures[4].get(),textures[5].get(),textures[6].get(),textures[7].get(),textures[8].get()},
        {m.roughness,m.metallic,m.normalScale,m.occlusionStrength,m.emissive.x,m.emissive.y,m.emissive.z,m.alphaCutoff,m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y,
            w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10],w[11],w[12],w[13],w[14],w[15],w[16],w[17],w[18],w[19],w[20],w[21],w[22],w[23],w[24],w[25],w[26],w[27],w[28],w[29],w[30],w[31],w[32],w[33],w[34],w[35],w[36],w[37]},
        std::uint32_t(m.alphaMode)|(std::uint32_t(m.shader)<<2)|(std::uint32_t(m.doubleSided)<<4)|(std::uint32_t(bool(m.normalTexture)&&!m.layers.enabled)<<5)};
    const auto layers=materialLayerParameters(m.layers);std::copy(layers.begin(),layers.end(),result.parameters.begin()+50);return result;
}
bool WebGpuRenderer::resourcesReady(const MeshDraw& draw) const {
    if(!meshes_.contains(draw.mesh.get())||!materials_.contains(materialKey(draw.material))) return false;
    if(draw.skinPose) {
        const auto it=skeletons_.find(draw.skinPose.get());
        if(it==skeletons_.end()||it->second.capacity<draw.skinPose->bones.size()||it->second.revision!=draw.skinPose->revision) return false;
    }
    if(draw.morphPose) {
        const auto it=morphs_.find(draw.morphPose.get());
        if(it==morphs_.end()||it->second.capacity<draw.morphPose->vertices.size()*sizeof(Vertex)||it->second.revision!=draw.morphPose->revision) return false;
    }
    return true;
}
WebGpuRenderer::GpuMesh& WebGpuRenderer::upload(const std::shared_ptr<const Mesh>& mesh) {
    if(const auto cached=meshes_.find(mesh.get());cached!=meshes_.end()) return cached->second;
    ProfileScope uploadProfile(ProfileCategory::Loading,"GPU resource upload");
    const auto resourceStart=host_.nowMs();
    bool shortIndices=true;
    for(auto index:mesh->indices) if(index>65535) { shortIndices=false;break; }
    admitUpload(mesh->vertices.size()*sizeof(Vertex)+((mesh->indices.size()*(shortIndices?2:4)+3)&~std::size_t(3))+mesh->skin.size()*sizeof(SkinVertex));
    const auto packed=packMeshIndices(*mesh);
    auto [it,inserted]=meshes_.try_emplace(mesh.get());
    if(inserted) {
        auto& gpu=it->second; gpu.source=mesh; gpu.indexCount=static_cast<std::uint32_t>(mesh->indices.size());
        gpu.indexFormat=packed.useUint16?wgpu::IndexFormat::Uint16:wgpu::IndexFormat::Uint32;
        gpu.bytes=mesh->vertices.size()*sizeof(Vertex)+packed.bytes.size();
        gpu.vertices=makeBuffer(device_,mesh->vertices.size()*sizeof(Vertex),wgpu::BufferUsage::Vertex,"Shared mesh vertices");
        gpu.indices=makeBuffer(device_,packed.bytes.size(),wgpu::BufferUsage::Index,"Shared mesh indices");
        device_.GetQueue().WriteBuffer(gpu.vertices,0,mesh->vertices.data(),mesh->vertices.size()*sizeof(Vertex));
        device_.GetQueue().WriteBuffer(gpu.indices,0,packed.bytes.data(),packed.bytes.size());
        counters_.uploadCalls+=2;counters_.uploadBytes+=gpu.bytes;++counters_.newMeshes;
        if(!mesh->skin.empty()) {
            const auto bytes=mesh->skin.size()*sizeof(SkinVertex);
            gpu.skin=makeBuffer(device_,bytes,wgpu::BufferUsage::Vertex,"Shared skin joints / weights");
            device_.GetQueue().WriteBuffer(gpu.skin,0,mesh->skin.data(),bytes);
            gpu.bytes+=bytes;++counters_.uploadCalls;counters_.uploadBytes+=bytes;
        }
        resourceMs_+=host_.nowMs()-resourceStart;
    }
    return meshes_.at(mesh.get());
}
WebGpuRenderer::GpuTexture& WebGpuRenderer::uploadTexture(const std::shared_ptr<const Texture>& texture,TextureSemantic semantic) {
    const auto key=std::make_pair(texture.get(),semantic);
    if(const auto cached=textures_.find(key);cached!=textures_.end()) return cached->second;
    const auto started=host_.nowMs();
    if(!texture->width||!texture->height||texture->width>4096||texture->height>4096||texture->pixels.size()!=std::uint64_t(texture->width)*texture->height*4)
        throw std::invalid_argument("Invalid RGBA texture fallback");
    const CompressedTexture* compressed=nullptr;
    for(const auto& variant:texture->compressed) {
        variant.validate(texture->width,texture->height);
        const bool supported=variant.format==TextureCompression::BC7 ? capabilities_.compressionBC :
            variant.format==TextureCompression::ETC2RGBA ? capabilities_.compressionETC2 : capabilities_.compressionASTC;
        if(!compressed&&quality_.compressedTextures&&supported&&variant.semantic==semantic) compressed=&variant;
    }
    std::uint64_t uploadBytes=0;
    if(compressed) for(const auto& level:compressed->levels) uploadBytes+=level.pixels.size();
    else for(auto w=texture->width,h=texture->height;;w=std::max(w/2,1u),h=std::max(h/2,1u)) {
        uploadBytes+=std::uint64_t(w)*h*4;if(w==1&&h==1)break;
    }
    admitUpload(uploadBytes);
    const auto fallback=compressed ? std::vector<TextureMip>{} : texture->mipChain(semantic);
    const auto& levels=compressed ? compressed->levels : fallback;
    GpuTexture gpu;gpu.source=texture;gpu.compressed=compressed!=nullptr;
    wgpu::TextureDescriptor desc{};desc.label="Shared semantic texture";desc.size={texture->width,texture->height,1};desc.mipLevelCount=levels.size();
    desc.format=semantic==TextureSemantic::Color ? wgpu::TextureFormat::RGBA8UnormSrgb : wgpu::TextureFormat::RGBA8Unorm;
    if(compressed) {
        const bool color=semantic==TextureSemantic::Color;
        switch(compressed->format) {
        case TextureCompression::BC7: desc.format=color ? wgpu::TextureFormat::BC7RGBAUnormSrgb : wgpu::TextureFormat::BC7RGBAUnorm;break;
        case TextureCompression::ETC2RGBA: desc.format=color ? wgpu::TextureFormat::ETC2RGBA8UnormSrgb : wgpu::TextureFormat::ETC2RGBA8Unorm;break;
        case TextureCompression::ASTC4x4: desc.format=color ? wgpu::TextureFormat::ASTC4x4UnormSrgb : wgpu::TextureFormat::ASTC4x4Unorm;break;
        }
    }
    desc.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::CopyDst;gpu.image=device_.CreateTexture(&desc);gpu.view=gpu.image.CreateView();
    for(std::uint32_t i=0;i<levels.size();++i) {
        const auto& level=levels[i];wgpu::TexelCopyTextureInfo dest{};dest.texture=gpu.image;dest.mipLevel=i;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow=compressed ? ((level.width+3)/4)*16 : level.width*4;
        layout.rowsPerImage=compressed ? (level.height+3)/4 : level.height;
        const wgpu::Extent3D extent{compressed ? ((level.width+3)/4)*4 : level.width,compressed ? ((level.height+3)/4)*4 : level.height,1};
        device_.GetQueue().WriteTexture(&dest,level.pixels.data(),level.pixels.size(),&layout,&extent);
        ++counters_.uploadCalls;counters_.uploadBytes+=level.pixels.size();gpu.bytes+=level.pixels.size();
    }
    wgpu::SamplerDescriptor sampler{};sampler.addressModeU=texture->wrapU==TextureWrap::Clamp ? wgpu::AddressMode::ClampToEdge : wgpu::AddressMode::Repeat;
    sampler.addressModeV=texture->wrapV==TextureWrap::Clamp ? wgpu::AddressMode::ClampToEdge : wgpu::AddressMode::Repeat;
    sampler.magFilter=sampler.minFilter=wgpu::FilterMode::Linear;sampler.mipmapFilter=wgpu::MipmapFilterMode::Linear;gpu.sampler=device_.CreateSampler(&sampler);
    if(semantic==TextureSemantic::Color) {
        std::array<wgpu::BindGroupEntry,2> entries{};entries[0].binding=0;entries[0].sampler=gpu.sampler;entries[1].binding=1;entries[1].textureView=gpu.view;
        wgpu::BindGroupDescriptor group{};group.layout=textureLayout_;group.entryCount=2;group.entries=entries.data();gpu.binding=device_.CreateBindGroup(&group);
    }
    ++counters_.newTextures;resourceMs_+=host_.nowMs()-started;return textures_.emplace(key,std::move(gpu)).first->second;
}
std::size_t WebGpuRenderer::MaterialKeyHash::operator()(const MaterialKey& key) const {
    std::size_t h=key.flags;
    auto mix=[&](std::size_t v) { h^=v+0x9e3779b9+(h<<6)+(h>>2); };
    for(auto t:key.textures) mix(std::hash<const Texture*>{}(t));
    for(auto v:key.parameters) mix(std::hash<float>{}(v));return h;
}
WebGpuRenderer::GpuMaterial& WebGpuRenderer::uploadMaterial(const Material& m) {
    const auto sources=materialTextures(m);
    const auto key=materialKey(m);
    if(auto it=materials_.find(key);it!=materials_.end()) { it->second.used=frameSerial_;return it->second; }
    const auto semantics=materialTextureSemantics(m);
    for(std::uint32_t i=0;i<9;++i) uploadTexture(sources[i],semantics[i]);
    admitUpload(400);
    const auto started=host_.nowMs();const auto previousResourceMs=resourceMs_;GpuMaterial gpu;gpu.sources=sources;gpu.used=frameSerial_;
    const auto w=waterParameters(m.water);
    std::array<Float4,25> data={Float4{m.roughness,m.metallic,m.normalScale,m.occlusionStrength},
        Float4{m.emissive.x,m.emissive.y,m.emissive.z,m.alphaCutoff},Float4{m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y},
        Float4{float(m.alphaMode),float(m.shader),float(m.doubleSided),float(bool(m.normalTexture)&&!m.layers.enabled)},
        Float4{w[0],w[1],w[2],w[3]},Float4{w[4],w[5],w[6],w[7]},Float4{w[8],w[9],w[10],w[11]},Float4{w[12],w[13],w[14],w[15]},Float4{w[16],w[17],w[18],w[19]},Float4{w[20],w[21],w[22],w[23]},Float4{w[24],w[25],w[26],w[27]}};
    const auto layers=materialLayerParameters(m.layers);
    for(std::size_t i=0;i<14;++i) data[11+i]={layers[i*4],layers[i*4+1],layers[i*4+2],layers[i*4+3]};
    gpu.buffer=makeBuffer(device_,sizeof(data),wgpu::BufferUsage::Uniform,"Material parameters");device_.GetQueue().WriteBuffer(gpu.buffer,0,data.data(),sizeof(data));
    ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(data);
    ++counters_.materialUploadCalls;counters_.materialUploadBytes+=sizeof(data);
    std::array<wgpu::BindGroupEntry,19> entries{};entries[0].binding=0;entries[0].buffer=gpu.buffer;entries[0].size=sizeof(data);
    for(std::uint32_t i=0;i<9;++i) {
        auto& t=uploadTexture(sources[i],semantics[i]);entries[1+i*2].binding=1+i*2;entries[1+i*2].sampler=t.sampler;
        entries[2+i*2].binding=2+i*2;entries[2+i*2].textureView=t.view;
    }
    wgpu::BindGroupDescriptor group{};group.layout=materialLayout_;group.entryCount=entries.size();group.entries=entries.data();gpu.binding=device_.CreateBindGroup(&group);
    resourceMs_=previousResourceMs+host_.nowMs()-started;return materials_.emplace(key,std::move(gpu)).first->second;
}
void WebGpuRenderer::uploadEnvironment() {
    const auto source=scene_->environment ? scene_->environment : Environment::studio();
    if(source==environment_) return;source->validate();
    std::uint64_t bytes=(source->diffuse.pixels.size()+source->brdf.size())*8;
    for(const auto& level:source->specular) bytes+=level.pixels.size()*8;
    admitUpload(bytes);
    const auto started=host_.nowMs();environmentBytes_=0;
    auto upload=[&](std::span<const EnvironmentLevel> levels,wgpu::Texture& texture,wgpu::TextureView& view) {
        if(texture) texture.Destroy();wgpu::TextureDescriptor desc{};desc.size={levels[0].size,levels[0].size,6};desc.mipLevelCount=levels.size();
        desc.format=wgpu::TextureFormat::RGBA16Float;desc.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::CopyDst;texture=device_.CreateTexture(&desc);
        for(std::uint32_t mip=0;mip<levels.size();++mip) {
            const auto& level=levels[mip];std::vector<std::uint16_t> pixels(level.pixels.size()*4);
            for(std::size_t i=0;i<level.pixels.size();++i) for(int c=0;c<4;++c) pixels[i*4+c]=floatToHalf(level.pixels[i][c]);
            wgpu::TexelCopyTextureInfo dest{};dest.texture=texture;dest.mipLevel=mip;wgpu::TexelCopyBufferLayout layout{};layout.bytesPerRow=level.size*8;layout.rowsPerImage=level.size;
            const wgpu::Extent3D extent{level.size,level.size,6};const auto bytes=pixels.size()*2;device_.GetQueue().WriteTexture(&dest,pixels.data(),bytes,&layout,&extent);
            ++counters_.uploadCalls;counters_.uploadBytes+=bytes;environmentBytes_+=bytes;
        }
        wgpu::TextureViewDescriptor vd{};vd.dimension=wgpu::TextureViewDimension::Cube;vd.arrayLayerCount=6;view=texture.CreateView(&vd);
    };
    upload(source->specular,specularEnvironment_,specularView_);upload({&source->diffuse,1},diffuseEnvironment_,diffuseView_);
    if(brdfLut_) brdfLut_.Destroy();wgpu::TextureDescriptor desc{};desc.size={source->lutSize,source->lutSize,1};desc.format=wgpu::TextureFormat::RGBA16Float;
    desc.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::CopyDst;brdfLut_=device_.CreateTexture(&desc);brdfView_=brdfLut_.CreateView();
    std::vector<std::uint16_t> pixels(source->brdf.size()*4);for(std::size_t i=0;i<source->brdf.size();++i) for(int c=0;c<4;++c) pixels[i*4+c]=floatToHalf(source->brdf[i][c]);
    wgpu::TexelCopyTextureInfo dest{};dest.texture=brdfLut_;wgpu::TexelCopyBufferLayout layout{};layout.bytesPerRow=source->lutSize*8;layout.rowsPerImage=source->lutSize;
    const wgpu::Extent3D extent{source->lutSize,source->lutSize,1};device_.GetQueue().WriteTexture(&dest,pixels.data(),pixels.size()*2,&layout,&extent);
    ++counters_.uploadCalls;counters_.uploadBytes+=pixels.size()*2;environmentBytes_+=pixels.size()*2;
    wgpu::SamplerDescriptor sampler{};sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=wgpu::AddressMode::ClampToEdge;
    sampler.minFilter=sampler.magFilter=wgpu::FilterMode::Linear;sampler.mipmapFilter=wgpu::MipmapFilterMode::Linear;environmentSampler_=device_.CreateSampler(&sampler);
    environment_=source;counters_.newTextures+=3;resourceMs_+=host_.nowMs()-started;
}
void WebGpuRenderer::uploadMorph(const std::shared_ptr<const MorphPose>& pose) {
    if(pose->vertices.empty()) throw std::invalid_argument("Empty morph vertex buffer");
    const auto bytes=pose->vertices.size()*sizeof(Vertex);
    const auto cached=morphs_.find(pose.get());
    if(cached==morphs_.end()||bytes>cached->second.capacity) admitUpload(bytes);
    auto& gpu=morphs_[pose.get()];
    const bool recreate=gpu.source.lock()!=pose || bytes>gpu.capacity;
    if(recreate) {
        if(gpu.vertices) gpu.vertices.Destroy();
        gpu.vertices=makeBuffer(device_,bytes,wgpu::BufferUsage::Vertex,"Morphed vertices before skinning");gpu.capacity=bytes;
    }
    if(recreate||gpu.revision!=pose->revision) {
        device_.GetQueue().WriteBuffer(gpu.vertices,0,pose->vertices.data(),bytes);
        ++counters_.uploadCalls;counters_.uploadBytes+=bytes;gpu.revision=pose->revision;
    }
    gpu.source=pose;
}
void WebGpuRenderer::uploadSkeleton(const std::shared_ptr<const SkinPose>& pose) {
    if(pose->bones.empty() || pose->bones.size()>1024) throw std::invalid_argument("Invalid GPU bone palette");
    const auto bytes=pose->bones.size()*sizeof(BoneUniform);
    const auto cached=skeletons_.find(pose.get());
    if(cached==skeletons_.end()||pose->bones.size()>cached->second.capacity) admitUpload(bytes);
    auto& gpu=skeletons_[pose.get()];
    const bool recreate=pose->bones.size()>gpu.capacity;
    if(recreate) {
        if(gpu.buffer) gpu.buffer.Destroy();gpu.capacity=pose->bones.size();
        gpu.buffer=makeBuffer(device_,bytes,wgpu::BufferUsage::Storage,"Bone transforms / normals");
        wgpu::BindGroupEntry entry{};entry.binding=0;entry.buffer=gpu.buffer;entry.size=bytes;
        wgpu::BindGroupDescriptor desc{};desc.layout=skinLayout_;desc.entryCount=1;desc.entries=&entry;
        gpu.binding=device_.CreateBindGroup(&desc);
    }
    if(recreate || gpu.revision!=pose->revision) {
        device_.GetQueue().WriteBuffer(gpu.buffer,0,pose->bones.data(),bytes);
        ++counters_.uploadCalls;counters_.uploadBytes+=bytes;
        ++counters_.boneUploadCalls;counters_.boneUploadBytes+=bytes;gpu.revision=pose->revision;
    }
    gpu.source=pose;
}
void WebGpuRenderer::render(double updateMs,double intervalMs) {
    ProfileScope profile(ProfileCategory::Render,"WebGPU render");
    if(!ready_) return;
    try {
        const auto started=host_.nowMs();FrameTiming timing{};timing.updateMs=updateMs;timing.intervalMs=intervalMs;counters_={};resourceMs_=0;++frameSerial_;
        uploadAdmission_.begin(uploadBudget_);uploadStarted_=started;
        resizeCanvas();
        int width=0,height=0; host_.drawableSize(width,height);
        if(width<=0 || height<=0) return;
        if(static_cast<std::uint32_t>(width)!=width_ || static_cast<std::uint32_t>(height)!=height_ ||
           (sampleCount_!=resolveMsaaSamples(quality_,capabilities_)||hdr_!=quality_.post.enabled)) resize(width,height);
        wgpu::SurfaceTexture backbuffer{};surface_.GetCurrentTexture(&backbuffer);
        if(!backbuffer.texture) {
            if(backbuffer.status==wgpu::SurfaceGetCurrentTextureStatus::Lost) deviceLost("Canvas surface lost");
            else if(backbuffer.status==wgpu::SurfaceGetCurrentTextureStatus::Error) fail("Cannot acquire canvas surface");
            else if(backbuffer.status==wgpu::SurfaceGetCurrentTextureStatus::Outdated) width_=height_=0;
            return;
        }
        if(shadowResourcesDirty_) createShadowResources();
        const auto canvasView=backbuffer.texture.CreateView();
        views_.resolve(width_,height_,resolvedViews_);
        std::uint32_t waterMask=0;for(const auto& resolved:resolvedViews_) waterMask|=views_.views()[resolved.index].camera->visibilityMask();
        underwaterViews_.resize(resolvedViews_.size());bool submerged=false;
        for(std::size_t i=0;i<resolvedViews_.size();++i) {const auto& camera=*views_.views()[resolvedViews_[i].index].camera;
            underwaterViews_[i]=underwaterView(*scene_,camera.worldPosition(),camera.visibilityMask());submerged|=bool(underwaterViews_[i]);}
        prepareWaterTargets(hdr_||submerged||waterSceneInputsRequired(*scene_,waterMask)||softParticlesRequired(*scene_,waterMask));
        prepareReflectionTargets(waterPlanarReflectionRequired(*scene_,waterMask));
        const auto view=sampleCount_>1 ? msaaColorView_ : waterCapture_ ? sceneColorView_ : canvasView;
        auto encoder=device_.CreateCommandEncoder();
        while(viewResources_.size()<views_.views().size()) viewResources_.push_back(std::make_unique<ViewResources>());
        const auto sampleSlot=beginGpuSample(started);
        std::uint32_t viewOrdinal=0;
        bool targetInitialized=false;
        try { uploadEnvironment(); } catch(const UploadDeferred&) { ++counters_.deferredAssetUploads; }
        for(const auto& resolved:resolvedViews_) {
            activeView_=viewResources_[resolved.index].get();
            activeCamera_=views_.views()[resolved.index].camera;
            const auto viewport=resolved.viewport;
            const auto collectStart=host_.nowMs();
            const auto* frustum=frustumCullingEnabled_ && activeCamera_->active() ? &activeCamera_->frustum(viewport.width/viewport.height) : nullptr;
            const DetailView detail{activeCamera_->viewProjection(viewport.width/viewport.height),viewport.width*quality_.detailScale,viewport.height*quality_.detailScale};
            activeView_->frame.collect(*scene_,instancingEnabled_,activeCamera_->active() ? activeCamera_->visibilityMask() : 0,frustum,lodEnabled_ ? &detail : nullptr);
            const auto& frame=activeView_->frame.sceneFrame();timing.collectMs+=host_.nowMs()-collectStart;
            const auto prepareStart=host_.nowMs();
            activeView_->fxFrame.prepare(frame,activeCamera_->view());
            for(auto it=meshes_.begin();it!=meshes_.end();) {
                if(it->second.source.expired()) it=meshes_.erase(it); else ++it;
            }
            for(auto it=textures_.begin();it!=textures_.end();) {
                if(it->second.source.expired()) it=textures_.erase(it); else ++it;
            }
            for(auto it=skeletons_.begin();it!=skeletons_.end();) {
                if(it->second.source.expired()) it=skeletons_.erase(it);else ++it;
            }
            for(auto it=morphs_.begin();it!=morphs_.end();) {
                if(it->second.source.expired()) it=morphs_.erase(it);else ++it;
            }
            prepareViewBindings(views_.views()[resolved.index]);
            reserveObjects(std::max<std::size_t>(frame.meshes.size(),1));
            FrameUniform uniforms{};uniforms.fog=prepareFog(scene_->fog(),*activeCamera_,views_.views()[resolved.index].fogEnabled);activeView_->fog=uniforms.fog; uniforms.waterClock.x=std::any_of(frame.meshes.begin(),frame.meshes.end(),[](const MeshDraw& draw){return draw.material.shader==MaterialShader::Water;}) ? float(waterTime_) : 0;uniforms.viewProjection=activeCamera_->viewProjection(viewport.width/viewport.height);
            uniforms.interactions=waterInteractionParameters(waterInteractions_,waterTime_,activeCamera_->visibilityMask());
            uniforms.inverseViewProjection=activeCamera_->inverseViewProjection(viewport.width/viewport.height);
            uniforms.viewport={viewport.x,viewport.y,viewport.width,viewport.height};
            uniforms.extent={float(width_),float(height_),0,0};
            const auto cameraWorld=activeCamera_->worldMatrix();const auto forward=normalized(Vec3{-cameraWorld.m[8],-cameraWorld.m[9],-cameraWorld.m[10]});
            uniforms.viewForward={forward.x,forward.y,forward.z,0};
            const auto camera=activeCamera_->worldPosition();
            const auto reflection=waterReflection(frame.meshes,camera);
            if(reflection) {uniforms.reflectionViewProjection=uniforms.viewProjection*reflection->mirror;
                uniforms.reflectionPlane={reflection->plane[0],reflection->plane[1],reflection->plane[2],reflection->plane[3]};uniforms.reflectionOptions.x=1;}
            if(!std::isfinite(scene_->environmentIntensity)||scene_->environmentIntensity<0||!std::isfinite(scene_->environmentRotation)) throw std::invalid_argument("Invalid environment settings");
            uniforms.cameraEnvironment={camera.x,camera.y,camera.z,scene_->environment ? scene_->environmentIntensity : 0};
            uniforms.environmentOptions={scene_->environmentRotation,float(environment_->specular.size()-1),shadowReceivingEnabled_ ? 0.0f : 1.0f,float(shadowFilterTaps_)};
            const auto count=std::min(frame.lights.size(),maxLights);
            uniforms.ambientCount={scene_->ambient.x,scene_->ambient.y,scene_->ambient.z,static_cast<float>(count)};
            if(frame.lights.size()>maxLights && !lightOverflowReported_) { host_.status("WebGpuRenderer uses first eight enabled lights",false); lightOverflowReported_=true; }
            for(std::size_t i=0;i<count;++i) {
                const auto& l=frame.lights[i];uniforms.lights[i]={{l.position.x,l.position.y,l.position.z,float(l.type)},
                    {l.direction.x,l.direction.y,l.direction.z,l.range},{l.color.x,l.color.y,l.color.z,l.intensity},{std::cos(l.innerAngle),std::cos(l.outerAngle),0,0}};
            }
            const auto plan=shadowsEnabled_?planShadows(frame.lights,*activeCamera_,viewport.width/viewport.height,quality_,shadowResolution_):ShadowPlan{};
            const auto shadowCount=plan.count;
            for(auto& shadow:uniforms.shadows)shadow.options.x=-1;
            for(std::size_t j=0;j<shadowCount;++j) {
                const auto& map=plan.maps[j];const auto& l=frame.lights[map.light];const auto& matrix=map.matrix;
                uniforms.shadows[j]={matrix,{float(j),l.shadow.depthBias,l.shadow.normalBias,1.0f/shadowResolution_},
                    {float(map.light),map.nearDistance,map.farDistance,map.cascade?1.f:0.f}};
                if(!activeView_->shadowMatrixValid[j]||std::memcmp(&activeView_->shadowMatrices[j],&matrix,sizeof(matrix))||activeView_->shadowWaterTime[j]!=uniforms.waterClock.x) {
                    FrameUniform shadowUniform{};shadowUniform.viewProjection=matrix;shadowUniform.waterClock=uniforms.waterClock;
                    device_.GetQueue().WriteBuffer(activeView_->shadowBuffers[j],0,&shadowUniform,sizeof(shadowUniform));
                    activeView_->shadowWaterTime[j]=uniforms.waterClock.x;activeView_->shadowMatrices[j]=matrix;activeView_->shadowMatrixValid[j]=true;
                    ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(shadowUniform);
                }
                activeView_->frame.prepareShadow(j,{matrix,float(shadowResolution_)*quality_.detailScale,float(shadowResolution_)*quality_.detailScale},shadowLodEnabled_,shadowCullingEnabled_,shadowCastingEnabled_);
            }
            if(!activeView_->frameUniformValid || std::memcmp(activeView_->frameUniformShadow.data(),&uniforms,sizeof(uniforms))) {
                device_.GetQueue().WriteBuffer(activeView_->frameBuffer,0,&uniforms,sizeof(uniforms));
                std::memcpy(activeView_->frameUniformShadow.data(),&uniforms,sizeof(uniforms));activeView_->frameUniformValid=true;
                ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(uniforms);
                ++counters_.frameUploadCalls;counters_.frameUploadBytes+=sizeof(uniforms);
            }
            for(std::size_t i=0;i<shadowCount;++i) for(const auto& batch:activeView_->frame.shadowBatches(i)) {
                try { upload(batch.mesh); } catch(const UploadDeferred&) { ++counters_.deferredAssetUploads; }
            }
            activeView_->frame.prepareObjects();
            const auto objects=activeView_->frame.objectData();
            for(const auto& draw:frame.meshes) {
                try {
                    upload(draw.mesh);uploadMaterial(draw.material);
                    if(draw.skinPose) uploadSkeleton(draw.skinPose);
                    if(draw.morphPose) uploadMorph(draw.morphPose);
                } catch(const UploadDeferred&) { ++counters_.deferredAssetUploads; }
            }
            for(const auto& range:activeView_->frame.objectUploads()) {
                device_.GetQueue().WriteBuffer(activeView_->objectBuffer,range.offset,objects.data()+range.offset,range.bytes);
                ++counters_.uploadCalls;counters_.uploadBytes+=range.bytes;
                ++counters_.objectUploadCalls;counters_.objectUploadBytes+=range.bytes;
            }
            prepareFx();counters_.particles+=activeView_->fxFrame.instances().size();
            counters_.meshParticles+=frame.meshParticleCount;counters_.meshes+=frame.meshCount;counters_.lights+=count;
            counters_.culledMeshes+=frame.culledMeshes;counters_.visibleMeshes+=frame.meshCount-frame.culledMeshes;
            counters_.frameStorageGrowths+=activeView_->frame.storageGrowths()+activeView_->fxFrame.storageGrowths();
            counters_.cameraLodParts+=activeView_->frame.cameraLodParts();
            counters_.shadowLodParts+=activeView_->frame.shadowLodParts();
            counters_.shadowCulledParts+=activeView_->frame.shadowCulledParts();
            counters_.changedObjects+=activeView_->frame.changedObjects();
            counters_.localTransformBuilds+=frame.transformRebuilds.localBuilds;counters_.worldTransformBuilds+=frame.transformRebuilds.worldBuilds;counters_.normalTransformBuilds+=frame.transformRebuilds.normalBuilds;
            timing.prepareMs+=host_.nowMs()-prepareStart;
            timing.resourceMs=resourceMs_;
            const auto encodeStart=host_.nowMs();
            if(sampleSlot>=0) timingSlots_[sampleSlot].shadowCounts[viewOrdinal]=shadowCount;
            for(std::size_t i=0;i<shadowCount;++i) {
                wgpu::RenderPassDepthStencilAttachment depth{};depth.view=shadowLayerViews_[i];depth.depthClearValue=1;
                depth.depthLoadOp=wgpu::LoadOp::Clear;depth.depthStoreOp=wgpu::StoreOp::Store;
                wgpu::RenderPassDescriptor desc{};desc.label="Directional shadow map";desc.depthStencilAttachment=&depth;
                wgpu::PassTimestampWrites timestamps{};
                if(sampleSlot>=0) {
                    timestamps.querySet=timingSlots_[sampleSlot].queries;
                    timestamps.beginningOfPassWriteIndex=viewOrdinal*24+6+i*2;
                    timestamps.endOfPassWriteIndex=viewOrdinal*24+7+i*2;
                    desc.timestampWrites=&timestamps;
                }
                auto pass=encoder.BeginRenderPass(&desc);pass.SetBindGroup(0,activeView_->shadowBindings[i]);pass.SetVertexBuffer(1,activeView_->objectBuffer);
                ++counters_.passes;++counters_.shadowPasses;
                for(const auto& shadowBatch:activeView_->frame.shadowBatches(i)) {
                    const auto& batch=shadowBatch.instances;
                    const auto& draw=activeView_->frame.instanceDraw(batch.firstInstance);
                    if(!resourcesReady(draw)||!meshes_.contains(shadowBatch.mesh.get())) { ++counters_.deferredAssetDraws;continue; }
                    const auto& mesh=meshes_.at(shadowBatch.mesh.get());pass.SetPipeline(materialPipeline(draw,true));
                    pass.SetVertexBuffer(0,draw.morphPose ? morphs_.at(draw.morphPose.get()).vertices : mesh.vertices);pass.SetBindGroup(1,uploadMaterial(draw.material).binding);
                    if(draw.skinPose) { pass.SetBindGroup(2,skeletons_.at(draw.skinPose.get()).binding);pass.SetVertexBuffer(2,mesh.skin); }
                    pass.SetIndexBuffer(mesh.indices,mesh.indexFormat);pass.DrawIndexed(shadowBatch.indexCount,batch.instanceCount,shadowBatch.firstIndex,0,batch.firstInstance);
                    ++counters_.drawCalls;++counters_.shadowDrawCalls;counters_.shadowInstances+=batch.instanceCount;
                    counters_.shadowTriangles+=std::uint64_t(shadowBatch.indexCount/3)*batch.instanceCount;
                    counters_.triangles+=std::uint64_t(shadowBatch.indexCount/3)*batch.instanceCount;
                }
                pass.End();
            }
            if(reflection) {
                auto mirrored=uniforms;mirrored.viewProjection=uniforms.reflectionViewProjection;mirrored.reflectionOptions={0,1,0,0};
                const auto eye=transformPoint(reflection->mirror,camera);mirrored.cameraEnvironment.x=eye.x;mirrored.cameraEnvironment.y=eye.y;mirrored.cameraEnvironment.z=eye.z;
                device_.GetQueue().WriteBuffer(activeView_->reflectionBuffer,0,&mirrored,sizeof(mirrored));
                ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(mirrored);
                wgpu::RenderPassColorAttachment color{};color.view=reflectionColorView_;color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;color.clearValue={0,0,0,0};
                wgpu::RenderPassDepthStencilAttachment depth{};depth.view=reflectionDepthView_;depth.depthLoadOp=wgpu::LoadOp::Clear;depth.depthStoreOp=wgpu::StoreOp::Discard;depth.depthClearValue=1;
                wgpu::RenderPassDescriptor desc{};desc.label="Planar water reflection";desc.colorAttachmentCount=1;desc.colorAttachments=&color;desc.depthStencilAttachment=&depth;
                wgpu::PassTimestampWrites times{};if(sampleSlot>=0) {times.querySet=timingSlots_[sampleSlot].queries;times.beginningOfPassWriteIndex=viewOrdinal*24+18;times.endOfPassWriteIndex=viewOrdinal*24+19;desc.timestampWrites=&times;timingSlots_[sampleSlot].reflectionPasses[viewOrdinal]=true;}
                auto pass=encoder.BeginRenderPass(&desc);pass.SetBindGroup(0,activeView_->reflectionBinding);pass.SetVertexBuffer(1,activeView_->objectBuffer);
                pass.SetViewport(viewport.x,viewport.y,viewport.width,viewport.height,0,1);pass.SetScissorRect(resolved.x,resolved.y,resolved.width,resolved.height);
                // Retained off-camera batches allow nearby objects outside the main frustum to reflect.
                for(const auto& batch:activeView_->frame.batches(RenderLayer::World)) {
                    const auto& draw=activeView_->frame.instanceDraw(batch.firstInstance);
                    if(draw.material.shader==MaterialShader::Water) continue;
                    if(!resourcesReady(draw)) {++counters_.deferredAssetDraws;continue;}
                    const auto& mesh=meshes_.at(draw.mesh.get());pass.SetPipeline(materialPipeline(draw,false,true));
                    pass.SetVertexBuffer(0,draw.morphPose ? morphs_.at(draw.morphPose.get()).vertices : mesh.vertices);
                    pass.SetBindGroup(1,uploadMaterial(draw.material).binding);
                    if(draw.skinPose) {pass.SetBindGroup(2,skeletons_.at(draw.skinPose.get()).binding);pass.SetVertexBuffer(2,mesh.skin);}
                    pass.SetIndexBuffer(mesh.indices,mesh.indexFormat);pass.DrawIndexed(draw.indexCount,batch.instanceCount,draw.firstIndex,0,batch.firstInstance);
                    ++counters_.drawCalls;counters_.triangles+=std::uint64_t(draw.indexCount/3)*batch.instanceCount;counters_.submittedInstances+=batch.instanceCount;
                }
                pass.End();++counters_.passes;
            }
            // A color clear for Background, then load previous color for World and Overlay.
            const bool drawSky=prepareSkyBindings(views_.views()[resolved.index],viewport.width/viewport.height);
            // Depth clears between layers, preserving depth testing within each layer.
            constexpr const char* layerNames[]={"Background","World","Overlay"};
            for(std::uint32_t layer=0;layer<static_cast<std::uint32_t>(RenderLayer::Count);++layer) {
                wgpu::RenderPassColorAttachment color{}; color.view=view; color.clearValue={0.018,0.026,0.042,1};
                if(!targetInitialized && layer==0 && views_.views()[resolved.index].clearColor
                    && resolved.x==0 && resolved.y==0 && resolved.width==width_ && resolved.height==height_) {
                    const auto& c=*views_.views()[resolved.index].clearColor;color.clearValue={c[0],c[1],c[2],c[3]};
                }
                color.loadOp=!targetInitialized ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load; color.storeOp=wgpu::StoreOp::Store;
                wgpu::RenderPassDepthStencilAttachment depth{}; depth.view=depthView_; depth.depthClearValue=1;
                depth.depthLoadOp=wgpu::LoadOp::Clear; depth.depthStoreOp=wgpu::StoreOp::Store;
                wgpu::RenderPassDescriptor desc{}; desc.label=layerNames[layer]; desc.colorAttachmentCount=1; desc.colorAttachments=&color; desc.depthStencilAttachment=&depth;
                wgpu::PassTimestampWrites timestamps{};
            if(sampleSlot>=0) { timestamps.querySet=timingSlots_[sampleSlot].queries;timestamps.beginningOfPassWriteIndex=viewOrdinal*24+layer*2;timestamps.endOfPassWriteIndex=viewOrdinal*24+layer*2+1;desc.timestampWrites=&timestamps; }
                auto pass=encoder.BeginRenderPass(&desc);
                pass.SetViewport(viewport.x,viewport.y,viewport.width,viewport.height,0,1);
                pass.SetScissorRect(resolved.x,resolved.y,resolved.width,resolved.height);
                const bool attachmentClear=layer==0 && !targetInitialized && resolved.x==0 && resolved.y==0
                    && resolved.width==width_ && resolved.height==height_;
                if(layer==0 && views_.views()[resolved.index].clearColor && !attachmentClear) {
                    pass.SetPipeline(clearPipeline_);pass.SetBindGroup(0,activeView_->clearBinding);pass.Draw(3);
                    ++counters_.drawCalls;++counters_.triangles;
                }
                targetInitialized=true;
                if(layer==0 && drawSky) {
                    pass.SetBindGroup(0,activeView_->skyBinding);pass.SetBindGroup(1,activeView_->skyTextureBinding);
                    if(scene_->sky()->geometry==SkyGeometry::Sphere) {
                        const auto& mesh=meshes_.at(skySphereMesh().get());pass.SetPipeline(skySpherePipeline_);
                        pass.SetVertexBuffer(0,mesh.vertices);pass.SetIndexBuffer(mesh.indices,mesh.indexFormat);pass.DrawIndexed(mesh.indexCount);
                        counters_.triangles+=mesh.indexCount/3;
                    } else {pass.SetPipeline(skyPipeline_);pass.Draw(3);++counters_.triangles;}
                    ++counters_.drawCalls;
                }
                pass.SetBindGroup(0,activeView_->binding);pass.SetVertexBuffer(1,activeView_->objectBuffer);
                ++counters_.passes;
                auto drawOpaque=[&](const InstanceBatch& batch) {
                    const auto& draw=activeView_->frame.instanceDraw(batch.firstInstance);
                    if(!resourcesReady(draw)) { ++counters_.deferredAssetDraws;return; }
                    const auto& mesh=meshes_.at(draw.mesh.get());
                    const bool skinned=bool(draw.skinPose);
                    pass.SetPipeline(materialPipeline(draw));
                    if(skinned) {
                        pass.SetBindGroup(2,skeletons_.at(draw.skinPose.get()).binding);pass.SetVertexBuffer(2,mesh.skin);
                        counters_.skinnedInstances+=batch.instanceCount;
                    }
                    pass.SetVertexBuffer(0,draw.morphPose ? morphs_.at(draw.morphPose.get()).vertices : mesh.vertices);
                    pass.SetBindGroup(1,uploadMaterial(draw.material).binding);
                    pass.SetIndexBuffer(mesh.indices,mesh.indexFormat); pass.DrawIndexed(draw.indexCount,batch.instanceCount,draw.firstIndex,0,batch.firstInstance);
                    ++counters_.drawCalls;counters_.triangles+=std::uint64_t(draw.indexCount/3)*batch.instanceCount;
                    counters_.submittedInstances+=batch.instanceCount;
                    counters_.instancedDrawCalls+=batch.instanceCount>1;
                    counters_.maxInstancesPerDraw=std::max<std::uint64_t>(counters_.maxInstancesPerDraw,batch.instanceCount);
                };
                const bool splitWater=waterCapture_&&layer==std::uint32_t(RenderLayer::World)
                    &&std::any_of(frame.meshes.begin(),frame.meshes.end(),[](const MeshDraw& draw){return draw.cameraVisible&&draw.layer==RenderLayer::World&&draw.material.shader==MaterialShader::Water&&draw.material.water.usesSceneInputs();});
                const auto batches=activeView_->frame.cameraBatches(static_cast<RenderLayer>(layer));
                for(const auto& batch:batches) {
                    const auto& draw=activeView_->frame.instanceDraw(batch.firstInstance);
                    if(!splitWater||draw.material.shader!=MaterialShader::Water) drawOpaque(batch);
                }
                const bool softDepth=(layer==std::uint32_t(RenderLayer::World)&&std::any_of(frame.particles.begin(),frame.particles.end(),[](const ParticleDraw& p){return p.softDistance>0;}))
                    ||(layer==std::uint32_t(RenderLayer::World)&&std::any_of(frame.ribbons.begin(),frame.ribbons.end(),[](const RibbonDraw& r){return r.softDistance>0;}));
                if(splitWater||softDepth) {
                    pass.End();captureWater(encoder,sampleSlot,viewOrdinal);
                    color.loadOp=wgpu::LoadOp::Load;depth.depthLoadOp=wgpu::LoadOp::Load;desc.label="World water / transparency";
                    if(sampleSlot>=0) {timestamps.beginningOfPassWriteIndex=viewOrdinal*24+16;timestamps.endOfPassWriteIndex=viewOrdinal*24+17;}
                    pass=encoder.BeginRenderPass(&desc);pass.SetViewport(viewport.x,viewport.y,viewport.width,viewport.height,0,1);
                    pass.SetScissorRect(resolved.x,resolved.y,resolved.width,resolved.height);
                    pass.SetBindGroup(0,activeView_->binding);pass.SetVertexBuffer(1,activeView_->objectBuffer);++counters_.passes;
                    for(const auto& batch:batches) if(splitWater&&activeView_->frame.instanceDraw(batch.firstInstance).material.shader==MaterialShader::Water) drawOpaque(batch);
                }
                // FX stay inside their layer pass: world depth is preserved until Overlay.
                for(const auto& batch:activeView_->fxFrame.batches(static_cast<RenderLayer>(layer))) {
                    const auto blend=batch.blend==BlendMode::Additive ? 1 : 0;
                    if(batch.ribbon){
                        if(!textures_.contains({batch.texture.get(),TextureSemantic::Color})){++counters_.deferredAssetDraws;continue;}
                        pass.SetPipeline(ribbonPipelines_[blend]);pass.SetBindGroup(0,activeView_->particleBinding);
                        pass.SetVertexBuffer(0,activeView_->ribbonBuffer);pass.SetBindGroup(1,textures_.at({batch.texture.get(),TextureSemantic::Color}).binding);
                        pass.Draw(batch.count,1,batch.first);++counters_.trailDrawCalls;counters_.trailSegments+=batch.count/6;counters_.triangles+=batch.count/3;
                    } else if(batch.particle) {
                        if(!textures_.contains({batch.texture.get(),TextureSemantic::Color})) { ++counters_.deferredAssetDraws;continue; }
                        pass.SetPipeline(particlePipelines_[blend]);pass.SetBindGroup(0,activeView_->particleBinding);
                        pass.SetVertexBuffer(0,activeView_->particleBuffer);pass.SetBindGroup(1,textures_.at({batch.texture.get(),TextureSemantic::Color}).binding);
                        pass.Draw(6,batch.count,0,batch.first);
                        ++counters_.particleDrawCalls;counters_.triangles+=std::uint64_t(batch.count)*2;
                        counters_.submittedInstances+=batch.count;counters_.instancedDrawCalls+=batch.count>1;
                        counters_.maxInstancesPerDraw=std::max<std::uint64_t>(counters_.maxInstancesPerDraw,batch.count);
                    } else {
                        const auto& draw=frame.meshes[batch.first];
                        if(!resourcesReady(draw)) { ++counters_.deferredAssetDraws;continue; }
                        const auto& mesh=meshes_.at(draw.mesh.get());
                        const bool skinned=bool(draw.skinPose);
                        pass.SetPipeline(materialPipeline(draw));
                        pass.SetBindGroup(0,activeView_->binding);pass.SetVertexBuffer(0,draw.morphPose ? morphs_.at(draw.morphPose.get()).vertices : mesh.vertices);pass.SetVertexBuffer(1,activeView_->objectBuffer);
                        pass.SetBindGroup(1,uploadMaterial(draw.material).binding);
                        if(skinned) { pass.SetBindGroup(2,skeletons_.at(draw.skinPose.get()).binding);pass.SetVertexBuffer(2,mesh.skin);++counters_.skinnedInstances; }
                        pass.SetIndexBuffer(mesh.indices,mesh.indexFormat);pass.DrawIndexed(draw.indexCount,1,draw.firstIndex,0,batch.first);
                        counters_.triangles+=draw.indexCount/3;++counters_.submittedInstances;++counters_.transparentMeshDraws;
                    }
                    ++counters_.drawCalls;
                }
                pass.End();
                if(layer==std::uint32_t(RenderLayer::World)&&underwaterViews_[viewOrdinal]) {
                    captureWater(encoder,sampleSlot,viewOrdinal,true);
                    drawUnderwater(encoder,view,*underwaterViews_[viewOrdinal],sampleSlot,viewOrdinal);
                }
            }
            timing.encodeMs+=host_.nowMs()-encodeStart;
            ++viewOrdinal;
        } // Ordered views all load the same color target and start fresh depth per layer.
        activeView_=nullptr;activeCamera_=nullptr;
        if(!targetInitialized) {
            wgpu::RenderPassColorAttachment color{};color.view=view;color.clearValue={0.018,0.026,0.042,1};
            color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;
            wgpu::RenderPassDescriptor desc{};desc.label="Empty compositor";desc.colorAttachmentCount=1;desc.colorAttachments=&color;
            auto pass=encoder.BeginRenderPass(&desc);pass.End();++counters_.passes;
        }
        if(sampleCount_>1) {
            // Resolve accumulated color once, after every view/layer and before the GUI.
            wgpu::RenderPassColorAttachment color{};color.view=msaaColorView_;color.resolveTarget=hdr_?sceneColorView_:canvasView;
            color.loadOp=wgpu::LoadOp::Load;color.storeOp=wgpu::StoreOp::Discard;
            wgpu::RenderPassDescriptor desc{};desc.label="MSAA compositor resolve";desc.colorAttachmentCount=1;desc.colorAttachments=&color;
            auto pass=encoder.BeginRenderPass(&desc);pass.End();++counters_.passes;
        }
        for(auto it=materials_.begin();it!=materials_.end();) {
            if(it->second.used!=frameSerial_) { it->second.buffer.Destroy();it=materials_.erase(it); } else ++it;
        }
        counters_.assetUploadBytes=uploadAdmission_.bytes();counters_.assetUploadResources=uploadAdmission_.resources();
        counters_.cachedMeshes=meshes_.size();counters_.cachedTextures=textures_.size()+3;counters_.cachedMaterials=materials_.size();
        counters_.shadowMapResolution=shadowResolution_;counters_.shadowFilterTaps=shadowFilterTaps_;
        counters_.width=width_;counters_.height=height_;counters_.gpuDepthBytes=std::uint64_t(width_)*height_*4*sampleCount_+std::uint64_t(shadowResolution_)*shadowResolution_*std::max(1u,quality_.maxShadowMaps)*4;
        if(msaaColor_) counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*(hdr_?8:4)*sampleCount_;
        if(gpuTimingEnabled_) for(const auto& slot:timingSlots_) counters_.gpuBufferBytes+=slot.queryCount*16;
        counters_.cachedSkeletons=skeletons_.size();
        for(const auto& [key,gpu]:skeletons_) { (void)key;counters_.gpuSkinBytes+=gpu.capacity*sizeof(BoneUniform); }
        if(reflectionWidth_>1) {counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*(hdr_?8:4);counters_.gpuDepthBytes+=std::uint64_t(width_)*height_*4;}
        if(waterCapture_) counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*(hdr_?20:(sampleCount_==1 ? 12 : 8));
        counters_.gpuBufferBytes+=counters_.gpuSkinBytes+materials_.size()*400;
        for(const auto& [key,gpu]:morphs_) {
            (void)key;counters_.gpuMeshBytes+=gpu.capacity;
            if(auto pose=gpu.source.lock()) counters_.cpuAssetBytes+=pose->vertices.capacity()*sizeof(Vertex);
        }
        counters_.gpuTextureBytes+=environmentBytes_;
        for(const auto& [key,gpu]:meshes_) {
            (void)key;counters_.gpuMeshBytes+=gpu.bytes;
            if(auto mesh=gpu.source.lock()) {
                counters_.cpuAssetBytes+=mesh->vertices.size()*sizeof(Vertex)+mesh->indices.size()*sizeof(std::uint32_t)+mesh->skin.size()*sizeof(SkinVertex);
                for(const auto& target:mesh->morphTargets) counters_.cpuAssetBytes+=(target.positions.size()+target.normals.size()+target.tangents.size())*sizeof(Vec3);
            }
        }
        for(const auto& [key,gpu]:textures_) {
            counters_.gpuTextureBytes+=gpu.bytes;
            const bool countedEarlier=(key.second!=TextureSemantic::Color&&textures_.contains({key.first,TextureSemantic::Color}))
                ||(key.second==TextureSemantic::Normal&&textures_.contains({key.first,TextureSemantic::Data}));
            if(auto texture=gpu.source.lock();texture&&!countedEarlier) {
                counters_.cpuAssetBytes+=texture->pixels.size();
                for(const auto& variant:texture->compressed) for(const auto& level:variant.levels) counters_.cpuAssetBytes+=level.pixels.size();
            }
        }
        for(const auto& level:environment_->specular) counters_.cpuAssetBytes+=level.pixels.size()*sizeof(std::array<float,4>);
        counters_.cpuAssetBytes+=(environment_->diffuse.pixels.size()+environment_->brdf.size())*sizeof(std::array<float,4>);
        for(const auto& resource:viewResources_) {
            if(resource->skyBuffer) counters_.gpuBufferBytes+=sizeof(SkyUniform);
            if(resource->underwaterBuffer) counters_.gpuBufferBytes+=sizeof(UnderwaterUniform);
            if(resource->frameBuffer) counters_.gpuBufferBytes+=(2+maxShadowLights)*sizeof(FrameUniform)+sizeof(ParticleCamera)+16+resource->particleCapacity*sizeof(ParticleInstance)+resource->ribbonCapacity*sizeof(RibbonVertex)+resource->objectCapacity*objectStride;
            counters_.frameStorageBytes+=resource->frame.storageBytes()+resource->fxFrame.storageBytes()+resource->particleShadow.capacity()*sizeof(ParticleInstance)+resource->ribbonShadow.capacity()*sizeof(RibbonVertex);
        }
        timing.resourceMs=resourceMs_;
        const auto finalEncodeStart=host_.nowMs();
        if(waterCapture_&&sampleCount_==1&&!hdr_) {
            wgpu::RenderPassColorAttachment color{};color.view=canvasView;color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;
            wgpu::RenderPassDescriptor desc{};desc.label="Water scene presentation";desc.colorAttachmentCount=1;desc.colorAttachments=&color;
            auto pass=encoder.BeginRenderPass(&desc);pass.SetPipeline(waterPresentPipeline_);pass.SetBindGroup(0,captureBinding_);pass.Draw(3);pass.End();
            ++counters_.passes;++counters_.drawCalls;++counters_.triangles;
        }
        if(hdr_)drawPost(encoder,canvasView);
        if(gui_) {
            if(!guiRenderer_) guiRenderer_=std::make_unique<GuiRenderer>(device_,format_);
            double logicalWidth=0,logicalHeight=0;
            const auto size=host_.logicalSize();logicalWidth=size.width;logicalHeight=size.height;
            const Vec2 logical{static_cast<float>(logicalWidth),static_cast<float>(logicalHeight)};
            gui_->layout(logical);
            guiRenderer_->render(gui_->draw(started/1000),logical,width_,height_,canvasView,encoder);
            const auto& stats=guiRenderer_->stats();
            counters_.guiDrawCalls=stats.drawCalls;counters_.guiTriangles=stats.triangles;
            counters_.guiUploadCalls=stats.uploadCalls;counters_.guiUploadBytes=stats.uploadBytes;
            counters_.guiTextureBytes=stats.textureBytes;counters_.guiBufferBytes=stats.bufferBytes;
            counters_.passes+=stats.passes;counters_.drawCalls+=stats.drawCalls;counters_.triangles+=stats.triangles;
            counters_.uploadCalls+=stats.uploadCalls;counters_.uploadBytes+=stats.uploadBytes;
            counters_.newTextures+=stats.newTextures;counters_.cachedTextures+=stats.cachedTextures;
            counters_.gpuTextureBytes+=stats.textureBytes;counters_.gpuBufferBytes+=stats.bufferBytes;
        }
        if(sampleSlot>=0) { const auto& slot=timingSlots_[sampleSlot];encoder.ResolveQuerySet(slot.queries,0,slot.queryCount,slot.resolve,0);encoder.CopyBufferToBuffer(slot.resolve,0,slot.readback,0,slot.queryCount*8); }
        host_.beforeSubmit(device_,encoder,backbuffer.texture);
        const auto commands=encoder.Finish();timing.encodeMs+=host_.nowMs()-finalEncodeStart;
        counters_.cachedMaterialPipelines=materialPipelines_.size();
        const auto submitStart=host_.nowMs();{ProfileScope submitProfile(ProfileCategory::Render,"Queue submit (CPU)");device_.GetQueue().Submit(1,&commands);}
        if(++stableFrames_==120) recoveryAttempts_=0;
        host_.present(surface_);
        timing.submitMs=host_.nowMs()-submitStart;
        timing.cpuMs=updateMs+host_.nowMs()-started;
        if(sampleSlot>=0) readGpuSample(sampleSlot);
        measurements_.recordFrame(timing,counters_);reportMeasurements(host_.nowMs());
        host_.stats(static_cast<int>(counters_.meshes),static_cast<int>(counters_.lights),static_cast<int>(counters_.passes));
    } catch(const std::exception& e) { activeView_=nullptr;activeCamera_=nullptr;fail(e.what()); }
}
} // namespace engine

