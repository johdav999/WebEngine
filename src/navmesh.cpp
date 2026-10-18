#include "engine/navmesh.hpp"
#include "engine/scene.hpp"
#include "engine/collider_component.hpp"
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourAlloc.h>
#include <limits>
#include <queue>

namespace engine {
namespace {
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
double distance(Vec3 a,Vec3 b) { return std::hypot(double(a.x)-b.x,double(a.y)-b.y,double(a.z)-b.z); }
void require(bool value,const char* message) { if(!value) throw std::invalid_argument(message); }
struct Context:rcContext {
    std::string error;
    Context():rcContext(true) {}
    void doLog(rcLogCategory category,const char* msg,int len) override { if(category==RC_LOG_ERROR) error.assign(msg,std::size_t(len)); }
};
template<class T,void(*Free)(T*)> using Owned=std::unique_ptr<T,decltype(Free)>;
void built(bool ok,const Context& c) { if(!ok) throw std::runtime_error(c.error.empty() ? "Navmesh bake failed" : c.error); }
Vec3 point(const float* p) { return {p[0],p[1],p[2]}; }
std::array<float,3> array(Vec3 p) { return {p.x,p.y,p.z}; }
}
struct NavMesh::Impl {
    struct Link { std::uint32_t to;Vec3 portal; };
    Owned<dtNavMesh,dtFreeNavMesh> nav{nullptr,dtFreeNavMesh};
    std::vector<Vec3> centers;
    std::vector<std::vector<Link>> links;
    dtPolyRef base=0;
};
NavMesh::NavMesh():impl_(std::make_unique<Impl>()) {}
NavMesh::~NavMesh()=default;
void NavMeshBuildSettings::validate() const {
    require(std::isfinite(cellSize)&&cellSize>=.01f&&cellSize<=10&&std::isfinite(cellHeight)&&cellHeight>=.01f&&cellHeight<=10,"Invalid navmesh voxel size");
    require(std::isfinite(agentRadius)&&agentRadius>0&&std::isfinite(agentHeight)&&agentHeight>0
        &&std::isfinite(maxSlopeAngle)&&maxSlopeAngle>0&&maxSlopeAngle<pi/2
        &&std::isfinite(maxStepHeight)&&maxStepHeight>=0&&maxStepHeight<agentHeight
        &&std::isfinite(endpointSnap)&&endpointSnap>=0&&std::isfinite(verticalSnap)&&verticalSnap>=0,"Invalid navmesh agent settings");
    require(agentHeight/cellHeight>=3&&agentHeight/cellHeight<=4096&&agentRadius/cellSize<=255,"Navmesh agent exceeds voxel limits");
    require(maxCells>0&&maxCells<=1048576&&maxRasterWork>0&&maxRasterWork<=8000000&&maxPolygons>0&&maxPolygons<=4096,"Invalid navmesh build budgets");
}
bool NavMesh::compatible(const CharacterMotorSettings& m) const noexcept {
    return m.radius<=settings_.agentRadius+1e-5f&&2*(m.radius+m.halfHeight)<=settings_.agentHeight+1e-5f
        &&m.maxSlopeAngle+1e-5f>=settings_.maxSlopeAngle&&m.stepHeight+1e-5f>=settings_.maxStepHeight;
}
std::shared_ptr<const NavMesh> NavMesh::bake(const Scene& scene,NavMeshBuildSettings settings,PhysicsQueryFilter filter) {
    settings.validate();const auto* world=scene.physicsIfCreated();require(world!=nullptr,"Synchronize static collision before baking navigation");
    filter.includeDynamic=filter.includeKinematic=filter.includeSensors=false;
    const auto probe=CollisionShape::sphere(.01f);
    return bakeGeometry(world->triangleGeometry(filter),settings,[&](Vec3 p) {
        Transform pose;pose.position=p+Vec3{0,.03f,0};
        return world->overlap(*probe,pose,filter).empty();
    });
}
std::shared_ptr<const NavMesh> NavMesh::bake(const PhysicsGeometry& geometry,NavMeshBuildSettings settings) {
    return bakeGeometry(geometry,settings,{});
}
std::shared_ptr<const NavMesh> NavMesh::bakeGeometry(const PhysicsGeometry& geometry,NavMeshBuildSettings settings,const std::function<bool(Vec3)>& surfaceClear) {
    settings.validate();
    require(geometry.vertices.size()>=3&&geometry.vertices.size()<=300000&&!geometry.indices.empty()
        &&geometry.indices.size()%3==0&&geometry.indices.size()<=300000,"Invalid navmesh source size");
    std::vector<float> vertices;vertices.reserve(geometry.vertices.size()*3);
    for(auto v:geometry.vertices) {
        require(finite(v)&&std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)})/std::min(settings.cellSize,settings.cellHeight)<1e7f,"Invalid navmesh source coordinate");
        vertices.insert(vertices.end(),{v.x,v.y,v.z});
    }
    std::vector<int> triangles;triangles.reserve(geometry.indices.size());std::size_t rasterWork=0;
    for(std::size_t i=0;i<geometry.indices.size();i+=3) {
        for(int k=0;k<3;++k) require(geometry.indices[i+k]<geometry.vertices.size(),"Navmesh source index out of range");
        const auto a=geometry.vertices[geometry.indices[i]],b=geometry.vertices[geometry.indices[i+1]],c=geometry.vertices[geometry.indices[i+2]];
        const auto n=cross(b-a,c-a);require(finite(n)&&dot(n,n)>1e-12f,"Degenerate navmesh source triangle");
        const auto w=std::ceil((double(std::max({a.x,b.x,c.x}))-std::min({a.x,b.x,c.x}))/settings.cellSize)+2;
        const auto h=std::ceil((double(std::max({a.z,b.z,c.z}))-std::min({a.z,b.z,c.z}))/settings.cellSize)+2;
        require(w*h<=double(settings.maxRasterWork-rasterWork),"Navmesh raster work budget exceeded");rasterWork+=std::size_t(w*h);
        for(int k=0;k<3;++k) triangles.push_back(int(geometry.indices[i+k]));
    }
    Context context;rcConfig cfg{};cfg.cs=settings.cellSize;cfg.ch=settings.cellHeight;
    cfg.walkableSlopeAngle=settings.maxSlopeAngle*180/pi;
    cfg.walkableHeight=int(std::ceil(settings.agentHeight/cfg.ch));
    cfg.walkableClimb=int(std::floor(settings.maxStepHeight/cfg.ch+1e-5f));cfg.walkableRadius=int(std::ceil(settings.agentRadius/cfg.cs));
    cfg.maxEdgeLen=std::max(1,int(2/cfg.cs));cfg.maxSimplificationError=.5f;cfg.maxVertsPerPoly=6;
    cfg.detailSampleDist=cfg.cs*6;cfg.detailSampleMaxError=cfg.ch;
    rcCalcBounds(vertices.data(),int(geometry.vertices.size()),cfg.bmin,cfg.bmax);
    cfg.bmin[1]-=cfg.ch;cfg.bmax[1]+=settings.agentHeight+cfg.ch;
    const auto width=std::floor((double(cfg.bmax[0])-cfg.bmin[0])/cfg.cs+.5),height=std::floor((double(cfg.bmax[2])-cfg.bmin[2])/cfg.cs+.5);
    require(width>0&&height>0&&width*height<=double(settings.maxCells),"Navmesh grid cell budget exceeded");
    require((double(cfg.bmax[1])-cfg.bmin[1])/cfg.ch<RC_SPAN_MAX_HEIGHT,"Navmesh vertical span range exceeded");
    cfg.width=int(width);cfg.height=int(height);
    Owned<rcHeightfield,rcFreeHeightField> solid(rcAllocHeightfield(),rcFreeHeightField);
    Owned<rcCompactHeightfield,rcFreeCompactHeightfield> compact(rcAllocCompactHeightfield(),rcFreeCompactHeightfield);
    Owned<rcContourSet,rcFreeContourSet> contours(rcAllocContourSet(),rcFreeContourSet);
    Owned<rcPolyMesh,rcFreePolyMesh> polys(rcAllocPolyMesh(),rcFreePolyMesh);
    Owned<rcPolyMeshDetail,rcFreePolyMeshDetail> detail(rcAllocPolyMeshDetail(),rcFreePolyMeshDetail);
    if(!solid||!compact||!contours||!polys||!detail) throw std::bad_alloc();
    built(rcCreateHeightfield(&context,*solid,cfg.width,cfg.height,cfg.bmin,cfg.bmax,cfg.cs,cfg.ch),context);
    std::vector<unsigned char> areas(triangles.size()/3);
    rcMarkWalkableTriangles(&context,cfg.walkableSlopeAngle,vertices.data(),int(geometry.vertices.size()),triangles.data(),int(areas.size()),areas.data());
    built(rcRasterizeTriangles(&context,vertices.data(),int(geometry.vertices.size()),triangles.data(),areas.data(),int(areas.size()),*solid,cfg.walkableClimb),context);
    rcFilterLowHangingWalkableObstacles(&context,cfg.walkableClimb,*solid);
    rcFilterLedgeSpans(&context,cfg.walkableHeight,cfg.walkableClimb,*solid);
    rcFilterWalkableLowHeightSpans(&context,cfg.walkableHeight,*solid);
    built(rcBuildCompactHeightfield(&context,cfg.walkableHeight,cfg.walkableClimb,*solid,*compact),context);
    // Triangle rasterization describes surfaces, including enclosed voids inside
    // tall solid colliders. The scene adapter excludes these using authoritative
    // solid occupancy before erosion/regions; raw triangle sources define their
    // own navigable interiors. No scene callback survives this synchronous bake.
    if(surfaceClear) {
        require(std::size_t(compact->spanCount)<=settings.maxRasterWork,"Navmesh occupancy query budget exceeded");
        for(int z=0;z<compact->height;++z) for(int x=0;x<compact->width;++x) {
            const auto cell=compact->cells[x+z*compact->width];
            for(unsigned i=cell.index;i<unsigned(cell.index+cell.count);++i) if(compact->areas[i]!=RC_NULL_AREA) {
                const Vec3 p{compact->bmin[0]+(x+.5f)*cfg.cs,compact->bmin[1]+compact->spans[i].y*cfg.ch,compact->bmin[2]+(z+.5f)*cfg.cs};
                if(!surfaceClear(p)) compact->areas[i]=RC_NULL_AREA;
            }
        }
    }
    built(rcErodeWalkableArea(&context,cfg.walkableRadius,*compact),context);
    built(rcBuildRegionsMonotone(&context,*compact,0,0,0),context);
    built(rcBuildContours(&context,*compact,cfg.maxSimplificationError,cfg.maxEdgeLen,*contours),context);
    built(rcBuildPolyMesh(&context,*contours,cfg.maxVertsPerPoly,*polys),context);
    require(polys->npolys>0&&std::size_t(polys->npolys)<=settings.maxPolygons,"Empty navmesh or polygon budget exceeded");
    built(rcBuildPolyMeshDetail(&context,*polys,*compact,cfg.detailSampleDist,cfg.detailSampleMaxError,*detail),context);
    for(int i=0;i<polys->npolys;++i) { polys->areas[i]=0;polys->flags[i]=1; }
    dtNavMeshCreateParams params{};params.verts=polys->verts;params.vertCount=polys->nverts;
    params.polys=polys->polys;params.polyAreas=polys->areas;params.polyFlags=polys->flags;params.polyCount=polys->npolys;params.nvp=polys->nvp;
    params.detailMeshes=detail->meshes;params.detailVerts=detail->verts;params.detailVertsCount=detail->nverts;
    params.detailTris=detail->tris;params.detailTriCount=detail->ntris;params.walkableHeight=settings.agentHeight;
    params.walkableRadius=settings.agentRadius;params.walkableClimb=settings.maxStepHeight;
    rcVcopy(params.bmin,polys->bmin);rcVcopy(params.bmax,polys->bmax);params.cs=cfg.cs;params.ch=cfg.ch;params.buildBvTree=true;
    unsigned char* bytes=nullptr;int size=0;
    if(!dtCreateNavMeshData(&params,&bytes,&size)) throw std::runtime_error("Could not build navmesh query data");
    std::unique_ptr<unsigned char,decltype(&dtFree)> data(bytes,dtFree);
    auto result=std::shared_ptr<NavMesh>(new NavMesh);result->settings_=settings;result->impl_->nav.reset(dtAllocNavMesh());
    if(!result->impl_->nav) throw std::bad_alloc();
    if(dtStatusFailed(result->impl_->nav->init(bytes,size,DT_TILE_FREE_DATA))) throw std::runtime_error("Could not initialize navmesh query data");
    data.release();auto& impl=*result->impl_;
    const auto* tile=std::as_const(*impl.nav).getTile(0);impl.base=impl.nav->getPolyRefBase(tile);
    auto mesh=std::make_shared<Mesh>();impl.centers.resize(std::size_t(polys->npolys));impl.links.resize(std::size_t(polys->npolys));
    for(int i=0;i<tile->header->vertCount;++i) mesh->vertices.push_back({point(tile->verts+i*3),{0,1,0},{}});
    for(int i=0;i<tile->header->polyCount;++i) {
        const auto& poly=tile->polys[i];Vec3 center{};
        for(int k=0;k<poly.vertCount;++k) center=center+point(tile->verts+poly.verts[k]*3);
        impl.centers[std::size_t(i)]=center*(1.f/poly.vertCount);
        for(int k=1;k+1<poly.vertCount;++k) {
            auto a=std::uint32_t(poly.verts[0]),b=std::uint32_t(poly.verts[k]),c=std::uint32_t(poly.verts[k+1]);
            if(cross(mesh->vertices[b].position-mesh->vertices[a].position,mesh->vertices[c].position-mesh->vertices[a].position).y<0) std::swap(b,c);
            mesh->indices.insert(mesh->indices.end(),{a,b,c});
        }
        for(int k=0;k<poly.vertCount;++k) if(poly.neis[k]&&!(poly.neis[k]&DT_EXT_LINK)) {
            const auto portal=(point(tile->verts+poly.verts[k]*3)+point(tile->verts+poly.verts[(k+1)%poly.vertCount]*3))*.5f;
            impl.links[std::size_t(i)].push_back({std::uint32_t(poly.neis[k]-1),portal});
        }
    }
    result->debugMesh_=std::move(mesh);result->stats_={areas.size(),std::size_t(cfg.width)*std::size_t(cfg.height),std::size_t(polys->npolys),std::size_t(polys->nverts),std::size_t(size)};
    return result;
}
std::optional<NavMeshLocation> NavMesh::project(Vec3 p,float range) const {
    require(finite(p)&&std::isfinite(range)&&range>=0,"Invalid navmesh projection");
    dtNavMeshQuery query;if(dtStatusFailed(query.init(impl_->nav.get(),4096))) throw std::bad_alloc();
    const auto origin=array(p);const std::array half={range,settings_.verticalSnap,range};std::array<float,3> nearest{};
    dtPolyRef ref=0;dtQueryFilter filter;
    if(dtStatusFailed(query.findNearestPoly(origin.data(),half.data(),&filter,&ref,nearest.data()))||!ref) return {};
    const auto projected=point(nearest.data());
    if(std::hypot(double(projected.x)-p.x,double(projected.z)-p.z)>double(range)+1e-5||std::abs(projected.y-p.y)>settings_.verticalSnap+1e-5f) return {};
    return NavMeshLocation{projected,std::uint32_t(ref-impl_->base)};
}
WaypointPath NavMesh::findPath(Vec3 start,Vec3 goal,const WaypointSearchSettings& settings) const {
    settings.validate();require(finite(start)&&finite(goal),"Invalid navmesh query endpoints");
    WaypointPath result;const auto range=std::min(settings.snapDistance,settings_.endpointSnap);
    const auto from=project(start,range),to=project(goal,range);if(!from||!to) return result;
    bool exhausted=false;
    auto test=[&](Vec3 a,Vec3 b) {
        if(distance(a,b)<1e-6) return true;
        if(result.testedSegments==settings.segmentBudget) { exhausted=true;return false; }
        ++result.testedSegments;return !settings.canTraverse||settings.canTraverse(a,b);
    };
    auto fail=[&]() { if(exhausted) result.status=PathStatus::BudgetExceeded;return result; };
    if(!test(start,from->position)) return fail();
    if(from->polygon==to->polygon) {
        result.expanded=1;
        if(!test(from->position,to->position)) return fail();
        result.status=PathStatus::Complete;result.nodes={from->polygon};result.points={from->position,to->position};return result;
    }
    const auto& centers=impl_->centers;
    if(!test(from->position,centers[from->polygon])||!test(centers[to->polygon],to->position)) return fail();
    using Entry=std::pair<double,std::uint32_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    std::vector<double> costs(centers.size(),std::numeric_limits<double>::infinity());
    std::vector<std::uint32_t> parents(centers.size(),std::uint32_t(centers.size()));
    std::vector<Vec3> portals(centers.size());std::vector<bool> closed(centers.size());
    costs[from->polygon]=distance(from->position,centers[from->polygon]);open.emplace(costs[from->polygon]+distance(centers[from->polygon],to->position),from->polygon);
    while(!open.empty()) {
        const auto current=open.top().second;open.pop();if(closed[current]) continue;
        if(result.expanded==settings.expansionBudget) { result.status=PathStatus::BudgetExceeded;return result; }
        closed[current]=true;++result.expanded;
        if(current==to->polygon) break;
        for(const auto& link:impl_->links[current]) {
            const auto cost=costs[current]+distance(centers[current],link.portal)+distance(link.portal,centers[link.to]);
            if(closed[link.to]||cost>=costs[link.to]) continue;
            if(!test(centers[current],link.portal)||!test(link.portal,centers[link.to])) { if(exhausted) return fail();continue; }
            costs[link.to]=cost;parents[link.to]=current;portals[link.to]=link.portal;
            open.emplace(cost+distance(centers[link.to],to->position),link.to);
        }
    }
    if(!closed[to->polygon]) return result;
    std::vector<std::uint32_t> corridor;
    for(auto node=to->polygon;;node=parents[node]) { corridor.push_back(node);if(node==from->polygon) break; }
    std::reverse(corridor.begin(),corridor.end());
    std::vector<Vec3> fallback={from->position,centers[corridor.front()]};
    for(std::size_t i=1;i<corridor.size();++i) { fallback.push_back(portals[corridor[i]]);fallback.push_back(centers[corridor[i]]); }
    fallback.push_back(to->position);
    // Funnel straightening is optional. A fully checked center/portal path is
    // retained if smoothing hits a dynamic obstruction or lacks query budget.
    dtNavMeshQuery query;if(dtStatusFailed(query.init(impl_->nav.get(),4096))) throw std::bad_alloc();
    std::vector<dtPolyRef> refs;for(auto polygon:corridor) refs.push_back(impl_->base|dtPolyRef(polygon));
    const auto a=array(from->position),b=array(to->position);
    std::vector<float> straight(4098*3);int count=0;
    const auto status=query.findStraightPath(a.data(),b.data(),refs.data(),int(refs.size()),straight.data(),nullptr,nullptr,&count,4098,DT_STRAIGHTPATH_ALL_CROSSINGS);
    if(!dtStatusFailed(status)&&!dtStatusDetail(status,DT_BUFFER_TOO_SMALL)&&count>0) {
        std::vector<Vec3> smooth;for(int i=0;i<count;++i) smooth.push_back(point(straight.data()+i*3));
        bool clear=true;
        if(smooth.size()-1>settings.segmentBudget-result.testedSegments) clear=false;
        else for(std::size_t i=1;i<smooth.size();++i) if(!test(smooth[i-1],smooth[i])) { clear=false;break; }
        if(clear) fallback=std::move(smooth);
    }
    if(fallback.size()>4098) { result.status=PathStatus::BudgetExceeded;return result; }
    result.status=PathStatus::Complete;result.nodes=std::move(corridor);result.points=std::move(fallback);return result;
}
std::optional<Vec3> NavMesh::raycast(Vec3 origin,Vec3 direction,float maxDistance) const {
    require(finite(origin)&&finite(direction)&&std::isfinite(maxDistance)&&maxDistance>0,"Invalid navmesh ray");
    const auto length=distance(direction,{});require(length>1e-7,"Invalid navmesh ray direction");
    const Vec3 d{float(direction.x/length),float(direction.y/length),float(direction.z/length)};
    std::optional<Vec3> hit;double nearest=maxDistance;
    const auto& mesh=*debugMesh_;
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
        const auto e1=b-a,e2=c-a,p=cross(d,e2);const auto det=double(dot(e1,p));if(std::abs(det)<1e-9) continue;
        const auto t=origin-a;const auto u=dot(t,p)/det;if(u<0||u>1) continue;
        const auto q=cross(t,e1);const auto v=dot(d,q)/det;if(v<0||u+v>1) continue;
        const auto at=dot(e2,q)/det;if(at<0||at>nearest) continue;
        nearest=at;hit=origin+d*float(at);
    }
    return hit;
}
} // namespace engine
