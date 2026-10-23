#pragma once
#include "engine/network_scene.hpp"
#include <deque>
#include <optional>
namespace engine::net {
struct TransformSample {
  Transform transform;
  bool interpolated = false;
};
// Owner-thread presentation only. Caller supplies monotonic receipt timestamps
// in seconds. No owned clock, scene, callback or transport; no extrapolation.
class TransformTimeline {
public:
  explicit TransformTimeline(double delay = .1, double maxGap = .5,
                             float snapDistance = 4);
  bool push(double received, const Transform &); // true when history resets
  std::optional<TransformSample> sample(double now) const;
  void reset() { samples_.clear(); }
  std::size_t size() const { return samples_.size(); }
  static constexpr std::size_t capacity = 32;

private:
  struct Entry {
    double time;
    Transform transform;
  };
  double delay_, gap_;
  float distance_;
  std::deque<Entry> samples_;
};
// Positional reconciliation smoothing. Rebase only when prediction is
// corrected, never for ordinary input steps. Authority/prediction are never
// modified.
class CorrectionSmoother {
public:
  explicit CorrectionSmoother(double halfLife = .08, float snapDistance = 4);
  void rebase(Vec3 oldPrediction, Vec3 newPrediction);
  Transform present(const Transform &prediction, double deltaSeconds);
  void reset() { offset_ = {}; }
  Vec3 offset() const { return offset_; }

private:
  double halfLife_;
  float distance_;
  Vec3 offset_{};
};
} // namespace engine::net
