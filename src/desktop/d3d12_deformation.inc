// Private revision-based pose uploads. Morphing remains in MeshComponent;
// the vertex shader skins those vertices with the shared BoneUniform palette.
template<class Source>
void prunePoses(Frame& frame,std::map<const Source*,PoseBuffer<Source>>& cache) {
    for(auto it=cache.begin();it!=cache.end();)if(it->second.source.expired()){
        frame.retained.push_back(it->second.image);it=cache.erase(it);
    }else ++it;
}
template<class Source>
bool poseDirty(const std::shared_ptr<const Source>& pose,const std::map<const Source*,PoseBuffer<Source>>& cache) const {
    const auto it=cache.find(pose.get());return it==cache.end()||it->second.source.lock()!=pose||it->second.revision!=pose->revision;
}
template<class Source>
ID3D12Resource& poseBuffer(Frame& frame,const std::shared_ptr<const Source>& pose,std::map<const Source*,PoseBuffer<Source>>& cache,
    const void* data,std::size_t bytes,D3D12_RESOURCE_STATES state,std::uint64_t& calls,std::uint64_t& uploaded) {
    if(!poseDirty(pose,cache))return *cache.at(pose.get()).image.Get();
    if(!cache.contains(pose.get())&&cache.size()>=4096)throw std::runtime_error("D3D12 pose cache budget exceeded");
    const auto address=upload(frame,data,bytes);auto& gpu=cache[pose.get()];
    if(!gpu.image||gpu.capacity<bytes){
        auto next=buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        if(gpu.image)frame.retained.push_back(gpu.image);gpu.image=std::move(next);gpu.capacity=bytes;
    }else transition(*list_.Get(),gpu.image.Get(),state,D3D12_RESOURCE_STATE_COPY_DEST);
    list_->CopyBufferRegion(gpu.image.Get(),0,frame.upload.Get(),address-frame.upload->GetGPUVirtualAddress(),bytes);
    transition(*list_.Get(),gpu.image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,state);
    gpu.source=pose;gpu.revision=pose->revision;++calls;uploaded+=bytes;return *gpu.image.Get();
}
void validateDeformation() {
    std::map<const Mesh*,UINT> checkedMeshes;
    std::set<const SkinPose*> checkedSkins;std::set<const MorphPose*> checkedMorphs;
    deformationUploadBytes_=0;
    auto reserve=[&](std::size_t bytes){
        if(!bytes||bytes>64*1024*1024||deformationUploadBytes_>64*1024*1024-bytes)
            throw std::invalid_argument("D3D12 deformation upload budget exceeded");
        deformationUploadBytes_+=bytes+256;
    };
    for(const auto& draw:preflight_.meshes){
        if(draw.skinPose){
            const auto& pose=draw.skinPose;
            if(pose->bones.empty()||pose->bones.size()>1024)throw std::invalid_argument("Invalid D3D12 bone palette");
            const auto& mesh=draw.mesh;
            if(mesh->skin.size()!=mesh->vertices.size())throw std::invalid_argument("Skin weights must cover every vertex");
            auto [where,inserted]=checkedMeshes.try_emplace(mesh.get(),0);
            if(inserted){
                const auto cached=meshes_.find(mesh.get());
                if(cached!=meshes_.end()&&cached->second.source.lock()==mesh)where->second=cached->second.maxJoint;
                else for(const auto& skin:mesh->skin)for(UINT i=0;i<4;++i){
                    if(!std::isfinite(skin.weights[i]))throw std::invalid_argument("Nonfinite D3D12 skin weight");
                    where->second=std::max(where->second,skin.joints[i]);
                }
            }
            if(where->second>=pose->bones.size())throw std::invalid_argument("Skin joint outside D3D12 palette");
            if(checkedSkins.insert(pose.get()).second&&poseDirty(pose,skins_)){
                for(const auto& bone:pose->bones)for(const auto& matrix:{bone.transform,bone.normal})
                    for(float value:matrix.m)if(!std::isfinite(value))throw std::invalid_argument("Nonfinite D3D12 bone matrix");
                reserve(pose->bones.size()*sizeof(BoneUniform));
            }
        }
        if(draw.morphPose){
            const auto& pose=draw.morphPose;
            if(pose->vertices.empty()||pose->vertices.size()!=draw.mesh->vertices.size())throw std::invalid_argument("Morph vertices must match source mesh");
            if(checkedMorphs.insert(pose.get()).second&&poseDirty(pose,morphs_)){
                for(const auto& v:pose->vertices)for(float value:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y,v.tangent[0],v.tangent[1],v.tangent[2],v.tangent[3]})
                    if(!std::isfinite(value))throw std::invalid_argument("Nonfinite D3D12 morph vertex");
                reserve(pose->vertices.size()*sizeof(Vertex));
            }
        }
    }
}
