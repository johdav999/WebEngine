#include "engine/editor_cinematic.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/effect_presets.hpp"
#include <iostream>

using namespace engine;
namespace {
int checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void near(double a,double b,double e=1e-4){check(std::abs(a-b)<e,"Cinematic numeric mismatch");}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,"Invalid cinematic input was accepted");}
class Device final:public AudioBackend {
public:
    std::map<std::uint32_t,AudioPlayOptions> voices;bool running=true;
    bool start(std::uint32_t,const SoundAsset&,const AudioPlayOptions&,float)override{return false;}
    bool startStream(std::uint32_t id,const AudioStreamAsset&,const AudioPlayOptions& o,float)override{voices[id]=o;return true;}
    void stop(std::uint32_t id)noexcept override{voices.erase(id);}
    bool playing(std::uint32_t id)const noexcept override{return voices.contains(id);}
    void gain(std::uint32_t,float)noexcept override{}
    void position(std::uint32_t,Vec3)noexcept override{}
    bool playbackRate(std::uint32_t,float)noexcept override{return true;}
    void listener(const AudioListenerPose&)noexcept override{}
    bool active()const noexcept override{return running;}
    void activate()noexcept override{running=true;}
    void suspend(bool value)noexcept override{running=!value;}
};
struct Fixture {
    Scene scene;
    CameraComponent *a,*b;
    Actor* actor;
    SkeletalMeshComponent* mesh;
    Fixture(){
        a=&scene.addChild<CameraComponent>("Wide");a->setSavedId("camera.a");a->transform.position={4,3,8};a->lookAt({0,1,0});
        b=&scene.addChild<CameraComponent>("Close");b->setSavedId("camera.b");b->transform.position={1,2,4};b->lookAt({0,1,0});
        actor=&scene.spawn("Performer");actor->setSavedId("actor");
        auto skeleton=std::make_shared<Skeleton>();skeleton->joints.push_back({"Root",-1,{}});skeleton->bindings={{},{0,Mat4::identity()}};
        AnimationClip clip;clip.name="Wave";clip.duration=2;clip.frameCount=2;clip.frames.resize(2);clip.frames[1].translation.y=1;skeleton->clips.push_back(clip);skeleton->validate();
        auto source=std::make_shared<Mesh>(*Mesh::cube());source->skeleton=skeleton;source->skin.resize(source->vertices.size());for(auto& v:source->skin)v.joints[0]=1;
        mesh=&actor->addComponent<SkeletalMeshComponent>("Character",source);mesh->setSavedId("character");mesh->animation().play(0);
        auto definition=std::make_shared<ParticleEmitterDefinition>();definition->seed=32;definition->spawnRate=24;definition->maxParticles=128;definition->velocityMin={-.2f,1,0};definition->velocityMax={.2f,2,0};
        auto effect=std::make_shared<EffectAsset>();effect->emitters.push_back({definition,{}});auto& fx=actor->addComponent<EffectComponent>("Fountain",effect);fx.setSavedId("effect");scene.synchronize();
    }
    std::vector<RenderView> views(){return {RenderView{a}};}
    CinematicSequenceData data(){
        CinematicSequenceData d;CinematicTrack motion;motion.id="motion";motion.target="actor";motion.keys={{0,{{-2,0,0},{},{1,1,1}}},{8,{{2,0,0},{},{1,1,1}}}};d.tracks.push_back(motion);
        CinematicTrack wide;wide.id="wide";wide.type=CinematicTrackType::Camera;wide.target="camera.a";wide.end=4;wide.keys={{0,a->transform},{4,a->transform}};d.tracks.push_back(wide);
        auto close=wide;close.id="close";close.target="camera.b";close.start=4;close.end=8;close.keys={{4,b->transform},{8,b->transform}};d.tracks.push_back(close);
        CinematicTrack animation;animation.id="animation";animation.target="character";animation.type=CinematicTrackType::Animation;animation.resource="Wave";animation.loop=true;d.tracks.push_back(animation);
        CinematicTrack sound;sound.id="sound";sound.type=CinematicTrackType::Audio;sound.resource="music.mp3";sound.start=1;sound.end=7;d.tracks.push_back(sound);
        CinematicTrack effect;effect.id="fx";effect.target="effect";effect.type=CinematicTrackType::Effect;effect.start=2;effect.end=6;d.tracks.push_back(effect);return d;
    }
};
std::vector<Vec3> particles(CinematicPreview& p){std::vector<Vec3> points;auto* e=dynamic_cast<EffectComponent*>(p.scene().findSavedNode("effect"));for(auto* emitter:e->emitters())for(const auto& particle:emitter->particles())points.push_back(particle.position);return points;}
void same(const std::vector<Vec3>& a,const std::vector<Vec3>& b){check(a.size()==b.size(),"Particle seek count differs");for(std::size_t i=0;i<a.size();++i){near(a[i].x,b[i].x);near(a[i].y,b[i].y);near(a[i].z,b[i].z);}}
void run(){
    Fixture f;auto data=f.data();auto asset=CinematicSequence::create(data);near(asset->sample(0,4).transform.position.x,0);
    auto angular=data;angular.tracks[0].keys[0].transform.rotation.y=3;angular.tracks[0].keys[1].transform.rotation.y=-3;near(CinematicSequence::create(angular)->sample(0,4).transform.rotation.y,pi);
    angular.tracks[0].keys[0].interpolation=CinematicInterpolation::Step;near(CinematicSequence::create(angular)->sample(0,4).transform.position.x,-2);
    check(asset->active(data.tracks[1],3.99)&&!asset->active(data.tracks[1],4)&&asset->active(data.tracks[2],4),"Camera cut endpoint ownership");
    for(int mode=0;mode<6;++mode){auto bad=data;if(mode==0)bad.duration=NAN;if(mode==1)bad.tracks[0].keys[1].time=0;if(mode==2)bad.tracks[0].keys[0].transform.scale.x=0;if(mode==3)bad.tracks[1].end=5;if(mode==4)bad.tracks[0].id=bad.tracks[1].id;if(mode==5)bad.tracks[0].keys[0].lens=INFINITY;rejects([&]{CinematicSequence::create(bad);});}
    auto many=data;for(int i=0;i<8;++i){auto t=data.tracks[4];t.id="audio"+std::to_string(i);many.tracks.push_back(t);}rejects([&]{CinematicSequence::create(many);});
    auto unsupported=data;unsupported.tracks[5].speed=2;rejects([&]{CinematicSequence::create(unsupported);});
    auto file=CinematicSequenceArchive::save(*asset);check(CinematicSequenceArchive::save(*CinematicSequenceArchive::load(file))==file,"Cinematic file round trip");rejects([&]{CinematicSequenceArchive::load(file+"trailing");});rejects([&]{CinematicSequenceArchive::load(file.substr(0,file.size()/2));});rejects([&]{CinematicSequenceArchive::load("WECINE 1 \"X\" 8 30 10000000");});
    CinematicSequenceEditor editor(asset);data.name="Changed";check(editor.replace(data)&&editor.dirty(),"Editor mutation");check(editor.undo()&&!editor.dirty()&&editor.redo(),"Independent cinematic history");editor.markSaved();check(!editor.dirty()&&!editor.replace(data),"No-op edit polluted history");auto previous=editor.asset();data.duration=-1;rejects([&]{editor.replace(data);});check(editor.asset()==previous,"Invalid edit replaced valid document");
    CinematicPlayer clock(asset);rejects([&]{clock.seek(9);});clock.play();clock.advance(.25);near(clock.time(),.25);clock.pause();clock.advance(.25);near(clock.time(),.25);clock.seek(7.9);clock.play();clock.advance(.2);near(clock.time(),8);check(clock.state()==CinematicPlaybackState::Paused,"Playback end did not pause");clock.looping=true;clock.play();clock.seek(7.9);clock.advance(.2);near(clock.time(),.1);rejects([&]{clock.advance(1);});near(clock.time(),.1);
    auto source=SceneArchive::save(f.scene,f.views());auto device=std::make_unique<Device>();auto* audio=device.get();CinematicPreview preview(f.scene,f.views(),asset,std::move(device));check(preview.scene().runtimeIdentity()!=f.scene.runtimeIdentity(),"Preview did not isolate scene");
    preview.seek(3);auto sample=particles(preview);check(!sample.empty(),"FX clip did not simulate");near(preview.player().time(),3);check(preview.replaySteps()<=600,"FX replay exceeded budget");auto* character=dynamic_cast<SkeletalMeshComponent*>(preview.scene().findSavedNode("character"));near(character->animation().time(),3);check(character->animation().state()==AnimationState::Paused,"Animation has a competing clock");
    preview.seek(1);check(preview.particleCount()==0,"FX visible before clip");preview.seek(3);same(sample,particles(preview));check(preview.views()[0].camera->savedId()=="camera.a","Wrong wide shot");preview.seek(5);check(preview.views()[0].camera->savedId()=="camera.b","Camera cut failed");
    preview.seek(2.5);preview.play();check(audio->voices.size()==1,"Sound clip not started");near(audio->voices.begin()->second.offsetSeconds,1.5);for(int i=0;i<30;++i)preview.advance(1./60);same(sample,particles(preview));
    audio->running=false;const auto paused=preview.player().time();preview.advance(.2);near(preview.player().time(),paused);audio->running=true;preview.pause();check(audio->voices.empty(),"Pause retained audio");preview.seek(3);check(audio->voices.empty(),"Scrub auditioned audio");preview.stop();near(preview.player().time(),0);check(preview.particleCount()==0,"Stop retained particles");
    check(SceneArchive::save(f.scene,f.views())==source,"Preview changed authoring bytes/animation state");
    auto bad=asset->data();bad.tracks[3].resource="Missing";rejects([&]{CinematicPreview candidate(f.scene,f.views(),CinematicSequence::create(bad));});check(SceneArchive::save(f.scene,f.views())==source,"Failed preview changed source");
    auto silent=asset->data();silent.tracks[4].enabled=false;silent.tracks[4].resource="missing.mp3";auto disabled=std::make_unique<Device>();disabled->running=false;CinematicPreview silentPreview(f.scene,f.views(),CinematicSequence::create(silent),std::move(disabled));check(!silentPreview.audio(),"Silent sequence opened an audio device");silentPreview.play();silentPreview.advance(.1);near(silentPreview.player().time(),.1);
    auto muted=asset->data();muted.tracks[3].enabled=false;muted.tracks[3].target="missing-character";CinematicPreview mutedPreview(f.scene,f.views(),CinematicSequence::create(muted));mutedPreview.seek(4);check(mutedPreview.views()[0].camera->savedId()=="camera.b","Disabled missing binding blocked preview");
    auto badLens=asset->data();badLens.tracks[2].keys.back().lens=1e-40f;rejects([&]{CinematicPreview candidate(f.scene,f.views(),CinematicSequence::create(badLens));});
    auto longData=asset->data();longData.duration=3600;longData.tracks[5].start=1000;longData.tracks[5].end=1010;CinematicPreview longPreview(f.scene,f.views(),CinematicSequence::create(longData));longPreview.seek(1009);check(longPreview.replaySteps()<=600&&longPreview.particleCount()>0,"FX seek replayed the full film");longPreview.seek(3599);check(longPreview.replaySteps()==0&&longPreview.particleCount()==0,"Inactive FX spent replay steps");
    auto& surface=f.scene.addChild<EffectComponent>("Surface trail",surfaceTrailEffect());surface.setSavedId("contact-effect");auto contact=asset->data();contact.tracks[5].target=surface.savedId();rejects([&]{CinematicPreview unsupported(f.scene,f.views(),CinematicSequence::create(contact));});
    auto decalAsset=std::make_shared<EffectAsset>();decalAsset->emitters.push_back({std::make_shared<ParticleEmitterDefinition>(),{}});EffectTrigger trigger;trigger.event=ParticleEventType::Birth;trigger.decal=EffectDecal{};decalAsset->triggers.push_back(trigger);decalAsset->validate();auto& decals=f.scene.addChild<EffectComponent>("Decal-only graph",decalAsset);decals.setSavedId("decal-effect");auto decalTrack=asset->data();decalTrack.tracks[5].target=decals.savedId();CinematicPreview decalPreview(f.scene,f.views(),CinematicSequence::create(decalTrack));decalPreview.seek(3);check(decalPreview.particleCount()>0,"Decal-only triggers broke FX preflight");
    RigidBodySettings body;body.motion=BodyMotion::Static;f.actor->addComponent<RigidBodyComponent>("Body",body);f.actor->addComponent<ColliderComponent>("Shape",CollisionShape::box({1,1,1}));f.scene.synchronize();rejects([&]{CinematicPreview candidate(f.scene,f.views(),asset);});
    std::unique_ptr<CinematicPreview> detached;{Fixture temporary;detached=std::make_unique<CinematicPreview>(temporary.scene,temporary.views(),CinematicSequence::create(temporary.data()));}detached->seek(4);check(detached->views()[0].camera->savedId()=="camera.b","Preview retained source pointer");
}
}
int main(){try{run();std::cout<<checks<<" cinematic checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
