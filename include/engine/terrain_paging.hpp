#pragma once
#include "engine/terrain.hpp"
#include "engine/assets.hpp"

namespace engine {
struct TerrainPageLayout {
    std::uint32_t samplesX=0,samplesZ=0,chunkCells=24;
    Vec2 spacing{1,1};float textureMeters=4;
    std::uint32_t lodLevels=3;std::array<float,3> lodPixels{240,120,60};
    void validate() const;
    std::uint32_t pageCount() const;
    TerrainPatch region(std::uint32_t page) const;
    std::pair<std::uint32_t,std::uint32_t> dimensions(std::uint32_t page) const;
    std::size_t maxPageBytes() const;
    bool operator==(const TerrainPageLayout&) const;
};
// Immutable dataset revision plus mutable per-page generation (zero for base data).
struct TerrainPage {
    TerrainPageLayout layout;std::uint32_t page=0;std::uint64_t revision=1,generation=0;
    std::vector<float> heights;std::vector<std::uint8_t> weights; // Optional sample-sized RGBA.
    std::shared_ptr<const PhysicalSurface> physicalSurface;
    std::vector<std::shared_ptr<const PhysicalSurface>> cellSurfaces;
    void validate() const;
};
std::vector<std::byte> encodeTerrainPage(const TerrainPage&);
TerrainPage decodeTerrainPage(std::span<const std::byte>);
struct TerrainPageResult {
    TerrainPageLayout layout;std::uint32_t page=0;std::uint64_t revision=0,generation=0;
    std::shared_ptr<const TerrainAsset> asset;
    std::shared_ptr<const CollisionShape> collision;
    std::size_t sourceBytes=0;
    bool generatedOffThread=false;
};
// Pure CPU work: no scene, GPU, Jolt world or platform objects.
std::shared_ptr<const TerrainPageResult> generateTerrainPage(const TerrainPage&,
    std::shared_ptr<const Material>,bool collision=false);
// Bounded worker transport: explicit LOD meshes, source patch and optional raw collider.
std::vector<std::byte> buildTerrainPageTransport(std::span<const std::byte> page,
    std::span<const std::byte> material,bool collision);
std::shared_ptr<const TerrainPageResult> hydrateTerrainPageTransport(std::span<const std::byte>,
    std::shared_ptr<const Material>,bool offThread=false);
struct TerrainPagingSettings {
    TerrainStreamSettings residency;
    std::uint32_t maxInFlight=2;
    std::size_t maxSourceBytes=4*1024*1024;
    void validate(const TerrainPageLayout&) const;
};
struct TerrainPagingStats {
    std::size_t resident=0,inFlight=0,queued=0,failed=0,sourceBytes=0,geometryBytes=0;
    std::size_t started=0,completed=0,cancelled=0,evicted=0,workerCompleted=0;
    std::string error;
    bool editing=false,editPending=false,writePending=false,writeFailed=false;
    std::size_t dirty=0,dirtyBytes=0,writing=0,writtenPages=0,writes=0;
    std::size_t edits=0,lastAffected=0,lastRebuilt=0;
    std::string editError,writeError;
};
using TerrainPageLoader=std::function<AssetLoader(std::uint32_t page)>;
struct TerrainPageWrite {
    std::uint64_t token=0;
    std::vector<TerrainPage> pages;
    std::vector<std::uint64_t> expectedGenerations;
    void validate() const;
};
struct TerrainPageWriteAck { std::uint64_t token=0; };
// The writer must compare generations and replace the entire batch atomically.
// Success acknowledges durable storage, not merely dispatch. Failure changes no pages.
using TerrainPageWriter=std::function<AssetLoader(std::shared_ptr<const TerrainPageWrite>)>;
std::vector<std::byte> encodeTerrainPageWrite(const TerrainPageWrite&);
TerrainPageWrite decodeTerrainPageWrite(std::span<const std::byte>);
struct TerrainPageEditingSettings {
    bool autoWriteback=true;
    std::size_t maxWriteBytes=8*1024*1024; // Batch staging cap, up to 64 MiB.
    void validate() const;
};
// Scene outlives this owner. update/clear/destruction require an idle boundary.
// Loader captures own their resources and must not capture scene pointers.
// Completion contexts remain safe after focus cancellation and owner destruction.
class TerrainPagedStream {
public:
    TerrainPagedStream(Scene&,TerrainPageLayout,std::uint64_t revision,TerrainPageLoader,TerrainPagingSettings={});
    ~TerrainPagedStream();
    TerrainPagedStream(const TerrainPagedStream&)=delete;
    TerrainPagedStream& operator=(const TerrainPagedStream&)=delete;
    void update(Vec2 localFocus); // Launch/publish budgets, never waits for IO/workers.
    void retryFailed();
    void clear(bool discardDirty=false); // Default rejects unsaved pages/pending writes.
    void enableEditing(TerrainPageWriter,TerrainPageEditingSettings={});
    bool edit(const TerrainBrush&); // Queues one atomic stamp; update loads required pages.
    void cancelEdit(); // Cancels only the not-yet-published stamp.
    void flush(); // Explicit write/retry; auto-write failures otherwise stop retries.
    bool hasUnsavedChanges() const;
    std::optional<TerrainGround> ground(float localX,float localZ) const; // Resident source only.
    const TerrainPagingStats& stats() const;
    const TerrainPageLayout& layout() const;
    const TerrainPagingSettings& settings() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    void applyEdit();
    void serviceWrites();
};
} // namespace engine
