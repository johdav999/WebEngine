#pragma once
#include "engine/material.hpp"
#include <span>

namespace engine {
// Atomic binding of a compiled browser graph. Maps are packed in ascending slot
// order: base RGBA, metallic/roughness, normal, occlusion, emissive.
// Unwritten maps inherit the source material (including imported maps).
Material bindMaterialGraph(const Material& source,std::span<const float> values,
    std::span<const std::uint8_t> pixels,std::uint32_t size,std::uint32_t mapMask);
}
