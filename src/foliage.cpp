#include "engine/foliage.hpp"
#include "engine/rigid_body_component.hpp"
#include <algorithm>
#include <limits>
#include <locale>
#include <sstream>
#include <tuple>

namespace engine {
namespace {
void require(bool ok,const char* message) { if(!ok) throw std::invalid_argument(message); }
bool finite(Vec2 v) { return std::isfinite(v.x)&&std::isfinite(v.y); }
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
constexpr float coordinateLimit=100000;
void focusCheck(Vec2 v) { require(finite(v)&&std::abs(v.x)<=coordinateLimit&&std::abs(v.y)<=coordinateLimit,"Foliage focus outside finite supported world"); }
std::uint64_t mix(std::uint64_t v) {
    v+=0x9e3779b97f4a7c15ull;v=(v^(v>>30))*0xbf58476d1ce4e5b9ull;
    v=(v^(v>>27))*0x94d049bb133111ebull;return v^(v>>31);
}
std::uint64_t identity(std::uint64_t seed,FoliageId id) {
    return mix(mix(mix(seed^id.species)^std::uint32_t(id.x))^std::uint32_t(id.z));
}
float random(std::uint64_t v) { return float(mix(v)>>40)*(1.0f/16777216.0f); }
double stepFor(const FoliageSpecies& s) { return std::max(double(s.minimumSpacing)/.6,std::sqrt(1.0/double(s.density))); }
FoliageCell cellFor(Vec2 p,float size) { return {std::int32_t(std::floor(double(p.x)/size)),std::int32_t(std::floor(double(p.y)/size))}; }
const FoliageSpecies& speciesFor(const FoliageCatalog& c,std::uint32_t id) {
    const auto& list=c.data().species;
    auto found=std::lower_bound(list.begin(),list.end(),id,[](const auto& s,auto value){return s.id<value;});
    require(found!=list.end()&&found->id==id,"Unknown foliage species");return *found;
}
bool removedId(std::span<const FoliageId> list,FoliageId id) { return std::binary_search(list.begin(),list.end(),id); }
float distance(Vec2 p,Vec3 q) { return std::hypot(p.x-q.x,p.y-q.z); }
void meshCheck(const FoliageDetail& d) {
    require(d.mesh&&!d.mesh->vertices.empty()&&d.mesh->vertices.size()<=65536,"Foliage detail requires bounded geometry");
    const auto& m=*d.mesh;
    require(!m.skeleton&&m.skin.empty()&&m.morphTargets.empty(),"Foliage requires static source geometry");
    require(!m.indices.empty()&&m.indices.size()%3==0&&m.indices.size()<=393216,"Invalid foliage triangles");
    require(d.bend.empty()||d.bend.size()==m.vertices.size(),"Foliage bend weights must cover every vertex");
    for(const auto& v:m.vertices) {
        require(finite(v.position)&&finite(v.normal)&&finite(v.uv),"Nonfinite foliage vertex");
        require(std::abs(v.position.x)<=1000&&std::abs(v.position.y)<=1000&&std::abs(v.position.z)<=1000,"Foliage mesh extent exceeded");
        for(float t:v.tangent)require(std::isfinite(t),"Nonfinite foliage tangent");
    }
    for(auto i:m.indices)require(i<m.vertices.size(),"Foliage index outside vertices");
    for(float w:d.bend)require(std::isfinite(w)&&w>=0&&w<=1,"Invalid foliage bend weight");
    for(const auto& material:m.materials) {require(bool(material),"Missing foliage material");material->validate();}
    for(const auto& part:m.parts)require(std::uint64_t(part.firstIndex)+part.indexCount<=m.indices.size()&&part.indexCount%3==0&&part.materialIndex<m.materials.size(),"Invalid foliage material range");
}
}
void FoliageSpecies::validate() const {
    require(id&&name.size()<=128&&!details.empty()&&details.size()<=8,"Invalid foliage species identity/details");
    require(std::isfinite(density)&&density>=0&&density<=100&&std::isfinite(minimumSpacing)&&minimumSpacing>=.1f&&minimumSpacing<=1000,"Invalid foliage density/spacing");
    require(std::isfinite(minimumHeight)&&std::isfinite(maximumHeight)&&minimumHeight>=-coordinateLimit&&maximumHeight<=coordinateLimit&&minimumHeight<=maximumHeight,"Invalid foliage height range");
    require(std::isfinite(maximumSlope)&&maximumSlope>=0&&maximumSlope<=pi/2,"Invalid foliage slope");
    require(std::isfinite(minimumScale)&&std::isfinite(maximumScale)&&minimumScale>=.01f&&maximumScale<=100&&minimumScale<=maximumScale,"Invalid foliage scale");
    require(std::isfinite(drawDistance)&&drawDistance>0&&drawDistance<=10000&&std::isfinite(shadowDistance)&&shadowDistance>=0&&shadowDistance<=drawDistance,"Invalid foliage distance");
    require(finite(tint)&&tint.x>=0&&tint.y>=0&&tint.z>=0&&tint.x<=16&&tint.y<=16&&tint.z<=16,"Invalid foliage tint");
    require(std::isfinite(trunkRadius)&&std::isfinite(trunkHeight)&&trunkRadius>=0&&trunkRadius<=100&&trunkHeight>=0&&trunkHeight<=1000&&((trunkRadius==0&&trunkHeight==0)||(trunkRadius>0&&trunkHeight>=2*trunkRadius)),"Invalid foliage trunk capsule");
    float previous=-1;
    for(const auto& d:details) {
        require(std::isfinite(d.distance)&&d.distance>previous&&d.distance<drawDistance,"Invalid foliage detail distance");
        meshCheck(d);previous=d.distance;
    }
    require(details.front().distance==0,"First foliage detail must start at zero");
}
void FoliageDefinition::validate() const {
    require(world&&std::isfinite(cellSize)&&cellSize>=1&&cellSize<=1024,"Invalid foliage world/cell size");
    require(!species.empty()&&species.size()<=64&&exclusions.size()<=1024,"Foliage definition count exceeded");
    require(maxCandidatesPerCell&&maxCandidatesPerCell<=65536&&maxInstancesPerCell&&maxInstancesPerCell<=16384&&maxRemoved<=262144,"Invalid foliage generation bounds");
    std::size_t candidates=0;
    for(std::size_t i=0;i<species.size();++i) {
        const auto& s=species[i];s.validate();
        for(std::size_t j=0;j<i;++j)require(s.id!=species[j].id,"Duplicate foliage species");
        if(s.density>0) {auto n=std::size_t(std::ceil(cellSize/stepFor(s)))+3;candidates+=n*n;}
    }
    require(candidates<=maxCandidatesPerCell,"Foliage candidate budget too small for definition");
    for(const auto& e:exclusions) require(finite(e.minimum)&&finite(e.maximum)&&e.minimum.x<=e.maximum.x&&e.minimum.y<=e.maximum.y,"Invalid foliage exclusion");
}
FoliageCatalog::FoliageCatalog(FoliageDefinition d):data_(std::move(d)) {}
std::shared_ptr<const FoliageCatalog> FoliageCatalog::create(FoliageDefinition data) {
    data.validate();std::sort(data.species.begin(),data.species.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return std::shared_ptr<const FoliageCatalog>(new FoliageCatalog(std::move(data)));
}
FoliageSampler foliageTerrainSampler(std::shared_ptr<const TerrainAsset> asset,Vec3 origin,std::uint32_t mask) {
    require(bool(asset)&&finite(origin),"Invalid foliage terrain source");
    return [asset=std::move(asset),origin,mask](Vec2 p)->std::optional<FoliageSurface> {
        if(auto ground=asset->ground(p.x-origin.x,p.y-origin.z))return FoliageSurface{ground->position.y+origin.y,ground->normal,mask};
        return {};
    };
}
std::vector<FoliagePlacement> generateFoliageCell(const FoliageCatalog& catalog,FoliageCell cell,const FoliageSampler& sample) {
    require(bool(sample),"Missing foliage surface sampler");const auto& d=catalog.data();
    const double left=double(cell.x)*d.cellSize,top=double(cell.z)*d.cellSize;
    require(std::abs(left)<=coordinateLimit+d.cellSize&&std::abs(top)<=coordinateLimit+d.cellSize,"Foliage cell outside supported world");
    std::vector<FoliagePlacement> out;std::size_t visited=0;
    for(const auto& s:d.species) {
        if(s.density==0)continue;const auto step=stepFor(s);
        const auto firstX=std::int32_t(std::floor(left/step))-1,lastX=std::int32_t(std::ceil((left+d.cellSize)/step));
        const auto firstZ=std::int32_t(std::floor(top/step))-1,lastZ=std::int32_t(std::ceil((top+d.cellSize)/step));
        for(auto z=firstZ;z<=lastZ;++z)for(auto x=firstX;x<=lastX;++x) {
            require(++visited<=d.maxCandidatesPerCell,"Foliage candidate budget exceeded");
            const FoliageId id{s.id,x,z};const auto hash=identity(d.seed,id);
            const Vec2 p{float((x+.3+.4*random(hash))*step),float((z+.3+.4*random(hash+1))*step)};
            // Very sparse species can have lattice steps far outside the world.
            // Test ownership before any narrowing of candidate coordinates.
            if(double(p.x)<left||double(p.x)>=left+d.cellSize||double(p.y)<top||double(p.y)>=top+d.cellSize)continue;
            if(random(hash+2)>std::min(1.0,double(s.density)*step*step))continue;
            bool excluded=false;for(const auto& e:d.exclusions)if(p.x>=e.minimum.x&&p.x<=e.maximum.x&&p.y>=e.minimum.y&&p.y<=e.maximum.y){excluded=true;break;}
            if(excluded)continue;
            auto surface=sample(p);if(!surface)continue;
            require(std::isfinite(surface->height)&&std::abs(surface->height)<=coordinateLimit&&finite(surface->normal)&&dot(surface->normal,surface->normal)>.0001f,"Invalid foliage surface sample");
            const auto n=normalized(surface->normal);
            if(surface->height<s.minimumHeight||surface->height>s.maximumHeight||n.y+1e-6f<std::cos(s.maximumSlope)||!(surface->mask&s.surfaceMask))continue;
            FoliagePlacement place;place.id=id;place.transform.position={p.x,surface->height,p.y};
            place.transform.rotation.y=random(hash+3)*2*pi;
            if(s.alignToGround) {
                // Rz*Ry*Rx: compose a ground basis with yaw, then extract Euler angles.
                auto forward=normalized(cross({1,0,0},n));
                if(dot(forward,forward)<.5f)forward=normalized(cross({0,0,1},n));
                const auto right=normalized(cross(n,forward));const float a=place.transform.rotation.y;
                const auto r=right*std::cos(a)-forward*std::sin(a),f=right*std::sin(a)+forward*std::cos(a);
                place.transform.rotation.y=std::asin(std::clamp(-r.z,-1.0f,1.0f));
                if(std::abs(std::cos(place.transform.rotation.y))>1e-5f) {place.transform.rotation.x=std::atan2(n.z,f.z);place.transform.rotation.z=std::atan2(r.y,r.x);}
                else {place.transform.rotation.x=0;place.transform.rotation.z=std::atan2(-n.x,n.y);}
            }
            const float scale=s.minimumScale+(s.maximumScale-s.minimumScale)*random(hash+4);
            place.transform.scale={scale,scale,scale};place.phase=random(hash+5)*2*pi;
            require(out.size()<d.maxInstancesPerCell,"Foliage instance budget exceeded");out.push_back(place);
        }
    }
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.id<b.id;});return out;
}
FoliageMeshComponent::FoliageMeshComponent(std::string name,const FoliageDetail& detail,float phase)
    :MeshComponent(std::move(name),detail.mesh),source_(detail.mesh),bend_(detail.bend),phase_(phase) {
    meshCheck(detail);require(std::isfinite(phase),"Invalid foliage wind phase");allowInstanceReordering=true;
}
std::shared_ptr<const MorphPose> FoliageMeshComponent::morphPose() const { return mesh==source_?pose_:nullptr; }
std::shared_ptr<MorphPose> FoliageMeshComponent::prepareWind(double seconds,const FoliageWind& wind) const {
    if(bend_.empty()||wind.amplitude==0||std::hypot(wind.direction.x,wind.direction.y)==0)return {};
    auto next=std::make_shared<MorphPose>();next->vertices=source_->vertices;next->revision=pose_?pose_->revision+1:1;
    const auto length=std::hypot(wind.direction.x,wind.direction.y);
    const auto local=transformDirection(inverseAffine(worldMatrix()),{wind.direction.x/length,0,wind.direction.y/length});
    const double turns=seconds*wind.frequency*2*double(pi);
    const double angle=std::remainder(turns,2*double(pi))+phase_;
    const double gustAngle=std::remainder(turns*.37,2*double(pi))+phase_*1.37;
    const auto amount=wind.amplitude*float(std::sin(angle)*(1-wind.gust+wind.gust*.5*(1+std::sin(gustAngle))));
    for(std::size_t i=0;i<next->vertices.size();++i) {
        auto& v=next->vertices[i];v.position=v.position+local*(bend_[i]*bend_[i]*amount);v.normal={};
    }
    for(std::size_t i=0;i<source_->indices.size();i+=3) {
        auto& a=next->vertices[source_->indices[i]];auto& b=next->vertices[source_->indices[i+1]];auto& c=next->vertices[source_->indices[i+2]];
        const auto normal=cross(b.position-a.position,c.position-a.position);
        a.normal=a.normal+normal;b.normal=b.normal+normal;c.normal=c.normal+normal;
    }
    for(std::size_t i=0;i<next->vertices.size();++i) {
        auto& v=next->vertices[i];v.normal=normalized(v.normal);
        if(dot(v.normal,v.normal)<.5f)v.normal=source_->vertices[i].normal;
        auto t=normalized(Vec3{v.tangent[0],v.tangent[1],v.tangent[2]}-v.normal*dot(v.normal,{v.tangent[0],v.tangent[1],v.tangent[2]}));
        if(dot(t,t)<.5f)t=normalized(cross(std::abs(v.normal.y)<.9f?Vec3{0,1,0}:Vec3{0,0,1},v.normal));
        v.tangent={t.x,t.y,t.z,v.tangent[3]};
    }
    return next;
}
void FoliageStreamSettings::validate() const {
    require(rendering==FoliageRendering::GpuInstances||rendering==FoliageRendering::CpuMeshes,"Invalid foliage rendering mode");
    require(visualRadius<=8&&collisionRadius<=8&&hysteresisCells<=4&&maxVisualCells&&maxVisualCells<=512&&maxCollisionCells&&maxCollisionCells<=512&&buildsPerUpdate&&buildsPerUpdate<=64,"Invalid foliage stream limits");
    const auto visualWidth=visualRadius*2+1,collisionWidth=collisionRadius*2+1;
    require(maxVisualCells>=visualWidth*visualWidth&&(!collision||maxCollisionCells>=collisionWidth*collisionWidth),"Foliage resident budget must fit the requested radius");
    require(std::isfinite(detailHysteresis)&&detailHysteresis>=0&&detailHysteresis<=.4f,"Invalid foliage detail hysteresis");
}
struct FoliageStream::Impl {
    struct Binding {FoliageId id;ComponentId component;Transform transform;float phase=0;std::size_t level=0;std::uint64_t key=0;std::size_t group=0;};
    struct Group {std::uint32_t species=0;std::size_t level=0;ComponentId component;};
    struct Resident {FoliageCell key;bool collision=false;ActorId actor;std::vector<Binding> bindings;std::size_t colliders=0;std::vector<Group> groups;};
    struct Prepared {Resident resident;std::unique_ptr<Actor> actor;};
    Scene& scene;std::shared_ptr<const FoliageCatalog> catalog;FoliageSampler sample;FoliageStreamSettings settings;
    std::vector<Resident> residents;std::vector<FoliageId> removed;FoliageStreamStats stats;
    Vec2 visualFocus{};double seconds=0;FoliageWind wind;bool presented=false;
    std::shared_ptr<MeshWindState> windState=std::make_shared<MeshWindState>();
    std::vector<std::vector<std::shared_ptr<const MeshBend>>> bends;
    Impl(Scene& scene,std::shared_ptr<const FoliageCatalog> catalog,FoliageSampler sample,FoliageStreamSettings settings)
        :scene(scene),catalog(std::move(catalog)),sample(std::move(sample)),settings(settings) {
        if(settings.rendering==FoliageRendering::GpuInstances)for(const auto& species:this->catalog->data().species) {
            auto& levels=bends.emplace_back();for(const auto& detail:species.details)levels.push_back(detail.bend.empty()?nullptr:MeshBend::create(detail.mesh,detail.bend));
        }
    }
    void idle() const {
        scene.requireIdle();require(scene.worldMatrix().m==Mat4::identity().m,"Foliage requires identity scene transform");
        for(const auto& r:residents)if(auto* actor=scene.findActor(r.actor))require(actor->parent()==&scene&&!actor->subtreePending(),"Foliage projection was reparented or queued for removal");
    }
    Prepared prepare(FoliageCell key,bool collision,const FoliageSampler& source,std::span<const FoliageId> removedIds) const {
        Prepared out;out.resident.key=key;out.resident.collision=collision;
        out.actor=std::make_unique<Actor>(collision?"Foliage collision cell":"Foliage visual cell");out.actor->archiveTransient_=true;
        std::vector<CompoundChild> shapes;
        const bool bulk=!collision&&settings.rendering==FoliageRendering::GpuInstances;
        std::vector<std::vector<MeshInstance>> instances;
        if(bulk)for(std::size_t species=0;species<catalog->data().species.size();++species) {
            const auto& s=catalog->data().species[species];
            for(std::size_t level=0;level<s.details.size();++level) {
                auto& component=out.actor->addComponent<MeshInstancesComponent>(s.name,s.details[level].mesh);
                component.color=s.tint;component.castsShadow=s.castsShadow;
                if(bends[species][level])component.setBend(bends[species][level],windState);
                out.resident.groups.push_back({s.id,level,{}});instances.emplace_back();
            }
        }
        for(const auto& p:generateFoliageCell(*catalog,key,source)) {
            if(removedId(removedIds,p.id))continue;const auto& s=speciesFor(*catalog,p.id.species);
            if(collision) {
                if(s.trunkRadius==0)continue;
                const float scale=p.transform.scale.x,radius=s.trunkRadius*scale,height=s.trunkHeight*scale;
                auto t=p.transform;t.scale={1,1,1};t.position=transformPoint(p.transform.matrix(),{0,s.trunkHeight*.5f,0});
                auto shape=height>2*radius?CollisionShape::capsule(height*.5f-radius,radius):CollisionShape::sphere(radius);
                shapes.push_back({std::move(shape),t,{}});
            } else if(bulk) {
                const auto group=std::size_t(std::find_if(out.resident.groups.begin(),out.resident.groups.end(),[&](const auto& g){return g.species==p.id.species&&g.level==0;})-out.resident.groups.begin());
                const auto instanceKey=std::uint64_t(out.resident.bindings.size()+1);
                instances[group].push_back({p.transform,{1,1,1},instanceKey,p.phase});
                out.resident.bindings.push_back({p.id,{},p.transform,p.phase,0,instanceKey,group});
            } else {
                auto& component=out.actor->addComponent<FoliageMeshComponent>(s.name,s.details.front(),p.phase);
                component.transform=p.transform;component.color=s.tint;component.castsShadow=s.castsShadow;
                out.resident.bindings.push_back({p.id,{},p.transform,p.phase,0});
            }
        }
        if(bulk)for(std::size_t i=0;i<out.resident.groups.size();++i) {
            auto* component=static_cast<MeshInstancesComponent*>(out.actor->children()[i].get());
            component->setInstances(MeshInstanceSet::create(component->mesh,std::move(instances[i])));
        }
        if(!shapes.empty()) {
            out.actor->addComponent<ColliderComponent>("Trunks",CollisionShape::compound(shapes));
            RigidBodySettings body;body.motion=BodyMotion::Static;out.actor->addComponent<RigidBodyComponent>("Static trunks",body);
        }
        out.resident.colliders=shapes.size();return out;
    }
    void install(std::vector<Prepared>& prepared) {
        // No lifecycle/physics callbacks execute during adoption. Roll back all
        // attached candidates if handle/storage allocation fails.
        std::vector<ActorId> installed;installed.reserve(prepared.size());
        try {
            for(auto& p:prepared) {
                auto* actor=p.actor.get();scene.adoptChild(std::move(p.actor));installed.push_back(actor->id());p.resident.actor=actor->id();
                if(!p.resident.groups.empty()) {
                    for(std::size_t i=0;i<p.resident.groups.size();++i)p.resident.groups[i].component=static_cast<Component*>(actor->children()[i].get())->id();
                    for(auto& b:p.resident.bindings)b.component=p.resident.groups[b.group].component;
                } else if(!p.resident.collision)for(std::size_t i=0;i<p.resident.bindings.size();++i)
                    p.resident.bindings[i].component=static_cast<Component*>(actor->children()[i].get())->id();
            }
        }catch(...) {for(auto id:installed)if(auto* actor=scene.findActor(id)){auto discarded=scene.detachChild(*actor);}throw;}
    }
    void retire(const Resident& r) {if(auto* actor=scene.findActor(r.actor)){auto discarded=scene.detachChild(*actor);}}
    void measure() {
        stats.visualCells=stats.collisionCells=stats.instances=stats.colliders=stats.bulkComponents=stats.gpuInstances=0;
        for(const auto& r:residents)if(scene.findActor(r.actor)) {
            if(r.collision){++stats.collisionCells;stats.colliders+=r.colliders;}
            else {++stats.visualCells;stats.instances+=r.bindings.size();stats.bulkComponents+=r.groups.size();if(!r.groups.empty())stats.gpuInstances+=r.bindings.size();}
        }
    }
};
FoliageStream::FoliageStream(Scene& scene,std::shared_ptr<const FoliageCatalog> catalog,FoliageSampler sample,FoliageStreamSettings settings) {
    require(bool(catalog)&&bool(sample),"Foliage requires catalog and source");settings.validate();
    impl_=std::make_unique<Impl>(scene,std::move(catalog),std::move(sample),settings);impl_->idle();
}
FoliageStream::~FoliageStream()=default;
void FoliageStream::update(Vec2 visualFocus,Vec2 collisionFocus) {
    auto& s=*impl_;s.idle();focusCheck(visualFocus);focusCheck(collisionFocus);
    struct Wanted {FoliageCell key;bool collision=false;int distance=0;};std::vector<Wanted> wanted;
    const auto size=s.catalog->data().cellSize;
    for(bool collision:{false,true}) {
        if(collision&&!s.settings.collision)continue;
        const auto focus=cellFor(collision?collisionFocus:visualFocus,size);
        const int radius=int(collision?s.settings.collisionRadius:s.settings.visualRadius);
        const auto maximum=collision?s.settings.maxCollisionCells:s.settings.maxVisualCells;
        std::vector<Wanted> group;
        for(int z=-radius;z<=radius;++z)for(int x=-radius;x<=radius;++x)group.push_back({{focus.x+x,focus.z+z},collision,x*x+z*z});
        for(const auto& r:s.residents)if(r.collision==collision&&s.scene.findActor(r.actor)) {
            const int x=r.key.x-focus.x,z=r.key.z-focus.z;
            if(std::abs(x)<=radius&&std::abs(z)<=radius)continue;
            if(std::abs(x)<=radius+int(s.settings.hysteresisCells)&&std::abs(z)<=radius+int(s.settings.hysteresisCells))group.push_back({r.key,collision,x*x+z*z});
        }
        std::sort(group.begin(),group.end(),[](const auto& a,const auto& b){return std::tie(a.distance,a.key)<std::tie(b.distance,b.key);});
        if(group.size()>maximum)group.resize(maximum);wanted.insert(wanted.end(),group.begin(),group.end());
    }
    // Gameplay collision admissions take priority over decorative cells.
    std::stable_sort(wanted.begin(),wanted.end(),[](const auto& a,const auto& b){return a.collision>b.collision;});
    std::vector<Impl::Resident> retained;std::vector<Impl::Prepared> prepared;std::size_t pending=0;
    retained.reserve(wanted.size());prepared.reserve(s.settings.buildsPerUpdate);
    for(const auto& w:wanted) {
        const auto existing=std::find_if(s.residents.begin(),s.residents.end(),[&](const auto& r){return r.key==w.key&&r.collision==w.collision&&s.scene.findActor(r.actor);});
        if(existing!=s.residents.end())retained.push_back(*existing);
        else if(prepared.size()<s.settings.buildsPerUpdate)prepared.push_back(s.prepare(w.key,w.collision,s.sample,s.removed));
        else ++pending;
    }
    retained.reserve(retained.size()+prepared.size());s.install(prepared);
    std::size_t evicted=0;
    for(const auto& old:s.residents)if(std::none_of(retained.begin(),retained.end(),[&](const auto& r){return r.actor==old.actor;})){s.retire(old);++evicted;}
    for(auto& p:prepared)retained.push_back(std::move(p.resident));s.residents.swap(retained);
    s.stats.built+=prepared.size();s.stats.evicted+=evicted;s.stats.pending=pending;s.measure();
    // Newly admitted content is initialized by the next explicit present call.
    if(!prepared.empty()||evicted)s.presented=false;
}
void FoliageStream::present(Vec2 focus,double seconds,const FoliageWind& wind) {
    auto& s=*impl_;s.idle();focusCheck(focus);wind.validate();require(std::isfinite(seconds)&&seconds>=0&&seconds<=1e12,"Invalid foliage simulation time");
    if(s.presented&&focus.x==s.visualFocus.x&&focus.y==s.visualFocus.y&&seconds==s.seconds&&wind.direction.x==s.wind.direction.x&&wind.direction.y==s.wind.direction.y&&wind.amplitude==s.wind.amplitude&&wind.frequency==s.wind.frequency&&wind.gust==s.wind.gust)return;
    if(s.settings.rendering==FoliageRendering::GpuInstances) {
        struct Publication {MeshInstancesComponent* component;std::shared_ptr<const MeshInstanceSet> instances;};
        struct Route {Impl::Binding* binding;std::size_t group,level;ComponentId component;};
        std::vector<Publication> publications;std::vector<Route> routes;
        if(!s.presented||focus.x!=s.visualFocus.x||focus.y!=s.visualFocus.y)for(auto& resident:s.residents)if(!resident.collision) {
            std::vector<std::vector<MeshInstance>> groups(resident.groups.size());
            for(auto& b:resident.bindings) {
                const auto& species=speciesFor(*s.catalog,b.id.species);const auto d=distance(focus,b.transform.position);auto level=b.level;
                while(level+1<species.details.size()&&d>species.details[level+1].distance*(1+s.settings.detailHysteresis))++level;
                while(level>0&&d<species.details[level].distance*(1-s.settings.detailHysteresis))--level;
                const auto group=std::size_t(std::find_if(resident.groups.begin(),resident.groups.end(),[&](const auto& g){return g.species==b.id.species&&g.level==level;})-resident.groups.begin());
                groups[group].push_back({b.transform,{1,1,1},b.key,b.phase,d<=species.drawDistance,species.castsShadow&&d<=species.shadowDistance});
                routes.push_back({&b,group,level,resident.groups[group].component});
            }
            for(std::size_t i=0;i<resident.groups.size();++i) {
                auto* component=dynamic_cast<MeshInstancesComponent*>(s.scene.findComponent(resident.groups[i].component));
                require(component!=nullptr,"Missing bulk foliage component");
                publications.push_back({component,MeshInstanceSet::create(component->mesh,std::move(groups[i]))});
            }
        }
        // Every candidate set is valid before the no-allocation publication below.
        for(auto& p:publications)p.component->setInstances(std::move(p.instances));
        for(const auto& r:routes){r.binding->group=r.group;r.binding->level=r.level;r.binding->component=r.component;}
        s.windState->configure(seconds,wind);s.visualFocus=focus;s.seconds=seconds;s.wind=wind;s.presented=true;s.stats.deformedVertices=0;return;
    }
    struct Change {FoliageMeshComponent* component;Impl::Binding* binding;std::unique_ptr<FoliageMeshComponent> candidate;std::shared_ptr<MorphPose> pose;bool enabled,shadow;std::size_t level;};
    std::vector<Change> changes;changes.reserve(s.stats.instances);std::size_t vertices=0;
    for(auto& r:s.residents)if(!r.collision)for(auto& b:r.bindings) {
        auto* component=dynamic_cast<FoliageMeshComponent*>(s.scene.findComponent(b.component));if(!component)continue;
        const auto& species=speciesFor(*s.catalog,b.id.species);const auto d=distance(focus,b.transform.position);auto level=b.level;
        while(level+1<species.details.size()&&d>species.details[level+1].distance*(1+s.settings.detailHysteresis))++level;
        while(level>0&&d<species.details[level].distance*(1-s.settings.detailHysteresis))--level;
        std::unique_ptr<FoliageMeshComponent> candidate;
        auto* deformation=component;
        if(level!=b.level||component->mesh!=species.details[level].mesh) {
            candidate=std::make_unique<FoliageMeshComponent>(species.name,species.details[level],b.phase);candidate->transform=b.transform;
            deformation=candidate.get();
        }
        const bool enabled=d<=species.drawDistance,shadow=species.castsShadow&&d<=species.shadowDistance;
        // Do not let culling or distance decide animation state. Every resident
        // weighted plant receives the same explicit time, including hidden ones.
        auto pose=deformation->prepareWind(seconds,wind);if(pose)vertices+=pose->vertices.size();
        changes.push_back({component,&b,std::move(candidate),std::move(pose),enabled,shadow,level});
    }
    for(auto& c:changes) {
        if(c.candidate) {
            c.component->mesh=c.candidate->mesh;c.component->source_=std::move(c.candidate->source_);
            c.component->bend_=std::move(c.candidate->bend_);
        }
        if(c.pose&&c.component->pose_&&!c.candidate) {
            // Preserve the renderer cache identity for an unchanged source/detail.
            // Staging completed for all plants before any live stream is changed.
            c.component->pose_->vertices.swap(c.pose->vertices);
            c.component->pose_->revision=c.pose->revision;
        } else c.component->pose_=std::move(c.pose);
        c.component->enabled=c.enabled;c.component->castsShadow=c.shadow;c.binding->level=c.level;
    }
    s.visualFocus=focus;s.seconds=seconds;s.wind=wind;s.presented=true;s.stats.deformedVertices=vertices;
}
void FoliageStream::rebuild(FoliageSampler source,std::vector<FoliageId> removedIds) {
    auto& s=*impl_;s.idle();require(bool(source),"Missing foliage source");
    std::vector<Impl::Prepared> prepared;prepared.reserve(s.residents.size());
    for(const auto& r:s.residents)prepared.push_back(s.prepare(r.key,r.collision,source,removedIds));
    std::vector<Impl::Resident> next;next.reserve(prepared.size());s.install(prepared);
    for(const auto& r:s.residents)s.retire(r);
    for(auto& p:prepared)next.push_back(std::move(p.resident));s.residents.swap(next);
    s.sample=std::move(source);s.removed.swap(removedIds);s.presented=false;s.stats.deformedVertices=0;s.measure();
}
void FoliageStream::replaceSource(FoliageSampler source) {rebuild(std::move(source),impl_->removed);}
void FoliageStream::setRemoved(std::vector<FoliageId> ids) {
    auto& s=*impl_;s.idle();require(ids.size()<=s.catalog->data().maxRemoved,"Foliage removed-ID budget exceeded");
    std::sort(ids.begin(),ids.end());require(std::adjacent_find(ids.begin(),ids.end())==ids.end(),"Duplicate foliage removal");
    for(auto id:ids)speciesFor(*s.catalog,id.species);
    if(ids==s.removed)return;rebuild(s.sample,std::move(ids));
}
void FoliageStream::remove(FoliageId id) {auto ids=impl_->removed;if(removedId(ids,id))return;ids.push_back(id);setRemoved(std::move(ids));}
std::span<const FoliageId> FoliageStream::removed() const {return impl_->removed;}
std::string FoliageStream::saveRemoved() const {
    const auto& s=*impl_;std::ostringstream out;out.imbue(std::locale::classic());
    out<<"WEFO 1 "<<s.catalog->data().world<<' '<<s.catalog->data().seed<<' '<<s.removed.size()<<'\n';
    for(auto id:s.removed)out<<id.species<<' '<<id.x<<' '<<id.z<<'\n';return out.str();
}
void FoliageStream::loadRemoved(std::string_view text) {
    const auto& d=impl_->catalog->data();require(text.size()<=128+std::size_t(d.maxRemoved)*40,"Foliage save byte budget exceeded");
    std::istringstream in{std::string(text)};in.imbue(std::locale::classic());std::string magic;unsigned version;std::uint64_t world,seed,count;
    require(bool(in>>magic>>version>>world>>seed>>count)&&magic=="WEFO"&&version==1&&world==d.world&&seed==d.seed&&count<=d.maxRemoved,"Invalid foliage save header/content identity");
    std::vector<FoliageId> ids;ids.reserve(std::size_t(count));
    for(std::uint64_t i=0;i<count;++i){FoliageId id;require(bool(in>>id.species>>id.x>>id.z),"Truncated foliage save");ids.push_back(id);}
    in>>std::ws;require(in.eof(),"Trailing foliage save content");setRemoved(std::move(ids));
}
std::optional<FoliageId> FoliageStream::placement(ComponentId id,std::uint64_t key) const {
    const auto& s=*impl_;if(!s.scene.findComponent(id))return {};
    for(const auto& r:s.residents)for(const auto& b:r.bindings)if(b.component==id&&b.key==key)return b.id;return {};
}
void FoliageStream::clear() {
    auto& s=*impl_;s.idle();for(const auto& r:s.residents)s.retire(r);s.residents.clear();s.stats={};s.presented=false;
}
const FoliageStreamStats& FoliageStream::stats() const {return impl_->stats;}
} // namespace engine
