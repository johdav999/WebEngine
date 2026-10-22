#pragma once
#include "engine/editor_cinematic.hpp"
#include "engine/effect_presets.hpp"
#include "engine/light_component.hpp"
#include "retarget_example.hpp"

inline std::shared_ptr<const engine::CinematicSequence> populateCinematicExample(engine::Scene& scene,engine::CameraComponent& wide){
    using namespace engine;
    auto& root=scene.spawn("Cinematic stage");root.setSavedId("cine.stage");
    auto& ground=root.addComponent<MeshComponent>("Stage floor",Mesh::plane(18));ground.color={.13f,.18f,.25f};
    for(int i=0;i<5;++i){auto& pillar=root.addComponent<MeshComponent>("Stage pillar",Mesh::cube());pillar.transform.position={float(i-2)*2,1,-3};pillar.transform.scale={.4f,2,.4f};pillar.color={.2f,.38f,.55f};}
    auto& performer=root.addChild<Actor>("Performer");performer.setSavedId("cine.performer");performer.transform.position={-2,0,0};
    auto& character=performer.addComponent<SkeletalMeshComponent>("Waving character",retargetExampleMesh(false));character.setSavedId("cine.character");character.animation().pause();
    auto& fx=performer.addComponent<EffectComponent>("Fountain cue",fountainEffect());fx.setSavedId("cine.effect");fx.transform.position={.7f,.1f,0};fx.stop(true);
    wide.transform.position={7,4,10};wide.lookAt({0,1.2f,0});wide.setPerspective(pi/3,.1f,100);
    auto& close=root.addComponent<CameraComponent>("Close shot");close.setSavedId("cine.camera.close");close.transform.position={3,2.6f,5};close.lookAt({1,1.4f,0});close.setPerspective(.7f,.1f,100);
    auto& light=root.addComponent<LightComponent>("Stage sun",LightType::Directional);light.direction={-.4f,-1,-.4f};light.intensity=3;light.shadow.enabled=true;scene.ambient={.3f,.3f,.3f};
    CinematicSequenceData d;d.name="A short greeting";
    CinematicTrack motion;motion.id="performer-motion";motion.name="Performer motion";motion.target=performer.savedId();motion.keys={{0,performer.transform},{4,{{1,0,0},{},{1,1,1}}},{8,{{-1,0,0},{},{1,1,1}}}};d.tracks.push_back(motion);
    CinematicTrack camera;camera.id="wide-shot";camera.name="Wide establishing shot";camera.type=CinematicTrackType::Camera;camera.target=wide.savedId();camera.end=4;camera.keys={{0,wide.transform},{4,wide.transform}};camera.keys.back().transform.position.x=5;d.tracks.push_back(camera);
    camera.id="close-shot";camera.name="Close greeting";camera.target=close.savedId();camera.start=4;camera.end=8;camera.keys={{4,close.transform,.7f},{8,close.transform,.7f}};camera.keys.back().transform.position.z=4;d.tracks.push_back(camera);
    CinematicTrack animation;animation.id="wave";animation.name="Wave and step";animation.type=CinematicTrackType::Animation;animation.target=character.savedId();animation.resource="Wave and step";animation.loop=true;d.tracks.push_back(animation);
    CinematicTrack sound;sound.id="music";sound.name="Quiet audio cue";sound.type=CinematicTrackType::Audio;sound.resource="assets/audio-stream-preview.mp3";sound.start=1;sound.end=7;sound.loop=true;sound.volume=.15f;d.tracks.push_back(sound);
    CinematicTrack effect;effect.id="fountain";effect.name="Fountain accent";effect.type=CinematicTrackType::Effect;effect.target=fx.savedId();effect.start=2;effect.end=6;d.tracks.push_back(effect);
    return CinematicSequence::create(std::move(d));
}
