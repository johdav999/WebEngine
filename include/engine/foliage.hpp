#pragma once
#include "engine/scene.hpp"
#include "engine/terrain.hpp"
#include "engine/mesh_instances.hpp"
#include <compare>
#include <functional>
#include <span>

namespace engine {
struct FoliageCell {
    std::int32_t x=0,z=0;
    auto operator<=>(const FoliageCell&) const=default;
};
// Stable source identity, independent of cell residency, scene handles and render LOD.
struct FoliageId {
    std::uint32_t species=0;
    std::int32_t x=0,z=0; // Global candidate lattice coordinates.
    auto operator<=>(const FoliageId&) const=default;
};
struct FoliageSurface {
    float height=0;
    Vec3 normal{0,1,0};
    std::uint32_t mask=0xffffffffu;
};
// Synchronous source-space X/Z query. Null means missing surface or a hole.
// Captures are owned by the callable; it must not depend on presentation geometry.
using FoliageSampler=std::function<std::optional<FoliageSurface>(Vec2)>;
FoliageSampler foliageTerrainSampler(std::shared_ptr<const TerrainAsset>,Vec3 origin={},std::uint32_t mask=0xffffffffu);
struct FoliageExclusion { Vec2 minimum{},maximum{}; };
struct FoliageDetail {
    std::shared_ptr<const Mesh> mesh;
    float distance=0; // First level starts at zero; subsequent levels increase.
    // Optional authored root-to-tip weights, one per vertex, in [0,1]. Empty is rigid.
    std::vector<float> bend;
};
struct FoliageSpecies {
    std::uint32_t id=1;
    std::string name="Plant";
    std::vector<FoliageDetail> details;
    float density=.1f,minimumSpacing=1; // Per m^2, spacing within this species.
    float minimumHeight=-100000,maximumHeight=100000,maximumSlope=pi/3;
    float minimumScale=.8f,maximumScale=1.2f;
    std::uint32_t surfaceMask=0xffffffffu;
    bool alignToGround=false,castsShadow=true;
    float drawDistance=80,shadowDistance=30;
    Vec3 tint{1,1,1};
    // Optional simple trunk collision. Dimensions are scaled into immutable shapes;
    // actors retain unit scale. Grass defaults to no collision.
    float trunkRadius=0,trunkHeight=0;
    void validate() const;
};
struct FoliageDefinition {
    std::uint64_t world=1,seed=1; // Game-owned content identity and generation seed.
    float cellSize=16;
    std::vector<FoliageSpecies> species;
    std::vector<FoliageExclusion> exclusions;
    std::uint32_t maxCandidatesPerCell=8192,maxInstancesPerCell=4096,maxRemoved=65536;
    void validate() const;
};
class FoliageCatalog {
public:
    static std::shared_ptr<const FoliageCatalog> create(FoliageDefinition);
    const FoliageDefinition& data() const { return data_; }
private:
    explicit FoliageCatalog(FoliageDefinition);
    FoliageDefinition data_;
};
struct FoliagePlacement {
    FoliageId id;
    Transform transform;
    float phase=0;
};
// Pure bounded generation; publication order and neighbouring residency are irrelevant.
std::vector<FoliagePlacement> generateFoliageCell(const FoliageCatalog&,FoliageCell,const FoliageSampler&);
using FoliageWind=MeshWindSettings;
enum class FoliageRendering { GpuInstances, CpuMeshes };
// Ordinary mesh deformation consumed by every existing color/shadow/bounds path.
// CPU wind is explicit and never advanced by collection or visibility.
class FoliageMeshComponent final : public MeshComponent {
public:
    FoliageMeshComponent(std::string,const FoliageDetail&,float phase);
    std::string_view typeName() const noexcept override { return "FoliageMeshComponent"; }
    std::shared_ptr<const MorphPose> morphPose() const override;
private:
    friend class FoliageStream;
    std::shared_ptr<const Mesh> source_;
    std::vector<float> bend_;
    float phase_=0;
    std::shared_ptr<MorphPose> pose_;
    std::shared_ptr<MorphPose> prepareWind(double,const FoliageWind&) const;
};
struct FoliageStreamSettings {
    std::uint32_t visualRadius=1,collisionRadius=0,hysteresisCells=1;
    std::uint32_t maxVisualCells=32,maxCollisionCells=16,buildsPerUpdate=4;
    float detailHysteresis=.1f;
    bool collision=false;
    FoliageRendering rendering=FoliageRendering::GpuInstances;
    void validate() const;
};
struct FoliageStreamStats {
    std::size_t visualCells=0,collisionCells=0,instances=0,colliders=0,pending=0,built=0,evicted=0;
    std::size_t deformedVertices=0;
    std::size_t bulkComponents=0,gpuInstances=0;
};
// Application-owned portable projection. Methods require an idle hierarchy and an
// identity Scene transform. Scene owns generated transient root actors; handles are
// generation checked. Destructor releases resources only: explicitly clear before
// discarding a stream if its scene will continue to live.
class FoliageStream {
public:
    FoliageStream(Scene&,std::shared_ptr<const FoliageCatalog>,FoliageSampler,FoliageStreamSettings={});
    ~FoliageStream();
    FoliageStream(const FoliageStream&)=delete;
    FoliageStream& operator=(const FoliageStream&)=delete;
    // Independent application-selected visual/gameplay focus. Bounded synchronous
    // preparation; failures leave published cells and metadata unchanged.
    void update(Vec2 visualFocus,Vec2 collisionFocus);
    // Presentation selection and explicit simulation-time wind. Same timestamp gives
    // the same deformation; pause/suspension simply retain the supplied timestamp.
    void present(Vec2 visualFocus,double simulationSeconds,const FoliageWind& = {});
    void replaceSource(FoliageSampler); // Regenerate all resident cells before publishing.
    void setRemoved(std::vector<FoliageId>); // Atomic, sorted, unique; never changes catalog.
    void remove(FoliageId);
    std::span<const FoliageId> removed() const;
    std::string saveRemoved() const; // Bounded WEFO 1 text; storage remains host-owned.
    void loadRemoved(std::string_view);
    std::optional<FoliageId> placement(ComponentId,std::uint64_t instanceKey=0) const;
    void clear(); // Also clears presentation history; retains persistent removals/source.
    const FoliageStreamStats& stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void rebuild(FoliageSampler,std::vector<FoliageId>);
};
} // namespace engine
