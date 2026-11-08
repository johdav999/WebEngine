#pragma once
#include "engine/math.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine {
struct SceneLight;

inline constexpr std::size_t maxDiffuseGiProbes=128;
inline constexpr std::size_t maxDiffuseGiStaticTriangles=4096;
inline constexpr std::size_t maxDiffuseGiDynamicTriangles=512;
inline constexpr std::size_t maxDiffuseGiLights=8;
inline constexpr std::size_t maxDiffuseGiTraceRays=256;

struct DiffuseGiVolume {
    Vec3 minimum{-4,0,-4},maximum{4,3,4};
    std::array<std::uint32_t,3> counts{8,4,4};
    float rayDistance=20;
    std::uint32_t raysPerProbe=48,updatesPerFrame=4;
    std::uint32_t bounces=1; // Each later sweep reads only the preceding complete sweep.
    void validate() const;
    std::size_t probeCount() const; // Call on a validated volume.
};

// World-space, two-sided opaque proxy with linear Lambertian reflectance and emission.
struct DiffuseGiTriangle {
    Vec3 a,b,c;
    Vec3 diffuse{.5f,.5f,.5f};
    Vec3 emissive{};
};

struct DiffuseGiProbe {
    // First six texels contain cosine-weighted radiance RGB divided by pi, with
    // a validity flag in alpha. The next six contain mean ray distance, mean
    // squared ray distance, sample count and validity. A miss uses rayDistance.
    // Each set is ordered +X, -X, +Y, -Y, +Z, -Z.
    std::array<std::array<float,4>,12> texels{};
};
static_assert(sizeof(DiffuseGiProbe)==12*4*sizeof(float));

struct DiffuseGiSnapshot {
    DiffuseGiVolume volume;
    // Grid endpoints coincide with volume bounds. X is the fastest moving axis.
    std::vector<DiffuseGiProbe> probes;
    std::uint32_t completedBounces=0; // Minimum fully completed sweep count in this field.
    void validate() const;
};

// Visibility-aware irradiance divided by pi, faded at the volume boundary.
// Validates the supplied snapshot and finite, nonzero normal before sampling.
Vec3 sampleDiffuseGi(const DiffuseGiSnapshot& snapshot,Vec3 position,Vec3 normal);

struct DiffuseGiRay {
    Vec3 origin,direction;
    float maximumDistance=0; // Zero uses the configured volume ray distance.
};
struct DiffuseGiRayResult {
    Vec3 radiance;
    float distance=0;
    bool hit=false;
};

// Immutable lease of one configured proxy generation. Retains shared BVHs and
// the ray-distance policy independently of the source tracer's lifetime.
class DiffuseGiTraceScene {
public:
    ~DiffuseGiTraceScene();
    std::vector<DiffuseGiRayResult> traceRadianceBatch(std::span<const DiffuseGiRay> rays,
        std::span<const SceneLight> lights,Vec3 skyAmbient,
        std::shared_ptr<const DiffuseGiSnapshot> incoming={}) const;
    std::uint64_t geometryRevision() const;
private:
    friend class DynamicDiffuseGi;
    struct Impl;
    explicit DiffuseGiTraceScene(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
};

// Optional owner-thread presentation system. Geometry and illumination are
// supplied by the caller and may be replaced independently of simulation.
class DynamicDiffuseGi {
public:
    DynamicDiffuseGi();
    ~DynamicDiffuseGi();
    DynamicDiffuseGi(const DynamicDiffuseGi&)=delete;
    DynamicDiffuseGi& operator=(const DynamicDiffuseGi&)=delete;

    void configure(const DiffuseGiVolume& volume);
    void replaceStatic(std::span<const DiffuseGiTriangle> triangles);
    void replaceDynamic(std::span<const DiffuseGiTriangle> triangles,Vec3 priorityWorldPosition);
    // Replaces the immutable snapshot only when a complete batch was traced.
    // The unchanged, converged field returns false without tracing or allocation.
    bool update(std::span<const SceneLight> lights,Vec3 skyAmbient);
    std::shared_ptr<const DiffuseGiSnapshot> snapshot() const;
    // Latest complete sweep in the current generation; null until the first one.
    std::shared_ptr<const DiffuseGiSnapshot> completedSnapshot() const;
    // Stable until accepted geometry/configuration changes; null before configure.
    std::shared_ptr<const DiffuseGiTraceScene> traceScene() const;
    // Synchronous, at most 256 rays and one million traversal/triangle tests.
    // Adds one incoming diffuse term at surface hits when a snapshot is supplied.
    // Rejects the complete batch on invalid data or exhaustion; retains no inputs.
    std::vector<DiffuseGiRayResult> traceRadianceBatch(std::span<const DiffuseGiRay> rays,
        std::span<const SceneLight> lights,Vec3 skyAmbient,
        std::shared_ptr<const DiffuseGiSnapshot> incoming={}) const;
    std::uint64_t revision() const;
    // Changes only when configure or either geometry replacement succeeds.
    std::uint64_t geometryRevision() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace engine
