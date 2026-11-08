#pragma once
#include <webgpu/webgpu_cpp.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <utility>

namespace engine {
// A current-frame max-depth pyramid for conventional [0, 1] depth. Mip zero
// has the depth attachment's pixel coordinates and is padded to a power of two
// on each axis. Padding is far depth so it cannot produce false occlusion.
class WebGpuClusterDepth {
public:
    bool build(const wgpu::Device& device,const wgpu::CommandEncoder& encoder,
               const wgpu::TextureView& depth,std::uint32_t width,std::uint32_t height) {
        if(!device||!encoder||!depth||!width||!height||width>maxAxis||height>maxAxis)return false;
        const auto paddedWidth=std::bit_ceil(width);
        const auto paddedHeight=std::bit_ceil(height);
        if(std::uint64_t(paddedWidth)*paddedHeight>maxBaseTexels)return false;
        if(!prepareKernels(device)||!prepareTexture(device,paddedWidth,paddedHeight))return false;

        const auto copy=binding(device,copy_.layout,depth,mips_[0]);
        if(!copy)return false;
        dispatch(encoder,copy_.pipeline,copy,width_,height_,"Cluster depth copy");
        for(std::uint32_t mip=1;mip<levels_;++mip) {
            // Each pass accesses a different pair of nonoverlapping mips.
            // A separate pass also orders the write before its first read.
            dispatch(encoder,reduce_.pipeline,reductions_[mip-1],
                     std::max(1u,width_>>mip),std::max(1u,height_>>mip),"Cluster depth max reduction");
        }
        return true;
    }

    // Valid for the current encoder after a successful build. The device owner
    // must reset on recovery. Previous commands retain their backing resources.
    wgpu::TextureView view() const { return view_; }
    void reset() { *this={}; }

private:
    static constexpr std::uint32_t maxAxis=4096;
    static constexpr std::uint64_t maxBaseTexels=8*1024*1024;
    static constexpr auto maxLevels=std::bit_width(maxAxis);
    static constexpr const char* copyShader=R"WGSL(
@group(0) @binding(0) var sourceDepth : texture_depth_2d;
@group(0) @binding(1) var destination : texture_storage_2d<r32float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id : vec3u) {
    if (any(id.xy >= textureDimensions(destination))) {
        return;
    }
    var value = 1.0;
    if (all(id.xy < textureDimensions(sourceDepth, 0))) {
        value = textureLoad(sourceDepth, vec2i(id.xy), 0);
    }
    textureStore(destination, vec2i(id.xy), vec4f(value, 0.0, 0.0, 0.0));
}
)WGSL";
    static constexpr const char* reduceShader=R"WGSL(
