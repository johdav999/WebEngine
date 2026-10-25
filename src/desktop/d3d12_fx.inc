// Camera-specific GPU streams retain no scene/resource borrows. Copies and retired
// buffers share the normal submission fence; paused identical streams upload nothing.
template<class T> D3D12_VERTEX_BUFFER_VIEW fxStream(Frame& frame,FxStream<T>& stream,std::span<const T> values,std::uint64_t& bytes) {
    if(values.empty()){stream.shadow.clear();return {};}
    const auto size=values.size_bytes();
    if(stream.shadow.size()!=values.size()||std::memcmp(stream.shadow.data(),values.data(),size)){
        if(!stream.image||stream.capacity<size){
            auto next=buffer(size,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
            if(stream.image)frame.retained.push_back(stream.image);stream.image=std::move(next);stream.capacity=size;
        }else transition(*list_.Get(),stream.image.Get(),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,D3D12_RESOURCE_STATE_COPY_DEST);
        const auto address=upload(frame,values.data(),size);
        list_->CopyBufferRegion(stream.image.Get(),0,frame.upload.Get(),address-frame.upload->GetGPUVirtualAddress(),size);
        transition(*list_.Get(),stream.image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        stream.shadow.assign(values.begin(),values.end());bytes+=size;
    }
    return {stream.image->GetGPUVirtualAddress(),UINT(size),sizeof(T)};
}
ID3D12PipelineState* fxPipeline(bool ribbon,BlendMode blend){
    const std::array<UINT,4> key{ribbon?1u:0u,blend==BlendMode::Additive?1u:0u,sampleCount_,hdr_?1u:0u};
    if(auto it=fxPipelines_.find(key);it!=fxPipelines_.end())return it->second.Get();
    std::array<D3D12_INPUT_ELEMENT_DESC,7> input{};
    for(UINT i=0;i<input.size();++i)input[i]={i==0?"POSITION":i==1?"COLOR":"TEXCOORD",i<2?0:i-2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,i*16,ribbon?D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA:D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,ribbon?0u:1u};
    auto p=pipelineDescription();p.pRootSignature=root_.Get();p.InputLayout={input.data(),ribbon?3u:7u};p.SampleMask=UINT_MAX;
    p.VS=ribbon?D3D12_SHADER_BYTECODE{d3d12_shaders::ribbonVS,sizeof(d3d12_shaders::ribbonVS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::particleVS,sizeof(d3d12_shaders::particleVS)};
    p.PS={d3d12_shaders::fxPS,sizeof(d3d12_shaders::fxPS)};
    p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
    auto& b=p.BlendState.RenderTarget[0];b.BlendEnable=TRUE;b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;
    b.DestBlend=key[1]?D3D12_BLEND_ONE:D3D12_BLEND_INV_SRC_ALPHA;b.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    p.DepthStencilState.DepthEnable=TRUE;p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=colorFormat();p.DSVFormat=DXGI_FORMAT_D32_FLOAT;p.SampleDesc.Count=sampleCount_;
    ComPtr<ID3D12PipelineState> next;checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&next)),"Create FX pipeline");return fxPipelines_.emplace(key,std::move(next)).first->second.Get();
}
void drawFx(Frame& frame,const FxBatch& batch,const CameraComponent& camera,const D3D12_VERTEX_BUFFER_VIEW& particles,const D3D12_VERTEX_BUFFER_VIEW& ribbons){
    if(draws_>=40960)throw std::runtime_error("Direct D3D12 total draw budget exceeded");
    const auto source=batch.texture?batch.texture:Texture::white();const auto right=camera.right(),up=normalized(cross(right,camera.forward()));
    const std::array<Float4,3> constants{{{right.x,right.y,right.z,0},{up.x,up.y,up.z,0},{float(source->width),float(source->height),float((source->wrapU==TextureWrap::Clamp?1:0)|(source->wrapV==TextureWrap::Clamp?2:0)),0}}};
    list_->SetGraphicsRootConstantBufferView(15,upload(frame,constants.data(),sizeof(constants)));
    list_->SetGraphicsRootDescriptorTable(2,texture(frame,source,true));list_->SetPipelineState(fxPipeline(batch.ribbon,batch.blend));
    const auto& vertices=batch.ribbon?ribbons:particles;list_->IASetVertexBuffers(0,1,&vertices);
    if(batch.ribbon){list_->DrawInstanced(batch.count,1,batch.first,0);++trailDraws_;trailSegments_+=batch.count/6;triangles_+=batch.count/3;}
    else{list_->DrawInstanced(6,batch.count,0,batch.first);++particleDraws_;triangles_+=std::uint64_t(batch.count)*2;}
    ++draws_;
}
