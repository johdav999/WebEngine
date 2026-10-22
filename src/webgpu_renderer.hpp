#pragma once
#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include "engine/performance.hpp"
#include "engine/render_frame.hpp"
#include "engine/fx_frame.hpp"
#include "engine/render_view.hpp"
#include "engine/render_quality.hpp"
#include "engine/upload_budget.hpp"
#include "webgpu_host.hpp"
#include <array>
#include <unordered_map>
#include <map>

namespace engine {
class GuiSystem;
class GuiRenderer;
class WebGpuRenderer {
public:
    WebGpuRenderer(Scene& scene,WebGpuHost& host);
#ifdef __EMSCRIPTEN__
    explicit WebGpuRenderer(Scene& scene);
    WebGpuRenderer(Scene& scene, CameraComponent& camera);
#endif
    void setScene(Scene& scene,std::span<const RenderView> views); // Validates before switching; caller retains ownership.
    void setViews(std::span<const RenderView> views);
    std::span<const RenderView> views() const { return views_.views(); }
    ~WebGpuRenderer();
    WebGpuRenderer(const WebGpuRenderer&)=delete;
    WebGpuRenderer& operator=(const WebGpuRenderer&)=delete;
    void initialize();
    void serviceRecovery(); // Host calls at frame boundaries, including while not ready.
    void retryRecovery(); // Explicit retry after bounded automatic attempts are exhausted.
    void setQuality(const RenderQuality& quality);
    void setWaterTime(double seconds) { validateWaterTime(seconds);waterTime_=seconds; }
    double waterTime() const { return waterTime_; }
    void setWaterInteractions(const WaterInteractions& values) { waterInteractions_=values; }
    const RenderQuality& quality() const { return quality_; }
    const RenderCapabilities& capabilities() const { return capabilities_; }
    std::uint32_t effectiveMsaaSamples() const { return depth_ ? sampleCount_ : 1u; }
    void resizeCanvas();
    void refreshQualityReport() { reportState(ready_ ? "ready" : failed_ ? "failed" : "initializing"); }
    void simulateDeviceLoss(); // Host may expose this through an opt-in acceptance fixture.
    void render(double updateMs=0,double intervalMs=0);
    void setAssetUploadBudget(UploadBudget budget) { budget.validate();uploadBudget_=budget; }
    const UploadBudget& assetUploadBudget() const { return uploadBudget_; }
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
    bool canFallback() const { return failed_ && !canvasAcquired_; }
    void abandon() { releaseGpu();failed_=true; }
    void setFallbackAllowed(bool allowed) { fallbackAllowed_=allowed; }
private:
    WebGpuHost& host_;
    struct CallbackState { WebGpuRenderer* owner; };
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
        std::array<const Texture*,9> textures;
        std::array<float,106> parameters;
        std::uint32_t flags=0;
        bool operator==(const MaterialKey&) const=default;
    };
    struct MaterialKeyHash { std::size_t operator()(const MaterialKey& key) const; };
    double waterTime_=0;
    WaterInteractions waterInteractions_;
    UploadBudget uploadBudget_;
    UploadAdmission uploadAdmission_;
    double uploadStarted_=0;
    void admitUpload(std::uint64_t bytes);
    MaterialKey materialKey(const Material& material) const;
    bool resourcesReady(const MeshDraw& draw) const;
    struct GpuMaterial {
        wgpu::Buffer buffer;wgpu::BindGroup binding;
        std::array<std::shared_ptr<const Texture>,9> sources;
        std::uint64_t used=0;
    };
    struct TimingSlot {
        wgpu::QuerySet queries;
        wgpu::Buffer resolve,readback;
        std::uint32_t queryCount=0;
        std::vector<std::uint32_t> shadowCounts;
        std::vector<bool> waterPasses,reflectionPasses,underwaterPasses;
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
    bool hdr_=false;
    wgpu::TextureFormat colorFormat() const {return hdr_?wgpu::TextureFormat::RGBA16Float:format_;}
    wgpu::Texture bloomTexture_;
    wgpu::TextureView bloomView_;
    wgpu::BindGroupLayout postLayout_;
    wgpu::RenderPipeline bloomPipeline_,postPipeline_;
    wgpu::Buffer postBuffer_;
    std::array<float,8> postValues_{};
    std::uint32_t postWidth_=0,postHeight_=0;
    void drawPost(wgpu::CommandEncoder&,wgpu::TextureView);

