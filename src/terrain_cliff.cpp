#include "engine/terrain_cliff.hpp"
#include "engine/mesh_optimizer.hpp"
#include "terrain_surface.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <map>

namespace engine {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
bool finite(float n){return std::isfinite(n);}
float smooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
float sq(Vec2 p){return p.x*p.x+p.y*p.y;}
Vec2 sub(Vec2 a,Vec2 b){return {a.x-b.x,a.y-b.y};}
float dot2(Vec2 a,Vec2 b){return a.x*b.x+a.y*b.y;}
using Edge=std::pair<unsigned,unsigned>;
Edge edge(unsigned a,unsigned b){return {std::min(a,b),std::max(a,b)};}
struct Segment {Vec2 a,b,tangent;float length,start;};
struct Sample {float side=0,along=0,distance2=std::numeric_limits<float>::infinity();};
}
void TerrainCliffSettings::validate()const{
    require(finite(upperHeight)&&finite(lowerHeight)&&std::abs(upperHeight)<=10000&&std::abs(lowerHeight)<=10000&&
        upperHeight-lowerHeight>=.1f&&upperHeight-lowerHeight<=100,"Cliff drop must be 0.1 through 100 metres");
    require(finite(faceAngle)&&faceAngle>=60&&faceAngle<=89,"Cliff face angle must be 60 through 89 degrees");
    require(finite(shoulderWidth)&&shoulderWidth>=.25f&&shoulderWidth<=50&&finite(endFade)&&endFade>=.25f&&endFade<=50&&
        finite(blendWidth)&&blendWidth>=.1f&&blendWidth<=shoulderWidth&&finite(maxEdgeLength)&&maxEdgeLength>=.05f&&maxEdgeLength<=2&&
        finite(irregularity)&&irregularity>=0&&irregularity<=.2f,"Invalid cliff refinement or transition settings");
    require(lodLevels<=3&&maxCells>0&&maxCells<=262144&&maxOutputTriangles>0&&maxOutputTriangles<=30000&&maxWork>0&&maxWork<=64000000,"Invalid cliff work budget");
}
TerrainCliffResult buildTerrainMeshCliff(const TerrainAsset& terrain,std::span<const Vec2> path,
    std::shared_ptr<const Material> rock,TerrainCliffSettings settings){
    settings.validate();require(!terrain.patch(),"Cliffs require full-source terrain");
    require(path.size()>=2&&path.size()<=16&&rock,"Cliff requires a rock material and 2 through 16 path points");
    const auto& d=terrain.data();const auto count=std::size_t(d.samplesX-1)*(d.samplesZ-1);
    require(count<=settings.maxCells,"Cliff source cell budget exceeded");
    const auto material=terrain_surface::blend(*d.material,*rock);
    std::uint32_t work=0;auto spend=[&](std::uint32_t n=1){if(n>settings.maxWork-work)throw std::length_error("Cliff work budget exceeded");work+=n;};
    const float drop=settings.upperHeight-settings.lowerHeight,run=drop/std::tan(settings.faceAngle*pi/180);
    const float radius=run*.5f+settings.shoulderWidth+std::max(d.spacing.x,d.spacing.y);
    Vec2 minimum=path.front(),maximum=minimum;std::vector<Segment> segments;float total=0;
    for(auto p:path){spend();require(finite(p.x)&&finite(p.y)&&std::abs(p.x)<=1e6f&&std::abs(p.y)<=1e6f,"Non-finite or excessive cliff path coordinate");
        minimum.x=std::min(minimum.x,p.x);minimum.y=std::min(minimum.y,p.y);maximum.x=std::max(maximum.x,p.x);maximum.y=std::max(maximum.y,p.y);}
    for(std::size_t i=1;i<path.size();++i){const auto delta=sub(path[i],path[i-1]);const float length=std::sqrt(sq(delta));
        require(length>=.25f,"Cliff path contains a repeated or short segment");const Vec2 tangent{delta.x/length,delta.y/length};
        if(!segments.empty())require(dot2(segments.back().tangent,tangent)>.5f,"Cliff path bend exceeds 60 degrees");
        segments.push_back({path[i-1],path[i],tangent,length,total});total+=length;}
    require(total>2*settings.endFade,"Cliff path is shorter than its end transitions");
    auto pointSegment=[](Vec2 p,const Segment& s){const auto v=sub(p,s.a);const float t=std::clamp(dot2(v,s.tangent),0.f,s.length);return sq(sub(v,{s.tangent.x*t,s.tangent.y*t}));};
    for(std::size_t i=0;i<segments.size();++i)for(std::size_t j=i+2;j<segments.size();++j){spend();
        const auto& a=segments[i];const auto& b=segments[j];
        auto cross2=[](Vec2 p,Vec2 q){return p.x*q.y-p.y*q.x;};const auto ab=sub(a.b,a.a),cd=sub(b.b,b.a),ac=sub(b.a,a.a);const float denom=cross2(ab,cd);
        if(std::abs(denom)>1e-8f){const float u=cross2(ac,cd)/denom,v=cross2(ac,ab)/denom;require(!(u>=0&&u<=1&&v>=0&&v<=1),"Cliff path crosses itself");}
        const auto gap=sub(b.a,a.b);
        const bool forward=dot2(a.tangent,b.tangent)>.5f&&dot2(gap,a.tangent)>=0&&dot2(gap,b.tangent)>=0;
        require(forward||std::min({pointSegment(a.a,b),pointSegment(a.b,b),pointSegment(b.a,a),pointSegment(b.b,a)})>4*radius*radius,"Cliff path approaches itself too closely");}
    require(minimum.x-radius>=0&&minimum.y-radius>=0&&maximum.x+radius<=(d.samplesX-1)*d.spacing.x&&maximum.y+radius<=(d.samplesZ-1)*d.spacing.y,"Cliff collar must fit inside the source terrain");
    TerrainCliffResult result;result.cutX=unsigned(std::floor((minimum.x-radius)/d.spacing.x));result.cutZ=unsigned(std::floor((minimum.y-radius)/d.spacing.y));
    const unsigned endX=unsigned(std::ceil((maximum.x+radius)/d.spacing.x)),endZ=unsigned(std::ceil((maximum.y+radius)/d.spacing.y));
    result.cellsX=endX-result.cutX;result.cellsZ=endZ-result.cutZ;
    require(std::size_t(result.cellsX)*result.cellsZ<=4096,"Cliff cut exceeds 4096 cells");
    auto holes=d.holes;if(holes.empty())holes.resize(count);result.footprint.resize(count);
    for(unsigned z=result.cutZ;z<endZ;++z)for(unsigned x=result.cutX;x<endX;++x){spend();const auto id=std::size_t(z)*(d.samplesX-1)+x;
        require(!d.hole(x,z),"Cliff collar overlaps an existing terrain hole");
        require(d.cellSurfaces.empty()||!d.cellSurfaces[id]||d.cellSurfaces[id]==d.physicalSurface,"Cliff collar has heterogeneous physical surfaces");
        holes[id]=result.footprint[id]=1;}
    auto nearest=[&](Vec2 p){Sample out;for(const auto& s:segments){spend();const auto v=sub(p,s.a);const float t=std::clamp(dot2(v,s.tangent),0.f,s.length);
        const auto q=sub(v,{s.tangent.x*t,s.tangent.y*t});const float distance2=sq(q);
        if(distance2<out.distance2)out={std::copysign(std::sqrt(distance2),s.tangent.x*v.y-s.tangent.y*v.x),s.start+t,distance2};}return out;};
    auto ground=[&](float x,float z){const float gx=std::clamp(x/d.spacing.x,0.f,float(d.samplesX-1)),gz=std::clamp(z/d.spacing.y,0.f,float(d.samplesZ-1));
        const auto ix=std::min(unsigned(gx),d.samplesX-2),iz=std::min(unsigned(gz),d.samplesZ-2);const float u=gx-ix,v=gz-iz;
        const float a=d.heights[std::size_t(iz)*d.samplesX+ix],b=d.heights[std::size_t(iz)*d.samplesX+ix+1],c=d.heights[std::size_t(iz+1)*d.samplesX+ix],e=d.heights[std::size_t(iz+1)*d.samplesX+ix+1];
        return u+v<=1?a+(b-a)*u+(c-a)*v:e+(c-e)*(1-u)+(b-e)*(1-v);};
    const float left=result.cutX*d.spacing.x,right=endX*d.spacing.x,front=result.cutZ*d.spacing.y,back=endZ*d.spacing.y;
    auto borderDistance=[&](Vec2 p){return std::min({p.x-left,right-p.x,p.y-front,back-p.y});};
    auto elevation=[&](Vec2 p){const float original=ground(p.x,p.y);const auto n=nearest(p);
        const float across=smooth((std::abs(n.side)-run*.5f)/settings.shoulderWidth);
        const float ends=smooth(std::min(n.along,total-n.along)/settings.endFade);
        const float face=settings.lowerHeight+drop*std::clamp(n.side/run+.5f,0.f,1.f);
        // Boundaries and lip/toe positions remain exact; seeded relief is face-only.
        const float band=std::clamp(1-std::abs(n.side)*2/run,0.f,1.f);
        const float phase=float(settings.seed%8191)*.137f;
        const float noise=std::sin(p.x*4.7f+phase)*std::sin(p.y*3.9f-phase)*settings.irregularity*band;
        const float influence=(1-across)*ends;
        return original+(face+noise-original)*influence;};
    auto mesh=std::make_shared<Mesh>();mesh->materials={material};
    std::vector<Vec2> positions;
    auto vertex=[&](Vec2 p){spend();if(positions.size()==90000)throw std::length_error("Cliff vertex budget exceeded");
        const auto id=unsigned(positions.size());positions.push_back(p);mesh->vertices.push_back({{p.x,elevation(p),p.y},{0,1,0},{}, {}});return id;};
    for(unsigned z=result.cutZ;z<=endZ;++z)for(unsigned x=result.cutX;x<=endX;++x)vertex({x*d.spacing.x,z*d.spacing.y});
    const auto columns=result.cellsX+1;
    for(unsigned z=0;z<result.cellsZ;++z)for(unsigned x=0;x<result.cellsX;++x){const auto a=z*columns+x,b=a+1,c=a+columns,e=c+1;mesh->indices.insert(mesh->indices.end(),{a,c,b,b,c,e});}
    // Shared-edge subdivisions retain the exact unsplit source-grid perimeter.
    auto boundary=[&](unsigned a,unsigned b){const auto p=positions[a],q=positions[b];return (p.x==left&&q.x==left)||(p.x==right&&q.x==right)||(p.y==front&&q.y==front)||(p.y==back&&q.y==back);};
    auto split=[&](const std::map<Edge,unsigned>& cuts){std::vector<unsigned> out;
        auto emit=[&](unsigned a,unsigned b,unsigned c){spend();if(out.size()/3==settings.maxOutputTriangles)throw std::length_error("Cliff triangle budget exceeded");out.insert(out.end(),{a,b,c});};
        for(std::size_t i=0;i<mesh->indices.size();i+=3){unsigned v[3]={mesh->indices[i],mesh->indices[i+1],mesh->indices[i+2]},m[3]{};bool cut[3]{};unsigned n=0;
            for(unsigned j=0;j<3;++j)if(auto it=cuts.find(edge(v[j],v[(j+1)%3]));it!=cuts.end()){cut[j]=true;m[j]=it->second;++n;}
            if(!n){emit(v[0],v[1],v[2]);continue;}
            unsigned start=0;while(!cut[start]||(n==2&&!cut[(start+1)%3]))++start;
            const auto a=v[start],b=v[(start+1)%3],c=v[(start+2)%3],ab=m[start],bc=m[(start+1)%3],ca=m[(start+2)%3];
            if(n==1){emit(a,ab,c);emit(ab,b,c);}
            else if(n==2){emit(b,bc,ab);emit(a,ab,c);emit(ab,bc,c);}
            else{emit(a,ab,ca);emit(ab,b,bc);emit(ca,bc,c);emit(ab,bc,ca);}
        }mesh->indices=std::move(out);};
    // Insert lip/toe feature edges before refinement. Infinite guide lines only
    // partition interior triangles; the source collar itself is never split.
    for(const auto& s:segments)for(float offset:{-run*.5f,run*.5f}){
        std::map<Edge,unsigned> cuts;
        for(std::size_t i=0;i<mesh->indices.size();i+=3)for(unsigned j=0;j<3;++j){spend();const auto e=edge(mesh->indices[i+j],mesh->indices[i+(j+1)%3]);if(cuts.contains(e)||boundary(e.first,e.second))continue;
            const auto a=sub(positions[e.first],s.a),b=sub(positions[e.second],s.a);
            const float da=s.tangent.x*a.y-s.tangent.y*a.x-offset,db=s.tangent.x*b.y-s.tangent.y*b.x-offset;
            if((da>1e-5f&&db< -1e-5f)||(da< -1e-5f&&db>1e-5f)){const float t=da/(da-db);const auto p=positions[e.first],q=positions[e.second];cuts.emplace(e,vertex({p.x+(q.x-p.x)*t,p.y+(q.y-p.y)*t}));}}
        if(!cuts.empty())split(cuts);
    }
    bool finished=false;
    for(unsigned pass=0;pass<12;++pass){std::map<Edge,unsigned> cuts;
        for(std::size_t i=0;i<mesh->indices.size();i+=3)for(unsigned j=0;j<3;++j){spend();const auto e=edge(mesh->indices[i+j],mesh->indices[i+(j+1)%3]);if(cuts.contains(e)||boundary(e.first,e.second))continue;
            const auto delta=mesh->vertices[e.first].position-mesh->vertices[e.second].position;
            const auto p0=positions[e.first],p1=positions[e.second];const Vec2 center{(p0.x+p1.x)*.5f,(p0.y+p1.y)*.5f};
            const float collar=1-smooth(borderDistance(center)/std::max(d.spacing.x,d.spacing.y));
            const float edgeLength=std::max(settings.maxEdgeLength,std::sqrt(d.spacing.x*d.spacing.x+d.spacing.y*d.spacing.y)*collar);
            if(dot(delta,delta)>edgeLength*edgeLength*1.00001f){const auto p=positions[e.first],q=positions[e.second];cuts.emplace(e,vertex({(p.x+q.x)*.5f,(p.y+q.y)*.5f}));}}
        if(cuts.empty()){finished=true;break;}split(cuts);
    }
    require(finished,"Cliff refinement depth exceeded");
    std::vector<Vec3> normals(mesh->vertices.size());
    for(std::size_t i=0;i<mesh->indices.size();i+=3){spend();const auto a=mesh->indices[i],b=mesh->indices[i+1],c=mesh->indices[i+2];
        const auto n=cross(mesh->vertices[b].position-mesh->vertices[a].position,mesh->vertices[c].position-mesh->vertices[a].position);
        require(dot(n,n)>1e-16f&&n.y>0,"Degenerate cliff surface triangle");for(auto id:{a,b,c})normals[id]=normals[id]+n;}
    auto sourceNormal=[&](Vec2 p){const float gx=p.x/d.spacing.x,gz=p.y/d.spacing.y;const auto x=std::min(unsigned(gx),d.samplesX-2),z=std::min(unsigned(gz),d.samplesZ-2);const float u=gx-x,v=gz-z;
        auto n=[&](unsigned a,unsigned b){const auto xl=a?a-1:a,xr=std::min(a+1,d.samplesX-1),zl=b?b-1:b,zr=std::min(b+1,d.samplesZ-1);
            const Vec3 dx{(xr-xl)*d.spacing.x,d.heights[std::size_t(b)*d.samplesX+xr]-d.heights[std::size_t(b)*d.samplesX+xl],0},dz{0,d.heights[std::size_t(zr)*d.samplesX+a]-d.heights[std::size_t(zl)*d.samplesX+a],(zr-zl)*d.spacing.y};return normalized(cross(dz,dx));};
        return normalized(n(x,z)*((1-u)*(1-v))+n(x+1,z)*(u*(1-v))+n(x,z+1)*((1-u)*v)+n(x+1,z+1)*(u*v));};
    MeshLodRecipe recipe;recipe.levels=settings.lodLevels;recipe.lockedVertices.reserve(mesh->vertices.size());
    for(unsigned i=0;i<mesh->vertices.size();++i){spend();auto& v=mesh->vertices[i];const auto p=positions[i];const auto n=nearest(p);const float distance=borderDistance(p),w=1-smooth(distance/settings.blendWidth);
        const auto normal=normalized(normals[i]);v.normal=normalized(normal*(1-w)+sourceNormal(p)*w);
        // Horizontal shoulders retain grass; the steep face receives rock.
        const float grass=std::max(w,smooth((normal.y-.45f)/.45f));v.uv={grass*(255.f/256)+.5f/256,.5f};
        recipe.lockedVertices.push_back(w>0||std::abs(std::abs(n.side)-run*.5f)<settings.maxEdgeLength*.5f);
    }
    mesh->parts={{0,unsigned(mesh->indices.size()),0}};mesh->generateTangents();
    if(settings.lodLevels){mesh->lodRecipe=std::move(recipe);MeshOptimizer::regenerateLods(*mesh);}
    (void)makeMeshTerrainActor("Validate cliff",mesh);
    result.outputTriangles=unsigned(mesh->indices.size()/3);require(result.outputTriangles<=settings.maxOutputTriangles,"Cliff triangle budget exceeded");
    result.terrain=terrain.withHoles(std::move(holes));result.mesh=std::move(mesh);return result;
}
} // namespace engine
