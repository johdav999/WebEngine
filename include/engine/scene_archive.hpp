#pragma once
#include "engine/scene.hpp"
#include "engine/render_view.hpp"
#include "engine/effect_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/terrain.hpp"
#include <span>
#include <typeindex>
#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif

namespace engine {
// Portable, self-contained archives. Resources are stored once by identity;
// runtime handles and GPU objects are recreated on load.
struct ArchiveLimits {
    std::size_t maxBytes=512ull*1024*1024,maxAllocatedBytes=512ull*1024*1024;
    std::uint32_t maxNodes=100000,maxResources=10000,maxDepth=128,maxElements=4000000;
};
struct AssetLibrary {
    std::vector<std::shared_ptr<const PhysicalSurface>> physicalSurfaces;
    std::vector<std::shared_ptr<const Mesh>> meshes;
    std::vector<std::shared_ptr<const Material>> materials;
    std::vector<std::shared_ptr<const Texture>> textures;
    std::vector<std::shared_ptr<const Skeleton>> skeletons;
    std::vector<std::shared_ptr<const Environment>> environments;
    std::vector<std::shared_ptr<const ParticleEmitterDefinition>> emitters;
    std::vector<std::shared_ptr<const EffectAsset>> effects;
    std::vector<std::shared_ptr<const CollisionShape>> collisionShapes;
    std::vector<std::shared_ptr<const TerrainAsset>> terrains;
};
struct SceneDocument {
    std::unique_ptr<Scene> scene;
    std::vector<RenderView> views;
    AssetLibrary assets; // Optional unreferenced assets are retained too.
    std::vector<std::byte> applicationData; // Opaque application metadata, never executed.
};
// Application-owned registry. Callbacks are copied for each synchronous operation;
// captures must outlive that operation. Factories return detached, childless components.
struct ComponentCodec {
    std::string key;
    std::type_index type=typeid(Component);
    std::uint32_t version=1;
    std::function<std::vector<std::byte>(const Component&)> encode;
    std::function<std::unique_ptr<Component>(std::uint32_t,std::span<const std::byte>)> decode;
    // Runs after the entire hierarchy is decoded, before lifecycle initialization.
    // Resolve saved references to fresh handles; throw for missing/wrong-type references.
    std::function<void(Component&,Scene&)> resolve;
};
class ComponentCodecs {
public:
    void add(ComponentCodec codec);
    const std::vector<ComponentCodec>& entries() const { return entries_; }
private:
    std::vector<ComponentCodec> entries_;
};
class SceneArchive {
public:
    static std::vector<std::byte> save(const Scene& scene,std::span<const RenderView> views={},
        const AssetLibrary& assets={},std::span<const std::byte> applicationData={},const ArchiveLimits& limits={},const ComponentCodecs& codecs={});
    // Constructs and validates a separate scene. A failure cannot change the caller's scene.
    static SceneDocument load(std::span<const std::byte> bytes,const ArchiveLimits& limits={},const ComponentCodecs& codecs={});
    static std::vector<std::byte> saveAssets(const AssetLibrary& assets,const ArchiveLimits& limits={});
    static AssetLibrary loadAssets(std::span<const std::byte> bytes,const ArchiveLimits& limits={});
    static AssetLibrary collectAssets(const Scene& scene);
    static SceneDocument clone(const Scene& scene,std::span<const RenderView> views={},const AssetLibrary& assets={},const ArchiveLimits& limits={},const ComponentCodecs& codecs={});
    // Clone built-in subtrees, share immutable assets, regenerate all saved identities.
    // No callbacks/custom behavior, scene roots or independently protected children.
    static std::unique_ptr<Node> duplicateSubtree(const Node& node,const ArchiveLimits& limits={});
#ifndef __EMSCRIPTEN__
    // Write a sibling temporary file and atomically replace the destination.
    static void saveFile(const std::filesystem::path& path,const Scene& scene,std::span<const RenderView> views={},
        const AssetLibrary& assets={},std::span<const std::byte> applicationData={},const ArchiveLimits& limits={},const ComponentCodecs& codecs={});
    static SceneDocument loadFile(const std::filesystem::path& path,const ArchiveLimits& limits={},const ComponentCodecs& codecs={});
    static void saveAssetsFile(const std::filesystem::path& path,const AssetLibrary& assets,const ArchiveLimits& limits={});
    static AssetLibrary loadAssetsFile(const std::filesystem::path& path,const ArchiveLimits& limits={});
#endif
};
} // namespace engine