    wgpu::Texture depth_;
    wgpu::TextureView depthView_;
    bool waterCapture_=false;
    std::vector<std::optional<UnderwaterView>> underwaterViews_;
    wgpu::BindGroupLayout underwaterLayout_;
    wgpu::RenderPipeline underwaterPipeline_;
    std::uint32_t underwaterSamples_=0;
    void drawUnderwater(wgpu::CommandEncoder&,wgpu::TextureView,const UnderwaterView&,int,std::uint32_t);
    wgpu::Texture reflectionColor_,reflectionDepth_;
    wgpu::TextureView reflectionColorView_,reflectionDepthView_;
    std::uint32_t reflectionWidth_=0,reflectionHeight_=0;
    void prepareReflectionTargets(bool required);
    std::uint32_t waterWidth_=0,waterHeight_=0,waterSamples_=0;
    std::uint64_t waterTargetRevision_=0;
    wgpu::Texture sceneColor_,waterColor_,waterDepth_;
    wgpu::TextureView sceneColorView_,waterColorView_,waterDepthView_;
    wgpu::Sampler waterSampler_;
    std::array<wgpu::BindGroupLayout,2> captureLayouts_;
    std::array<wgpu::RenderPipeline,2> capturePipelines_;
    wgpu::RenderPipeline waterPresentPipeline_;
    wgpu::BindGroup captureBinding_;
    void createWaterPipelines();
    void prepareWaterTargets(bool required);
    void captureWater(wgpu::CommandEncoder&,int sampleSlot,std::uint32_t viewOrdinal,bool underwater=false);
    wgpu::Texture msaaColor_;
    wgpu::TextureView msaaColorView_;
    std::uint32_t sampleCount_=1;
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
    std::array<wgpu::RenderPipeline,2> particlePipelines_,ribbonPipelines_;
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
        wgpu::BindGroup binding,reflectionBinding,underwaterBinding,particleBinding,clearBinding;
        wgpu::Buffer underwaterBuffer;
        std::array<std::byte,256> underwaterShadow{};
        bool underwaterValid=false;
        std::uint64_t underwaterRevision=0;
        wgpu::BindGroup skyBinding,skyTextureBinding;
        wgpu::Buffer skyBuffer;
        SkyUniform skyShadow{};
        bool skyValid=false;
        std::shared_ptr<const Texture> skyTexture;
        wgpu::Buffer reflectionBuffer,frameBuffer,objectBuffer,particleCameraBuffer,particleBuffer,ribbonBuffer,clearBuffer;
        std::size_t objectCapacity=0,particleCapacity=0,ribbonCapacity=0;
        std::vector<ParticleInstance> particleShadow;
        std::vector<RibbonVertex> ribbonShadow;
        FogUniform fog;
        std::array<std::byte,2336> frameUniformShadow{};
        std::array<wgpu::Buffer,maxShadowLights> shadowBuffers;
        std::array<wgpu::BindGroup,maxShadowLights> shadowBindings;
        std::array<Mat4,maxShadowLights> shadowMatrices{};
        std::array<bool,maxShadowLights> shadowMatrixValid{};
        std::array<float,maxShadowLights> shadowWaterTime{};
        std::array<std::byte,256> particleCameraShadow{};
        std::array<float,4> clearShadow{};
        bool frameUniformValid=false,particleCameraValid=false,clearValid=false;
        std::shared_ptr<const Environment> boundEnvironment;
        std::uint64_t waterRevision=0;
    };
    std::vector<std::unique_ptr<ViewResources>> viewResources_;
    ViewResources* activeView_=nullptr; // Only used while preparing/encoding a resolved view.
    CameraComponent* activeCamera_=nullptr;
    wgpu::RenderPipeline clearPipeline_;
    wgpu::BindGroupLayout clearLayout_;
    wgpu::RenderPipeline skyPipeline_,skySpherePipeline_;
    wgpu::BindGroupLayout skyLayout_;
    wgpu::Sampler skySampler_;
    std::uint32_t width_=0,height_=0;
    bool ready_=false, lightOverflowReported_=false,canvasAcquired_=false,fallbackAllowed_=true;
    RenderQuality quality_;
    RenderCapabilities capabilities_;
    bool initializing_=false,recoveryPending_=false,failed_=false;
    std::uint32_t recoveryAttempts_=0,deviceGeneration_=0,stableFrames_=0;
    double retryAtMs_=0,initializationStartedMs_=0;
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
    void createSkyPipeline();
    bool prepareSkyBindings(const RenderView& view,float aspect);
    void prepareViewBindings(const RenderView& view);
    void prepareFx();
    void resize(std::uint32_t width,std::uint32_t height);
    void reserveObjects(std::size_t count);
    GpuMesh& upload(const std::shared_ptr<const Mesh>& mesh);
    GpuTexture& uploadTexture(const std::shared_ptr<const Texture>& texture,TextureSemantic semantic=TextureSemantic::Color);
    GpuMaterial& uploadMaterial(const Material& material);
    wgpu::RenderPipeline materialPipeline(const MeshDraw& draw,bool shadow=false,bool reflection=false);
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

