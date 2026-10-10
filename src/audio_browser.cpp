#include "engine/audio_browser.hpp"
#include <emscripten.h>

namespace engine {
EM_JS(unsigned, audio_device_create, (), { return window.EngineAudioDevices.create(); });
EM_JS(void, audio_device_destroy, (unsigned device), { window.EngineAudioDevices.dispose(device); });
EM_JS(int, audio_device_start, (unsigned device,unsigned voice,unsigned sound,unsigned rate,unsigned channels,
    const float* samples,unsigned count,int loop,double offset,int spatial,float x,float y,float z,float ref,float max,float rolloff,float gain), {
    const pcm=new Float32Array(HEAPU8.buffer,samples,count);
    return window.EngineAudioDevices.get(device).start(voice,sound,rate,channels,pcm,
        {loop:!!loop,offset,spatial:!!spatial,position:[x,y,z],referenceDistance:ref,maxDistance:max,rolloff},gain);
});
EM_JS(void, audio_device_stop, (unsigned device,unsigned voice), { window.EngineAudioDevices.get(device).stop(voice); });
EM_JS(int, audio_device_playing, (unsigned device,unsigned voice), { return window.EngineAudioDevices.get(device).voices.has(voice); });
EM_JS(void, audio_device_gain, (unsigned device,unsigned voice,float gain), { window.EngineAudioDevices.get(device).gain(voice,gain); });
EM_JS(void, audio_device_position, (unsigned device,unsigned voice,float x,float y,float z), {
    window.EngineAudioDevices.get(device).position(voice,[x,y,z]);
});
EM_JS(void, audio_device_listener, (unsigned device,float x,float y,float z,float fx,float fy,float fz,float ux,float uy,float uz), {
    window.EngineAudioDevices.get(device).listener({position:[x,y,z],forward:[fx,fy,fz],up:[ux,uy,uz]});
});
EM_JS(int, audio_device_active, (unsigned device), { return window.EngineAudioDevices.get(device).active; });
EM_JS(void, audio_device_activate, (unsigned device), { window.EngineAudioDevices.get(device).activate(); });
EM_JS(void, audio_device_suspend, (unsigned device,int value), { window.EngineAudioDevices.get(device).suspend(!!value); });
BrowserAudioBackend::BrowserAudioBackend():device_(audio_device_create()) {}
BrowserAudioBackend::~BrowserAudioBackend() { audio_device_destroy(device_); }
bool BrowserAudioBackend::start(std::uint32_t id,const SoundAsset& s,const AudioPlayOptions& o,float g) {
    return audio_device_start(device_,id,s.identity(),s.sampleRate(),s.channels(),s.samples().data(),unsigned(s.samples().size()),
        o.loop,o.offsetSeconds,o.spatial,o.position.x,o.position.y,o.position.z,o.referenceDistance,o.maxDistance,o.rolloff,g)!=0;
}
void BrowserAudioBackend::stop(std::uint32_t id) noexcept { audio_device_stop(device_,id); }
bool BrowserAudioBackend::playing(std::uint32_t id) const noexcept { return audio_device_playing(device_,id)!=0; }
void BrowserAudioBackend::gain(std::uint32_t id,float value) noexcept { audio_device_gain(device_,id,value); }
void BrowserAudioBackend::position(std::uint32_t id,Vec3 p) noexcept { audio_device_position(device_,id,p.x,p.y,p.z); }
void BrowserAudioBackend::listener(const AudioListenerPose& p) noexcept {
    audio_device_listener(device_,p.position.x,p.position.y,p.position.z,p.forward.x,p.forward.y,p.forward.z,p.up.x,p.up.y,p.up.z);
}
bool BrowserAudioBackend::active() const noexcept { return audio_device_active(device_)!=0; }
void BrowserAudioBackend::activate() noexcept { audio_device_activate(device_); }
void BrowserAudioBackend::suspend(bool value) noexcept { audio_device_suspend(device_,value); }
} // namespace engine
