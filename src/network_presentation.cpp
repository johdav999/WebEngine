#include "engine/network_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace engine::net {
namespace {
void time(double t) {
  if (!std::isfinite(t) || t < 0)
    throw std::invalid_argument("Presentation time");
}
void point(Vec3 p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
      std::abs(p.x) > 1e6f || std::abs(p.y) > 1e6f || std::abs(p.z) > 1e6f)
    throw std::invalid_argument("Presentation position");
}
float length(Vec3 p) { return std::sqrt(dot(p, p)); }
bool flip(Vec3 a, Vec3 b) {
  return a.x * b.x <= 0 || a.y * b.y <= 0 || a.z * b.z <= 0;
}
float angle(float a, float b, float t) {
  return std::remainder(a + std::remainder(b - a, 2 * pi) * t, 2 * pi);
}
} // namespace
TransformTimeline::TransformTimeline(double delay, double gap, float distance)
    : delay_(delay), gap_(gap), distance_(distance) {
  if (!std::isfinite(delay) || delay < 0 || delay > .5 || !std::isfinite(gap) ||
      gap <= delay || gap > 10 || !std::isfinite(distance) || distance <= 0 ||
      distance > 1e6f)
    throw std::invalid_argument("Interpolation settings");
}
bool TransformTimeline::push(double received, const Transform &t) {
  time(received);
  (void)encodeTransform(t);
  if (!samples_.empty() && received < samples_.back().time)
    throw std::invalid_argument("Regressing receipt time");
  const bool cut =
      samples_.empty() || received - samples_.back().time > gap_ ||
      length(t.position - samples_.back().transform.position) > distance_ ||
      flip(t.scale, samples_.back().transform.scale);
  auto candidate = samples_;
  if (cut)
    candidate.clear();
  if (!candidate.empty() && received == candidate.back().time)
    candidate.back().transform = t;
  else
    candidate.push_back({received, t});
  if (candidate.size() > capacity)
    candidate.pop_front();
  samples_ = std::move(candidate);
  return cut;
}
std::optional<TransformSample> TransformTimeline::sample(double now) const {
  time(now);
  if (samples_.empty())
    return {};
  const auto target = now - delay_;
  if (target <= samples_.front().time)
    return TransformSample{samples_.front().transform, false};
  for (std::size_t i = 1; i < samples_.size(); ++i)
    if (target < samples_[i].time) {
      const auto &a = samples_[i - 1];
      const auto &b = samples_[i];
      const float f = float((target - a.time) / (b.time - a.time));
      Transform t;
      t.position = a.transform.position +
                   (b.transform.position - a.transform.position) * f;
      t.scale = a.transform.scale + (b.transform.scale - a.transform.scale) * f;
      auto x = a.transform.rotation, y = b.transform.rotation;
      t.rotation = {angle(x.x, y.x, f), angle(x.y, y.y, f), angle(x.z, y.z, f)};
      (void)encodeTransform(t);
      return TransformSample{t, true};
    }
  return TransformSample{samples_.back().transform, false};
}
CorrectionSmoother::CorrectionSmoother(double halfLife, float distance)
    : halfLife_(halfLife), distance_(distance) {
  if (!std::isfinite(halfLife) || halfLife <= 0 || halfLife > 1 ||
      !std::isfinite(distance) || distance <= 0 || distance > 1e6f)
    throw std::invalid_argument("Correction settings");
}
void CorrectionSmoother::rebase(Vec3 old, Vec3 next) {
  point(old);
  point(next);
  auto candidate = offset_ + old - next;
  offset_ = length(old - next) > distance_ || length(candidate) > distance_
                ? Vec3{}
                : candidate;
}
Transform CorrectionSmoother::present(const Transform &predicted, double dt) {
  time(dt);
  (void)encodeTransform(predicted);
  auto next = dt > .5 ? Vec3{} : offset_ * float(std::exp2(-dt / halfLife_));
  if (length(next) < .0001f)
    next = {};
  auto result = predicted;
  result.position = result.position + next;
  result.position = {std::clamp(result.position.x, -1e6f, 1e6f),
                     std::clamp(result.position.y, -1e6f, 1e6f),
                     std::clamp(result.position.z, -1e6f, 1e6f)};
  (void)encodeTransform(result);
  offset_ = next;
  return result;
}
} // namespace engine::net