@group(0) @binding(0) var sourceDepth : texture_2d<f32>;
@group(0) @binding(1) var destination : texture_storage_2d<r32float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id : vec3u) {
    if (any(id.xy >= textureDimensions(destination))) {
        return;
    }
    let last = textureDimensions(sourceDepth, 0) - vec2u(1u);
    let a = min(id.xy * 2u, last);
    let b = min(a + vec2u(1u), last);
    let d00 = textureLoad(sourceDepth, vec2i(a), 0).x;
    let d01 = textureLoad(sourceDepth, vec2i(vec2u(a.x, b.y)), 0).x;
    let d10 = textureLoad(sourceDepth, vec2i(vec2u(b.x, a.y)), 0).x;
    let d11 = textureLoad(sourceDepth, vec2i(b), 0).x;
    textureStore(destination, vec2i(id.xy), vec4f(max(max(d00, d01), max(d10, d11)), 0.0, 0.0, 0.0));
}
)WGSL";

    struct Kernel {
        wgpu::BindGroupLayout layout;
        wgpu::ComputePipeline pipeline;
    };
    Kernel copy_,reduce_;
    wgpu::Texture texture_;
    wgpu::TextureView view_;
    std::array<wgpu::TextureView,maxLevels> mips_;
    std::array<wgpu::BindGroup,maxLevels-1> reductions_;
    std::uint32_t width_=0,height_=0,levels_=0;

    static Kernel kernel(const wgpu::Device& device,const char* code,wgpu::TextureSampleType sampleType,const char* label) {
        std::array<wgpu::BindGroupLayoutEntry,2> entries{};
        entries[0].binding=0;
        entries[0].visibility=wgpu::ShaderStage::Compute;
        entries[0].texture.sampleType=sampleType;
        entries[0].texture.viewDimension=wgpu::TextureViewDimension::e2D;
        entries[1].binding=1;
        entries[1].visibility=wgpu::ShaderStage::Compute;
        entries[1].storageTexture.access=wgpu::StorageTextureAccess::WriteOnly;
        entries[1].storageTexture.format=wgpu::TextureFormat::R32Float;
        entries[1].storageTexture.viewDimension=wgpu::TextureViewDimension::e2D;
        wgpu::BindGroupLayoutDescriptor bindingDesc{};
        bindingDesc.entryCount=entries.size();
        bindingDesc.entries=entries.data();
        auto layout=device.CreateBindGroupLayout(&bindingDesc);
        if(!layout)return {};
        wgpu::PipelineLayoutDescriptor pipelineLayout{};
        pipelineLayout.bindGroupLayoutCount=1;
        pipelineLayout.bindGroupLayouts=&layout;
        wgpu::ShaderSourceWGSL source{};
        source.code=code;
        wgpu::ShaderModuleDescriptor module{};
        module.nextInChain=&source;
        wgpu::ComputePipelineDescriptor pipeline{};
        pipeline.label=label;
        pipeline.layout=device.CreatePipelineLayout(&pipelineLayout);
        pipeline.compute.module=device.CreateShaderModule(&module);
        pipeline.compute.entryPoint="main";
        if(!pipeline.layout||!pipeline.compute.module)return {};
        return {layout,device.CreateComputePipeline(&pipeline)};
    }

    bool prepareKernels(const wgpu::Device& device) {
        if(copy_.pipeline&&reduce_.pipeline)return true;
        auto copy=kernel(device,copyShader,wgpu::TextureSampleType::Depth,"Cluster depth copy");
        auto reduce=kernel(device,reduceShader,wgpu::TextureSampleType::UnfilterableFloat,"Cluster depth max reduction");
        if(!copy.pipeline||!reduce.pipeline)return false;
        copy_=std::move(copy);
        reduce_=std::move(reduce);
        return true;
    }

    bool prepareTexture(const wgpu::Device& device,std::uint32_t width,std::uint32_t height) {
        if(texture_&&width==width_&&height==height_)return true;
        wgpu::Limits limits{};
        if(device.GetLimits(&limits)!=wgpu::Status::Success||width>limits.maxTextureDimension2D
           ||height>limits.maxTextureDimension2D)return false;
        const auto levels=std::uint32_t(std::bit_width(std::max(width,height)));
        wgpu::TextureDescriptor desc{};
        desc.label="Cluster hierarchical depth";
        desc.size={width,height,1};
        desc.format=wgpu::TextureFormat::R32Float;
        desc.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::StorageBinding;
        desc.mipLevelCount=levels;
        auto texture=device.CreateTexture(&desc);
        if(!texture)return false;
        auto view=texture.CreateView();
        if(!view)return false;
        std::array<wgpu::TextureView,maxLevels> mips;
        std::array<wgpu::BindGroup,maxLevels-1> reductions;
        for(std::uint32_t mip=0;mip<levels;++mip) {
            wgpu::TextureViewDescriptor mipDesc{};
            mipDesc.baseMipLevel=mip;
            mipDesc.mipLevelCount=1;
            mips[mip]=texture.CreateView(&mipDesc);
            if(!mips[mip])return false;
            if(mip) {
                reductions[mip-1]=binding(device,reduce_.layout,mips[mip-1],mips[mip]);
                if(!reductions[mip-1])return false;
            }
        }
        texture_=std::move(texture);
        view_=std::move(view);
        mips_=std::move(mips);
        reductions_=std::move(reductions);
        width_=width;
        height_=height;
        levels_=levels;
        return true;
    }

    static wgpu::BindGroup binding(const wgpu::Device& device,const wgpu::BindGroupLayout& layout,
                                  const wgpu::TextureView& source,const wgpu::TextureView& destination) {
        std::array<wgpu::BindGroupEntry,2> entries{};
        entries[0].binding=0;
        entries[0].textureView=source;
        entries[1].binding=1;
        entries[1].textureView=destination;
        wgpu::BindGroupDescriptor desc{};
        desc.layout=layout;
        desc.entryCount=entries.size();
        desc.entries=entries.data();
        return device.CreateBindGroup(&desc);
    }

    static void dispatch(const wgpu::CommandEncoder& encoder,const wgpu::ComputePipeline& pipeline,
                         const wgpu::BindGroup& binding,std::uint32_t width,std::uint32_t height,const char* label) {
        wgpu::ComputePassDescriptor desc{};
        desc.label=label;
        auto pass=encoder.BeginComputePass(&desc);
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0,binding);
        pass.DispatchWorkgroups((width+7)/8,(height+7)/8);
        pass.End();
    }
};
} // namespace engine
