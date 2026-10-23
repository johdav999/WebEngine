#include "webgpu_renderer.hpp"
#include "shaders.hpp"
#include "hdr_shader.hpp"
#include "underwater_render.hpp"
#include <cstring>

namespace engine {
void WebGpuRenderer::createWaterPipelines() {
    wgpu::SamplerDescriptor sampler{};sampler.minFilter=sampler.magFilter=wgpu::FilterMode::Linear;
    waterSampler_=device_.CreateSampler(&sampler);
    for(unsigned mode=0;mode<2;++mode) {
        std::string source=water_capture_shader;
        if(mode) {
            auto replace=[&](const char* from,const char* to){auto p=source.find(from);source.replace(p,std::strlen(from),to);};
            replace("texture_2d<f32>","texture_multisampled_2d<f32>");
            replace("texture_depth_2d;","texture_depth_multisampled_2d;");replace("const samples=1;","const samples=4;");
        }
        wgpu::ShaderSourceWGSL code{};code.code=source.c_str();wgpu::ShaderModuleDescriptor shader{};shader.nextInChain=&code;
        auto module=device_.CreateShaderModule(&shader);
        std::array<wgpu::BindGroupLayoutEntry,2> entries{};
        for(unsigned i=0;i<2;++i) {
            entries[i].binding=i;entries[i].visibility=wgpu::ShaderStage::Fragment;
            entries[i].texture.viewDimension=wgpu::TextureViewDimension::e2D;entries[i].texture.multisampled=mode!=0;
            entries[i].texture.sampleType=i==0 ? (mode ? wgpu::TextureSampleType::UnfilterableFloat : wgpu::TextureSampleType::Float) : wgpu::TextureSampleType::Depth;
        }
        wgpu::BindGroupLayoutDescriptor binding{};binding.entryCount=entries.size();binding.entries=entries.data();captureLayouts_[mode]=device_.CreateBindGroupLayout(&binding);
        wgpu::PipelineLayoutDescriptor layout{};layout.bindGroupLayoutCount=1;layout.bindGroupLayouts=&captureLayouts_[mode];
        auto pipelineLayout=device_.CreatePipelineLayout(&layout);
        std::array<wgpu::ColorTargetState,2> targets{};targets[0].format=colorFormat();targets[1].format=wgpu::TextureFormat::R32Float;
        wgpu::FragmentState fragment{};fragment.module=module;fragment.entryPoint="captureMain";fragment.targetCount=2;fragment.targets=targets.data();
        wgpu::RenderPipelineDescriptor pipeline{};pipeline.label="Opaque water color/depth snapshot";pipeline.layout=pipelineLayout;
        pipeline.vertex.module=module;pipeline.vertex.entryPoint="vertexMain";pipeline.fragment=&fragment;
        capturePipelines_[mode]=device_.CreateRenderPipeline(&pipeline);
        if(!mode) {fragment.entryPoint="presentMain";fragment.targetCount=1;pipeline.label="Single-sample water compositor";waterPresentPipeline_=device_.CreateRenderPipeline(&pipeline);}
    }
}
void WebGpuRenderer::prepareWaterTargets(bool required) {
    const auto w=required ? width_ : 1u,h=required ? height_ : 1u;
    if(waterWidth_==w&&waterHeight_==h&&waterSamples_==sampleCount_&&waterCapture_==required) return;
    auto texture=[&](wgpu::TextureFormat format,const char* label) {
        wgpu::TextureDescriptor d{};d.label=label;d.size={w,h,1};d.format=format;
        d.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::RenderAttachment;return device_.CreateTexture(&d);
    };
    auto color=texture(colorFormat(),"Opaque scene color snapshot");auto depth=texture(wgpu::TextureFormat::R32Float,"Resolved opaque depth snapshot");
    wgpu::Texture sceneColor;
    if(required&&(sampleCount_==1||hdr_)) sceneColor=texture(colorFormat(),"Single-sample water scene target");
    if(sceneColor_) sceneColor_.Destroy();if(waterColor_) waterColor_.Destroy();if(waterDepth_) waterDepth_.Destroy();
    sceneColor_=sceneColor;waterColor_=color;waterDepth_=depth;
    sceneColorView_=sceneColor ? sceneColor.CreateView() : wgpu::TextureView{};
    waterColorView_=color.CreateView();waterDepthView_=depth.CreateView();
    waterWidth_=w;waterHeight_=h;waterSamples_=sampleCount_;waterCapture_=required;++waterTargetRevision_;
    captureBinding_=nullptr;
    if(required) {
        std::array<wgpu::BindGroupEntry,2> entries{};entries[0].binding=0;entries[0].textureView=sampleCount_>1 ? msaaColorView_ : sceneColorView_;
        entries[1].binding=1;entries[1].textureView=depthView_;
        wgpu::BindGroupDescriptor binding{};binding.layout=captureLayouts_[sampleCount_>1 ? 1 : 0];binding.entryCount=2;binding.entries=entries.data();captureBinding_=device_.CreateBindGroup(&binding);
    }
}
void WebGpuRenderer::captureWater(wgpu::CommandEncoder& encoder,int sampleSlot,std::uint32_t ordinal,bool underwater) {
    std::array<wgpu::RenderPassColorAttachment,2> colors{};
    colors[0].view=waterColorView_;colors[1].view=waterDepthView_;colors[1].clearValue={1,0,0,1};
    for(auto& color:colors) {color.loadOp=wgpu::LoadOp::Clear;color.storeOp=wgpu::StoreOp::Store;}
    wgpu::RenderPassDescriptor d{};d.label="Water opaque snapshot / depth resolve";d.colorAttachmentCount=2;d.colorAttachments=colors.data();
    wgpu::PassTimestampWrites times{};
    if(sampleSlot>=0) {times.querySet=timingSlots_[sampleSlot].queries;times.beginningOfPassWriteIndex=ordinal*24+(underwater ? 20 : 14);times.endOfPassWriteIndex=times.beginningOfPassWriteIndex+1;d.timestampWrites=&times;
        if(underwater) timingSlots_[sampleSlot].underwaterPasses[ordinal]=true;else timingSlots_[sampleSlot].waterPasses[ordinal]=true;}
    auto pass=encoder.BeginRenderPass(&d);pass.SetPipeline(capturePipelines_[sampleCount_>1 ? 1 : 0]);pass.SetBindGroup(0,captureBinding_);
    const auto& view=resolvedViews_[ordinal];pass.SetScissorRect(view.x,view.y,view.width,view.height);pass.Draw(3);pass.End();
    ++counters_.passes;++counters_.drawCalls;++counters_.triangles;
}
void WebGpuRenderer::prepareReflectionTargets(bool required) {
    const auto w=required ? width_ : 1u,h=required ? height_ : 1u;
    if(reflectionWidth_==w&&reflectionHeight_==h) return;
    wgpu::TextureDescriptor d{};d.label="Planar reflection color";d.size={w,h,1};d.format=colorFormat();
    d.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::RenderAttachment;
    auto color=device_.CreateTexture(&d);d.label="Planar reflection depth";d.format=wgpu::TextureFormat::Depth24Plus;d.usage=wgpu::TextureUsage::RenderAttachment;
    auto depth=device_.CreateTexture(&d);
    if(reflectionColor_) reflectionColor_.Destroy();if(reflectionDepth_) reflectionDepth_.Destroy();
    reflectionColor_=color;reflectionDepth_=depth;reflectionColorView_=color.CreateView();reflectionDepthView_=depth.CreateView();
    reflectionWidth_=w;reflectionHeight_=h;++waterTargetRevision_;
}
void WebGpuRenderer::drawUnderwater(wgpu::CommandEncoder& encoder,wgpu::TextureView target,const UnderwaterView& water,int sampleSlot,std::uint32_t ordinal) {
    if(!underwaterLayout_) {
        std::array<wgpu::BindGroupLayoutEntry,3> entries{};
        for(unsigned i=0;i<2;++i) {entries[i].binding=i;entries[i].visibility=wgpu::ShaderStage::Fragment;
            entries[i].texture.viewDimension=wgpu::TextureViewDimension::e2D;entries[i].texture.sampleType=i ? wgpu::TextureSampleType::UnfilterableFloat : wgpu::TextureSampleType::Float;}
        entries[2].binding=2;entries[2].visibility=wgpu::ShaderStage::Fragment;entries[2].buffer.type=wgpu::BufferBindingType::Uniform;entries[2].buffer.minBindingSize=sizeof(UnderwaterUniform);
        wgpu::BindGroupLayoutDescriptor layout{};layout.entryCount=entries.size();layout.entries=entries.data();underwaterLayout_=device_.CreateBindGroupLayout(&layout);
    }
    if(!underwaterPipeline_||underwaterSamples_!=sampleCount_) {
        wgpu::ShaderSourceWGSL code{};const auto source=hdrShader(underwater_shader,hdr_);code.code=source.c_str();wgpu::ShaderModuleDescriptor module{};module.nextInChain=&code;auto shader=device_.CreateShaderModule(&module);
        wgpu::PipelineLayoutDescriptor layout{};layout.bindGroupLayoutCount=1;layout.bindGroupLayouts=&underwaterLayout_;auto pipelineLayout=device_.CreatePipelineLayout(&layout);
        wgpu::ColorTargetState color{};color.format=colorFormat();
        wgpu::FragmentState fragment{};fragment.module=shader;fragment.entryPoint="fragmentMain";fragment.targetCount=1;fragment.targets=&color;
        wgpu::RenderPipelineDescriptor pipeline{};pipeline.label="Depth-aware underwater fog and caustics";pipeline.layout=pipelineLayout;
        pipeline.vertex.module=shader;pipeline.vertex.entryPoint="vertexMain";pipeline.fragment=&fragment;pipeline.multisample.count=sampleCount_;
        underwaterPipeline_=device_.CreateRenderPipeline(&pipeline);underwaterSamples_=sampleCount_;
    }
    auto& view=*activeView_;
    if(!view.underwaterBuffer) {wgpu::BufferDescriptor d{};d.label="Underwater view parameters";d.size=sizeof(UnderwaterUniform);d.usage=wgpu::BufferUsage::Uniform|wgpu::BufferUsage::CopyDst;view.underwaterBuffer=device_.CreateBuffer(&d);}
    if(!view.underwaterBinding||view.underwaterRevision!=waterTargetRevision_) {
        std::array<wgpu::BindGroupEntry,3> entries{};entries[0].binding=0;entries[0].textureView=waterColorView_;
        entries[1].binding=1;entries[1].textureView=waterDepthView_;entries[2].binding=2;entries[2].buffer=view.underwaterBuffer;entries[2].size=sizeof(UnderwaterUniform);
        wgpu::BindGroupDescriptor d{};d.layout=underwaterLayout_;d.entryCount=entries.size();d.entries=entries.data();view.underwaterBinding=device_.CreateBindGroup(&d);view.underwaterRevision=waterTargetRevision_;
    }
    const auto& resolved=resolvedViews_[ordinal];const auto uniforms=prepareUnderwater(water,*activeCamera_,resolved.viewport,waterTime_);
    static_assert(sizeof(view.underwaterShadow)==sizeof(UnderwaterUniform));
    if(!view.underwaterValid||std::memcmp(&view.underwaterShadow,&uniforms,sizeof(uniforms))) {
        device_.GetQueue().WriteBuffer(view.underwaterBuffer,0,&uniforms,sizeof(uniforms));
        view.underwaterShadow=uniforms;view.underwaterValid=true;
        ++counters_.uploadCalls;counters_.uploadBytes+=sizeof(uniforms);++counters_.frameUploadCalls;counters_.frameUploadBytes+=sizeof(uniforms);
    }
    wgpu::RenderPassColorAttachment color{};color.view=target;color.loadOp=wgpu::LoadOp::Load;color.storeOp=wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor d{};d.label="Underwater fog / absorption";d.colorAttachmentCount=1;d.colorAttachments=&color;
    wgpu::PassTimestampWrites times{};if(sampleSlot>=0) {times.querySet=timingSlots_[sampleSlot].queries;times.beginningOfPassWriteIndex=ordinal*24+22;times.endOfPassWriteIndex=ordinal*24+23;d.timestampWrites=&times;}
    auto pass=encoder.BeginRenderPass(&d);pass.SetPipeline(underwaterPipeline_);pass.SetBindGroup(0,view.underwaterBinding);
    pass.SetScissorRect(resolved.x,resolved.y,resolved.width,resolved.height);pass.Draw(3);pass.End();
    ++counters_.passes;++counters_.drawCalls;++counters_.triangles;
}

void WebGpuRenderer::drawPost(wgpu::CommandEncoder& encoder,wgpu::TextureView target) {
    if(!postLayout_) {
        std::array<wgpu::BindGroupLayoutEntry,4> e{};
        for(unsigned i=0;i<2;++i){e[i].binding=i;e[i].visibility=wgpu::ShaderStage::Fragment;e[i].texture.sampleType=wgpu::TextureSampleType::Float;e[i].texture.viewDimension=wgpu::TextureViewDimension::e2D;}
        e[2].binding=2;e[2].visibility=wgpu::ShaderStage::Fragment;e[2].sampler.type=wgpu::SamplerBindingType::Filtering;
        e[3].binding=3;e[3].visibility=wgpu::ShaderStage::Fragment;e[3].buffer.type=wgpu::BufferBindingType::Uniform;e[3].buffer.minBindingSize=32;
        wgpu::BindGroupLayoutDescriptor d{};d.entryCount=e.size();d.entries=e.data();postLayout_=device_.CreateBindGroupLayout(&d);
        wgpu::BufferDescriptor b{};b.size=32;b.usage=wgpu::BufferUsage::Uniform|wgpu::BufferUsage::CopyDst;postBuffer_=device_.CreateBuffer(&b);
        wgpu::ShaderSourceWGSL code{};code.code=post_shader;wgpu::ShaderModuleDescriptor m{};m.nextInChain=&code;auto shader=device_.CreateShaderModule(&m);
        wgpu::PipelineLayoutDescriptor l{};l.bindGroupLayoutCount=1;l.bindGroupLayouts=&postLayout_;auto layout=device_.CreatePipelineLayout(&l);
        wgpu::ColorTargetState color{};color.format=wgpu::TextureFormat::RGBA16Float;
        wgpu::FragmentState f{};f.module=shader;f.entryPoint="extractMain";f.targetCount=1;f.targets=&color;
        wgpu::RenderPipelineDescriptor r{};r.label="HDR bloom extraction";r.layout=layout;r.vertex.module=shader;r.vertex.entryPoint="vertexMain";r.fragment=&f;
        bloomPipeline_=device_.CreateRenderPipeline(&r);color.format=format_;f.entryPoint="compositeMain";r.label="HDR bloom and color grading";postPipeline_=device_.CreateRenderPipeline(&r);
    }
    const auto w=std::max(1u,(width_+3)/4),h=std::max(1u,(height_+3)/4);
    if(postWidth_!=w||postHeight_!=h) {
        if(bloomTexture_)bloomTexture_.Destroy();
        wgpu::TextureDescriptor d{};d.size={w,h,1};d.format=wgpu::TextureFormat::RGBA16Float;d.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::RenderAttachment;
        bloomTexture_=device_.CreateTexture(&d);bloomView_=bloomTexture_.CreateView();postWidth_=w;postHeight_=h;
    }
    const auto& p=quality_.post;const std::array<float,8> values={std::exp2(p.exposure),p.bloomStrength,p.bloomThreshold,0,p.saturation,p.contrast,0,0};
    if(values!=postValues_){device_.GetQueue().WriteBuffer(postBuffer_,0,values.data(),32);postValues_=values;++counters_.uploadCalls;counters_.uploadBytes+=32;}
    auto binding=[&](wgpu::TextureView bloom){std::array<wgpu::BindGroupEntry,4> e{};
        e[0].binding=0;e[0].textureView=sceneColorView_;e[1].binding=1;e[1].textureView=bloom;e[2].binding=2;e[2].sampler=waterSampler_;e[3].binding=3;e[3].buffer=postBuffer_;e[3].size=32;
        wgpu::BindGroupDescriptor d{};d.layout=postLayout_;d.entryCount=e.size();d.entries=e.data();return device_.CreateBindGroup(&d);};
    auto pass=[&](wgpu::TextureView view,wgpu::RenderPipeline pipeline,wgpu::BindGroup group){
        wgpu::RenderPassColorAttachment c{};c.view=view;c.loadOp=wgpu::LoadOp::Clear;c.storeOp=wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor d{};d.colorAttachmentCount=1;d.colorAttachments=&c;
        auto r=encoder.BeginRenderPass(&d);r.SetPipeline(pipeline);r.SetBindGroup(0,group);r.Draw(3);r.End();++counters_.passes;++counters_.drawCalls;++counters_.triangles;};
    if(p.bloomStrength>0)pass(bloomView_,bloomPipeline_,binding(sceneColorView_));
    pass(target,postPipeline_,binding(bloomView_));
    counters_.gpuTextureBytes+=std::uint64_t(w)*h*8;counters_.gpuBufferBytes+=32;
}
} // namespace engine
