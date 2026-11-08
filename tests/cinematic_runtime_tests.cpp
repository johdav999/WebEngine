#include "engine/cinematic_runtime.hpp"
#include "engine/camera_component.hpp"
#include "engine/effect_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/scene.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <iostream>

using namespace engine;
namespace {
void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
void near(double value,double expected) {
    check(std::abs(value-expected)<.0001,"Unexpected numeric cinematic sample");
}
template<class F> void rejects(F&& action) {
    try { action(); } catch(const std::exception&) { return; }
    throw std::runtime_error("Invalid live cinematic operation succeeded");
}

class CountingActor final : public Actor {
public:
    CountingActor():Actor("Gameplay ticker") { setTickEnabled(true); }
    unsigned updates=0;
protected:
    void tick(double) override { ++updates; }
};

CinematicTrack track(CinematicTrackType type,std::string name,std::string target={}) {
    CinematicTrack result;
    result.id=result.name=std::move(name);
    result.target=std::move(target);
    result.type=type;
    result.end=1;
    return result;
}

struct Fixture {
    Scene scene;
    CountingActor& ticker=scene.spawn<CountingActor>();
    Actor& actor=scene.spawn("Performer");
    CameraComponent& wide=scene.addChild<CameraComponent>("Wide");
    CameraComponent& close=scene.addChild<CameraComponent>("Close");
    MeshComponent* sprite=nullptr;
    SkeletalMeshComponent* mesh=nullptr;
    EffectComponent* effect=nullptr;

    Fixture() {
        actor.setSavedId("performer");
        actor.transform.position.x=9;
        wide.setSavedId("wide");
        wide.setPerspective(.9f,.1f,100);
        close.setSavedId("close");
        close.setOrthographic(5,.1f,100);

        auto texture=std::make_shared<Texture>();
        texture->width=4;texture->height=4;texture->pixels.assign(4*4*4,255);
        sprite=&actor.addComponent<MeshComponent>("Panel",Mesh::plane());
        sprite->setSavedId("panel");
        sprite->texture=std::move(texture);
        sprite->enabled=false;

        auto skeleton=std::make_shared<Skeleton>();
        skeleton->joints.push_back({"Root",-1,{}});
        skeleton->bindings={{},{0,Mat4::identity()}};
        AnimationClip clip;
        clip.name="Lift";clip.duration=1;clip.frameCount=2;clip.frames.resize(2);
        clip.frames.back().translation.y=2;
        clip.markers.push_back({.2,"silent",{}});
        skeleton->clips.push_back(std::move(clip));
        auto geometry=std::make_shared<Mesh>(*Mesh::cube());
        geometry->skeleton=std::move(skeleton);
        geometry->skin.resize(geometry->vertices.size());
        for(auto& vertex:geometry->skin) vertex.joints[0]=1;
        mesh=&actor.addComponent<SkeletalMeshComponent>("Dancer",geometry);
        mesh->setSavedId("dancer");
        const JointPose original{{0,3,0}};
        mesh->skeleton().setPose(std::span(&original,1));

        auto definition=std::make_shared<ParticleEmitterDefinition>();
        definition->spawnRate=100;
        definition->lifetimeMin=definition->lifetimeMax=2;
        auto asset=std::make_shared<EffectAsset>();
        asset->emitters.push_back({definition,{}});
        effect=&actor.addComponent<EffectComponent>("Dust",asset);
        effect->setSavedId("dust");
        effect->stop(true);
        effect->enabled=false;
        scene.synchronize();
    }

