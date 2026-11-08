#pragma once

#include "engine/math.hpp"
#include <webgpu/webgpu_cpp.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace engine {

struct alignas(16) GpuClusterCandidate {
    std::array<float,4> minimum{},maximum{};
    std::uint32_t firstIndex=0,indexCount=0;
    std::uint32_t selectionIndex=~std::uint32_t(0),padding=0;
};
static_assert(sizeof(GpuClusterCandidate)==48);

struct GpuClusterBin {
    wgpu::Buffer sourceIndices; // Read-only storage with unpacked uint32 indices.
    Mat4 clip;
    std::array<float,4> viewport{}; // Pixel x, y, width, height.
    std::vector<GpuClusterCandidate> candidates;
    Mat4 model=Mat4::identity(); // Supply separately from the view projection to match rasterization.
};

class WebGpuClusterBatches {
public:
    static constexpr std::size_t maxBins=64,maxCandidates=128,maxIndexBytes=8*1024*1024;
    static constexpr std::size_t maxHierarchyCandidates=4096,maxBinCandidates=512;
    static constexpr std::uint64_t argumentStride=5*sizeof(std::uint32_t);

    // Each preparation has distinct GPU buffers so a subsequent view can be
    // prepared while the previous view's commands are still awaiting submission.
    bool prepare(const wgpu::Device& device,std::span<const GpuClusterBin> bins,const wgpu::Buffer& selection={}) {
        bins_.clear();
        indices_=nullptr;
        arguments_=nullptr;
        if(bins.empty()||bins.size()>maxBins)return false;
        wgpu::Limits limits{};
        if(device.GetLimits(&limits)!=wgpu::Status::Success||limits.maxStorageBuffersPerShaderStage<5
            ||limits.maxComputeInvocationsPerWorkgroup<64||limits.maxComputeWorkgroupSizeX<64
            ||limits.maxComputeWorkgroupStorageSize<maxBinCandidates*2*sizeof(std::uint32_t))return false;
        const auto maxRanges=selection?maxHierarchyCandidates:maxCandidates;
        std::size_t candidates=0;
        std::uint64_t indexBytes=0;
        for(const auto& bin:bins) {
            if(!bin.sourceIndices||!(bin.sourceIndices.GetUsage()&wgpu::BufferUsage::Storage)
                ||bin.sourceIndices.GetSize()%sizeof(std::uint32_t)
                ||bin.sourceIndices.GetSize()>limits.maxStorageBufferBindingSize
                ||bin.candidates.empty()||bin.candidates.size()>maxBinCandidates||bin.candidates.size()>maxRanges-candidates
                ||!finite(bin.clip.m)||!finite(bin.model.m)||!finite(bin.viewport)
                ||bin.viewport[0]<0||bin.viewport[1]<0||bin.viewport[2]<=0||bin.viewport[3]<=0
                ||bin.viewport[0]+bin.viewport[2]>limits.maxTextureDimension2D
                ||bin.viewport[1]+bin.viewport[3]>limits.maxTextureDimension2D)return false;
            candidates+=bin.candidates.size();
            for(const auto& range:bin.candidates) {
                const auto end=std::uint64_t(range.firstIndex)+range.indexCount;
                if(!range.indexCount||range.indexCount%3||end>bin.sourceIndices.GetSize()/sizeof(std::uint32_t)
                    ||!finite(range.minimum)||!finite(range.maximum))return false;
                if(range.selectionIndex!=~std::uint32_t(0)&&(!selection
                    ||std::uint64_t(range.selectionIndex)*4+4>selection.GetSize()))return false;
                for(unsigned axis=0;axis<3;++axis)if(range.minimum[axis]>range.maximum[axis])return false;
                indexBytes+=std::uint64_t(range.indexCount)*sizeof(std::uint32_t);
                if(indexBytes>maxIndexBytes)return false;
            }
        }
        const auto argumentsBytes=bins.size()*argumentStride;
        const auto maxBuffer=std::min(limits.maxBufferSize,limits.maxStorageBufferBindingSize);
        if(indexBytes>maxBuffer||argumentsBytes>maxBuffer
            ||sizeof(BinHeader)+candidates*sizeof(GpuClusterCandidate)>maxBuffer)return false;
        if(!pipeline_)createPipeline(device);

        wgpu::BufferDescriptor output{};
        output.label="Compacted cluster indices";output.size=indexBytes;
        output.usage=wgpu::BufferUsage::Storage|wgpu::BufferUsage::Index;
        auto indices=device.CreateBuffer(&output);
        output.label="Cluster indirect draw arguments";output.size=argumentsBytes;
        output.usage=wgpu::BufferUsage::Storage|wgpu::BufferUsage::Indirect|wgpu::BufferUsage::CopySrc;
        auto arguments=device.CreateBuffer(&output);
        if(!indices||!arguments)return false;

        std::vector<BinBuffers> resources;
        resources.reserve(bins.size());
        std::uint32_t first=0;
        for(const auto& bin:bins) {
            const BinHeader header{bin.clip,bin.model,bin.viewport,static_cast<std::uint32_t>(bin.candidates.size()),
                first,static_cast<std::uint32_t>(resources.size())};
            const auto dataSize=bin.candidates.size()*sizeof(GpuClusterCandidate);
            wgpu::BufferDescriptor input{};
            input.label="Cluster cull candidates";input.size=sizeof(header)+dataSize;
            input.usage=wgpu::BufferUsage::Storage;input.mappedAtCreation=true;
            auto ranges=device.CreateBuffer(&input);
            if(!ranges)return false;
            auto* bytes=static_cast<std::byte*>(ranges.GetMappedRange(0,input.size));
            if(!bytes)return false;
            std::memcpy(bytes,&header,sizeof(header));
            std::memcpy(bytes+sizeof(header),bin.candidates.data(),dataSize);
            ranges.Unmap();
            resources.push_back({bin.sourceIndices,ranges});
            for(const auto& range:bin.candidates)first+=range.indexCount;
        }
        indices_=std::move(indices);
        arguments_=std::move(arguments);
        bins_=std::move(resources);
        if(selection)selection_=selection;
        else {
            wgpu::BufferDescriptor dummy{};
            dummy.size=4;dummy.usage=wgpu::BufferUsage::Storage;
            selection_=device.CreateBuffer(&dummy);
        }
        return true;
    }

