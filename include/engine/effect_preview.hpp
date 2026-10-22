#pragma once
#include "engine/effect_component.hpp"
#include "engine/camera_component.hpp"
#include "engine/scene.hpp"
namespace engine {
// Optional editor utility. Owns its scene/camera/particles; no borrowed source nodes.
// Release renderer scene/camera bindings before destroying this owner.
class EffectPreview {
public:
    EffectPreview();
    Scene& scene(){return scene_;}
    CameraComponent& camera(){return *camera_;}
    void start(std::shared_ptr<const EffectAsset> asset);
    void stop();
    void restart();
    void setTrailMotion(bool enabled); // Optional preview-only source orbit.
    bool trailMotion() const{return trailMotion_;}
    void pause(bool value){paused_=value;}
    void advance(double seconds);
    void step(double seconds=1.0/60);
    void isolate(int emitter); // -1 restores all; selection is preview-only.
    bool active() const{return active_;}
    bool paused() const{return paused_;}
    double seconds() const{return seconds_;}
    std::size_t particleCount() const;
    std::size_t trailPointCount() const;
    std::size_t impactCount() const;
    std::size_t collisionQueries() const;
    std::uint64_t droppedParticles() const;
    EffectCompositionStats compositionStats() const{return effect_?effect_->compositionStats():EffectCompositionStats{};}
private:
    Scene scene_{"FX preview"};
    CameraComponent* camera_;
    EffectComponent* effect_=nullptr;
    Actor* collisionFloor_=nullptr; // Lazily owned by scene_, only collision previews need physics.
    Node* ground_=nullptr;
    Node* slope_=nullptr;
    bool active_=false,paused_=false,trailMotion_=true;
    double seconds_=0;
    void positionTrailSources(double seconds);
};
}
