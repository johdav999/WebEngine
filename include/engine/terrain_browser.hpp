#pragma once
#include "engine/terrain_paging.hpp"
#include "engine/asset_browser.hpp"

namespace engine {
struct BrowserTerrainOptions {
    BrowserAssetOptions source{256*1024,30000};
    std::size_t maxResultBytes=8*1024*1024;
    std::string workerUrl="terrain-worker.js";
    bool collision=false;
    std::string storageNamespace; // Empty: read-only HTTP. Otherwise IndexedDB overlay first.
};
// engine_assets_browser: HTTP + page decode/base/LOD/raw-collider generation in
// isolated workers, two active and at most 32 queued/running across this module.
// Cancellation terminates workers; no scene or renderer references are captured.
AssetLoader browserTerrainPageLoader(std::string url,TerrainPageLayout,std::uint32_t page,
    std::uint64_t revision,std::shared_ptr<const Material>,BrowserTerrainOptions={});
// Uses web/terrain-storage.js; namespace must also be supplied to page loaders.
TerrainPageWriter browserTerrainPageWriter(std::string storageNamespace,std::uint32_t timeoutMilliseconds=30000);
} // namespace engine
