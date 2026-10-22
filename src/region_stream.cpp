#include "engine/region_stream.hpp"
#include "engine/physics.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/light_component.hpp"
#include <set>
#include <limits>

namespace engine {
namespace {
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
bool keyValid(std::string_view s){return !s.empty()&&s.size()<=24&&std::all_of(s.begin(),s.end(),[](unsigned char c){return ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))||c=='_'||c=='-'||c=='.';});}
float axis(Vec3 v,int i){return i==0?v.x:i==1?v.y:v.z;}
void axis(Vec3& v,int i,float x){if(i==0)v.x=x;else if(i==1)v.y=x;else v.z=x;}
bool active(RegionStatus s){return s==RegionStatus::Loading||s==RegionStatus::Ready;}
std::string prefix(const RegionManifestData& m,const RegionRecord& r,RegionPart p){return "rg:"+m.scope+":"+r.key+":"+(p==RegionPart::Visual?"v:":"c:");}
void sourceCheck(const Scene& scene,RegionPart part,std::size_t maxNodes,const RegionBounds* bounds=nullptr){
    std::size_t count=0,bodies=0;require(!scene.children().empty(),"Empty region source");
    auto inspect=[&](const Node& n,const Mat4& world){
        require(n.enabled&&!n.isPendingRemoval(),"Region payload nodes must be enabled");
        if(&n==&scene)return;
        require(++count<=maxNodes&&n.savedId().size()<=48&&!n.archiveTransient(),"Region source node/identity limit");
        if(typeid(n)==typeid(Actor)){require(!static_cast<const Actor&>(n).tickEnabled(),"Region actors must be static content");return;}
        if(typeid(n)==typeid(Node))return;
        const auto* component=dynamic_cast<const Component*>(&n);require(component&&!component->tickEnabled()&&!component->fixedTickEnabled(),"Region source cannot own ticking behavior");
        component->validate();
        if(part==RegionPart::Visual){
            require(typeid(n)==typeid(MeshComponent)||typeid(n)==typeid(LightComponent),"Visual regions accept static mesh/light content only");
            if(const auto* mesh=dynamic_cast<const MeshComponent*>(&n)){
                require(mesh->mesh&&!mesh->mesh->skeleton&&mesh->mesh->skin.empty()&&mesh->mesh->morphTargets.empty(),"Region visual mesh must be static");
                const auto b=mesh->mesh->localBounds();require(b.valid,"Region visual bounds missing");
                if(bounds)for(int i=0;i<8;++i){const auto p=transformPoint(world,{i&1?b.maximum.x:b.minimum.x,i&2?b.maximum.y:b.minimum.y,i&4?b.maximum.z:b.minimum.z});
                    require(p.x>=bounds->minimum.x-.01f&&p.y>=bounds->minimum.y-.01f&&p.z>=bounds->minimum.z-.01f&&p.x<=bounds->maximum.x+.01f&&p.y<=bounds->maximum.y+.01f&&p.z<=bounds->maximum.z+.01f,"Region visual exceeds manifest bounds");}
            }
        }else{
            require(typeid(n)==typeid(RigidBodyComponent)||typeid(n)==typeid(ColliderComponent),"Collision regions accept static bodies/colliders only");
            if(const auto* body=dynamic_cast<const RigidBodyComponent*>(&n)){
                require(body->settings().motion==BodyMotion::Static&&!body->settings().isSensor&&body->hasBody(),"Region collision needs registered static solid bodies");++bodies;
                if(bounds){const auto b=scene.physicsIfCreated()->bodyBounds(body->id());require(bounds->contains({b.minimum,b.maximum},.01f),"Region collision exceeds manifest bounds");}
            }
        }
    };
    auto walk=[&](auto&& self,const Node& n)->void{inspect(n,n.worldMatrix());for(const auto& child:n.children())self(self,*child);};walk(walk,scene);
    if(part==RegionPart::Collision)require(bodies>0,"Region collision source has no solid body");
    for(const auto& root:scene.children()){const auto* n=root.get();require(typeid(*n)==typeid(Actor),"Region payload roots must be ordinary actors");}
}
}
void RegionBounds::validate()const{for(int i=0;i<3;++i)require(std::isfinite(axis(minimum,i))&&std::isfinite(axis(maximum,i))&&std::abs(axis(minimum,i))<=1000000&&std::abs(axis(maximum,i))<=1000000&&axis(minimum,i)<axis(maximum,i),"Invalid region bounds");}
bool RegionBounds::intersects(const RegionBounds& b)const{return minimum.x<=b.maximum.x&&maximum.x>=b.minimum.x&&minimum.y<=b.maximum.y&&maximum.y>=b.minimum.y&&minimum.z<=b.maximum.z&&maximum.z>=b.minimum.z;}
bool RegionBounds::contains(const RegionBounds& b,float t)const{return b.minimum.x>=minimum.x-t&&b.minimum.y>=minimum.y-t&&b.minimum.z>=minimum.z-t&&b.maximum.x<=maximum.x+t&&b.maximum.y<=maximum.y+t&&b.maximum.z<=maximum.z+t;}
void RegionManifestData::validate()const{
    require(keyValid(scope)&&!regions.empty()&&regions.size()<=128&&requiredOnRestore.size()<=regions.size(),"Invalid region manifest size/scope");std::set<std::string> keys;
    for(const auto& r:regions){require(keyValid(r.key)&&keys.insert(r.key).second,"Duplicate/invalid region key");r.bounds.validate();require(!r.visualAsset.empty()||!r.collisionAsset.empty(),"Region has no sources");
        if(!r.visualAsset.empty())AssetId{r.visualAsset};
        if(!r.collisionAsset.empty())AssetId{r.collisionAsset};
        for(auto bytes:{r.visualBytes,r.collisionBytes})require(bytes>=1024&&bytes<=16*1024*1024,"Region part budget must be 1 KiB..16 MiB");}
    std::set<std::string> required;for(const auto& id:requiredOnRestore){const auto i=std::find_if(regions.begin(),regions.end(),[&](const auto& r){return r.key==id;});require(i!=regions.end()&&!i->collisionAsset.empty()&&required.insert(id).second,"Invalid required restore region");}
}
std::shared_ptr<const RegionManifest> RegionManifest::create(RegionManifestData data){data.validate();return std::shared_ptr<const RegionManifest>(new RegionManifest(std::move(data)));}
RegionManifestComponent::RegionManifestComponent(std::string name,std::shared_ptr<const RegionManifest> manifest):Component(std::move(name)){configure(std::move(manifest));}
void RegionManifestComponent::configure(std::shared_ptr<const RegionManifest> manifest){require(bool(manifest),"Missing region manifest");requireIdle();manifest->data().validate();manifest_=std::move(manifest);}
void RegionManifestComponent::validate()const{Component::validate();require(bool(manifest_),"Missing region manifest");manifest_->data().validate();require(worldMatrix().m==Mat4::identity().m,"Region manifests author world bounds; keep their hierarchy transform identity");}
std::shared_ptr<const RegionSource> RegionSource::create(RegionPart part,std::vector<std::byte> bytes){require((part==RegionPart::Visual||part==RegionPart::Collision)&&!bytes.empty()&&bytes.size()<=16*1024*1024,"Invalid/big region transport");return std::shared_ptr<const RegionSource>(new RegionSource(part,std::move(bytes)));}
std::shared_ptr<const RegionSource> RegionSource::capture(RegionPart part,std::span<const Actor* const> actors){
    require(!actors.empty()&&actors.size()<=128,"Region capture expects 1..128 actors");Scene source("Region payload");source.environment.reset();
    for(const auto* a:actors){require(a&&!a->archiveTransient(),"Invalid region source actor");source.adoptChild(SceneArchive::cloneSubtree(*a));}
    source.synchronize();sourceCheck(source,part,512);ArchiveLimits limits;limits.maxBytes=16*1024*1024;limits.maxAllocatedBytes=16*1024*1024;limits.maxNodes=513;limits.maxDepth=16;return create(part,SceneArchive::save(source,{},{},{},limits));
}
void RegionStreamSettings::validate()const{
    require(maxResidentRegions>=1&&maxResidentRegions<=128&&maxInFlight>=1&&maxInFlight<=32&&maxStarts>=1&&maxStarts<=32&&maxPublications>=1&&maxPublications<=32&&maxNodesPerPart>=2&&maxNodesPerPart<=512&&maxTrackedBodies>=1&&maxTrackedBodies<=4096&&maxReservedBytes>=3072&&maxReservedBytes<=512ull*1024*1024&&std::isfinite(bodyPinMargin)&&bodyPinMargin>=0&&bodyPinMargin<=1000,"Invalid region streaming budgets");
}
struct RegionStream::Impl {
    struct Part {RegionStatus status=RegionStatus::Absent;AssetRequest request;std::vector<std::string> roots;std::string error;bool wanted=false;int priority=0;};
    struct Entry {Part visual,collision;bool pinned=false;};
    Scene& scene;AssetManager& assets;std::shared_ptr<const RegionManifest> manifest;RegionStreamSettings settings;std::vector<Entry> entries;std::vector<RegionBounds> extraPins;RegionStreamStats stats;std::vector<RegionResidency> report;
    Impl(Scene& s,AssetManager& a,std::shared_ptr<const RegionManifest> m,RegionStreamSettings b):scene(s),assets(a),manifest(std::move(m)),settings(b){require(bool(manifest),"Missing region manifest");manifest->data().validate();settings.validate();entries.resize(manifest->data().regions.size());stats.restoring=!manifest->data().requiredOnRestore.empty();refresh();}
    Part& part(std::size_t i,RegionPart p){return p==RegionPart::Visual?entries[i].visual:entries[i].collision;}
    const Part& part(std::size_t i,RegionPart p)const{return p==RegionPart::Visual?entries[i].visual:entries[i].collision;}
    std::size_t cost(std::size_t i,RegionPart p)const{const auto& r=manifest->data().regions[i];return 3ull*(p==RegionPart::Visual?r.visualBytes:r.collisionBytes);}
    const std::string& asset(std::size_t i,RegionPart p)const{const auto& r=manifest->data().regions[i];return p==RegionPart::Visual?r.visualAsset:r.collisionAsset;}
    std::size_t index(std::string_view key)const{const auto& r=manifest->data().regions;for(std::size_t i=0;i<r.size();++i)if(r[i].key==key)return i;throw std::invalid_argument("Unknown region key");}
    void refresh(){
        stats.residentRegions=stats.inFlight=stats.reservedBytes=stats.pinnedRegions=stats.waitingBudget=stats.failed=0;report.clear();
        for(std::size_t i=0;i<entries.size();++i){const auto& e=entries[i];if(active(e.visual.status)||active(e.collision.status))++stats.residentRegions;if(e.pinned)++stats.pinnedRegions;
            for(auto p:{RegionPart::Visual,RegionPart::Collision}){const auto s=part(i,p).status;if(active(s))stats.reservedBytes+=cost(i,p);if(s==RegionStatus::Loading)++stats.inFlight;if(s==RegionStatus::WaitingBudget)++stats.waitingBudget;if(s==RegionStatus::Failed)++stats.failed;}
            report.push_back({manifest->data().regions[i].key,e.visual.status,e.collision.status,e.pinned,e.visual.error,e.collision.error});}
    }
    void pins(){
        for(auto& e:entries)e.pinned=false;
        stats.safetyBlocked=false;stats.error.clear();std::size_t bodies=0;
        auto pin=[&](RegionBounds b){for(std::size_t i=0;i<entries.size();++i)if(b.intersects(manifest->data().regions[i].bounds))entries[i].pinned=true;};
        for(const auto& b:extraPins)pin(b);
        if(!scene.isActiveInHierarchy()||scene.worldMatrix().m!=Mat4::identity().m){stats.safetyBlocked=true;stats.error="Inactive or transformed scene: region publication and readiness blocked";for(auto& e:entries)e.pinned=true;return;}
        scene.visit([&](const Node& n,const Mat4&){const auto* body=dynamic_cast<const RigidBodyComponent*>(&n);if(!body||!body->isActiveInHierarchy()||body->settings().motion==BodyMotion::Static)return;
            if(++bodies>settings.maxTrackedBodies||!body->hasBody()){stats.safetyBlocked=true;return;}
            try{const auto b=scene.physicsIfCreated()->bodyBounds(body->id());const Vec3 margin{settings.bodyPinMargin,settings.bodyPinMargin,settings.bodyPinMargin};pin({b.minimum-margin,b.maximum+margin});
                for(std::size_t i=0;i<entries.size();++i)if(body->savedId().starts_with(prefix(manifest->data(),manifest->data().regions[i],RegionPart::Collision)))entries[i].pinned=true;
            }catch(const std::exception&){stats.safetyBlocked=true;}});
        if(stats.safetyBlocked){for(auto& e:entries)e.pinned=true;stats.error="Unregistered/excess simulated bodies: eviction and readiness blocked";}
    }
    void evict(std::size_t i,RegionPart p){auto& state=part(i,p);if(state.status==RegionStatus::Loading){state.request.cancel();++stats.cancelled;}
        if(state.status==RegionStatus::Ready){for(const auto& id:state.roots)if(auto* n=scene.findSavedNode(id)){auto removed=scene.detachChild(*n);}scene.synchronize();++stats.evicted;}
        state.request=AssetRequest{};state.roots.clear();state.status=RegionStatus::Absent;state.error.clear();refresh();
    }
    std::int64_t score(std::size_t i,RegionPart p)const{return std::int64_t(part(i,p).priority)*2+(p==RegionPart::Collision?1:0);}
    bool reserve(std::size_t i,RegionPart p){
        refresh();auto fits=[&]{const auto regionActive=active(entries[i].visual.status)||active(entries[i].collision.status);return stats.reservedBytes+cost(i,p)<=settings.maxReservedBytes&&(regionActive||stats.residentRegions<settings.maxResidentRegions);};
        while(!fits()){
            std::size_t victim=entries.size();RegionPart phase=RegionPart::Visual;auto lowest=score(i,p);
            for(std::size_t j=0;j<entries.size();++j)for(auto q:{RegionPart::Visual,RegionPart::Collision})if(active(part(j,q).status)&&!(q==RegionPart::Collision&&entries[j].pinned)&&score(j,q)<lowest){victim=j;phase=q;lowest=score(j,q);}
            if(victim==entries.size())return false;
            evict(victim,phase);
        }return true;
    }
    void publish(std::size_t i,RegionPart p){
        auto& state=part(i,p);const auto source=state.request.get<RegionSource>();require(source&&source->part()==p,"Region source part mismatch");
        const auto& record=manifest->data().regions[i];const auto budget=p==RegionPart::Visual?record.visualBytes:record.collisionBytes;require(source->bytes().size()<=budget,"Region encoded byte budget exceeded");
        ArchiveLimits limits;limits.maxBytes=budget;limits.maxAllocatedBytes=budget;limits.maxNodes=settings.maxNodesPerPart+1;limits.maxDepth=16;limits.maxElements=65536;limits.maxResources=1024;
        auto decoded=SceneArchive::load(source->bytes(),limits);require(decoded.views.empty()&&decoded.applicationData.empty(),"Region sources must not contain camera views or application state");sourceCheck(*decoded.scene,p,settings.maxNodesPerPart,&record.bounds);
        std::vector<std::unique_ptr<Node>> candidates;std::vector<Node*> nodes;std::vector<std::string> ids;
        decoded.scene->visit([&](const Node& n,const Mat4&){if(&n!=decoded.scene.get()){nodes.push_back(const_cast<Node*>(&n));ids.push_back(prefix(manifest->data(),record,p)+n.savedId());require(Node::validSavedId(ids.back())&&!scene.findSavedNode(ids.back()),"Region saved identity collision");}});
        // Two passes avoid collisions with user-authored source identities during remapping.
        for(std::size_t j=0;j<nodes.size();++j)nodes[j]->setSavedId("region-staging-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx:"+std::to_string(j));
        for(std::size_t j=0;j<nodes.size();++j)nodes[j]->setSavedId(ids[j]);
        while(!decoded.scene->children().empty())candidates.push_back(decoded.scene->detachChild(*decoded.scene->children().back()));
        std::reverse(candidates.begin(),candidates.end());
        std::vector<std::string> roots;roots.reserve(candidates.size());for(auto& n:candidates){n->archiveTransient_=true;roots.push_back(n->savedId());}
        std::size_t installed=0;
        try{for(auto& n:candidates){scene.adoptChild(std::move(n));++installed;}scene.synchronize();
            if(p==RegionPart::Collision)for(const auto& id:roots){const auto* a=dynamic_cast<const Actor*>(scene.findSavedNode(id));require(a&&a->getComponent<RigidBodyComponent>()&&a->getComponent<RigidBodyComponent>()->hasBody(),"Region collision registration failed");}
        }catch(...){for(std::size_t j=0;j<installed;++j)if(auto* n=scene.findSavedNode(roots[j])){auto removed=scene.detachChild(*n);}scene.synchronize();throw;}
        state.roots=std::move(roots);state.status=RegionStatus::Ready;++stats.published;
    }
};
RegionStream::RegionStream(Scene& s,AssetManager& a,std::shared_ptr<const RegionManifest> m,RegionStreamSettings settings):impl_(std::make_unique<Impl>(s,a,std::move(m),settings)){s.requireIdle();require(s.worldMatrix().m==Mat4::identity().m,"Region stream scene transform must be identity");}
RegionStream::~RegionStream(){try{clear();}catch(...){/* Scene owns remaining content; never tear support out during traversal. */}}
void RegionStream::update(std::span<const RegionInterest> interests,std::span<const RegionBounds> extraPins){
    auto& s=*impl_;s.scene.requireIdle();require(interests.size()<=128&&extraPins.size()<=128,"Region interest/pin count exceeded");for(const auto& i:interests){s.index(i.key);require(i.priority>=-1000000&&i.priority<=1000000,"Invalid region priority");}for(const auto& b:extraPins)b.validate();
    s.extraPins.assign(extraPins.begin(),extraPins.end());s.scene.synchronize();s.pins();
    if(s.stats.safetyBlocked){s.refresh();return;}
    for(auto& e:s.entries)for(auto* p:{&e.visual,&e.collision}){p->wanted=false;p->priority=-1000001;}
    auto want=[&](std::size_t i,RegionPart p,int priority){if(s.asset(i,p).empty())return;auto& a=s.part(i,p);a.wanted=true;a.priority=std::max(a.priority,priority);};
    for(const auto& i:interests){const auto n=s.index(i.key);if(i.visual)want(n,RegionPart::Visual,i.priority);if(i.collision)want(n,RegionPart::Collision,i.priority);}
    for(std::size_t i=0;i<s.entries.size();++i)if(s.entries[i].pinned)want(i,RegionPart::Collision,1000002);
    if(s.stats.restoring)for(const auto& id:s.manifest->data().requiredOnRestore)want(s.index(id),RegionPart::Collision,1000003);
    for(std::size_t i=0;i<s.entries.size();++i)for(auto p:{RegionPart::Visual,RegionPart::Collision})if(!s.part(i,p).wanted&&(active(s.part(i,p).status)||s.part(i,p).status==RegionStatus::WaitingBudget))s.evict(i,p);
    std::vector<std::pair<std::size_t,RegionPart>> order;for(std::size_t i=0;i<s.entries.size();++i)for(auto p:{RegionPart::Collision,RegionPart::Visual})if(s.part(i,p).wanted)order.emplace_back(i,p);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return s.score(a.first,a.second)>s.score(b.first,b.second);});
    std::uint32_t published=0,started=0;
    for(auto [i,p]:order){auto& a=s.part(i,p);if(a.status!=RegionStatus::Loading)continue;const auto progress=a.request.progress();
        try{if(progress.status==AssetStatus::Failed||progress.status==AssetStatus::Cancelled)throw std::runtime_error(progress.error.empty()?"Region load cancelled":progress.error);
            if(progress.status==AssetStatus::Ready&&published<s.settings.maxPublications){++published;s.publish(i,p);}
        }catch(const std::exception& e){a.request.cancel();a.request=AssetRequest{};a.status=RegionStatus::Failed;a.error=std::string(e.what()).substr(0,512);}
    }
    for(auto [i,p]:order){auto& a=s.part(i,p);if(a.status!=RegionStatus::Absent&&a.status!=RegionStatus::WaitingBudget)continue;s.refresh();if(started>=s.settings.maxStarts||s.stats.inFlight>=s.settings.maxInFlight)continue;
        if(!s.reserve(i,p)){a.status=RegionStatus::WaitingBudget;continue;}
        try{a.request=s.assets.load<RegionSource>(AssetId{s.asset(i,p)});a.status=RegionStatus::Loading;++started;++s.stats.started;}catch(const std::exception& e){a.status=RegionStatus::Failed;a.error=std::string(e.what()).substr(0,512);}
    }s.refresh();
}
void RegionStream::retryFailed(){auto& s=*impl_;s.scene.requireIdle();for(auto& e:s.entries)for(auto* p:{&e.visual,&e.collision})if(p->status==RegionStatus::Failed){p->status=RegionStatus::Absent;p->error.clear();}s.refresh();}
bool RegionStream::clear(){auto& s=*impl_;s.scene.requireIdle();s.scene.synchronize();s.pins();for(const auto& e:s.entries)if(e.pinned&&e.collision.status==RegionStatus::Ready){for(auto& entry:s.entries)for(auto* p:{&entry.visual,&entry.collision})if(p->status==RegionStatus::Loading){p->request.cancel();p->request=AssetRequest{};p->status=RegionStatus::Absent;}s.refresh();return false;}
    for(std::size_t i=0;i<s.entries.size();++i)for(auto p:{RegionPart::Visual,RegionPart::Collision})s.evict(i,p);
    s.stats.restoring=false;s.refresh();return true;}
