#pragma once
#include "engine/prefab_asset.hpp"
#include "engine/editor.hpp"

namespace engine {
struct PrefabOverrideInfo {std::string key,node,field;};
std::vector<PrefabOverrideInfo> prefabOverrides(const Actor&,const EditorPropertyRegistry&);
// Three-way merge. Explicit local field/structure differences win. Deleting an
// upstream node with local edits is a conflict and leaves all inputs unchanged.
std::unique_ptr<Actor> mergePrefab(const Actor& previous,const Actor& local,const Actor& incoming,
    const EditorPropertyRegistry&);
std::shared_ptr<const PrefabAsset> rebasePrefab(std::shared_ptr<const PrefabAsset> variant,
    std::shared_ptr<const PrefabAsset> base,const EditorPropertyRegistry&);
// Refresh bases and nested dependencies bottom-up, preserving parent/variant
// overrides. Returns the same pointer if unaffected. No live scene mutation.
std::shared_ptr<const PrefabAsset> refreshPrefabDependencies(std::shared_ptr<const PrefabAsset>,
    std::span<const std::shared_ptr<const PrefabAsset>> replacements,const EditorPropertyRegistry&);
// Independent asset workbench using ordinary scene authoring/history on a private
// scene. The live scene and source asset are never edited by these operations.
class PrefabAssetEditor {
public:
    explicit PrefabAssetEditor(std::shared_ptr<const PrefabAsset>);
    EditorSession& session(){return *session_;}
    Actor& definition(){return *static_cast<Actor*>(scene_.children()[0].get());}
    const std::shared_ptr<const PrefabAsset>& source() const {return asset_;}
    std::shared_ptr<const PrefabAsset> snapshot(std::string name) const;
    bool nestPrefab(std::shared_ptr<const PrefabAsset>,Vec3 position={});
private:
    std::shared_ptr<const PrefabAsset> asset_;
    Scene scene_;
    std::unique_ptr<EditorSession> session_;
};
}
