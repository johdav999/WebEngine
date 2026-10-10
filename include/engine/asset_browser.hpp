#pragma once
#include "engine/assets.hpp"

namespace engine {
struct BrowserAssetOptions {
    std::size_t maxBytes=32*1024*1024;
    std::uint32_t timeoutMilliseconds=30000;
};
// The byte span is borrowed only for the decode call. Publish through the context.
using AssetByteDecoder=std::function<void(std::span<const std::byte>,const AssetLoadContext&)>;
// Browser-only target engine_assets_browser; asynchronous HTTP, no IndexedDB.
// Decoding runs on the browser thread and may block it; no worker is implied.
AssetLoader browserAssetLoader(std::string url,AssetByteDecoder decoder,BrowserAssetOptions options={});
} // namespace engine
