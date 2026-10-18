#pragma once
#include "engine/renderer.hpp"
#include "engine/gui.hpp"
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <unordered_map>

namespace engine {
// Browser-only backend. All scene, camera and GUI references are borrowed.
class WebGlRenderer {
public:
    explicit WebGlRenderer(Scene& scene):scene_(&scene) {}
    ~WebGlRenderer();
    void initialize();
    void serviceRecovery();
    void retryRecovery();
    void simulateDeviceLoss();
    void setScene(Scene& scene,std::span<const RenderView> views);
    void setViews(std::span<const RenderView> views) { views_.set(views); }
    std::span<const RenderView> views() const { return views_.views(); }
    void setQuality(const RenderQuality& quality);
    void setWaterTime(double seconds) { validateWaterTime(seconds);waterTime_=seconds; }
    double waterTime() const { return waterTime_; }
    void setWaterInteractions(const WaterInteractions& values) { waterInteractions_=values; }
    const RenderQuality& quality() const { return quality_; }
    const RenderCapabilities& capabilities() const { return capabilities_; }
    std::uint32_t effectiveMsaaSamples() const { return sampleCount_; }
    void resizeCanvas();
    void refreshQualityReport();
    void render(double updateMs,double intervalMs);
    void setAssetUploadBudget(UploadBudget budget) { budget.validate();uploadBudget_=budget; }
    const UploadBudget& assetUploadBudget() const { return uploadBudget_; }
    void resetMeasurements() { measurements_.reset();lastReport_=-1000; }
    void setInstancingEnabled(bool) {} // Draws individually; capability report declares this.
    void setFrustumCullingEnabled(bool enabled) { culling_=enabled; }
    void setShadowsEnabled(bool) {}
    void setLodEnabled(bool enabled) { quality_.lod=enabled; }
    void setShadowLodEnabled(bool enabled) { quality_.shadowLod=enabled; }
    void setShadowCastingEnabled(bool) {}
    void setShadowReceivingEnabled(bool) {}
    void setShadowCullingEnabled(bool) {}
    void setShadowQuality(std::uint32_t resolution,std::uint32_t taps);
    void setGui(GuiSystem* gui) { gui_=gui; }
    bool ready() const { return ready_; }
private:
    double waterTime_=0;
    WaterInteractions waterInteractions_;
    WaterInteractionParameters interactionParameters_;
    UploadBudget uploadBudget_;
    UploadAdmission uploadAdmission_;
    double uploadStarted_=0;
    bool budgeted_=false;
    void admitUpload(std::uint64_t bytes);
    struct GpuMesh {
        std::weak_ptr<const Mesh> source;
        GLuint vao=0,vertices=0,indices=0;
        std::uint64_t bytes=0;
        bool deformed=false;
    };
    struct GpuTexture { std::weak_ptr<const Texture> source;GLuint image=0;std::uint64_t bytes=0; };
    Scene* scene_;
    GuiSystem* gui_=nullptr;
    RenderViews views_;
    std::vector<ResolvedRenderView> resolved_;
    RenderFrame frame_;
    FxFrame fx_;
    std::vector<Vertex> deformed_;
    std::unordered_map<const Mesh*,GpuMesh> meshes_;
    std::unordered_map<const Texture*,GpuTexture> textures_;
    RenderQuality quality_;
    RenderCapabilities capabilities_;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_=0;
    GLuint meshProgram_=0,guiProgram_=0,guiVao_=0,guiVertices_=0,guiIndices_=0,particleVao_=0,particleBuffer_=0,ribbonVao_=0,ribbonBuffer_=0;
    GLuint skyProgram_=0,skyVao_=0,skySampler_=0;
    bool waterCapture_=false;
    std::vector<std::optional<UnderwaterView>> underwaterViews_;
    GLuint underwaterProgram_=0,underwaterVao_=0;
    void drawUnderwater(const UnderwaterView&,const CameraComponent&,const ResolvedRenderView&);
    bool reflectionPass_=false;
    GLuint reflectionFramebuffer_=0,reflectionColor_=0,reflectionDepth_=0;
    std::uint32_t reflectionWidth_=0,reflectionHeight_=0;
    void prepareReflectionTargets(bool required);
    void releaseReflectionTargets(bool lost=false);
    GLuint sceneFramebuffer_=0,sceneColor_=0,sceneDepth_=0,waterFramebuffer_=0,waterColor_=0,waterDepth_=0;
    std::uint32_t waterWidth_=0,waterHeight_=0;
    void prepareWaterTargets(bool required);
    void releaseWaterTargets(bool lost=false);
    void captureWater();
    GLuint msaaFramebuffer_=0,msaaColor_=0,msaaDepth_=0;
    std::uint32_t targetWidth_=0,targetHeight_=0,sampleCount_=1;
    void resizeTargets(std::uint32_t width,std::uint32_t height,std::uint32_t samples);
    bool ready_=false,failed_=false,restorePending_=false,culling_=true;
    std::uint32_t width_=0,height_=0,generation_=0;
    double lastReport_=-1000;
    PerformanceMonitor measurements_;
    FrameCounters counters_;
    void createResources();
    void releaseResources(bool lost=false);
    void fail(const std::string& message);
    GpuMesh& mesh(const MeshDraw& draw);
    GLuint texture(const std::shared_ptr<const Texture>& source);
    void drawMesh(const MeshDraw& draw,const Mat4& vp,const SceneFrame& frame);
    void drawGui(double now);
    void drawSky(const RenderView& view,float aspect);
    void drawRibbon(const FxBatch& batch,const Mat4& vp);
    void drawParticles(const FxBatch& batch,const CameraComponent& camera,const Mat4& vp);
    static bool lost(int,const void*,void* data);
    static bool restored(int,const void*,void* data);
};
}
