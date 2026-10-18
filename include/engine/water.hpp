#pragma once
#include "engine/math.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace engine {
class Scene;
struct MeshDraw;
// Transient presentation inputs. Applications own contact detection and wake emission.
struct WaterInteraction {
    Vec3 position{},direction{1,0,0}; // World-space origin and wake heading.
    double startTime=0; // Same clock as Renderer::setWaterTime.
    float strength=.6f,radius=.7f,wavelength=.6f,speed=2,lifetime=4,wakeLength=0;
    std::uint32_t visibilityMask=0xffffffffu;
    void validate() const; // wakeLength==0: expanding ring; >0: trailing V wake.
};
inline constexpr std::size_t maxWaterInteractions=16;
class WaterInteractions {
public:
    void set(std::span<const WaterInteraction> values); // Validates the whole snapshot before publishing.
    std::span<const WaterInteraction> values() const { return {values_.data(),count_}; }
private:
    std::array<WaterInteraction,maxWaterInteractions> values_{};
    std::size_t count_=0;
};
// Bounded numeric staging with relative ages; no borrowed scene or renderer resources.
struct WaterInteractionParameters {
    std::array<float,4> options{};
    std::array<std::array<float,4>,maxWaterInteractions*4> data{};
};
WaterInteractionParameters waterInteractionParameters(const WaterInteractions&,double time,std::uint32_t mask=0xffffffffu);
// Presentation-only waves in mesh-local XZ. Published settings are immutable.
struct WaterSettings {
    float amplitude=0.12f,wavelength=3,speed=1,steepness=0.35f;
    Vec2 direction{1,0.3f};
    float rippleScale=6,rippleStrength=0.18f,reflectionStrength=1;
    Vec3 horizon{0.65f,0.8f,1},zenith{0.06f,0.2f,0.45f}; // Linear reflection radiance.
    float refractionStrength=0,refractionDistortion=0.015f,foamStrength=0,foamWidth=0.35f;
    Vec3 absorption{0.7f,0.24f,0.12f}; // Per world unit.
    float foamScale=8;
    Vec3 foamColor{0.85f,0.93f,1}; // Linear display color.
    float planarReflectionStrength=0; // Optional opaque World capture, one plane per view.
    float underwaterStrength=0,underwaterFogDensity=0.12f,underwaterMaxDistance=100;
    Vec3 underwaterColor{0.015f,0.12f,0.16f}; // Linear display-light scattering color.
    // Submerged receiver lighting: strength, cells/metre, clock multiplier, world-depth fade.
    float causticsStrength=0,causticsScale=2,causticsSpeed=1,causticsDepth=8;
    bool usesSceneInputs() const { return refractionStrength>0||foamStrength>0; }
    bool usesUnderwater() const { return underwaterStrength>0&&(causticsStrength>0||underwaterFogDensity>0||absorption.x>0||absorption.y>0||absorption.z>0); }
    void validate() const;
};
std::array<float,38> waterParameters(const WaterSettings& settings);
// Screen-space effects are supported only on active World-layer water.
bool waterSceneInputsRequired(const Scene& scene,std::uint32_t visibilityMask=0xffffffffu);
bool waterPlanarReflectionRequired(const Scene& scene,std::uint32_t visibilityMask=0xffffffffu);
struct WaterReflection {
    std::array<float,4> plane{}; // Normal points toward the original camera.
    Mat4 mirror=Mat4::identity();
};
// First camera-visible World water requesting planar reflection wins. No scene mutation.
std::optional<WaterReflection> waterReflection(std::span<const MeshDraw> draws,Vec3 eye);
struct UnderwaterView {
    Mat4 worldToLocal;
    Vec3 minimum,maximum; // Local XZ footprint; local Y<=0 is water, no bottom boundary.
    WaterSettings settings;
};
// First active World water containing the eye. Works even when its surface is offscreen.
std::optional<UnderwaterView> underwaterView(const Scene& scene,Vec3 eye,std::uint32_t visibilityMask=0xffffffffu);
// World distance within the footprint below the plane, clamped to the material limit.
float underwaterDistance(const UnderwaterView& water,Vec3 start,Vec3 end);
// Renderer clock is caller-owned; no wall-clock advancement or simulation writes.
void validateWaterTime(double seconds);
} // namespace engine
