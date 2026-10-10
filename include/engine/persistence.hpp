#pragma once
#include "engine/scene_archive.hpp"

namespace engine {
// Application schema and payload are distinct from the engine archive version.
struct ApplicationStateCodec {
    std::string application;
    std::uint32_t version=1;
    // Pure validation; throw on invalid current-version data. Required, including on save.
    std::function<void(std::span<const std::byte>)> validate;
    // Optional one-version-at-a-time migration: version N -> N+1. Never called for future data.
    std::function<std::vector<std::byte>(std::uint32_t,std::span<const std::byte>)> migrate;
};
struct SavedScene {
    const Scene* scene=nullptr; // Borrowed for save only.
    std::span<const RenderView> views;
    const AssetLibrary* assets=nullptr;
    std::span<const std::byte> applicationData;
};
struct ApplicationDocument {
    std::string application,activeScene; // activeScene is the scene root's savedId, or empty.
    std::uint32_t version=1;
    bool paused=false;
    std::vector<std::byte> state;
    std::vector<SceneDocument> scenes; // Separate staged ownership; no live runtime is changed.
};
class ApplicationArchive {
public:
    static std::vector<std::byte> save(const ApplicationStateCodec& schema,std::span<const std::byte> state,
        std::span<const SavedScene> scenes={},std::string_view activeScene={},bool paused=false,
        const ArchiveLimits& limits={},const ComponentCodecs& codecs={});
    static ApplicationDocument load(std::span<const std::byte> bytes,const ApplicationStateCodec& schema,
        const ArchiveLimits& limits={},const ComponentCodecs& codecs={});
};
} // namespace engine
