#pragma once
#include "engine/component.hpp"
#include <map>
#include <span>

namespace engine {
class Scene;
// Immutable interleaved PCM; instances share samples, never playback state.
class SoundAsset final {
public:
    static std::shared_ptr<const SoundAsset> create(std::uint32_t sampleRate,
        std::uint32_t channels,std::span<const float> samples);
    std::uint32_t sampleRate() const { return rate_; }
    std::uint32_t channels() const { return channels_; }
    std::span<const float> samples() const { return samples_; }
    double duration() const { return double(samples_.size()/channels_)/rate_; }
    std::uint32_t identity() const { return identity_; }
private:
    SoundAsset(std::uint32_t rate,std::uint32_t channels,std::span<const float> samples);
    std::uint32_t rate_,channels_,identity_;
    std::vector<float> samples_;
};
struct AudioBusId {
    std::uint64_t system=0;
    std::uint32_t value=0;
    explicit operator bool() const { return system!=0 && value!=0; }
    bool operator==(const AudioBusId&) const = default;
};
struct AudioVoiceId {
    std::uint64_t system=0;
    std::uint32_t value=0;
    explicit operator bool() const { return system!=0 && value!=0; }
    bool operator==(const AudioVoiceId&) const = default;
};
struct AudioListenerPose { Vec3 position{},forward{0,0,-1},up{0,1,0}; };
struct AudioPlayOptions {
    AudioBusId bus{}; // Empty selects master.
    float volume=1;
    bool loop=false,spatial=false;
    double offsetSeconds=0;
    Vec3 position{};
    float referenceDistance=1,maxDistance=10000,rolloff=1;
};
class AudioEmitterComponent : public Component {
public:
    using Component::Component;
    std::shared_ptr<const SoundAsset> sound;
    std::string_view typeName() const noexcept override { return "AudioEmitterComponent"; }
};
class AudioListenerComponent : public Component {
public:
    using Component::Component;
    std::string_view typeName() const noexcept override { return "AudioListenerComponent"; }
};
// Backend owns device resources, shares buffers, reports real completion and activation.
// start must publish an entire voice or return false; remaining methods must not throw.
class AudioBackend {
public:
    virtual ~AudioBackend()=default;
    virtual bool start(std::uint32_t voice,const SoundAsset&,const AudioPlayOptions&,float gain)=0;
    virtual void stop(std::uint32_t voice) noexcept=0;
    virtual bool playing(std::uint32_t voice) const noexcept=0;
    virtual void gain(std::uint32_t voice,float value) noexcept=0;
    virtual void position(std::uint32_t voice,Vec3 value) noexcept=0;
    virtual void listener(const AudioListenerPose&) noexcept=0;
    virtual bool active() const noexcept=0;
    virtual void activate() noexcept=0; // Initiate activation in a platform gesture.
    virtual void suspend(bool value) noexcept=0;
};
// Application-owned, single-threaded. No retained scene pointers or callbacks.
class AudioSystem final {
public:
    explicit AudioSystem(std::unique_ptr<AudioBackend>,std::size_t maxVoices=128);
    ~AudioSystem();
    AudioSystem(const AudioSystem&)=delete;
    AudioSystem& operator=(const AudioSystem&)=delete;
    AudioBackend& backend() { return *backend_; }
    AudioBusId masterBus() const { return {identity_,1}; }
    AudioBusId createBus(std::string name,AudioBusId parent={});
    bool setBusVolume(AudioBusId,float);
    bool setBusMuted(AudioBusId,bool);
    bool fadeBus(AudioBusId,float target,double seconds);
    float busVolume(AudioBusId) const;
    bool busMuted(AudioBusId) const;
    AudioVoiceId play(std::shared_ptr<const SoundAsset>,AudioPlayOptions={});
    AudioVoiceId playEmitter(const AudioEmitterComponent&,AudioPlayOptions={});
    bool stop(AudioVoiceId,double fadeSeconds=0);
    void stopAll() noexcept;
    bool setVoiceVolume(AudioVoiceId,float);
    bool setVoiceMuted(AudioVoiceId,bool);
    bool fadeVoice(AudioVoiceId,float target,double seconds);
    bool setVoicePosition(AudioVoiceId,Vec3);
    bool isPlaying(AudioVoiceId) const;
    float voiceVolume(AudioVoiceId) const;
    std::size_t voiceCount() const { return voices_.size(); }
    void setListenerPose(const AudioListenerPose&); // Validates before replacing; clears binding.
    void bindListener(ComponentId); // Empty clears binding and resets to origin.
    const AudioListenerPose& listenerPose() const { return listener_; }
    // Call after scene updates with the current scene (or null before unload).
    // Fades advance only while backend audio is active; spatial poses use simulation transforms.
    void update(double deltaSeconds,const Scene* scene=nullptr);
private:
    struct Fade { float from=1,to=1;double elapsed=0,duration=0;bool running=false; };
    struct Bus { std::string name;std::uint32_t parent=0;float volume=1;bool muted=false;Fade fade; };
    struct Voice {
        std::shared_ptr<const SoundAsset> sound;
        AudioPlayOptions options;
        ComponentId emitter{};
        bool muted=false,stopAfterFade=false;
        Fade fade;
    };
    std::unique_ptr<AudioBackend> backend_;
    std::uint64_t identity_;
    std::uint32_t nextBus_=2,nextVoice_=1;
    std::size_t maxVoices_;
    std::map<std::uint32_t,Bus> buses_;
    std::map<std::uint32_t,Voice> voices_;
    ComponentId listenerBinding_{};
    AudioListenerPose listener_;
    Bus* bus(AudioBusId);
    Voice* voice(AudioVoiceId);
    float effectiveGain(const Voice&) const;
    void refreshGains() noexcept;
};
} // namespace engine
