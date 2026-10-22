#pragma once
#include "engine/audio.hpp"

namespace engine {
// Emscripten-only implementation. Javascript and device types stay in the adapter.
class BrowserAudioBackend final : public AudioBackend {
public:
    BrowserAudioBackend();
    std::uint32_t deviceId() const { return device_; } // Browser decode/status bridge.
    ~BrowserAudioBackend() override;
    bool start(std::uint32_t,const SoundAsset&,const AudioPlayOptions&,float) override;
    void stop(std::uint32_t) noexcept override;
    bool playing(std::uint32_t) const noexcept override;
    void gain(std::uint32_t,float) noexcept override;
    void position(std::uint32_t,Vec3) noexcept override;
    bool playbackRate(std::uint32_t,float) noexcept override;
    void listener(const AudioListenerPose&) noexcept override;
    bool active() const noexcept override;
    void activate() noexcept override;
    void suspend(bool) noexcept override;
    AudioCapabilities capabilities() const noexcept override { return {true,true,true}; }
    bool startStream(std::uint32_t,const AudioStreamAsset&,const AudioPlayOptions&,float) override;
    bool reverb(const AudioReverbSettings&) override;
    void environment(std::uint32_t,float,float) noexcept override;
    void muteOutput(bool) noexcept override;
    void clearEffects() noexcept override;
    bool seekStream(std::uint32_t,double) noexcept override;
    double streamPosition(std::uint32_t) const noexcept override;
private:
    std::uint32_t device_;
};
} // namespace engine
