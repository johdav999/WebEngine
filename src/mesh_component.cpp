#include "engine/mesh_component.hpp"

namespace engine {
void MeshComponent::synchronizeMorphs() const {
    // An expired weak mesh and a newly cleared mesh both compare equal to null.
    // Clear the instance state first so that cannot retain a stale deformation.
    if(!mesh) {morphWeights_.clear();morphPose_.reset();morphMesh_.reset();morphDirty_=true;return;}
    if(morphMesh_.lock()==mesh) return;
    if(mesh) mesh->validateMorphTargets();
    std::vector<float> weights;
    if(mesh) for(const auto& target:mesh->morphTargets) weights.push_back(target.defaultWeight);
    morphWeights_=std::move(weights);morphMesh_=mesh;morphPose_.reset();morphDirty_=true;
}
void MeshComponent::setMorphWeight(std::size_t target,float weight) {
    synchronizeMorphs();
    if(target>=morphWeights_.size()) throw std::out_of_range("Morph target does not exist");
    if(!std::isfinite(weight)) throw std::invalid_argument("Morph weight must be finite");
    if(morphWeights_[target]!=weight) { morphWeights_[target]=weight;morphDirty_=true; }
}
void MeshComponent::setMorphWeight(std::string_view target,float weight) {
    synchronizeMorphs();
    if(mesh) for(std::size_t i=0;i<mesh->morphTargets.size();++i)
        if(mesh->morphTargets[i].name==target) { setMorphWeight(i,weight);return; }
    throw std::out_of_range("Morph target does not exist");
}
float MeshComponent::morphWeight(std::size_t target) const {
    synchronizeMorphs();return morphWeights_.at(target);
}
void MeshComponent::resetMorphWeights() {
    synchronizeMorphs();
    if(mesh) for(std::size_t i=0;i<morphWeights_.size();++i) setMorphWeight(i,mesh->morphTargets[i].defaultWeight);
}
std::shared_ptr<const MorphPose> MeshComponent::morphPose() const {
    synchronizeMorphs();
    if(std::none_of(morphWeights_.begin(),morphWeights_.end(),[](float w){return w!=0;})) return {};
    if(!morphDirty_) return morphPose_;
    auto vertices=mesh->vertices;
    for(std::size_t target=0;target<morphWeights_.size();++target) {
        const auto weight=morphWeights_[target];if(weight==0) continue;
        const auto& offsets=mesh->morphTargets[target];
        for(std::size_t i=0;i<vertices.size();++i) {
            auto& v=vertices[i];v.position=v.position+offsets.positions[i]*weight;
            if(!offsets.normals.empty()) v.normal=v.normal+offsets.normals[i]*weight;
            if(!offsets.tangents.empty()) {
                const auto t=offsets.tangents[i]*weight;v.tangent[0]+=t.x;v.tangent[1]+=t.y;v.tangent[2]+=t.z;
            }
        }
    }
    for(std::size_t i=0;i<vertices.size();++i) {
        auto& v=vertices[i];
        auto finite=[](Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
        Vec3 t{v.tangent[0],v.tangent[1],v.tangent[2]};
        if(!finite(v.position)||!finite(v.normal)||!finite(t)) throw std::invalid_argument("Morph deformation exceeds finite vertex range");
        v.normal=normalized(v.normal);
        if(dot(v.normal,v.normal)<0.5f) {v.normal=normalized(mesh->vertices[i].normal);if(dot(v.normal,v.normal)<0.5f) v.normal={0,1,0};}
        t=normalized(t-v.normal*dot(v.normal,t));
        if(dot(t,t)<0.5f) t=normalized(cross(std::abs(v.normal.y)<0.9f ? Vec3{0,1,0} : Vec3{0,0,1},v.normal));
        v.tangent={t.x,t.y,t.z,v.tangent[3]};
    }
    if(!morphPose_) morphPose_=std::make_shared<MorphPose>();
    morphPose_->vertices.swap(vertices);++morphPose_->revision;morphDirty_=false;return morphPose_;
}
MeshBounds MeshComponent::geometryBounds(const std::shared_ptr<const SkinPose>& pose) const {
    if(!mesh) return {};
    const auto morph=morphPose();const auto morphRevision=morph ? morph->revision : 0;
    if(!pose&&!morph) return mesh->localBounds();
    const auto revision=pose ? pose->revision : 0;
    if(boundsMesh_.lock()==mesh && boundsPose_.lock()==pose && boundsMorph_.lock()==morph && boundsRevision_==revision && boundsMorphRevision_==morphRevision) return animatedBounds_;
    if(pose&&mesh->skin.size()!=mesh->vertices.size()) throw std::invalid_argument("Skeletal mesh requires skin weights for every vertex");
    const auto& vertices=morph ? morph->vertices : mesh->vertices;
    MeshBounds next;
    for(std::size_t i=0;i<mesh->vertices.size();++i) {
        Vec3 p=vertices[i].position;
        if(pose) {
            p={};const auto& skin=mesh->skin[i];
            for(std::size_t j=0;j<4;++j) {
                if(skin.joints[j]>=pose->bones.size()) throw std::invalid_argument("Skin joint outside bone palette");
                p=p+transformPoint(pose->bones[skin.joints[j]].transform,vertices[i].position)*skin.weights[j];
            }
        }
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) { next.valid=false;break; }
        if(!next.valid) { next.minimum=next.maximum=p;next.valid=true; }
        else {
            next.minimum={std::min(next.minimum.x,p.x),std::min(next.minimum.y,p.y),std::min(next.minimum.z,p.z)};
            next.maximum={std::max(next.maximum.x,p.x),std::max(next.maximum.y,p.y),std::max(next.maximum.z,p.z)};
        }
    }
    animatedBounds_=next;boundsMesh_=mesh;boundsPose_=pose;boundsMorph_=morph;boundsRevision_=revision;boundsMorphRevision_=morphRevision;return next;
}
MeshBounds MeshComponent::localBounds(const std::shared_ptr<const SkinPose>& pose) const {
    auto bounds=geometryBounds(pose);
    if(!mesh||!bounds.valid) return bounds;
    float margin=0;
    for(std::size_t slot=0;slot<std::max<std::size_t>(1,mesh->materials.size());++slot) {
        const auto m=resolvedMaterial(slot,false);
        if(m.shader==MaterialShader::Water) margin=std::max(margin,m.water.amplitude);
    }
    bounds.minimum=bounds.minimum-Vec3{margin,margin,margin};
    bounds.maximum=bounds.maximum+Vec3{margin,margin,margin};return bounds;
}
void MeshComponent::setMaterial(std::size_t slot,MaterialInstance instance) {
    const auto count=mesh ? std::max<std::size_t>(mesh->materials.size(),1) : 0;
    if(slot>=count) throw std::out_of_range("Material slot does not exist");
    const auto fallback=slot<mesh->materials.size() ? mesh->materials[slot] : Material::standard();
    instance.resolve(fallback); // Validate before replacing the existing override.
    if(materialOverrides.size()<=slot) materialOverrides.resize(slot+1);
    materialOverrides[slot]=std::move(instance);
}
void MeshComponent::clearMaterial(std::size_t slot) {
    if(slot<materialOverrides.size()) materialOverrides[slot].reset();
}
Material MeshComponent::resolvedMaterial(std::size_t slot,bool metadata) const {
    const auto fallback=mesh&&slot<mesh->materials.size()&&mesh->materials[slot] ? mesh->materials[slot] : Material::standard();
    auto result=slot<materialOverrides.size()&&materialOverrides[slot] ? materialOverrides[slot]->resolve(fallback,metadata) : metadata ? *fallback : fallback->parameters();
    const bool slotTexture=slot<materialOverrides.size()&&materialOverrides[slot]&&(materialOverrides[slot]->material||materialOverrides[slot]->baseColorTexture);
    if(texture&&!slotTexture) result.baseColorTexture=texture;
    if(blend!=BlendMode::Opaque) result.alphaMode=blend==BlendMode::Alpha ? AlphaMode::Blend : AlphaMode::Additive;
    result.validate();return result;
}
void MeshComponent::loadTexture(std::span<const std::byte> bytes,const TextureLimits& limits) {
    auto candidate=Texture::loadMemory(bytes,"Mesh texture",limits);
    auto overrides=materialOverrides;
    for(auto& instance:overrides) if(instance) instance->baseColorTexture=candidate;
    texture=std::move(candidate);materialOverrides=std::move(overrides);
}
MeshComponent::MeshComponent(std::string name, std::shared_ptr<const Mesh> mesh)
    : Component(std::move(name)), mesh(std::move(mesh)) {}
MeshComponent::~MeshComponent() = default;
void MeshComponent::loadGlb(std::span<const std::byte> data,const GlbImportOptions& options) {
    mesh=GlbImporter::loadMemory(data,options);materialOverrides.clear();
}
void MeshComponent::loadFbx(std::span<const std::byte> data,const FbxImportOptions& options) {
    mesh=FbxImporter::loadMemory(data,options);
    materialOverrides.clear();
}
#ifndef __EMSCRIPTEN__
void MeshComponent::loadGlbFile(const std::filesystem::path& path,const GlbImportOptions& options) {
    mesh=GlbImporter::loadFile(path,options);materialOverrides.clear();
}
void MeshComponent::loadFbxFile(const std::filesystem::path& path,const FbxImportOptions& options) {
    mesh=FbxImporter::loadFile(path,options);
    materialOverrides.clear();
}
#endif
} // namespace engine
