#pragma once
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/collider_component.hpp"
#include <optional>

namespace engine {
// Row-major samples: heights[z*samplesX+x], local +Y up, origin at sample (0,0).
// Construct with TerrainAsset::create; published source and generated meshes are immutable.
struct TerrainData {
    std::uint32_t samplesX=0,samplesZ=0,chunkCells=32;
    Vec2 spacing{1,1};
    float textureMeters=4;
    std::uint32_t lodLevels=3;
    std::array<float,3> lodPixels{240,120,60};
    std::shared_ptr<const PhysicalSurface> physicalSurface;
    std::vector<std::shared_ptr<const PhysicalSurface>> cellSurfaces; // Optional (samplesX-1)*(samplesZ-1), null inherits.
    std::vector<float> heights;
    std::shared_ptr<const Material> material=Material::standard();
    void validate() const;
    std::size_t derivedBytes() const; // Conservative generated chunk CPU payload estimate.
    std::size_t chunkDerivedBytes() const; // Conservative upper bound for one chunk.
};
struct TerrainGround { Vec3 position,normal; };
struct TerrainHit { Vec3 position,normal;float distance=0;ComponentId component; };
enum class TerrainBrushMode { Raise, Flatten, Smooth, Paint };
struct TerrainBrush {
    TerrainBrushMode mode=TerrainBrushMode::Raise;
    Vec2 center{}; // Source-local metres (X/Z).
    float radius=2,strength=1,targetHeight=0;
    std::uint32_t layer=0;
    void validate() const;
};
struct TerrainEdit;
// An interior chunk inside a small source tile with a normal/interpolation halo.
// Offset is in global sample indices; geometry/UVs stay in global source space.
struct TerrainPatch {
    std::uint32_t x=0,z=0,cellsX=0,cellsZ=0,offsetX=0,offsetZ=0,page=0;
    bool operator==(const TerrainPatch&) const=default;
};
class TerrainAsset : public std::enable_shared_from_this<TerrainAsset> {
public:
    static std::shared_ptr<const TerrainAsset> create(TerrainData data,bool buildChunks=true);
    static std::shared_ptr<const TerrainAsset> createPatch(TerrainData,TerrainPatch,bool build=true,
        std::shared_ptr<const Mesh> prepared={});
    const auto& patch() const { return patch_; }
    // Red-channel RGBA8 values map linearly to [minimum,maximum], without gamma decoding.
    static std::shared_ptr<const TerrainAsset> fromHeightmap(const Texture& image,float minimum,float maximum,
        Vec2 spacing={1,1},std::uint32_t chunkCells=32,float textureMeters=4,
        std::shared_ptr<const Material> material=Material::standard());
    const TerrainData& data() const { return data_; }
    const std::vector<std::shared_ptr<const Mesh>>& chunks() const { return chunks_; }
    std::uint32_t chunkCount() const;
    bool eager() const { return !chunks_.empty(); }
    // Source-only assets retain no generated meshes. The caller owns each result.
    std::shared_ptr<const Mesh> buildChunk(std::uint32_t chunk) const;
    TerrainEdit edit(const TerrainBrush&) const;
    TerrainEdit withPhysicalSurfaces(std::shared_ptr<const PhysicalSurface>,std::vector<std::shared_ptr<const PhysicalSurface>> cells={}) const;
    TerrainEdit withLayers(const MaterialLayers& layers) const; // Immutable appearance edit; shares all geometry.
    std::optional<TerrainGround> ground(float localX,float localZ) const;
    std::shared_ptr<const CollisionShape> collisionShape() const;
    std::shared_ptr<const CollisionShape> chunkCollision(std::uint32_t chunk) const;
private:
    friend class TerrainPagedStream;
    explicit TerrainAsset(TerrainData data,bool buildChunks);
    TerrainData data_;
    std::vector<std::shared_ptr<const Mesh>> chunks_;
    std::optional<TerrainPatch> patch_;
};
struct TerrainEdit {
    std::shared_ptr<const TerrainAsset> before,after;
    std::vector<std::uint32_t> affectedChunks; // Includes the normal-sampling halo.
    bool heightsChanged=false,paintChanged=false,surfacesChanged=false;
    bool changed() const { return before!=after; }
};
// One ordinary mesh draw per chunk; all chunks in an instance share one TerrainAsset.
// Published mesh replacement goes through applyTerrainEdit, retaining component IDs.
class TerrainComponent : public MeshComponent {
public:
    TerrainComponent(std::string name,std::shared_ptr<const TerrainAsset> asset,std::uint32_t chunk);
    std::string_view typeName() const noexcept override { return "TerrainComponent"; }
    const auto& asset() const { return asset_; }
    std::uint32_t chunk() const { return chunk_; }
    void validate() const override;
private:
    friend void applyTerrainEdit(Actor&,const TerrainEdit&);
    friend class TerrainStream;
    friend class TerrainPagedStream;
    void publish(std::shared_ptr<const TerrainAsset>,std::shared_ptr<const Mesh>,bool paint);
    std::shared_ptr<const TerrainAsset> asset_;
    std::shared_ptr<const Mesh> sourceMesh_;
    std::uint32_t chunk_;
};
// Builds a detached actor before publication. Collision is opt-in and uses full-resolution
// source triangles, independently of rendering. Adopt directly under Scene for physics.
std::unique_ptr<Actor> makeTerrainActor(std::string name,std::shared_ptr<const TerrainAsset> asset,bool collision=false);
// Call at an idle scene boundary; prepares geometry/collision before publication.
// A legacy whole-instance collider is rebuilt for height edits, never for painting.
void applyTerrainEdit(Actor&,const TerrainEdit&);
class Scene;
struct TerrainStreamSettings {
    std::uint32_t radiusChunks=1,maxResident=9,buildsPerUpdate=2;
    bool collision=false;
    Vec3 origin{}; // Translation only; each physical chunk is a scene-root actor.
    void validate() const;
};
struct TerrainStreamStats {
    std::size_t resident=0,pending=0,built=0,evicted=0,geometryBytes=0,lastRebuilt=0;
};
// Explicit synchronous derived-chunk streaming, not network/disk source paging.
// Scene must outlive this owner. Call update/apply/clear outside guarded traversal.
// CPU source stays resident; generated CPU/GPU resources are released on eviction.
class TerrainStream {
public:
    TerrainStream(Scene&,std::shared_ptr<const TerrainAsset>,TerrainStreamSettings={});
    ~TerrainStream();
    TerrainStream(const TerrainStream&)=delete;
    TerrainStream& operator=(const TerrainStream&)=delete;
    void update(Vec2 localFocus);
    void apply(const TerrainEdit&);
    void clear();
    const auto& asset() const { return asset_; }
    const auto& stats() const { return stats_; }
    const auto& settings() const { return settings_; }
private:
    Scene& scene_;
    std::shared_ptr<const TerrainAsset> asset_;
    TerrainStreamSettings settings_;
    std::vector<std::pair<std::uint32_t,ActorId>> resident_;
    TerrainStreamStats stats_;
    void measure();
};
// Closest two-sided triangle hit among active terrain chunks under root. Direction is
// normalized internally; distance is in world units. Queries never create physics.
std::optional<TerrainHit> terrainRaycast(const Node& root,Vec3 origin,Vec3 direction,float maxDistance);
// Highest world-space ground surface at X/Z, including rotated/scaled visual instances.
// Physics instances retain the existing rigid-body unit-scale/root-actor restrictions.
std::optional<TerrainHit> terrainGround(const Node& root,float worldX,float worldZ);
} // namespace engine
