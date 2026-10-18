#include "driving_audio_demo.hpp"
#include "engine/audio_browser.hpp"
#include "engine/vehicle_component.hpp"
#include "engine/physics.hpp"
#include <emscripten.h>

namespace {
using namespace engine;
// Quiet deterministic generated loops, deliberately illustrative rather than licensed recordings.
std::shared_ptr<const SoundAsset> tone(float hz,bool noise=false,bool decay=false) {
    std::vector<float> pcm(decay?12000:48000);std::uint32_t seed=42;
    for(std::size_t i=0;i<pcm.size();++i) {
        seed=1664525*seed+1013904223;
        float wave=noise?(float(seed>>8)/8388608.f-1):std::sin(2*pi*hz*float(i)/48000);
        pcm[i]=.12f*wave*(decay?std::exp(-float(i)/1800):1);
    }
    return SoundAsset::create(48000,1,pcm);
}
struct DrivingAudio {
    AudioSystem audio{std::make_unique<BrowserAudioBackend>(),16};
    AudioBusId engine=audio.createBus("Engine"),tyres=audio.createBus("Tyres"),impacts=audio.createBus("Impacts");
    Scene emitters{"Driving audio proxies"};
    ComponentId target{},emitter{};ActorId actor{};
    std::array<AudioVoiceId,4> loops{};
    std::array<std::shared_ptr<const SoundAsset>,4> sounds{tone(80),tone(160),tone(600,true),tone(60,true)};
    std::shared_ptr<const SoundAsset> hard=tone(100,false,true),soft=tone(60,true,true);
    float baseRPM=2000,slipGain=.3f,volume=.4f,rate=1,load=0,slip=0,road=0,cooldown=0;
    bool enabled=false;unsigned hits=0;std::string surface="default",impact="none";
    DrivingAudio(){auto& e=emitters.spawn<Actor>().addComponent<AudioEmitterComponent>();emitter=e.id();}
    void stop(){audio.stopAll();loops={};target={};actor={};cooldown=0;rate=1;load=slip=road=0;}
};
std::unique_ptr<DrivingAudio> demo;
bool stopped=false,suspended=false;
DrivingAudio& get(){if(stopped)throw std::logic_error("Driving audio has shut down");if(!demo){demo=std::make_unique<DrivingAudio>();demo->audio.backend().suspend(suspended);}return *demo;}
VehicleComponent* vehicle(Scene& scene){VehicleComponent* v=nullptr;scene.visit([&](const Node& n,const Mat4&){if(!v)if(auto* c=dynamic_cast<const VehicleComponent*>(&n);c&&c->isActiveInHierarchy())v=const_cast<VehicleComponent*>(c);});return v;}
// Scene owns this forwarding callback. It captures no scene/node/device pointers;
// existing host callback remains owned and runs first. Repeated preparation is idempotent.
struct ImpactHook {
    std::function<void(const CollisionEvent&)> previous;
    void operator()(const CollisionEvent& event)const{
        if(previous)previous(event);
        if(!demo||!demo->enabled||!demo->target||suspended||!demo->audio.backend().active())return;
        auto& d=*demo;
        if(event.phase!=CollisionPhase::Begin||!event.impactValid||event.closingSpeed<1||d.cooldown>0
            ||(event.first!=d.actor&&event.second!=d.actor))return;
        const auto& material=event.first==d.actor?event.secondSurface:event.firstSurface;
        const auto id=material.surface?material.surface->id:"default";
        const bool gravel=id=="gravel";d.impact=gravel?"gravel":"hard";
        auto* e=dynamic_cast<AudioEmitterComponent*>(d.emitters.findComponent(d.emitter));
        e->sound=gravel?d.soft:d.hard;e->transform.position=event.point;
        AudioPlayOptions o;o.bus=d.impacts;o.volume=std::min(1.f,event.closingSpeed/12);o.playbackRate=gravel?.7f:1.2f;
        o.spatial=true;o.position=event.point;
        if(d.audio.play(e->sound,o)){++d.hits;d.cooldown=.15f;}
    }
};
EM_JS(void,report_driving_audio,(unsigned device,int enabled,int count,float rate,float load,float slip,float road,unsigned hits,const char* surface,const char* impact),{
    window.drivingAudioState={device,enabled:!!enabled,voices:count,rate,load,slip,road,hits,surface:UTF8ToString(surface),impact:UTF8ToString(impact)};
});
void publish(){if(demo){auto& d=*demo;report_driving_audio(static_cast<BrowserAudioBackend&>(d.audio.backend()).deviceId(),d.enabled,int(d.audio.voiceCount()),d.rate,d.load,d.slip,d.road,d.hits,d.surface.c_str(),d.impact.c_str());}}
}
void prepareDrivingAudio(engine::Scene& scene){
    if(!vehicle(scene))return;
    auto& world=scene.physics();
    if(!world.onCollision.target<ImpactHook>())world.onCollision=ImpactHook{std::move(world.onCollision)};
}
void drivingAudioCommand(engine::Scene& scene,int command,float value){
    if(!std::isfinite(value))throw std::invalid_argument("Invalid driving audio value");
    if((command==21&&(value<500||value>8000))||(command==22&&(value<0||value>2))||(command==23&&(value<0||value>1))
        ||(command==20&&value!=0&&value!=1)||command<20||command>23)throw std::invalid_argument("Invalid driving audio settings");
    auto& d=get();
    if(command==20){if(value==0){d.stop();d.enabled=false;}else{d.enabled=true;prepareDrivingAudio(scene);d.audio.backend().activate();}}
    if(command==21)d.baseRPM=value;
    if(command==22)d.slipGain=value;
    if(command==23){d.volume=value;d.audio.setBusVolume(d.audio.masterBus(),value);}
    publish();
}
void updateDrivingAudio(engine::Scene& scene,double dt,bool running){
    if(!demo)return;auto& d=*demo;auto* v=vehicle(scene);
    if(!d.enabled||!running||!v){d.stop();publish();return;}
    if(v->id()!=d.target){d.stop();d.target=v->id();d.actor=v->owner()->id();}
    const auto& state=v->state();d.cooldown=std::max(0.f,d.cooldown-float(dt));
    d.rate=std::clamp(state.rpm/d.baseRPM,.125f,8.f);d.load=std::abs(v->command().throttle);d.slip=0;d.road=0;d.surface="default";
    for(const auto& wheel:state.wheels)if(wheel.contact){
        d.slip=std::max(d.slip,std::max(std::abs(wheel.lateralSlip),std::abs(wheel.longitudinalSlip)));
        if(wheel.surface.surface){const auto& id=wheel.surface.surface->id;if(id=="gravel"){d.surface=id;d.road=std::clamp(std::abs(state.speed)/15.f,0.f,1.f);}else if(d.surface=="default")d.surface=id;}
    }
    d.slip=std::clamp(d.slip*d.slipGain,0.f,1.f);
    auto* e=dynamic_cast<AudioEmitterComponent*>(d.emitters.findComponent(d.emitter));
    e->transform.position=transformPoint(v->owner()->worldMatrix(),{});
    const std::array<float,4> gains{.65f*(1-d.load),.65f*d.load,d.slip,d.road};
    for(std::size_t i=0;i<4;++i){
        if(!d.audio.isPlaying(d.loops[i])){e->sound=d.sounds[i];AudioPlayOptions o;o.loop=true;o.bus=i<2?d.engine:d.tyres;o.volume=gains[i];o.playbackRate=i<2?d.rate:1;d.loops[i]=d.audio.playEmitter(*e,o);}
        d.audio.setVoiceVolume(d.loops[i],gains[i]);d.audio.setVoicePlaybackRate(d.loops[i],i<2?d.rate:(i==2?1.f:std::clamp(std::abs(state.speed)/10,.5f,2.f)));
    }
    // Camera-independent listener near the car: illustrative cockpit mix.
    d.audio.setListenerPose({e->transform.position+Vec3{0,1,-2},{0,0,1},{0,1,0}});
    d.audio.setBusVolume(d.audio.masterBus(),d.volume);d.emitters.synchronize();d.audio.update(dt,&d.emitters);publish();
}
void stopDrivingAudio(){if(demo){demo->stop();demo->enabled=false;publish();}}
void suspendDrivingAudio(bool value){suspended=value;if(demo)demo->audio.backend().suspend(value);}
void shutdownDrivingAudio(){stopped=true;demo.reset();}
