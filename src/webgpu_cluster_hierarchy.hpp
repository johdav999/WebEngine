#pragma once

#include "engine/math.hpp"
#include <webgpu/webgpu_cpp.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace engine {

struct alignas(16) GpuHierarchyNode {
    std::array<float,4> minimum{},maximum{};
    std::uint32_t left=std::numeric_limits<std::uint32_t>::max();
    std::uint32_t right=std::numeric_limits<std::uint32_t>::max();
    std::uint32_t page=0,firstIndex=0,indexCount=0;
    float error=0;
    std::array<std::uint32_t,2> padding{};
};
static_assert(sizeof(GpuHierarchyNode)==64);

struct GpuHierarchyPlan {
    std::vector<GpuHierarchyNode> nodes; // Complete binary tree rooted at local node zero.
    std::vector<std::uint32_t> pageReady; // Zero or one; page zero is pinned and ready.
    Mat4 clip=Mat4::identity(),model=Mat4::identity();
    std::array<float,4> viewport{}; // Pixel x, y, width, height.
    float scale=1;
    bool frustumCulling=true;
};

class WebGpuClusterHierarchy {
public:
    static constexpr std::size_t maxPlans=64,maxNodes=511,maxPages=32705;
    static constexpr unsigned maxDepth=8;
    static constexpr std::uint32_t leaf=std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t statsWords=4;

    // Input buffers are immutable for this preparation. Preparing a later view
    // does not overwrite resources referenced by an earlier command encoder.
    bool prepare(const wgpu::Device& device,std::span<const GpuHierarchyPlan> plans) {
        plans_.clear();selection_=nullptr;feedback_=nullptr;feedbackBytes_=0;
        if(plans.empty()||plans.size()>maxPlans)return false;
        wgpu::Limits limits{};
        if(device.GetLimits(&limits)!=wgpu::Status::Success||limits.maxStorageBuffersPerShaderStage<4
            ||limits.maxUniformBuffersPerShaderStage<1||limits.maxComputeWorkgroupSizeX<1
            ||limits.maxComputeInvocationsPerWorkgroup<1)return false;
        std::size_t nodeWords=0,feedbackWords=0;
        for(const auto& plan:plans) {
            if(!validate(plan)||plan.nodes.size()*sizeof(GpuHierarchyNode)>limits.maxStorageBufferBindingSize
                ||plan.pageReady.size()*sizeof(std::uint32_t)>limits.maxStorageBufferBindingSize)return false;
            nodeWords+=plan.nodes.size();
            feedbackWords+=plan.pageReady.size()+statsWords;
        }
        const auto storageLimit=std::min(limits.maxBufferSize,limits.maxStorageBufferBindingSize);
        if(nodeWords*sizeof(std::uint32_t)>storageLimit||feedbackWords*sizeof(std::uint32_t)>storageLimit
            ||sizeof(Parameters)>limits.maxUniformBufferBindingSize)return false;
        if(!pipeline_)createPipeline(device);
        auto selection=emptyBuffer(device,nodeWords*sizeof(std::uint32_t),wgpu::BufferUsage::Storage,"GPU LOD selection flags");
        auto feedback=emptyBuffer(device,feedbackWords*sizeof(std::uint32_t),
            wgpu::BufferUsage::Storage|wgpu::BufferUsage::CopySrc,"GPU LOD page demand");
        if(!selection||!feedback)return false;

        std::vector<PlanBuffers> prepared;
        prepared.reserve(plans.size());
        std::uint32_t selectionOffset=0,requestOffset=0;
        for(const auto& plan:plans) {
            const auto nodeCount=static_cast<std::uint32_t>(plan.nodes.size());
            const auto pageCount=static_cast<std::uint32_t>(plan.pageReady.size());
            const auto statsOffset=requestOffset+pageCount;
            const Parameters values{plan.clip,plan.model,plan.viewport,
                {nodeCount,pageCount,selectionOffset,requestOffset},
                plan.scale,plan.frustumCulling?1u:0u,statsOffset};
            auto parameters=initializedBuffer(device,&values,sizeof(values),wgpu::BufferUsage::Uniform,"GPU LOD parameters");
            auto nodes=initializedBuffer(device,plan.nodes.data(),plan.nodes.size()*sizeof(GpuHierarchyNode),
                wgpu::BufferUsage::Storage,"GPU LOD hierarchy");
            auto ready=initializedBuffer(device,plan.pageReady.data(),plan.pageReady.size()*sizeof(std::uint32_t),
                wgpu::BufferUsage::Storage,"GPU LOD page readiness");
            if(!parameters||!nodes||!ready)return false;
            prepared.push_back({std::move(parameters),std::move(nodes),std::move(ready),
                selectionOffset,requestOffset,statsOffset});
            selectionOffset+=nodeCount;
            requestOffset=statsOffset+statsWords;
        }
        plans_=std::move(prepared);selection_=std::move(selection);feedback_=std::move(feedback);
        feedbackBytes_=feedbackWords*sizeof(std::uint32_t);
        return true;
    }

