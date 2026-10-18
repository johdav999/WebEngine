#include "engine/renderer.hpp"
#include "webgpu_renderer.hpp"
#include "webgl_renderer.hpp"
#include <emscripten.h>
EM_JS(int, renderer_webgpu_available, (), { return isSecureContext && !!navigator.gpu; });
namespace engine {
struct Renderer::Impl {
    WebGpuRenderer gpu;
    WebGlRenderer gl;
    bool useGl=false,forceGpu=false,initialized=false;
    explicit Impl(Scene& scene):gpu(scene),gl(scene) {}
};
Renderer::Renderer(Scene& scene):impl_(std::make_unique<Impl>(scene)) {}
Renderer::Renderer(Scene& scene,CameraComponent& camera):Renderer(scene) { const RenderView view{&camera};setViews({&view,1}); }
Renderer::~Renderer()=default;
void Renderer::initialize(RenderBackendPreference preference) {
    if(impl_->initialized) return;
    if(preference!=RenderBackendPreference::Automatic&&preference!=RenderBackendPreference::WebGpu&&preference!=RenderBackendPreference::WebGl2)
        throw std::invalid_argument("Unknown renderer backend preference");
    impl_->initialized=true;
    impl_->useGl=preference==RenderBackendPreference::WebGl2||(preference==RenderBackendPreference::Automatic&&!renderer_webgpu_available());
    impl_->forceGpu=preference==RenderBackendPreference::WebGpu;
    impl_->gpu.setFallbackAllowed(!impl_->forceGpu);
    if(impl_->useGl) impl_->gl.initialize();else impl_->gpu.initialize();
}
void Renderer::serviceRecovery() {
    if(impl_->useGl) { impl_->gl.serviceRecovery();return; }
    impl_->gpu.serviceRecovery();
    // Adapter/device failure has not acquired a canvas context. Fall back at the
    // host boundary, never from inside a pending WebGPU callback.
    if(!impl_->forceGpu && impl_->gpu.canFallback()) { impl_->gpu.abandon();impl_->useGl=true;impl_->gl.initialize(); }
}
void Renderer::retryRecovery() { if(impl_->useGl) impl_->gl.retryRecovery();else impl_->gpu.retryRecovery(); }
void Renderer::setScene(Scene& scene,std::span<const RenderView> views) { if(!impl_->useGl) impl_->gpu.setScene(scene,views);impl_->gl.setScene(scene,views);setWaterInteractions({}); }
void Renderer::setViews(std::span<const RenderView> views) { if(!impl_->useGl) impl_->gpu.setViews(views);impl_->gl.setViews(views); }
std::span<const RenderView> Renderer::views() const { if(impl_->useGl) return impl_->gl.views();return impl_->gpu.views(); }
void Renderer::setWaterTime(double seconds) { validateWaterTime(seconds);impl_->gpu.setWaterTime(seconds);impl_->gl.setWaterTime(seconds); }
double Renderer::waterTime() const { return impl_->useGl ? impl_->gl.waterTime() : impl_->gpu.waterTime(); }
void Renderer::setWaterInteractions(std::span<const WaterInteraction> values) {
    WaterInteractions validated;validated.set(values);
    impl_->gpu.setWaterInteractions(validated);impl_->gl.setWaterInteractions(validated);
}
void Renderer::setQuality(const RenderQuality& quality) { impl_->gl.setQuality(quality);if(!impl_->useGl) impl_->gpu.setQuality(quality); }
const RenderQuality& Renderer::quality() const { if(impl_->useGl) return impl_->gl.quality();return impl_->gpu.quality(); }
const RenderCapabilities& Renderer::capabilities() const { if(impl_->useGl) return impl_->gl.capabilities();return impl_->gpu.capabilities(); }
std::uint32_t Renderer::effectiveMsaaSamples() const { return impl_->useGl ? impl_->gl.effectiveMsaaSamples() : impl_->gpu.effectiveMsaaSamples(); }
void Renderer::resizeCanvas() { if(impl_->useGl) impl_->gl.resizeCanvas();else impl_->gpu.resizeCanvas(); }
void Renderer::refreshQualityReport() { if(impl_->useGl) impl_->gl.refreshQualityReport();else impl_->gpu.refreshQualityReport(); }
void Renderer::simulateDeviceLoss() { if(impl_->useGl) impl_->gl.simulateDeviceLoss();else impl_->gpu.simulateDeviceLoss(); }
void Renderer::render(double updateMs,double intervalMs) { if(impl_->useGl) impl_->gl.render(updateMs,intervalMs);else impl_->gpu.render(updateMs,intervalMs); }
void Renderer::resetMeasurements() { if(impl_->useGl) impl_->gl.resetMeasurements();else impl_->gpu.resetMeasurements(); }
void Renderer::setInstancingEnabled(bool enabled) { if(!impl_->useGl) impl_->gpu.setInstancingEnabled(enabled);impl_->gl.setInstancingEnabled(enabled); }
void Renderer::setFrustumCullingEnabled(bool enabled) { if(!impl_->useGl) impl_->gpu.setFrustumCullingEnabled(enabled);impl_->gl.setFrustumCullingEnabled(enabled); }
void Renderer::setShadowsEnabled(bool enabled) { if(!impl_->useGl) impl_->gpu.setShadowsEnabled(enabled);impl_->gl.setShadowsEnabled(enabled); }
void Renderer::setLodEnabled(bool enabled) { if(!impl_->useGl) impl_->gpu.setLodEnabled(enabled);impl_->gl.setLodEnabled(enabled); }
void Renderer::setShadowLodEnabled(bool enabled) { if(!impl_->useGl) impl_->gpu.setShadowLodEnabled(enabled);impl_->gl.setShadowLodEnabled(enabled); }
void Renderer::setShadowCastingEnabled(bool enabled) { if(!impl_->useGl) impl_->gpu.setShadowCastingEnabled(enabled);impl_->gl.setShadowCastingEnabled(enabled); }
void Renderer::setShadowReceivingEnabled(bool enabled) { if(!impl_->useGl) impl_->gpu.setShadowReceivingEnabled(enabled);impl_->gl.setShadowReceivingEnabled(enabled); }
void Renderer::setShadowCullingEnabled(bool enabled) { if(!impl_->useGl) impl_->gpu.setShadowCullingEnabled(enabled);impl_->gl.setShadowCullingEnabled(enabled); }
void Renderer::setShadowQuality(std::uint32_t resolution,std::uint32_t taps) { if(!impl_->useGl) impl_->gpu.setShadowQuality(resolution,taps);impl_->gl.setShadowQuality(resolution,taps); }
void Renderer::setGui(GuiSystem* gui) { if(!impl_->useGl) impl_->gpu.setGui(gui);impl_->gl.setGui(gui); }
bool Renderer::ready() const { if(impl_->useGl) return impl_->gl.ready();return impl_->gpu.ready(); }
}

namespace engine {
void Renderer::setAssetUploadBudget(UploadBudget budget) { impl_->gpu.setAssetUploadBudget(budget);impl_->gl.setAssetUploadBudget(budget); }
const UploadBudget& Renderer::assetUploadBudget() const { return impl_->useGl ? impl_->gl.assetUploadBudget() : impl_->gpu.assetUploadBudget(); }
}
