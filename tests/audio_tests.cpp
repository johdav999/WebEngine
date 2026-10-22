#include "engine/audio.hpp"
#include "engine/scene.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
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
    std::map<std::uint32_t,const AudioStreamAsset*> streams;
    std::map<std::uint32_t,std::pair<float,float>> environments;
    std::map<std::uint32_t,double> cursors;
    AudioReverbSettings room;
    bool failStream=false,failReverb=false,outputMuted=false;
    std::size_t clears=0;
    AudioListenerPose pose;
    bool running=true,fail=false,throwStart=false,failRate=false;
    std::size_t stops=0;
    bool start(std::uint32_t id,const SoundAsset& sound,const AudioPlayOptions& o,float g) override {
        if(throwStart) throw std::runtime_error("device failure");
        if(fail) return false;
        records.emplace(id,Record{&sound,o,g,o.position});return true;
    }
    void stop(std::uint32_t id) noexcept override { records.erase(id);streams.erase(id);environments.erase(id);cursors.erase(id);++stops; }
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
    AudioCapabilities capabilities() const noexcept override { return {true,true,true}; }
    bool startStream(std::uint32_t id,const AudioStreamAsset& stream,const AudioPlayOptions& o,float g) override {
        if(failStream)return false;streams.emplace(id,&stream);cursors[id]=o.offsetSeconds;records.emplace(id,Record{nullptr,o,g,o.position});return true;
    }
    bool reverb(const AudioReverbSettings& value) override { if(failReverb)return false;room=value;return true; }
    void environment(std::uint32_t id,float cutoff,float send) noexcept override { environments[id]={cutoff,send}; }
    void muteOutput(bool value) noexcept override { outputMuted=value; }
    void clearEffects() noexcept override { ++clears; }
    bool seekStream(std::uint32_t id,double value) noexcept override { if(!streams.contains(id))return false;cursors[id]=value;return true; }
    double streamPosition(std::uint32_t id) const noexcept override { auto found=cursors.find(id);return found==cursors.end()?-1:found->second; }
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
void environmentAndStreams() {
    auto device=std::make_unique<Device>();auto& d=*device;AudioSystem a(std::move(device),4);
    invalid([]{AudioStreamAsset::create("");});invalid([]{AudioStreamAsset::create("a\nb");});invalid([]{AudioStreamAsset::create(std::string(4097,'a'));});
    auto stream=AudioStreamAsset::create("music/long.ogg");auto weak=std::weak_ptr<const AudioStreamAsset>(stream);
    AudioPlayOptions options;options.loop=true;options.offsetSeconds=120;options.reverbSend=.4f;
    auto track=a.playStream(stream,options);check(bool(track),"Stream start failed");stream.reset();check(!weak.expired(),"Stream reference was not retained");
    check(d.records.at(track.value).sound==nullptr&&d.streams.size()==1,"Streaming copied PCM");near(float(a.streamPosition(track)),120);
    check(a.seekStream(track,3600),"Long track seek rejected");near(float(a.streamPosition(track)),3600);
    invalid([&]{a.seekStream(track,-1);});near(float(a.streamPosition(track)),3600);
    auto pcm=a.play(sound());check(!a.seekStream(pcm,1)&&a.streamPosition(pcm)==-1,"PCM accepted stream seek");
    AudioSystem foreign(std::make_unique<Device>());check(!foreign.seekStream(track,0),"Foreign stream seek accepted");
    check(a.setVoicePlaybackRate(track,2),"Stream rate rejected");a.setBusVolume(a.masterBus(),.5f);near(d.records.at(track.value).gain,.5f);
    a.setBusMuted(a.masterBus(),true);check(d.outputMuted,"Master mute did not silence effect tails");a.setBusMuted(a.masterBus(),false);check(!d.outputMuted,"Output did not unmute");
    check(a.setReverb({.5f,2,.04f}),"Room rejected");d.failReverb=true;check(!a.setReverb({.8f,3,.05f}),"Backend room rejection ignored");near(a.reverb().wet,.5f);d.failReverb=false;
    invalid([&]{a.setReverb({.5f,9,.1f});});near(a.reverb().decaySeconds,2);
    options.reverbSend=2;invalid([&]{a.playStream(AudioStreamAsset::create("music.mp3"),options);});options.reverbSend=.4f;
    d.failStream=true;check(!a.playStream(AudioStreamAsset::create("missing.mp3"))&&a.voiceCount()==2,"Failed stream published state");d.failStream=false;
    a.stop(track);check(weak.expired()&&a.streamPosition(track)==-1,"Stopped stream retained resource/cursor");a.stopAll();check(d.clears>0,"Stop-all retained effect tails");

    Scene scene;auto& listenerActor=scene.spawn<Actor>();auto& listener=listenerActor.addComponent<AudioListenerComponent>();
    auto& emitterActor=scene.spawn<Actor>();emitterActor.transform.position={0,0,-4};auto& emitter=emitterActor.addComponent<AudioEmitterComponent>();emitter.sound=sound();
    RigidBodySettings solid;solid.motion=BodyMotion::Static;
    for(auto* actor:{&listenerActor,&emitterActor}){actor->addComponent<RigidBodyComponent>("Self body",solid);actor->addComponent<ColliderComponent>("Self shape",CollisionShape::box({.2f,.2f,.2f}));}
    auto& wall=scene.spawn<Actor>();wall.transform.position={0,0,-2};solid.collisionLayer=3;wall.addComponent<RigidBodyComponent>("Wall",solid);wall.addComponent<ColliderComponent>("Wall shape",CollisionShape::box({2,2,.2f}));
    solid.isSensor=true;solid.collisionLayer=0;auto& sensor=scene.spawn<Actor>();sensor.transform.position={0,0,-1};sensor.addComponent<RigidBodyComponent>("Sensor",solid);sensor.addComponent<ColliderComponent>("Sensor shape",CollisionShape::box({1,1,.1f}));
    scene.synchronize();a.bindListener(listener.id());AudioOcclusionSettings settings;settings.enabled=true;a.setOcclusion(settings);
    AudioPlayOptions o;o.loop=true;o.occlusion=true;o.reverbSend=.7f;auto voice=a.playEmitter(emitter,o);
    a.update(.1,&scene);near(a.voiceOcclusion(voice),1-std::exp(-1.f));check(a.lastOcclusionQueries()==1,"Occlusion query count wrong");check(d.environments.at(voice.value).first<20000,"Occlusion did not low-pass");near(d.environments.at(voice.value).second,.7f);
    check(d.records.at(voice.value).gain<1,"Wall did not reduce gain");d.running=false;const auto frozen=a.voiceOcclusion(voice);a.update(10,&scene);near(a.voiceOcclusion(voice),frozen);check(a.lastOcclusionQueries()==0,"Suspended audio spent physics queries");d.running=true;
    settings.transitionSeconds=0;settings.layers=1;a.setOcclusion(settings);a.update(0,&scene);near(a.voiceOcclusion(voice),0); // Sensor and self bodies must not occlude.
    settings.layers=8;a.setOcclusion(settings);a.update(0,&scene);near(a.voiceOcclusion(voice),1);near(d.records.at(voice.value).gain,.35f*.5f);
    settings.maxQueriesPerUpdate=0;invalid([&]{a.setOcclusion(settings);});check(a.occlusion().maxQueriesPerUpdate==8,"Invalid occlusion replaced settings");settings.maxQueriesPerUpdate=1;a.setOcclusion(settings);
    auto v2=a.playEmitter(emitter,o),v3=a.playEmitter(emitter,o);for(int i=0;i<3;++i){a.update(0,&scene);check(a.lastOcclusionQueries()==1,"Per-update query budget exceeded");}
    near(a.voiceOcclusion(v2),1);near(a.voiceOcclusion(v3),1); // A one-query budget must visit all voices fairly.
    wall.enabled=false;scene.synchronize();for(int i=0;i<3;++i)a.update(0,&scene);near(a.voiceOcclusion(voice),0);
    wall.enabled=true;scene.synchronize();for(int i=0;i<3;++i)a.update(0,&scene);near(a.voiceOcclusion(voice),1);
    a.update(0,nullptr);check(a.voiceCount()==0,"Scene unload retained occluded emitter voices");
    o.spatial=false;auto dry=a.play(sound(),o);a.update(0,&scene);check(a.lastOcclusionQueries()==0&&a.voiceOcclusion(dry)==0,"Non-spatial voice spent wall rays");
    settings.enabled=false;a.setOcclusion(settings);a.update(0,&scene);near(d.environments.at(dry.value).first,20000);
}
}
int main() {
    try { assetsAndVoices();busesAndFades();spatialAndLifetimes();environmentAndStreams();std::cout<<"Audio contracts passed, including environment and streams\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
