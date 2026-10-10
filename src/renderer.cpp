#include "engine/renderer.hpp"
#include "engine/mesh_optimizer.hpp"
#include "shaders.hpp"
#include "gui_renderer.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstddef>
#include <cstring>
#include <limits>

EM_JS(void, report_status, (const char* text, int error), {
    window.engineStatus(UTF8ToString(text), !!error);
});
EM_JS(void, report_stats, (int meshes, int lights, int layers), {
    window.engineStats(meshes, lights, layers);
});
EM_JS(void, report_performance, (const char* json,const char* build), { const snapshot=JSON.parse(UTF8ToString(json));snapshot.build=UTF8ToString(build);window.enginePerformance(snapshot); });
EM_JS(int, gpu_timing_requested, (), { return new URLSearchParams(location.search).get('gpuTiming') !== 'off'; });

EM_JS(void, report_render_state, (const char* json), { window.EngineQuality?.report(JSON.parse(UTF8ToString(json))); });
EM_JS(double, canvas_css_width, (), { return document.querySelector('#viewport').clientWidth; });
EM_JS(double, canvas_css_height, (), { return document.querySelector('#viewport').clientHeight; });
EM_JS(double, canvas_pixel_ratio, (), { return window.devicePixelRatio || 1; });

namespace engine {
namespace {
std::string messageText(wgpu::StringView message) {
    return message.data ? std::string(message.data, message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length) : std::string{};
}
struct alignas(16) Float4 { float x=0,y=0,z=0,w=0; };
struct GpuLight { Float4 positionType,directionRange,colorIntensity; };
struct GpuShadow { Mat4 viewProjection; Float4 options; };
struct FrameUniform { Mat4 viewProjection; Float4 ambientCount; std::array<GpuLight,Renderer::maxLights> lights; Float4 cameraEnvironment,environmentOptions; std::array<GpuShadow,Renderer::maxLights> shadows; };
struct ParticleCamera { Mat4 viewProjection;Float4 right,up; };
static_assert(sizeof(ParticleCamera)==96);
static_assert(sizeof(FrameUniform)==1136 && sizeof(ObjectUniform)==160);
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
void Renderer::reportState(const char* state) {
    const auto& c=capabilities_;const auto& q=quality_;
    const auto compressedCount=std::count_if(textures_.begin(),textures_.end(),[](const auto& entry){return entry.second.compressed;});
    const auto json=std::string("{\"state\":\"")+state+"\",\"generation\":"+std::to_string(deviceGeneration_)+
        ",\"recoveryAttempts\":"+std::to_string(recoveryAttempts_)+",\"maxTextureDimension2D\":"+std::to_string(c.maxTextureDimension2D)+
        ",\"maxTextureArrayLayers\":"+std::to_string(c.maxTextureArrayLayers)+",\"maxBufferSize\":"+std::to_string(c.maxBufferSize)+
        ",\"timestampQuery\":"+(c.timestampQuery?"true":"false")+",\"compressionBC\":"+(c.compressionBC?"true":"false")+
        ",\"compressionETC2\":"+(c.compressionETC2?"true":"false")+",\"compressionASTC\":"+(c.compressionASTC?"true":"false")+
        ",\"compressedTextureCount\":"+std::to_string(compressedCount)+",\"width\":"+std::to_string(width_)+",\"height\":"+std::to_string(height_)+
        ",\"resolutionScale\":"+std::to_string(q.resolution.scale)+",\"maxPixelRatio\":"+std::to_string(q.resolution.maxPixelRatio)+
        ",\"maxDimension\":"+std::to_string(q.resolution.maxDimension)+",\"maxPixels\":"+std::to_string(q.resolution.maxPixels)+
        ",\"shadowResolution\":"+std::to_string(shadowResolution_)+",\"shadowFilterTaps\":"+std::to_string(shadowFilterTaps_)+
        ",\"maxShadowMaps\":"+std::to_string(q.maxShadowMaps)+",\"lod\":"+(lodEnabled_?"true":"false")+
        ",\"shadowLod\":"+(shadowLodEnabled_?"true":"false")+",\"compressedTextures\":"+(q.compressedTextures?"true":"false")+"}";
    report_render_state(json.c_str());
}
void Renderer::fail(const std::string& message) {
    ready_=false;initializing_=false;
    if(recoveryAttempts_ && recoveryAttempts_<3) {
        recoveryPending_=true;retryAtMs_=emscripten_get_now()+250*recoveryAttempts_;reportState("recovering");
        report_status(message.c_str(),0);
    } else { recoveryPending_=false;failed_=true;reportState("failed");report_status(message.c_str(),1); }
}
void Renderer::deviceLost(const std::string& message) {
    ready_=false;initializing_=false;
    if(recoveryAttempts_>=3) { fail("GPU recovery exhausted: "+message);return; }
    recoveryPending_=true;retryAtMs_=emscripten_get_now()+250;reportState("recovering");
    report_status("GPU disconnected. Reconnecting…",0);
}
void Renderer::serviceRecovery() {
    if(recoveryPending_&&!initializing_&&emscripten_get_now()>=retryAtMs_) {
        recoveryPending_=false;++recoveryAttempts_;releaseGpu();initialize();
    }
}
void Renderer::retryRecovery() {
    if(!failed_) return;
    failed_=false;recoveryAttempts_=0;deviceLost("Explicit retry");
}
void Renderer::simulateDeviceLoss() { if(ready_&&device_) device_.Destroy(); }
void Renderer::resizeCanvas() {
    const auto extent=resolveResolution(canvas_css_width(),canvas_css_height(),canvas_pixel_ratio(),quality_.resolution,capabilities_);
    int w=0,h=0;emscripten_get_canvas_element_size("#viewport",&w,&h);
    if(w!=int(extent.width)||h!=int(extent.height)) emscripten_set_canvas_element_size("#viewport",extent.width,extent.height);
}
void Renderer::setQuality(const RenderQuality& quality) {
    quality.validate(); // Validate the entire candidate before publishing any setting.
    if(quality.maxShadowMaps!=quality_.maxShadowMaps) shadowResourcesDirty_=true;
    if(quality.compressedTextures!=quality_.compressedTextures) { materials_.clear();textures_.clear(); }
    quality_=quality;setShadowQuality(std::min(quality.shadowResolution,capabilities_.maxTextureDimension2D),quality.shadowFilterTaps);
    lodEnabled_=quality.lod;shadowLodEnabled_=quality.shadowLod;shadowOverflowReported_=false;
    resizeCanvas();resetMeasurements();reportState(ready_?"ready":failed_?"failed":"initializing");
}
Renderer::Renderer(Scene& scene) : scene_(&scene) {}
Renderer::Renderer(Scene& scene,CameraComponent& camera) : Renderer(scene) {
    const RenderView view{&camera};setViews({&view,1});
}
void Renderer::setViews(std::span<const RenderView> views) {
    views_.set(views);
    // Release removed views, retain caches for unchanged slots.
    if(viewResources_.size()>views.size()) viewResources_.resize(views.size());
}
void Renderer::setScene(Scene& scene,std::span<const RenderView> views) {
    for(const auto& view:views) if(!view.camera||view.camera->scene()!=&scene)
        throw std::invalid_argument("Render camera does not belong to the target scene");
    setViews(views);
    scene_=&scene;
}
Renderer::~Renderer() { releaseGpu();callbacks_->owner=nullptr; }
void Renderer::releaseGpu() {
    // Invalidate every outstanding adapter/device/timestamp callback before destroying resources.
    callbacks_->owner=nullptr;
    callbacks_=std::make_shared<CallbackState>(CallbackState{this});
    ready_=false;initializing_=false;guiRenderer_.reset();
    if(surface_&&width_) surface_.Unconfigure();
    viewResources_.clear();activeView_=nullptr;activeCamera_=nullptr;
    meshes_.clear();textures_.clear();materials_.clear();skeletons_.clear();morphs_.clear();
    materialPipelines_.clear();shadowPipelines_.clear();particlePipelines_={};timingSlots_={};
    meshShader_=nullptr;meshPipelineLayout_=nullptr;skinPipelineLayout_=nullptr;
    shadowPipelineLayout_=nullptr;shadowSkinPipelineLayout_=nullptr;shadowLayout_=nullptr;
    shadowTexture_=nullptr;shadowView_=nullptr;shadowLayerViews_={};shadowSampler_=nullptr;
    particleLayout_=nullptr;bindLayout_=nullptr;textureLayout_=nullptr;materialLayout_=nullptr;skinLayout_=nullptr;
    clearPipeline_=nullptr;clearLayout_=nullptr;depth_=nullptr;depthView_=nullptr;
    specularEnvironment_=nullptr;diffuseEnvironment_=nullptr;brdfLut_=nullptr;
    specularView_=nullptr;diffuseView_=nullptr;brdfView_=nullptr;environmentSampler_=nullptr;environment_.reset();environmentBytes_=0;
    if(device_) device_.Destroy();
    device_=nullptr;adapter_=nullptr;surface_=nullptr;instance_=nullptr;
    width_=height_=0;gpuTimingFailed_=false;stableFrames_=0;resetMeasurements();
}
void Renderer::initialize() {
    if(ready_||initializing_) return;
    initializing_=true;reportState(recoveryAttempts_?"recovering":"initializing");
    instance_=wgpu::CreateInstance();
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas{}; canvas.selector="#viewport";
    wgpu::SurfaceDescriptor surfaceDesc{}; surfaceDesc.nextInChain=&canvas;
    surface_=instance_.CreateSurface(&surfaceDesc);
    wgpu::RequestAdapterOptions options{}; options.powerPreference=wgpu::PowerPreference::HighPerformance;
    instance_.RequestAdapter(&options,wgpu::CallbackMode::AllowSpontaneous,
        [callbacks=callbacks_](wgpu::RequestAdapterStatus status,wgpu::Adapter adapter,wgpu::StringView message) {
            auto* self=callbacks->owner; if(!self) return;
            if(status!=wgpu::RequestAdapterStatus::Success) { self->fail("No WebGPU adapter: "+messageText(message)); return; }
            self->adapter_=std::move(adapter);
            wgpu::DeviceDescriptor desc{};
            self->gpuTimingSupported_=self->adapter_.HasFeature(wgpu::FeatureName::TimestampQuery);
            self->gpuTimingEnabled_=self->gpuTimingSupported_ && gpu_timing_requested();
            std::vector<wgpu::FeatureName> features;
            if(self->gpuTimingEnabled_) features.push_back(wgpu::FeatureName::TimestampQuery);
            for(auto feature:{wgpu::FeatureName::TextureCompressionBC,wgpu::FeatureName::TextureCompressionETC2,wgpu::FeatureName::TextureCompressionASTC})
                if(self->adapter_.HasFeature(feature)) features.push_back(feature);
            desc.requiredFeatureCount=features.size();desc.requiredFeatures=features.data();
            // Device-lost callback retains the context for the repeating error callback's lifetime.
            desc.SetUncapturedErrorCallback([](const wgpu::Device&,wgpu::ErrorType,wgpu::StringView message,CallbackState* state) {
                if(state->owner) state->owner->fail("WebGPU error: "+messageText(message));
            },callbacks.get());
            desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,[callbacks](const wgpu::Device&,wgpu::DeviceLostReason,wgpu::StringView message) {
                if(callbacks->owner) callbacks->owner->deviceLost(messageText(message));
            });
            self->adapter_.RequestDevice(&desc,wgpu::CallbackMode::AllowSpontaneous,
                [callbacks](wgpu::RequestDeviceStatus status,wgpu::Device device,wgpu::StringView message) {
                    auto* self=callbacks->owner; if(!self) return;
                    if(status!=wgpu::RequestDeviceStatus::Success) { self->fail("Cannot create WebGPU device: "+messageText(message)); return; }
                    self->device_=std::move(device);
                    wgpu::Limits limits{};
                    if(!self->device_.GetLimits(&limits)) { self->fail("Cannot query WebGPU limits");return; }
                    auto& c=self->capabilities_;c.maxTextureDimension2D=limits.maxTextureDimension2D;
                    c.maxTextureArrayLayers=limits.maxTextureArrayLayers;c.maxBufferSize=limits.maxBufferSize;
                    c.timestampQuery=self->gpuTimingSupported_;
                    c.compressionBC=self->device_.HasFeature(wgpu::FeatureName::TextureCompressionBC);
                    c.compressionETC2=self->device_.HasFeature(wgpu::FeatureName::TextureCompressionETC2);
                    c.compressionASTC=self->device_.HasFeature(wgpu::FeatureName::TextureCompressionASTC);
                    self->shadowResolution_=std::min(self->quality_.shadowResolution,c.maxTextureDimension2D);
                    ++self->deviceGeneration_;self->resizeCanvas();
                    wgpu::SurfaceCapabilities capabilities{};
                    self->surface_.GetCapabilities(self->adapter_,&capabilities);
                    if(capabilities.formatCount==0) { self->fail("No supported canvas format"); return; }
                    self->format_=capabilities.formats[0];
                    self->createPipeline();
                    self->createClearPipeline();
                    self->initializeTiming();
                    self->ready_=true;self->initializing_=false;self->failed_=false;self->reportState("ready");
                    report_status("Renderer ready",0);
                });
        });
}
void Renderer::configureTimingSlot(TimingSlot& slot,std::uint32_t queryCount) {
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
void Renderer::initializeTiming() {
    if(gpuTimingEnabled_) for(auto& slot:timingSlots_) configureTimingSlot(slot,14);
}
int Renderer::beginGpuSample(double nowMs) {
    if(!gpuTimingEnabled_ || gpuTimingFailed_ || resolvedViews_.empty() || nowMs-lastGpuSampleMs_<100) return -1;
    // WebGPU caps timestamp query sets at 8192 entries. Larger view lists still render.
    if(resolvedViews_.size()>8192/14) return -1;
    for(std::size_t i=0;i<timingSlots_.size();++i) {
        auto& slot=timingSlots_[i];if(slot.busy) continue;
        const auto queries=static_cast<std::uint32_t>(resolvedViews_.size()*14);
        if(slot.queryCount!=queries) configureTimingSlot(slot,queries);
        slot.shadowCounts.assign(resolvedViews_.size(),0);
        slot.busy=true;slot.epoch=measurementEpoch_;lastGpuSampleMs_=nowMs;return static_cast<int>(i);
    }
    return -1; // Never block rendering on a timestamp readback.
}
void Renderer::readGpuSample(int index) {
    auto& slot=timingSlots_[index];
    slot.readback.MapAsync(wgpu::MapMode::Read,0,slot.queryCount*8,wgpu::CallbackMode::AllowSpontaneous,
        [callbacks=callbacks_,index](wgpu::MapAsyncStatus status,wgpu::StringView) {
            auto* self=callbacks->owner;if(!self) return;
            auto& slot=self->timingSlots_[index];slot.busy=false;
            if(status!=wgpu::MapAsyncStatus::Success) { self->gpuTimingFailed_=true;return; }
            const auto* times=static_cast<const std::uint64_t*>(slot.readback.GetConstMappedRange(0,slot.queryCount*8));
            std::array<double,3> durations{};double shadowMs=0;bool valid=times!=nullptr;
            if(times) for(std::uint32_t pair=0;pair<slot.queryCount/2;++pair) {
                if(pair%7>=3+slot.shadowCounts[pair/7]) continue;
                if(times[pair*2+1]<times[pair*2]) valid=false;
                else if(pair%7>=3) shadowMs+=double(times[pair*2+1]-times[pair*2])*1e-6;
                else durations[pair%7]+=static_cast<double>(times[pair*2+1]-times[pair*2])*1e-6; // WebGPU timestamps are nanoseconds.
            }
            slot.readback.Unmap();
            if(valid && slot.epoch==self->measurementEpoch_) self->measurements_.recordGpu(durations,emscripten_get_now(),shadowMs);
        });
}
void Renderer::resetMeasurements() {
    measurements_.reset();++measurementEpoch_;lastReportMs_=-1000;lastGpuSampleMs_=-1000;
}
void Renderer::reportMeasurements(double nowMs) {
    if(nowMs-lastReportMs_<250) return;
    lastReportMs_=nowMs;reportState("ready");
    const char* gpuState=!gpuTimingSupported_ ? "unsupported" : !gpuTimingEnabled_ ? "disabled" : gpuTimingFailed_ ? "readback-failed" : "enabled";
    const auto json=measurements_.json(nowMs,gpuState);
#ifdef NDEBUG
    report_performance(json.c_str(),"Release");
#else
    report_performance(json.c_str(),"Debug");
#endif
}
void Renderer::createPipeline() {
    wgpu::ShaderSourceWGSL code{};code.code=mesh_shader;
    wgpu::ShaderModuleDescriptor shader{};shader.nextInChain=&code;shader.label="PBR / Unlit mesh materials";
    meshShader_=device_.CreateShaderModule(&shader);
    std::array<wgpu::BindGroupLayoutEntry,7> frameBindings{};
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
    wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=frameBindings.size();layout.entries=frameBindings.data();bindLayout_=device_.CreateBindGroupLayout(&layout);
    // Dedicated particle shaders retain their compact color-only binding layout.
    std::array<wgpu::BindGroupLayoutEntry,2> textureBindings{};
    textureBindings[0].binding=0;textureBindings[0].visibility=wgpu::ShaderStage::Fragment;textureBindings[0].sampler.type=wgpu::SamplerBindingType::Filtering;
    textureBindings[1].binding=1;textureBindings[1].visibility=wgpu::ShaderStage::Fragment|wgpu::ShaderStage::Vertex;
    textureBindings[1].texture.sampleType=wgpu::TextureSampleType::Float;textureBindings[1].texture.viewDimension=wgpu::TextureViewDimension::e2D;
    layout.entryCount=textureBindings.size();layout.entries=textureBindings.data();textureLayout_=device_.CreateBindGroupLayout(&layout);
    std::array<wgpu::BindGroupLayoutEntry,11> materialBindings{};
    materialBindings[0].binding=0;materialBindings[0].visibility=wgpu::ShaderStage::Fragment;
    materialBindings[0].buffer.type=wgpu::BufferBindingType::Uniform;materialBindings[0].buffer.minBindingSize=64;
    for(std::uint32_t i=0;i<5;++i) {
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
}
void Renderer::setShadowQuality(std::uint32_t resolution,std::uint32_t taps) {
    if((resolution!=256 && resolution!=512 && resolution!=1024 && resolution!=2048) || (taps!=1 && taps!=9))
        throw std::invalid_argument("Shadow quality requires 256/512/1024/2048 resolution and 1/9 taps");
    if(shadowResolution_!=resolution) {
        shadowResourcesDirty_=true;
        for(auto& view:viewResources_) view->frame.resetShadowDetailHistory();
    }
    shadowResolution_=resolution;shadowFilterTaps_=taps;quality_.shadowResolution=resolution;quality_.shadowFilterTaps=taps;
}
void Renderer::createShadowResources() {
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
wgpu::RenderPipeline Renderer::materialPipeline(const MeshDraw& draw,bool shadow) {
    const auto& m=draw.material;const bool skin=bool(draw.skinPose),transparent=draw.blend!=BlendMode::Opaque;
    const auto key=std::uint32_t(m.alphaMode)|(std::uint32_t(m.shader)<<2)|(std::uint32_t(m.doubleSided)<<3)|(std::uint32_t(skin)<<4)|(std::uint32_t(draw.mirrored)<<5);
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
    wgpu::ColorTargetState target{};target.format=format_;if(transparent) target.blend=&blend;
    wgpu::FragmentState fragment{};fragment.module=meshShader_;fragment.entryPoint=transparent ? "transparentMain" : "fragmentMain";fragment.targetCount=1;fragment.targets=&target;
    if(shadow) { fragment.entryPoint="shadowFragment";fragment.targetCount=0;fragment.targets=nullptr; }
    wgpu::DepthStencilState depth{};depth.format=wgpu::TextureFormat::Depth24Plus;depth.depthWriteEnabled=!transparent;depth.depthCompare=transparent ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Less;
    if(shadow) { depth.format=wgpu::TextureFormat::Depth32Float;depth.depthWriteEnabled=true;depth.depthCompare=wgpu::CompareFunction::Less;depth.depthBias=1;depth.depthBiasSlopeScale=1; }
    wgpu::RenderPipelineDescriptor desc{};desc.label="Cached material pipeline";desc.layout=skin ? skinPipelineLayout_ : meshPipelineLayout_;
    if(shadow) { desc.label="Directional shadow caster";desc.layout=skin ? shadowSkinPipelineLayout_ : shadowPipelineLayout_; }
    desc.vertex.module=meshShader_;desc.vertex.entryPoint=skin ? "skinnedVertexMain" : "vertexMain";desc.vertex.bufferCount=skin ? 3 : 2;desc.vertex.buffers=buffers.data();
    desc.fragment=shadow && m.alphaMode!=AlphaMode::Mask ? nullptr : &fragment;desc.depthStencil=&depth;desc.primitive.topology=wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode=m.doubleSided ? wgpu::CullMode::None : wgpu::CullMode::Back;desc.primitive.frontFace=draw.mirrored ? wgpu::FrontFace::CW : wgpu::FrontFace::CCW;
    const auto pipeline=device_.CreateRenderPipeline(&desc);pipelines.emplace(key,pipeline);return pipeline;
}
void Renderer::createFxPipelines() {
    wgpu::BindGroupLayoutEntry entry{};entry.binding=0;entry.visibility=wgpu::ShaderStage::Vertex;
    entry.buffer.type=wgpu::BufferBindingType::Uniform;entry.buffer.minBindingSize=sizeof(ParticleCamera);
    wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=1;layout.entries=&entry;
    particleLayout_=device_.CreateBindGroupLayout(&layout);
    const std::array layouts={particleLayout_,textureLayout_};
    wgpu::PipelineLayoutDescriptor pipelineLayout{};pipelineLayout.bindGroupLayoutCount=layouts.size();pipelineLayout.bindGroupLayouts=layouts.data();
    const auto explicitLayout=device_.CreatePipelineLayout(&pipelineLayout);
    wgpu::ShaderSourceWGSL code{};code.code=particle_shader;
    wgpu::ShaderModuleDescriptor shaderDesc{};shaderDesc.nextInChain=&code;shaderDesc.label="Particle billboards / flipbooks";
    const auto shader=device_.CreateShaderModule(&shaderDesc);
    std::array<wgpu::VertexAttribute,4> attributes{};
    for(std::size_t i=0;i<attributes.size();++i) { attributes[i].format=wgpu::VertexFormat::Float32x4;attributes[i].offset=i*16;attributes[i].shaderLocation=static_cast<std::uint32_t>(i); }
    wgpu::VertexBufferLayout buffer{};buffer.arrayStride=sizeof(ParticleInstance);buffer.stepMode=wgpu::VertexStepMode::Instance;
    buffer.attributeCount=attributes.size();buffer.attributes=attributes.data();
    wgpu::ColorTargetState target{};target.format=format_;
    wgpu::FragmentState fragment{};fragment.module=shader;fragment.entryPoint="particleFragment";fragment.targetCount=1;fragment.targets=&target;
    wgpu::DepthStencilState depth{};depth.format=wgpu::TextureFormat::Depth24Plus;depth.depthWriteEnabled=false;depth.depthCompare=wgpu::CompareFunction::LessEqual;
    wgpu::RenderPipelineDescriptor desc{};desc.label="Instanced particle FX";desc.layout=explicitLayout;
    desc.vertex.module=shader;desc.vertex.entryPoint="particleVertex";desc.vertex.bufferCount=1;desc.vertex.buffers=&buffer;
    desc.fragment=&fragment;desc.depthStencil=&depth;desc.primitive.topology=wgpu::PrimitiveTopology::TriangleList;
    for(std::size_t i=0;i<2;++i) { const auto blend=fxBlend(i==1);target.blend=&blend;particlePipelines_[i]=device_.CreateRenderPipeline(&desc); }

}
void Renderer::createClearPipeline() {
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
    wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=1;layout.entries=&entry;clearLayout_=device_.CreateBindGroupLayout(&layout);
    wgpu::PipelineLayoutDescriptor pipelineLayout{};pipelineLayout.bindGroupLayoutCount=1;pipelineLayout.bindGroupLayouts=&clearLayout_;
    const auto explicitLayout=device_.CreatePipelineLayout(&pipelineLayout);
    wgpu::ColorTargetState target{};target.format=format_;
    wgpu::FragmentState fragment{};fragment.module=shader;fragment.entryPoint="fragmentMain";fragment.targetCount=1;fragment.targets=&target;
    wgpu::DepthStencilState depth{};depth.format=wgpu::TextureFormat::Depth24Plus;depth.depthWriteEnabled=false;depth.depthCompare=wgpu::CompareFunction::Always;
    wgpu::RenderPipelineDescriptor desc{};desc.label="Viewport color clear";desc.layout=explicitLayout;
    desc.vertex.module=shader;desc.vertex.entryPoint="vertexMain";desc.fragment=&fragment;desc.depthStencil=&depth;
    clearPipeline_=device_.CreateRenderPipeline(&desc);
}
void Renderer::prepareViewBindings(const RenderView& view) {
    auto& v=*activeView_;
    if(!v.frameBuffer) {
        v.frameBuffer=makeBuffer(device_,sizeof(FrameUniform),wgpu::BufferUsage::Uniform,"View camera / lights");
        v.particleCameraBuffer=makeBuffer(device_,sizeof(ParticleCamera),wgpu::BufferUsage::Uniform,"View particle camera");
        v.clearBuffer=makeBuffer(device_,16,wgpu::BufferUsage::Uniform,"View clear color");
        wgpu::BindGroupEntry entry{};entry.binding=0;entry.buffer=v.particleCameraBuffer;entry.size=sizeof(ParticleCamera);
        wgpu::BindGroupDescriptor group{};group.layout=particleLayout_;group.entryCount=1;group.entries=&entry;
        v.particleBinding=device_.CreateBindGroup(&group);
        entry.buffer=v.clearBuffer;entry.size=16;group.layout=clearLayout_;v.clearBinding=device_.CreateBindGroup(&group);
        for(std::size_t i=0;i<maxShadowLights;++i) {
            v.shadowBuffers[i]=makeBuffer(device_,sizeof(FrameUniform),wgpu::BufferUsage::Uniform,"View shadow camera");
            entry.buffer=v.shadowBuffers[i];entry.size=sizeof(FrameUniform);group.layout=shadowLayout_;
            v.shadowBindings[i]=device_.CreateBindGroup(&group);
        }
    }
    if(v.boundEnvironment!=environment_) {
        std::array<wgpu::BindGroupEntry,7> entries{};entries[0].binding=0;entries[0].buffer=v.frameBuffer;entries[0].size=sizeof(FrameUniform);
        entries[1].binding=1;entries[1].textureView=specularView_;entries[2].binding=2;entries[2].textureView=diffuseView_;entries[3].binding=3;entries[3].textureView=brdfView_;
        entries[4].binding=4;entries[4].sampler=environmentSampler_;
        entries[5].binding=5;entries[5].textureView=shadowView_;entries[6].binding=6;entries[6].sampler=shadowSampler_;
        wgpu::BindGroupDescriptor group{};group.layout=bindLayout_;group.entryCount=entries.size();group.entries=entries.data();
        v.binding=device_.CreateBindGroup(&group);v.boundEnvironment=environment_;
    }
    const auto& color=view.clearColor;
    if(color && (!v.clearValid || v.clearShadow!=*color)) {
        device_.GetQueue().WriteBuffer(v.clearBuffer,0,color->data(),16);v.clearShadow=*color;v.clearValid=true;
        ++counters_.uploadCalls;counters_.uploadBytes+=16;
    }
}
void Renderer::prepareFx() {
    const auto instances=activeView_->fxFrame.instances();
    for(const auto& draw:activeView_->frame.sceneFrame().particles) uploadTexture(draw.texture);
    if(instances.empty()) { activeView_->particleShadow.clear();return; }
    bool recreate=instances.size()>activeView_->particleCapacity;
    if(recreate) {
        wgpu::Limits limits{};if(!device_.GetLimits(&limits)) throw std::runtime_error("Cannot query particle GPU limits");
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
    const auto viewport=activeCamera_->pixelViewport(float(width_),float(height_));
    const auto right=activeCamera_->right();const auto up=normalized(cross(right,activeCamera_->forward()));
    const ParticleCamera camera{activeCamera_->viewProjection(viewport.width/viewport.height),{right.x,right.y,right.z,0},{up.x,up.y,up.z,0}};
    if(!activeView_->particleCameraValid||std::memcmp(activeView_->particleCameraShadow.data(),&camera,sizeof(camera))) {
        device_.GetQueue().WriteBuffer(activeView_->particleCameraBuffer,0,&camera,sizeof(camera));
        std::memcpy(activeView_->particleCameraShadow.data(),&camera,sizeof(camera));activeView_->particleCameraValid=true;
        ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(camera);
    }
}
void Renderer::resize(std::uint32_t width,std::uint32_t height) {
    width_=width; height_=height;
    wgpu::SurfaceConfiguration config{}; config.device=device_; config.format=format_;
    config.width=width; config.height=height; config.usage=wgpu::TextureUsage::RenderAttachment;
    config.alphaMode=wgpu::CompositeAlphaMode::Opaque; surface_.Configure(&config);
    if(depth_) depth_.Destroy();
    wgpu::TextureDescriptor desc{}; desc.label="Layer depth buffer"; desc.size={width,height,1};
    desc.format=wgpu::TextureFormat::Depth24Plus; desc.usage=wgpu::TextureUsage::RenderAttachment;
    depth_=device_.CreateTexture(&desc); depthView_=depth_.CreateView();
}
void Renderer::reserveObjects(std::size_t count) {
    if(count<=activeView_->objectCapacity) return;
    wgpu::Limits limits{};
    if(!device_.GetLimits(&limits)) throw std::runtime_error("Cannot query GPU instance-buffer limit");
    const auto maxCapacity=std::min<std::uint64_t>(limits.maxBufferSize/objectStride,std::numeric_limits<std::size_t>::max()/objectStride);
    if(count>maxCapacity) throw std::length_error("Instance data exceeds the GPU buffer-size limit");
    activeView_->objectCapacity=static_cast<std::size_t>(std::min<std::uint64_t>(std::max<std::uint64_t>(count,std::uint64_t(activeView_->objectCapacity)*2+16),maxCapacity));
    if(activeView_->objectBuffer) activeView_->objectBuffer.Destroy();
    activeView_->objectBuffer=makeBuffer(device_,activeView_->objectCapacity*objectStride,wgpu::BufferUsage::Vertex,"Per-instance transforms / normals / tint");
    ++counters_.objectBufferGrowths;
    activeView_->frame.invalidateObjects();
}
Renderer::GpuMesh& Renderer::upload(const std::shared_ptr<const Mesh>& mesh) {
    if(const auto cached=meshes_.find(mesh.get());cached!=meshes_.end()) return cached->second;
    const auto resourceStart=emscripten_get_now();
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
        resourceMs_+=emscripten_get_now()-resourceStart;
    }
    for(const auto& level:mesh->lods) upload(level.mesh);
    return meshes_.at(mesh.get());
}
Renderer::GpuTexture& Renderer::uploadTexture(const std::shared_ptr<const Texture>& texture,TextureSemantic semantic) {
    const auto key=std::make_pair(texture.get(),semantic);
    if(const auto cached=textures_.find(key);cached!=textures_.end()) return cached->second;
    const auto started=emscripten_get_now();
    if(!texture->width||!texture->height||texture->width>4096||texture->height>4096||texture->pixels.size()!=std::uint64_t(texture->width)*texture->height*4)
        throw std::invalid_argument("Invalid RGBA texture fallback");
    const CompressedTexture* compressed=nullptr;
    for(const auto& variant:texture->compressed) {
        variant.validate(texture->width,texture->height);
        const bool supported=variant.format==TextureCompression::BC7 ? capabilities_.compressionBC :
            variant.format==TextureCompression::ETC2RGBA ? capabilities_.compressionETC2 : capabilities_.compressionASTC;
        if(!compressed&&quality_.compressedTextures&&supported&&variant.semantic==semantic) compressed=&variant;
    }
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
    ++counters_.newTextures;resourceMs_+=emscripten_get_now()-started;return textures_.emplace(key,std::move(gpu)).first->second;
}
std::size_t Renderer::MaterialKeyHash::operator()(const MaterialKey& key) const {
    std::size_t h=key.flags;
    auto mix=[&](std::size_t v) { h^=v+0x9e3779b9+(h<<6)+(h>>2); };
    for(auto t:key.textures) mix(std::hash<const Texture*>{}(t));
    for(auto v:key.parameters) mix(std::hash<float>{}(v));return h;
}
Renderer::GpuMaterial& Renderer::uploadMaterial(const Material& m) {
    const std::array<std::shared_ptr<const Texture>,5> sources={m.baseColorTexture ? m.baseColorTexture : Texture::white(),
        m.metallicRoughnessTexture ? m.metallicRoughnessTexture : Texture::white(),m.normalTexture ? m.normalTexture : Texture::flatNormal(),
        m.occlusionTexture ? m.occlusionTexture : Texture::white(),m.emissiveTexture ? m.emissiveTexture : Texture::white()};
    const MaterialKey key{{sources[0].get(),sources[1].get(),sources[2].get(),sources[3].get(),sources[4].get()},
        {m.roughness,m.metallic,m.normalScale,m.occlusionStrength,m.emissive.x,m.emissive.y,m.emissive.z,m.alphaCutoff,m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y},
        std::uint32_t(m.alphaMode)|(std::uint32_t(m.shader)<<2)|(std::uint32_t(m.doubleSided)<<3)|(std::uint32_t(bool(m.normalTexture))<<4)};
    if(auto it=materials_.find(key);it!=materials_.end()) { it->second.used=frameSerial_;return it->second; }
    const auto started=emscripten_get_now();const auto previousResourceMs=resourceMs_;GpuMaterial gpu;gpu.sources=sources;gpu.used=frameSerial_;
    const std::array<Float4,4> data={Float4{m.roughness,m.metallic,m.normalScale,m.occlusionStrength},
        Float4{m.emissive.x,m.emissive.y,m.emissive.z,m.alphaCutoff},Float4{m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y},
        Float4{float(m.alphaMode),float(m.shader),float(m.doubleSided),float(bool(m.normalTexture))}};
    gpu.buffer=makeBuffer(device_,sizeof(data),wgpu::BufferUsage::Uniform,"Material parameters");device_.GetQueue().WriteBuffer(gpu.buffer,0,data.data(),sizeof(data));
    ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(data);
    ++counters_.materialUploadCalls;counters_.materialUploadBytes+=sizeof(data);
    std::array<wgpu::BindGroupEntry,11> entries{};entries[0].binding=0;entries[0].buffer=gpu.buffer;entries[0].size=sizeof(data);
    constexpr std::array semantics={TextureSemantic::Color,TextureSemantic::Data,TextureSemantic::Normal,TextureSemantic::Data,TextureSemantic::Color};
    for(std::uint32_t i=0;i<5;++i) {
        auto& t=uploadTexture(sources[i],semantics[i]);entries[1+i*2].binding=1+i*2;entries[1+i*2].sampler=t.sampler;
        entries[2+i*2].binding=2+i*2;entries[2+i*2].textureView=t.view;
    }
    wgpu::BindGroupDescriptor group{};group.layout=materialLayout_;group.entryCount=entries.size();group.entries=entries.data();gpu.binding=device_.CreateBindGroup(&group);
    resourceMs_=previousResourceMs+emscripten_get_now()-started;return materials_.emplace(key,std::move(gpu)).first->second;
}
void Renderer::uploadEnvironment() {
    const auto source=scene_->environment ? scene_->environment : Environment::studio();
    if(source==environment_) return;source->validate();const auto started=emscripten_get_now();environmentBytes_=0;
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
    environment_=source;counters_.newTextures+=3;resourceMs_+=emscripten_get_now()-started;
}
void Renderer::uploadMorph(const std::shared_ptr<const MorphPose>& pose) {
    if(pose->vertices.empty()) throw std::invalid_argument("Empty morph vertex buffer");
    auto& gpu=morphs_[pose.get()];
    const auto bytes=pose->vertices.size()*sizeof(Vertex);
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
void Renderer::uploadSkeleton(const std::shared_ptr<const SkinPose>& pose) {
    if(pose->bones.empty() || pose->bones.size()>1024) throw std::invalid_argument("Invalid GPU bone palette");
    auto& gpu=skeletons_[pose.get()];
    const auto bytes=pose->bones.size()*sizeof(BoneUniform);
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
void Renderer::render(double updateMs,double intervalMs) {
    if(!ready_) return;
    try {
        resizeCanvas();
        const auto started=emscripten_get_now();FrameTiming timing{};timing.updateMs=updateMs;timing.intervalMs=intervalMs;counters_={};resourceMs_=0;++frameSerial_;
        int width=0,height=0; emscripten_get_canvas_element_size("#viewport",&width,&height);
        if(width<=0 || height<=0) return;
        if(static_cast<std::uint32_t>(width)!=width_ || static_cast<std::uint32_t>(height)!=height_) resize(width,height);
        wgpu::SurfaceTexture backbuffer{};surface_.GetCurrentTexture(&backbuffer);
        if(!backbuffer.texture) {
            if(backbuffer.status==wgpu::SurfaceGetCurrentTextureStatus::Lost) deviceLost("Canvas surface lost");
            else if(backbuffer.status==wgpu::SurfaceGetCurrentTextureStatus::Error) fail("Cannot acquire canvas surface");
            else if(backbuffer.status==wgpu::SurfaceGetCurrentTextureStatus::Outdated) width_=height_=0;
            return;
        }
        if(shadowResourcesDirty_) createShadowResources();
        const auto view=backbuffer.texture.CreateView();
        auto encoder=device_.CreateCommandEncoder();
        views_.resolve(width_,height_,resolvedViews_);
        while(viewResources_.size()<views_.views().size()) viewResources_.push_back(std::make_unique<ViewResources>());
        const auto sampleSlot=beginGpuSample(started);
        std::uint32_t viewOrdinal=0;
        bool targetInitialized=false;
        uploadEnvironment();
        for(const auto& resolved:resolvedViews_) {
            activeView_=viewResources_[resolved.index].get();
            activeCamera_=views_.views()[resolved.index].camera;
            const auto viewport=resolved.viewport;
            const auto collectStart=emscripten_get_now();
            const auto* frustum=frustumCullingEnabled_ && activeCamera_->active() ? &activeCamera_->frustum(viewport.width/viewport.height) : nullptr;
            const DetailView detail{activeCamera_->viewProjection(viewport.width/viewport.height),viewport.width,viewport.height};
            activeView_->frame.collect(*scene_,instancingEnabled_,activeCamera_->active() ? activeCamera_->visibilityMask() : 0,frustum,lodEnabled_ ? &detail : nullptr);
            const auto& frame=activeView_->frame.sceneFrame();timing.collectMs+=emscripten_get_now()-collectStart;
            const auto prepareStart=emscripten_get_now();
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
            FrameUniform uniforms{}; uniforms.viewProjection=activeCamera_->viewProjection(viewport.width/viewport.height);
            const auto camera=activeCamera_->worldPosition();
            if(!std::isfinite(scene_->environmentIntensity)||scene_->environmentIntensity<0||!std::isfinite(scene_->environmentRotation)) throw std::invalid_argument("Invalid environment settings");
            uniforms.cameraEnvironment={camera.x,camera.y,camera.z,scene_->environment ? scene_->environmentIntensity : 0};
            uniforms.environmentOptions={scene_->environmentRotation,float(environment_->specular.size()-1),shadowReceivingEnabled_ ? 0.0f : 1.0f,float(shadowFilterTaps_)};
            const auto count=std::min(frame.lights.size(),maxLights);
            uniforms.ambientCount={scene_->ambient.x,scene_->ambient.y,scene_->ambient.z,static_cast<float>(count)};
            if(frame.lights.size()>maxLights && !lightOverflowReported_) { emscripten_log(EM_LOG_WARN,"Renderer uses first eight enabled lights"); lightOverflowReported_=true; }
            std::size_t shadowCount=0;
            for(auto& shadow:uniforms.shadows) shadow.options.x=-1;
            for(std::size_t i=0;i<count;++i) {
                const auto& l=frame.lights[i]; uniforms.lights[i]={{l.position.x,l.position.y,l.position.z,l.type==LightType::Point ? 1.0f : 0.0f},
                    {l.direction.x,l.direction.y,l.direction.z,l.range},{l.color.x,l.color.y,l.color.z,l.intensity}};
                if(shadowsEnabled_&&quality_.maxShadowMaps>0&&l.shadow.enabled&&l.intensity>0) {
                    if(shadowCount==quality_.maxShadowMaps) {
                        if(quality_.maxShadowMaps==maxShadowLights&&!shadowOverflowReported_) { emscripten_log(EM_LOG_WARN,"Renderer uses first four shadow-casting directional lights");shadowOverflowReported_=true; }
                        continue;
                    }
                    const auto matrix=directionalShadowMatrix(l.direction,l.shadowCenter,l.shadow,shadowResolution_);
                    uniforms.shadows[i]={matrix,{float(shadowCount),l.shadow.depthBias,l.shadow.normalBias,1.0f/shadowResolution_}};
                    if(!activeView_->shadowMatrixValid[shadowCount]||std::memcmp(&activeView_->shadowMatrices[shadowCount],&matrix,sizeof(matrix))) {
                        device_.GetQueue().WriteBuffer(activeView_->shadowBuffers[shadowCount],0,&matrix,sizeof(matrix));
                        activeView_->shadowMatrices[shadowCount]=matrix;activeView_->shadowMatrixValid[shadowCount]=true;
                        ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(matrix);
                    }
                    activeView_->frame.prepareShadow(shadowCount,{matrix,float(shadowResolution_),float(shadowResolution_)},shadowLodEnabled_,shadowCullingEnabled_,shadowCastingEnabled_);
                    ++shadowCount;
                }
            }
            if(!activeView_->frameUniformValid || std::memcmp(activeView_->frameUniformShadow.data(),&uniforms,sizeof(uniforms))) {
                device_.GetQueue().WriteBuffer(activeView_->frameBuffer,0,&uniforms,sizeof(uniforms));
                std::memcpy(activeView_->frameUniformShadow.data(),&uniforms,sizeof(uniforms));activeView_->frameUniformValid=true;
                ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(uniforms);
                ++counters_.frameUploadCalls;counters_.frameUploadBytes+=sizeof(uniforms);
            }
            for(std::size_t i=0;i<shadowCount;++i) for(const auto& batch:activeView_->frame.shadowBatches(i)) upload(batch.mesh);
            activeView_->frame.prepareObjects();
            const auto objects=activeView_->frame.objectData();
            for(const auto& draw:frame.meshes) {
                upload(draw.sourceMesh ? draw.sourceMesh : draw.mesh);
                upload(draw.mesh);
                uploadMaterial(draw.material);
                if(draw.skinPose) uploadSkeleton(draw.skinPose);
                if(draw.morphPose) uploadMorph(draw.morphPose);
            }
            for(const auto& range:activeView_->frame.objectUploads()) {
                device_.GetQueue().WriteBuffer(activeView_->objectBuffer,range.offset,objects.data()+range.offset,range.bytes);
                ++counters_.uploadCalls;counters_.uploadBytes+=range.bytes;
                ++counters_.objectUploadCalls;counters_.objectUploadBytes+=range.bytes;
            }
            prepareFx();counters_.particles+=activeView_->fxFrame.instances().size();
            counters_.meshes+=frame.meshCount;counters_.lights+=count;
            counters_.culledMeshes+=frame.culledMeshes;counters_.visibleMeshes+=frame.meshCount-frame.culledMeshes;
            counters_.frameStorageGrowths+=activeView_->frame.storageGrowths()+activeView_->fxFrame.storageGrowths();
            counters_.cameraLodParts+=activeView_->frame.cameraLodParts();
            counters_.shadowLodParts+=activeView_->frame.shadowLodParts();
            counters_.shadowCulledParts+=activeView_->frame.shadowCulledParts();
            counters_.changedObjects+=activeView_->frame.changedObjects();
            counters_.localTransformBuilds+=frame.transformRebuilds.localBuilds;counters_.worldTransformBuilds+=frame.transformRebuilds.worldBuilds;counters_.normalTransformBuilds+=frame.transformRebuilds.normalBuilds;
            timing.prepareMs+=emscripten_get_now()-prepareStart;
            timing.resourceMs=resourceMs_;
            const auto encodeStart=emscripten_get_now();
            if(sampleSlot>=0) timingSlots_[sampleSlot].shadowCounts[viewOrdinal]=shadowCount;
            for(std::size_t i=0;i<shadowCount;++i) {
                wgpu::RenderPassDepthStencilAttachment depth{};depth.view=shadowLayerViews_[i];depth.depthClearValue=1;
                depth.depthLoadOp=wgpu::LoadOp::Clear;depth.depthStoreOp=wgpu::StoreOp::Store;
                wgpu::RenderPassDescriptor desc{};desc.label="Directional shadow map";desc.depthStencilAttachment=&depth;
                wgpu::PassTimestampWrites timestamps{};
                if(sampleSlot>=0) {
                    timestamps.querySet=timingSlots_[sampleSlot].queries;
                    timestamps.beginningOfPassWriteIndex=viewOrdinal*14+6+i*2;
                    timestamps.endOfPassWriteIndex=viewOrdinal*14+7+i*2;
                    desc.timestampWrites=&timestamps;
                }
                auto pass=encoder.BeginRenderPass(&desc);pass.SetBindGroup(0,activeView_->shadowBindings[i]);pass.SetVertexBuffer(1,activeView_->objectBuffer);
                ++counters_.passes;++counters_.shadowPasses;
                for(const auto& shadowBatch:activeView_->frame.shadowBatches(i)) {
                    const auto& batch=shadowBatch.instances;
                    const auto& draw=activeView_->frame.instanceDraw(batch.firstInstance);
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
            // A color clear for Background, then load previous color for World and Overlay.
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
            if(sampleSlot>=0) { timestamps.querySet=timingSlots_[sampleSlot].queries;timestamps.beginningOfPassWriteIndex=viewOrdinal*14+layer*2;timestamps.endOfPassWriteIndex=viewOrdinal*14+layer*2+1;desc.timestampWrites=&timestamps; }
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
                pass.SetBindGroup(0,activeView_->binding);pass.SetVertexBuffer(1,activeView_->objectBuffer);
                ++counters_.passes;
                for(const auto& batch:activeView_->frame.cameraBatches(static_cast<RenderLayer>(layer))) {
                    const auto& draw=activeView_->frame.instanceDraw(batch.firstInstance);
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
                }
                // FX stay inside their layer pass: world depth is preserved until Overlay.
                for(const auto& batch:activeView_->fxFrame.batches(static_cast<RenderLayer>(layer))) {
                    const auto blend=batch.blend==BlendMode::Additive ? 1 : 0;
                    if(batch.particle) {
                        pass.SetPipeline(particlePipelines_[blend]);pass.SetBindGroup(0,activeView_->particleBinding);
                        pass.SetVertexBuffer(0,activeView_->particleBuffer);pass.SetBindGroup(1,textures_.at({batch.texture.get(),TextureSemantic::Color}).binding);
                        pass.Draw(6,batch.count,0,batch.first);
                        ++counters_.particleDrawCalls;counters_.triangles+=std::uint64_t(batch.count)*2;
                        counters_.submittedInstances+=batch.count;counters_.instancedDrawCalls+=batch.count>1;
                        counters_.maxInstancesPerDraw=std::max<std::uint64_t>(counters_.maxInstancesPerDraw,batch.count);
                    } else {
                        const auto& draw=frame.meshes[batch.first];const auto& mesh=meshes_.at(draw.mesh.get());
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
            }
            timing.encodeMs+=emscripten_get_now()-encodeStart;
            ++viewOrdinal;
        } // Ordered views all load the same color target and start fresh depth per layer.
        activeView_=nullptr;activeCamera_=nullptr;
        if(!targetInitialized) {
            wgpu::RenderPassColorAttachment color{};color.view=view;color.clearValue={0.018,0.026,0.042,1};
            color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;
            wgpu::RenderPassDescriptor desc{};desc.label="Empty compositor";desc.colorAttachmentCount=1;desc.colorAttachments=&color;
            auto pass=encoder.BeginRenderPass(&desc);pass.End();++counters_.passes;
        }
        for(auto it=materials_.begin();it!=materials_.end();) {
            if(it->second.used!=frameSerial_) { it->second.buffer.Destroy();it=materials_.erase(it); } else ++it;
        }
        counters_.cachedMeshes=meshes_.size();counters_.cachedTextures=textures_.size()+3;counters_.cachedMaterials=materials_.size();
        counters_.shadowMapResolution=shadowResolution_;counters_.shadowFilterTaps=shadowFilterTaps_;
        counters_.width=width_;counters_.height=height_;counters_.gpuDepthBytes=std::uint64_t(width_)*height_*4+std::uint64_t(shadowResolution_)*shadowResolution_*std::max(1u,quality_.maxShadowMaps)*4;
        if(gpuTimingEnabled_) for(const auto& slot:timingSlots_) counters_.gpuBufferBytes+=slot.queryCount*16;
        counters_.cachedSkeletons=skeletons_.size();
        for(const auto& [key,gpu]:skeletons_) { (void)key;counters_.gpuSkinBytes+=gpu.capacity*sizeof(BoneUniform); }
        counters_.gpuBufferBytes+=counters_.gpuSkinBytes+materials_.size()*64;
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
            if(resource->frameBuffer) counters_.gpuBufferBytes+=(1+maxShadowLights)*sizeof(FrameUniform)+sizeof(ParticleCamera)+16+resource->particleCapacity*sizeof(ParticleInstance)+resource->objectCapacity*objectStride;
            counters_.frameStorageBytes+=resource->frame.storageBytes()+resource->fxFrame.storageBytes()+resource->particleShadow.capacity()*sizeof(ParticleInstance);
        }
        timing.resourceMs=resourceMs_;
        const auto finalEncodeStart=emscripten_get_now();
        if(gui_) {
            if(!guiRenderer_) guiRenderer_=std::make_unique<GuiRenderer>(device_,format_);
            double logicalWidth=0,logicalHeight=0;
            emscripten_get_element_css_size("#viewport",&logicalWidth,&logicalHeight);
            const Vec2 logical{static_cast<float>(logicalWidth),static_cast<float>(logicalHeight)};
            gui_->layout(logical);
            guiRenderer_->render(gui_->draw(started/1000),logical,width_,height_,view,encoder);
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
        const auto commands=encoder.Finish();timing.encodeMs+=emscripten_get_now()-finalEncodeStart;
        counters_.cachedMaterialPipelines=materialPipelines_.size();
        const auto submitStart=emscripten_get_now();device_.GetQueue().Submit(1,&commands);
        if(++stableFrames_==120) recoveryAttempts_=0;timing.submitMs=emscripten_get_now()-submitStart;
        timing.cpuMs=updateMs+emscripten_get_now()-started;
        if(sampleSlot>=0) readGpuSample(sampleSlot);
        measurements_.recordFrame(timing,counters_);reportMeasurements(emscripten_get_now());
        report_stats(static_cast<int>(counters_.meshes),static_cast<int>(counters_.lights),static_cast<int>(counters_.passes));
    } catch(const std::exception& e) { activeView_=nullptr;activeCamera_=nullptr;fail(e.what()); }
}
} // namespace engine