    // The pyramid stores maximum depth, with the current viewport at its
    // original attachment coordinates in level zero.
    void encode(const wgpu::Device& device,const wgpu::CommandEncoder& encoder,const wgpu::TextureView& pyramid) const {
        if(bins_.empty())return;
        wgpu::ComputePassDescriptor compute{};
        compute.label="Cull and compact cluster ranges";
        auto pass=encoder.BeginComputePass(&compute);
        pass.SetPipeline(pipeline_);
        for(const auto& bin:bins_) {
            std::array<wgpu::BindGroupEntry,6> entries{};
            const std::array buffers{bin.ranges,bin.source,indices_,arguments_};
            for(unsigned i=0;i<buffers.size();++i) {
                entries[i].binding=i;entries[i].buffer=buffers[i];entries[i].size=buffers[i].GetSize();
            }
            entries[4].binding=4;entries[4].textureView=pyramid;
            entries[5].binding=5;entries[5].buffer=selection_;entries[5].size=selection_.GetSize();
            wgpu::BindGroupDescriptor desc{};
            desc.layout=layout_;desc.entryCount=entries.size();desc.entries=entries.data();
            pass.SetBindGroup(0,device.CreateBindGroup(&desc));
            pass.DispatchWorkgroups(1);
        }
        pass.End();
    }

    const wgpu::Buffer& indices() const { return indices_; }
    const wgpu::Buffer& arguments() const { return arguments_; }
    static std::uint64_t argumentOffset(std::size_t bin) { return bin*argumentStride; }
    void reset() { *this=WebGpuClusterBatches{}; }

private:
    struct alignas(16) BinHeader {
        Mat4 clip,model;
        std::array<float,4> viewport;
        std::uint32_t count,firstIndex,argumentBin,padding=0;
    };
    static_assert(sizeof(BinHeader)==160);
    struct BinBuffers { wgpu::Buffer source,ranges; };
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
    wgpu::Buffer indices_,arguments_,selection_;
    std::vector<BinBuffers> bins_;

