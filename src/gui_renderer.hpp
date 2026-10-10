#pragma once
#include "engine/gui.hpp"
#include <webgpu/webgpu_cpp.h>
#include <unordered_map>

namespace engine {
class GuiRenderer {
public:
    struct Stats {
        std::uint64_t passes=0,drawCalls=0,triangles=0,uploadCalls=0,uploadBytes=0;
        std::uint64_t newTextures=0,cachedTextures=0,textureBytes=0,bufferBytes=0;
    };
    GuiRenderer(wgpu::Device device,wgpu::TextureFormat format);
    void render(const GuiDrawList& list,Vec2 logicalSize,std::uint32_t width,std::uint32_t height,
        const wgpu::TextureView& target,const wgpu::CommandEncoder& encoder);
    const Stats& stats() const { return stats_; }
private:
    Stats stats_;
    struct GpuTexture { std::weak_ptr<const Texture> source;wgpu::Texture image;wgpu::BindGroup binding; };
    wgpu::Device device_;
    wgpu::RenderPipeline pipeline_;
    wgpu::BindGroupLayout textureLayout_;
    wgpu::BindGroup screenBinding_;
    wgpu::Buffer screen_,vertices_,indices_;
    std::size_t vertexCapacity_=0,indexCapacity_=0;
    Vec2 previousSize_{};
    std::vector<GuiVertex> previousVertices_;
    std::vector<std::uint32_t> previousIndices_;
    std::unordered_map<const Texture*,GpuTexture> textures_;
    const wgpu::BindGroup& texture(const std::shared_ptr<const Texture>& source);
};
}
