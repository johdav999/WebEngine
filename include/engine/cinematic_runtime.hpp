#pragma once
#include "engine/audio.hpp"
#include "engine/cinematic_sequence.hpp"
#include <functional>

namespace engine {
class CameraComponent;
enum class CinematicRuntimeStatus { Idle, Playing, Completed, Skipped, Cancelled, Failed };
using CinematicAudioResolver = std::function<std::shared_ptr<const AudioStreamAsset>(std::string_view)>;

// One live playback per application-controlled session. The host owns gameplay
// input, simulation pause, presentation binding and completion policy. Call on
// the scene owner while idle, after simulation and before camera/render binding.
class CinematicRuntime final {
public:
    CinematicRuntime();
    ~CinematicRuntime();
    CinematicRuntime(const CinematicRuntime&) = delete;
    CinematicRuntime& operator=(const CinematicRuntime&) = delete;

    // Binds and validates before taking control. An enabled sound track requires
    // a streaming backend; the resolver is called synchronously and is not retained.
    void start(Scene&,std::shared_ptr<const CinematicSequence>,
        std::unique_ptr<AudioBackend> = {},CinematicAudioResolver = {});
    void advance(Scene&,double presentationDelta);
    void skip(Scene&);
    void cancel(Scene&) noexcept;
    // Cancel the current session and restore surviving presentation targets.
    void abandon() noexcept;

    CinematicRuntimeStatus status() const;
    const std::shared_ptr<const CinematicSequence>& asset() const;
    double time() const;
    std::string_view subtitle() const;
    std::string_view error() const;
    // Borrowed for immediate host presentation; null means use the gameplay view.
    CameraComponent* camera(Scene&) const;
    // Owned by the playback. The host activates/suspends its backend as needed.
    AudioSystem* audio() const;
private:
    friend class Scene;
    void sceneLost(Scene&) noexcept;
    bool ownsSubtree(const Node&) const noexcept;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool starting_=false;
};
} // namespace engine
