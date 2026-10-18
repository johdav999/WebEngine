#include "engine/selection.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/physics.hpp"

namespace engine {
namespace {
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
void validateQuery(const SelectionQuery& query) {
    if(query.geometry!=SelectionGeometry::Bounds&&query.geometry!=SelectionGeometry::Triangles)
        throw std::invalid_argument("Invalid selection geometry mode");
    if(!std::isfinite(query.maxDistance)||query.maxDistance<=0||!std::isfinite(query.minDistance)
        ||query.minDistance<0||query.minDistance>query.maxDistance) throw std::invalid_argument("Invalid selection distance range");
}
Vec3 direction(const Mat4& m,Vec3 v) {
    return {m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z,m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z,m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z};
}
std::optional<float> intersect(Ray ray,const MeshBounds& bounds,const Mat4& model,float minimum,float maximum) {
    if(!bounds.valid) return {};
    Mat4 local;
    try { local=inverse(model); } catch(const std::invalid_argument&) { return {}; } // Collapsed/nonfinite objects cannot be picked.
    const auto o=transformPoint(local,ray.origin),d=direction(local,ray.direction);
    if(!finite(o)||!finite(d)||!finite(bounds.minimum)||!finite(bounds.maximum)) return {};
    const float origin[]={o.x,o.y,o.z},delta[]={d.x,d.y,d.z};
    const float low[]={bounds.minimum.x,bounds.minimum.y,bounds.minimum.z},high[]={bounds.maximum.x,bounds.maximum.y,bounds.maximum.z};
    double near=minimum,far=maximum;
    for(int axis=0;axis<3;++axis) {
        if(low[axis]>high[axis]) return {};
        if(delta[axis]==0) { if(origin[axis]<low[axis]||origin[axis]>high[axis]) return {};continue; }
        double a=(double(low[axis])-origin[axis])/delta[axis],b=(double(high[axis])-origin[axis])/delta[axis];
        if(a>b) std::swap(a,b);
        near=std::max(near,a);far=std::min(far,b);
        if(near>far) return {};
    }
    return static_cast<float>(near);
}
bool accepts(const InteractionComponent& provider,SelectionKind kind) {
    return provider.isActiveInHierarchy() && (kind==SelectionKind::Selection ? provider.selectable : provider.focusable);
}
struct SurfaceHit {
    float distance=0;
    std::optional<std::uint32_t> triangle;
    Vec3 normal{},barycentric{};
    Vec2 uv{};
};
struct PreciseVector { double x,y,z; };
PreciseVector precise(Vec3 p) { return {p.x,p.y,p.z}; }
PreciseVector operator-(PreciseVector a,PreciseVector b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
PreciseVector cross(PreciseVector a,PreciseVector b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
double dot(PreciseVector a,PreciseVector b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
// Original CPU geometry, morphed before skinning exactly as in localBounds/rendering.
// Broad-phase bounds avoid deforming/testing triangles on unrelated surfaces.
std::optional<SurfaceHit> meshHit(Ray ray,const MeshComponent& component,const Mat4& model,
    const SelectionQuery& query,SelectionGeometry geometry) {
    const auto* skeletal=dynamic_cast<const SkeletalMeshComponent*>(&component);
    const auto pose=skeletal ? skeletal->skinPose() : nullptr;
    const auto broad=intersect(ray,component.localBounds(pose),model,query.minDistance,query.maxDistance);
    if(!broad) return {};
    if(geometry==SelectionGeometry::Bounds) return SurfaceHit{*broad,{}, {}, {}, {}};
    const auto& mesh=*component.mesh;
    const auto morph=component.morphPose();
    const auto& vertices=morph ? morph->vertices : mesh.vertices;
    std::vector<Vec3> positions;positions.reserve(vertices.size());
    for(std::size_t i=0;i<vertices.size();++i) {
        auto p=vertices[i].position;
        if(pose) {
            p={};const auto& skin=mesh.skin[i];
            for(std::size_t j=0;j<4;++j)
                p=p+transformPoint(pose->bones.at(skin.joints[j]).transform,vertices[i].position)*skin.weights[j];
        }
        p=transformPoint(model,p);
        if(!finite(p)) throw std::invalid_argument("Nonfinite picking geometry");
        positions.push_back(p);
    }
    std::optional<SurfaceHit> best;
    const auto origin=precise(ray.origin),direction=precise(ray.direction);
    const auto range=[&](std::size_t first,std::size_t count) {
        if(first>mesh.indices.size()||count>mesh.indices.size()-first||first%3||count%3)
            throw std::invalid_argument("Invalid picking triangle range");
        for(std::size_t index=first;index<first+count;index+=3) {
            const auto ia=mesh.indices[index],ib=mesh.indices[index+1],ic=mesh.indices[index+2];
            if(ia>=positions.size()||ib>=positions.size()||ic>=positions.size())
                throw std::invalid_argument("Picking index outside vertices");
            const auto a=precise(positions[ia]),e1=precise(positions[ib])-a,e2=precise(positions[ic])-a;
            const auto p=cross(direction,e2);const double determinant=dot(e1,p);
            const double scale=std::sqrt(dot(e1,e1)*dot(e2,e2));
            if(scale==0||std::abs(determinant)<=1e-8*scale) continue; // Two-sided, including mirrored meshes.
            const auto delta=origin-a;const auto q=cross(delta,e1);
            const double u=dot(delta,p)/determinant,v=dot(direction,q)/determinant;
            if(u<0||v<0||u+v>1) continue;
            const double t=dot(e2,q)/determinant;
            if(!std::isfinite(t)||t<query.minDistance||t>query.maxDistance||(best&&t>=best->distance)) continue;
            const float w=float(1-u-v),fu=float(u),fv=float(v);
            const auto uvA=vertices[ia].uv,uvB=vertices[ib].uv,uvC=vertices[ic].uv;
            const auto n=cross(e1,e2);const double normalLength=std::hypot(n.x,n.y,n.z);
            const Vec3 normal{float(n.x/normalLength),float(n.y/normalLength),float(n.z/normalLength)};
            best=SurfaceHit{float(t),static_cast<std::uint32_t>(index/3),normal,{w,fu,fv},
                {uvA.x*w+uvB.x*fu+uvC.x*fv,uvA.y*w+uvB.y*fu+uvC.y*fv}};
        }
    };
    if(mesh.parts.empty()) range(0,mesh.indices.size());
    else for(const auto& part:mesh.parts) range(part.firstIndex,part.indexCount);
    return best;
}
}
bool SelectionSystem::valid(ActorId id,SelectionKind kind) const {
    const auto* actor=scene_.findActor(id);
    if(!actor||!actor->isActiveInHierarchy()) return false;
    for(const auto* provider:actor->getComponents<InteractionComponent>()) if(accepts(*provider,kind)) return true;
    return false;
}
ActorId SelectionSystem::selected() const { return valid(selected_,SelectionKind::Selection) ? selected_ : ActorId{}; }
ActorId SelectionSystem::focused() const { return valid(focused_,SelectionKind::Focus) ? focused_ : ActorId{}; }
bool SelectionSystem::set(ActorId id,SelectionKind kind,SelectionReason reason) {
    if(!id) id={};
    if(id&&!valid(id,kind)) return false;
    auto& current=kind==SelectionKind::Selection ? selected_ : focused_;
    if(id==current) return true;
    const auto previous=current;current=id;
    changes_.push_back({kind,previous,id,reason,++sequence_});publish();return true;
}
bool SelectionSystem::select(ActorId id,SelectionReason reason) { return set(id,SelectionKind::Selection,reason); }
bool SelectionSystem::focus(ActorId id,SelectionReason reason) { return set(id,SelectionKind::Focus,reason); }
void SelectionSystem::clear(SelectionReason reason) { focus({},reason);select({},reason); }
void SelectionSystem::refresh() {
    if(selected_&&!valid(selected_,SelectionKind::Selection)) select({},SelectionReason::Invalidated);
    if(focused_&&!valid(focused_,SelectionKind::Focus)) focus({},SelectionReason::Invalidated);
}
SelectionSystem::ListenerId SelectionSystem::subscribe(std::function<void(const SelectionChange&)> listener) {
    if(!listener) throw std::invalid_argument("Empty selection listener");
    const auto id=nextListener_++;listeners_.emplace(id,std::move(listener));return id;
}
bool SelectionSystem::unsubscribe(ListenerId id) { return listeners_.erase(id)!=0; }
void SelectionSystem::publish() {
    if(publishing_) return;
    publishing_=true;Node::TraversalGuard guard(scene_);
    try {
        while(!changes_.empty()) {
            const auto change=changes_.front();changes_.pop_front();
            std::vector<ListenerId> ids;
            for(const auto& [id,callback]:listeners_) { (void)callback;ids.push_back(id); }
            for(auto id:ids) if(const auto found=listeners_.find(id);found!=listeners_.end()) {
                auto callback=found->second;callback(change);
            }
            // A listener may have destroyed or disabled the current target.
            refresh();
        }
    } catch(...) { publishing_=false;throw; }
    publishing_=false;
}
std::optional<SelectionHit> SelectionSystem::resolveRay(Ray ray,const SelectionQuery& query,SelectionKind kind) {
    validateQuery(query);
    const double length=std::hypot(double(ray.direction.x),double(ray.direction.y),double(ray.direction.z));
    if(!finite(ray.origin)||!finite(ray.direction)||length<=0)
        throw std::invalid_argument("Invalid selection ray");
    ray.direction={float(ray.direction.x/length),float(ray.direction.y/length),float(ray.direction.z/length)};
    std::optional<SelectionHit> hit;
    std::optional<float> blockerDistance;ActorId blockerActor;
    const auto consider=[&](const InteractionComponent& provider,ComponentId surface,const SurfaceHit& value) {
        if(!hit||value.distance<hit->distance) hit=SelectionHit{provider.owner()->id(),provider.id(),surface,
            ray.origin+ray.direction*value.distance,value.distance,value.triangle,value.normal,value.barycentric,value.uv};
    };
    // Reuse the render transform (including physics interpolation), original
    // geometry and animated bounds. LOD and shadow detail do not affect picking.
    scene_.visit([&](const Node& node,const Mat4& authoritativeWorld) {
        const auto* physics=scene_.physicsIfCreated();
        const auto model=physics ? physics->renderMatrix(node,authoritativeWorld) : authoritativeWorld;
        if(const auto* mesh=dynamic_cast<const MeshComponent*>(&node);mesh&&mesh->mesh
            &&mesh->opacity>0&&(mesh->visibilityMask&query.visibilityMask)) {
            std::vector<const InteractionComponent*> providers;
            if(mesh->owner()) for(const auto* provider:mesh->owner()->getComponents<InteractionComponent>()) {
                if(!accepts(*provider,kind)||provider->proxyBounds||!(provider->selectionMask&query.selectionMask)) continue;
                providers.push_back(provider);
            }
            const bool blocks=query.occlusion&&(mesh->visibilityMask&query.occluderMask);
            if(!providers.empty()||blocks) {
                const auto value=meshHit(ray,*mesh,model,query,blocks ? SelectionGeometry::Triangles : query.geometry);
                if(blocks&&value&&(!blockerDistance||value->distance<*blockerDistance)) {
                    blockerDistance=value->distance;blockerActor=mesh->owner() ? mesh->owner()->id() : ActorId{};
                }
                const auto target=blocks&&query.geometry==SelectionGeometry::Bounds&&!providers.empty()
                    ? meshHit(ray,*mesh,model,query,SelectionGeometry::Bounds) : value;
                if(target) for(const auto* provider:providers) consider(*provider,mesh->id(),*target);
            }
        }
        const auto* provider=dynamic_cast<const InteractionComponent*>(&node);
        if(!provider||!provider->owner()||!accepts(*provider,kind)||!provider->proxyBounds||!(provider->selectionMask&query.selectionMask)) return;
        provider->validate();
        if(const auto distance=intersect(ray,*provider->proxyBounds,model,query.minDistance,query.maxDistance))
            consider(*provider,provider->id(),SurfaceHit{*distance,{}, {}, {}, {}});
    });
    if(hit&&blockerDistance&&blockerActor!=hit->actor&&*blockerDistance<hit->distance) return {};
    return hit;
}
std::optional<SelectionHit> SelectionSystem::resolvePointer(Vec2 pixel,const CameraComponent& camera,PixelViewport viewport,const SelectionQuery& query,SelectionKind kind) {
    validateQuery(query);
    if(!std::isfinite(pixel.x)||!std::isfinite(pixel.y)||!std::isfinite(viewport.x)||!std::isfinite(viewport.y)
        ||!std::isfinite(viewport.width)||!std::isfinite(viewport.height)||viewport.width<=0||viewport.height<=0)
        throw std::invalid_argument("Invalid selection pointer viewport");
    if(!camera.active()|| (camera.scene()&&camera.scene()!=&scene_) ||pixel.x<viewport.x||pixel.y<viewport.y
        ||pixel.x>=viewport.x+viewport.width||pixel.y>=viewport.y+viewport.height) return {};
    auto masked=query;masked.visibilityMask&=camera.visibilityMask();
    const auto ray=camera.screenPointToRay(pixel,viewport);
    const auto near=camera.screenToWorld(pixel,0,viewport),far=camera.screenToWorld(pixel,1,viewport);
    masked.minDistance=std::max(query.minDistance,std::max(0.0f,dot(near-ray.origin,ray.direction)));
    masked.maxDistance=std::min(query.maxDistance,dot(far-ray.origin,ray.direction));
    if(masked.minDistance>masked.maxDistance) return {};
    return resolveRay(ray,masked,kind);
}
void SelectionSystem::processInput(const InputSystem& input,const CameraComponent& camera,PixelViewport viewport,const SelectionQuery& query) {
    refresh();
    const auto updateFocus=[&] {
        const auto hit=pointer_ ? resolvePointer(*pointer_,camera,viewport,query,SelectionKind::Focus) : std::nullopt;
        focus(hit ? hit->actor : ActorId{},SelectionReason::Pointer);
    };
    for(const auto& event:input.events()) {
        const auto type=static_cast<InputEventType>(int(event.type));
        if(type==InputEventType::PointerDown||type==InputEventType::PointerMove||type==InputEventType::PointerUp) {
            pointer_=Vec2{float(event.pixelX),float(event.pixelY)};pointerId_=static_cast<std::int32_t>(event.id);updateFocus();
        } else if(type==InputEventType::CancelAll || (type==InputEventType::PointerCancel&&event.id==pointerId_)) {
            pointer_.reset();focus({},SelectionReason::Cancelled);
        } else if((type==InputEventType::ActionValue||type==InputEventType::ActionDelta)&&event.id==InputActions::Select&&(event.x!=0||event.y!=0)) {
            // Selection has its own filter, independent of focusable targets.
            const auto hit=pointer_ ? resolvePointer(*pointer_,camera,viewport,query) : std::nullopt;
            select(hit ? hit->actor : ActorId{},SelectionReason::Pointer);
        }
    }
    if(pointer_) updateFocus(); // Re-pick a stationary pointer when camera/objects move.
}
ActionEligibility SelectionSystem::eligibility(const InteractionAction& action,const InteractionContext& context) const {
    if(!action.execute) return {false,"Action is not bound"};
    if(context.instigator) {
        const auto* actor=scene_.findActor(context.instigator);
        if(!actor||!actor->isActiveInHierarchy()) return {false,"Instigator is unavailable"};
    }
    try { return action.eligibility ? action.eligibility(context) : ActionEligibility{}; }
    catch(const std::exception& error) { return {false,error.what()}; }
    catch(...) { return {false,"Eligibility check failed"}; }
}
std::vector<AvailableAction> SelectionSystem::availableActions(ActorId target,ActorId instigator) {
    Node::TraversalGuard guard(scene_);
    std::vector<AvailableAction> result;
    const auto* actor=scene_.findActor(target);
    if(!actor||!actor->isActiveInHierarchy()) return result;
    std::vector<ComponentId> providers;
    for(const auto* provider:actor->getComponents<InteractionComponent>()) providers.push_back(provider->id());
    for(auto id:providers) {
        const auto* provider=dynamic_cast<const InteractionComponent*>(scene_.findComponent(id));
        if(!provider||!provider->isActiveInHierarchy()) continue;
        const auto revision=provider->actionRevision();const auto actions=provider->actions();
        for(const auto& action:actions) {
            auto eligible=eligibility(action,{scene_,target,instigator});
            if(!provider->isActiveInHierarchy()||provider->actionRevision()!=revision) eligible={false,"Action provider changed"};
            result.push_back({{target,id,action.id,revision},action.label,std::move(eligible)});
        }
    }
    refresh();return result;
}
ActionResult SelectionSystem::execute(const ActionHandle& handle,ActorId instigator) {
    Node::TraversalGuard guard(scene_);
    auto* provider=dynamic_cast<InteractionComponent*>(scene_.findComponent(handle.provider));
    const auto validProvider=[&] {
        return provider&&provider->isActiveInHierarchy()&&provider->owner()&&provider->owner()->id()==handle.target&&provider->actionRevision()==handle.revision;
    };
    if(!validProvider()) return {ActionStatus::Unavailable,"Action provider is unavailable or changed"};
    const auto found=std::find_if(provider->actions().begin(),provider->actions().end(),[&](const auto& action) { return action.id==handle.id; });
    if(found==provider->actions().end()) return {ActionStatus::Unavailable,"Unknown action"};
    const auto action=*found; // A callback may remove itself or destroy its provider.
    const InteractionContext context{scene_,handle.target,instigator};
    const auto eligible=eligibility(action,context);
    if(!validProvider()) { refresh();return {ActionStatus::Unavailable,"Action provider changed during eligibility check"}; }
    if(!eligible.allowed) { refresh();return {ActionStatus::Ineligible,eligible.reason}; }
    if(instigator) {
        const auto* actor=scene_.findActor(instigator);
        if(!actor||!actor->isActiveInHierarchy()) { refresh();return {ActionStatus::Ineligible,"Instigator changed during eligibility check"}; }
    }
    ActionResult result;
    try { result=action.execute(context); }
    catch(const std::exception& error) { result={ActionStatus::Failed,error.what()}; }
    catch(...) { result={ActionStatus::Failed,"Action execution failed"}; }
    refresh();return result;
}
} // namespace engine
