// Private sky/fog/HDR presentation. Scene settings and clocks remain portable.
DXGI_FORMAT colorFormat() const {return hdr_?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;}
void queryPostSupport() {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{DXGI_FORMAT_R16G16B16A16_FLOAT};
    constexpr UINT required=D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_RENDER_TARGET|D3D12_FORMAT_SUPPORT1_BLENDABLE|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE|D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    capabilities_.hdrPostProcessing=SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))&&(UINT(support.Support1)&required)==required;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa{DXGI_FORMAT_R16G16B16A16_FLOAT,4,D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,0};
    hdrMsaa_=capabilities_.msaa4x&&capabilities_.hdrPostProcessing&&(support.Support1&D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE)
        &&SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&msaa,sizeof(msaa)))&&msaa.NumQualityLevels;
}
ComPtr<ID3D12Resource> colorTarget(UINT w,UINT h,DXGI_FORMAT format,UINT samples,D3D12_CPU_DESCRIPTOR_HANDLE view) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;
    d.Format=format;d.SampleDesc.Count=samples;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};clear.Format=format;auto properties=heap(D3D12_HEAP_TYPE_DEFAULT);ComPtr<ID3D12Resource> image;
    checked(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&image)),"Create presentation target");
    device_->CreateRenderTargetView(image.Get(),nullptr,view);return image;
}
D3D12_GRAPHICS_PIPELINE_STATE_DESC fullPipeline() const {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=root_.Get();p.SampleMask=UINT_MAX;
    p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
    auto& b=p.BlendState.RenderTarget[0];b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;
    b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.SampleDesc.Count=1;return p;
}
D3D12_GPU_DESCRIPTOR_HANDLE fogGridBinding(Frame& frame,bool enabled) {
    const auto slot=reserveDescriptors(frame,1);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels=1;
    device_->CreateShaderResourceView(enabled?fogGrid_.Get():nullptr,&view,srvCpu(frame,slot));
    return srvGpu(frame,slot);
}
void prepareFogGridTarget(Frame& frame,bool clouds) {
    const UINT height=clouds?fog_grid::height+cloud_grid::height*cloud_grid::depth:fog_grid::height;
    if(fogGrid_&&fogGrid_->GetDesc().Height>=height)return;
    if(fogGrid_)frame.retained.push_back(std::move(fogGrid_));
    fogGrid_=colorTarget(fog_grid::width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,1,rtv(frameCount+7));
    transition(*list_.Get(),fogGrid_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
void drawCloudGrid(Frame& frame,const SkyConstants& constants) {
    if(!cloudGridPipeline_) {
        auto p=fullPipeline();
        p.VS={d3d12_shaders::fullVS,sizeof(d3d12_shaders::fullVS)};
        p.PS={d3d12_shaders::cloudGridPS,sizeof(d3d12_shaders::cloudGridPS)};
        p.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
        checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&cloudGridPipeline_)),"Create cloud prefix pipeline");
    }
    list_->SetGraphicsRootConstantBufferView(7,upload(frame,&constants,sizeof(constants)));
    transition(*list_.Get(),fogGrid_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto target=rtv(frameCount+7);
    const D3D12_VIEWPORT viewport{0,float(fog_grid::height),float(cloud_grid::width),float(cloud_grid::height*cloud_grid::depth),0,1};
    const D3D12_RECT rect{0,LONG(fog_grid::height),LONG(cloud_grid::width),LONG(fog_grid::height+cloud_grid::height*cloud_grid::depth)};
    list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);list_->OMSetRenderTargets(1,&target,FALSE,nullptr);
    list_->SetPipelineState(cloudGridPipeline_.Get());list_->DrawInstanced(3,1,0,0);
    transition(*list_.Get(),fogGrid_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ++draws_;++triangles_;++cloudGridPasses_;
}
void drawCloudShadow(Frame& frame,const SkyConstants& constants) {
    if(!cloudShadowPipeline_) {
        auto p=fullPipeline();
        p.VS={d3d12_shaders::fullVS,sizeof(d3d12_shaders::fullVS)};
        p.PS={d3d12_shaders::cloudShadowPS,sizeof(d3d12_shaders::cloudShadowPS)};
        p.NumRenderTargets=0;p.RTVFormats[0]=DXGI_FORMAT_UNKNOWN;p.DSVFormat=DXGI_FORMAT_D32_FLOAT;
        p.DepthStencilState.DepthEnable=TRUE;p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&cloudShadowPipeline_)),"Create cloud shadow pipeline");
    }
    list_->SetGraphicsRootConstantBufferView(7,upload(frame,&constants,sizeof(constants)));
    transition(*list_.Get(),shadowMaps_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto target=dsv_->GetCPUDescriptorHandleForHeapStart();target.ptr+=(cloud_grid::shadowLayer+1)*dsvStride_;
    const D3D12_VIEWPORT viewport{0,0,float(shadowResolution_),float(shadowResolution_),0,1};
    const D3D12_RECT rect{0,0,LONG(shadowResolution_),LONG(shadowResolution_)};
    list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);list_->OMSetRenderTargets(0,nullptr,FALSE,&target);
    list_->ClearDepthStencilView(target,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    list_->SetPipelineState(cloudShadowPipeline_.Get());list_->DrawInstanced(3,1,0,0);
    transition(*list_.Get(),shadowMaps_.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ++draws_;++triangles_;++cloudShadowPasses_;
}
bool prepareFogLightCache(Frame& frame,FogUniform& fog,const ViewLighting& lighting) {
    const bool dirty=fogLightCacheState_.prepare(fog,lighting,quality_);
    if(!fogLightCacheState_.active()||!capabilities_.hdrPostProcessing) {
        fogLightCacheState_.reset();fog.volumeLighting[2]=0;fog.volumeLighting[3]=0;
        if(fogLightCache_)frame.retained.push_back(std::move(fogLightCache_));
        return false;
    }
    try {
        if(!fogLightCachePipeline_) {
            auto p=fullPipeline();
            p.VS={d3d12_shaders::fullVS,sizeof(d3d12_shaders::fullVS)};
            p.PS={d3d12_shaders::fogLightCachePS,sizeof(d3d12_shaders::fogLightCachePS)};
            p.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
            checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&fogLightCachePipeline_)),"Create fog light cache pipeline");
        }
        const bool created=!fogLightCache_;
        if(created) {
            fogLightCache_=colorTarget(fog_light_cache::width,fog_light_cache::height,
                DXGI_FORMAT_R16G16B16A16_FLOAT,1,rtv(frameCount+8));
            transition(*list_.Get(),fogLightCache_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        frame.retained.push_back(fogLightCache_);
        return dirty||created;
    } catch(const DesktopDeviceLost&) {
        throw;
    } catch(const std::exception&) {
        checked(device_->GetDeviceRemovedReason(),"Fog light cache allocation device");
        fogLightCacheState_.reset();fog.volumeLighting[2]=0;fog.volumeLighting[3]=0;
        return false;
    }
}
D3D12_GPU_DESCRIPTOR_HANDLE fogLightCacheBinding(Frame& frame,bool enabled) {
    const auto slot=reserveDescriptors(frame,1);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels=1;
    device_->CreateShaderResourceView(enabled?fogLightCache_.Get():nullptr,&view,srvCpu(frame,slot));
    return srvGpu(frame,slot);
}
void drawFogLightCache() {
    transition(*list_.Get(),fogLightCache_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto target=rtv(frameCount+8);
    const D3D12_VIEWPORT viewport{0,0,float(fog_light_cache::width),float(fog_light_cache::height),0,1};
    const D3D12_RECT rect{0,0,LONG(fog_light_cache::width),LONG(fog_light_cache::height)};
    list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);list_->OMSetRenderTargets(1,&target,FALSE,nullptr);
    list_->SetPipelineState(fogLightCachePipeline_.Get());list_->DrawInstanced(3,1,0,0);
    transition(*list_.Get(),fogLightCache_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ++draws_;++triangles_;++fogLightCacheBuilds_;
}
void drawFogGrid() {
    if(!fogGridPipeline_) {
        auto p=fullPipeline();
        p.VS={d3d12_shaders::fullVS,sizeof(d3d12_shaders::fullVS)};
        p.PS={d3d12_shaders::fogGridPS,sizeof(d3d12_shaders::fogGridPS)};
        p.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
        checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&fogGridPipeline_)),"Create volumetric fog pipeline");
    }
    const auto target=rtv(frameCount+7);
    if(!fogGrid_) fogGrid_=colorTarget(fog_grid::width,fog_grid::height,DXGI_FORMAT_R16G16B16A16_FLOAT,1,target);
    else transition(*list_.Get(),fogGrid_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_VIEWPORT viewport{0,0,float(fog_grid::width),float(fog_grid::height),0,1};
    const D3D12_RECT rect{0,0,LONG(fog_grid::width),LONG(fog_grid::height)};
    list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);list_->OMSetRenderTargets(1,&target,FALSE,nullptr);
    list_->SetPipelineState(fogGridPipeline_.Get());list_->DrawInstanced(3,1,0,0);
    transition(*list_.Get(),fogGrid_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ++draws_;++triangles_;++fogGridPasses_;
}
ID3D12PipelineState* skyPipeline(bool sphere,bool volume) {
    const std::array<UINT,4> key{surfacePass_?1u:sampleCount_,hdr_&&!surfacePass_?1u:0u,sphere?1u:0u,volume?1u:0u};
    if(auto it=skyPipelines_.find(key);it!=skyPipelines_.end())return it->second.Get();
    auto p=fullPipeline();p.SampleDesc.Count=key[0];p.RTVFormats[0]=surfacePass_?DXGI_FORMAT_R8G8B8A8_UNORM:colorFormat();p.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    p.VS=sphere?D3D12_SHADER_BYTECODE{d3d12_shaders::skySphereVS,sizeof(d3d12_shaders::skySphereVS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::skyVS,sizeof(d3d12_shaders::skyVS)};
    p.PS=volume?D3D12_SHADER_BYTECODE{d3d12_shaders::skyVolumePS,sizeof(d3d12_shaders::skyVolumePS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::skyPS,sizeof(d3d12_shaders::skyPS)};
    const D3D12_INPUT_ELEMENT_DESC input{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,position),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
    if(sphere){p.InputLayout={&input,1};p.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;p.RasterizerState.FrontCounterClockwise=TRUE;}
    ComPtr<ID3D12PipelineState> result;checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&result)),"Create sky pipeline");
    return skyPipelines_.emplace(key,std::move(result)).first->second.Get();
}
void drawSky(Frame& frame,const RenderView& view,float aspect,const FogUniform& fog,const CloudUniform& clouds) {
    if(!scene_->sky()||!view.skyEnabled)return;
    const auto& sky=*scene_->sky();SkyConstants constants{prepareSky(sky,*view.camera,aspect,quality_.volumetricCloudSteps),{hdr_&&!surfacePass_?1.f:0.f,0,0,0}};constants.sky.fog=fog;constants.sky.cloud=clouds;
    list_->SetGraphicsRootConstantBufferView(7,upload(frame,&constants,sizeof(constants)));
    list_->SetGraphicsRootDescriptorTable(2,texture(frame,sky.mode==SkyMode::Panorama?sky.panorama:Texture::white(),true));
    const bool volume=constants.sky.cloudShape[2]>0;
    if(volume)list_->SetGraphicsRootDescriptorTable(20,texture(frame,skyCloudNoiseTexture(),false,true));
    const bool sphere=sky.geometry==SkyGeometry::Sphere;list_->SetPipelineState(skyPipeline(sphere,volume));
    if(sphere){auto& gpu=mesh(frame,skySphereMesh());list_->IASetVertexBuffers(0,1,&gpu.vb);list_->IASetIndexBuffer(&gpu.ib);list_->DrawIndexedInstanced(gpu.ib.SizeInBytes/4,1,0,0,0);}
    else list_->DrawInstanced(3,1,0,0);
    ++skyDraws_;
}
D3D12_GPU_DESCRIPTOR_HANDLE postBinding(Frame& frame,ID3D12Resource* bloom) {
    const auto first=reserveDescriptors(frame,2);const std::array<ID3D12Resource*,2> images{hdrColor_.Get(),bloom};
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
    for(UINT i=0;i<2;++i)device_->CreateShaderResourceView(images[i],&view,srvCpu(frame,first+i));return srvGpu(frame,first);
}
void drawPost(Frame& frame,UINT backBuffer) {
    if(!bloomPipeline_){auto p=fullPipeline();p.VS={d3d12_shaders::fullVS,sizeof(d3d12_shaders::fullVS)};
        p.PS={d3d12_shaders::bloomPS,sizeof(d3d12_shaders::bloomPS)};p.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
        checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&bloomPipeline_)),"Create bloom pipeline");
        p.PS={d3d12_shaders::compositePS,sizeof(d3d12_shaders::compositePS)};p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&compositePipeline_)),"Create composite pipeline");}
    const auto& post=quality_.post;const std::array<float,8> settings{std::exp2(post.exposure),post.bloomStrength,post.bloomThreshold,0,post.saturation,post.contrast,float(post.toneMapper),0};
    list_->SetGraphicsRootConstantBufferView(8,upload(frame,settings.data(),sizeof(settings)));
    auto draw=[&](D3D12_CPU_DESCRIPTOR_HANDLE target,UINT w,UINT h,ID3D12PipelineState* pipeline,D3D12_GPU_DESCRIPTOR_HANDLE bindings){
        const D3D12_VIEWPORT viewport{0,0,float(w),float(h),0,1};const D3D12_RECT rect{0,0,LONG(w),LONG(h)};
        list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);list_->OMSetRenderTargets(1,&target,FALSE,nullptr);
        list_->SetGraphicsRootDescriptorTable(9,bindings);list_->SetPipelineState(pipeline);list_->DrawInstanced(3,1,0,0);++postPasses_;
    };
    if(post.bloomStrength>0){
        GpuSpan timing(*this,"Bloom");
        draw(rtv(frameCount+2),std::max(1u,(width_+3)/4),std::max(1u,(height_+3)/4),bloomPipeline_.Get(),postBinding(frame,hdrColor_.Get()));++bloomPasses_;
        transition(*list_.Get(),bloom_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    GpuSpan timing(*this,"Composition");
    transition(*list_.Get(),targets_[backBuffer].Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    draw(rtv(backBuffer),width_,height_,compositePipeline_.Get(),postBinding(frame,post.bloomStrength>0?bloom_.Get():hdrColor_.Get()));
    if(post.bloomStrength>0)transition(*list_.Get(),bloom_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition(*list_.Get(),hdrColor_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
}
