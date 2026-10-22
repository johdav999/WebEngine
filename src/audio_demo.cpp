#include "audio_demo.hpp"
#include "engine/audio_browser.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
#include <emscripten.h>

namespace {
using namespace engine;
struct Demo {
    Scene spatialScene{"Audio example"};
    AudioSystem audio{std::make_unique<BrowserAudioBackend>()};
    AudioBusId effects=audio.createBus("Effects"),music=audio.createBus("Music");
    std::shared_ptr<const SoundAsset> sound,environmentSound;
    AudioVoiceId loop,environmentVoice,stream;
    ComponentId emitter,listener,wall;
    float x=2,rate=1;
    Demo() {
        std::vector<float> samples(48000*2);
        for(std::size_t i=0;i<samples.size();++i) samples[i]=0.12f*std::sin(2*pi*220*float(i)/48000);
        sound=SoundAsset::create(48000,1,samples);
        for(std::size_t i=0;i<samples.size();++i)samples[i]=.07f*(std::sin(2*pi*220*float(i)/48000)+std::sin(2*pi*3000*float(i)/48000));
        environmentSound=SoundAsset::create(48000,1,samples);
        AudioOcclusionSettings settings;settings.enabled=true;audio.setOcclusion(settings);
    }
    AudioEmitterComponent& bind() {
        auto& scene=spatialScene;
        auto* found=dynamic_cast<AudioEmitterComponent*>(scene.findComponent(emitter));
        if(!found) {
            auto& actor=scene.spawn<Actor>("Audio example emitter and listener");
            auto& e=actor.addComponent<AudioEmitterComponent>("Positional sound");emitter=e.id();
            auto& l=actor.addComponent<AudioListenerComponent>("Audio listener");listener=l.id();
            audio.bindListener(listener);found=&e;
        }
        found->sound=sound;found->transform.position={x,0,-2};return *found;
    }
    void obstruction(bool enabled) {
        auto* found=spatialScene.findComponent(wall);
        if(!found){auto& actor=spatialScene.spawn<Actor>("Audio obstruction wall");actor.transform.position={0,0,-1};RigidBodySettings settings;settings.motion=BodyMotion::Static;auto& body=actor.addComponent<RigidBodyComponent>("Wall body",settings);wall=body.id();actor.addComponent<ColliderComponent>("Wall collision",CollisionShape::box({6,3,.1f}));found=&body;}
        found->owner()->enabled=enabled;
    }
};
std::unique_ptr<Demo> demo;
bool stopped=false;
Demo& get() {
    if(stopped) throw std::logic_error("Audio example has shut down");
    if(!demo) demo=std::make_unique<Demo>();
    return *demo;
}
EM_JS(void, report_audio, (int count,int loop,float master,float effects,float music,int mute,const char* error,float occlusion,int queries,double streamPosition,int streaming), {
    window.engineAudioSystemReport?.({voices:count,loop:!!loop,master,effects,music,mute:!!mute,error:UTF8ToString(error),occlusion,occlusionQueries:queries,streamPosition,streaming:!!streaming});
});
void report(const char* error="") {
    if(!demo) return;
    auto& d=*demo;report_audio(int(d.audio.voiceCount()),d.audio.isPlaying(d.loop),d.audio.busVolume(d.audio.masterBus()),
        d.audio.busVolume(d.effects),d.audio.busVolume(d.music),d.audio.busMuted(d.audio.masterBus()),error,
        d.audio.voiceOcclusion(d.environmentVoice),int(d.audio.lastOcclusionQueries()),d.audio.streamPosition(d.stream),d.audio.isPlaying(d.stream));
}
}
void audioDemoCommand(engine::Scene& scene,int command,float value) {
    (void)scene;
    try {
        auto& d=get();
        switch(command) {
        case 0:d.audio.backend().activate();break;
        case 1:{ AudioPlayOptions o;o.bus=d.effects;o.playbackRate=d.rate;d.audio.playEmitter(d.bind(),o);break; }
        case 2:{ d.audio.stop(d.loop);AudioPlayOptions o;o.bus=d.music;o.loop=true;o.playbackRate=d.rate;d.loop=d.audio.play(d.sound,o);break; }
        case 3:d.audio.stop(d.loop,1);break;
        case 4:d.audio.stopAll();break;
        case 5:d.audio.setBusVolume(d.audio.masterBus(),value);break;
        case 6:d.audio.setBusVolume(d.effects,value);break;
        case 7:d.audio.setBusVolume(d.music,value);break;
        case 8:d.audio.setBusMuted(d.audio.masterBus(),value!=0);break;
        case 9:d.x=value;d.bind();break;
        case 10:d.audio.fadeBus(d.music,value,1);break;
        case 11:if(!std::isfinite(value)||value<.125f||value>8)throw std::invalid_argument("Invalid preview rate");d.rate=value;d.audio.setVoicePlaybackRate(d.loop,value);d.audio.setVoicePlaybackRate(d.stream,value);break;
        case 12:{d.audio.stop(d.environmentVoice);auto& e=d.bind();e.sound=d.environmentSound;AudioPlayOptions o;o.bus=d.effects;o.loop=true;o.occlusion=true;o.reverbSend=1;d.environmentVoice=d.audio.playEmitter(e,o);break;}
        case 13:d.obstruction(value!=0);break;
        case 14:{auto settings=d.audio.occlusion();settings.enabled=value!=0;d.audio.setOcclusion(settings);break;}
        case 15:{AudioReverbSettings room;if(value==1)room={.4f,.6f,.01f};else if(value==2)room={.65f,3,.04f};else if(value!=0)throw std::invalid_argument("Unknown audio room");if(!d.audio.setReverb(room))throw std::runtime_error("Room reverb unavailable");break;}
        case 16:if(!d.audio.seekStream(d.stream,value))throw std::invalid_argument("Track position unavailable");break;
        case 17:d.audio.stop(d.stream);break;
        case 18:d.audio.stop(d.environmentVoice);break;
        default:throw std::invalid_argument("Unknown audio example command");
        }
        report();
    } catch(const std::exception& error) { report(error.what()); }
}
bool audioDemoLoad(std::uint32_t rate,std::uint32_t channels,std::span<const float> samples) {
    try { auto sound=engine::SoundAsset::create(rate,channels,samples);get().sound=std::move(sound);report();return true; }
    catch(const std::exception& error) { report(error.what());return false; }
}
bool audioDemoStream(const char* uri) {
    try {if(!uri)return false;auto& d=get();AudioPlayOptions o;o.bus=d.music;o.loop=true;o.playbackRate=d.rate;
        auto next=d.audio.playStream(AudioStreamAsset::create(uri),o);if(!next)return false;
        d.audio.stop(d.stream);d.stream=next;report();return true;
    }catch(const std::exception& error){report(error.what());return false;}
}
void updateAudioDemo(const engine::Scene& scene,double seconds) {
    (void)scene;
    if(demo) { demo->spatialScene.synchronize();demo->audio.update(seconds,&demo->spatialScene);report(); }
}
void suspendAudioDemo(bool value) { if(demo) demo->audio.backend().suspend(value); }
void stopAudioPreview(){if(demo)demo->audio.stopAll();}
void shutdownAudioDemo() { stopped=true;demo.reset(); }
