#pragma once
#include "engine/scene.hpp"
#include <functional>
#include <span>

namespace engine {
// Instance attributes occupy 176 bytes in the existing reusable 256-byte slot.
struct alignas(16) ObjectUniform { Mat4 model,normal; std::array<float,4> color,shadow,wind; };
static_assert(sizeof(ObjectUniform)==176);
struct ObjectUploadRange { std::size_t offset=0,bytes=0; };
// A mesh override selects a cluster range while retaining the original object slot.
struct InstanceBatch {
    std::uint32_t firstInstance=0,instanceCount=1,firstIndex=0,indexCount=0;
    std::shared_ptr<const Mesh> mesh{};
    MeshBounds clusterBounds{};
    bool hierarchyRoot=false;
    std::uint32_t selectionIndex=~std::uint32_t(0);
};
using ClusterPageUpload=std::function<bool(const std::shared_ptr<const Mesh>&,bool pinned)>;
struct DetailView { Mat4 viewProjection; float width=1,height=1; };
struct ShadowBatch {
    InstanceBatch instances;
    std::shared_ptr<const Mesh> mesh;
    std::uint32_t firstIndex=0,indexCount=0;
};
class RenderFrame {
public:
    static constexpr std::size_t objectStride=256;
    // One collect/prepare cycle per frame; upload objectUploads() before next prepare.
    void collect(const Scene& scene,bool instancing=true,std::uint32_t visibilityMask=0xffffffffu,const Frustum* frustum=nullptr,
                 const DetailView* detail=nullptr,const ClusterPageUpload& upload={},bool gpuHierarchy=false,bool cullUnused=false);
    // Independent per-light selection; object slots are shared with camera draws.
    void prepareShadow(std::size_t light,const DetailView& detail,bool lod=true,bool culling=true,bool casting=true,
                       const ClusterPageUpload& upload={});
    std::span<const ShadowBatch> shadowBatches(std::size_t light) const { return shadows_.at(light).batches; }
    std::size_t cameraLodParts() const { return cameraLodParts_; }
    std::size_t shadowLodParts() const { return shadowLodParts_; }
    std::size_t shadowCulledParts() const { return shadowCulledParts_; }
    void resetShadowDetailHistory() { for(auto& shadow:shadows_) for(auto& state:shadow.levels) state={}; }
    void prepareObjects();
    const SceneFrame& sceneFrame() const { return scene_; }
    const MeshDraw& instanceDraw(std::size_t slot) const { return scene_.meshes[instanceSources_.at(slot)]; }
    MeshDraw batchDraw(const InstanceBatch& batch,std::uint32_t offset=0) const;
    std::uint32_t instanceSource(std::size_t slot) const { return instanceSources_.at(slot); }
    std::span<const InstanceBatch> batches(RenderLayer layer) const { return batches_.at(static_cast<std::size_t>(layer)); }
    // Camera-visible batches; batches() retains off-screen geometry for shadow passes.
    std::span<const InstanceBatch> cameraBatches(RenderLayer layer) const { return cameraBatches_.at(static_cast<std::size_t>(layer)); }
    std::span<const InstanceBatch> shadowBatches() const { return shadowBatches_; }
    std::span<const std::byte> objectData() const { return objects_; }
    std::span<const ObjectUploadRange> objectUploads() const { return uploads_; }
    std::size_t changedObjects() const { return changedObjects_; } // Includes forced GPU initialization.
    void invalidateObjects() { forceUpload_=true; } // GPU buffer recreated: all active slots must upload.
    std::size_t storageGrowths() const { return storageGrowths_; }
    std::size_t storageBytes() const;
private:
    SceneFrame scene_;
    struct LodState { ComponentId component;std::weak_ptr<const Mesh> source;std::size_t level=0;std::uint64_t instanceKey=0; };
    struct ShadowState { std::vector<LodState> levels;std::vector<ShadowBatch> batches; };
    std::vector<LodState> cameraLevels_;
    std::array<ShadowState,4> shadows_;
    std::size_t cameraLodParts_=0,shadowLodParts_=0,shadowCulledParts_=0;
    std::size_t chooseLod(const MeshDraw& draw,LodState& state,const DetailView* detail,bool shadow=false);
    std::vector<std::uint32_t> instanceSources_;
    std::array<std::vector<InstanceBatch>,static_cast<std::size_t>(RenderLayer::Count)> batches_;
    std::array<std::vector<InstanceBatch>,static_cast<std::size_t>(RenderLayer::Count)> cameraBatches_;
    std::vector<InstanceBatch> shadowBatches_;
    std::vector<std::byte> objects_;
    std::vector<ObjectUploadRange> uploads_;
    std::size_t storageGrowths_=0;
    std::size_t changedObjects_=0;
    bool forceUpload_=true;
};
} // namespace engine
