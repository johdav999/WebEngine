#pragma once
#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include "engine/performance.hpp"
#include "engine/render_frame.hpp"
#include "engine/fx_frame.hpp"
#include "engine/render_view.hpp"
#include "engine/render_quality.hpp"
#include <webgpu/webgpu_cpp.h>
#include <array>
#include <unordered_map>
#include <map>

namespace engine {
class GuiSystem;
class GuiRenderer;
class Renderer {
public:
    explicit Renderer(Scene& scene);
    Renderer(Scene& scene, CameraComponent& camera);
    void setScene(Scene& scene,std::span<const RenderView> views); // Validates before switching; caller retains ownership.
    void setViews(std::span<const RenderView> views);
    std::span<const RenderView> views() const { return views_.views(); }
    ~Renderer();
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    void initialize();
    void serviceRecovery(); // Host calls at frame boundaries, including while not ready.
    void retryRecovery(); // Explicit retry after bounded automatic attempts are exhausted.
    void setQuality(const RenderQuality& quality);
    const RenderQuality& quality() const { return quality_; }
    const RenderCapabilities& capabilities() const { return capabilities_; }
    void resizeCanvas();
    void refreshQualityReport() { reportState(ready_ ? "ready" : failed_ ? "failed" : "initializing"); }
    void simulateDeviceLoss(); // Host may expose this through an opt-in acceptance fixture.
    void render(double updateMs=0,double intervalMs=0);
    void resetMeasurements();
    void setInstancingEnabled(bool enabled) { instancingEnabled_=enabled; }
    void setFrustumCullingEnabled(bool enabled) { frustumCullingEnabled_=enabled; }
    void setShadowsEnabled(bool enabled) { shadowsEnabled_=enabled; }
    void setLodEnabled(bool enabled) { lodEnabled_=enabled;quality_.lod=enabled; }
    void setShadowLodEnabled(bool enabled) { shadowLodEnabled_=enabled;quality_.shadowLod=enabled; }
    void setShadowCastingEnabled(bool enabled) { shadowCastingEnabled_=enabled; }
    void setShadowReceivingEnabled(bool enabled) { shadowReceivingEnabled_=enabled; }
    void setShadowCullingEnabled(bool enabled) { shadowCullingEnabled_=enabled; }
    void setShadowQuality(std::uint32_t resolution,std::uint32_t taps);
    void setGui(GuiSystem* gui) { gui_=gui; }
    bool ready() const { return ready_; }
    // Each ordered layer has its own depth range. Overlay meshes render above World meshes.
    static constexpr std::size_t maxLights=8;
    static constexpr std::size_t maxShadowLights=4;
    static constexpr std::uint32_t shadowResolution=1024;
private:
    struct CallbackState { Renderer* owner; };
    std::shared_ptr<CallbackState> callbacks_=std::make_shared<CallbackState>(CallbackState{this});
    struct GpuMesh {
        std::weak_ptr<const Mesh> source;
        wgpu::Buffer vertices, indices,skin;
        wgpu::IndexFormat indexFormat=wgpu::IndexFormat::Uint32;
        std::uint32_t indexCount=0;
        std::uint64_t bytes=0;
    };
    struct GpuTexture {
        std::weak_ptr<const Texture> source;
        wgpu::Texture image;
        wgpu::TextureView view;
        wgpu::Sampler sampler;
        wgpu::BindGroup binding;
        std::uint64_t bytes=0;
        bool compressed=false;
    };
    struct MaterialKey {
        std::array<const Texture*,5> textures;
        std::array<float,12> parameters;
        std::uint32_t flags=0;
        bool operator==(const MaterialKey&) const=default;
    };
    struct MaterialKeyHash { std::size_t operator()(const MaterialKey& key) const; };
    struct GpuMaterial {
        wgpu::Buffer buffer;wgpu::BindGroup binding;
        std::array<std::shared_ptr<const Texture>,5> sources;
        std::uint64_t used=0;
    };
    struct TimingSlot {
        wgpu::QuerySet queries;
        wgpu::Buffer resolve,readback;
        std::uint32_t queryCount=0;
        std::vector<std::uint32_t> shadowCounts;
        bool busy=false;
        std::uint64_t epoch=0;
    };
    struct GpuSkeleton {
        std::weak_ptr<const SkinPose> source;
        wgpu::Buffer buffer;wgpu::BindGroup binding;
        std::size_t capacity=0;std::uint64_t revision=0;
    };
    struct GpuMorph {
        std::weak_ptr<const MorphPose> source;
        wgpu::Buffer vertices;
        std::size_t capacity=0;std::uint64_t revision=0;
    };
    Scene* scene_;
    RenderViews views_;
    std::vector<ResolvedRenderView> resolvedViews_;
    GuiSystem* gui_=nullptr; // Caller owns the GUI and keeps it alive through rendering.
    std::unique_ptr<GuiRenderer> guiRenderer_;
    wgpu::Instance instance_;
    wgpu::Adapter adapter_;
    wgpu::Device device_;
    wgpu::Surface surface_;
    wgpu::TextureFormat format_{};
    wgpu::Texture depth_;
    wgpu::TextureView depthView_;
    wgpu::ShaderModule meshShader_;
    wgpu::PipelineLayout meshPipelineLayout_,skinPipelineLayout_;
    std::unordered_map<std::uint32_t,wgpu::RenderPipeline> materialPipelines_;
    std::unordered_map<std::uint32_t,wgpu::RenderPipeline> shadowPipelines_;
    wgpu::PipelineLayout shadowPipelineLayout_,shadowSkinPipelineLayout_;
    wgpu::BindGroupLayout shadowLayout_;
    wgpu::Texture shadowTexture_;
    wgpu::TextureView shadowView_;
    std::array<wgpu::TextureView,maxShadowLights> shadowLayerViews_;
    wgpu::Sampler shadowSampler_;
    std::array<wgpu::RenderPipeline,2> particlePipelines_;
    wgpu::BindGroupLayout particleLayout_;
    wgpu::BindGroupLayout bindLayout_;
    wgpu::BindGroupLayout textureLayout_;
    wgpu::BindGroupLayout materialLayout_;
    wgpu::BindGroupLayout skinLayout_;

