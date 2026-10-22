#pragma once
#include "engine/render_quality.hpp"

namespace engine {
struct AdaptiveQualitySettings {
    double targetMs=16.67, smoothingSeconds=.5, warmupSeconds=1;
    double overloadSeconds=1, recoverySeconds=5, settleSeconds=1;
    double lowerRatio=1.12, recoverRatio=1.03;
    float step=.05f, minResolutionFactor=.5f, minDetailFactor=.5f;
    std::uint32_t minShadowResolution=256, minShadowMaps=1;
    void validate() const;
};
// Optional owner-thread presentation policy. No clocks, renderer, callbacks or
// scene references are retained. Feed completed, visible frame intervals in ms.
class AdaptiveQuality {
public:
    void start(const RenderQuality& ceiling,const AdaptiveQualitySettings& settings={});
    void configure(const AdaptiveQualitySettings&); // Retains pressure, restarts evidence.
    void stop(); // Restores effective() to the retained ceiling.
    void resetEvidence(); // Lifecycle/resize boundary: keep quality, discard old timing.
    bool observe(double intervalMs,bool eligible=true); // True only if effective settings changed.
    bool enabled() const {return enabled_;}
    const RenderQuality& ceiling() const {return ceiling_;}
    const RenderQuality& effective() const {return effective_;}
    const AdaptiveQualitySettings& settings() const {return settings_;}
    float pressure() const {return pressure_;} // 0 ceiling, 1 configured floor.
    double smoothedMs() const {return smoothed_;}
    std::uint64_t changes() const {return changes_;}
    const char* phase() const {return phase_;}
private:
    RenderQuality resolve() const;
    RenderQuality ceiling_,effective_;
    AdaptiveQualitySettings settings_;
    bool enabled_=false;
    float pressure_=0;
    double smoothed_=0,warmup_=0,settle_=0,over_=0,under_=0;
    std::uint64_t changes_=0;
    const char* phase_="disabled";
};
}
