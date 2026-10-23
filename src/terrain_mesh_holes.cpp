#include "engine/terrain_mesh.hpp"
#include <map>
#include <set>
#include <tuple>

namespace engine {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
using Edge=std::pair<unsigned,unsigned>;
Edge edge(unsigned a,unsigned b){return {std::min(a,b),std::max(a,b)};}
double orient(Vec2 a,Vec2 b,Vec2 c){return double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x);}
bool intersects(Vec2 a,Vec2 b,Vec2 c,Vec2 d){
    if(std::max(a.x,b.x)<std::min(c.x,d.x)||std::max(c.x,d.x)<std::min(a.x,b.x)||std::max(a.y,b.y)<std::min(c.y,d.y)||std::max(c.y,d.y)<std::min(a.y,b.y))return false;
    const auto p=orient(a,b,c),q=orient(a,b,d),r=orient(c,d,a),s=orient(c,d,b);
    return ((p<=0&&q>=0)||(q<=0&&p>=0))&&((r<=0&&s>=0)||(s<=0&&r>=0));
}
}
void TerrainMeshHoleSettings::validate()const{
    for(auto x:meshToTerrain.m)require(std::isfinite(x),"Non-finite mesh hole transform");
    require(meshToTerrain.m[3]==0&&meshToTerrain.m[7]==0&&meshToTerrain.m[11]==0&&meshToTerrain.m[15]==1,"Mesh hole transform must be affine");
    (void)inverseAffine(meshToTerrain);
    require(std::isfinite(contactTolerance)&&contactTolerance>=0&&contactTolerance<=1,"Invalid terrain rim contact tolerance");
    require(maxBoundaryEdges>=3&&maxBoundaryEdges<=8192&&maxCells>0&&maxCells<=4194304&&maxWork>0&&maxWork<=64000000,"Invalid terrain hole budgets");
}
TerrainMeshHoleResult buildTerrainMeshHoleMask(const TerrainAsset& terrain,const Mesh& mesh,TerrainMeshHoleSettings settings){
    settings.validate();require(!terrain.patch(),"Mesh hole preparation requires full-source terrain");
    const auto& d=terrain.data();const auto cellCount=std::size_t(d.samplesX-1)*(d.samplesZ-1);
    require(cellCount<=settings.maxCells,"Terrain hole cell budget exceeded");
    require(!mesh.skeleton&&mesh.skin.empty()&&mesh.morphTargets.empty()&&!mesh.indices.empty()&&mesh.indices.size()%3==0&&mesh.indices.size()<=90000&&!mesh.vertices.empty()&&mesh.vertices.size()<=90000,"Mesh hole preparation requires a bounded static triangle mesh");
    std::uint32_t work=0;auto spend=[&]{if(work==settings.maxWork)throw std::length_error("Terrain hole work budget exceeded");++work;};
    std::map<std::tuple<float,float,float>,unsigned> canonical;std::vector<Vec3> points;std::vector<unsigned> ids;
    for(const auto& vertex:mesh.vertices){spend();require(finite(vertex.position),"Non-finite mesh hole vertex");const auto p=transformPoint(settings.meshToTerrain,vertex.position);require(finite(p),"Mesh hole transform overflow");
        const auto [it,added]=canonical.emplace(std::tuple{p.x,p.y,p.z},unsigned(points.size()));if(added)points.push_back(p);ids.push_back(it->second);}
    std::map<Edge,unsigned> incidence;
    for(std::size_t i=0;i<mesh.indices.size();i+=3){unsigned v[3];for(int j=0;j<3;++j){spend();require(mesh.indices[i+j]<ids.size(),"Mesh hole index out of range");v[j]=ids[mesh.indices[i+j]];}
        require(v[0]!=v[1]&&v[1]!=v[2]&&v[2]!=v[0],"Degenerate mesh hole triangle");
        for(int j=0;j<3;++j)++incidence[edge(v[j],v[(j+1)%3])];}
    auto contact=[&](Vec3 p){
        const float gx=p.x/d.spacing.x,gz=p.z/d.spacing.y;
        if(gx<0||gz<0||gx>d.samplesX-1||gz>d.samplesZ-1)return false;
        const auto x=std::min(unsigned(gx),d.samplesX-2),z=std::min(unsigned(gz),d.samplesZ-2);const float u=gx-x,v=gz-z;
        const float a=d.heights[std::size_t(z)*d.samplesX+x],b=d.heights[std::size_t(z)*d.samplesX+x+1],c=d.heights[std::size_t(z+1)*d.samplesX+x],e=d.heights[std::size_t(z+1)*d.samplesX+x+1];
        const float h=u+v<=1?a+(b-a)*u+(c-a)*v:e+(c-e)*(1-u)+(b-e)*(1-v);
        return std::abs(p.y-h)<=settings.contactTolerance;
    };
    std::vector<Edge> candidates;std::set<unsigned> endpoints;
    for(auto [e,n]:incidence){spend();if(contact(points[e.first])&&contact(points[e.second])&&contact((points[e.first]+points[e.second])*.5f)){
        if(n==1){require(candidates.size()<settings.maxBoundaryEdges,"Terrain hole boundary budget exceeded");candidates.push_back(e);endpoints.insert(e.first);endpoints.insert(e.second);}}}
    // Geometric T-junctions/overlapping interior sheets can leave index-open
    // edges on the plateau. A real rim has no terrain-contact surface on at
    // least one side. Grid bins bound the local coverage tests.
    std::map<std::size_t,std::vector<unsigned>> bins;
    if(!candidates.empty())for(unsigned i=0;i<mesh.indices.size();i+=3){
        const auto a=points[ids[mesh.indices[i]]],b=points[ids[mesh.indices[i+1]]],c=points[ids[mesh.indices[i+2]]];
        if(std::abs(orient({a.x,a.z},{b.x,b.z},{c.x,c.z}))<1e-15)continue;
        double x0=std::min({a.x,b.x,c.x})/d.spacing.x,x1=std::max({a.x,b.x,c.x})/d.spacing.x,z0=std::min({a.z,b.z,c.z})/d.spacing.y,z1=std::max({a.z,b.z,c.z})/d.spacing.y;
        if(x1<0||z1<0||x0>d.samplesX-1||z0>d.samplesZ-1)continue;
        const auto minX=unsigned(std::max(0.,x0)),maxX=unsigned(std::min(double(d.samplesX-2),x1)),minZ=unsigned(std::max(0.,z0)),maxZ=unsigned(std::min(double(d.samplesZ-2),z1));
        for(unsigned z=minZ;z<=maxZ;++z)for(unsigned x=minX;x<=maxX;++x){spend();bins[std::size_t(z)*(d.samplesX-1)+x].push_back(i);}
    }
    auto covered=[&](Vec2 p){
        if(p.x<0||p.y<0||p.x>(d.samplesX-1)*d.spacing.x||p.y>(d.samplesZ-1)*d.spacing.y)return false;
        const auto x=std::min(unsigned(p.x/d.spacing.x),d.samplesX-2),z=std::min(unsigned(p.y/d.spacing.y),d.samplesZ-2);
        const auto bucket=bins.find(std::size_t(z)*(d.samplesX-1)+x);if(bucket==bins.end())return false;
        for(auto i:bucket->second){spend();const auto a=points[ids[mesh.indices[i]]],b=points[ids[mesh.indices[i+1]]],c=points[ids[mesh.indices[i+2]]];
            const Vec2 aa{a.x,a.z},bb{b.x,b.z},cc{c.x,c.z};const auto area=orient(aa,bb,cc),u=orient(p,bb,cc)/area,v=orient(aa,p,cc)/area,w=1-u-v;
            if(u>=-1e-9&&v>=-1e-9&&w>=-1e-9&&contact({p.x,float(u*a.y+v*b.y+w*c.y),p.y}))return true;
        }return false;
    };
    std::vector<Edge> exposed;endpoints.clear();
    for(auto e:candidates){spend();const auto a=points[e.first],b=points[e.second];const double dx=b.x-a.x,dz=b.z-a.z,length=std::hypot(dx,dz);
        if(length<1e-12){exposed.push_back(e);endpoints.insert(e.first);endpoints.insert(e.second);continue;}
        const double amount=std::min(d.spacing.x,d.spacing.y)*1e-4/length;const Vec2 p{(a.x+b.x)*.5f,(a.z+b.z)*.5f},offset{float(-dz*amount),float(dx*amount)};
        if(covered({p.x+offset.x,p.y+offset.y})&&covered({p.x-offset.x,p.y-offset.y}))continue;
        exposed.push_back(e);endpoints.insert(e.first);endpoints.insert(e.second);
    }
    candidates=std::move(exposed);
    // A long exposed edge and its adjacent shorter edges cancel after splitting.
    // Only terrain-contact boundaries participate; underground ends never cut.
    incidence.clear();
    for(auto e:candidates){const auto a=points[e.first],delta=points[e.second]-a;const double length2=dot(delta,delta);require(length2>1e-16,"Collapsed terrain rim edge");
        std::vector<std::pair<double,unsigned>> cuts{{0,e.first},{1,e.second}};
        for(auto id:endpoints){spend();if(id==e.first||id==e.second)continue;const auto offset=points[id]-a;const double t=dot(offset,delta)/length2;
            if(t<=0||t>=1)continue;const auto distance=offset-delta*float(t);if(dot(distance,distance)<=1e-12*std::max(1.,length2))cuts.push_back({t,id});}
        std::sort(cuts.begin(),cuts.end());for(std::size_t i=1;i<cuts.size();++i)++incidence[edge(cuts[i-1].second,cuts[i].second)];}
    std::map<unsigned,std::vector<unsigned>> neighbors;std::vector<Edge> boundary;
    for(auto [e,n]:incidence)if(n==1){neighbors[e.first].push_back(e.second);neighbors[e.second].push_back(e.first);boundary.push_back(e);}
    for(const auto& [id,next]:neighbors)require(next.size()==2,"Terrain-contact mesh rim must form closed loops");
    require(boundary.size()<=settings.maxBoundaryEdges,"Terrain hole boundary budget exceeded");
    auto flat=[&](unsigned id){auto snap=[](float p,float spacing){const double q=double(p)/spacing,nearest=std::round(q);return std::abs(q-nearest)<1e-5?float(nearest*spacing):p;};return Vec2{snap(points[id].x,d.spacing.x),snap(points[id].z,d.spacing.y)};};
    for(std::size_t i=0;i<boundary.size();++i)for(std::size_t j=0;j<i;++j){spend();const auto a=boundary[i],b=boundary[j];
        if(a.first==b.first||a.first==b.second||a.second==b.first||a.second==b.second)continue;
        require(!intersects(flat(a.first),flat(a.second),flat(b.first),flat(b.second)),"Terrain rim projections intersect");}
    TerrainMeshHoleResult result;result.holes=d.holes;if(result.holes.empty())result.holes.resize(cellCount);result.footprint.resize(cellCount);
    std::set<unsigned> visited;
    for(const auto& [start,next]:neighbors){if(visited.contains(start))continue;unsigned previous=start,current=next[0];double area=0;
        do{spend();visited.insert(previous);const auto a=flat(previous),b=flat(current);area+=double(a.x)*b.y-double(b.x)*a.y;const auto& around=neighbors.at(current);const auto following=around[0]==previous?around[1]:around[0];previous=current;current=following;}while(previous!=start);
        require(std::abs(area)>1e-10*double(d.spacing.x)*d.spacing.y,"Terrain rim projection has zero area");++result.loops;
    }
    if(boundary.empty())return result;
    Vec2 lo=flat(boundary[0].first),hi=lo;
    for(auto e:boundary)for(auto id:{e.first,e.second}){const auto p=flat(id);lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);}
    const auto minX=unsigned(lo.x/d.spacing.x),minZ=unsigned(lo.y/d.spacing.y),maxX=std::min(d.samplesX-2,unsigned(std::ceil(hi.x/d.spacing.x)-1)),maxZ=std::min(d.samplesZ-2,unsigned(std::ceil(hi.y/d.spacing.y)-1));
    for(unsigned z=minZ;z<=maxZ;++z)for(unsigned x=minX;x<=maxX;++x){
        const Vec2 centre{(x+.5f)*d.spacing.x,(z+.5f)*d.spacing.y};
        bool inside=false,crossing=false;
        for(auto e:boundary){spend();const auto p=flat(e.first),q=flat(e.second);
            if((p.y>centre.y)!=(q.y>centre.y)&&centre.x<double(q.x-p.x)*(centre.y-p.y)/(q.y-p.y)+p.x)inside=!inside;
            double first=0,last=1;
            for(int axis=0;axis<2;++axis){const double step=axis?d.spacing.y:d.spacing.x,low=(axis?z:x)*step+step*1e-8,high=low+step*(1-2e-8),origin=axis?p.y:p.x,delta=double(axis?q.y:q.x)-origin;
                if(std::abs(delta)<1e-30){if(origin<=low||origin>=high){first=2;break;}}
                else{double a=(low-origin)/delta,b=(high-origin)/delta;if(a>b)std::swap(a,b);first=std::max(first,a);last=std::min(last,b);}
            }
            if(first<=last)crossing=true;
        }
        if(inside||crossing){const auto id=std::size_t(z)*(d.samplesX-1)+x;result.footprint[id]=1;++result.footprintCells;if(!result.holes[id])++result.newlyCutCells;result.holes[id]=1;}
    }
    return result;
}
TerrainEdit cutTerrainHolesFromMesh(const TerrainAsset& terrain,const Mesh& mesh,TerrainMeshHoleSettings settings){
    return terrain.withHoles(buildTerrainMeshHoleMask(terrain,mesh,settings).holes);
}
} // namespace engine
