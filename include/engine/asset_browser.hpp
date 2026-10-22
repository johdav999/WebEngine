#pragma once
#include "engine/assets.hpp"
#include "engine/asset_decode.hpp"

namespace engine {
using BrowserAssetOptions=AssetSourceOptions;
// The byte span is borrowed only for the decode call. Publish through the context.
using AssetByteDecoder=std::function<void(std::span<const std::byte>,const AssetLoadContext&)>;
// Browser-only target engine_assets_browser; asynchronous HTTP, no IndexedDB.
// Decoding runs on the browser thread and may block it; no worker is implied.
AssetLoader browserAssetLoader(std::string url,AssetByteDecoder decoder,BrowserAssetOptions options={});
// Retains the original aggregate layout for existing browser callers.
struct BrowserDecodeOptions {
    AssetSourceOptions source;
    std::size_t maxResultBytes=256*1024*1024;
    std::string workerUrl="asset-worker.js";
};
// Optional bounded worker queue (two active workers). Each operation owns its
// worker, including HTTP, decode timeout and cancellation; no scene captures.
AssetLoader browserDecodedAssetLoader(std::string url,AssetDecodeKind kind,BrowserDecodeOptions options={});
// Shared decoding policy with explicit browser-only worker location.
AssetLoader browserDecodedAssetLoader(std::string url,AssetDecodeKind kind,AssetDecodeOptions options,std::string workerUrl);
AssetLoader browserAssetFingerprintLoader(std::string url,AssetSourceOptions options={});
} // namespace engine
