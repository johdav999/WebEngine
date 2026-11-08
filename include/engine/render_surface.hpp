#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace engine {
// Immutable description and identity. Each renderer owns its own GPU allocation.
class RenderSurface {
public:
    static constexpr std::uint32_t maxDimension=2048;

    RenderSurface(std::uint32_t width,std::uint32_t height):width_(width),height_(height) {
        if(!width || !height || width>maxDimension || height>maxDimension)
            throw std::invalid_argument("Invalid render surface extent");
    }

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

private:
    const std::uint32_t width_,height_;
};

using RenderSurfaceHandle=std::shared_ptr<const RenderSurface>;
} // namespace engine