    template<std::size_t N>
    static bool finite(const std::array<float,N>& values) {
        return std::all_of(values.begin(),values.end(),[](float value){return std::isfinite(value);});
    }

    void createPipeline(const wgpu::Device& device) {
        std::array<wgpu::BindGroupLayoutEntry,6> entries{};
        for(unsigned i=0;i<entries.size();++i) {
            entries[i].binding=i;
            entries[i].visibility=wgpu::ShaderStage::Compute;
        }
        entries[0].buffer.type=wgpu::BufferBindingType::ReadOnlyStorage;
        entries[1].buffer.type=wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].buffer.type=wgpu::BufferBindingType::Storage;
        entries[3].buffer.type=wgpu::BufferBindingType::Storage;
        entries[4].texture.sampleType=wgpu::TextureSampleType::UnfilterableFloat;
        entries[4].texture.viewDimension=wgpu::TextureViewDimension::e2D;
        entries[5].buffer.type=wgpu::BufferBindingType::ReadOnlyStorage;
        wgpu::BindGroupLayoutDescriptor bindings{};
        bindings.label="Cluster cull layout";bindings.entryCount=entries.size();bindings.entries=entries.data();
        layout_=device.CreateBindGroupLayout(&bindings);
        wgpu::PipelineLayoutDescriptor layout{};
        layout.bindGroupLayoutCount=1;layout.bindGroupLayouts=&layout_;
        wgpu::ShaderSourceWGSL source{};source.code=shader;
        wgpu::ShaderModuleDescriptor module{};module.nextInChain=&source;
        wgpu::ComputePipelineDescriptor pipeline{};
        pipeline.label="Cluster cull and compact";pipeline.layout=device.CreatePipelineLayout(&layout);
        pipeline.compute.module=device.CreateShaderModule(&module);pipeline.compute.entryPoint="compact";
        pipeline_=device.CreateComputePipeline(&pipeline);
    }

    static constexpr const char* shader=R"WGSL(
struct Range {
    minimum: vec4f,
    maximum: vec4f,
    first_index: u32,
    index_count: u32,
    selection_index: u32,
    padding: u32,
}
struct Bin {
    clip: mat4x4f,
    model: mat4x4f,
    viewport: vec4f,
    count: u32,
    first_index: u32,
    argument_bin: u32,
    padding: u32,
    ranges: array<Range>,
}
struct Arguments {
    index_count: u32,
    instance_count: u32,
    first_index: u32,
    base_vertex: i32,
    first_instance: u32,
}
@group(0) @binding(0) var<storage, read> bin: Bin;
@group(0) @binding(1) var<storage, read> source_indices: array<u32>;
@group(0) @binding(2) var<storage, read_write> indices: array<u32>;
@group(0) @binding(3) var<storage, read_write> arguments: array<Arguments>;
@group(0) @binding(4) var pyramid: texture_2d<f32>;
@group(0) @binding(5) var<storage, read> selected_nodes: array<u32>;
var<workgroup> visible_counts: array<u32, 512>;
var<workgroup> offsets: array<u32, 512>;

fn magnitude(matrix: mat4x4f, point: vec4f) -> vec4f {
    return abs(matrix[0]) * point.x + abs(matrix[1]) * point.y + abs(matrix[2]) * point.z + abs(matrix[3]) * point.w;
}

