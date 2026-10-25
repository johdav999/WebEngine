// Private D3D12Graphics root signature and PSO construction.
void pipelines() {
    std::array<D3D12_ROOT_PARAMETER,17> params{};
    params[0].ParameterType=params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[0].Descriptor.ShaderRegister=0;params[1].Descriptor.ShaderRegister=1;
    std::array<D3D12_DESCRIPTOR_RANGE,3> ranges{{
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,5,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,6,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}}};
    for(UINT i=0;i<3;++i){const UINT slot=i?i+3:2;params[slot].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[slot].DescriptorTable={1,&ranges[i]};params[slot].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
    params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[3].Constants={2,0,4};params[3].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    params[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[6].Descriptor.ShaderRegister=3;params[6].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    params[7].ParameterType=params[8].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[7].Descriptor.ShaderRegister=4;params[8].Descriptor.ShaderRegister=5;
    const D3D12_DESCRIPTOR_RANGE postRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,9,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    params[9].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[9].DescriptorTable={1,&postRange};params[9].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    params[10].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[10].Descriptor.ShaderRegister=11;params[10].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    const D3D12_DESCRIPTOR_RANGE layerRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,12,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    params[11].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[11].DescriptorTable={1,&layerRange};params[11].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_DESCRIPTOR_RANGE waterRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,16,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    params[12].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[12].DescriptorTable={1,&waterRange};params[12].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_DESCRIPTOR_RANGE captureRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,19,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    params[13].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[13].DescriptorTable={1,&captureRange};params[13].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    params[14].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[14].Descriptor.ShaderRegister=6;params[14].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    params[15].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[15].Descriptor.ShaderRegister=7;
    const D3D12_DESCRIPTOR_RANGE instanceRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,23,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    params[16].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[16].DescriptorTable={1,&instanceRange};params[16].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    std::array<D3D12_STATIC_SAMPLER_DESC,6> samplers{};
    for(UINT i=0;i<6;++i){auto& s=samplers[i];s.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=(i>=4||(i&1))?D3D12_TEXTURE_ADDRESS_MODE_CLAMP:D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        s.AddressV=(i>=4||(i&2))?D3D12_TEXTURE_ADDRESS_MODE_CLAMP:D3D12_TEXTURE_ADDRESS_MODE_WRAP;s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MaxLOD=D3D12_FLOAT32_MAX;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
    samplers[5].Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;samplers[5].ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    D3D12_ROOT_SIGNATURE_DESC desc{UINT(params.size()),params.data(),UINT(samplers.size()),samplers.data(),D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob,errors;checked(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors),"Serialize root signature");
    checked(device_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_)),"Create root signature");
    const D3D12_INPUT_ELEMENT_DESC input[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(GuiVertex,position),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(GuiVertex,uv),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,offsetof(GuiVertex,color),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    auto p=pipelineDescription();p.pRootSignature=root_.Get();p.VS={d3d12_shaders::guiVS,sizeof(d3d12_shaders::guiVS)};p.PS={d3d12_shaders::guiPS,sizeof(d3d12_shaders::guiPS)};p.InputLayout={input,3};p.SampleMask=UINT_MAX;
    p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=p.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;p.SampleDesc.Count=1;
    checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&guiPipeline_)),"Create GUI pipeline");
}
ID3D12PipelineState* materialPipeline(const MeshDraw& draw,bool shadow,bool instanced=false) {
    const auto& material=draw.material;
    const UINT blend=shadow?0:material.alphaMode==AlphaMode::Additive?2:material.alphaMode==AlphaMode::Blend?1:0;
    // Front-face orientation also matters without culling: double-sided lighting
    // uses SV_IsFrontFace to orient normals, including mirrored water surfaces.
    const UINT winding=(material.doubleSided?0u:1u)|((draw.mirrored!=reflectionPass_)?2u:0u);
    const bool skin=bool(draw.skinPose);
    const bool unlit=material.shader==MaterialShader::Unlit&&!material.layers.enabled;
    const std::array<UINT,8> key{blend,winding,shadow||reflectionPass_?1:sampleCount_,shadow?1u:hdr_?2u:0u,skin?1u:0u,reflectionPass_?1u:0u,instanced?1u:0u,unlit?1u:0u};
    if(auto it=meshPipelines_.find(key);it!=meshPipelines_.end())return it->second.Get();
    const D3D12_INPUT_ELEMENT_DESC input[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,position),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,normal),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(Vertex,uv),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TANGENT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,offsetof(Vertex,tangent),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,1,offsetof(SkinVertex,joints),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,offsetof(SkinVertex,weights),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    auto p=pipelineDescription();p.pRootSignature=root_.Get();p.InputLayout={input,skin?6u:4u};p.SampleMask=UINT_MAX;
    p.VS=shadow?D3D12_SHADER_BYTECODE{d3d12_shaders::shadowVS,sizeof(d3d12_shaders::shadowVS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::meshVS,sizeof(d3d12_shaders::meshVS)};
    if(skin)p.VS=shadow?D3D12_SHADER_BYTECODE{d3d12_shaders::skinShadowVS,sizeof(d3d12_shaders::skinShadowVS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::skinVS,sizeof(d3d12_shaders::skinVS)};
    if(instanced){
        p.VS=shadow?D3D12_SHADER_BYTECODE{d3d12_shaders::instanceShadowVS,sizeof(d3d12_shaders::instanceShadowVS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::instanceVS,sizeof(d3d12_shaders::instanceVS)};
        if(skin)p.VS=shadow?D3D12_SHADER_BYTECODE{d3d12_shaders::instanceSkinShadowVS,sizeof(d3d12_shaders::instanceSkinShadowVS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::instanceSkinVS,sizeof(d3d12_shaders::instanceSkinVS)};
    }
    p.PS=shadow?D3D12_SHADER_BYTECODE{d3d12_shaders::shadowPS,sizeof(d3d12_shaders::shadowPS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::meshPS,sizeof(d3d12_shaders::meshPS)};
    if(unlit&&!shadow)p.PS={d3d12_shaders::unlitPS,sizeof(d3d12_shaders::unlitPS)};
    p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=(winding&1)?D3D12_CULL_MODE_BACK:D3D12_CULL_MODE_NONE;
    p.RasterizerState.FrontCounterClockwise=(winding&2)?FALSE:TRUE;p.RasterizerState.DepthClipEnable=TRUE;
    if(shadow){p.RasterizerState.DepthBias=1;p.RasterizerState.SlopeScaledDepthBias=1;}
    auto& b=p.BlendState.RenderTarget[0];b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;
    b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    if(blend){b.BlendEnable=TRUE;b.SrcBlend=D3D12_BLEND_SRC_ALPHA;b.DestBlend=blend==2?D3D12_BLEND_ONE:D3D12_BLEND_INV_SRC_ALPHA;b.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;}
    p.DepthStencilState.DepthEnable=TRUE;p.DepthStencilState.DepthWriteMask=blend?D3D12_DEPTH_WRITE_MASK_ZERO:D3D12_DEPTH_WRITE_MASK_ALL;
    p.DepthStencilState.DepthFunc=blend?D3D12_COMPARISON_FUNC_LESS_EQUAL:D3D12_COMPARISON_FUNC_LESS;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=shadow?0:1;p.RTVFormats[0]=shadow?DXGI_FORMAT_UNKNOWN:colorFormat();
    p.DSVFormat=DXGI_FORMAT_D32_FLOAT;p.SampleDesc.Count=key[2];
    if(debug_)fprintf(stderr,"D3D12 creating material pipeline shadow=%d instanced=%d\n",int(shadow),int(instanced));
    ComPtr<ID3D12PipelineState> result;checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&result)),"Create material pipeline");
    if(debug_)fprintf(stderr,"D3D12 material pipeline ready\n");
    return meshPipelines_.emplace(key,std::move(result)).first->second.Get();
}
