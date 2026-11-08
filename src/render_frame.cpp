#include "engine/render_frame.hpp"
#include "engine/mesh_detail.hpp"
#include "engine/camera_component.hpp"
#include "engine/cluster_stream.hpp"
#include <cstring>
#include <limits>
#include <algorithm>
#include <numeric>
#include <tuple>

namespace engine {
namespace {
constexpr std::size_t extraClusterDraws=128; // Per camera or shadow map.
std::size_t sourceIndexCount(const MeshDraw& draw) {
    const auto& mesh=*draw.sourceMesh;
    return mesh.parts.empty()?mesh.indices.size():mesh.parts[draw.part].indexCount;
}
bool useClusters(const MeshDraw& draw,const DetailView* detail,bool shadow) {
    return detail&&draw.sourceBounds.valid&&(draw.sourceMesh->clusterLod||draw.sourceMesh->clusterStream)
        &&!draw.skinPose&&!draw.morphPose&&draw.layer==RenderLayer::World
        &&draw.blend==BlendMode::Opaque&&draw.material.alphaMode==AlphaMode::Opaque
        &&draw.material.shader!=MaterialShader::Water&&draw.material.advanced.windAmplitude==0
        &&(shadow?draw.lodPolicy.shadowLevel:draw.lodPolicy.cameraLevel)<0;
}
bool selectClusters(const MeshDraw& draw,const DetailView& detail,float scale,const Frustum* frustum,
                    std::span<ClusterRange> output,std::size_t& count,const ClusterPageUpload& upload) {
    const auto& source=*draw.sourceMesh;
    if(!source.clusterStream)return source.clusterLod->select(draw.part,draw.model,detail,scale,frustum,output,count);
    auto& stream=*source.clusterStream;
    const auto& hierarchy=stream.asset().hierarchy();
    if(draw.part>=hierarchy.roots().size()||!upload||!upload(stream.mesh(0),true))return false;
    const auto ready=[&](std::uint32_t page,double priority) {
        stream.demand(page,priority);
        const auto& mesh=stream.mesh(page);
        return mesh&&upload(mesh,false);
    };
    if(hierarchy.select(draw.part,draw.model,detail,scale,frustum,output,count,ready))return true;
    if(output.empty())return false;
    const auto& root=hierarchy.groups()[hierarchy.roots()[draw.part]];
    output.front()=root.range;
    output.front().bounds=root.bounds;
    count=1;
    return true;
}
const std::shared_ptr<const Mesh>& clusterMesh(const MeshDraw& draw,ClusterRange range) {
    const auto& source=*draw.sourceMesh;
    return source.clusterStream?source.clusterStream->mesh(range.page):source.clusterLod->geometry();
}
void useLod(MeshDraw& draw,std::size_t level) {
    if(!level) return;
    const auto& source=*draw.sourceMesh;
    const auto mesh=source.lods.at(level-1).mesh;
    if(!mesh || mesh->parts.size()!=source.parts.size() || mesh->skeleton || !mesh->morphTargets.empty())
        throw std::invalid_argument("Invalid static LOD geometry");
    if(mesh->parts.empty()) { draw.firstIndex=0;draw.indexCount=std::uint32_t(mesh->indices.size()); }
    else {
        const auto& part=mesh->parts.at(draw.part);
        if(part.materialIndex!=source.parts.at(draw.part).materialIndex) throw std::invalid_argument("LOD material slots differ");
        draw.firstIndex=part.firstIndex;draw.indexCount=part.indexCount;
    }
    draw.mesh=mesh;
}
}
MeshDraw RenderFrame::batchDraw(const InstanceBatch& batch,std::uint32_t offset) const {
    auto draw=instanceDraw(batch.firstInstance+offset);
    if(batch.indexCount) {
        draw.mesh=batch.mesh;
        draw.firstIndex=batch.firstIndex;
        draw.indexCount=batch.indexCount;
    }
    return draw;
}
std::size_t RenderFrame::chooseLod(const MeshDraw& draw,LodState& state,const DetailView* detail,bool shadow) {
    if(state.component!=draw.component || state.source.lock()!=draw.sourceMesh||state.instanceKey!=draw.instanceKey) state={draw.component,draw.sourceMesh,0,draw.instanceKey};
    state.level=detail && !draw.bend && !draw.skinPose && !draw.morphPose && !draw.sourceMesh->skeleton
        && draw.sourceMesh->skin.empty() && draw.sourceMesh->morphTargets.empty() && !draw.sourceMesh->lods.empty()
        ? selectMeshLod(*draw.sourceMesh,projectedMeshPixels(draw.sourceBounds,draw.model,detail->viewProjection,detail->width,detail->height)*(shadow ? draw.lodPolicy.shadowScale : draw.lodPolicy.cameraScale),state.level) : 0;
    const int fixed=shadow ? draw.lodPolicy.shadowLevel : draw.lodPolicy.cameraLevel;
    if(detail&&fixed>=0&&draw.sourceBounds.valid&&!draw.bend&&!draw.skinPose&&!draw.morphPose&&!draw.sourceMesh->skeleton&&draw.sourceMesh->skin.empty()&&draw.sourceMesh->morphTargets.empty())
        state.level=std::min(std::size_t(fixed),draw.sourceMesh->lods.size());
    return state.level;
}
void RenderFrame::collect(const Scene& scene,bool instancing,std::uint32_t visibilityMask,const Frustum* frustum,
                          const DetailView* detail,const ClusterPageUpload& upload,bool gpuHierarchy) {
    for(auto& batches:batches_) batches.clear();
    for(auto& batches:cameraBatches_) batches.clear();
    shadowBatches_.clear();
    for(auto& shadow:shadows_) shadow.batches.clear();
    scene.collect(scene_,visibilityMask,frustum);storageGrowths_=scene_.storageGrowths;
    cameraLodParts_=shadowLodParts_=shadowCulledParts_=0;
    const auto levelsCapacity=cameraLevels_.capacity();cameraLevels_.resize(scene_.meshes.size());
    storageGrowths_+=levelsCapacity!=cameraLevels_.capacity();
    for(std::size_t i=0;i<scene_.meshes.size();++i) {
        auto& draw=scene_.meshes[i];
        if(!draw.sourceMesh) draw.sourceMesh=draw.mesh;
        const auto level=chooseLod(draw,cameraLevels_[i],detail);
        cameraLodParts_+=bool(level)&&draw.cameraVisible;
        useLod(draw,level);
    }
    const auto* white=Texture::white().get();
    const auto sourceCapacity=instanceSources_.capacity();
    instanceSources_.resize(scene_.meshes.size());
    storageGrowths_+=sourceCapacity!=instanceSources_.capacity();
    std::iota(instanceSources_.begin(),instanceSources_.end(),0u);
    // Sort only contiguous, explicitly reorderable opaque runs within one layer.
    // Original SceneFrame indices (including transparent FX slots) stay intact.
    auto key=[&](std::uint32_t index) {
        const auto& d=scene_.meshes[index];const auto& m=d.material;
        auto address=[](const auto* p) { return reinterpret_cast<std::uintptr_t>(p); };
        return std::tuple{address(d.mesh.get()),d.firstIndex,d.indexCount,address(d.skinPose.get()),
            address(d.morphPose.get()),address(d.bend.get()),address(d.wind.get()),d.mirrored,address(d.texture ? d.texture.get() : white),
            address(m.baseColorTexture ? m.baseColorTexture.get() : white),address(m.baseColorSurface.get()),address(m.metallicRoughnessTexture.get()),
            address(m.normalTexture.get()),address(m.occlusionTexture.get()),address(m.emissiveTexture.get()),
            m.roughness,m.metallic,m.normalScale,m.occlusionStrength,m.emissive.x,m.emissive.y,m.emissive.z,
            m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y,m.alphaMode,m.alphaCutoff,m.doubleSided,m.shader};
    };
    if(instancing) for(std::size_t begin=0;begin<instanceSources_.size();) {
        const auto& first=scene_.meshes[begin];
        if(!first.allowInstanceReordering || first.blend!=BlendMode::Opaque) { ++begin;continue; }
        auto end=begin+1;
        while(end<instanceSources_.size() && scene_.meshes[end].allowInstanceReordering
            && scene_.meshes[end].blend==BlendMode::Opaque && scene_.meshes[end].layer==first.layer) ++end;
        std::sort(instanceSources_.begin()+begin,instanceSources_.begin()+end,[&](auto a,auto b) {
            const auto ka=key(a),kb=key(b);return ka==kb ? a<b : ka<kb;
        });
        begin=end;
    }
    for(std::size_t layer=0;layer<batches_.size();++layer) {
        auto& batches=batches_[layer];
        for(std::uint32_t i=0;i<instanceSources_.size();++i) {
            const auto& draw=instanceDraw(i);
            if(static_cast<std::size_t>(draw.layer)!=layer) continue;
            if(draw.blend!=BlendMode::Opaque) continue; // Submitted by the camera-sorted FX queue.
            if(instancing && !batches.empty()) {
                auto& batch=batches.back();const auto& first=instanceDraw(batch.firstInstance);
                // Contiguous slots allow firstInstance to address unchanged object staging directly.
                // Order-sensitive components retain their original precedence.
                if(i==std::uint64_t(batch.firstInstance)+batch.instanceCount && draw.mesh==first.mesh
                    && draw.skinPose==first.skinPose
                    && draw.morphPose==first.morphPose
                    && draw.bend==first.bend && draw.wind==first.wind
                    && draw.mirrored==first.mirrored && sameMaterialBinding(draw.material,first.material)
                    && draw.firstIndex==first.firstIndex && draw.indexCount==first.indexCount
                    && (draw.texture ? draw.texture.get() : white)==(first.texture ? first.texture.get() : white)) {
                    ++batch.instanceCount;continue;
                }
            }
            const auto capacity=batches.capacity();batches.push_back({i,1});
            storageGrowths_+=capacity!=batches.capacity();
        }
    }
    // Split at invisible slots or cluster ranges without repacking object data.
    std::size_t extra=extraClusterDraws;
    std::array<ClusterRange,ClusterLod::maxRanges> ranges;
    for(std::size_t layer=0;layer<batches_.size();++layer) {
        auto& visible=cameraBatches_[layer];
        const auto append=[&](InstanceBatch batch) {
            const auto capacity=visible.capacity();visible.push_back(batch);
            storageGrowths_+=capacity!=visible.capacity();
        };
        for(const auto& batch:batches_[layer]) {
            bool inRun=false;
            for(std::uint32_t i=0;i<batch.instanceCount;++i) {
                const auto slot=batch.firstInstance+i;
                const auto& draw=instanceDraw(slot);
                if(!draw.cameraVisible) { inRun=false;continue; }
                if(gpuHierarchy&&useClusters(draw,detail,false)) {
                    const auto& source=*draw.sourceMesh;
                    const auto& hierarchy=source.clusterStream?source.clusterStream->asset().hierarchy():*source.clusterLod;
                    const auto& root=hierarchy.groups()[hierarchy.roots()[draw.part]];
                    const auto& geometry=source.clusterStream?source.clusterStream->mesh(0):hierarchy.geometry();
                    // GPU mode submits the pinned root and immutable tree metadata,
                    // without running the CPU camera selector or issuing page demand.
                    if(upload&&upload(geometry,true))
                        append({slot,1,root.range.firstIndex,root.range.indexCount,geometry,root.bounds,true});
                    else append({slot,1});
                    inRun=false;
                    continue;
                }
                std::size_t count=0;
                // Cluster expansion can also break an otherwise instanced draw.
                if(useClusters(draw,detail,false)&&selectClusters(draw,*detail,draw.lodPolicy.cameraScale,
                    frustum,std::span(ranges).first(std::min(extra,ranges.size())),count,upload)) {
                    extra-=count;
                    std::size_t indices=0;
                    for(std::size_t j=0;j<count;++j) {
                        indices+=ranges[j].indexCount;
                        append({slot,1,ranges[j].firstIndex,ranges[j].indexCount,clusterMesh(draw,ranges[j]),ranges[j].bounds});
                    }
                    cameraLodParts_-=bool(cameraLevels_[instanceSource(slot)].level);
                    cameraLodParts_+=count&&indices<sourceIndexCount(draw);
                    inRun=false;
                    continue;
                }
                if(inRun) ++visible.back().instanceCount;
                else {
                    append({slot,1});inRun=true;
                }
            }
        }
    }
    // Casters keep off-camera slots, splitting only where cast participation differs.
    for(const auto& batch:batches_[static_cast<std::size_t>(RenderLayer::World)]) {
        bool inRun=false;
        for(std::uint32_t i=0;i<batch.instanceCount;++i) {
            const auto slot=batch.firstInstance+i;
            if(!instanceDraw(slot).castsShadow) { inRun=false;continue; }
            if(inRun) ++shadowBatches_.back().instanceCount;
            else {
                const auto capacity=shadowBatches_.capacity();shadowBatches_.push_back({slot,1});
                storageGrowths_+=capacity!=shadowBatches_.capacity();inRun=true;
            }
        }
    }
}
void RenderFrame::prepareShadow(std::size_t light,const DetailView& detail,bool lod,bool culling,bool casting,
                                const ClusterPageUpload& upload) {
    auto& state=shadows_.at(light);state.batches.clear();
    const auto capacity=state.levels.capacity();state.levels.resize(scene_.meshes.size());
    storageGrowths_+=capacity!=state.levels.capacity();
    if(!casting) return;
    const auto frustum=Frustum::fromMatrix(detail.viewProjection);
    std::size_t extra=extraClusterDraws;
    std::array<ClusterRange,ClusterLod::maxRanges> ranges;
    const auto append=[&](ShadowBatch batch) {
        const auto batchCapacity=state.batches.capacity();
        state.batches.push_back(std::move(batch));
        storageGrowths_+=batchCapacity!=state.batches.capacity();
    };
    for(const auto& batch:shadowBatches_) {
        bool inRun=false;
        for(std::uint32_t i=0;i<batch.instanceCount;++i) {
            const auto slot=batch.firstInstance+i;const auto& original=instanceDraw(slot);
            const auto& bounds=original.sourceBounds;
            if(culling && bounds.valid && !frustum.intersectsTransformedAabb(bounds.minimum,bounds.maximum,original.model)) {
                ++shadowCulledParts_;inRun=false;continue;
            }
            std::size_t count=0;
            if(useClusters(original,lod?&detail:nullptr,true)&&selectClusters(original,detail,original.lodPolicy.shadowScale,
                culling?&frustum:nullptr,std::span(ranges).first(std::min(extra,ranges.size())),count,upload)) {
                extra-=count;
                std::size_t indices=0;
                for(std::size_t j=0;j<count;++j) {
                    indices+=ranges[j].indexCount;
                    append({{slot,1},clusterMesh(original,ranges[j]),ranges[j].firstIndex,ranges[j].indexCount});
                }
                shadowLodParts_+=count&&indices<sourceIndexCount(original);
                inRun=false;
                continue;
            }
            auto draw=original;draw.mesh=draw.sourceMesh;
            if(draw.mesh->parts.empty()) { draw.firstIndex=0;draw.indexCount=std::uint32_t(draw.mesh->indices.size()); }
            else { const auto& part=draw.mesh->parts.at(draw.part);draw.firstIndex=part.firstIndex;draw.indexCount=part.indexCount; }
            const auto level=chooseLod(draw,state.levels[instanceSource(slot)],lod ? &detail : nullptr,true);
            shadowLodParts_+=bool(level);useLod(draw,level);
            if(inRun && state.batches.back().mesh==draw.mesh && state.batches.back().firstIndex==draw.firstIndex && state.batches.back().indexCount==draw.indexCount)
                ++state.batches.back().instances.instanceCount;
            else {
                append({{slot,1},draw.mesh,draw.firstIndex,draw.indexCount});inRun=true;
            }
        }
    }
}
std::size_t RenderFrame::storageBytes() const {
    auto bytes=scene_.storageBytes()+objects_.capacity()+uploads_.capacity()*sizeof(ObjectUploadRange)
        +instanceSources_.capacity()*sizeof(std::uint32_t);
    for(const auto& batches:batches_) bytes+=batches.capacity()*sizeof(InstanceBatch);
    for(const auto& batches:cameraBatches_) bytes+=batches.capacity()*sizeof(InstanceBatch);
    bytes+=shadowBatches_.capacity()*sizeof(InstanceBatch);
    bytes+=cameraLevels_.capacity()*sizeof(LodState);
    for(const auto& state:shadows_) bytes+=state.levels.capacity()*sizeof(LodState)+state.batches.capacity()*sizeof(ShadowBatch);
    return bytes;
}
void RenderFrame::prepareObjects() {
    const auto count=scene_.meshes.size();
    if(count>std::numeric_limits<std::size_t>::max()/objectStride
        || (count && count-1>std::numeric_limits<std::uint32_t>::max()/objectStride))
        throw std::length_error("Instance data offsets exceed the address space");
    const auto capacity=objects_.capacity();
    const auto previousCount=objects_.size()/objectStride;
    objects_.resize(count*objectStride); // Newly exposed bytes, including padding, are zeroed.
    storageGrowths_+=objects_.capacity()!=capacity;
    uploads_.clear();changedObjects_=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& draw=instanceDraw(i);
        const ObjectUniform object{draw.model,draw.normal,{draw.color.x,draw.color.y,draw.color.z,draw.opacity},{draw.receivesShadow ? 1.0f : 0.0f,draw.layer==RenderLayer::World&&((draw.material.shader==MaterialShader::Water&&draw.material.water.usesSceneInputs())||draw.material.advanced.transmission>0) ? 1.0f : 0.0f,draw.layer==RenderLayer::World&&draw.material.shader==MaterialShader::Water ? 1.0f : 0.0f,draw.layer==RenderLayer::World ? (draw.blend==BlendMode::Additive ? 2.0f : 1.0f) : 0.0f},{draw.windPhase,0,0,0}};
        const auto offset=i*objectStride;
        const bool changed=i>=previousCount || std::memcmp(objects_.data()+offset,&object,sizeof(object));
        if(changed) std::memcpy(objects_.data()+offset,&object,sizeof(object));
        if(changed || forceUpload_) {
            ++changedObjects_;
            // Merge adjacent dirty slots, including their initialized alignment padding.
            if(!uploads_.empty() && uploads_.back().offset+uploads_.back().bytes+objectStride-sizeof(ObjectUniform)==offset)
                uploads_.back().bytes=offset+sizeof(ObjectUniform)-uploads_.back().offset;
            else {
                const auto rangeCapacity=uploads_.capacity();uploads_.push_back({offset,sizeof(ObjectUniform)});
                storageGrowths_+=rangeCapacity!=uploads_.capacity();
            }
        }
    }
    forceUpload_=false;
}
} // namespace engine
