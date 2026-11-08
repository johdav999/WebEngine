#pragma once
#include <cstdint>

namespace engine::fog_grid {
// The shaders store sixteen depth slices vertically in one RGBA16F texture.
inline constexpr std::uint32_t width=64,rows=36,slices=16,height=rows*slices;
}