    std::unordered_map<const Mesh*,GpuMesh> meshes_;
    std::map<std::pair<const Texture*,TextureSemantic>,GpuTexture> textures_;
    std::unordered_map<MaterialKey,GpuMaterial,MaterialKeyHash> materials_;
    std::uint64_t frameSerial_=0;
    std::shared_ptr<const Environment> environment_;
    wgpu::Texture specularEnvironment_,diffuseEnvironment_,brdfLut_;
    wgpu::TextureView specularView_,diffuseView_,brdfView_;
    wgpu::Sampler environmentSampler_;
    std::uint64_t environmentBytes_=0;
    std::unordered_map<const SkinPose*,GpuSkeleton> skeletons_;
    std::unordered_map<const MorphPose*,GpuMorph> morphs_;
    struct ViewResources {
        RenderFrame frame;
        FxFrame fxFrame;
        wgpu::BindGroup binding,particleBinding,clearBinding;
        wgpu::Buffer frameBuffer,objectBuffer,particleCameraBuffer,particleBuffer,clearBuffer;
        std::size_t objectCapacity=0,particleCapacity=0;
        std::vector<ParticleInstance> particleShadow;
        std::array<std::byte,1136> frameUniformShadow{};
        std::array<wgpu::Buffer,maxShadowLights> shadowBuffers;
        std::array<wgpu::BindGroup,maxShadowLights> shadowBindings;
        std::array<Mat4,maxShadowLights> shadowMatrices{};
        std::array<bool,maxShadowLights> shadowMatrixValid{};
        std::array<std::byte,96> particleCameraShadow{};
        std::array<float,4> clearShadow{};
        bool frameUniformValid=false,particleCameraValid=false,clearValid=false;
        std::shared_ptr<const Environment> boundEnvironment;
    };
    std::vector<std::unique_ptr<ViewResources>> viewResources_;
    ViewResources* activeView_=nullptr; // Only used while preparing/encoding a resolved view.
    CameraComponent* activeCamera_=nullptr;
    wgpu::RenderPipeline clearPipeline_;
    wgpu::BindGroupLayout clearLayout_;
    std::uint32_t width_=0,height_=0;
    bool ready_=false, lightOverflowReported_=false;
    RenderQuality quality_;
    RenderCapabilities capabilities_;
    bool initializing_=false,recoveryPending_=false,failed_=false;
    std::uint32_t recoveryAttempts_=0,deviceGeneration_=0,stableFrames_=0;
    double retryAtMs_=0;
    void releaseGpu();
    void deviceLost(const std::string& message);
    void reportState(const char* state);
    bool instancingEnabled_=true;
    bool frustumCullingEnabled_=true;
    bool shadowsEnabled_=true,shadowOverflowReported_=false;
    bool lodEnabled_=true,shadowLodEnabled_=true,shadowCastingEnabled_=true,shadowReceivingEnabled_=true,shadowCullingEnabled_=true;
    bool shadowResourcesDirty_=false;
    std::uint32_t shadowResolution_=shadowResolution,shadowFilterTaps_=9;
    bool gpuTimingSupported_=false,gpuTimingEnabled_=false,gpuTimingFailed_=false;
    std::array<TimingSlot,3> timingSlots_{};
    PerformanceMonitor measurements_;
    FrameCounters counters_{};
    double lastReportMs_=-1000,lastGpuSampleMs_=-1000;
    double resourceMs_=0;
    std::uint64_t measurementEpoch_=0;
    void createPipeline();
    void createShadowResources();
    void createFxPipelines();
    void createClearPipeline();
    void prepareViewBindings(const RenderView& view);
    void prepareFx();
    void resize(std::uint32_t width,std::uint32_t height);
    void reserveObjects(std::size_t count);
    GpuMesh& upload(const std::shared_ptr<const Mesh>& mesh);
    GpuTexture& uploadTexture(const std::shared_ptr<const Texture>& texture,TextureSemantic semantic=TextureSemantic::Color);
    GpuMaterial& uploadMaterial(const Material& material);
    wgpu::RenderPipeline materialPipeline(const MeshDraw& draw,bool shadow=false);
    void uploadEnvironment();
    void uploadSkeleton(const std::shared_ptr<const SkinPose>& pose);
    void uploadMorph(const std::shared_ptr<const MorphPose>& pose);
    void fail(const std::string& message);
    void initializeTiming();
    void configureTimingSlot(TimingSlot& slot,std::uint32_t queryCount);
    int beginGpuSample(double nowMs);
    void readGpuSample(int slot);
    void reportMeasurements(double nowMs);
};
} // namespace engine
