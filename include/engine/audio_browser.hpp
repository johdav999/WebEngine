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
    void listener(const AudioListenerPose&) noexcept override;
    bool active() const noexcept override;
    void activate() noexcept override;
    void suspend(bool) noexcept override;
private:
    std::uint32_t device_;
};
} // namespace engine
