// The renderer owns one GPU target per registered surface. Each frame context
// retains the targets referenced by its RTVs and shader-visible descriptors.
std::shared_ptr<SurfaceTarget> ensureSurfaceTarget(const RenderSurfaceHandle& source) {
    if(auto it=surfaceTargets_.find(source.get());it!=surfaceTargets_.end())return it->second;
    auto next=std::make_shared<SurfaceTarget>();
    next->source=source;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=source->width();
    desc.Height=source->height();
    desc.DepthOrArraySize=desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
    desc.SampleDesc.Count=1;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const auto properties=heap(D3D12_HEAP_TYPE_DEFAULT);
    auto result=device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&next->color));
    if(result==E_OUTOFMEMORY)return {};
    checked(result,"Create render surface color");

    desc.Format=DXGI_FORMAT_D32_FLOAT;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    D3D12_CLEAR_VALUE clear{};
    clear.Format=desc.Format;
    clear.DepthStencil.Depth=1;
    result=device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&next->depth));
    if(result==E_OUTOFMEMORY)return {};
    checked(result,"Create render surface depth");

    D3D12_DESCRIPTOR_HEAP_DESC descriptors{};
    descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    descriptors.NumDescriptors=1;
    result=device_->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&next->rtv));
    if(result==E_OUTOFMEMORY)return {};
    checked(result,"Create render surface RTV heap");
    descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    result=device_->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&next->dsv));
    if(result==E_OUTOFMEMORY)return {};
    checked(result,"Create render surface DSV heap");

    // Scene shaders encode SDR into the UNORM RTV. The sampled view decodes it.
    D3D12_RENDER_TARGET_VIEW_DESC colorView{};
    colorView.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    colorView.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(next->color.Get(),&colorView,next->rtv->GetCPUDescriptorHandleForHeapStart());
    device_->CreateDepthStencilView(next->depth.Get(),nullptr,next->dsv->GetCPUDescriptorHandleForHeapStart());
    return surfaceTargets_.emplace(source.get(),std::move(next)).first->second;
}

std::shared_ptr<SurfaceTarget> sampledSurface(const Material& material,bool shadow) const {
    if(surfacePass_||shadow||reflectionPass_||!material.baseColorSurface)return {};
    const auto it=surfaceTargets_.find(material.baseColorSurface.get());
    if(it==surfaceTargets_.end())return {};
    const auto& target=it->second;
    if(target->captured||std::find(recordedSurfaces_.begin(),recordedSurfaces_.end(),target.get())!=recordedSurfaces_.end())
        return target;
    return {};
}
