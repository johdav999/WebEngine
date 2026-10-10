#include "gui_renderer.hpp"
#include "shaders.hpp"
#include <cstring>

namespace engine {
namespace {
wgpu::Buffer buffer(wgpu::Device device,std::size_t bytes,wgpu::BufferUsage usage) {
    wgpu::BufferDescriptor desc{};desc.size=bytes;desc.usage=usage|wgpu::BufferUsage::CopyDst;desc.label="GUI buffer";
    return device.CreateBuffer(&desc);
}
}
GuiRenderer::GuiRenderer(wgpu::Device device,wgpu::TextureFormat format) : device_(std::move(device)) {
    wgpu::BindGroupLayoutEntry screen{};screen.binding=0;screen.visibility=wgpu::ShaderStage::Vertex;screen.buffer.type=wgpu::BufferBindingType::Uniform;screen.buffer.minBindingSize=16;
    wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=1;layout.entries=&screen;
    const auto screenLayout=device_.CreateBindGroupLayout(&layout);
    std::array<wgpu::BindGroupLayoutEntry,2> entries{};
    entries[0].binding=0;entries[0].visibility=wgpu::ShaderStage::Fragment;entries[0].sampler.type=wgpu::SamplerBindingType::Filtering;
    entries[1].binding=1;entries[1].visibility=wgpu::ShaderStage::Fragment;entries[1].texture.sampleType=wgpu::TextureSampleType::Float;entries[1].texture.viewDimension=wgpu::TextureViewDimension::e2D;
    layout.entryCount=2;layout.entries=entries.data();textureLayout_=device_.CreateBindGroupLayout(&layout);
    const std::array layouts={screenLayout,textureLayout_};
    wgpu::PipelineLayoutDescriptor pipelineLayout{};pipelineLayout.bindGroupLayoutCount=layouts.size();pipelineLayout.bindGroupLayouts=layouts.data();
    wgpu::ShaderSourceWGSL code{};code.code=gui_shader;
    wgpu::ShaderModuleDescriptor shaderDesc{};shaderDesc.nextInChain=&code;shaderDesc.label="Screen-space GUI";
    const auto shader=device_.CreateShaderModule(&shaderDesc);
    std::array<wgpu::VertexAttribute,3> attributes{};
    attributes[0].shaderLocation=0;attributes[0].format=wgpu::VertexFormat::Float32x2;attributes[0].offset=offsetof(GuiVertex,position);
    attributes[1].shaderLocation=1;attributes[1].format=wgpu::VertexFormat::Float32x2;attributes[1].offset=offsetof(GuiVertex,uv);
    attributes[2].shaderLocation=2;attributes[2].format=wgpu::VertexFormat::Float32x4;attributes[2].offset=offsetof(GuiVertex,color);
    wgpu::VertexBufferLayout vertex{};vertex.arrayStride=sizeof(GuiVertex);vertex.attributeCount=attributes.size();vertex.attributes=attributes.data();vertex.stepMode=wgpu::VertexStepMode::Vertex;
    wgpu::BlendState blend{};
    blend.color.srcFactor=blend.alpha.srcFactor=wgpu::BlendFactor::One;
    blend.color.dstFactor=blend.alpha.dstFactor=wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.color.operation=blend.alpha.operation=wgpu::BlendOperation::Add;
    wgpu::ColorTargetState color{};color.format=format;color.blend=&blend;color.writeMask=wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};fragment.module=shader;fragment.entryPoint="fragmentMain";fragment.targetCount=1;fragment.targets=&color;
    wgpu::RenderPipelineDescriptor desc{};desc.label="GUI / no depth";desc.layout=device_.CreatePipelineLayout(&pipelineLayout);
    desc.vertex.module=shader;desc.vertex.entryPoint="vertexMain";desc.vertex.bufferCount=1;desc.vertex.buffers=&vertex;
    desc.fragment=&fragment;desc.primitive.topology=wgpu::PrimitiveTopology::TriangleList;desc.primitive.cullMode=wgpu::CullMode::None;desc.multisample.count=1;
    pipeline_=device_.CreateRenderPipeline(&desc);
    screen_=buffer(device_,16,wgpu::BufferUsage::Uniform);
    wgpu::BindGroupEntry binding{};binding.binding=0;binding.buffer=screen_;binding.size=16;
    wgpu::BindGroupDescriptor group{};group.layout=screenLayout;group.entryCount=1;group.entries=&binding;screenBinding_=device_.CreateBindGroup(&group);
}
const wgpu::BindGroup& GuiRenderer::texture(const std::shared_ptr<const Texture>& source) {
    if(const auto it=textures_.find(source.get());it!=textures_.end()) return it->second.binding;
    if(source->width==0||source->height==0||source->pixels.size()!=std::size_t(source->width)*source->height*4) throw std::invalid_argument("Invalid GUI image pixels");
    GpuTexture gpu;gpu.source=source;
    wgpu::TextureDescriptor desc{};desc.label="GUI image / font atlas";desc.size={source->width,source->height,1};desc.format=wgpu::TextureFormat::RGBA8Unorm;
    desc.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::CopyDst;gpu.image=device_.CreateTexture(&desc);
    wgpu::TexelCopyTextureInfo dest{};dest.texture=gpu.image;
    wgpu::TexelCopyBufferLayout layout{};layout.bytesPerRow=source->width*4;layout.rowsPerImage=source->height;
    const wgpu::Extent3D extent{source->width,source->height,1};device_.GetQueue().WriteTexture(&dest,source->pixels.data(),source->pixels.size(),&layout,&extent);
    ++stats_.uploadCalls;stats_.uploadBytes+=source->pixels.size();++stats_.newTextures;
    wgpu::SamplerDescriptor sampler{};sampler.magFilter=sampler.minFilter=wgpu::FilterMode::Linear;
    sampler.addressModeU=sampler.addressModeV=wgpu::AddressMode::ClampToEdge;
    std::array<wgpu::BindGroupEntry,2> bindings{};bindings[0].binding=0;bindings[0].sampler=device_.CreateSampler(&sampler);bindings[1].binding=1;bindings[1].textureView=gpu.image.CreateView();
    wgpu::BindGroupDescriptor group{};group.layout=textureLayout_;group.entryCount=bindings.size();group.entries=bindings.data();gpu.binding=device_.CreateBindGroup(&group);
    return textures_.emplace(source.get(),std::move(gpu)).first->second.binding;
}
void GuiRenderer::render(const GuiDrawList& list,Vec2 logical,std::uint32_t width,std::uint32_t height,const wgpu::TextureView& target,const wgpu::CommandEncoder& encoder) {
    stats_={};
    for(auto it=textures_.begin();it!=textures_.end();) {
        if(it->second.source.expired()) { it->second.image.Destroy();it=textures_.erase(it); } else ++it;
    }
    for(const auto& command:list.commands) texture(command.texture);
    stats_.cachedTextures=textures_.size();
    for(const auto& entry:textures_) if(const auto source=entry.second.source.lock()) stats_.textureBytes+=source->pixels.size();
    stats_.bufferBytes=16+vertexCapacity_*sizeof(GuiVertex)+indexCapacity_*sizeof(std::uint32_t);
    if(list.indices.empty()||logical.x<=0||logical.y<=0) return;
    bool growVertices=list.vertices.size()>vertexCapacity_,growIndices=list.indices.size()>indexCapacity_;
    if(growVertices) { if(vertices_) vertices_.Destroy();vertexCapacity_=std::max(list.vertices.size(),vertexCapacity_*2+256);vertices_=buffer(device_,vertexCapacity_*sizeof(GuiVertex),wgpu::BufferUsage::Vertex); }
    if(growIndices) { if(indices_) indices_.Destroy();indexCapacity_=std::max(list.indices.size(),indexCapacity_*2+384);indices_=buffer(device_,indexCapacity_*sizeof(std::uint32_t),wgpu::BufferUsage::Index); }
    if(growVertices||list.vertices.size()!=previousVertices_.size()||std::memcmp(list.vertices.data(),previousVertices_.data(),list.vertices.size()*sizeof(GuiVertex))) {
        device_.GetQueue().WriteBuffer(vertices_,0,list.vertices.data(),list.vertices.size()*sizeof(GuiVertex));previousVertices_=list.vertices;
        ++stats_.uploadCalls;stats_.uploadBytes+=list.vertices.size()*sizeof(GuiVertex);
    }
    if(growIndices||list.indices!=previousIndices_) { device_.GetQueue().WriteBuffer(indices_,0,list.indices.data(),list.indices.size()*sizeof(std::uint32_t));previousIndices_=list.indices;++stats_.uploadCalls;stats_.uploadBytes+=list.indices.size()*sizeof(std::uint32_t); }
    if(logical.x!=previousSize_.x||logical.y!=previousSize_.y) {
        const std::array<float,4> size={logical.x,logical.y,0,0};device_.GetQueue().WriteBuffer(screen_,0,size.data(),sizeof(size));previousSize_=logical;
        ++stats_.uploadCalls;stats_.uploadBytes+=sizeof(size);
    }
    wgpu::RenderPassColorAttachment color{};color.view=target;color.loadOp=wgpu::LoadOp::Load;color.storeOp=wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor desc{};desc.label="GUI";desc.colorAttachmentCount=1;desc.colorAttachments=&color;
    auto pass=encoder.BeginRenderPass(&desc);pass.SetPipeline(pipeline_);pass.SetViewport(0,0,width,height,0,1);
    ++stats_.passes;stats_.bufferBytes=16+vertexCapacity_*sizeof(GuiVertex)+indexCapacity_*sizeof(std::uint32_t);
    pass.SetBindGroup(0,screenBinding_);pass.SetVertexBuffer(0,vertices_);pass.SetIndexBuffer(indices_,wgpu::IndexFormat::Uint32);
    const float sx=width/logical.x,sy=height/logical.y;
    for(const auto& command:list.commands) {
        // Round inward: nested clipping must not leak pixels into adjacent controls.
        const auto x=static_cast<std::uint32_t>(std::clamp(std::ceil(command.clip.x*sx),0.f,float(width)));
        const auto y=static_cast<std::uint32_t>(std::clamp(std::ceil(command.clip.y*sy),0.f,float(height)));
        const auto right=static_cast<std::uint32_t>(std::clamp(std::floor((command.clip.x+command.clip.width)*sx),0.f,float(width)));
        const auto bottom=static_cast<std::uint32_t>(std::clamp(std::floor((command.clip.y+command.clip.height)*sy),0.f,float(height)));
        if(right<=x||bottom<=y) continue;
        pass.SetScissorRect(x,y,right-x,bottom-y);pass.SetBindGroup(1,textures_.at(command.texture.get()).binding);
        pass.DrawIndexed(command.indexCount,1,command.firstIndex,0,0);
        ++stats_.drawCalls;stats_.triangles+=command.indexCount/3;
    }
    pass.End();
}
}
