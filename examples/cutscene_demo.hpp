#pragma once

#include "engine/cinematic_runtime.hpp"
#include "../src/cinematic_example.hpp"
#include <algorithm>
#include <array>

namespace cutscene_demo {
using namespace engine;

enum class Mode { Scene3D, Scene2D, Mixed };

inline std::shared_ptr<const Mesh> signalCard() {
    auto texture=std::make_shared<Texture>();
    texture->name="Gate signal atlas";
    texture->width=128;
    texture->height=64;
    texture->wrapU=texture->wrapV=TextureWrap::Clamp;
    for(unsigned y=0;y<64;++y) {
        for(unsigned x=0;x<128;++x) {
            const auto u=x%64;
            const bool open=x>=64;
            const bool outline=u<3||u>60||y<3||y>60;
            const bool hinge=(u>10&&u<14)||(u>49&&u<53);
            const bool gate=hinge||(y>19&&y<46&&(open?(u<24||u>40):(u>13&&u<50)));
            const std::array<std::uint8_t,4> color=outline
                ? std::array<std::uint8_t,4>{100,142,157,255}
                : gate ? (open ? std::array<std::uint8_t,4>{80,232,182,255}
                               : std::array<std::uint8_t,4>{247,150,92,255})
                       : std::array<std::uint8_t,4>{16,34,48,255};
            texture->pixels.insert(texture->pixels.end(),color.begin(),color.end());
        }
    }
    auto material=std::make_shared<Material>();
    material->shader=MaterialShader::Unlit;
    material->alphaMode=AlphaMode::Blend;
    material->doubleSided=true;
    material->baseColorTexture=std::move(texture);
    auto quad=std::make_shared<Mesh>(*Mesh::plane());
    quad->materials={std::move(material)};
    return quad;
}

inline void appendSignalSprite(CinematicSequenceData& data,const MeshComponent& card,double start) {
    CinematicTrack sprite;
    sprite.id=card.savedId();
    sprite.name="Gate signal atlas";
    sprite.type=CinematicTrackType::Sprite;
    sprite.target=card.savedId();
    sprite.start=start;
    sprite.end=start+4;
    sprite.atlasColumns=2;
    CinematicKey fadeIn;
    fadeIn.time=start;
    fadeIn.opacity=0;
    CinematicKey closed;
    closed.time=start+.6;
    CinematicKey open=closed;
    open.time=start+1.7;
    open.spriteFrame=1;
    CinematicKey fadeOut=open;
    fadeOut.time=start+4;
    fadeOut.opacity=0;
    sprite.keys={fadeIn,closed,open,fadeOut};
    data.tracks.push_back(std::move(sprite));
}

inline void appendSignal(CinematicSequenceData& data,const CameraComponent& camera,
                         const MeshComponent& card,double start) {
    CinematicTrack shot;
    shot.id="signal-shot";
    shot.name="Orthographic signal";
    shot.type=CinematicTrackType::Camera;
    shot.target=camera.savedId();
    shot.start=start;
    shot.end=start+4;
    shot.keys={{start,camera.transform,4.8f},{start+4,camera.transform,3.4f}};
    data.tracks.push_back(std::move(shot));
    appendSignalSprite(data,card,start);
}

inline void caption(CinematicSequenceData& data,std::string id,std::string line,
                    double start,double end) {
    CinematicTrack track;
    track.id=std::move(id);
    track.name="Subtitle";
    track.type=CinematicTrackType::Subtitle;
    track.resource=std::move(line);
    track.start=start;
    track.end=end;
    data.tracks.push_back(std::move(track));
}

struct Demo {
    explicit Demo(Scene& scene,CameraComponent& gameplay) : gameplayCamera(gameplay.id()) {
        auto& root=scene.spawn("Cutscene game");
        gameplay.transform.position={0,5,9};
        gameplay.lookAt({0,1,0});
        gameplay.setPerspective(.85f,.1f,100);
        auto& player=root.addComponent<MeshComponent>("Player token",Mesh::cube());
        player.transform.position={-4,.4f,2};
        player.transform.scale={.7f,.7f,.7f};
        player.color={.2f,.85f,1};
        playerId=player.id();

        auto& gate=root.addComponent<MeshComponent>("Gate",Mesh::cube());
        gate.transform.position={0,.75f,2};
        gate.transform.scale={.25f,1.5f,1.8f};
        gate.color={.95f,.46f,.24f};
        gateId=gate.id();
        auto& wide=root.addComponent<CameraComponent>("Greeting camera");
        wide.setSavedId("cine.game-wide");
        auto greeting=populateCinematicExample(scene,wide)->data();
        greeting.name="Open the gate in 3D";
        caption(greeting,"greeting-caption","Keeper: Welcome. I'll open the gate.",.6,4);
        caption(greeting,"hand-off-caption","Keeper: The path is yours.",4.2,7.8);
        sequences[0]=CinematicSequence::create(greeting);

        auto& orthographic=root.addComponent<CameraComponent>("Signal camera");
        orthographic.setSavedId("cine.signal-camera");
        orthographic.transform.position={30,1,6};
        orthographic.lookAt({30,1,0});
        orthographic.setOrthographic(4.8f,.1f,100);
        auto& card=root.addComponent<MeshComponent>("Gate signal card",signalCard());
        card.setSavedId("cine.signal-card");
        card.transform.position={30,1,0};
        card.transform.rotation.x=pi/2;
        card.transform.scale={3.2f,1,3.2f};
        card.castsShadow=false;
        card.color={1,1,1};
        card.enabled=false;

        auto& overlay=root.addComponent<CameraComponent>("Signal overlay camera");
        overlay.transform.position={60,1,6};
        overlay.lookAt({60,1,0});
        overlay.setOrthographic(4.2f,.1f,100);
        overlay.setViewport({.69f,.56f,.28f,.38f});
        overlay.setVisibilityMask(2);
        overlayCamera=overlay.id();
        auto& inset=root.addComponent<MeshComponent>("Signal overlay card",card.mesh);
        inset.setSavedId("cine.signal-overlay");
        inset.transform=card.transform;
        inset.transform.position.x=60;
        inset.color=card.color;
        inset.castsShadow=false;
        inset.visibilityMask=2;
        inset.enabled=false;
        overlayCard=inset.id();

        CinematicSequenceData twoD;
        twoD.name="Open the gate in 2D";
        twoD.duration=4;
        appendSignal(twoD,orthographic,card,0);
        caption(twoD,"signal-closed","Gate closed. Waiting for clearance.",0,1.7);
        caption(twoD,"signal-open","Clearance granted. Gate open.",1.7,4);
        sequences[1]=CinematicSequence::create(std::move(twoD));

        greeting.name="Open the gate with a mixed cut";
        greeting.duration=12;
        appendSignalSprite(greeting,inset,4);
        appendSignal(greeting,orthographic,card,8);
        caption(greeting,"mixed-signal","Signal confirms the gate is open.",9.7,12);
        sequences[2]=CinematicSequence::create(std::move(greeting));
    }

