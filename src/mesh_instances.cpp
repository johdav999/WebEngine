#include "engine/mesh_instances.hpp"
#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include <set>

namespace engine {
namespace {
void require(bool ok,const char* text) { if(!ok)throw std::invalid_argument(text); }
bool finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
void include(MeshBounds& b,Vec3 p) {
    require(finite(p),"Nonfinite mesh instance bounds");
    if(!b.valid){b={p,p,true};return;}
    b.minimum={std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};
    b.maximum={std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};
}
void staticMesh(const Mesh& m) {
    require(!m.vertices.empty()&&m.vertices.size()<=1048576&&!m.indices.empty()&&m.indices.size()%3==0,"Invalid bulk mesh geometry");
    require(!m.skeleton&&m.skin.empty()&&m.morphTargets.empty(),"Bulk mesh instances require static source geometry");
    for(const auto& v:m.vertices)require(finite(v.position)&&finite(v.normal),"Nonfinite bulk mesh vertex");
    for(auto i:m.indices)require(i<m.vertices.size(),"Bulk mesh index outside vertices");
    for(const auto& part:m.parts)require(std::uint64_t(part.firstIndex)+part.indexCount<=m.indices.size()&&part.indexCount%3==0&&part.materialIndex<m.materials.size(),"Invalid bulk mesh material range");
    for(const auto& material:m.materials) {
        require(bool(material),"Missing bulk mesh material");material->validate();
        require(material->alphaMode==AlphaMode::Opaque||material->alphaMode==AlphaMode::Mask,"Bulk instances require opaque/masked materials");
        require(material->shader!=MaterialShader::Water,"Bulk instances do not support water materials");
    }
}
MeshBounds paddedBounds(MeshBounds bounds,const Mat4& normal,const MeshWindState* state) {
    if(bounds.valid&&state&&state->amplitude()>0) {
        const auto& p=state->parameters();const auto direction=Vec3{p[0],p[1],p[2]}*p[3];
        const Vec3 local{normal.m[0]*direction.x+normal.m[1]*direction.y+normal.m[2]*direction.z,
            normal.m[4]*direction.x+normal.m[5]*direction.y+normal.m[6]*direction.z,
            normal.m[8]*direction.x+normal.m[9]*direction.y+normal.m[10]*direction.z};
        const Vec3 margin{std::abs(local.x),std::abs(local.y),std::abs(local.z)};bounds.minimum=bounds.minimum-margin;bounds.maximum=bounds.maximum+margin;
    }
    return bounds;
}
}
void MeshWindSettings::validate() const {
    require(std::isfinite(direction.x)&&std::isfinite(direction.y)&&std::abs(direction.x)<=10000&&std::abs(direction.y)<=10000&&std::isfinite(amplitude)&&amplitude>=0&&amplitude<=10&&std::isfinite(frequency)&&frequency>=0&&frequency<=20&&std::isfinite(gust)&&gust>=0&&gust<=1,"Invalid mesh wind settings");
}
void MeshWindState::configure(double seconds,const MeshWindSettings& settings) {
    settings.validate();require(std::isfinite(seconds)&&seconds>=0&&seconds<=1e12,"Invalid mesh wind time");
    const float length=std::hypot(settings.direction.x,settings.direction.y);
    const double angle=seconds*settings.frequency*2*double(pi);
    const std::array<float,8> next{length>0?settings.direction.x/length:0,0,length>0?settings.direction.y/length:0,length>0?settings.amplitude:0,
        float(std::remainder(angle,2*double(pi))),float(std::remainder(angle*.37,2*double(pi))),settings.gust,0};
    if(parameters_!=next){parameters_=next;++revision_;}
}
Vec3 MeshWindState::displacement(float phase) const {
    const auto& p=parameters_;const float amount=p[3]*std::sin(p[4]+phase)*(1-p[6]+p[6]*.5f*(1+std::sin(p[5]+phase*1.37f)));
    return Vec3{p[0],p[1],p[2]}*amount;
}
std::shared_ptr<const MeshBend> MeshBend::create(std::shared_ptr<const Mesh> mesh,std::span<const float> weights) {
    require(bool(mesh),"Missing bend mesh");staticMesh(*mesh);
    require(weights.size()==mesh->vertices.size(),"Bend weights must cover all mesh vertices");
    for(float w:weights)require(std::isfinite(w)&&w>=0&&w<=1,"Invalid mesh bend weight");
    auto out=std::shared_ptr<MeshBend>(new MeshBend);out->mesh_=std::move(mesh);out->vertices_.resize(weights.size());
    std::vector<Vec3> gradients(weights.size());std::vector<float> areas(weights.size());
    for(std::size_t i=0;i<out->mesh_->indices.size();i+=3) {
        auto a=out->mesh_->indices[i],b=out->mesh_->indices[i+1],c=out->mesh_->indices[i+2];
        const auto u=out->mesh_->vertices[b].position-out->mesh_->vertices[a].position,v=out->mesh_->vertices[c].position-out->mesh_->vertices[a].position;
        const auto n=cross(u,v);const float squared=dot(n,n);if(squared<=1e-16f)continue;
        const float area=std::sqrt(squared);
        const auto gradient=(cross(v,n)*(weights[b]-weights[a])+cross(n,u)*(weights[c]-weights[a]))*(1/squared);
        require(finite(gradient),"Nonfinite mesh bend gradient");
        for(auto index:{a,b,c}){gradients[index]=gradients[index]+gradient*area;areas[index]+=area;}
    }
    for(std::size_t i=0;i<weights.size();++i){const auto g=areas[i]>0?gradients[i]*(1/areas[i]):Vec3{};require(finite(g),"Nonfinite mesh bend gradient");out->vertices_[i]={weights[i],g.x,g.y,g.z};}
    return out;
}
std::shared_ptr<const MeshInstanceSet> MeshInstanceSet::create(std::shared_ptr<const Mesh> mesh,std::vector<MeshInstance> values) {
    require(bool(mesh)&&values.size()<=65536,"Invalid mesh instance set");staticMesh(*mesh);
    auto out=std::shared_ptr<MeshInstanceSet>(new MeshInstanceSet);out->mesh_=std::move(mesh);out->values_=std::move(values);
    out->models_.reserve(out->values_.size());out->normals_.reserve(out->values_.size());std::set<std::uint64_t> keys;
    const auto bounds=out->mesh_->localBounds();
    for(const auto& v:out->values_) {
        require(v.key&&keys.insert(v.key).second&&std::isfinite(v.phase)&&finite(v.color)&&v.color.x>=0&&v.color.y>=0&&v.color.z>=0&&v.color.x<=16&&v.color.y<=16&&v.color.z<=16,"Invalid mesh instance key/color/phase");
        require(finite(v.transform.position)&&finite(v.transform.rotation)&&finite(v.transform.scale),"Nonfinite mesh instance transform");
        const auto model=v.transform.matrix(),normal=transpose(inverseAffine(model));
        for(float f:model.m)require(std::isfinite(f),"Nonfinite mesh instance matrix");
        for(float f:normal.m)require(std::isfinite(f),"Nonfinite mesh instance normal matrix");
        out->models_.push_back(model);out->normals_.push_back(normal);
        if(bounds.valid)for(int i=0;i<8;++i)include(out->bounds_,transformPoint(model,{i&1?bounds.maximum.x:bounds.minimum.x,i&2?bounds.maximum.y:bounds.minimum.y,i&4?bounds.maximum.z:bounds.minimum.z}));
    }
    return out;
}
MeshBounds meshWindBounds(MeshBounds bounds,const Mat4& model,const MeshWindState* state) {
    return state&&state->amplitude()>0?paddedBounds(bounds,transpose(inverseAffine(model)),state):bounds;
}
MeshInstancesComponent::MeshInstancesComponent(std::string name,std::shared_ptr<const Mesh> source):MeshComponent(std::move(name),std::move(source)) {
    require(bool(mesh),"Missing mesh instance source");staticMesh(*mesh);archiveTransient_=true;allowInstanceReordering=true;receivesDecals=false;
}
void MeshInstancesComponent::setInstances(std::shared_ptr<const MeshInstanceSet> value) {
    require(!value||value->mesh()==mesh,"Mesh instance set/source mismatch");instances_=std::move(value);
}
void MeshInstancesComponent::setBend(std::shared_ptr<const MeshBend> bend,std::shared_ptr<const MeshWindState> wind) {
    require((!bend&&!wind)||(bend&&wind&&bend->mesh()==mesh),"Mesh bend/source/state mismatch");bend_=std::move(bend);wind_=std::move(wind);
}
void MeshInstancesComponent::appendDraws(SceneFrame& frame,const Mat4& world,const Frustum* frustum,bool retainOffCamera) const {
    if(!instances_)return;require(instances_->mesh()==mesh&&(!bend_||bend_->mesh()==mesh),"Bulk mesh source replaced without its instance data");
    lodPolicy.validate();require(blend==BlendMode::Opaque&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid bulk mesh blending");
    std::vector<Material> materials;const auto slots=std::max<std::size_t>(1,mesh->materials.size());materials.reserve(slots);
    for(std::size_t i=0;i<slots;++i){auto m=resolvedMaterial(i,false);require((m.alphaMode==AlphaMode::Opaque||m.alphaMode==AlphaMode::Mask)&&m.shader!=MaterialShader::Water,"Unsupported bulk material override");materials.push_back(std::move(m));}
    const auto cellBounds=meshWindBounds(instances_->bounds(),world,wind_.get());
    const bool cellVisible=!frustum||!frustumCulling||!cellBounds.valid||frustum->intersectsTransformedAabb(cellBounds.minimum,cellBounds.maximum,world);
    const auto parentNormal=transpose(inverseAffine(world));
    const bool identity=world.m==Mat4::identity().m;
    for(std::size_t i=0;i<instances_->values().size();++i) {
        const auto& instance=instances_->values()[i];if(!instance.visible)continue;
        const auto model=identity?instances_->model(i):world*instances_->model(i),normal=identity?instances_->normal(i):parentNormal*instances_->normal(i);
        const auto bounds=paddedBounds(mesh->localBounds(),normal,wind_.get());
        const bool visible=cellVisible&&(!frustum||!frustumCulling||!bounds.valid||frustum->intersectsTransformedAabb(bounds.minimum,bounds.maximum,model));
        ++frame.meshCount;frame.culledMeshes+=!visible;
        if(!visible&&!retainOffCamera)continue;
        std::uint32_t part=0;
        auto append=[&](std::size_t slot,std::uint32_t first,std::uint32_t count) {
            require(slot<materials.size()&&std::uint64_t(first)+count<=mesh->indices.size()&&count%3==0,"Invalid bulk mesh material range");
            auto material=materials[slot];const Vec3 tint{color.x*instance.color.x*material.baseColor.x,color.y*instance.color.y*material.baseColor.y,color.z*instance.color.z*material.baseColor.z};
            const Vec3 x{model.m[0],model.m[1],model.m[2]},y{model.m[4],model.m[5],model.m[6]},z{model.m[8],model.m[9],model.m[10]};
            MeshDraw draw{mesh,model,tint,layer,material.baseColorTexture,first,count,normal,{},BlendMode::Opaque,opacity*material.opacity,std::move(material),dot(x,cross(y,z))<0,
                castsShadow&&instance.castsShadow&&layer==RenderLayer::World,receivesShadow&&layer==RenderLayer::World,visible,{},true,mesh,bounds,id(),part++,lodPolicy,false,visibilityMask};
            draw.bend=bend_;draw.wind=wind_;draw.windPhase=instance.phase;draw.instanceKey=instance.key;
            const auto l=std::size_t(layer);require(l<frame.layerDraws.size()&&frame.meshes.size()<std::numeric_limits<std::uint32_t>::max(),"Bulk draw budget/layer exceeded");
            const auto mc=frame.meshes.capacity(),lc=frame.layerDraws[l].capacity();frame.layerDraws[l].push_back(std::uint32_t(frame.meshes.size()));frame.meshes.push_back(std::move(draw));frame.storageGrowths+=(mc!=frame.meshes.capacity())+(lc!=frame.layerDraws[l].capacity());
        };
        if(mesh->parts.empty())append(0,0,std::uint32_t(mesh->indices.size()));else for(const auto& p:mesh->parts)append(p.materialIndex,p.firstIndex,p.indexCount);
    }
}
} // namespace engine