fn visible(range: Range) -> bool {
    var outside = vec4<bool>(true);
    var outside_z = vec2<bool>(true);
    var screen_min = vec2f(1e20);
    var screen_max = vec2f(-1e20);
    var nearest = 1.0;
    var projectable = true;
    for (var corner = 0u; corner < 8u; corner++) {
        let bits = vec3u(corner) & vec3u(1u, 2u, 4u);
        let point = vec4f(select(range.minimum.xyz, range.maximum.xyz, bits != vec3u(0u)), 1.0);
        let clip = bin.clip * (bin.model * point);
        // Overflow or a nonfinite transform must keep the candidate.
        if (!all(abs(clip) < vec4f(1e20))) { return true; }
        let error_scale = magnitude(bin.clip, magnitude(bin.model, abs(point)));
        let tolerance = 1e-5 * max(1.0, max(max(error_scale.x, error_scale.y), max(error_scale.z, error_scale.w)));
        let planes = vec4f(clip.w + clip.x, clip.w - clip.x, clip.w + clip.y, clip.w - clip.y);
        outside = select(vec4<bool>(false), outside, planes < vec4f(-tolerance));
        outside_z = select(vec2<bool>(false), outside_z, vec2f(clip.z, clip.w - clip.z) < vec2f(-tolerance));
        if (clip.w <= 8.0 * tolerance || clip.z <= tolerance) {
            projectable = false;
        } else {
            let ndc = clip.xyz / clip.w;
            let screen = bin.viewport.xy + (vec2f(0.5) + vec2f(0.5, -0.5) * ndc.xy) * bin.viewport.zw;
            let screen_margin = (vec2f(1.0) + abs(ndc.xy)) * (0.5 * tolerance / (clip.w - tolerance)) * bin.viewport.zw;
            screen_min = min(screen_min, screen - screen_margin);
            screen_max = max(screen_max, screen + screen_margin);
            nearest = min(nearest, (clip.z - tolerance) / (clip.w + tolerance));
        }
    }
    if (any(outside) || any(outside_z)) { return false; }
    if (!projectable) { return true; }

    // Expand around the projected AABB before rounding. Texels beyond the
    // attachment's real edge are padded at far depth by the pyramid producer.
    let base_max = vec2f(textureDimensions(pyramid, 0) - vec2u(1u));
    let lo = vec2u(clamp(floor(screen_min - vec2f(1.0)), vec2f(0.0), base_max));
    let hi = vec2u(clamp(ceil(screen_max + vec2f(1.0)), vec2f(0.0), base_max));
    let width = hi - lo + vec2u(1u);
    let span = max(width.x, width.y);
    let mip = min(32u - countLeadingZeros(span - 1u), textureNumLevels(pyramid) - 1u);
    let a = vec2i(lo >> vec2u(mip));
    let b = vec2i(hi >> vec2u(mip));
    let farthest = max(
        max(textureLoad(pyramid, a, i32(mip)).r, textureLoad(pyramid, vec2i(a.x, b.y), i32(mip)).r),
        max(textureLoad(pyramid, vec2i(b.x, a.y), i32(mip)).r, textureLoad(pyramid, b, i32(mip)).r));
    return !(nearest > farthest + 1e-5);
}

@compute @workgroup_size(64)
fn compact(@builtin(local_invocation_index) lane: u32) {
    for (var i = lane; i < bin.count; i += 64u) {
        let range = bin.ranges[i];
        var selected = true;
        if (range.selection_index != 0xffffffffu) {
            selected = selected_nodes[range.selection_index] != 0u;
        }
        visible_counts[i] = 0u;
        if (selected && visible(range)) { visible_counts[i] = range.index_count; }
    }
    workgroupBarrier();
    if (lane == 0u) {
        var count = 0u;
        for (var i = 0u; i < bin.count; i++) {
            offsets[i] = bin.first_index + count;
            count += visible_counts[i];
        }
        arguments[bin.argument_bin] = Arguments(count, 1u, bin.first_index, 0, 0u);
    }
    workgroupBarrier();
    // Copy disjoint slices in candidate order to preserve equal-depth ordering.
    for (var i = 0u; i < bin.count; i++) {
        let first = bin.ranges[i].first_index;
        for (var j = lane; j < visible_counts[i]; j += 64u) {
            indices[offsets[i] + j] = source_indices[first + j];
        }
    }
}
)WGSL";
};
} // namespace engine
