#include "engine/effect_archive.hpp"
namespace engine {
std::vector<std::byte> EffectArchive::save(std::shared_ptr<const EffectAsset> asset,const ArchiveLimits& limits) {
    if(!asset) throw std::invalid_argument("Missing effect asset");
    asset->validate();
    AssetLibrary library;library.effects.push_back(std::move(asset));return SceneArchive::saveAssets(library,limits);
}
std::shared_ptr<const EffectAsset> EffectArchive::load(std::span<const std::byte> bytes,const ArchiveLimits& limits) {
    auto library=SceneArchive::loadAssets(bytes,limits);
    if(library.effects.size()!=1||!library.effects[0]) throw std::invalid_argument("Effect file must contain exactly one effect");
    library.effects[0]->validate();return library.effects[0];
}
}
