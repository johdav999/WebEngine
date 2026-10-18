#pragma once
#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include "engine/performance.hpp"
#include "engine/render_frame.hpp"
#include "engine/fx_frame.hpp"
#include "engine/render_view.hpp"
#include "engine/render_quality.hpp"
#include "engine/upload_budget.hpp"
#include "engine/water.hpp"
#include <array>

namespace engine {
class GuiSystem;
class GuiRenderer;
class Renderer {
public:
    explicit Renderer(Scene& scene);
    Renderer(Scene& scene, CameraComponent& camera);
    void setScene(Scene& scene,std::span<const RenderView> views); // Validates before switching; caller retains ownership.
    void setViews(std::span<const RenderView> views);
    std::span<const RenderView> views() const;
    ~Renderer();
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    void initialize(RenderBackendPreference preference=RenderBackendPreference::Automatic);
    void serviceRecovery(); // Host calls at frame boundaries, including while not ready.
    void retryRecovery(); // Explicit retry after bounded automatic attempts are exhausted.
    void setQuality(const RenderQuality& quality);
    void setWaterTime(double seconds); // Explicit presentation clock, shared across views.
    double waterTime() const;
    void setWaterInteractions(std::span<const WaterInteraction> values); // Copied, maximum 16; empty clears.
    const RenderQuality& quality() const;
    const RenderCapabilities& capabilities() const;
    std::uint32_t effectiveMsaaSamples() const; // 1 before targets exist or when 4x is unsupported.
    void resizeCanvas();
    void refreshQualityReport();
    void simulateDeviceLoss(); // Host may expose this through an opt-in acceptance fixture.
    void render(double updateMs=0,double intervalMs=0);
    void resetMeasurements();
    void setAssetUploadBudget(UploadBudget budget);
    const UploadBudget& assetUploadBudget() const;
    void setInstancingEnabled(bool enabled);
    void setFrustumCullingEnabled(bool enabled);
    void setShadowsEnabled(bool enabled);
    void setLodEnabled(bool enabled);
    void setShadowLodEnabled(bool enabled);
    void setShadowCastingEnabled(bool enabled);
    void setShadowReceivingEnabled(bool enabled);
    void setShadowCullingEnabled(bool enabled);
    void setShadowQuality(std::uint32_t resolution,std::uint32_t taps);
    void setGui(GuiSystem* gui);
    bool ready() const;
    // Each ordered layer has its own depth range. Overlay meshes render above World meshes.
    static constexpr std::size_t maxLights=8;
    static constexpr std::size_t maxShadowLights=4;
    static constexpr std::uint32_t shadowResolution=1024;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace engine
