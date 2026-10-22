#pragma once
#include "engine/asset_decode.hpp"
#include "engine/desktop_services.hpp"

namespace engine {
struct DesktopAssetExecutorSettings {
    std::size_t workers=2; // 1..8; includes IO, decoding and hydration.
    std::size_t maxQueued=64; // 1..256; excludes running jobs.
};
struct DesktopAssetExecutorDiagnostics {std::size_t active=0,queued=0;};
// Explicit owner-thread service, never a global pool. Loaders hold weak service
// references, copied paths/decoders, and no scene/renderer borrows. Destruction
// fails pending operations, cancels queued work and joins all worker threads.
// Cancel/timeout suppress publication promptly; running synchronous importers
// cannot be interrupted and retain their slot until they return. Never capture
// the executor or destroy it from a decoder. Custom captures must be thread safe.
class DesktopAssetExecutor {
public:
    explicit DesktopAssetExecutor(DesktopAssetExecutorSettings settings={});
    ~DesktopAssetExecutor();
    DesktopAssetExecutor(const DesktopAssetExecutor&)=delete;
    DesktopAssetExecutor& operator=(const DesktopAssetExecutor&)=delete;
    DesktopAssetExecutorDiagnostics diagnostics() const;
    AssetLoader decodedFileLoader(std::filesystem::path,AssetDecodeKind,AssetDecodeOptions={}) const;
    AssetLoader decodedAssetLoader(const DesktopFiles&,std::filesystem::path relative,AssetDecodeKind,AssetDecodeOptions={}) const;
    AssetLoader fingerprintFileLoader(std::filesystem::path path,AssetSourceOptions options={}) const {
        return decodedFileLoader(std::move(path),AssetDecodeKind::Fingerprint,{options,4096});
    }
    // Decoder runs off-thread, owns its returned resource and borrows bytes only
    // during this call. It must not access scenes, GUI, GPU or owner-only services.
    template<class T> AssetLoader fileLoader(std::filesystem::path path,
        std::function<std::shared_ptr<const T>(std::span<const std::byte>)> decode,AssetSourceOptions options={}) const {
        return makeLoader([path=checkedPath(std::move(path))]{return path;},wrap<T>(std::move(decode)),options);
    }
    template<class T> AssetLoader assetLoader(const DesktopFiles& files,std::filesystem::path relative,
        std::function<std::shared_ptr<const T>(std::span<const std::byte>)> decode,AssetSourceOptions options={}) const {
        (void)files.assetPath(relative);
        return makeLoader([files,relative=std::move(relative)]{return files.assetPath(relative);},wrap<T>(std::move(decode)),options);
    }
private:
    using Publish=std::function<void(const AssetLoadContext&)>;
    using Decode=std::function<Publish(std::span<const std::byte>)>;
    template<class T> static Decode wrap(std::function<std::shared_ptr<const T>(std::span<const std::byte>)> decode) {
        if(!decode)throw std::invalid_argument("Missing background asset decoder");
        return [decode=std::move(decode)](std::span<const std::byte> bytes)->Publish {
            auto resource=decode(bytes);
            if(!resource)throw std::runtime_error("Background decoder returned null");
            return [resource=std::move(resource)](const AssetLoadContext& context){context.complete<T>(resource);};
        };
    }
    static std::filesystem::path checkedPath(std::filesystem::path);
    AssetLoader makeLoader(std::function<std::filesystem::path()>,Decode,AssetSourceOptions) const;
    static Decode decoder(AssetDecodeKind,AssetDecodeOptions);
    struct Impl;std::shared_ptr<Impl> impl_;
};
}
