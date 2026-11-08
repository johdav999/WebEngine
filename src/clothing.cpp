#include "engine/clothing.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace engine {
namespace {
bool identifier(std::string_view id) {
    return !id.empty() && id.size()<=63 && std::all_of(id.begin(),id.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';
    });
}
void colorValid(Vec3 c) {
    for(float v:{c.x,c.y,c.z}) if(!std::isfinite(v)||v<0||v>1)
        throw std::invalid_argument("Clothing color must be finite and in [0,1]");
}
void choicesValid(std::span<const ClothingChoice> choices) {
    if(choices.size()>16) throw std::invalid_argument("Too many clothing choices");
    std::set<std::string> ids;
    for(const auto& c:choices) {
        if(!identifier(c.item)||!ids.insert(c.item).second) throw std::invalid_argument("Invalid/duplicate clothing ID");
        colorValid(c.color);
    }
}
void meshValid(const std::shared_ptr<const Mesh>& mesh,const std::shared_ptr<const Skeleton>& rig) {
    if(!mesh||mesh->skeleton!=rig||mesh->vertices.empty()||mesh->skin.size()!=mesh->vertices.size())
        throw std::invalid_argument("Clothing meshes must use the driver's shared skeleton and skin all vertices");
    if(mesh->indices.empty()||mesh->indices.size()%3||!mesh->lods.empty())
        throw std::invalid_argument("Clothing requires indexed triangles without static LODs");
    for(const auto& part:mesh->parts)
        if(part.firstIndex>mesh->indices.size()||part.indexCount>mesh->indices.size()-part.firstIndex||part.indexCount%3||part.materialIndex>=mesh->materials.size())
            throw std::invalid_argument("Invalid clothing material range");
    for(const auto& v:mesh->vertices) for(float f:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y,v.tangent[0],v.tangent[1],v.tangent[2],v.tangent[3]})
        if(!std::isfinite(f)) throw std::invalid_argument("Nonfinite clothing vertex");
    for(auto i:mesh->indices) if(i>=mesh->vertices.size()) throw std::invalid_argument("Invalid clothing index");
    for(const auto& v:mesh->skin) {
        float sum=0;
        for(std::size_t i=0;i<4;++i) {
            if(v.joints[i]>=rig->bindings.size()||!std::isfinite(v.weights[i])||v.weights[i]<0)
                throw std::invalid_argument("Invalid clothing skin binding/weight");
            sum+=v.weights[i];
        }
        if(std::abs(sum-1)>1e-4f) throw std::invalid_argument("Clothing weights must sum to one");
    }
    mesh->validateMorphTargets();
}
// An ordinary skeletal draw in every renderer, bounds, shadow and picking path.
// The disabled private player never advances; the final driver palette includes IK.
class ClothingMesh final:public SkeletalMeshComponent {
public:
    ClothingMesh(std::string name,std::shared_ptr<const Mesh> mesh,std::shared_ptr<const SkinPose> pose,Vec3 tint)
        :SkeletalMeshComponent(std::move(name),std::move(mesh)),pose_(std::move(pose)) {
        animation().setTickEnabled(false);
        color={1,1,1}; // Garment tint lives in material overrides, not the primitive fallback color.
        const auto count=std::max<std::size_t>(1,this->mesh->materials.size());
        for(std::size_t i=0;i<count;++i) {
            auto material=std::make_shared<Material>(resolvedMaterial(i));
            material->baseColor={material->baseColor.x*tint.x,material->baseColor.y*tint.y,material->baseColor.z*tint.z};
            setMaterial(i,MaterialInstance{material});
        }
    }
    std::shared_ptr<const SkinPose> skinPose() const override { return pose_; }
    void pose(std::shared_ptr<const SkinPose> value) { pose_=std::move(value); }
private:
    std::shared_ptr<const SkinPose> pose_;
};
std::vector<Vec3> restPositions(const Mesh& mesh) {
    std::vector<Vec3> result;result.reserve(mesh.vertices.size());for(auto v:mesh.vertices)result.push_back(v.position);return result;
}
void rigidWorld(const Mat4& world) {
    for(float v:world.m)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite cloth transform");
    const Vec3 x{world.m[0],world.m[1],world.m[2]},y{world.m[4],world.m[5],world.m[6]},z{world.m[8],world.m[9],world.m[10]};
    if(std::abs(dot(x,x)-1)>.001f||std::abs(dot(y,y)-1)>.001f||std::abs(dot(z,z)-1)>.001f
       ||std::abs(dot(x,y))>.001f||std::abs(dot(x,z))>.001f||std::abs(dot(y,z))>.001f||dot(x,cross(y,z))<.999f)
        throw std::invalid_argument("Physical clothing requires a rigid unit-scale driver transform");
}
class ClothingCloth final:public MeshComponent {
public:
    struct Prepared {ClothSimulation simulation;std::shared_ptr<MorphPose> pose;};
    ClothingCloth(const ClothingItem& item,std::shared_ptr<const SkinPose> pose,Vec3 tint,const Mat4& world)
        :MeshComponent(item.id,staticMesh(*item.mesh)),source_(item.mesh),simulation_(*item.cloth,restPositions(*item.mesh)) {
        color={1,1,1};
        for(std::size_t i=0;i<std::max<std::size_t>(1,mesh->materials.size());++i) {
            MaterialInstance material;material.tint=tint;setMaterial(i,std::move(material));
        }
        simulation_.reset(targets(*pose,world));pose_=project(simulation_,world,1);
    }
    std::shared_ptr<const MorphPose> morphPose() const override {return pose_;}
    Prepared prepare(double delta,const SkinPose& pose,const Mat4& world,std::span<const ClothCapsule> capsules,Vec3 acceleration,bool reset,ClothEnvironment environment={}) const {
        auto next=simulation_;auto points=targets(pose,world);
        if(reset)next.reset(points);else next.advance(delta,points,capsules,acceleration,environment);
        auto stream=project(next,world,pose_->revision+1);
        return {std::move(next),std::move(stream)};
    }
    void commit(Prepared&& prepared) noexcept {
        simulation_=std::move(prepared.simulation);
        // Stable stream identity reuses existing GPU buffers; commit only at the
        // simulation boundary, before collection/rendering, as for ordinary morphs.
        pose_->vertices.swap(prepared.pose->vertices);pose_->revision=prepared.pose->revision;
    }
    ClothStats stats() const {return simulation_.stats();}
private:
    std::shared_ptr<const Mesh> source_;
    ClothSimulation simulation_;
    std::shared_ptr<MorphPose> pose_;
    static std::shared_ptr<const Mesh> staticMesh(const Mesh& source) {
        auto mesh=std::make_shared<Mesh>(source);mesh->skin.clear();mesh->skeleton.reset();return mesh;
    }
    std::vector<Vec3> targets(const SkinPose& pose,const Mat4& world) const {
        rigidWorld(world);std::vector<Vec3> result(source_->vertices.size());
        for(std::size_t i=0;i<result.size();++i) {
            const auto& skin=source_->skin[i];Vec3 position{};
            for(std::size_t j=0;j<4;++j)position=position+transformPoint(pose.bones.at(skin.joints[j]).transform,source_->vertices[i].position)*skin.weights[j];
            result[i]=transformPoint(world,position);
        }
        return result;
    }
    std::shared_ptr<MorphPose> project(const ClothSimulation& simulation,const Mat4& world,std::uint64_t revision) const {
        auto result=std::make_shared<MorphPose>();result->vertices=source_->vertices;result->revision=revision;
        auto inverse=inverseAffine(world);const auto positions=simulation.positions();
        std::vector<Vec3> tangents(positions.size()),bitangents(positions.size());
        for(std::size_t i=0;i<positions.size();++i){result->vertices[i].position=transformPoint(inverse,positions[i]);result->vertices[i].normal={};}
        for(std::size_t i=0;i<source_->indices.size();i+=3) {
            const auto a=source_->indices[i],b=source_->indices[i+1],c=source_->indices[i+2];
            auto& va=result->vertices[a];auto& vb=result->vertices[b];auto& vc=result->vertices[c];
            auto e1=vb.position-va.position,e2=vc.position-va.position,n=cross(e1,e2);
            for(auto index:{a,b,c})result->vertices[index].normal=result->vertices[index].normal+n;
            float u1=vb.uv.x-va.uv.x,v1=vb.uv.y-va.uv.y,u2=vc.uv.x-va.uv.x,v2=vc.uv.y-va.uv.y,det=u1*v2-v1*u2;
            if(std::abs(det)>1e-8f)for(auto index:{a,b,c}){tangents[index]=tangents[index]+(e1*v2-e2*v1)*(1/det);bitangents[index]=bitangents[index]+(e2*u1-e1*u2)*(1/det);}
        }
        for(std::size_t i=0;i<positions.size();++i) {
            auto& v=result->vertices[i];v.normal=normalized(v.normal);if(dot(v.normal,v.normal)<.5f)v.normal={0,0,1};
            auto t=normalized(tangents[i]-v.normal*dot(tangents[i],v.normal));
            if(dot(t,t)<.5f)t=normalized(cross(v.normal,std::abs(v.normal.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0}));
            v.tangent={t.x,t.y,t.z,dot(cross(v.normal,t),bitangents[i])<0?-1.f:1.f};
        }
        return result;
    }
};
}
void ClothingCatalog::validate() const {
    if(!skeleton||body.size()>64||items.size()>256) throw std::invalid_argument("Invalid clothing catalog size/rig");
    skeleton->validate();
    std::set<std::string> ids;
    std::uint64_t regions=0;
    for(const auto& b:body) {
        if(!identifier(b.id)||!ids.insert(b.id).second||!b.regions||(regions&b.regions))
            throw std::invalid_argument("Body regions need unique IDs and disjoint nonzero masks");
        regions|=b.regions;colorValid(b.color);meshValid(b.mesh,skeleton);
    }
    ids.clear();
    for(const auto& item:items) {
        if(!identifier(item.id)||!identifier(item.slot)||!ids.insert(item.id).second||(item.hidesRegions&~regions))
            throw std::invalid_argument("Invalid clothing item, slot or region mask");
        meshValid(item.mesh,skeleton);
        if(item.cloth) {
            if(!item.mesh->morphTargets.empty())throw std::invalid_argument("Physical clothing cannot also use authored morph targets");
            item.cloth->validate(restPositions(*item.mesh));
            std::set<std::array<std::uint32_t,3>> faces;
            for(std::size_t i=0;i<item.mesh->indices.size();i+=3){std::array<std::uint32_t,3> face{item.mesh->indices[i],item.mesh->indices[i+1],item.mesh->indices[i+2]};std::sort(face.begin(),face.end());faces.insert(face);}
            for(auto face:item.cloth->surfaceTriangles){std::sort(face.begin(),face.end());if(!faces.contains(face))throw std::invalid_argument("Cloth surface must reference garment triangles");}
        }
    }
}
std::string saveClothingSelection(std::span<const ClothingChoice> choices) {
    choicesValid(choices);
    std::ostringstream out;out.imbue(std::locale::classic());out<<"WECL 1 "<<choices.size()<<'\n'<<std::setprecision(9);
    for(const auto& c:choices) out<<c.item<<' '<<c.color.x<<' '<<c.color.y<<' '<<c.color.z<<'\n';
    return out.str();
}
ClothingSelection loadClothingSelection(std::string_view text) {
    if(text.size()>4096) throw std::invalid_argument("Clothing save too large");
    std::istringstream in{std::string(text)};in.imbue(std::locale::classic());
    std::string magic;unsigned version=0,count=0;
    if(!(in>>magic>>version>>count)||magic!="WECL"||version!=1||count>16) throw std::invalid_argument("Invalid clothing save header");
    ClothingSelection out(count);
    for(auto& c:out) if(!(in>>c.item>>c.color.x>>c.color.y>>c.color.z)) throw std::invalid_argument("Truncated clothing save");
    in>>std::ws;if(!in.eof()) throw std::invalid_argument("Trailing clothing save data");
    choicesValid(out);return out;
}
ClothingInstance::ClothingInstance(ComponentId driver,ClothingCatalog catalog):driver_(driver),catalog_(std::move(catalog)) {
    if(!driver) throw std::invalid_argument("Clothing requires a registered driver");
    catalog_.validate();
}
void ClothingInstance::apply(Scene& scene,ClothingSelection selection) {
    auto* driver=dynamic_cast<SkeletalMeshComponent*>(scene.findComponent(driver_));
    if(!driver||!driver->mesh||driver->mesh->skeleton!=catalog_.skeleton||driver->isPendingRemoval())
        throw std::invalid_argument("Clothing driver is unavailable or uses a different rig");
    driver->requireIdle();
    choicesValid(selection);
    std::set<std::string> slots;
    std::uint64_t hidden=0;
    std::vector<const ClothingItem*> selected;
    for(const auto& choice:selection) {
        auto it=std::find_if(catalog_.items.begin(),catalog_.items.end(),[&](const auto& item){return item.id==choice.item;});
        if(it==catalog_.items.end()||!slots.insert(it->slot).second) throw std::invalid_argument("Unknown item or occupied clothing slot");
        selected.push_back(&*it);hidden|=it->hidesRegions;
    }
    auto next=std::make_unique<Component>("Clothing");next->archiveTransient_=true;
    auto pose=driver->skinPose();
    for(const auto& b:catalog_.body) if(!(b.regions&hidden))
        next->addChild<ClothingMesh>(b.id,b.mesh,pose,b.color);
    for(std::size_t i=0;i<selected.size();++i) {
        if(selected[i]->cloth)next->addChild<ClothingCloth>(*selected[i],pose,selection[i].color,driver->worldMatrix());
        else next->addChild<ClothingMesh>(selected[i]->id,selected[i]->mesh,pose,selection[i].color);
    }
    auto* nextPtr=next.get();
    // At the idle boundary, preflight detach before publishing the replacement.
    auto* old=scene.findComponent(root_);
    if(old && old->parent()!=driver) throw std::logic_error("Clothing root was reparented");
    if(old && old->subtreePending()) throw std::logic_error("Clothing subtree has pending removal; flush first");
    driver->adoptChild(std::move(next));
    if(old) { auto retired=driver->detachChild(*old); }
    root_=nextPtr->id();selection_.swap(selection);
}
bool ClothingInstance::synchronize(Scene& scene) {
    auto* root=scene.findComponent(root_);
    if(!root) return false;
    auto* driver=dynamic_cast<SkeletalMeshComponent*>(scene.findComponent(driver_));
    const bool valid=driver&&!driver->isPendingRemoval()&&driver->mesh&&driver->mesh->skeleton==catalog_.skeleton&&root->parent()==driver;
    root->enabled=valid;
    if(valid) {
        auto pose=driver->skinPose();
        for(const auto& child:root->children()) if(auto* mesh=dynamic_cast<ClothingMesh*>(child.get())) mesh->pose(pose);
    }
    return valid;
}
void ClothingInstance::clear(Scene& scene) {
    if(scene.runtimeIdentity()!=driver_.scene) throw std::invalid_argument("Clothing belongs to another scene");
    if(auto* root=scene.findComponent(root_)) { auto retired=root->parent()->detachChild(*root); }
    root_={};selection_.clear();
}
bool ClothingInstance::advanceCloth(Scene& scene,double delta,std::span<const ClothCapsule> capsules,Vec3 acceleration,ClothEnvironment environment) {
    if(!std::isfinite(delta)||delta<0||delta>.25)throw std::invalid_argument("Invalid cloth frame delta");
    if(auto* root=scene.findComponent(root_))root->requireIdle();
    if(!synchronize(scene))return false;
    auto* driver=dynamic_cast<SkeletalMeshComponent*>(scene.findComponent(driver_));
    if(!driver->isActiveInHierarchy()||delta==0)return true;
    const auto pose=driver->skinPose();const auto world=driver->worldMatrix();
    std::vector<std::pair<ClothingCloth*,ClothingCloth::Prepared>> staged;
    for(auto& child:scene.findComponent(root_)->children())if(auto* cloth=dynamic_cast<ClothingCloth*>(child.get()))
        staged.emplace_back(cloth,cloth->prepare(delta,*pose,world,capsules,acceleration,false,environment));
    for(auto& [cloth,state]:staged)cloth->commit(std::move(state));
    return true;
}
bool ClothingInstance::resetCloth(Scene& scene) {
    if(auto* root=scene.findComponent(root_))root->requireIdle();
    if(!synchronize(scene))return false;
    auto* driver=dynamic_cast<SkeletalMeshComponent*>(scene.findComponent(driver_));
    const auto pose=driver->skinPose();const auto world=driver->worldMatrix();
    std::vector<std::pair<ClothingCloth*,ClothingCloth::Prepared>> staged;
    for(auto& child:scene.findComponent(root_)->children())if(auto* cloth=dynamic_cast<ClothingCloth*>(child.get()))
        staged.emplace_back(cloth,cloth->prepare(0,*pose,world,{}, {},true));
    for(auto& [cloth,state]:staged)cloth->commit(std::move(state));
    return true;
}
ClothStats ClothingInstance::clothStats(const Scene& scene) const {
    ClothStats stats;
    if(auto* root=scene.findComponent(root_))for(auto& child:root->children())if(auto* cloth=dynamic_cast<const ClothingCloth*>(child.get())) {
        auto value=cloth->stats();stats.steps+=value.steps;stats.resets+=value.resets;stats.droppedSeconds+=value.droppedSeconds;
        stats.selfContacts+=value.selfContacts;stats.environmentContacts+=value.environmentContacts;
        stats.surfaceContacts+=value.surfaceContacts;stats.sweptContacts+=value.sweptContacts;
        stats.edgeContacts+=value.edgeContacts;
        stats.aerodynamicFaces+=value.aerodynamicFaces;
        stats.movingContacts+=value.movingContacts;
    }
    return stats;
}
} // namespace engine
