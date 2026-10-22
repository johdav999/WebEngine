#include "engine/audio.hpp"
#include "engine/scene.hpp"
#include "engine/physics.hpp"
#include <atomic>
#include <limits>

namespace engine {
namespace {
std::atomic<std::uint64_t> nextSystem{1};
std::atomic<std::uint32_t> nextSound{1};
void volume(float value) {
    if(!std::isfinite(value)||value<0||value>1) throw std::invalid_argument("Audio volume must be in [0,1]");
}
void rateValid(float value) {
    if(!std::isfinite(value)||value<0.125f||value>8) throw std::invalid_argument("Audio playback rate must be in [0.125,8]");
}
void seconds(double value) {
    if(!std::isfinite(value)||value<0) throw std::invalid_argument("Audio time must be finite and nonnegative");
}
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
void positionValid(Vec3 v) { if(!finite(v)) throw std::invalid_argument("Invalid audio position"); }
AudioListenerPose validated(AudioListenerPose pose) {
    positionValid(pose.position);positionValid(pose.forward);positionValid(pose.up);
    pose.forward=normalized(pose.forward);
    const auto right=normalized(cross(pose.forward,pose.up));
    if(dot(pose.forward,pose.forward)<0.9f||dot(right,right)<0.9f) throw std::invalid_argument("Degenerate audio orientation");
    pose.up=normalized(cross(right,pose.forward));return pose;
}
template<class Fade> void begin(Fade& fade,float current,float target,double duration) {
    fade={current,target,0,duration,duration>0};
}
template<class Fade> void advance(Fade& fade,float& current,double dt) {
    if(!fade.running) return;
    fade.elapsed=std::min(fade.duration,fade.elapsed+dt);
    current=fade.from+(fade.to-fade.from)*float(fade.elapsed/fade.duration);
    if(fade.elapsed>=fade.duration) { current=fade.to;fade.running=false; }
}
}
SoundAsset::SoundAsset(std::uint32_t rate,std::uint32_t channels,std::span<const float> samples)
    :rate_(rate),channels_(channels),identity_(nextSound.fetch_add(1)),samples_(samples.begin(),samples.end()) {}
std::shared_ptr<const SoundAsset> SoundAsset::create(std::uint32_t rate,std::uint32_t channels,std::span<const float> samples) {
    if(rate<8000||rate>192000||channels<1||channels>8||samples.empty()||samples.size()%channels!=0
        ||samples.size()>32*1024*1024||double(samples.size()/channels)/rate>900)
        throw std::invalid_argument("Invalid or oversized audio samples");
    for(float sample:samples) if(!std::isfinite(sample)||sample<-1||sample>1) throw std::invalid_argument("Invalid PCM sample");
    return std::shared_ptr<const SoundAsset>(new SoundAsset(rate,channels,samples));
}
std::shared_ptr<const AudioStreamAsset> AudioStreamAsset::create(std::string uri) {
    if(uri.empty()||uri.size()>4096)throw std::invalid_argument("Invalid audio stream URI");
    for(unsigned char c:uri)if(c<=32||c==127)throw std::invalid_argument("Invalid audio stream URI");
    return std::shared_ptr<const AudioStreamAsset>(new AudioStreamAsset(std::move(uri)));
}
void AudioReverbSettings::validate() const {
    volume(wet);
    if(!std::isfinite(decaySeconds)||decaySeconds<.1f||decaySeconds>8||!std::isfinite(preDelaySeconds)||preDelaySeconds<0||preDelaySeconds>.2f)
        throw std::invalid_argument("Invalid room reverb settings");
}
void AudioOcclusionSettings::validate() const {
    volume(blockedGain);
    if(maxQueriesPerUpdate<1||maxQueriesPerUpdate>64||!std::isfinite(lowPassHz)||lowPassHz<20||lowPassHz>20000||
       !std::isfinite(transitionSeconds)||transitionSeconds<0||transitionSeconds>10)
        throw std::invalid_argument("Invalid audio occlusion settings");
}
AudioSystem::AudioSystem(std::unique_ptr<AudioBackend> backend,std::size_t maximum)
    :backend_(std::move(backend)),identity_(nextSystem.fetch_add(1)),maxVoices_(maximum) {
    if(!backend_||maximum==0||maximum>65536) throw std::invalid_argument("Invalid audio backend or voice budget");
    buses_.emplace(1,Bus{"Master",0,1,false,{}});backend_->listener(listener_);
}
AudioSystem::~AudioSystem() { stopAll(); }
AudioSystem::Bus* AudioSystem::bus(AudioBusId id) {
    if(id.system!=identity_) return nullptr;
    auto it=buses_.find(id.value);return it==buses_.end()?nullptr:&it->second;
}
AudioSystem::Voice* AudioSystem::voice(AudioVoiceId id) {
    if(id.system!=identity_) return nullptr;
    auto it=voices_.find(id.value);return it==voices_.end()?nullptr:&it->second;
}
AudioBusId AudioSystem::createBus(std::string name,AudioBusId parent) {
    if(parent==AudioBusId{}) parent=masterBus();
    if(!bus(parent)||name.empty()||nextBus_==std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("Invalid audio bus");
    const auto id=nextBus_++;buses_.emplace(id,Bus{std::move(name),parent.value,1,false,{}});return {identity_,id};
}
float AudioSystem::effectiveGain(const Voice& v) const {
    if(v.muted) return 0;
    float gain=v.options.volume;
    for(auto id=v.options.bus.value;id!=0;) {
        const auto& b=buses_.at(id);if(b.muted) return 0;
        gain*=b.volume;id=b.parent;
    }
    return gain*(1-v.occlusion*(1-occlusion_.blockedGain));
}
void AudioSystem::refreshGains() noexcept {
    const auto& master=buses_.at(1);backend_->muteOutput(master.muted||master.volume==0);
    for(const auto& [id,v]:voices_) backend_->gain(id,effectiveGain(v));
}
bool AudioSystem::setBusVolume(AudioBusId id,float value) {
    volume(value);auto* b=bus(id);if(!b) return false;b->volume=value;b->fade.running=false;refreshGains();return true;
}
bool AudioSystem::setBusMuted(AudioBusId id,bool value) {
    auto* b=bus(id);if(!b) return false;b->muted=value;refreshGains();return true;
}
bool AudioSystem::fadeBus(AudioBusId id,float target,double duration) {
    volume(target);seconds(duration);auto* b=bus(id);if(!b) return false;
    begin(b->fade,b->volume,target,duration);if(duration==0) b->volume=target;refreshGains();return true;
}
float AudioSystem::busVolume(AudioBusId id) const {
    auto* b=const_cast<AudioSystem*>(this)->bus(id);return b?b->volume:0;
}
bool AudioSystem::busMuted(AudioBusId id) const { auto* b=const_cast<AudioSystem*>(this)->bus(id);return b&&b->muted; }
AudioVoiceId AudioSystem::play(std::shared_ptr<const SoundAsset> sound,AudioPlayOptions options) {
    if(!sound) throw std::invalid_argument("Audio requires a sound asset");
    if(options.offsetSeconds>=sound->duration())throw std::invalid_argument("Invalid audio offset");
    return startVoice(std::move(sound),{},options);
}
AudioVoiceId AudioSystem::playStream(std::shared_ptr<const AudioStreamAsset> stream,AudioPlayOptions options) {
    if(!stream)throw std::invalid_argument("Audio requires a stream asset");
    return startVoice({},std::move(stream),options);
}
AudioVoiceId AudioSystem::startVoice(std::shared_ptr<const SoundAsset> sound,std::shared_ptr<const AudioStreamAsset> stream,AudioPlayOptions options) {
    volume(options.volume);rateValid(options.playbackRate);seconds(options.offsetSeconds);positionValid(options.position);
    volume(options.reverbSend);
    if(options.bus==AudioBusId{}) options.bus=masterBus();
    if(!bus(options.bus)||!std::isfinite(options.referenceDistance)
        ||!std::isfinite(options.maxDistance)||!std::isfinite(options.rolloff)||options.referenceDistance<=0
        ||options.maxDistance<options.referenceDistance||options.rolloff<0)
        throw std::invalid_argument("Invalid audio playback options");
    if(voices_.size()>=maxVoices_||nextVoice_==std::numeric_limits<std::uint32_t>::max()) return {};
    const auto id=nextVoice_++;
    Voice candidate;candidate.sound=std::move(sound);candidate.stream=std::move(stream);candidate.options=options;
    auto [it,inserted]=voices_.emplace(id,std::move(candidate));(void)inserted;
    try {
        const auto& v=it->second;
        const bool started=v.stream?backend_->startStream(id,*v.stream,options,effectiveGain(v)):backend_->start(id,*v.sound,options,effectiveGain(v));
        if(!started) { voices_.erase(it);return {}; }
        backend_->environment(id,20000,options.reverbSend);
    } catch(...) { backend_->stop(id);voices_.erase(it);throw; }
    return {identity_,id};
}
AudioVoiceId AudioSystem::playEmitter(const AudioEmitterComponent& emitter,AudioPlayOptions options) {
    if(!emitter.id()||!emitter.isActiveInHierarchy()) return {};
    options.spatial=true;options.position=transformPoint(emitter.worldMatrix(),{});
    auto id=play(emitter.sound,options);if(auto* v=voice(id)) v->emitter=emitter.id();return id;
}
bool AudioSystem::stop(AudioVoiceId id,double duration) {
    seconds(duration);auto* v=voice(id);if(!v) return false;
    if(duration>0) { fadeVoice(id,0,duration);v->stopAfterFade=true; }
    else { backend_->stop(id.value);voices_.erase(id.value); }
    return true;
}
void AudioSystem::stopAll() noexcept { for(const auto& [id,v]:voices_) { (void)v;backend_->stop(id); }voices_.clear();backend_->clearEffects(); }
bool AudioSystem::setVoiceVolume(AudioVoiceId id,float value) {
    volume(value);auto* v=voice(id);if(!v) return false;
    v->options.volume=value;v->fade.running=false;v->stopAfterFade=false;refreshGains();return true;
}
bool AudioSystem::setVoiceMuted(AudioVoiceId id,bool value) {
    auto* v=voice(id);if(!v) return false;v->muted=value;refreshGains();return true;
}
bool AudioSystem::fadeVoice(AudioVoiceId id,float target,double duration) {
    volume(target);seconds(duration);auto* v=voice(id);if(!v) return false;
    begin(v->fade,v->options.volume,target,duration);v->stopAfterFade=false;
    if(duration==0) v->options.volume=target;
    refreshGains();return true;
}
bool AudioSystem::setVoicePosition(AudioVoiceId id,Vec3 value) {
    positionValid(value);auto* v=voice(id);if(!v||!v->options.spatial||v->emitter) return false;
    v->options.position=value;backend_->position(id.value,value);return true;
}
bool AudioSystem::setVoicePlaybackRate(AudioVoiceId id,float value) {
    rateValid(value);auto* v=voice(id);if(!v||!backend_->playbackRate(id.value,value)) return false;
    v->options.playbackRate=value;return true;
}
float AudioSystem::voicePlaybackRate(AudioVoiceId id) const {
    auto* v=const_cast<AudioSystem*>(this)->voice(id);return v?v->options.playbackRate:0;
}
bool AudioSystem::seekStream(AudioVoiceId id,double value) {
    seconds(value);const auto* v=voice(id);return v&&v->stream&&backend_->seekStream(id.value,value);
}
double AudioSystem::streamPosition(AudioVoiceId id) const {
    const auto* v=const_cast<AudioSystem*>(this)->voice(id);return v&&v->stream?backend_->streamPosition(id.value):-1;
}
bool AudioSystem::setReverb(const AudioReverbSettings& value) {
    value.validate();if(!backend_->reverb(value))return false;reverb_=value;return true;
}
void AudioSystem::setOcclusion(const AudioOcclusionSettings& value) { value.validate();occlusion_=value; }
float AudioSystem::voiceOcclusion(AudioVoiceId id) const { const auto* v=const_cast<AudioSystem*>(this)->voice(id);return v?v->occlusion:0; }
bool AudioSystem::isPlaying(AudioVoiceId id) const {
    return const_cast<AudioSystem*>(this)->voice(id)&&backend_->playing(id.value);
}
float AudioSystem::voiceVolume(AudioVoiceId id) const {
    auto* v=const_cast<AudioSystem*>(this)->voice(id);return v?v->options.volume:0;
}
void AudioSystem::setListenerPose(const AudioListenerPose& pose) {
    auto next=validated(pose);listener_=next;listenerBinding_={};backend_->listener(listener_);
}
void AudioSystem::bindListener(ComponentId id) {
    listenerBinding_=id;listener_={};backend_->listener(listener_);
}
void AudioSystem::update(double delta,const Scene* scene) {
    seconds(delta);
    if(listenerBinding_) {
        const auto* c=scene?dynamic_cast<const AudioListenerComponent*>(scene->findComponent(listenerBinding_)):nullptr;
        if(!c||!c->isActiveInHierarchy()) { listener_={};backend_->listener(listener_); }
        else {
            const auto world=c->worldMatrix();
            try { listener_=validated({transformPoint(world,{}),transformDirection(world,{0,0,-1}),transformDirection(world,{0,1,0})});
                backend_->listener(listener_);
            } catch(const std::invalid_argument&) { /* Retain the last valid presentation pose. */ }
        }
    }
    const double dt=backend_->active()?delta:0;
    for(auto& [id,b]:buses_) { (void)id;advance(b.fade,b.volume,dt); }
    for(auto it=voices_.begin();it!=voices_.end();) {
        const auto id=it->first;auto& v=it->second;bool remove=!backend_->playing(id);
        if(v.emitter) {
            const auto* c=scene?dynamic_cast<const AudioEmitterComponent*>(scene->findComponent(v.emitter)):nullptr;
            if(!c||!c->isActiveInHierarchy()) remove=true;
            else {
                const auto p=transformPoint(c->worldMatrix(),{});
                if(finite(p)) { v.options.position=p;backend_->position(id,p); }
            }
        }
        advance(v.fade,v.options.volume,dt);
        if(v.stopAfterFade&&!v.fade.running) remove=true;
        if(remove) { backend_->stop(id);it=voices_.erase(it); }
        else ++it;
    }
    updateEnvironment(scene,dt);refreshGains();
}
void AudioSystem::updateEnvironment(const Scene* scene,double dt) {
    lastOcclusionQueries_=0;
    const auto* physics=scene?scene->physicsIfCreated():nullptr;
    const auto* listener=scene&&listenerBinding_?scene->findComponent(listenerBinding_):nullptr;
    const bool usable=occlusion_.enabled&&physics&&(!listenerBinding_||(listener&&listener->isActiveInHierarchy()));
    if(!usable)for(auto& [id,v]:voices_){(void)id;v.blocked=false;}
    else if(backend_->active()&&!voices_.empty()) {
        PhysicsQueryFilter filter;filter.layers=occlusion_.layers;filter.includeSensors=false;filter.excludedActors.reserve(2);
        auto it=voices_.upper_bound(occlusionCursor_);
        for(std::size_t visited=0;visited<voices_.size()&&lastOcclusionQueries_<occlusion_.maxQueriesPerUpdate;++visited) {
            if(it==voices_.end())it=voices_.begin();auto& v=it->second;occlusionCursor_=it->first;++it;
            if(!v.options.spatial||!v.options.occlusion){v.blocked=false;continue;}
            const auto direction=v.options.position-listener_.position;const float distance=std::sqrt(dot(direction,direction));
            v.blocked=false;if(distance<=.02f||!std::isfinite(distance))continue;
            filter.excludedActors.clear();
            if(listener&&listener->owner())filter.excludedActors.push_back(listener->owner()->id());
            if(const auto* emitter=scene->findComponent(v.emitter);emitter&&emitter->owner())filter.excludedActors.push_back(emitter->owner()->id());
            v.blocked=physics->raycast(listener_.position,direction,distance-.01f,filter).has_value();++lastOcclusionQueries_;
        }
    }
    const float blend=occlusion_.transitionSeconds==0?1:float(-std::expm1(-dt/occlusion_.transitionSeconds));
    for(auto& [id,v]:voices_) {
        const float target=usable&&v.options.spatial&&v.options.occlusion&&v.blocked?1.f:0.f;
        v.occlusion+=(target-v.occlusion)*blend;
        // Exponential interpolation in frequency gives a smoother perceptual transition.
        const float cutoff=20000*std::pow(occlusion_.lowPassHz/20000,v.occlusion);
        backend_->environment(id,cutoff,v.options.reverbSend);
    }
}
} // namespace engine
