#pragma once
#include "webgpu_cluster_hierarchy.hpp"
#include "webgpu_cluster_batches.hpp"
#include "engine/cluster_lod.hpp"
#include "engine/cluster_stream.hpp"
#include "engine/render_frame.hpp"
#include <algorithm>
#include <functional>
#include <numeric>
#include <tuple>

namespace engine {
// Structural enumeration only. Projected-error and residency decisions are made
// by the GPU. Local node IDs keep each material part's traversal independently bounded.
inline bool buildGpuHierarchyPlan(const ClusterLod& source,std::size_t part,GpuHierarchyPlan& plan) {
    plan.nodes.clear();
    if(part>=source.roots().size())return false;
    std::vector<std::uint32_t> visited;
    const auto copy=[&](auto&& self,std::uint32_t id,unsigned depth)->std::uint32_t {
        if(id>=source.groups().size()||depth>8||plan.nodes.size()>=511
            ||std::find(visited.begin(),visited.end(),id)!=visited.end())
            throw std::invalid_argument("Invalid GPU hierarchy part");
        visited.push_back(id);
        const auto& group=source.groups()[id];
        const auto local=std::uint32_t(plan.nodes.size());
        const auto& lo=group.bounds.minimum;const auto& hi=group.bounds.maximum;
        GpuHierarchyNode node{};
        node.minimum={lo.x,lo.y,lo.z,0};node.maximum={hi.x,hi.y,hi.z,0};
        node.left=node.right=~std::uint32_t(0);
        node.page=group.range.page;node.firstIndex=group.range.firstIndex;
        node.indexCount=group.range.indexCount;node.error=group.error;
        plan.nodes.push_back(node);
        if(!group.leaf()) {
            const auto left=self(self,group.children[0],depth+1);
            const auto right=self(self,group.children[1],depth+1);
            plan.nodes[local].left=left;plan.nodes[local].right=right;
        }
        return local;
    };
    try { copy(copy,source.roots()[part],0); }
    catch(const std::invalid_argument&) { plan.nodes.clear();return false; }
    return true;
}

struct GpuHierarchyFeedbackSource {
    std::weak_ptr<ClusterPageStream> stream;
    std::weak_ptr<const Mesh> sourceMesh;
    ComponentId component;
    std::uint64_t generation=0;
    std::uint32_t pageCount=0,requestOffset=0,statsOffset=0;
};
struct GpuHierarchyExpansion {
    std::vector<InstanceBatch> batches;
    std::vector<GpuHierarchyPlan> plans;
    std::vector<GpuHierarchyFeedbackSource> sources;
};

inline bool expandGpuHierarchies(std::span<const InstanceBatch> input,const RenderFrame& frame,
    const DetailView& view,bool frustumCulling,const ClusterPageUpload& upload,
    const std::function<wgpu::Buffer(const std::shared_ptr<const Mesh>&)>& indices,GpuHierarchyExpansion& output) {
    std::size_t totalNodes=0;
    for(const auto& batch:input) {
        if(!batch.hierarchyRoot) { output.batches.push_back(batch);continue; }
        if(output.plans.size()>=WebGpuClusterHierarchy::maxPlans)return false;
        const auto& draw=frame.instanceDraw(batch.firstInstance);
        const auto& source=*draw.sourceMesh;
        const auto stream=source.clusterStream;
        const auto& hierarchy=stream?stream->asset().hierarchy():*source.clusterLod;
        GpuHierarchyPlan plan;
        plan.clip=view.viewProjection;plan.model=draw.model;
        plan.viewport={0,0,view.width,view.height};plan.scale=draw.lodPolicy.cameraScale;
        plan.frustumCulling=frustumCulling;
        if(!buildGpuHierarchyPlan(hierarchy,draw.part,plan))return false;
        if(totalNodes+plan.nodes.size()>WebGpuClusterBatches::maxHierarchyCandidates)return false;
        totalNodes+=plan.nodes.size();
        const auto pageCount=stream?stream->asset().pages().size():1;
        std::vector<std::shared_ptr<const Mesh>> pages(pageCount);
        plan.pageReady.resize(pageCount);
        for(std::uint32_t page=0;page<pageCount;++page) {
            const auto& mesh=stream?stream->mesh(page):hierarchy.geometry();
            if(!mesh||!upload(mesh,page==0))continue;
            if(!indices(mesh))continue;
            pages[page]=mesh;plan.pageReady[page]=1;
        }
        if(!plan.pageReady[0])return false;
        GpuHierarchyFeedbackSource feedback;
        feedback.stream=stream;feedback.generation=stream?stream->generation():0;
        feedback.sourceMesh=draw.sourceMesh;feedback.component=draw.component;
        feedback.pageCount=std::uint32_t(pageCount);
        std::vector<std::uint32_t> order(plan.nodes.size());
        std::iota(order.begin(),order.end(),0u);
        std::sort(order.begin(),order.end(),[&](auto a,auto b){
            const auto& x=plan.nodes[a];const auto& y=plan.nodes[b];
            return std::tie(x.page,x.firstIndex)<std::tie(y.page,y.firstIndex);
        });
        for(auto nodeIndex:order) {
            const auto& node=plan.nodes[nodeIndex];
            if(!pages[node.page])continue;
            auto expanded=batch;
            expanded.mesh=pages[node.page];expanded.firstIndex=node.firstIndex;expanded.indexCount=node.indexCount;
            expanded.clusterBounds={{node.minimum[0],node.minimum[1],node.minimum[2]},
                {node.maximum[0],node.maximum[1],node.maximum[2]},true};
            expanded.selectionIndex=std::uint32_t(totalNodes-plan.nodes.size()+nodeIndex);
            expanded.hierarchyRoot=false;
            output.batches.push_back(std::move(expanded));
        }
        output.plans.push_back(std::move(plan));
        output.sources.push_back(std::move(feedback));
    }
    return !output.plans.empty();
}
} // namespace engine
