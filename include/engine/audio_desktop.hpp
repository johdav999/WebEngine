#pragma once
#include "engine/audio.hpp"
#include <filesystem>

namespace engine {
struct DesktopAudioDiagnostics {
    bool ready=false,active=false;
    std::uint64_t mixedFrames=0;
    std::size_t voices=0;
    std::size_t streams=0,bufferedStreamFrames=0;
    std::uint64_t decodedStreamFrames=0,streamUnderruns=0;
    float peak=0; // Peak mixed sample since device creation, before clipping.
    std::string error;
};
// Optional Windows SDL output adapter for the existing application-owned AudioSystem.
// Device opens only on activate(). Controls/diagnostics belong to the owner thread;
// the device callback owns no scenes, widgets, user callbacks or borrowed samples.
class DesktopAudioBackend final : public AudioBackend {
public:
    DesktopAudioBackend();
    ~DesktopAudioBackend() override;
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
    void muteOutput(bool) noexcept override;
    AudioCapabilities capabilities() const noexcept override { return {true,true,true}; }
    bool startStream(std::uint32_t,const AudioStreamAsset&,const AudioPlayOptions&,float) override;
    bool seekStream(std::uint32_t,double) noexcept override;
    double streamPosition(std::uint32_t) const noexcept override;
    bool reverb(const AudioReverbSettings&) override;
    void environment(std::uint32_t,float lowPassHz,float send) noexcept override;
    void clearEffects() noexcept override;
    DesktopAudioDiagnostics diagnostics() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Bounded RIFF/WAVE decode to the existing immutable PCM resource. No device opens.
// PCM/IEEE-float WAV only. Long encoded files use the separate streaming adapter.
// Encodes an absolute local-drive path as a UTF-8 file URI (spaces/Unicode supported).
// Streams use bounded worker decoding through system Media Foundation codecs.
std::string desktopAudioStreamUri(const std::filesystem::path&);
std::shared_ptr<const SoundAsset> decodeDesktopWav(std::span<const std::byte>);
}
