#pragma once
#include "engine/assets.hpp"
#include "engine/asset_reload.hpp"
#include "engine/scene_archive.hpp"
#include <variant>

namespace engine {
enum class AssetDecodeKind { Fbx, Glb, Image, TextureBundle, Fingerprint };
struct AssetDecodeOptions {
    AssetSourceOptions source;
    std::size_t maxResultBytes=256*1024*1024;
};
using DecodedAsset=std::variant<std::shared_ptr<const Mesh>,std::shared_ptr<const Texture>,std::shared_ptr<const AssetFingerprint>>;
// Hydrate immutable resources without publishing. Native workers can do this
// before their cancellation/deadline gate; browser owners use the same validator.
DecodedAsset hydrateDecodedAsset(std::span<const std::byte>,AssetDecodeKind,const ArchiveLimits& limits={});
// Isolated decoder transport. Import uses existing default importer limits.
// Hydration validates/copies archives; the host chooses the executing thread.
std::vector<std::byte> decodeAssetBytes(std::span<const std::byte>,AssetDecodeKind,
    const ArchiveLimits& limits={});
void completeDecodedAsset(std::span<const std::byte>,AssetDecodeKind,const AssetLoadContext&,
    const ArchiveLimits& limits={});
}
