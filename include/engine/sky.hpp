#pragma once
#include "engine/camera_component.hpp"
#include "engine/fog.hpp"
#include "engine/texture.hpp"
#include "engine/mesh.hpp"

namespace engine {
class Scene;
enum class SkyMode { Gradient, Panorama, Atmosphere };
enum class SkyGeometry { Fullscreen, Sphere };
struct SkyClouds {
    bool enabled=false;
    float coverage=0.5f,density=1,scale=0.7f;
    Vec2 wind{0.015f,0.006f},offset{}; // Noise-domain units/second; explicit accumulated phase.
};
struct SkyAtmosphere {
    Vec3 sunDirection{0.4f,0.8f,0.2f}; // World-space direction towards the sun.
    float solarIntensity=20,rayleigh=1,mie=1,anisotropy=0.76f,altitude=1; // Altitude in km.
};
struct SkyDayNight {
    bool enabled=false,running=true;
    double hour=12,daySeconds=180;
    float azimuth=0,sunIntensity=2.2f,moonIntensity=0.06f;
};
// Scene-owned presentation data. Published panorama pixels are immutable sRGB RGBA8.
struct SkyBackground {
    SkyMode mode=SkyMode::Gradient;
    SkyGeometry geometry=SkyGeometry::Fullscreen;
    std::shared_ptr<const Texture> panorama;
    Vec3 horizon{0.65f,0.8f,1},zenith{0.05f,0.2f,0.55f},nadir{0.12f,0.14f,0.18f}; // Linear radiance.
    float rotation=0,brightness=1; // Radians about world Y; linear radiance multiplier.
    SkyClouds clouds;
    SkyAtmosphere atmosphere;
    SkyDayNight dayNight;
    void validate() const;
};
// Shared backend-neutral shader payload. No camera position or simulation state.
struct SkyUniform {
    std::array<float,4> right,up,forward,horizon,zenith,nadir;
    Mat4 clipFromDirection;
    std::array<float,4> sun,air,clouds,cloudWind;
    FogUniform fog;
    std::array<float,4> fogRight{},fogUp{};
};
SkyUniform prepareSky(const SkyBackground& sky,const CameraComponent& camera,float aspect);
std::shared_ptr<const Mesh> skySphereMesh(); // Shared inward-facing unit sphere, unlit material.
struct SkyDaylight { Vec3 sunDirection,color,ambient;float intensity,environmentIntensity;bool moon; };
SkyDaylight evaluateDayNight(const SkyDayNight& settings);
// Caller owns clock/scene. Resolves the optional directional light through a scene-scoped handle.
// Invalid data, stale/foreign/non-directional light handles publish nothing.
void advanceSky(Scene& scene,double deltaSeconds,ComponentId sun={});
} // namespace engine
