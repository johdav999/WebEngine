#include "engine/editor.hpp"
#include "engine/vehicle_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/character_motor_component.hpp"
#include <algorithm>

namespace engine {
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool same(const Transform& a,const Transform& b) {
    return a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z
        &&a.rotation.x==b.rotation.x&&a.rotation.y==b.rotation.y&&a.rotation.z==b.rotation.z
        &&a.scale.x==b.scale.x&&a.scale.y==b.scale.y&&a.scale.z==b.scale.z;
}
Ray checked(Ray ray) {
    const double length=std::hypot(double(ray.direction.x),double(ray.direction.y),double(ray.direction.z));
    if(!finite(ray.origin)||!finite(ray.direction)||length==0) throw std::invalid_argument("Invalid editor ray");
    ray.direction=ray.direction*float(1/length);return ray;
}
Mat4 translation(Vec3 p) { auto m=Mat4::identity();m.m[12]=p.x;m.m[13]=p.y;m.m[14]=p.z;return m; }
Mat4 rotation(Vec3 a,double angle) {
    const float c=float(std::cos(angle)),s=float(std::sin(angle)),t=1-c;auto m=Mat4::identity();
    m.m[0]=t*a.x*a.x+c;m.m[4]=t*a.x*a.y-s*a.z;m.m[8]=t*a.x*a.z+s*a.y;
    m.m[1]=t*a.x*a.y+s*a.z;m.m[5]=t*a.y*a.y+c;m.m[9]=t*a.y*a.z-s*a.x;
    m.m[2]=t*a.x*a.z-s*a.y;m.m[6]=t*a.y*a.z+s*a.x;m.m[10]=t*a.z*a.z+c;return m;
}
Transform physicsPose(const Node& node) {
    Node pose;pose.setWorldTransform(node.worldMatrix());auto result=pose.transform;
    // Rotation decomposition can produce 0.99999994 rather than exactly 1.
    // Reject authored scaling, then supply the solver's exact unit-scale contract.
    if(std::abs(result.scale.x-1)>1e-4f||std::abs(result.scale.y-1)>1e-4f||std::abs(result.scale.z-1)>1e-4f)
        throw std::invalid_argument("Physics actors require unit world scale");
    result.scale={1,1,1};return result;
}
std::optional<float> boundsHit(Ray ray,const MeshBounds& bounds,const Mat4& model,float near,float far) {
    if(!bounds.valid) return {};
    Mat4 inverse;try { inverse=inverseAffine(model); } catch(const std::invalid_argument&) { return {}; }
    const auto p=transformPoint(inverse,ray.origin),d=transformDirection(inverse,ray.direction);
    const float origins[]={p.x,p.y,p.z},directions[]={d.x,d.y,d.z};
    const float low[]={bounds.minimum.x,bounds.minimum.y,bounds.minimum.z},high[]={bounds.maximum.x,bounds.maximum.y,bounds.maximum.z};
    for(int i=0;i<3;++i) {
        if(directions[i]==0) { if(origins[i]<low[i]||origins[i]>high[i]) return {};continue; }
        float a=(low[i]-origins[i])/directions[i],b=(high[i]-origins[i])/directions[i];
        if(a>b) std::swap(a,b);near=std::max(near,a);far=std::min(far,b);if(near>far) return {};
    }
    return near;
}
}
EditorSession::EditorSession(Scene& scene,std::size_t historyLimit,AssetLibrary* assets,std::shared_ptr<const EditorPropertyRegistry> properties)
    :scene_(scene),limit_(historyLimit),assets_(assets ? assets : &ownedAssets_),properties_(std::move(properties)) {
    if(!properties_) throw std::invalid_argument("Missing editor property registry");
    if(!limit_||limit_>10000) throw std::invalid_argument("Invalid editor history limit");
}
EditorSession::~EditorSession() { try { cancel(); } catch(...) {} }
Node* EditorSession::selected() const { return selection_.empty() ? nullptr : scene_.findSavedNode(selection_); }
bool EditorSession::select(std::string_view id) {
    if(!id.empty()&&!scene_.findSavedNode(id)) return false;
    cancel();selection_=id;selections_=id.empty()?std::vector<std::string>{}:std::vector<std::string>{std::string(id)};return true;
}
std::string EditorSession::readOnlyReason(const Node& node) const {
    if(node.scene()!=&scene_||node.isPendingRemoval()) return "Node is no longer in this document";
    if(node.archiveTransient())return "Resident region content is managed by its stream";
    if(dynamic_cast<const RegionManifestComponent*>(&node)||(dynamic_cast<const Actor*>(&node)&&static_cast<const Actor&>(node).getComponent<RegionManifestComponent>()))return "Region manifests use world bounds; keep their transform identity";
    if(&node==&scene_) return "Scene root transform is read-only";
    for(const Node* p=&node;p;p=p->parent())if(p->isProtectedChild()) return "This implementation child is managed by its owning component";
    if(dynamic_cast<const RouteComponent*>(&node))return "Position the owning actor; route geometry has identity local transform";
    if(dynamic_cast<const VehicleComponent*>(&node)||dynamic_cast<const RigidBodyComponent*>(&node)) return "Position the owning actor; rigid body transforms must be identity";
    if(dynamic_cast<const CharacterMotorComponent*>(&node)) return "Position the owning actor; motor transforms must be identity";
    return {};
}
std::string EditorSession::pick(Ray ray,std::uint32_t mask,float near,float far) const {
    ray=checked(ray);
    if(!std::isfinite(near)||!std::isfinite(far)||near<0||far<near) throw std::invalid_argument("Invalid editor picking range");
    auto frame=scene_.collect(mask);std::string result;float closest=far;
    for(const auto& draw:frame.meshes) {
        if(draw.layer!=RenderLayer::World||draw.opacity<=0) continue;
        const auto* mesh=dynamic_cast<const MeshComponent*>(scene_.findComponent(draw.component));
        if(!mesh) continue;
        if(auto hit=boundsHit(ray,mesh->localBounds(draw.skinPose),draw.model,near,closest);hit&&(*hit<closest||result.empty())) {
            closest=*hit;result=mesh->savedId();
        }
    }
    return result;
}
Node* EditorSession::resolve(const Change& change,const Transform& expected) const {
    auto* node=scene_.findSavedNode(change.target);
    if(!node||!readOnlyReason(*node).empty()||!node->parent()||node->parent()->savedId()!=change.parent
        ||node->parent()->worldMatrix().m!=change.parentWorld.m||!same(node->transform,expected)) return nullptr;
    return node;
}
void EditorSession::apply(Node& node,const Transform& value) {
    if(!finite(value.position)||!finite(value.rotation)||!finite(value.scale)) throw std::invalid_argument("Transform values must be finite");
    const auto before=node.transform;
    auto* actor=dynamic_cast<Actor*>(&node);
    auto* body=actor ? actor->getComponent<RigidBodyComponent>() : nullptr;
    if(body&&(value.scale.x!=1||value.scale.y!=1||value.scale.z!=1)) throw std::invalid_argument("Physics actors require unit scale");
    try {
        node.setLocalTransform(value);
        // Reject invalid component-specific transforms before reconciliation.
        node.visit([](const Node& child,const Mat4&) { if(auto* component=dynamic_cast<const Component*>(&child)) component->validate(); });
        if(body&&body->hasBody()) {
            body->teleport(physicsPose(node),false);
            node.transform=value; // Keep exact authored Euler values across teleport decomposition.
        }
        scene_.synchronize();
    } catch(...) {
        node.transform=before;
        if(body&&body->hasBody()) {
            body->teleport(physicsPose(node),false);node.transform=before;
        }
        throw;
    }
}
bool EditorSession::beginTransform() {
    if(transforming()) return false;
    auto roots=selectionRoots();if(roots.empty())return false;
    Transaction tx;
    for(auto* node:roots) {
        if(!readOnlyReason(*node).empty()||!node->parent())return false;
        tx.changes.push_back({node->savedId(),node->parent()->savedId(),node->parent()->worldMatrix(),node->transform,node->transform});
        tx.worlds.push_back(node->worldMatrix());
    }
    tx.change=tx.changes.front();tx.world=tx.worlds.front();transaction_=std::move(tx);return true;
}
bool EditorSession::previewTransform(const Transform& value) {
    if(!transaction_||transaction_->changes.size()!=1) return false;
    auto& change=transaction_->changes.front();auto* node=resolve(change,change.after);
    if(!node) {transaction_.reset();return false;}
    apply(*node,value);change.after=node->transform;transaction_->change=change;return true;
}
bool EditorSession::setTransform(const Transform& value) {
    if(selectionIds().size()!=1)return false;
    if(!beginTransform()) return false;
    try { if(!previewTransform(value)) return false;return commit(); }
    catch(...) { cancel();throw; }
}
std::optional<Vec3> EditorSession::sample(Ray ray) const {
    ray=checked(ray);const auto& tx=*transaction_;const auto delta=tx.pivot-ray.origin;
    if(tx.mode!=EditorGizmo::Rotate) {
        const double b=dot(tx.axis,ray.direction),denominator=1-b*b;
        if(denominator<1e-8) return {};
        const double distance=(dot(delta,ray.direction)-b*dot(delta,tx.axis))/denominator;
        if(distance<0||!std::isfinite(distance)) return {};
        auto point=tx.pivot+tx.axis*float(b*distance-dot(delta,tx.axis));return finite(point) ? std::optional<Vec3>(point) : std::nullopt;
    }
    const double denominator=dot(ray.direction,tx.axis);if(std::abs(denominator)<1e-7) return {};
    const double distance=dot(delta,tx.axis)/denominator;if(distance<0||!std::isfinite(distance)) return {};
    auto point=ray.origin+ray.direction*float(distance)-tx.pivot;
    if(!finite(point)||dot(point,point)<1e-12f) return {};return normalized(point);
}
bool EditorSession::beginGizmo(EditorGizmo mode,int axis,Ray ray) {
    if(axis<0||axis>(mode==EditorGizmo::Scale?3:2)||mode<EditorGizmo::Move||mode>EditorGizmo::Scale)return false;
    ray=checked(ray);if(!beginTransform())return false;
    auto& tx=*transaction_;tx.mode=mode;tx.uniform=axis==3;
    tx.axis=axis==1?Vec3{0,1,0}:axis==2?Vec3{0,0,1}:Vec3{1,0,0};
    if(mode==EditorGizmo::Scale&&!tx.uniform)tx.axis=normalized(transformDirection(tx.world,tx.axis));
    tx.pivot=selectionPivot();
    tx.scaleLength=std::max(.5f,std::sqrt(dot(tx.pivot-ray.origin,tx.pivot-ray.origin))*.12f);
    auto first=sample(ray);if(!first){transaction_.reset();return false;}
    tx.first=tx.previous=*first;tx.gizmo=true;return true;
}
bool EditorSession::dragGizmo(Ray ray) {
    if(!transaction_||!transaction_->gizmo)return false;
    auto point=sample(ray);if(!point)return false;auto& tx=*transaction_;
    for(const auto& change:tx.changes)if(!resolve(change,change.after)){cancel();return false;}
    Mat4 delta=Mat4::identity();double angle=tx.angle;
    if(tx.mode==EditorGizmo::Move) {
        auto move=*point-tx.first;
        if(snap_.translation){const auto coordinate=dot(tx.pivot+move,tx.axis);move=tx.axis*(std::round(coordinate/snap_.grid)*snap_.grid-dot(tx.pivot,tx.axis));}
        delta=translation(move);
    } else if(tx.mode==EditorGizmo::Rotate) {
        angle+=std::atan2(double(dot(tx.axis,cross(tx.previous,*point))),double(dot(tx.previous,*point)));
        auto applied=snap_.rotation?std::round(angle/snap_.angle)*snap_.angle:angle;
        delta=translation(tx.pivot)*rotation(tx.axis,applied)*translation(tx.pivot*(-1));
    } else {
        const float factor=std::exp(std::clamp(dot(*point-tx.first,tx.axis)/tx.scaleLength,-4.6f,4.6f));
        auto scale=Mat4::identity();
        if(tx.uniform){scale.m[0]=scale.m[5]=scale.m[10]=factor;}
        else {const float v[]={tx.axis.x,tx.axis.y,tx.axis.z};for(int x=0;x<3;++x)for(int y=0;y<3;++y)scale.m[x*4+y]+=(factor-1)*v[x]*v[y];}
        delta=translation(tx.pivot)*scale*translation(tx.pivot*(-1));
    }
    std::vector<Transform> candidates;
    try {for(std::size_t i=0;i<tx.changes.size();++i){Node staged;staged.setWorldTransform(inverseAffine(tx.changes[i].parentWorld)*delta*tx.worlds[i]);candidates.push_back(staged.transform);}}
    catch(const std::invalid_argument&){return false;}
    std::size_t applied=0;
    try {for(;applied<candidates.size();++applied)apply(*scene_.findSavedNode(tx.changes[applied].target),candidates[applied]);}
    catch(const std::exception&) {while(applied){--applied;apply(*scene_.findSavedNode(tx.changes[applied].target),tx.changes[applied].after);}return false;}
    for(std::size_t i=0;i<candidates.size();++i)tx.changes[i].after=scene_.findSavedNode(tx.changes[i].target)->transform;
    tx.previous=*point;tx.angle=angle;return true;
}
bool EditorSession::commit() {
    if(!transaction_)return false;
    for(const auto& c:transaction_->changes)if(!resolve(c,c.after)){cancel();return false;}
    const auto changes=transaction_->changes;
    if(std::all_of(changes.begin(),changes.end(),[](const Change& c){return same(c.before,c.after);})){transaction_.reset();return true;}
    Command command;command.change=changes.front();
    if(changes.size()>1)command.edit=BatchTransform{changes,selectionIds()};
    push(std::move(command));transaction_.reset();return true;
}
bool EditorSession::cancel() {
    if(terrainTransaction_)return cancelTerrainStroke();
    if(!transaction_)return false;
    auto changes=transaction_->changes;transaction_.reset();bool complete=true;
    for(const auto& c:changes){if(auto* node=resolve(c,c.after))apply(*node,c.before);else complete=false;}
    return complete;
}
bool EditorSession::undo() {
    cancel();if(!cursor_||!replay(history_[cursor_-1],false)) return false;
    --cursor_;state_=history_[cursor_].change.beforeState;++revision_;return true;
}
bool EditorSession::redo() {
    cancel();if(cursor_==history_.size()||!replay(history_[cursor_],true)) return false;
    state_=history_[cursor_].change.afterState;++cursor_;++revision_;return true;
}
} // namespace engine
