#pragma once
#include "engine/fbx_importer.hpp"

namespace engine {
// Identical resource limits, companion-image resolver and timing contract to FBX.
using GlbImportOptions = FbxImportOptions;
using GlbImportMeasurements = FbxImportMeasurements;
class GlbImporter {
public:
    // glTF 2.0 binary container or strict ZIP_STORED WebEngine material bundle.
    // Reads version-1 extras.WEBENGINE_materials; performs no bundle extraction.
    // Independent map UVs are rebaked into bounded, approximate triangle atlases.
    // Merges selected-scene instances in Y-up meters.
    // No implicit IO. Unsupported required extensions are rejected explicitly.
    static std::shared_ptr<const Mesh> loadMemory(std::span<const std::byte> data,
        const GlbImportOptions& options={});
#ifndef __EMSCRIPTEN__
    static std::shared_ptr<const Mesh> loadFile(const std::filesystem::path& path,
        const GlbImportOptions& options={});
#endif
};
}