    void start(Scene& scene,Mode mode,std::unique_ptr<AudioBackend> backend={},
               CinematicAudioResolver resolve={}) {
        runtime.start(scene,sequences.at(static_cast<std::size_t>(mode)),
                      std::move(backend),std::move(resolve));
        if(auto* audio=runtime.audio()) audio->backend().activate();
    }

    bool playing() const { return runtime.status()==CinematicRuntimeStatus::Playing; }
    bool gateOpen() const { return gateOpen_; }
    CameraComponent* camera(Scene& scene) const {
        if(auto* shot=runtime.camera(scene)) return shot;
        return dynamic_cast<CameraComponent*>(scene.findComponent(gameplayCamera));
    }
    CameraComponent* overlay(Scene& scene) const {
        const auto* card=scene.findComponent(overlayCard);
        return card&&card->isActiveInHierarchy()
            ?dynamic_cast<CameraComponent*>(scene.findComponent(overlayCamera)):nullptr;
    }
    void move(Scene& scene,float delta) {
        if(playing()) return;
        if(auto* player=scene.findComponent(playerId)) {
            auto& x=player->transform.position.x;
            x=std::clamp(x+delta,-4.5f,gateOpen_?4.5f:-.65f);
        }
    }
    void reset(Scene& scene) {
        if(playing()) return;
        gateOpen_=false;
        if(auto* gate=scene.findComponent(gateId)) gate->transform.position.y=.75f;
        if(auto* player=scene.findComponent(playerId)) player->transform.position.x=-4;
        scene.synchronize();
    }
    void advance(Scene& scene,double delta) {
        if(!playing()) return;
        runtime.advance(scene,delta);
        applyOutcome(scene);
    }
    void skip(Scene& scene) {
        if(!playing()) return;
        runtime.skip(scene);
        applyOutcome(scene);
    }
    void applyOutcome(Scene& scene) {
        const auto status=runtime.status();
        if(gateOpen_||(status!=CinematicRuntimeStatus::Completed&&status!=CinematicRuntimeStatus::Skipped)) return;
        if(auto* gate=scene.findComponent(gateId)) {
            gate->transform.position.y=3.25f;
            gateOpen_=true;
            scene.synchronize();
        }
    }

    CinematicRuntime runtime;
    ComponentId gameplayCamera,playerId,gateId,overlayCamera,overlayCard;
    std::array<std::shared_ptr<const CinematicSequence>,3> sequences;
private:
    bool gateOpen_=false;
};
}