bool RegionStream::collisionReady(const RegionBounds& query)const{
    query.validate();const auto& s=*impl_;if(s.stats.safetyBlocked||!s.scene.isActiveInHierarchy()||s.scene.worldMatrix().m!=Mat4::identity().m)return false;
    std::vector<RegionBounds> pieces{query};std::size_t work=0;
    for(std::size_t i=0;i<s.entries.size();++i)if(s.entries[i].collision.status==RegionStatus::Ready){std::vector<RegionBounds> next;const auto cover=s.manifest->data().regions[i].bounds;
        for(const auto& piece:pieces){if(++work>8192)return false;if(!piece.intersects(cover)){next.push_back(piece);continue;}RegionBounds middle=piece;
            for(int axisIndex=0;axisIndex<3;++axisIndex){const auto low=std::max(axis(piece.minimum,axisIndex),axis(cover.minimum,axisIndex)),high=std::min(axis(piece.maximum,axisIndex),axis(cover.maximum,axisIndex));if(low>axis(middle.minimum,axisIndex)){auto slab=middle;axis(slab.maximum,axisIndex,low);next.push_back(slab);axis(middle.minimum,axisIndex,low);}if(high<axis(middle.maximum,axisIndex)){auto slab=middle;axis(slab.minimum,axisIndex,high);next.push_back(slab);axis(middle.maximum,axisIndex,high);}}
            if(next.size()>1024)return false;
        }pieces=std::move(next);if(pieces.empty())return true;
    }
    return false;
}
bool RegionStream::visualReady(std::string_view key)const{return impl_->entries[impl_->index(key)].visual.status==RegionStatus::Ready;}
bool RegionStream::restorationReady()const{if(!impl_->scene.isActiveInHierarchy()||impl_->scene.worldMatrix().m!=Mat4::identity().m)return false;if(!impl_->stats.restoring)return !impl_->stats.safetyBlocked;for(const auto& id:impl_->manifest->data().requiredOnRestore)if(impl_->entries[impl_->index(id)].collision.status!=RegionStatus::Ready)return false;return !impl_->stats.safetyBlocked;}
bool RegionStream::finishRestore(){if(!restorationReady())return false;impl_->stats.restoring=false;return true;}
const Node* RegionStream::resolve(std::string_view key,RegionPart p,std::string_view id)const{const auto i=impl_->index(key);if(impl_->part(i,p).status!=RegionStatus::Ready)return nullptr;return impl_->scene.findSavedNode(prefix(impl_->manifest->data(),impl_->manifest->data().regions[i],p)+std::string(id));}
std::string RegionStream::regionOf(ActorId actor)const{const auto* n=impl_->scene.findActor(actor);if(!n||!n->archiveTransient())return {};for(const auto& r:impl_->manifest->data().regions)for(auto p:{RegionPart::Visual,RegionPart::Collision})if(n->savedId().starts_with(prefix(impl_->manifest->data(),r,p)))return r.key;return {};}
const RegionStreamStats& RegionStream::stats()const{return impl_->stats;}
const std::vector<RegionResidency>& RegionStream::residency()const{return impl_->report;}
const std::shared_ptr<const RegionManifest>& RegionStream::manifest()const{return impl_->manifest;}
std::vector<std::byte> RegionStream::checkpoint(std::span<const std::string> required,std::span<const std::byte> data,std::span<const RenderView> views,const AssetLibrary& assets,const ComponentCodecs& codecs)const{
    auto definition=impl_->manifest->data();definition.requiredOnRestore.assign(required.begin(),required.end());auto manifest=RegionManifest::create(std::move(definition));
    auto snapshot=SceneArchive::clone(impl_->scene,views,assets,{},codecs);RegionManifestComponent* target=nullptr;auto find=[&](auto&& self,Node& n)->void{if(auto* m=dynamic_cast<RegionManifestComponent*>(&n);m&&m->manifest()->data().scope==manifest->data().scope){require(!target,"Duplicate manifest scope in checkpoint");target=m;}for(const auto& child:n.children())self(self,*child);};find(find,*snapshot.scene);require(target,"Checkpoint scene must contain this manifest scope");target->configure(manifest);
    return SceneArchive::save(*snapshot.scene,snapshot.views,snapshot.assets,data,{},codecs);
}
} // namespace engine
