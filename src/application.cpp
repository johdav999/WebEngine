#include "engine/application.hpp"
#include <atomic>
#include <limits>
#include <stdexcept>

namespace engine {
namespace {
std::atomic<std::uint64_t> nextApplicationIdentity{1};
struct BusyGuard {
    bool& busy;
    explicit BusyGuard(bool& value):busy(value) { busy=true; }
    ~BusyGuard() { busy=false; }
};
void tick(ApplicationClock& clock,double delta) {
    clock.delta=delta;clock.seconds+=delta;++clock.frames;
}
}
Application::Application(ApplicationSettings settings):settings_(settings),
    fixedClock_(settings.fixedStep,settings.maxFixedSteps),identity_(nextApplicationIdentity.fetch_add(1)) {
    if(!identity_) throw std::overflow_error("Application identities exhausted");
    if(!std::isfinite(settings.maxFrameDelta)||settings.maxFrameDelta<=0||settings.maxFrameDelta>0.25)
        throw std::invalid_argument("Invalid application frame delta limit");
}
Application::~Application() {
    // Explicit shutdown is required while the presentation host is still alive.
    binding_={};busy_=false;shutdown();
}
void Application::requireIdle() const {
    if(busy_) throw std::logic_error("Application operation requires an idle frame boundary");
}
void Application::requireLive() const {
    if(state_==ApplicationState::Stopped) throw std::logic_error("Application has shut down");
}
Scene* Application::findScene(SceneId id) const {
    if(!id||id.application!=identity_) return nullptr;
    const auto found=scenes_.find(id.value);return found==scenes_.end() ? nullptr : found->second.get();
}
SceneId Application::loadScene(std::unique_ptr<Scene> scene) {
    requireIdle();requireLive();
    if(!scene||scene->lifecycleState_!=SceneLifecycleState::Detached)
        throw std::invalid_argument("Expected a detached scene");
    if(nextScene_==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Scene IDs exhausted");
    BusyGuard guard(busy_);
    // Reserve ownership before running user code, so allocation cannot fail after onLoad.
    const SceneId id{nextScene_++,identity_};
    auto [entry,inserted]=scenes_.emplace(id.value,std::move(scene));(void)inserted;
    try { entry->second->onLoad(); }
    catch(...) { entry->second->onUnload();scenes_.erase(entry);throw; }
    entry->second->lifecycleState_=SceneLifecycleState::Loaded;return id;
}
SceneId Application::loadScene(const std::function<std::unique_ptr<Scene>()>& loader) {
    requireIdle();requireLive();
    if(!loader) throw std::invalid_argument("Expected a scene loader");
    std::unique_ptr<Scene> scene;
    { BusyGuard guard(busy_);scene=loader(); }
    return loadScene(std::move(scene));
}
bool Application::unloadScene(SceneId id) {
    requireIdle();requireLive();auto* scene=findScene(id);if(!scene) return false;
    if(id==active_) throw std::logic_error("Transition away before unloading the active scene");
    if(transition_&&transition_->next==id) throw std::logic_error("Cannot unload a pending transition target");
    BusyGuard guard(busy_);scene->onUnload();scene->lifecycleState_=SceneLifecycleState::Detached;
    preparations_.erase(id.value);scenes_.erase(id.value);return true;
}
void Application::prepareScene(SceneId id,std::shared_ptr<AssetPreparation> assets,
    std::function<void(Scene&,const AssetPreparation&)> prepare) {
    requireIdle();requireLive();
    if(!findScene(id)||id==active_||!assets) throw std::invalid_argument("Expected an inactive scene and asset preparation");
    preparations_.insert_or_assign(id.value,Preparation{std::move(assets),std::move(prepare),false,{}});
}
bool Application::preparationReady(SceneId id) {
    const auto found=preparations_.find(id.value);
    if(found==preparations_.end()||found->second.ready) return true;
    auto& preparation=found->second;
    if(!preparation.error.empty()) throw std::runtime_error(preparation.error);
    const auto progress=preparation.assets->progress();
    if(progress.status==AssetStatus::Failed||progress.status==AssetStatus::Cancelled) {
        preparation.error=progress.error.empty() ? "Scene asset preparation cancelled" : progress.error;
        preparation.assets->cancel();throw std::runtime_error(preparation.error);
    }
    if(progress.status!=AssetStatus::Ready) return false;
    BusyGuard guard(busy_);
    try {
        if(preparation.prepare) preparation.prepare(*findScene(id),*preparation.assets);
        preparation.ready=true;
    } catch(const std::exception& error) {
        preparation.error=error.what();if(preparation.error.empty()) preparation.error="Scene resource preparation failed";throw;
    }
    catch(...) { preparation.error="Scene resource preparation failed";throw; }
    return true;
}
void Application::setSceneBinding(std::function<void(Scene*)> binding) {
    requireIdle();
    if(state_!=ApplicationState::Created) throw std::logic_error("Configure scene binding before start");
    binding_=std::move(binding);
}
void Application::clearTiming() {
    lastTimestamp_.reset();fixedClock_.reset();input_.reset();
    frame_.input.delta=frame_.simulation.delta=frame_.presentation.delta=0;
    frame_.interval=0;frame_.fixedSteps=0;frame_.interpolationAlpha=1;
    if(auto* scene=activeScene()) if(auto* world=scene->physicsIfCreated()) world->setInterpolationAlpha(1);
}
void Application::resetClocks() { requireIdle();clearTiming(); }
void Application::start(SceneId initial) {
    requireIdle();
    if(state_!=ApplicationState::Created) throw std::logic_error("Application can only start once");
    auto* scene=findScene(initial);if(!scene) throw std::invalid_argument("Unknown initial scene");
    if(!preparationReady(initial)) throw std::logic_error("Initial scene assets are still loading");
    BusyGuard guard(busy_);if(binding_) binding_(scene);
    active_=initial;state_=ApplicationState::Running;scene->lifecycleState_=SceneLifecycleState::Active;
    scene->onActivate();clearTiming();
}
void Application::applyControl(ApplicationState next) {
    if(next==state_) return;
    BusyGuard guard(busy_);
    if(next==ApplicationState::Stopped) {
        if(binding_) binding_(nullptr); // Release host references before any scene destruction.
        if(auto* scene=activeScene()) { scene->onDeactivate();scene->lifecycleState_=SceneLifecycleState::Loaded; }
        active_={};transition_.reset();control_.reset();state_=next;clearTiming();
        for(auto& [id,scene]:scenes_) { (void)id;scene->onUnload();scene->lifecycleState_=SceneLifecycleState::Detached; }
        preparations_.clear();scenes_.clear();assets_.clear();return;
    }
    state_=next;clearTiming();
    if(auto* scene=activeScene()) {
        if(next==ApplicationState::Paused) scene->onPause();else scene->onResume();
    }
}
void Application::pause() {
    requireLive();if(state_==ApplicationState::Created) throw std::logic_error("Start before pausing");
    if(busy_) { if(control_!=ApplicationState::Stopped) control_=ApplicationState::Paused; }
    else applyControl(ApplicationState::Paused);
}
void Application::resume() {
    requireLive();if(state_==ApplicationState::Created) throw std::logic_error("Start before resuming");
    if(busy_) { if(control_!=ApplicationState::Stopped) control_=ApplicationState::Running; }
    else applyControl(ApplicationState::Running);
}
void Application::shutdown() {
    if(state_==ApplicationState::Stopped) return;
    if(busy_) control_=ApplicationState::Stopped;else applyControl(ApplicationState::Stopped);
}
void Application::setSuspended(bool value) {
    requireIdle();requireLive();if(value==suspended_) return;
    suspended_=value;clearTiming();
}
bool Application::submitInput(std::span<const InputPacket> packets) {
    if(state_==ApplicationState::Created||state_==ApplicationState::Stopped||suspended_) return false;
    return input_.submit(packets);
}
void Application::requestTransition(SceneId next,bool unloadPrevious) {
    requireLive();
    if(state_==ApplicationState::Created) throw std::logic_error("Start before transitioning");
    if(!findScene(next)) throw std::invalid_argument("Unknown transition scene");
    transition_=Transition{next,unloadPrevious};
}
bool Application::commitTransition() {
    requireIdle();requireLive();if(!transition_) return false;
    const auto request=*transition_;transition_.reset();
    if(!preparationReady(request.next)) {
        if(!transition_) transition_=request;
        return false;
    }
    // Preparation is user code: a newer request must survive this boundary.
    if(transition_) return false;
    if(request.next==active_) return false;
    auto* next=findScene(request.next);if(!next) throw std::invalid_argument("Unknown transition scene");
    const auto previous=active_;auto* old=activeScene();
    {
        BusyGuard guard(busy_);if(binding_) binding_(next);
        if(old) { old->onDeactivate();old->lifecycleState_=SceneLifecycleState::Loaded; }
        active_=request.next;next->lifecycleState_=SceneLifecycleState::Active;clearTiming();
        next->onActivate();if(state_==ApplicationState::Paused) next->onPause();
    }
    if(request.unloadPrevious) unloadScene(previous);
    return true;
}
const ApplicationFrame& Application::advance(double timestamp,const ApplicationFrameCallbacks& callbacks) {
    requireIdle();
    if(!std::isfinite(timestamp)||timestamp<0||(lastTimestamp_&&timestamp<*lastTimestamp_))
        throw std::invalid_argument("Expected a monotonic finite timestamp in seconds");
    frame_.input.delta=frame_.simulation.delta=frame_.presentation.delta=0;frame_.fixedSteps=0;frame_.interval=0;
    if(control_) { const auto next=*control_;control_.reset();applyControl(next); }
    if(state_!=ApplicationState::Stopped) { BusyGuard guard(busy_);assets_.update(); }
    if(state_==ApplicationState::Created||state_==ApplicationState::Stopped||suspended_) return frame_;
    commitTransition();
    if(control_) { const auto next=*control_;control_.reset();applyControl(next); }
    if(state_==ApplicationState::Stopped) return frame_;
    frame_.interval=lastTimestamp_ ? timestamp-*lastTimestamp_ : 0;lastTimestamp_=timestamp;
    const double delta=std::min(frame_.interval,settings_.maxFrameDelta);
    tick(frame_.input,delta);tick(frame_.presentation,delta);
    try {
        BusyGuard guard(busy_);auto& scene=*activeScene();input_.beginFrame();
        if(callbacks.input) callbacks.input(scene,input_,frame_);
        if(state_==ApplicationState::Running&&!control_) {
            tick(frame_.simulation,delta);
            if(callbacks.simulate) callbacks.simulate(scene,frame_);
            if(!control_) {
                scene.update(delta);
                if(!control_) {
                    frame_.fixedSteps=fixedClock_.advance(scene,frame_.interval);
                    frame_.fixedSeconds+=frame_.fixedSteps*settings_.fixedStep;
                    frame_.droppedSimulationSeconds=fixedClock_.droppedSeconds();frame_.interpolationAlpha=fixedClock_.alpha();
                }
            }
        } else scene.synchronize();
        if(control_!=ApplicationState::Stopped) {
            if(callbacks.afterSimulation) callbacks.afterSimulation(scene,frame_);
            if(callbacks.present) callbacks.present(scene,frame_);
        }
    } catch(...) {
        const auto next=control_==ApplicationState::Stopped ? ApplicationState::Stopped : ApplicationState::Paused;
        control_.reset();applyControl(next);clearTiming();throw;
    }
    if(control_) { const auto next=*control_;control_.reset();applyControl(next); }
    return frame_;
}
} // namespace engine