    void encode(const wgpu::Device& device,const wgpu::CommandEncoder& encoder) const {
        if(plans_.empty())return;
        wgpu::ComputePassDescriptor description{};
        description.label="Select GPU cluster LOD and request pages";
        auto pass=encoder.BeginComputePass(&description);
        pass.SetPipeline(pipeline_);
        for(const auto& plan:plans_) {
            const std::array buffers{plan.parameters,plan.nodes,plan.ready,selection_,feedback_};
            std::array<wgpu::BindGroupEntry,5> entries{};
            for(unsigned i=0;i<entries.size();++i) {
                entries[i].binding=i;entries[i].buffer=buffers[i];entries[i].size=buffers[i].GetSize();
            }
            wgpu::BindGroupDescriptor bindings{};
            bindings.layout=layout_;bindings.entryCount=entries.size();bindings.entries=entries.data();
            pass.SetBindGroup(0,device.CreateBindGroup(&bindings));
            pass.DispatchWorkgroups(1);
        }
        pass.End();
    }

    const wgpu::Buffer& selection() const { return selection_; }
    const wgpu::Buffer& feedback() const { return feedback_; }
    std::uint64_t feedbackBytes() const { return feedbackBytes_; }
    std::uint32_t selectionOffset(std::size_t plan) const { return plans_.at(plan).selectionOffset; }
    std::uint32_t requestWordOffset(std::size_t plan) const { return plans_.at(plan).requestOffset; }
    std::uint32_t statsWordOffset(std::size_t plan) const { return plans_.at(plan).statsOffset; }
    void reset() { *this=WebGpuClusterHierarchy{}; }

private:
    struct alignas(16) Parameters {
        Mat4 clip,model;
        std::array<float,4> viewport;
        std::array<std::uint32_t,4> counts; // Nodes, pages, selection base, request base.
        float scale;
        std::uint32_t frustum,statsOffset,padding=0;
    };
    static_assert(sizeof(Parameters)==176);
    struct PlanBuffers {
        wgpu::Buffer parameters,nodes,ready;
        std::uint32_t selectionOffset,requestOffset,statsOffset;
    };
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
    wgpu::Buffer selection_,feedback_;
    std::vector<PlanBuffers> plans_;
    std::uint64_t feedbackBytes_=0;

