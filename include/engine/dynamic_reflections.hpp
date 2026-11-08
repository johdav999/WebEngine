#pragma once
#include "engine/dynamic_gi.hpp"
#include "engine/environment.hpp"

namespace engine {
struct ReflectionProbeSnapshot {
    std::shared_ptr<const Environment> environment;
    Vec3 position{}, minimum{-4,0,-4}, maximum{4,3,4};
    float intensity=1, blendDistance=.5f;
    void validate() const;
};

// Portable references for the per-fragment blend and box projection.
float reflectionProbeWeight(const ReflectionProbeSnapshot&,Vec3 position);
Vec3 reflectionProbeDirection(const ReflectionProbeSnapshot&,Vec3 position,Vec3 direction);

struct ReflectionCaptureSettings {
    Vec3 position{0,1,0}, minimum{-4,0,-4}, maximum{4,3,4};
    float intensity=1, blendDistance=.5f, rayDistance=30;
    std::uint32_t size=16, workPerUpdate=128, convolutionSamples=16;
    void validate() const;
};
struct ReflectionCaptureProgress {
    std::uint32_t completed=0, total=0;
    bool pending=false;
};

// Owner-thread, camera-independent capture from the existing GI proxy BVHs.
// Each update spends at most workPerUpdate primary reflection rays or filter taps.
// The shared tracer also performs its bounded per-light shadow queries at hits.
// A shared immutable trace lease pins geometry for the complete capture cycle.
// Geometry/light/sky/GI changes are coalesced until the current capture finishes,
// then start the next capture with fresh inputs.
class DynamicReflectionCapture {
public:
    DynamicReflectionCapture();
    ~DynamicReflectionCapture();
    DynamicReflectionCapture(const DynamicReflectionCapture&)=delete;
    DynamicReflectionCapture& operator=(const DynamicReflectionCapture&)=delete;

    // Reuses an existing validated BRDF LUT. No environment bake occurs here.
    // Prior published reflections stay valid until the new capture is complete.
    void configure(const ReflectionCaptureSettings&,std::shared_ptr<const Environment> brdfSource);
    // Coalesces with pending work.
    void invalidate();
    // Returns true if any bounded capture/filter work ran. Publication happens only
    // after the entire cubemap and mip chain finish. Pass gi.completedSnapshot()
    // for additional diffuse bounces. The source is borrowed only during this call.
    bool update(const DynamicDiffuseGi& source,std::span<const SceneLight> lights,Vec3 skyRadiance,
                std::shared_ptr<const DiffuseGiSnapshot> incoming={});
    std::shared_ptr<const ReflectionProbeSnapshot> snapshot() const;
    ReflectionCaptureProgress progress() const;
    std::uint64_t revision() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace engine
