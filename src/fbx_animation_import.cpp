#include "fbx_animation_import.hpp"
#include <limits>

namespace engine {
namespace {
Mat4 matrix(const ufbx_matrix& value) {
    auto result=Mat4::identity();
    for(int c=0;c<4;++c) for(int r=0;r<3;++r) {
        const auto f=static_cast<float>(value.cols[c].v[r]);
        if(!std::isfinite(f)) throw std::runtime_error("Non-finite FBX bone matrix");
        result.m[c*4+r]=f;
    }
    return result;
}
JointPose localPose(const ufbx_matrix& local) {
    const auto t=ufbx_matrix_to_transform(&local);
    JointPose result{{float(t.translation.x),float(t.translation.y),float(t.translation.z)},
        {float(t.scale.x),float(t.scale.y),float(t.scale.z)},{float(t.rotation.x),float(t.rotation.y),float(t.rotation.z),float(t.rotation.w)}};
    const auto rebuilt=result.matrix(),original=matrix(local);
    for(std::size_t i=0;i<16;++i) if(std::abs(rebuilt.m[i]-original.m[i])>0.002f*std::max(1.0f,std::abs(original.m[i])))
        throw std::runtime_error("FBX skeleton contains unsupported sheared local transforms");
    inverseAffine(rebuilt);return result;
}
JointPose pose(const ufbx_node& node) {
    auto local=node.node_to_world;
    if(node.parent) { const auto inverse=ufbx_matrix_invert(&node.parent->node_to_world);local=ufbx_matrix_mul(&inverse,&local); }
    return localPose(local);
}
}
FbxSkinImport::FbxSkinImport(const ufbx_scene& scene,const FbxImportOptions& options,Mesh& result):scene_(scene),options_(options),result_(result) {
    if(scene.skin_deformers.count==0) return;
    skeleton_=std::make_shared<Skeleton>();skeleton_->bindings.push_back({});
    for(const auto* node:scene.nodes) if(node->mesh) {
        addJoint(node);
        if(node->mesh->skin_deformers.count>1) throw std::runtime_error("Multiple skin deformers on one FBX mesh are unsupported");
        for(const auto* skin:node->mesh->skin_deformers) {
            if(skin->skinning_method!=UFBX_SKINNING_METHOD_LINEAR) warn("Dual-quaternion skin converted to four-weight linear skinning");
            for(const auto* cluster:skin->clusters) addJoint(cluster->bone_node);
        }
    }
    // Cluster bind poses can differ from the nodes' default (or first animation) pose.
    std::vector<ufbx_matrix> bindWorld;for(const auto* node:nodes_) bindWorld.push_back(node->node_to_world);
    std::vector<bool> overridden(nodes_.size());
    for(const auto* cluster:scene.skin_clusters) if(cluster->bone_node && joints_.contains(cluster->bone_node)) {
        const auto index=joints_.at(cluster->bone_node);const auto bind=matrix(cluster->bind_to_world);
        if(overridden[index]) {
            const auto previous=matrix(bindWorld[index]);
            for(std::size_t i=0;i<16;++i) if(std::abs(previous.m[i]-bind.m[i])>0.002f*std::max(1.0f,std::abs(bind.m[i])))
                throw std::runtime_error("FBX meshes have inconsistent bind poses for a shared bone");
        }
        bindWorld[index]=cluster->bind_to_world;overridden[index]=true;
    }
    for(std::size_t i=0;i<nodes_.size();++i) {
        auto local=bindWorld[i];const auto parent=skeleton_->joints[i].parent;
        if(parent>=0) {const auto inverse=ufbx_matrix_invert(&bindWorld[parent]);local=ufbx_matrix_mul(&inverse,&local);}
        skeleton_->joints[i].bindPose=localPose(local);
    }
    result_.skeleton=skeleton_;
}
void FbxSkinImport::warn(const std::string& message) {
    if(std::find(result_.warnings.begin(),result_.warnings.end(),message)==result_.warnings.end()) result_.warnings.push_back(message);
}
std::uint32_t FbxSkinImport::addJoint(const ufbx_node* node) {
    if(!node) throw std::runtime_error("FBX skin references a missing bone");
    if(auto it=joints_.find(node);it!=joints_.end()) return it->second;
    const auto parent=node->parent ? static_cast<std::int32_t>(addJoint(node->parent)) : -1;
    if(nodes_.size()>=256) throw std::runtime_error("FBX skeleton exceeds 256 hierarchy joints");
    const auto index=static_cast<std::uint32_t>(nodes_.size());nodes_.push_back(node);joints_.emplace(node,index);
    skeleton_->joints.push_back({std::string(node->name.data,node->name.length),parent,pose(*node)});return index;
}
SkinVertex FbxSkinImport::vertex(const ufbx_node& node,std::uint32_t corner) {
    if(!active()) return {};
    auto [it,created]=bindings_.try_emplace(&node);
    const auto* skin=node.mesh->skin_deformers.count ? node.mesh->skin_deformers.data[0] : nullptr;
    if(created) {
        const auto inverseGeometry=inverseAffine(matrix(node.geometry_to_world));
        auto append=[&](std::uint32_t joint,const Mat4& inverse) {
            if(skeleton_->bindings.size()>=1024) throw std::runtime_error("FBX exceeds 1024 skin bindings");
            it->second.push_back(static_cast<std::uint32_t>(skeleton_->bindings.size()));skeleton_->bindings.push_back({joint,inverse});
        };
        if(skin) for(const auto* cluster:skin->clusters) append(joints_.at(cluster->bone_node),matrix(cluster->geometry_to_bone)*inverseGeometry);
        // Final entry is a rigid mesh-node binding for unweighted/static submeshes.
        append(joints_.at(&node),inverseAffine(matrix(node.node_to_world)));
    }
    SkinVertex result;result.joints[0]=it->second.back();
    const auto control=node.mesh->vertex_indices.data[corner];
    if(!skin || control>=skin->vertices.count || skin->vertices.data[control].num_weights==0) return result;
    const auto& source=skin->vertices.data[control];result.weights={};
    double total=0;std::size_t count=0;std::array<double,4> weights{};
    for(std::size_t i=0;i<source.num_weights;++i) {
        const auto& weight=skin->weights.data[source.weight_begin+i];
        if(!std::isfinite(weight.weight) || weight.weight<0 || weight.cluster_index>=skin->clusters.count) throw std::runtime_error("Invalid FBX skin weight");
        if(weight.weight>0 && count<4) { result.joints[count]=it->second[weight.cluster_index];weights[count]=weight.weight;total+=weight.weight;++count; }
    }
    if(source.num_weights>4) warn("Skin influences reduced to the four strongest weights and normalized");
    if(!std::isfinite(total)) throw std::runtime_error("FBX skin weight sum is non-finite");
    if(total<=0) { result.joints[0]=it->second.back();result.weights={1,0,0,0}; }
    else for(std::size_t i=0;i<4;++i) result.weights[i]=float(weights[i]/total);
    return result;
}
void FbxSkinImport::finish() {
    if(!active()) return;
    if(scene_.anim_stacks.count>64) throw std::runtime_error("FBX exceeds 64 animation clips");
    std::size_t bytes=0;
    for(const auto* stack:scene_.anim_stacks) {
        const auto duration=stack->time_end-stack->time_begin;
        if(!std::isfinite(stack->time_begin) || !std::isfinite(duration) || duration<0) throw std::runtime_error("Invalid FBX animation duration");
        const auto frames=std::max(1.0,std::ceil(duration*options_.animationSampleRate)+1);
        const auto frameBytes=nodes_.size()*sizeof(JointPose);
        if(frames>std::numeric_limits<std::uint32_t>::max() || frames>double((options_.maxAnimationBytes-bytes)/frameBytes))
            throw std::runtime_error("FBX sampled animations exceed the memory limit");
        AnimationClip clip{std::string(stack->name.data,stack->name.length),duration,static_cast<std::uint32_t>(frames),{}};
        clip.frames.reserve(clip.frameCount*nodes_.size());bytes+=clip.frameCount*frameBytes;
        for(std::uint32_t frame=0;frame<clip.frameCount;++frame) {
            const auto time=stack->time_begin+(clip.frameCount>1 ? duration*frame/(clip.frameCount-1) : 0);
            ufbx_evaluate_opts options{};options.temp_allocator.memory_limit=options_.parserMemoryBytes;options.result_allocator.memory_limit=options_.parserMemoryBytes;
            ufbx_error error{};
            std::unique_ptr<ufbx_scene,decltype(&ufbx_free_scene)> evaluated(ufbx_evaluate_scene(&scene_,stack->anim,time,&options,&error),ufbx_free_scene);
            if(!evaluated) throw std::runtime_error("FBX animation evaluation failed");
            for(const auto* node:nodes_) clip.frames.push_back(pose(*evaluated->nodes.data[node->typed_id]));
        }
        skeleton_->clips.push_back(std::move(clip));
    }
    skeleton_->validate();
}
} // namespace engine
