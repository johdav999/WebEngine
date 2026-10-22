#pragma once
#include "engine/component.hpp"
#include "engine/mesh.hpp"
#include "engine/blend_mode.hpp"
namespace engine {
struct SceneFrame;
struct DecalSettings {
    std::shared_ptr<const Texture> texture=Texture::white();
    Vec3 color{1,1,1};float opacity=1,normalOffset=.002f,normalThreshold=.1f;
    float lifetime=0,fadeOut=0; // Seconds; lifetime zero is persistent.
    BlendMode blend=BlendMode::Alpha;
    std::uint32_t receiverMask=0xffffffffu,maxSourceTriangles=20000,maxTriangles=4096;
    void validate() const;
    bool operator==(const DecalSettings& other) const;
};
// Unit box [-.5,.5] projects along local -Y; local XZ maps the full texture.
// Cached clipped geometry is presentation-only, not scene/physics state.
class DecalComponent : public Component {
public:
    explicit DecalComponent(std::string name="Decal",DecalSettings settings={});
    std::string_view typeName() const noexcept override{return "DecalComponent";}
    void setSettings(DecalSettings settings);
    const DecalSettings& settings() const{return settings_;}
    void validate() const override {Component::validate();settings_.validate();}
    std::uint32_t visibilityMask=1;
    void appendDraw(SceneFrame& frame,const Mat4& world) const;
    bool projectionLimited() const{return limited_;}
    std::size_t triangleCount() const{return projected_?projected_->indices.size()/3:0;}
    std::uint64_t projectionBuilds() const{return builds_;}
    double age() const{return age_;}
    float effectiveOpacity() const;
protected:
    void tick(double seconds) override;
private:
    friend class ArchiveCodec;
    friend class EffectComponent;
    double age_=0;
    bool effectSpawned_=false;
    Transform spawnPose_;
    DecalSettings settings_;
    mutable std::shared_ptr<const Mesh> projected_;
    mutable std::uint64_t key_=0,builds_=0;
    mutable bool cached_=false,limited_=false;
};
}
