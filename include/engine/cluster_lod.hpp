#pragma once
#include "engine/mesh.hpp"
#include <functional>
#include <span>

namespace engine {
struct DetailView;
struct Frustum;

struct ClusterRange {
    std::uint32_t firstIndex=0,indexCount=0,page=0;
    MeshBounds bounds{}; // Filled for selected ranges, including coalesced ranges.
};
struct ClusterGroup {
    MeshBounds bounds;
    ClusterRange range;
    float error=0; // Accumulated local-space simplification error estimate.
    std::array<std::uint32_t,2> children{};
    bool leaf() const { return children[0]==0; }
};

// Immutable derived representation. Prepare before publishing a static Mesh.
// The packed geometry uses source vertex attributes, with indices ordered by level.
class ClusterLod {
public:
    static constexpr std::size_t maxRanges=64;
    static std::shared_ptr<const ClusterLod> build(const Mesh& source);
    // Cap local-space patch simplification error for a coarse streaming root.
    static std::shared_ptr<const ClusterLod> build(const Mesh& source,float maxPatchError);
    const std::shared_ptr<const Mesh>& geometry() const { return geometry_; }
    std::span<const ClusterGroup> groups() const { return groups_; }
    std::span<const std::uint32_t> roots() const { return roots_; }

    // Produces coalesced ranges in index order. Empty success means fully culled.
    // Failure leaves count zero so the caller can use ordinary mesh detail.
    bool select(std::size_t part,const Mat4& model,const DetailView& view,float scale,
                const Frustum* frustum,std::span<ClusterRange> output,std::size_t& count,
                const std::function<bool(std::uint32_t,double)>& pageReady={}) const;

private:
    friend class ClusterPageAsset;
    ClusterLod()=default;
    std::shared_ptr<const Mesh> geometry_;
    std::vector<ClusterGroup> groups_;
    std::vector<std::uint32_t> roots_;
};
} // namespace engine
