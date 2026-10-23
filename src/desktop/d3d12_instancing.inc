// RenderFrame owns the 256-byte ObjectUniform slots and their dirty ranges.
struct InstanceStream {ComPtr<ID3D12Resource> image;std::size_t capacity=0;};
std::array<InstanceStream,32> instanceStreams_;
std::size_t instanceUploadBudget_=0;
std::uint64_t submittedInstances_=0,instancedDraws_=0,maxInstances_=0,objectUploadBytes_=0;
D3D12_GPU_VIRTUAL_ADDRESS instanceObjects(Frame& frame,RenderFrame& collected,std::size_t view){
    collected.prepareObjects();const auto data=collected.objectData();if(data.empty())return 0;
    auto& gpu=instanceStreams_[view];const bool grow=!gpu.image||gpu.capacity<data.size();
    if(grow){auto next=buffer(data.size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);if(gpu.image)frame.retained.push_back(gpu.image);gpu.image=std::move(next);gpu.capacity=data.size();}
    else if(!collected.objectUploads().empty())transition(*list_.Get(),gpu.image.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto copy=[&](std::size_t offset,std::size_t bytes){const auto address=upload(frame,data.data()+offset,bytes);list_->CopyBufferRegion(gpu.image.Get(),offset,frame.upload.Get(),address-frame.upload->GetGPUVirtualAddress(),bytes);objectUploadBytes_+=bytes;};
    if(grow)copy(0,data.size());else for(const auto& range:collected.objectUploads())copy(range.offset,range.bytes);
    if(grow||!collected.objectUploads().empty())transition(*list_.Get(),gpu.image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return gpu.image->GetGPUVirtualAddress();
}
void drawBatch(Frame& frame,const RenderFrame& collected,const InstanceBatch& batch,D3D12_GPU_VIRTUAL_ADDRESS objects,bool shadow=false,const ShadowBatch* selected=nullptr){
    auto source=collected.instanceDraw(batch.firstInstance);
    if(selected){source.mesh=selected->mesh;source.firstIndex=selected->firstIndex;source.indexCount=selected->indexCount;}
    draw(frame,source,shadow,batch.instanceCount,objects+std::size_t(batch.firstInstance)*RenderFrame::objectStride);
}
