#pragma once
#include "engine/input.hpp"
#include "engine/assets.hpp"
#include "engine/physics.hpp"
#include "engine/scene.hpp"
#include <functional>
#include <map>
#include <optional>

namespace engine {
enum class ApplicationState { Created, Running, Paused, Stopped };
struct SceneId {
    std::uint64_t value=0,application=0;
    explicit operator bool() const { return value!=0&&application!=0; }
    bool operator==(const SceneId&) const=default;
};
struct ApplicationSettings {
    double maxFrameDelta=0.05,fixedStep=1.0/60.0;
    std::size_t maxFixedSteps=8;
};
struct ApplicationClock {
    double seconds=0,delta=0;
    std::uint64_t frames=0;
};
struct ApplicationFrame {
    ApplicationClock input,simulation,presentation;
    double interval=0,fixedSeconds=0,droppedSimulationSeconds=0,interpolationAlpha=1;
    std::size_t fixedSteps=0;
};
struct ApplicationFrameCallbacks {
    std::function<void(Scene&,const InputSystem&,const ApplicationFrame&)> input;
    // Runs before the scene's variable update, using the simulation clock.
    std::function<void(Scene&,const ApplicationFrame&)> simulate;
    std::function<void(Scene&,const ApplicationFrame&)> afterSimulation;
    std::function<void(Scene&,const ApplicationFrame&)> present;
};

// Single-threaded owner of scenes, input snapshots and all frame clocks.
// Host timestamps are monotonic seconds. Hosts submit input before advance().
class Application {
public:
    explicit Application(ApplicationSettings settings={});
    ~Application();
    Application(const Application&)=delete;
    Application& operator=(const Application&)=delete;
    SceneId loadScene(std::unique_ptr<Scene> scene); // Calls onLoad; failure retains the active scene.
    SceneId loadScene(const std::function<std::unique_ptr<Scene>()>& loader);
    bool unloadScene(SceneId id); // Idle, inactive scenes only; destroys their actors/resources.
    Scene* findScene(SceneId id) const;
    Scene* activeScene() const { return findScene(active_); }
    SceneId activeSceneId() const { return active_; }
    std::size_t loadedSceneCount() const { return scenes_.size(); }
    AssetManager& assets() { return assets_; }
    // Inactive scenes only. The callback builds/validates CPU or backend resources
    // after every dependency is ready, before presentation binding/activation.
    void prepareScene(SceneId,std::shared_ptr<AssetPreparation>,
        std::function<void(Scene&,const AssetPreparation&)> prepare={});
    ApplicationState state() const { return state_; }
    bool suspended() const { return suspended_; }
    const ApplicationFrame& clocks() const { return frame_; }
    const InputSystem& input() const { return input_; }
    bool submitInput(std::span<const InputPacket> packets);

    // Binding validates/rebinds the presentation host before scene activation.
    // nullptr releases all scene/camera references before shutdown destruction.
    // On exception it must leave its previous binding intact; keep it alive through shutdown.
    void setSceneBinding(std::function<void(Scene*)> binding);
    void start(SceneId initialScene);
    void pause();
    void resume();
    void shutdown(); // Terminal, idempotent; deferred when called from a frame callback.
    void setSuspended(bool value); // Host visibility; preserves explicit pause state.
    void resetClocks(); // Drops baselines/input/fixed remainder, preserves elapsed totals.

    // Last request wins. Requests from ticks/callbacks commit before the next input snapshot.
    void requestTransition(SceneId next,bool unloadPrevious=false);
    bool commitTransition(); // Optional explicit idle boundary; binding failure preserves old scene.
    const ApplicationFrame& advance(double timestamp,const ApplicationFrameCallbacks& callbacks={});
private:
    struct Transition { SceneId next;bool unloadPrevious; };
    struct Preparation {
        std::shared_ptr<AssetPreparation> assets;
        std::function<void(Scene&,const AssetPreparation&)> prepare;
        bool ready=false;
        std::string error;
    };
    ApplicationSettings settings_;
    FixedStepClock fixedClock_;
    InputSystem input_;
    AssetManager assets_;
    std::map<std::uint64_t,std::unique_ptr<Scene>> scenes_;
    std::map<std::uint64_t,Preparation> preparations_;
    std::uint64_t identity_,nextScene_=1;
    SceneId active_;
    ApplicationState state_=ApplicationState::Created;
    bool suspended_=false,busy_=false;
    std::optional<double> lastTimestamp_;
    std::optional<Transition> transition_;
    std::optional<ApplicationState> control_;
    std::function<void(Scene*)> binding_;
    ApplicationFrame frame_;
    void requireIdle() const;
    void requireLive() const;
    void applyControl(ApplicationState state);
    void clearTiming();
    bool preparationReady(SceneId);
};
} // namespace engine
