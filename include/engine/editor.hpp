#pragma once
#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include "engine/scene_archive.hpp"
#include "engine/editor_properties.hpp"
#include "engine/terrain.hpp"
#include "engine/route.hpp"
#include "engine/region_stream.hpp"
#include "engine/decal.hpp"
#include "engine/ai_controller.hpp"
#include "engine/animation_library.hpp"
#include "engine/prefab_asset.hpp"

namespace engine {
enum class EditorGizmo { Move, Rotate, Scale };
struct EditorSnap {
    bool translation=false,rotation=false;
    float grid=1,angle=pi/12;
    void validate() const;
};
// Optional authoring session. Borrows an idle, non-ticking Scene which must outlive
// it. Saved identities address nodes; history and selection are never archived.
class EditorSession {
public:
    explicit EditorSession(Scene& scene,std::size_t historyLimit=128,AssetLibrary* assets=nullptr,
        std::shared_ptr<const EditorPropertyRegistry> properties=EditorPropertyRegistry::builtins());
    ~EditorSession();
    EditorSession(const EditorSession&)=delete;
    EditorSession& operator=(const EditorSession&)=delete;
    Node* selected() const;
    SkeletalMeshComponent* selectedSkeletalMesh() const;
    Actor* selectedPrefabRoot() const;
    bool placePrefab(std::shared_ptr<const PrefabAsset>,Vec3 position={},Node* parent=nullptr);
    bool linkPrefab(std::shared_ptr<const PrefabAsset>);
    bool updatePrefabs(std::span<const std::shared_ptr<const PrefabAsset>>,bool revertSelected=false);
    bool unpackPrefab();
    bool setAnimationLibrary(std::shared_ptr<const AnimationLibrary>);
    AiControllerComponent* selectedAiController() const;
    bool setAiController(AiControllerSettings,std::shared_ptr<const AiBindingRegistry> registry=AiBindingRegistry::builtins());
    bool select(std::string_view savedId); // Empty clears; invalid selection preserves it.
    bool selectMany(const std::vector<std::string>& ids); // Last ID is the primary inspector target.
    bool toggleSelection(std::string_view id);
    std::vector<std::string> selectionIds() const;
    std::vector<Node*> selectionRoots() const; // Ancestor selection covers descendants once.
    bool createRegionManifest(std::shared_ptr<const RegionManifest>);
    RegionManifestComponent* selectedRegionManifest() const;
    bool setRegionManifest(std::shared_ptr<const RegionManifest>);
    bool createRoute(std::shared_ptr<const RouteAsset>,Vec3 position={});
    RouteComponent* selectedRoute() const;
    bool setRoute(std::shared_ptr<const RouteAsset>); // One exact-state grouped edit; source/collision published together.
    bool createVehicle(Vec3 position={0,.95f,0}); // Example preset, undoable.
    bool addComponent(std::string_view type,std::shared_ptr<const Mesh> mesh={});
    bool removeSelectedComponents();
    bool reparentSelected(std::string_view parent,ReparentMode mode=ReparentMode::KeepWorld);
    Vec3 selectionPivot() const;
    std::string pick(Ray ray,std::uint32_t visibilityMask=0xffffffffu,
        float minDistance=0,float maxDistance=1000) const; // Visible mesh bounds, no providers needed.
    std::string readOnlyReason(const Node& node) const;
    bool setTransform(const Transform& transform);
    bool beginTransform();
    bool previewTransform(const Transform& transform);
    bool beginGizmo(EditorGizmo mode,int axis,Ray ray);
    bool dragGizmo(Ray ray);
    bool commit();
    bool cancel(); // Does not overwrite a later external write or changed parent.
    bool transforming() const { return transaction_.has_value()||terrainTransaction_.has_value(); }
    Actor* terrainActor() const; // Selected terrain chunk or its complete owning actor.
    std::string terrainReadOnlyReason(const Actor& actor) const;
    bool createTerrain(TerrainData data,Vec3 origin={},bool collision=false);
    bool beginTerrainStroke();
    bool stampTerrain(const TerrainBrush& brush); // Source-local coordinates; preview only.
    bool commitTerrainStroke(); // One command for the whole gesture.
    bool setTerrainLayerTexture(std::size_t layer,std::shared_ptr<const Texture> texture); // Null clears.
    bool undo();
    bool redo();
    std::size_t undoCount() const { return cursor_; }
    std::size_t redoCount() const { return history_.size()-cursor_; }
    bool dirty() const { return state_!=savedState_; }
    void markSaved() { savedState_=state_; }
    void markRecovered() { savedState_=std::numeric_limits<std::uint64_t>::max(); }
    std::uint64_t revision() const { return revision_; }
    std::vector<EditorPropertyInfo> properties() const;
    bool setProperty(std::string_view key,const EditorValue& value);
    EffectComponent* selectedEffect() const;
    bool createEffect(std::shared_ptr<const EffectAsset> asset,Vec3 position={});
    bool setEffectAsset(std::shared_ptr<const EffectAsset> asset); // Atomic subtree replacement; exact-state undo.
    bool setCollider(std::shared_ptr<const CollisionShape>,std::shared_ptr<const PhysicalSurface> surface);
    bool setTerrainPhysicalSurfaces(std::shared_ptr<const PhysicalSurface>,std::vector<std::shared_ptr<const PhysicalSurface>> cells={});
    bool setDecalTexture(std::shared_ptr<const Texture> texture);
    bool setMaterial(std::size_t slot,const Material& material);
    bool clearMaterial(std::size_t slot);
    bool importAssets(const AssetLibrary& assets);
    AssetLibrary& assets() { return *assets_; }
    const AssetLibrary& assets() const { return *assets_; }
    bool placeMesh(std::shared_ptr<const Mesh> mesh,std::string name,Vec3 groundAnchor);
    bool duplicateSelected(Vec3 worldOffset={1,0,0});
    Vec3 placementPoint(Ray ray) const; // Authoring ground plane Y=0.
    void setSnap(EditorSnap snap) { snap.validate();snap_=snap; }
    const EditorSnap& snap() const { return snap_; }
    std::string propertyReadOnlyReason(const Node& node) const;
private:
    struct Change {
        std::string target,parent;
        Mat4 parentWorld;
        Transform before,after;
        std::uint64_t beforeState=0,afterState=0;
    };
    struct Transaction {
        Change change;
        std::vector<Change> changes;
        std::vector<Mat4> worlds;
        Mat4 world;
        EditorGizmo mode=EditorGizmo::Move;
        Vec3 axis{},pivot{},first{},previous{};
        double angle=0;
        float scaleLength=1;
        bool uniform=false;
        bool gizmo=false;
    };
    struct BatchTransform { std::vector<Change> changes;std::vector<std::string> selection; };
    struct StructureItem {
        std::string target,beforeParent,afterParent;
        Transform before,after;
        Mat4 beforeWorld,afterWorld;
        std::size_t beforeIndex=0,afterIndex=0;
        std::unique_ptr<Node> parked;
        std::uint64_t fingerprint=0;
    };
    struct StructureEdit { std::vector<StructureItem> items;std::vector<std::string> beforeSelection,afterSelection; };
    struct PropertyEdit { EditorPropertyDefinition definition;EditorValue before,after; };
    struct RegionManifestEdit {std::shared_ptr<const RegionManifest> before,after;};
    struct RouteEdit {std::shared_ptr<const RouteAsset> before,after;};
    struct ColliderEdit {std::shared_ptr<const CollisionShape> before,after;std::shared_ptr<const PhysicalSurface> beforeSurface,afterSurface;};
    struct DecalEdit { DecalSettings before,after; };
    struct MaterialEdit {
        std::shared_ptr<const Mesh> mesh;
        std::vector<std::optional<MaterialInstance>> before,after;
    };
    struct CreationEdit {
        std::unique_ptr<Node> parked;
        std::vector<std::string> previousSelection;
        std::uint64_t fingerprint=0;
    };
    struct LibraryEdit { AssetLibrary before,after;std::vector<std::string> previousSelection; };
    struct EffectEdit {
        std::unique_ptr<Node> parked;
        std::uint64_t fingerprint=0;
        std::size_t index=0;
    };
    struct TerrainChange {
        TerrainEdit edit;
        std::vector<std::string> selection;
        std::uint64_t beforeFingerprint=0,afterFingerprint=0;
    };
    struct AiControllerEdit {AiControllerSettings before,after;};
    struct AnimationEdit {std::unique_ptr<Node> parked;std::uint64_t expected=0;};
    struct PrefabSwap {std::string target,parent;Mat4 parentWorld;std::size_t index;std::uint64_t expected;std::unique_ptr<Node> parked;};
    struct PrefabEdit {std::vector<PrefabSwap> items;std::vector<std::string> selection;};
    struct Command;
    bool replayPrefab(Command& command);
    bool replacePrefabRoots(const std::vector<Actor*>&,std::vector<std::unique_ptr<Node>>);
    struct Command {
        Change change;
        std::variant<std::monostate,PropertyEdit,MaterialEdit,CreationEdit,LibraryEdit,BatchTransform,StructureEdit,TerrainChange,EffectEdit,DecalEdit,ColliderEdit,RouteEdit,AiControllerEdit,RegionManifestEdit,AnimationEdit,PrefabEdit> edit;
    };
    bool replayAnimation(Command& command);
    Scene& scene_;
    std::string selection_;
    std::vector<std::string> selections_;
    std::size_t limit_,cursor_=0;
    std::vector<Command> history_;
    std::optional<Transaction> transaction_;
    std::optional<Command> terrainTransaction_;
    std::uint64_t state_=0,savedState_=0,serial_=0;
    std::uint64_t revision_=0;
    AssetLibrary ownedAssets_;
    AssetLibrary* assets_;
    std::shared_ptr<const EditorPropertyRegistry> properties_;
    EditorSnap snap_;
    Node* resolve(const Change& change,const Transform& expected) const;
    void apply(Node& node,const Transform& transform);
    std::optional<Vec3> sample(Ray ray) const;
    void push(Command command);
    bool replay(Command& command,bool redo);
    bool create(std::unique_ptr<Node> node,Node& parent);
    bool replayStructure(StructureEdit& edit,bool redo);
    bool replayTransforms(const std::vector<Change>& changes,bool redo);
    void restoreSelection(const std::vector<std::string>& ids);
    void validateHierarchy() const;
    void writeProperty(Node& node,const EditorPropertyDefinition& definition,const EditorValue& value);
    std::uint64_t fingerprint(const Node& node) const;
    bool replayEffect(Command& command);
    bool replayTerrain(Command& command,bool redo);
    bool cancelTerrainStroke();
    void publishTerrain(Actor& actor,const TerrainEdit& edit);
};
} // namespace engine
