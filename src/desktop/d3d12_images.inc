// Private D3D12Graphics resource helpers. Included inside the implementation class.
D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(Frame& frame,UINT slot) const {
    auto h=frame.descriptors->GetCPUDescriptorHandleForHeapStart();h.ptr+=std::size_t(slot)*srvStride_;return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(Frame& frame,UINT slot) const {
    auto h=frame.descriptors->GetGPUDescriptorHandleForHeapStart();h.ptr+=std::size_t(slot)*srvStride_;return h;
}
UINT reserveDescriptors(Frame& frame,UINT count) {
    if(count>descriptorCount-frame.descriptorUsed)throw std::runtime_error("Direct D3D12 descriptor budget exceeded");
    const auto slot=frame.descriptorUsed;frame.descriptorUsed+=count;return slot;
}
ComPtr<ID3D12Resource> uploadImage(Frame& frame,const D3D12_RESOURCE_DESC& desc,
                                  const std::vector<std::vector<unsigned char>>& pixels) {
    const UINT count=UINT(desc.MipLevels)*desc.DepthOrArraySize;
    if(pixels.size()!=count)throw std::invalid_argument("Incomplete D3D12 image payload");
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(count);UINT64 bytes=0;
    std::vector<UINT> rows(count);std::vector<UINT64> rowBytes(count);
    device_->GetCopyableFootprints(&desc,0,count,0,layouts.data(),rows.data(),rowBytes.data(),&bytes);
    if(bytes>256*1024*1024)throw std::runtime_error("Direct D3D12 image upload exceeds 256 MiB");
    auto properties=heap(D3D12_HEAP_TYPE_DEFAULT);ComPtr<ID3D12Resource> image;
    checked(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&image)),"Create image");
    auto staging=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    unsigned char* mapped=nullptr;D3D12_RANGE empty{};
    checked(staging->Map(0,&empty,reinterpret_cast<void**>(&mapped)),"Map image upload");
    for(UINT i=0;i<count;++i){
        const auto& layout=layouts[i];const std::size_t row=std::size_t(rowBytes[i]);
        if(pixels[i].size()!=row*rows[i]){staging->Unmap(0,nullptr);throw std::invalid_argument("Invalid image subresource size");}
        for(UINT y=0;y<rows[i];++y)
            std::memcpy(mapped+layout.Offset+std::size_t(y)*layout.Footprint.RowPitch,pixels[i].data()+y*row,row);
    }
    staging->Unmap(0,nullptr);
    for(UINT i=0;i<count;++i){
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=staging.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=layouts[i];
        to.pResource=image.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.SubresourceIndex=i;list_->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    }
    transition(*list_.Get(),image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    frame.retained.push_back(std::move(staging));return image;
}
UINT textureSlot(Frame& frame,std::shared_ptr<const Texture> source,TextureSemantic semantic,bool rgbaOnly=false) {
    if(!source)source=Texture::white();const TextureKey key{source.get(),semantic,rgbaOnly};
    if(const auto it=frame.textureSlots.find(key);it!=frame.textureSlots.end())return it->second;
    auto found=textures_.find(key);
    if(found==textures_.end()){
        if(textures_.size()>=4096)throw std::runtime_error("Direct D3D12 texture cache budget exceeded");
        const CompressedTexture* compressed=nullptr;
        if(!rgbaOnly)for(const auto& variant:source->compressed){variant.validate(source->width,source->height);if(!compressed&&quality_.compressedTextures&&capabilities_.compressionBC&&variant.format==TextureCompression::BC7&&variant.semantic==semantic)compressed=&variant;}
        const auto fallback=compressed?std::vector<TextureMip>{}:source->mipChain(semantic);const auto& mips=compressed?compressed->levels:fallback;std::vector<std::vector<unsigned char>> pixels;pixels.reserve(mips.size());
        for(const auto& mip:mips)pixels.push_back(mip.pixels);
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=source->width;desc.Height=source->height;
        desc.DepthOrArraySize=1;desc.MipLevels=UINT16(mips.size());desc.Format=compressed?DXGI_FORMAT_BC7_TYPELESS:DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;
        GpuTexture next;next.source=source;next.compressed=compressed!=nullptr;
        next.format=compressed?(semantic==TextureSemantic::Color?DXGI_FORMAT_BC7_UNORM_SRGB:DXGI_FORMAT_BC7_UNORM):(semantic==TextureSemantic::Color?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM);
        for(const auto& mip:mips)next.bytes+=mip.pixels.size();next.image=uploadImage(frame,desc,pixels);textureUploadBytes_+=next.bytes;found=textures_.emplace(key,std::move(next)).first;
    }
    const auto slot=reserveDescriptors(frame,1);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=found->second.format;
    view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=found->second.image->GetDesc().MipLevels;
    device_->CreateShaderResourceView(found->second.image.Get(),&view,srvCpu(frame,slot));frame.textureSlots.emplace(key,slot);return slot;
}
D3D12_GPU_DESCRIPTOR_HANDLE texture(Frame& frame,std::shared_ptr<const Texture> source,bool srgb,bool rgbaOnly=false) {
    return srvGpu(frame,textureSlot(frame,std::move(source),srgb?TextureSemantic::Color:TextureSemantic::Data,rgbaOnly));
}
std::array<std::shared_ptr<const Texture>,9> drawTextures(const MeshDraw& draw) const {
    auto sources=materialTextures(draw.material);
    if(!draw.material.layers.enabled&&!draw.material.baseColorTexture&&draw.texture)sources[0]=draw.texture;
    return sources;
}
D3D12_GPU_DESCRIPTOR_HANDLE materialBinding(Frame& frame,const MeshDraw& draw) {
    const auto sources=drawTextures(draw);MaterialKey key;key.second=draw.material.layers.enabled;
    for(UINT i=0;i<9;++i)key.first[i]=sources[i].get();
    if(auto it=frame.materialSlots.find(key);it!=frame.materialSlots.end())return srvGpu(frame,it->second);
    const auto first=reserveDescriptors(frame,9);
    const auto semantics=materialTextureSemantics(draw.material);
    for(UINT i=0;i<9;++i){
        textureSlot(frame,sources[i],semantics[i]); // Ensure upload/cache publication first.
        const auto& gpu=textures_.at({sources[i].get(),semantics[i],false});const auto& image=gpu.image;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=gpu.format;
        view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=image->GetDesc().MipLevels;
        device_->CreateShaderResourceView(image.Get(),&view,srvCpu(frame,first+i));
    }
    frame.materialSlots.emplace(key,first);return srvGpu(frame,first);
}
D3D12_GPU_DESCRIPTOR_HANDLE environmentBinding(Frame& frame) {
    static const auto fallback=Environment::studio();const auto source=scene_->environment?scene_->environment:fallback;
    if(environment_.source.lock()!=source){
        GpuEnvironment next;next.source=source;next.levels=UINT(source->specular.size());
        auto halfPixels=[](const std::array<float,4>* data,std::size_t count){
            std::vector<unsigned char> bytes(count*8);for(std::size_t i=0;i<count;++i)for(UINT c=0;c<4;++c){const auto h=floatToHalf(data[i][c]);std::memcpy(bytes.data()+i*8+c*2,&h,2);}return bytes;
        };
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;desc.DepthOrArraySize=6;
        desc.Width=desc.Height=source->specular.front().size;desc.MipLevels=UINT16(next.levels);
        std::vector<std::vector<unsigned char>> pixels;
        for(UINT face=0;face<6;++face)for(const auto& level:source->specular){const std::size_t count=std::size_t(level.size)*level.size;pixels.push_back(halfPixels(level.pixels.data()+face*count,count));}
        next.images[0]=uploadImage(frame,desc,pixels);
        desc.Width=desc.Height=source->diffuse.size;desc.MipLevels=1;pixels.clear();
        const auto count=std::size_t(source->diffuse.size)*source->diffuse.size;
        for(UINT face=0;face<6;++face)pixels.push_back(halfPixels(source->diffuse.pixels.data()+face*count,count));
        next.images[1]=uploadImage(frame,desc,pixels);
        desc.Width=desc.Height=source->lutSize;desc.DepthOrArraySize=1;pixels={halfPixels(source->brdf.data(),source->brdf.size())};next.images[2]=uploadImage(frame,desc,pixels);
        for(auto& image:environment_.images)if(image)frame.retained.push_back(std::move(image));environment_=std::move(next);
    }
    const auto first=reserveDescriptors(frame,3);
    for(UINT i=0;i<3;++i){D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if(i<2){view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;view.TextureCube.MipLevels=i==0?environment_.levels:1;}
        else {view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;}
        device_->CreateShaderResourceView(environment_.images[i].Get(),&view,srvCpu(frame,first+i));}
    return srvGpu(frame,first);
}
void prepareShadowTargets() {
    const UINT resolution=quality_.shadowResolution,layers=std::max(1u,quality_.maxShadowMaps);
    if(resolution==shadowResolution_&&layers==shadowLayers_)return;idle();
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=desc.Height=resolution;desc.DepthOrArraySize=UINT16(layers);desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    auto properties=heap(D3D12_HEAP_TYPE_DEFAULT);D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
    ComPtr<ID3D12Resource> next;checked(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&next)),"Create shadow array");
    shadowMaps_=std::move(next);shadowResolution_=resolution;shadowLayers_=layers;
    for(UINT i=0;i<layers;++i){D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.FirstArraySlice=i;view.Texture2DArray.ArraySize=1;
        auto handle=dsv_->GetCPUDescriptorHandleForHeapStart();handle.ptr+=(i+1)*dsvStride_;device_->CreateDepthStencilView(shadowMaps_.Get(),&view,handle);}
}
D3D12_GPU_DESCRIPTOR_HANDLE shadowBinding(Frame& frame) {
    const auto slot=reserveDescriptors(frame,1);D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_FLOAT;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2DArray.MipLevels=1;view.Texture2DArray.ArraySize=shadowLayers_;
    device_->CreateShaderResourceView(shadowMaps_.Get(),&view,srvCpu(frame,slot));return srvGpu(frame,slot);
}
