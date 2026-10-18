#pragma once
#include "engine/terrain.hpp"
#include "engine/terrain_paging.hpp"
#include "engine/scene.hpp"
#include "engine/orbit_controller.hpp"

class TerrainDemo {
public:
    void start(engine::Scene&,engine::OrbitController&,engine::CameraComponent&);
    void stop(engine::Scene&,engine::OrbitController&);
    void replace(engine::Scene&,engine::OrbitController&,std::shared_ptr<const engine::TerrainAsset>);
    void drop(engine::Scene&,float x=0,float z=0);
    void frame(engine::Scene&,engine::OrbitController&);
    void mark(engine::Scene&,const engine::TerrainHit&);
    bool edit(engine::Scene&,engine::TerrainBrush); // World X/Z brush center.
    bool history(engine::Scene&,bool redo);
    void streaming(engine::Scene&,engine::OrbitController&,bool enabled,engine::TerrainStreamSettings,engine::Vec2 worldFocus);
    void streamFocus(engine::Vec2 worldFocus);
    void paging(engine::Scene&,engine::OrbitController&,bool enabled,engine::TerrainPagingSettings,engine::Vec2 worldFocus);
    const engine::TerrainPagedStream* pages() const { return paged_.get(); }
    engine::Vec2 viewFocus() const {if(!paged_) return {};const auto o=paged_->settings().residency.origin;return {focus_.x+o.x,focus_.y+o.z};}
    void retryPages() { if(paged_) paged_->retryFailed(); }
    void flushPages() { if(paged_) paged_->flush(); }
    void discardPages() { if(paged_) paged_->clear(true); }
    void cancelPagedStamp() { if(paged_) paged_->cancelEdit(); }
    bool update(engine::Scene&); // Reports admissions/edits, so the app reapplies presentation policy.
    const auto& source() const { return asset_; }
    const engine::TerrainStreamStats* streamStats() const { return stream_ ? &stream_->stats() : nullptr; }
    std::size_t affected() const { return affected_; }
    std::size_t rebuilt() const { return rebuilt_; }
    std::size_t undoCount() const { return undo_.size(); }
    std::size_t redoCount() const { return redo_.size(); }
    bool active() const { return active_; }
    unsigned revision() const { return revision_; }
private:
    engine::ActorId ground_,decor_,marker_;
    engine::ComponentId camera_;
    engine::ProjectionMode previousProjection_=engine::ProjectionMode::Perspective;
    float previousFov_=1,previousNear_=.1f,previousFar_=500,previousHeight_=20;
    std::vector<engine::ActorId> balls_;
    engine::OrbitState previousOrbit_;
    std::vector<std::pair<std::string,bool>> previousNodes_;
    std::optional<engine::SkyBackground> previousSky_;
    engine::Vec3 previousAmbient_;
    float previousEnvironmentIntensity_=1;
    bool active_=false;
    unsigned revision_=0;
    std::shared_ptr<const engine::TerrainAsset> asset_;
    std::unique_ptr<engine::TerrainStream> stream_;
    std::unique_ptr<engine::TerrainPagedStream> paged_;
    engine::Vec2 focus_{};
    std::vector<engine::TerrainEdit> undo_,redo_;
    std::size_t affected_=0,rebuilt_=0;
    void apply(engine::Scene&,const engine::TerrainEdit&);
    void requireSavedPages() const;
};
