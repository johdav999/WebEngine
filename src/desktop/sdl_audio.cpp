#include "engine/audio_desktop.hpp"
#include "audio_stream.hpp"
#include "audio_dsp.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace engine {
namespace {
bool validRate(float v){return std::isfinite(v)&&v>=.125f&&v<=8;}
std::uint32_t u32(std::span<const std::byte> bytes,std::size_t at) {
    return std::to_integer<std::uint32_t>(bytes[at])|(std::to_integer<std::uint32_t>(bytes[at+1])<<8)|
        (std::to_integer<std::uint32_t>(bytes[at+2])<<16)|(std::to_integer<std::uint32_t>(bytes[at+3])<<24);
}
std::uint16_t u16(std::span<const std::byte> bytes,std::size_t at){return std::uint16_t(std::to_integer<unsigned>(bytes[at])|(std::to_integer<unsigned>(bytes[at+1])<<8));}
}
std::shared_ptr<const SoundAsset> decodeDesktopWav(std::span<const std::byte> bytes) {
    // Validate decoded allocation bounds before SDL's WAV loader can allocate.
    if(bytes.size()<44||bytes.size()>32*1024*1024||std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4)||std::uint64_t(u32(bytes,4))+8!=bytes.size())
        throw std::invalid_argument("Invalid or oversized desktop WAV");
    std::uint32_t rate=0,channels=0,bits=0,block=0,dataBytes=0;bool format=false,data=false;
    for(std::size_t at=12;at<bytes.size();) {
        if(bytes.size()-at<8)throw std::invalid_argument("Truncated WAV chunk");
        const auto length=u32(bytes,at+4);const auto start=at+8;
        if(length>bytes.size()-start)throw std::invalid_argument("Truncated WAV data");
        if(!std::memcmp(bytes.data()+at,"fmt ",4)) {
            if(format||length<16)throw std::invalid_argument("Invalid WAV format chunk");
            const auto kind=u16(bytes,start);channels=u16(bytes,start+2);rate=u32(bytes,start+4);block=u16(bytes,start+12);bits=u16(bytes,start+14);
            if((kind!=1&&kind!=3)||channels<1||channels>8||rate<8000||rate>192000||
                (kind==1?(bits!=8&&bits!=16&&bits!=24&&bits!=32):(bits!=32))||block!=channels*(bits/8))throw std::invalid_argument("Unsupported WAV format");
            format=true;
        }else if(!std::memcmp(bytes.data()+at,"data",4)) {if(data)throw std::invalid_argument("Duplicate WAV data");dataBytes=length;data=true;}
        const std::size_t padded=std::size_t(length)+(length&1u);if(padded>bytes.size()-start)throw std::invalid_argument("Truncated WAV padding");at=start+padded;
    }
    if(!format||!data||!dataBytes||dataBytes%block||std::uint64_t(dataBytes)/block*channels>32*1024*1024||double(dataBytes/block)/rate>900)
        throw std::invalid_argument("Invalid WAV sample bounds");
    SDL_AudioSpec source{};Uint8* decoded=nullptr;Uint32 size=0;
    if(!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes.data(),bytes.size()),true,&source,&decoded,&size))throw std::runtime_error(SDL_GetError());
    const std::unique_ptr<Uint8,decltype(&SDL_free)> wav(decoded,SDL_free);
    const SDL_AudioSpec output{SDL_AUDIO_F32,int(channels),int(rate)};Uint8* converted=nullptr;int convertedSize=0;
    if(!SDL_ConvertAudioSamples(&source,decoded,int(size),&output,&converted,&convertedSize))throw std::runtime_error(SDL_GetError());
    const std::unique_ptr<Uint8,decltype(&SDL_free)> pcm(converted,SDL_free);
    if(convertedSize<=0||convertedSize%(sizeof(float)*channels))throw std::runtime_error("Invalid converted WAV");
    return SoundAsset::create(rate,channels,{reinterpret_cast<const float*>(converted),std::size_t(convertedSize)/sizeof(float)});
}
struct DesktopAudioBackend::Impl {
    struct Pcm {std::uint32_t rate,channels;std::vector<float> samples;};
    struct Voice {std::shared_ptr<const Pcm> pcm;AudioPlayOptions options;double cursor=0;float gain=1;bool done=false;std::shared_ptr<desktop_audio::Stream> encoded;desktop_audio::LowPass filter;float send=0;};
    mutable std::mutex mutex;
    std::map<std::uint32_t,Voice> voices;
    std::map<std::uint32_t,std::weak_ptr<const Pcm>> cache;
    AudioListenerPose listener;
    std::unique_ptr<desktop_audio::Room> room;
    SDL_AudioStream* stream=nullptr;bool initialized=false,muted=false;
    std::atomic<bool> running=false,suspended=false;
    std::uint64_t frames=0;float peak=0;std::string error;
    ~Impl(){running=false;if(stream)SDL_DestroyAudioStream(stream);if(initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);}
    void mix(std::span<float> output) {
        std::lock_guard lock(mutex);std::fill(output.begin(),output.end(),0.f);
        if(!running||suspended)return;
        std::array<float,2048> wet{},decoded{};
        for(auto& [id,voice]:voices) {
            (void)id;if(voice.done)continue;
            const auto* pcm=voice.pcm.get();const auto count=pcm?pcm->samples.size()/pcm->channels:0;
            if(voice.encoded&&!voice.encoded->render(std::span(decoded).first(output.size()),voice.options.playbackRate))voice.done=true;
            float left=voice.gain,right=voice.gain;
            if(voice.options.spatial) {
                const auto& o=voice.options;const auto rightAxis=cross(listener.forward,listener.up);
                const double x=double(o.position.x)-listener.position.x,y=double(o.position.y)-listener.position.y,z=double(o.position.z)-listener.position.z;
                const double distance=std::sqrt(x*x+y*y+z*z),d=std::clamp(distance,double(o.referenceDistance),double(o.maxDistance));
                const float attenuation=float(o.referenceDistance/(o.referenceDistance+double(o.rolloff)*(d-o.referenceDistance)));
                const float pan=distance>.0001?float(std::clamp((x*rightAxis.x+y*rightAxis.y+z*rightAxis.z)/distance,-1.,1.)):0;
                left*=attenuation*std::sqrt((1-pan)*.5f);right*=attenuation*std::sqrt((1+pan)*.5f);
            }
            const double step=pcm?double(pcm->rate)/48000*voice.options.playbackRate:0;
            for(std::size_t i=0;i<output.size();i+=2) {
                float l=0,r=0;
                if(pcm){
                    if(voice.cursor>=count) {if(voice.options.loop)voice.cursor=std::fmod(voice.cursor,double(count));else{voice.done=true;break;}}
                    const auto a=std::size_t(voice.cursor),b=a+1<count?a+1:voice.options.loop?0:a;const float t=float(voice.cursor-a);
                    auto sample=[&](std::size_t channel){return pcm->samples[a*pcm->channels+channel]*(1-t)+pcm->samples[b*pcm->channels+channel]*t;};
                    l=sample(0);r=pcm->channels==1?l:sample(1);
                    if(voice.options.spatial||pcm->channels>2){float sum=0;for(std::uint32_t c=0;c<pcm->channels;++c)sum+=sample(c);l=r=sum/pcm->channels;}
                    voice.cursor+=step;
                }else {l=decoded[i];r=decoded[i+1];if(voice.options.spatial)l=r=(l+r)*.5f;}
                voice.filter.process(l,r);l*=left;r*=right;
                output[i]+=l;output[i+1]+=r;wet[i]+=l*voice.send;wet[i+1]+=r*voice.send;
            }
            if(pcm&&!voice.options.loop&&voice.cursor>=count)voice.done=true;
        }
        if(room)for(std::size_t i=0;i<output.size();i+=2)room->process(wet[i],wet[i+1],output[i],output[i+1]);
        if(muted)std::fill(output.begin(),output.end(),0.f);

        for(auto& value:output){peak=std::max(peak,std::abs(value));value=std::clamp(value,-1.f,1.f);}frames+=output.size()/2;
    }
    static void SDLCALL callback(void* userdata,SDL_AudioStream* stream,int additional,int) noexcept {
        auto& self=*static_cast<Impl*>(userdata);std::array<float,2048> buffer{};
        try {
            while(additional>0) {
                const int bytes=std::min(additional,int(sizeof(buffer)));const auto samples=std::size_t((bytes+7)/8)*2;
                self.mix(std::span(buffer).first(samples));
                if(!SDL_PutAudioStreamData(stream,buffer.data(),int(samples*sizeof(float)))){self.running=false;break;}
                additional-=int(samples*sizeof(float));
            }
        }catch(...){self.running=false;}
    }
};
DesktopAudioBackend::DesktopAudioBackend():impl_(std::make_unique<Impl>()){}
DesktopAudioBackend::~DesktopAudioBackend()=default;
bool DesktopAudioBackend::start(std::uint32_t id,const SoundAsset& sound,const AudioPlayOptions& options,float gain) {
    if(!id||!validRate(options.playbackRate)||!std::isfinite(gain)||gain<0||gain>1||!std::isfinite(options.offsetSeconds)||options.offsetSeconds<0)return false;
    std::lock_guard lock(impl_->mutex);auto& p=*impl_;if(p.voices.contains(id)||p.voices.size()>=256)return false;
    auto cached=p.cache[sound.identity()].lock();
    if(!cached) {auto pcm=std::make_shared<Impl::Pcm>();pcm->rate=sound.sampleRate();pcm->channels=sound.channels();pcm->samples.assign(sound.samples().begin(),sound.samples().end());cached=std::move(pcm);p.cache[sound.identity()]=cached;}
    const double count=double(cached->samples.size()/cached->channels);double cursor=options.offsetSeconds*cached->rate;
    if(options.loop)cursor=std::fmod(cursor,count);else if(cursor>=count)return false;
    p.voices.emplace(id,Impl::Voice{std::move(cached),options,cursor,gain});
    std::erase_if(p.cache,[](const auto& entry){return entry.second.expired();});return true;
}
void DesktopAudioBackend::stop(std::uint32_t id) noexcept {
    // Join a decoding worker only after releasing the device mutex.
    decltype(impl_->voices)::node_type removed;
    {std::lock_guard lock(impl_->mutex);removed=impl_->voices.extract(id);}
    try{if(removed&&removed.mapped().encoded){const auto error=removed.mapped().encoded->error();if(!error.empty()){std::lock_guard lock(impl_->mutex);impl_->error=error;}}}catch(...){}
}
bool DesktopAudioBackend::playing(std::uint32_t id) const noexcept {std::lock_guard lock(impl_->mutex);const auto found=impl_->voices.find(id);return found!=impl_->voices.end()&&!found->second.done&&(!found->second.encoded||found->second.encoded->playing());}
void DesktopAudioBackend::gain(std::uint32_t id,float value) noexcept {if(!std::isfinite(value))return;std::lock_guard lock(impl_->mutex);if(auto it=impl_->voices.find(id);it!=impl_->voices.end())it->second.gain=std::clamp(value,0.f,1.f);}
void DesktopAudioBackend::position(std::uint32_t id,Vec3 value) noexcept {std::lock_guard lock(impl_->mutex);if(auto it=impl_->voices.find(id);it!=impl_->voices.end())it->second.options.position=value;}
bool DesktopAudioBackend::playbackRate(std::uint32_t id,float value) noexcept {if(!validRate(value))return false;std::lock_guard lock(impl_->mutex);const auto it=impl_->voices.find(id);if(it==impl_->voices.end()||it->second.done)return false;it->second.options.playbackRate=value;return true;}
void DesktopAudioBackend::listener(const AudioListenerPose& value) noexcept {std::lock_guard lock(impl_->mutex);impl_->listener=value;}
bool DesktopAudioBackend::active() const noexcept{return impl_->running&&!impl_->suspended;}
void DesktopAudioBackend::activate() noexcept {
    auto& p=*impl_;try {
        if(!p.initialized){if(!SDL_InitSubSystem(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());p.initialized=true;}
        if(!p.stream){const SDL_AudioSpec format{SDL_AUDIO_F32,2,48000};p.stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&format,Impl::callback,&p);if(!p.stream)throw std::runtime_error(SDL_GetError());}
        if(!p.suspended){p.running=true;if(!SDL_ResumeAudioStreamDevice(p.stream)){p.running=false;throw std::runtime_error(SDL_GetError());}}
        std::lock_guard lock(p.mutex);p.error.clear();
    }catch(const std::exception& e){p.running=false;std::lock_guard lock(p.mutex);p.error=e.what();}
}
void DesktopAudioBackend::suspend(bool value) noexcept {
    auto& p=*impl_;p.suspended=value;if(!p.stream)return;
    if(value)SDL_PauseAudioStreamDevice(p.stream);else if(p.running)SDL_ResumeAudioStreamDevice(p.stream);
}
void DesktopAudioBackend::muteOutput(bool value) noexcept {std::lock_guard lock(impl_->mutex);impl_->muted=value;}
bool DesktopAudioBackend::startStream(std::uint32_t id,const AudioStreamAsset& asset,const AudioPlayOptions& options,float gain) {
    if(!id||!validRate(options.playbackRate)||!std::isfinite(gain)||gain<0||gain>1||!std::isfinite(options.offsetSeconds)||options.offsetSeconds<0||options.offsetSeconds>=86400)return false;
    try {
        const auto path=desktop_audio::streamPath(asset.uri());
        if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>16ull*1024*1024*1024)return false;
        {std::lock_guard lock(impl_->mutex);if(impl_->voices.contains(id)||impl_->voices.size()>=256||std::count_if(impl_->voices.begin(),impl_->voices.end(),[](const auto& e){return bool(e.second.encoded);})>=8)return false;}
        auto stream=std::make_shared<desktop_audio::Stream>(path,options.offsetSeconds,options.loop);
        Impl::Voice voice;voice.options=options;voice.gain=gain;voice.encoded=std::move(stream);
        std::lock_guard lock(impl_->mutex);impl_->voices.emplace(id,std::move(voice));return true;
    }catch(const std::exception& e){std::lock_guard lock(impl_->mutex);impl_->error=e.what();return false;}
}
namespace {
std::shared_ptr<desktop_audio::Stream> streamFor(auto& p,std::uint32_t id) {std::lock_guard lock(p.mutex);const auto it=p.voices.find(id);return it==p.voices.end()||it->second.done?nullptr:it->second.encoded;}
}
bool DesktopAudioBackend::seekStream(std::uint32_t id,double time) noexcept {const auto stream=streamFor(*impl_,id);return stream&&stream->seek(time);}
double DesktopAudioBackend::streamPosition(std::uint32_t id) const noexcept {const auto stream=streamFor(*impl_,id);return stream?stream->position():-1;}
bool DesktopAudioBackend::reverb(const AudioReverbSettings& settings) {
    auto next=std::make_unique<desktop_audio::Room>(settings);
    {std::lock_guard lock(impl_->mutex);impl_->room.swap(next);}return true;
}
void DesktopAudioBackend::environment(std::uint32_t id,float hz,float send) noexcept {
    if(!std::isfinite(hz)||!std::isfinite(send))return;
    std::lock_guard lock(impl_->mutex);if(auto it=impl_->voices.find(id);it!=impl_->voices.end()){it->second.filter.cutoff(hz);it->second.send=std::clamp(send,0.f,1.f);}
}
void DesktopAudioBackend::clearEffects() noexcept {std::lock_guard lock(impl_->mutex);if(impl_->room)impl_->room->clear();}
DesktopAudioDiagnostics DesktopAudioBackend::diagnostics() const {
    const auto& p=*impl_;std::lock_guard lock(p.mutex);DesktopAudioDiagnostics d;d.ready=p.stream!=nullptr;d.active=active();d.mixedFrames=p.frames;d.voices=p.voices.size();d.peak=p.peak;d.error=p.error;
    for(const auto& [id,v]:p.voices)if(v.encoded){++d.streams;d.bufferedStreamFrames+=v.encoded->buffered();d.decodedStreamFrames+=v.encoded->decoded();d.streamUnderruns+=v.encoded->underruns();if(const auto e=v.encoded->error();!e.empty())d.error=e;}
    return d;
}
}
