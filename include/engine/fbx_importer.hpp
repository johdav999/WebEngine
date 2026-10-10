#pragma once
#include "engine/mesh.hpp"
#include <cstddef>
#include <span>
#include <functional>
#include <string_view>
#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif

namespace engine {
struct FbxImportMeasurements {
    double parseMs=0,textureDecodeMs=0,convertMs=0,optimizeMs=0,totalMs=0;
    std::size_t decodedTextureBytes=0,decodedTextures=0;
    std::size_t inputVertices=0,outputVertices=0;
};
struct FbxImportOptions {
    bool optimizeMeshes=true; // Lossless; disable for measurements/comparison.
    std::size_t maxFileBytes=32*1024*1024;
    std::size_t maxVertices=1'000'000;
    std::size_t maxIndices=3'000'000;
    std::size_t parserMemoryBytes=128*1024*1024; // Limit for each parser allocator.
    TextureLimits textureLimits;
    std::size_t maxTextureBytes=128*1024*1024; // Total decoded RGBA bytes per import.
    std::size_t maxAnimationBytes=32*1024*1024;
    std::size_t maxMorphTargets=128,maxMorphBytes=64*1024*1024;
    double animationSampleRate=30; // Inclusive endpoints; clips interpolated at runtime.
    // Explicit companion-file resolver. Empty data means missing, retained as a warning.
    // loadMemory makes no implicit filesystem or network reads.
    // Native loadFile defaults to companion images confined to the model directory.
    std::function<std::vector<std::byte>(std::string_view)> resolveTexture;
    FbxImportMeasurements* measurements=nullptr; // Optional caller-owned output, valid through the call.
};
class FbxImporter {
public:
    // Binary or ASCII FBX: geometry, skin bindings and sampled clips in Y-up meters.
    // All mesh instances are merged with their geometry-to-world transforms baked in.
    // Throws on invalid input or limits; owns no references to the supplied byte buffer.
    static std::shared_ptr<const Mesh> loadMemory(std::span<const std::byte> data,
        const FbxImportOptions& options={});
#ifndef __EMSCRIPTEN__
    static std::shared_ptr<const Mesh> loadFile(const std::filesystem::path& path,
        const FbxImportOptions& options={});
#endif
};
} // namespace engine
