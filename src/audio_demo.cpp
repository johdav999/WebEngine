#include "audio_demo.hpp"
#include "engine/audio_browser.hpp"
#include <emscripten.h>

namespace {
using namespace engine;
struct Demo {
    Scene spatialScene{"Audio example"};
    AudioSystem audio{std::make_unique<BrowserAudioBackend>()};
    AudioBusId effects=audio.createBus("Effects"),music=audio.createBus("Music");
    std::shared_ptr<const SoundAsset> sound;
    AudioVoiceId loop;
    ComponentId emitter,listener;
    float x=2;
    Demo() {
        std::vector<float> samples(48000*2);
        for(std::size_t i=0;i<samples.size();++i) samples[i]=0.12f*std::sin(2*pi*220*float(i)/48000);
        sound=SoundAsset::create(48000,1,samples);
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
};
std::unique_ptr<Demo> demo;
bool stopped=false;
Demo& get() {
    if(stopped) throw std::logic_error("Audio example has shut down");
    if(!demo) demo=std::make_unique<Demo>();
    return *demo;
}
EM_JS(void, report_audio, (int count,int loop,float master,float effects,float music,int mute,const char* error), {
    window.engineAudioSystemReport?.({voices:count,loop:!!loop,master,effects,music,mute:!!mute,error:UTF8ToString(error)});
});
void report(const char* error="") {
    if(!demo) return;
    auto& d=*demo;report_audio(int(d.audio.voiceCount()),d.audio.isPlaying(d.loop),d.audio.busVolume(d.audio.masterBus()),
        d.audio.busVolume(d.effects),d.audio.busVolume(d.music),d.audio.busMuted(d.audio.masterBus()),error);
}
}
void audioDemoCommand(engine::Scene& scene,int command,float value) {
    (void)scene;
    try {
        auto& d=get();
        switch(command) {
        case 0:d.audio.backend().activate();break;
        case 1:{ AudioPlayOptions o;o.bus=d.effects;d.audio.playEmitter(d.bind(),o);break; }
        case 2:{ d.audio.stop(d.loop);AudioPlayOptions o;o.bus=d.music;o.loop=true;d.loop=d.audio.play(d.sound,o);break; }
        case 3:d.audio.stop(d.loop,1);break;
        case 4:d.audio.stopAll();break;
        case 5:d.audio.setBusVolume(d.audio.masterBus(),value);break;
        case 6:d.audio.setBusVolume(d.effects,value);break;
        case 7:d.audio.setBusVolume(d.music,value);break;
        case 8:d.audio.setBusMuted(d.audio.masterBus(),value!=0);break;
        case 9:d.x=value;d.bind();break;
        case 10:d.audio.fadeBus(d.music,value,1);break;
        default:throw std::invalid_argument("Unknown audio example command");
        }
        report();
    } catch(const std::exception& error) { report(error.what()); }
}
bool audioDemoLoad(std::uint32_t rate,std::uint32_t channels,std::span<const float> samples) {
    try { auto sound=engine::SoundAsset::create(rate,channels,samples);get().sound=std::move(sound);report();return true; }
    catch(const std::exception& error) { report(error.what());return false; }
}
void updateAudioDemo(const engine::Scene& scene,double seconds) {
    (void)scene;
    if(demo) { demo->spatialScene.synchronize();demo->audio.update(seconds,&demo->spatialScene);report(); }
}
void suspendAudioDemo(bool value) { if(demo) demo->audio.backend().suspend(value); }
void shutdownAudioDemo() { stopped=true;demo.reset(); }
