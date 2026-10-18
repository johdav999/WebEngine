#pragma once
#include "engine/scene_archive.hpp"
namespace engine {
// A .wefx is a versioned asset archive containing exactly one reusable effect.
// Textures and static surface meshes are embedded once by resource identity.
class EffectArchive {
public:
    static std::vector<std::byte> save(std::shared_ptr<const EffectAsset> asset,const ArchiveLimits& limits={});
    static std::shared_ptr<const EffectAsset> load(std::span<const std::byte> bytes,const ArchiveLimits& limits={});
};
}
