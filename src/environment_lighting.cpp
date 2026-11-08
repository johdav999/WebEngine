#include "engine/environment_lighting.hpp"

namespace engine {
namespace {
constexpr std::uint32_t cubeFaces = 6;
using Pixel = std::array<float, 4>;

bool finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Pixel blendPixels(const Pixel& a, const Pixel& b, float weight) {
    Pixel result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = std::lerp(a[i], b[i], weight);
    return result;
}

Pixel sampleFace(const EnvironmentLevel& level, std::uint32_t face, float u, float v) {
    const float x = std::clamp(u * level.size - 0.5f, 0.0f, float(level.size - 1));
    const float y = std::clamp(v * level.size - 0.5f, 0.0f, float(level.size - 1));
    const auto x0 = static_cast<std::uint32_t>(x);
    const auto y0 = static_cast<std::uint32_t>(y);
    const auto x1 = std::min(x0 + 1, level.size - 1);
    const auto y1 = std::min(y0 + 1, level.size - 1);
    const auto offset = std::size_t(face) * level.size * level.size;
    const auto& a = level.pixels[offset + y0 * level.size + x0];
    const auto& b = level.pixels[offset + y0 * level.size + x1];
    const auto& c = level.pixels[offset + y1 * level.size + x0];
    const auto& d = level.pixels[offset + y1 * level.size + x1];
    return blendPixels(blendPixels(a, b, x - x0), blendPixels(c, d, x - x0), y - y0);
}

void appendCube(EnvironmentLevel& output, const EnvironmentLevel& lower,
                const EnvironmentLevel& upper, float mipBlend) {
    if (mipBlend == 0 && lower.size == output.size) {
        output.pixels.insert(output.pixels.end(), lower.pixels.begin(), lower.pixels.end());
        return;
    }
    for (std::uint32_t face = 0; face < cubeFaces; ++face) {
        for (std::uint32_t y = 0; y < output.size; ++y) {
            for (std::uint32_t x = 0; x < output.size; ++x) {
                const float u = (x + 0.5f) / output.size;
                const float v = (y + 0.5f) / output.size;
                const auto a = sampleFace(lower, face, u, v);
                output.pixels.push_back(mipBlend == 0 ? a : blendPixels(a, sampleFace(upper, face, u, v), mipBlend));
            }
        }
    }
}
} // namespace

void LightingZone::validate() const {
    const auto extent = maximum - minimum;
    if (!finite(minimum) || !finite(maximum) || !finite(extent) ||
        !std::isnormal(blendDistance) || blendDistance <= 0 ||
        blendDistance > 0.5f * std::min({extent.x, extent.y, extent.z}) ||
        !std::isfinite(intensity) || intensity < 0 || !std::isfinite(rotation) || !environment)
        throw std::invalid_argument("Invalid lighting zone");
    environment->validate();
}

LightingZoneWeights sampleLightingZones(std::span<const LightingZone> zones, Vec3 position) {
    LightingZoneWeights weights;
    if (!finite(position))
        return weights;

    float total = 0;
    for (std::size_t i = 0; i < std::min(zones.size(), maxLightingZones); ++i) {
        const auto lower = position - zones[i].minimum;
        const auto upper = zones[i].maximum - position;
        const float edge = std::min({lower.x, lower.y, lower.z, upper.x, upper.y, upper.z});
        const float t = std::clamp(edge / zones[i].blendDistance, 0.0f, 1.0f);
        weights.local[i] = t * t * (3 - 2 * t);
        total += weights.local[i];
    }
    weights.global = std::max(1 - total, 0.0f);
    for (auto& local : weights.local)
        local /= std::max(1.0f, total);
    return weights;
}

LightingEnvironmentArray packLightingEnvironments(const Environment& global,
                                                   std::span<const LightingZone> zones,
                                                   const Environment* reflection) {
    if (zones.size() > maxLightingZones)
        throw std::invalid_argument("Too many lighting zones");
    global.validate();
    for (const auto& zone : zones)
        zone.validate();
    if (reflection)
        reflection->validate();

    const auto localCount=zones.size()+(reflection?1:0);
    LightingEnvironmentArray packed{global.specular, global.diffuse, static_cast<std::uint32_t>(1 + localCount)};
    packed.diffuse.pixels.reserve(packed.diffuse.pixels.size() * packed.cubeCount);
    for (const auto& zone : zones)
        appendCube(packed.diffuse, zone.environment->diffuse, zone.environment->diffuse, 0);
    if (reflection)
        appendCube(packed.diffuse, reflection->diffuse, reflection->diffuse, 0);

    const auto mipCount = packed.specular.size();
    for (std::size_t mip = 0; mip < mipCount; ++mip) {
        auto& level = packed.specular[mip];
        level.pixels.reserve(level.pixels.size() * packed.cubeCount);
        for (std::size_t i=0;i<localCount;++i) {
            const auto& environment=i<zones.size()?*zones[i].environment:*reflection;
            const auto& specular = environment.specular;
            const float sourceMip = float(mip * (specular.size() - 1)) / float(mipCount - 1);
            const auto lower = static_cast<std::size_t>(sourceMip);
            const auto upper = std::min(lower + 1, specular.size() - 1);
            appendCube(level, specular[lower], specular[upper], sourceMip - lower);
        }
    }
    return packed;
}
} // namespace engine