    template<std::size_t N>
    static bool finite(const std::array<float,N>& values) {
        return std::all_of(values.begin(),values.end(),[](float value){return std::isfinite(value);});
    }
    static bool validate(const GpuHierarchyPlan& plan) {
        if(plan.nodes.empty()||plan.nodes.size()>maxNodes||plan.pageReady.empty()||plan.pageReady.size()>maxPages
            ||plan.pageReady.front()!=1||!finite(plan.clip.m)||!finite(plan.model.m)||!finite(plan.viewport)
            ||!(plan.scale>0)||!std::isfinite(plan.scale)||plan.viewport[0]<0||plan.viewport[1]<0
            ||plan.viewport[2]<=0||plan.viewport[3]<=0||plan.nodes.front().page!=0)return false;
        for(auto ready:plan.pageReady)if(ready>1)return false;
        for(const auto& node:plan.nodes) {
            if(!finite(node.minimum)||!finite(node.maximum)||!std::isfinite(node.error)||node.error<0
                ||node.page>=plan.pageReady.size()||!node.indexCount||node.indexCount%3||node.firstIndex%3
                ||std::uint64_t(node.firstIndex)+node.indexCount>std::numeric_limits<std::uint32_t>::max()
                ||(node.left==leaf)!=(node.right==leaf))return false;
            for(unsigned axis=0;axis<3;++axis)if(node.minimum[axis]>node.maximum[axis])return false;
        }
        std::vector<bool> seen(plan.nodes.size());
        const auto visit=[&](auto&& self,std::uint32_t id,unsigned depth)->bool {
            if(id>=plan.nodes.size()||seen[id]||depth>maxDepth)return false;
            seen[id]=true;
            const auto& node=plan.nodes[id];
            if(node.left==leaf)return true;
            for(auto child:{node.left,node.right}) {
                if(!self(self,child,depth+1))return false;
                const auto& next=plan.nodes[child];
                if(next.error>node.error)return false;
                for(unsigned axis=0;axis<3;++axis)
                    if(next.minimum[axis]<node.minimum[axis]||next.maximum[axis]>node.maximum[axis])return false;
            }
            return true;
        };
        return visit(visit,0,0)&&std::find(seen.begin(),seen.end(),false)==seen.end();
    }
    static wgpu::Buffer emptyBuffer(const wgpu::Device& device,std::uint64_t bytes,wgpu::BufferUsage usage,const char* label) {
        wgpu::BufferDescriptor description{};
        description.label=label;description.size=bytes;description.usage=usage;
        return device.CreateBuffer(&description);
    }
    static wgpu::Buffer initializedBuffer(const wgpu::Device& device,const void* data,std::size_t bytes,
                                          wgpu::BufferUsage usage,const char* label) {
        wgpu::BufferDescriptor description{};
        description.label=label;description.size=bytes;description.usage=usage;description.mappedAtCreation=true;
        auto buffer=device.CreateBuffer(&description);
        if(!buffer)return {};
        auto* target=buffer.GetMappedRange(0,bytes);
        if(!target)return {};
        std::memcpy(target,data,bytes);
        buffer.Unmap();
        return buffer;
    }
    void createPipeline(const wgpu::Device& device) {
        std::array<wgpu::BindGroupLayoutEntry,5> entries{};
        for(unsigned i=0;i<entries.size();++i) {
            entries[i].binding=i;entries[i].visibility=wgpu::ShaderStage::Compute;
            entries[i].buffer.type=i==0?wgpu::BufferBindingType::Uniform:
                i<3?wgpu::BufferBindingType::ReadOnlyStorage:wgpu::BufferBindingType::Storage;
        }
        wgpu::BindGroupLayoutDescriptor bindings{};
        bindings.label="GPU cluster hierarchy layout";bindings.entryCount=entries.size();bindings.entries=entries.data();
        layout_=device.CreateBindGroupLayout(&bindings);
        wgpu::PipelineLayoutDescriptor layout{};
        layout.bindGroupLayoutCount=1;layout.bindGroupLayouts=&layout_;
        wgpu::ShaderSourceWGSL code{};code.code=shader;
        wgpu::ShaderModuleDescriptor module{};module.nextInChain=&code;
        wgpu::ComputePipelineDescriptor pipeline{};
        pipeline.label="GPU cluster hierarchy traversal";pipeline.layout=device.CreatePipelineLayout(&layout);
        pipeline.compute.module=device.CreateShaderModule(&module);pipeline.compute.entryPoint="traverse";
        pipeline_=device.CreateComputePipeline(&pipeline);
    }

