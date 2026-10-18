#include "engine/audio.hpp"
#include "engine/scene.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void near(float a,float b) { check(std::abs(a-b)<0.0001f,"Audio gain/pose mismatch"); }
template<class F> void invalid(F f) {
    bool caught=false;try { f(); } catch(const std::invalid_argument&) { caught=true; }
    check(caught,"Invalid audio input was accepted");
}
class Device final : public AudioBackend {
public:
    struct Record { const SoundAsset* sound;AudioPlayOptions options;float gain;Vec3 position; };
    std::map<std::uint32_t,Record> records;
    AudioListenerPose pose;
    bool running=true,fail=false,throwStart=false,failRate=false;
    std::size_t stops=0;
    bool start(std::uint32_t id,const SoundAsset& sound,const AudioPlayOptions& o,float g) override {
        if(throwStart) throw std::runtime_error("device failure");
        if(fail) return false;
        records.emplace(id,Record{&sound,o,g,o.position});return true;
    }
    void stop(std::uint32_t id) noexcept override { records.erase(id);++stops; }
    bool playing(std::uint32_t id) const noexcept override { return records.contains(id); }
    void gain(std::uint32_t id,float g) noexcept override { if(records.contains(id)) records.at(id).gain=g; }
    bool playbackRate(std::uint32_t id,float value) noexcept override {
        if(failRate||!records.contains(id)) return false;records.at(id).options.playbackRate=value;return true;
    }
    void position(std::uint32_t id,Vec3 p) noexcept override { if(records.contains(id)) records.at(id).position=p; }
    void listener(const AudioListenerPose& p) noexcept override { pose=p; }
    bool active() const noexcept override { return running; }
    void activate() noexcept override { running=true; }
    void suspend(bool value) noexcept override { running=!value; }
};
std::shared_ptr<const SoundAsset> sound() { return SoundAsset::create(8000,1,std::vector<float>(8000,0.2f)); }
void assetsAndVoices() {
    invalid([]{SoundAsset::create(7999,1,std::vector<float>(1));});
    invalid([]{SoundAsset::create(8000,0,std::vector<float>(1));});
    invalid([]{SoundAsset::create(8000,2,std::vector<float>(3));});
    invalid([]{SoundAsset::create(8000,1,std::vector<float>{std::numeric_limits<float>::quiet_NaN()});});
    invalid([]{SoundAsset::create(8000,1,std::vector<float>{1.1f});});
    auto s=sound();near(float(s->duration()),1);
    auto device=std::make_unique<Device>();auto& d=*device;AudioSystem audio(std::move(device),2);
    auto v=audio.play(s),w=audio.play(s);check(v&&w&&v!=w,"Simultaneous voices have no independent identity");
    check(d.records.at(v.value).sound==d.records.at(w.value).sound,"Sound resource was duplicated");
    check(!audio.play(s),"Voice budget was exceeded");
    AudioSystem other(std::make_unique<Device>());check(!other.stop(v),"Foreign voice resolved");
    audio.stop(v);check(!audio.stop(v)&&audio.isPlaying(w),"Stopping one voice affected another");
    d.records.erase(w.value);check(!audio.setVoicePlaybackRate(w,2),"Completed backend voice accepted rate");check(!audio.isPlaying(w),"Natural completion did not resolve");audio.update(0);
    check(audio.voiceCount()==0,"Completed voice retained state");
    d.fail=true;check(!audio.play(s)&&audio.voiceCount()==0,"Failed voice published partial state");d.fail=false;
    d.throwStart=true;try { audio.play(s); } catch(const std::runtime_error&) {}
    check(audio.voiceCount()==0,"Throwing backend retained state");d.throwStart=false;
    AudioPlayOptions o;o.loop=true;o.offsetSeconds=0.25;
    auto loop=audio.play(s,o);check(d.records.at(loop.value).options.loop,"Loop setting was lost");
    near(float(d.records.at(loop.value).options.offsetSeconds),0.25f);
    near(audio.voicePlaybackRate(loop),1);check(audio.setVoicePlaybackRate(loop,2),"Rate control failed");
    near(d.records.at(loop.value).options.playbackRate,2);
    d.failRate=true;check(!audio.setVoicePlaybackRate(loop,3),"Backend rejection lost");near(audio.voicePlaybackRate(loop),2);d.failRate=false;
    for(float bad: {0.f,-1.f,0.124f,8.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        invalid([&]{audio.setVoicePlaybackRate(loop,bad);});o.playbackRate=bad;invalid([&]{audio.play(s,o);});
    }
    near(audio.voicePlaybackRate(loop),2);o.playbackRate=0.125f;auto slow=audio.play(s,o);check(bool(slow),"Minimum rate rejected");audio.stop(slow);
    check(!other.setVoicePlaybackRate(loop,1),"Foreign rate changed");near(other.voicePlaybackRate(loop),0);
    d.running=false;check(audio.setVoicePlaybackRate(loop,8),"Suspended rate rejected");near(audio.voicePlaybackRate(loop),8);d.running=true;
    o.playbackRate=1;
    o.offsetSeconds=1;invalid([&]{audio.play(s,o);});
    o.offsetSeconds=0;o.referenceDistance=0;invalid([&]{audio.play(s,o);});
    auto weak=std::weak_ptr<const SoundAsset>(s);s.reset();check(!weak.expired(),"Active voice lost shared resource");
    audio.stopAll();check(weak.expired(),"Stopped voice retained shared resource");
}
void busesAndFades() {
    auto device=std::make_unique<Device>();auto& d=*device;AudioSystem a(std::move(device));
    auto group=a.createBus("Group"),child=a.createBus("Effects",group);AudioPlayOptions o;o.bus=child;o.volume=0.8f;
    auto v=a.play(sound(),o);a.setBusVolume(group,0.5f);a.setBusVolume(child,0.25f);near(d.records.at(v.value).gain,0.1f);
    a.setBusMuted(group,true);near(d.records.at(v.value).gain,0);a.setBusMuted(group,false);near(d.records.at(v.value).gain,0.1f);
    a.setVoiceMuted(v,true);near(d.records.at(v.value).gain,0);a.setVoiceMuted(v,false);
    invalid([&]{a.setBusVolume(child,std::numeric_limits<float>::quiet_NaN());});near(a.busVolume(child),0.25f);
    AudioSystem other(std::make_unique<Device>());invalid([&]{a.createBus("Foreign",other.masterBus());});
    check(!a.setBusVolume(other.masterBus(),0.5f),"Foreign bus resolved");
    a.fadeVoice(v,0,2);a.update(1);near(a.voiceVolume(v),0.4f);
    a.fadeVoice(v,1,2);a.update(1);near(a.voiceVolume(v),0.7f);
    d.running=false;a.update(100);near(a.voiceVolume(v),0.7f);d.running=true;
    a.update(1);near(a.voiceVolume(v),1);a.fadeBus(group,0,2);a.update(1);near(a.busVolume(group),0.25f);
    near(d.records.at(v.value).gain,0.0625f);
    a.setBusVolume(group,1);a.update(5);near(a.busVolume(group),1);
    a.stop(v,1);a.update(0.5);near(a.voiceVolume(v),0.5f);check(a.isPlaying(v),"Fade stop ended early");
    a.update(0.5);check(!a.isPlaying(v)&&a.voiceCount()==0,"Fade stop retained voice");
    invalid([&]{a.update(-1);});invalid([&]{a.fadeBus(child,0,-1);});
    v=a.play(sound());a.stop(v,1);a.setVoiceVolume(v,0.3f);a.update(2);check(a.isPlaying(v),"Interrupted stop fade still stopped");
    a.fadeVoice(v,0.6f,0);near(a.voiceVolume(v),0.6f);
}
void spatialAndLifetimes() {
    auto device=std::make_unique<Device>();auto& d=*device;AudioSystem a(std::move(device));
    Scene scene;auto& actor=scene.spawn<Actor>();actor.transform.position={10,2,3};
    auto& emitter=actor.addComponent<AudioEmitterComponent>();emitter.sound=sound();emitter.transform.position={1,0,0};
    auto& listener=scene.spawn<Actor>().addComponent<AudioListenerComponent>();listener.transform.position={0,1,0};
    a.bindListener(listener.id());auto v=a.playEmitter(emitter);a.update(0,&scene);
    near(d.records.at(v.value).position.x,11);near(d.pose.position.y,1);
    actor.transform.position.x=20;a.update(0,&scene);near(d.records.at(v.value).position.x,21);
    listener.transform.rotation.y=pi/2;a.update(0,&scene);near(d.pose.forward.x,-1);
    auto before=a.listenerPose();invalid([&]{a.setListenerPose({{}, {0,0,0},{0,1,0}});});near(a.listenerPose().position.y,before.position.y);
    listener.transform.scale={0,0,0};a.update(0,&scene);near(d.pose.position.y,1);
    actor.enabled=false;a.update(0,&scene);check(!a.isPlaying(v),"Disabled emitter kept playing");actor.enabled=true;
    v=a.playEmitter(emitter);const auto stale=emitter.id();emitter.destroy();a.update(0,&scene);
    check(!a.isPlaying(v)&&!scene.findComponent(stale),"Pending removal emitter kept playing");scene.flushPendingDestroy();
    auto& next=actor.addComponent<AudioEmitterComponent>();next.sound=sound();v=a.playEmitter(next);
    next.reparent(scene);a.update(0,&scene);check(a.isPlaying(v),"Same-scene reparent lost voice");
    Scene foreign;next.reparent(foreign);a.update(0,&scene);check(!a.isPlaying(v),"Scene transfer retained stale emitter");
    v=a.playEmitter(next);a.update(0,nullptr);check(!a.isPlaying(v),"Null scene retained emitter voice");near(d.pose.position.y,0);
    AudioPlayOptions o;o.spatial=true;v=a.play(sound(),o);a.setVoicePosition(v,{3,4,5});near(d.records.at(v.value).position.z,5);
    invalid([&]{a.setVoicePosition(v,{std::numeric_limits<float>::infinity(),0,0});});near(d.records.at(v.value).position.z,5);
    auto detached=AudioEmitterComponent{};check(!a.playEmitter(detached),"Detached emitter played");
}
}
int main() {
    try { assetsAndVoices();busesAndFades();spatialAndLifetimes();std::cout<<"Audio contracts passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
