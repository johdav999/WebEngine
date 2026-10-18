#pragma once
#include "engine/texture.hpp"

namespace engine {
// Strict 2D, complete, pre-encoded KTX2 chains; no runtime transcoder.
CompressedTexture loadKtx2(std::span<const std::byte> bytes,TextureSemantic semantic,
    const TextureLimits& limits={});
struct TextureVariantSource {
    TextureSemantic semantic=TextureSemantic::Color;
    std::vector<std::byte> ktx2;
};
// Versioned delivery bundle retains the original encoded RGBA fallback plus KTX2.
std::vector<std::byte> packTextureBundle(std::span<const std::byte> fallback,
    std::span<const TextureVariantSource> variants,const TextureLimits& limits={});
std::shared_ptr<const Texture> loadTextureBundle(std::span<const std::byte> bytes,
    std::string name={},const TextureLimits& limits={});
}
