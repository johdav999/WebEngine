#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace engine {
enum class TextureWrap { Repeat, Clamp };
enum class TextureSemantic { Color, Data, Normal };
struct TextureMip { std::uint32_t width=0,height=0;std::vector<std::uint8_t> pixels; };
enum class TextureCompression { BC7, ETC2RGBA, ASTC4x4 };
// Pre-encoded 4x4/16-byte blocks. Semantic-specific chains are authored offline.
struct CompressedTexture {
    TextureCompression format=TextureCompression::BC7;
    TextureSemantic semantic=TextureSemantic::Color;
    std::vector<TextureMip> levels;
    void validate(std::uint32_t width,std::uint32_t height) const;
};
struct TextureLimits {
    std::size_t maxFileBytes=32*1024*1024;
    std::uint32_t maxDimension=4096;
    std::size_t maxDecodedBytes=64*1024*1024;
};
// Immutable shared source pixels, top-down RGBA8. Material slots determine decoding.
struct Texture {
    std::string name;
    std::uint32_t width=0,height=0;
    std::vector<std::uint8_t> pixels;
    TextureWrap wrapU=TextureWrap::Repeat,wrapV=TextureWrap::Repeat;
    std::vector<CompressedTexture> compressed; // Optional variants; RGBA pixels remain the portable fallback.
    static std::shared_ptr<const Texture> loadMemory(std::span<const std::byte> bytes,
        std::string name={},const TextureLimits& limits={});
    static std::shared_ptr<const Texture> white();
    static std::shared_ptr<const Texture> flatNormal();
    std::vector<TextureMip> mipChain(TextureSemantic semantic) const;
};
} // namespace engine
