#pragma once
#include "engine/assets.hpp"
#include "engine/scene_archive.hpp"

namespace engine {
enum class AssetDecodeKind { Fbx, Glb, Image, TextureBundle };
// Isolated decoder transport. Import uses existing default importer limits.
// Hydration on the owner still validates/copies archives; no source parsing there.
std::vector<std::byte> decodeAssetBytes(std::span<const std::byte>,AssetDecodeKind,
    const ArchiveLimits& limits={});
void completeDecodedAsset(std::span<const std::byte>,AssetDecodeKind,const AssetLoadContext&,
    const ArchiveLimits& limits={});
}