    static constexpr const char* shader=R"WGSL(
struct Node {
    minimum: vec4f,
    maximum: vec4f,
    left: u32,
    right: u32,
    page: u32,
    first_index: u32,
    index_count: u32,
    error: f32,
    padding: vec2u,
}
struct Parameters {
    clip: mat4x4f,
    model: mat4x4f,
    viewport: vec4f,
    counts: vec4u,
    scale: f32,
    frustum: u32,
    stats_offset: u32,
    padding: u32,
}
@group(0) @binding(0) var<uniform> params: Parameters;
@group(0) @binding(1) var<storage, read> nodes: array<Node>;
@group(0) @binding(2) var<storage, read> page_ready: array<u32>;
@group(0) @binding(3) var<storage, read_write> selection: array<u32>;
@group(0) @binding(4) var<storage, read_write> feedback: array<u32>;

fn magnitude(matrix: mat4x4f, point: vec4f) -> vec4f {
    return abs(matrix[0]) * point.x + abs(matrix[1]) * point.y + abs(matrix[2]) * point.z + abs(matrix[3]) * point.w;
}
fn corner(node: Node, index: u32) -> vec4f {
    let bits = vec3u(index) & vec3u(1u, 2u, 4u);
    return vec4f(select(node.minimum.xyz, node.maximum.xyz, bits != vec3u(0u)), 1.0);
}
fn visible(node: Node) -> bool {
    if (params.frustum == 0u) { return true; }
    var outside = vec4<bool>(true);
    var outside_z = vec2<bool>(true);
    for (var i = 0u; i < 8u; i++) {
        let point = corner(node, i);
        let q = params.clip * (params.model * point);
        if (!all(abs(q) < vec4f(1e30))) { return true; }
        let terms = magnitude(params.clip, magnitude(params.model, abs(point)));
        let tolerance = 1e-5 * max(1.0, max(max(terms.x, terms.y), max(terms.z, terms.w)));
        let planes = vec4f(q.w + q.x, q.w - q.x, q.w + q.y, q.w - q.y);
        outside = select(vec4<bool>(false), outside, planes < vec4f(-tolerance));
        outside_z = select(vec2<bool>(false), outside_z, vec2f(q.z, q.w - q.z) < vec2f(-tolerance));
    }
    return !any(outside) && !any(outside_z);
}
fn row_length(matrix: mat4x4f, row: u32) -> f32 {
    let terms = abs(vec3f(matrix[0][row], matrix[1][row], matrix[2][row]));
    let scale = max(terms.x, max(terms.y, terms.z));
    if (scale == 0.0) { return 0.0; }
    return scale * length(terms / scale);
}
fn projected_error(node: Node) -> f32 {
    let matrix = params.clip * params.model;
    let dw = row_length(matrix, 3u) * node.error;
    let dz = row_length(matrix, 2u) * node.error;
    var min_w = 1e30;
    var max_xy = vec2f(0.0);
    for (var i = 0u; i < 8u; i++) {
        let point = corner(node, i);
        let q = params.clip * (params.model * point);
        let terms = magnitude(params.clip, magnitude(params.model, abs(point)));
        let tolerance = 1e-6 * max(1.0, max(max(terms.x, terms.y), max(terms.z, terms.w)));
        if (!all(abs(q) < vec4f(1e30)) || !(q.w > dw + tolerance + 1e-8) || !(q.z > dz + tolerance)) {
            return 1e30;
        }
        min_w = min(min_w, q.w - tolerance);
        max_xy = max(max_xy, (abs(q.xy) + vec2f(tolerance)) / (q.w - tolerance));
    }
    let differential = vec2f(row_length(matrix, 0u), row_length(matrix, 1u)) * node.error;
    let pixels = (differential + max_xy * dw) * params.viewport.zw;
    let result = 0.5 * max(pixels.x, pixels.y) / (min_w - dw) * params.scale;
    if (!(result >= 0.0 && result < 1e30)) { return 1e30; }
    return result;
}
fn request(page: u32, error: f32) {
    if (page == 0u) { return; }
    // Quantized monotonic finite priority. Clamp before integer conversion.
    let priority = u32(clamp(ceil(error * 256.0), 1.0, 16777215.0));
    let index = params.counts.w + page;
    feedback[index] = max(feedback[index], priority);
}

@compute @workgroup_size(1)
fn traverse() {
    for (var i = 0u; i < params.counts.x; i++) { selection[params.counts.z + i] = 0u; }
    for (var i = 0u; i < params.counts.y; i++) { feedback[params.counts.w + i] = 0u; }
    var stack: array<u32, 9>;
    stack[0] = 0u;
    var count = 1u;
    var visited = 0u;
    var selected = 0u;
    var fallback = 0u;
    loop {
        if (count == 0u) { break; }
        count--;
        let index = stack[count];
        let node = nodes[index];
        visited++;
        if (!visible(node)) { continue; }
        if (node.left != 0xffffffffu) {
            let error = projected_error(node);
            if (error > 1.0) {
                let left_page = nodes[node.left].page;
                let right_page = nodes[node.right].page;
                request(left_page, error);
                request(right_page, error);
                if (page_ready[left_page] != 0u && page_ready[right_page] != 0u) {
                    stack[count] = node.right;
                    stack[count + 1u] = node.left;
                    count += 2u;
                    continue;
                }
                fallback++;
            }
        }
        selection[params.counts.z + index] = 1u;
        selected++;
        request(node.page, 1.0);
    }
    var requested = 0u;
    for (var i = 1u; i < params.counts.y; i++) {
        requested += select(0u, 1u, feedback[params.counts.w + i] != 0u);
    }
    feedback[params.stats_offset] = visited;
    feedback[params.stats_offset + 1u] = selected;
    feedback[params.stats_offset + 2u] = requested;
    feedback[params.stats_offset + 3u] = fallback;
}
)WGSL";
};
} // namespace engine
