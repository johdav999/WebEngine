#pragma once
#include <cstdint>

namespace engine {
enum class RenderLayer : std::uint32_t { Background, World, Overlay, Count };
} // namespace engine
