#pragma once
#include "engine/math.hpp"
#include <memory>
#include <vector>
#include <optional>
namespace engine {
struct SplinePoint {
    Vec3 position{},tangent{0,0,10}; // Hermite derivative in local metres per segment.
    float bank=0; // Radians about forward, interpolated linearly between knots.
};
struct SplineData {
    std::vector<SplinePoint> points;
    bool closed=false;
    unsigned samplesPerSegment=64; // Fixed arc/query approximation, 8..128.
    void validate() const;
};
struct SplineSample { Vec3 position{},tangent{};float bank=0;double parameter=0,distance=0; };
struct SplineNearest { SplineSample sample;float distanceSquared=0; };
// Immutable cubic Hermite curve: C1 at knots, not necessarily C2. No scene dependency.
class SplineAsset final {
public:
    static std::shared_ptr<const SplineAsset> create(SplineData);
    const SplineData& data() const{return data_;}
    unsigned segments() const{return unsigned(data_.points.size())-(data_.closed?0:1);}
    double length() const{return arc_.back();}
    std::size_t derivedBytes() const{return arc_.size()*sizeof(double);}
    SplineSample sample(double parameter) const; // Open clamps, closed wraps, finite only.
    SplineSample atDistance(double distance) const;
    // Approximate global polyline nearest + bounded local refinement. No partial result on budget exhaustion.
    std::optional<SplineNearest> nearest(Vec3 point,std::size_t maxIntervals=32768) const;
private:
    explicit SplineAsset(SplineData);
    SplineData data_;std::vector<double> arc_;
};
} // namespace engine
