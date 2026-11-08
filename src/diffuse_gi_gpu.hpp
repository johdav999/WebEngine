#pragma once
#include "engine/dynamic_gi.hpp"

namespace engine {
inline constexpr std::size_t diffuseGiHalfTexels = maxDiffuseGiProbes / 2 * 12;
using DiffuseGiGpuTexel = std::array<float,4>;
struct DiffuseGiGpuData {
    std::array<DiffuseGiGpuTexel,3> metadata{};
    std::array<DiffuseGiGpuTexel,diffuseGiHalfTexels*2> probes{};
};
static_assert(maxDiffuseGiProbes == 128 && sizeof(DiffuseGiProbe) == 12 * 16);
static_assert(sizeof(DiffuseGiGpuData) == (3 + 1536) * 16);

inline DiffuseGiGpuData packDiffuseGi(const DiffuseGiSnapshot* snapshot) {
    DiffuseGiGpuData data;
    if (!snapshot) return data;
    const auto& volume = snapshot->volume;
    data.metadata = {{{volume.minimum.x, volume.minimum.y, volume.minimum.z, 0},
                      {volume.maximum.x, volume.maximum.y, volume.maximum.z, 1},
                      {float(volume.counts[0]), float(volume.counts[1]), float(volume.counts[2]), 0}}};
    auto destination = data.probes.begin();
    for (const auto& probe : snapshot->probes)
        destination = std::copy(probe.texels.begin(), probe.texels.end(), destination);
    return data;
}
} // namespace engine
