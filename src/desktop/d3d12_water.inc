// Private water targets and passes. Shared helpers own selection and numeric payloads.
D3D12_CPU_DESCRIPTOR_HANDLE reflectionDsv() const {auto view=dsv_->GetCPUDescriptorHandleForHeapStart();view.ptr+=5*dsvStride_;return view;}
void prepareWaterTargets(bool capture,bool reflection){
    const UINT w=capture?width_:1,h=capture?height_:1,rw=reflection?width_:1,rh=reflection?height_:1;
    if(waterWidth_==w&&waterHeight_==h&&reflectionWidth_==rw&&reflectionHeight_==rh&&waterCapture_==capture&&waterSamples_==sampleCount_&&waterHdr_==hdr_)return;
    idle();waterScene_.Reset();waterColor_.Reset();waterDepth_.Reset();reflectionColor_.Reset();reflectionDepth_.Reset();
    if(capture&&sampleCount_==1&&!hdr_)waterScene_=colorTarget(width_,height_,colorFormat(),1,rtv(frameCount+3));
    waterColor_=colorTarget(w,h,colorFormat(),1,rtv(frameCount+4));waterDepth_=colorTarget(w,h,DXGI_FORMAT_R32_FLOAT,1,rtv(frameCount+5));
    reflectionColor_=colorTarget(rw,rh,colorFormat(),1,rtv(frameCount+6));
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=rw;d.Height=rh;d.DepthOrArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT_D32_FLOAT;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    auto properties=heap(D3D12_HEAP_TYPE_DEFAULT);D3D12_CLEAR_VALUE clear{};clear.Format=d.Format;clear.DepthStencil.Depth=1;
    checked(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&reflectionDepth_)),"Create reflection depth");device_->CreateDepthStencilView(reflectionDepth_.Get(),nullptr,reflectionDsv());
    waterWidth_=w;waterHeight_=h;reflectionWidth_=rw;reflectionHeight_=rh;waterSamples_=sampleCount_;waterHdr_=hdr_;waterCapture_=capture;waterDirty_=true;
}
void initializeWaterTargets(){
    if(!waterDirty_)return;const float black[]{0,0,0,0},farDepth[]{1,0,0,1};
    list_->ClearRenderTargetView(rtv(frameCount+4),black,0,nullptr);list_->ClearRenderTargetView(rtv(frameCount+5),farDepth,0,nullptr);list_->ClearRenderTargetView(rtv(frameCount+6),black,0,nullptr);
    for(auto* image:{waterColor_.Get(),waterDepth_.Get(),reflectionColor_.Get()})transition(*list_.Get(),image,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    waterDirty_=false;
}
void imageView(Frame& frame,UINT slot,ID3D12Resource* image,DXGI_FORMAT format,bool multisampled=false){
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=format;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.ViewDimension=multisampled?D3D12_SRV_DIMENSION_TEXTURE2DMS:D3D12_SRV_DIMENSION_TEXTURE2D;if(!multisampled)d.Texture2D.MipLevels=1;
    device_->CreateShaderResourceView(image,&d,srvCpu(frame,slot));
}
D3D12_GPU_DESCRIPTOR_HANDLE waterBinding(Frame& frame,bool mirrored=false){
    const auto first=reserveDescriptors(frame,3);imageView(frame,first,waterColor_.Get(),colorFormat());imageView(frame,first+1,waterDepth_.Get(),DXGI_FORMAT_R32_FLOAT);
    // Never expose the active reflection render target to the reflection shader.
    imageView(frame,first+2,mirrored?waterColor_.Get():reflectionColor_.Get(),colorFormat());return srvGpu(frame,first);
}
ID3D12PipelineState* waterPipeline(bool underwater){
    const std::array<UINT,3> key{underwater?1u:0u,sampleCount_,hdr_?1u:0u};if(auto it=waterPipelines_.find(key);it!=waterPipelines_.end())return it->second.Get();
    auto p=fullPipeline();p.VS={d3d12_shaders::fullVS,sizeof(d3d12_shaders::fullVS)};p.RTVFormats[0]=colorFormat();
    if(underwater){p.SampleDesc.Count=sampleCount_;p.PS={d3d12_shaders::underwaterPS,sizeof(d3d12_shaders::underwaterPS)};}
    else{p.NumRenderTargets=2;p.RTVFormats[1]=DXGI_FORMAT_R32_FLOAT;p.PS=sampleCount_>1?D3D12_SHADER_BYTECODE{d3d12_shaders::waterCaptureMsPS,sizeof(d3d12_shaders::waterCaptureMsPS)}:D3D12_SHADER_BYTECODE{d3d12_shaders::waterCapturePS,sizeof(d3d12_shaders::waterCapturePS)};}
    ComPtr<ID3D12PipelineState> next;checked(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&next)),"Create water pipeline");return waterPipelines_.emplace(key,std::move(next)).first->second.Get();
}
void fullViewport(){const D3D12_VIEWPORT viewport{0,0,float(width_),float(height_),0,1};list_->RSSetViewports(1,&viewport);}
void captureWater(Frame& frame,ID3D12Resource* source,const D3D12_RECT& rect){
    transition(*list_.Get(),source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);transition(*list_.Get(),depth_.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    for(auto* image:{waterColor_.Get(),waterDepth_.Get()})transition(*list_.Get(),image,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float black[]{0,0,0,0},farDepth[]{1,0,0,1};list_->ClearRenderTargetView(rtv(frameCount+4),black,0,nullptr);list_->ClearRenderTargetView(rtv(frameCount+5),farDepth,0,nullptr);
    const auto first=reserveDescriptors(frame,4);for(UINT i=0;i<4;i++)imageView(frame,first+i,((i<2)==(sampleCount_==1))?(i%2?depth_.Get():source):nullptr,i%2?DXGI_FORMAT_R32_FLOAT:colorFormat(),i>=2);
    const D3D12_CPU_DESCRIPTOR_HANDLE targets[]{rtv(frameCount+4),rtv(frameCount+5)};list_->OMSetRenderTargets(2,targets,FALSE,nullptr);fullViewport();list_->RSSetScissorRects(1,&rect);
    list_->SetGraphicsRootDescriptorTable(13,srvGpu(frame,first));list_->SetPipelineState(waterPipeline(false));list_->DrawInstanced(3,1,0,0);++waterSnapshots_;
    for(auto* image:{waterColor_.Get(),waterDepth_.Get()})transition(*list_.Get(),image,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(*list_.Get(),source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);transition(*list_.Get(),depth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
}
void drawUnderwater(Frame& frame,ID3D12Resource* source,D3D12_CPU_DESCRIPTOR_HANDLE target,const CameraComponent& camera,const ResolvedRenderView& view,const UnderwaterView& water,double seconds,const D3D12_RECT& rect){
    captureWater(frame,source,rect);const auto data=prepareUnderwater(water,camera,view.viewport,seconds);
    list_->SetGraphicsRootConstantBufferView(14,upload(frame,&data,sizeof(data)));list_->SetGraphicsRootDescriptorTable(12,waterBinding(frame));
    list_->OMSetRenderTargets(1,&target,FALSE,nullptr);fullViewport();list_->RSSetScissorRects(1,&rect);list_->SetPipelineState(waterPipeline(true));list_->DrawInstanced(3,1,0,0);++underwaterPasses_;
}
void drawReflection(Frame& frame,const RenderFrame& collected,D3D12_GPU_VIRTUAL_ADDRESS objects,const WaterReflection& reflection,const FrameConstants& constants,const D3D12_RECT& rect,const D3D12_VIEWPORT& viewport){
    auto mirrored=constants;mirrored.viewProjection=constants.reflectionViewProjection;mirrored.reflectionOptions={0,1,0,0};
    const auto eye=transformPoint(reflection.mirror,{constants.cameraEnvironment[0],constants.cameraEnvironment[1],constants.cameraEnvironment[2]});mirrored.cameraEnvironment[0]=eye.x;mirrored.cameraEnvironment[1]=eye.y;mirrored.cameraEnvironment[2]=eye.z;
    list_->SetGraphicsRootConstantBufferView(0,upload(frame,&mirrored,sizeof(mirrored)));list_->SetGraphicsRootDescriptorTable(12,waterBinding(frame,true));
    transition(*list_.Get(),reflectionColor_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);const auto color=rtv(frameCount+6),depth=reflectionDsv();const float clear[]{0,0,0,0};
    list_->ClearRenderTargetView(color,clear,0,nullptr);list_->ClearDepthStencilView(depth,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);list_->OMSetRenderTargets(1,&color,FALSE,&depth);list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&rect);
    reflectionPass_=true;
    for(const auto& batch:collected.batches(RenderLayer::World))if(collected.instanceDraw(batch.firstInstance).material.shader!=MaterialShader::Water)drawBatch(frame,collected,batch,objects);
    reflectionPass_=false;++reflectionPasses_;transition(*list_.Get(),reflectionColor_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
void drawReflectionMesh(Frame& frame,const MeshDraw& mesh){draw(frame,mesh);}