    CinematicSequenceData data() const {
        CinematicSequenceData data;
        data.duration=1;
        auto motion=track(CinematicTrackType::Transform,"Move","performer");
        motion.keys={{0,{}},{1,{{2,0,0}}}};
        motion.keys[0].interpolation=CinematicInterpolation::Linear;
        auto opening=track(CinematicTrackType::Camera,"Opening","wide");
        opening.end=.5;
        opening.keys={{0,{},.6f}};
        auto ending=track(CinematicTrackType::Camera,"Ending","close");
        ending.start=.5;ending.end=.75;
        ending.keys={{.5,{},8}};
        auto animation=track(CinematicTrackType::Animation,"Animation","dancer");
        animation.resource="Lift";
        auto panel=track(CinematicTrackType::Sprite,"Panel","panel");
        panel.atlasColumns=2;
        panel.keys={{0},{.5}};
        panel.keys.back().spriteFrame=1;
        auto subtitle=track(CinematicTrackType::Subtitle,"Subtitle");
        subtitle.resource="Ready to go";
        subtitle.end=.75;
        auto dust=track(CinematicTrackType::Effect,"Effect","dust");
        data.tracks={motion,opening,ending,animation,panel,subtitle,dust};
        return data;
    }
};

struct AudioObservation {
    bool active=false,destroyed=false;
    unsigned starts=0,stops=0;
    std::uint32_t playing=0;
    std::string uri;
    AudioPlayOptions options;
};
class Device final : public AudioBackend {
public:
    explicit Device(AudioObservation& observation):o(observation) {}
    ~Device() override { o.destroyed=true; }
    bool start(std::uint32_t,const SoundAsset&,const AudioPlayOptions&,float) override { return false; }
    bool startStream(std::uint32_t id,const AudioStreamAsset& stream,const AudioPlayOptions& options,float) override {
        o.playing=id;
        ++o.starts;
        o.options=options;
        o.uri=stream.uri();
        return true;
    }
    void stop(std::uint32_t id) noexcept override { if(o.playing==id) { o.playing=0;++o.stops; } }
    bool playing(std::uint32_t id) const noexcept override { return o.playing==id; }
    void gain(std::uint32_t,float) noexcept override {}
    void position(std::uint32_t,Vec3) noexcept override {}
    bool playbackRate(std::uint32_t,float) noexcept override { return true; }
    void listener(const AudioListenerPose&) noexcept override {}
    bool active() const noexcept override { return o.active; }
    void activate() noexcept override { o.active=true; }
    void suspend(bool value) noexcept override { o.active=!value; }
    AudioCapabilities capabilities() const noexcept override { return {.streaming=true}; }
private:
    AudioObservation& o;
};

void playback() {
    Fixture fixture;
    const auto sequence=CinematicSequence::create(fixture.data());
    CinematicRuntime runtime;
    check(runtime.status()==CinematicRuntimeStatus::Idle,"Runtime not initially idle");
    runtime.start(fixture.scene,sequence);
    check(runtime.camera(fixture.scene)==&fixture.wide,"Opening camera binding failed");
    check(runtime.subtitle()=="Ready to go"&&fixture.sprite->enabled,"Initial 2D channels not sampled");
    near(fixture.wide.verticalFov(),.6);
    near(fixture.actor.transform.position.x,0);
    near(fixture.mesh->skinPose()->bones[1].transform.m[13],0);
    check(!runtime.audio(),"Silent cutscene created an audio device");

    CinematicRuntime other;
    rejects([&]{other.start(fixture.scene,sequence);});
    rejects([&]{fixture.scene.detachChild(fixture.actor);});
    Scene wrong;
    rejects([&]{fixture.actor.reparent(wrong);});
    rejects([&]{runtime.advance(wrong,.25);});
    check(!runtime.camera(wrong),"Camera handle crossed scene owners");
    rejects([&]{runtime.advance(fixture.scene,.5);});

    fixture.scene.spawn("Deferred gameplay actor").destroy();
    const auto queuedChildren=fixture.scene.children().size();
    runtime.advance(fixture.scene,.25);
    const auto count=fixture.effect->particleCount();
    check(count>0&&fixture.ticker.updates==0,"FX presentation tick ran gameplay or failed to emit");
    check(fixture.scene.children().size()==queuedChildren,"FX tick flushed unrelated gameplay removals");
    fixture.scene.update(.25);
    check(fixture.effect->particleCount()==count&&fixture.ticker.updates==1,"Gameplay clock advanced owned FX");
    check(fixture.mesh->animation().events().empty()&&fixture.mesh->animation().state()==AnimationState::Stopped,
        "Cinematic sampling changed the gameplay animation clock or emitted a marker");
    near(fixture.mesh->skinPose()->bones[1].transform.m[13],.5);
    runtime.advance(fixture.scene,.25);
    check(runtime.camera(fixture.scene)==&fixture.close,"Orthographic cut failed");
    near(fixture.wide.verticalFov(),.9);
    near(fixture.close.orthographicHeight(),8);
    near(fixture.sprite->resolvedMaterial(0).uvOffset.x,.5);
    runtime.advance(fixture.scene,.25);
    check(!runtime.camera(fixture.scene)&&runtime.subtitle().empty(),"Gap did not restore the gameplay camera");
    near(fixture.close.orthographicHeight(),5);
    runtime.advance(fixture.scene,.25);
    check(runtime.status()==CinematicRuntimeStatus::Completed,"Natural completion not delivered");
    near(fixture.actor.transform.position.x,9);
    near(fixture.mesh->skinPose()->bones[1].transform.m[13],3);
    check(!fixture.effect->enabled&&!fixture.effect->playing()&&fixture.effect->particleCount()==0,
        "Completion retained FX playback");
    check(!fixture.sprite->enabled&&fixture.sprite->materialOverrides.empty(),"Completion retained sprite overrides");
    runtime.skip(fixture.scene);
    check(runtime.status()==CinematicRuntimeStatus::Completed,"Terminal outcome changed on repeat skip");

    auto invalid=fixture.data();
    invalid.tracks[1].target="missing";
    rejects([&]{runtime.start(fixture.scene,CinematicSequence::create(invalid));});
    check(runtime.status()==CinematicRuntimeStatus::Completed,"Failed preflight replaced previous result");
    near(fixture.actor.transform.position.x,9);

    runtime.start(fixture.scene,sequence);
    runtime.skip(fixture.scene);
    check(runtime.status()==CinematicRuntimeStatus::Skipped,"Skip did not publish the skipped outcome");
    near(runtime.time(),1);
    near(fixture.actor.transform.position.x,9);
    runtime.start(fixture.scene,sequence);
    fixture.close.destroy();
    runtime.advance(fixture.scene,.25);
    check(runtime.status()==CinematicRuntimeStatus::Failed&&!runtime.error().empty(),"Removed target was not detected");
    near(fixture.actor.transform.position.x,9);
    check(!fixture.effect->enabled&&!fixture.sprite->enabled,"Failure left surviving 2D/FX channels owned");
}

void lifecycleAndAudio() {
    Fixture fixture;
    const auto sequence=CinematicSequence::create(fixture.data());
    {
        CinematicRuntime runtime;
        runtime.start(fixture.scene,sequence);
        runtime.advance(fixture.scene,.25);
    }
    near(fixture.actor.transform.position.x,9);
    check(!fixture.effect->playing()&&fixture.effect->particleCount()==0,"Destructor retained live FX");
    auto& body=fixture.actor.addComponent<RigidBodyComponent>("Physics writer",RigidBodySettings{});
    body.enabled=false;
    CinematicRuntime runtime;
    rejects([&]{runtime.start(fixture.scene,sequence);});
    body.destroy();
    fixture.scene.flushPendingDestroy();

    auto audioData=fixture.data();
    auto sound=track(CinematicTrackType::Audio,"Sound");
    sound.resource="intro.wav";sound.offset=.5;sound.speed=2;
    audioData.tracks.push_back(sound);
    const auto audioSequence=CinematicSequence::create(std::move(audioData));
    rejects([&]{runtime.start(fixture.scene,audioSequence);});
    AudioObservation observation;
    auto resolve=[](std::string_view id) {
        check(id=="intro.wav","Resolver did not receive authored ID");
        return AudioStreamAsset::create("sounds/resolved.wav");
    };
    runtime.start(fixture.scene,audioSequence,std::make_unique<Device>(observation),resolve);
    runtime.advance(fixture.scene,.25);
    near(runtime.time(),0);
    runtime.audio()->backend().activate();
    runtime.advance(fixture.scene,.25);
    check(observation.starts==1&&observation.uri=="sounds/resolved.wav","Resolved sound cue not started");
    near(observation.options.offsetSeconds,1);
    runtime.audio()->backend().suspend(true);
    runtime.advance(fixture.scene,.25);
    near(runtime.time(),.25);
    runtime.cancel(fixture.scene);
    check(runtime.status()==CinematicRuntimeStatus::Cancelled&&observation.stops==1&&observation.destroyed,
        "Cancellation did not release owned audio");

    AudioObservation unloaded;
    {
        auto temporary=std::make_unique<Fixture>();
        runtime.start(temporary->scene,audioSequence,std::make_unique<Device>(unloaded),resolve);
        runtime.audio()->backend().activate();
        runtime.advance(temporary->scene,.25);
    }
    check(runtime.status()==CinematicRuntimeStatus::Cancelled&&!runtime.audio()&&unloaded.stops==1&&unloaded.destroyed,
        "Scene destruction did not synchronously cancel playback");
}
} // namespace

int main() {
    try {
        playback();
        lifecycleAndAudio();
        std::cout<<"Live cinematic playback and ownership checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
