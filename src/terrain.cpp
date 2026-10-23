#include "engine/terrain.hpp"
#include "engine/rigid_body_component.hpp"
#include <limits>
#include <map>
#include <cstring>
#include "engine/scene.hpp"

namespace engine {
namespace {
void require(bool value,const char* message) { if(!value) throw std::invalid_argument(message); }
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
Vec3 unit(Vec3 p) {
    const double length=std::sqrt(double(p.x)*p.x+double(p.y)*p.y+double(p.z)*p.z);
    return length>0 ? Vec3{float(p.x/length),float(p.y/length),float(p.z/length)} : Vec3{};
}
Vec3 point(const TerrainData& d,std::uint32_t x,std::uint32_t z,std::uint32_t offsetX=0,std::uint32_t offsetZ=0) {
    return {float(x+offsetX)*d.spacing.x,d.heights[std::size_t(z)*d.samplesX+x],float(z+offsetZ)*d.spacing.y};
}
void validateEdit(const TerrainEdit& edit) {
    require(edit.before&&edit.after,"Missing terrain edit source");
    const auto& a=edit.before->data();const auto& b=edit.after->data();
    require(a.samplesX==b.samplesX&&a.samplesZ==b.samplesZ&&a.chunkCells==b.chunkCells&&a.spacing.x==b.spacing.x&&a.spacing.y==b.spacing.y&&
        a.textureMeters==b.textureMeters&&a.lodLevels==b.lodLevels&&a.lodPixels==b.lodPixels,"Terrain edits cannot replace topology or LOD settings");
    require(a.holes==b.holes||edit.holesChanged,"Missing terrain hole change flag");
    if(a.holes!=b.holes) {
        const auto columns=(a.samplesX-2)/a.chunkCells+1;
        for(std::uint32_t z=0;z<a.samplesZ-1;++z)for(std::uint32_t x=0;x<a.samplesX-1;++x)
            if(a.hole(x,z)!=b.hole(x,z))require(std::find(edit.affectedChunks.begin(),edit.affectedChunks.end(),(z/a.chunkCells)*columns+x/a.chunkCells)!=edit.affectedChunks.end(),"Missing dirty hole chunk");
    }
    require(std::is_sorted(edit.affectedChunks.begin(),edit.affectedChunks.end())&&std::adjacent_find(edit.affectedChunks.begin(),edit.affectedChunks.end())==edit.affectedChunks.end(),"Invalid terrain dirty-chunk list");
    for(auto chunk:edit.affectedChunks) require(chunk<edit.before->chunkCount(),"Invalid terrain dirty chunk");
}
using TerrainOverrides=std::vector<std::optional<MaterialInstance>>;
TerrainOverrides paintedOverrides(const TerrainComponent& part,const TerrainAsset& asset) {
    auto result=part.materialOverrides;if(result.empty()) result.resize(1);if(!result[0]) result[0]=MaterialInstance{};
    result[0]->layers=asset.data().material->layers;return result;
}
// Compact coarse interiors retain EVERY full-resolution perimeter sample. Adjacent
// chunks therefore have identical edge segments at any pair of LOD levels.
std::shared_ptr<Mesh> coarseChunk(const TerrainData& d,std::uint32_t x0,std::uint32_t z0,std::uint32_t nx,std::uint32_t nz,std::uint32_t step,std::uint32_t offsetX=0,std::uint32_t offsetZ=0,bool holeAware=false) {
    auto mesh=std::make_shared<Mesh>();std::map<std::uint64_t,std::uint32_t> vertices;
    auto sample=[&](float x,float z) {
        x=std::clamp(x,0.f,float(d.samplesX-1));z=std::clamp(z,0.f,float(d.samplesZ-1));
        const auto ix=std::min(std::uint32_t(x),d.samplesX-2),iz=std::min(std::uint32_t(z),d.samplesZ-2);
        const float u=x-ix,v=z-iz;const auto a=point(d,ix,iz),b=point(d,ix+1,iz),c=point(d,ix,iz+1),e=point(d,ix+1,iz+1);
        return u+v<=1 ? a.y+(b.y-a.y)*u+(c.y-a.y)*v : e.y+(c.y-e.y)*(1-u)+(b.y-e.y)*(1-v);
    };
    auto vertex=[&](float x,float z) {
        const auto key=(std::uint64_t(std::uint32_t(x*2))<<32)|std::uint32_t(z*2);
        if(auto found=vertices.find(key);found!=vertices.end()) return found->second;
        const float gx=x0+x,gz=z0+z;
        const float xl=std::max(gx-1,0.f),xr=std::min(gx+1,float(d.samplesX-1)),zl=std::max(gz-1,0.f),zr=std::min(gz+1,float(d.samplesZ-1));
        const Vec3 dx{(xr+offsetX)*d.spacing.x-(xl+offsetX)*d.spacing.x,sample(xr,gz)-sample(xl,gz),0},dz{0,sample(gx,zr)-sample(gx,zl),(zr+offsetZ)*d.spacing.y-(zl+offsetZ)*d.spacing.y};
        const Vec3 p{(gx+offsetX)*d.spacing.x,sample(gx,gz),(gz+offsetZ)*d.spacing.y};const auto t=unit(dx);
        const auto index=std::uint32_t(mesh->vertices.size());vertices.emplace(key,index);
        mesh->vertices.push_back({p,unit(cross(dz,dx)),{p.x/d.textureMeters,p.z/d.textureMeters},{t.x,t.y,t.z,-1}});return index;
    };
    auto tile=[&](auto&& self,std::uint32_t x,std::uint32_t z,std::uint32_t right,std::uint32_t bottom)->void {
        if(holeAware){
            bool cut=false,solid=false;
            for(auto b=z;b<bottom;++b)for(auto a=x;a<right;++a){if(d.hole(x0+a,z0+b))cut=true;else solid=true;}
            if(!solid)return;
            if(right-x==1&&bottom-z==1){const auto a=vertex(float(x),float(z)),b=vertex(float(right),float(z)),c=vertex(float(x),float(bottom)),e=vertex(float(right),float(bottom));
                mesh->indices.insert(mesh->indices.end(),{a,c,b,b,c,e});return;}
            bool curved=false;
            if(!cut){
                const float h=point(d,x0+x,z0+z).y,dx=(point(d,x0+right,z0+z).y-h)/float(right-x),dz=(point(d,x0+x,z0+bottom).y-h)/float(bottom-z);
                for(auto b=z;b<=bottom&&!curved;++b)for(auto a=x;a<=right;++a)
                    if(std::abs(point(d,x0+a,z0+b).y-(h+dx*(a-x)+dz*(b-z)))>1e-5f){curved=true;break;}
            }
            if(cut||curved){
                // Keep source triangles on curved cut tiles. Otherwise independent
                // full-detail shadows can reveal displaced coarse receivers.
                const auto mx=x+(right-x)/2,mz=z+(bottom-z)/2;
                if(mx>x&&mz>z){self(self,x,z,mx,mz);self(self,mx,z,right,mz);self(self,x,mz,mx,bottom);self(self,mx,mz,right,bottom);}
                else if(mx>x){self(self,x,z,mx,bottom);self(self,mx,z,right,bottom);}
                else{self(self,x,z,right,mz);self(self,x,mz,right,bottom);}return;
            }
        }
        const auto center=vertex((float(x)+right)*.5f,(float(z)+bottom)*.5f);
        std::vector<std::uint32_t> edge;edge.reserve(4+step*4);
        for(auto y=z;y<bottom;y+=(holeAware||x==0) ? 1 : bottom-z) edge.push_back(vertex(float(x),float(y)));
        for(auto a=x;a<right;a+=(holeAware||bottom==nz) ? 1 : right-x) edge.push_back(vertex(float(a),float(bottom)));
        for(auto y=bottom;y>z;y-=(holeAware||right==nx) ? 1 : bottom-z) edge.push_back(vertex(float(right),float(y)));
        for(auto a=right;a>x;a-=(holeAware||z==0) ? 1 : right-x) edge.push_back(vertex(float(a),float(z)));
        for(std::size_t i=0;i<edge.size();++i) mesh->indices.insert(mesh->indices.end(),{center,edge[i],edge[(i+1)%edge.size()]});
    };
    for(std::uint32_t z=0;z<nz;z+=step)for(std::uint32_t x=0;x<nx;x+=step)
        tile(tile,x,z,std::min(x+step,nx),std::min(z+step,nz));
    mesh->materials={d.material};mesh->parts={{0,std::uint32_t(mesh->indices.size()),0}};return mesh;
}
std::shared_ptr<const Mesh> generateChunk(const TerrainData& d,std::uint32_t x0,std::uint32_t z0,std::uint32_t nx,std::uint32_t nz,std::uint32_t offsetX=0,std::uint32_t offsetZ=0) {
        auto mesh=std::make_shared<Mesh>();mesh->vertices.reserve(std::size_t(nx+1)*(nz+1));mesh->indices.reserve(std::size_t(nx)*nz*6);
        for(std::uint32_t z=z0;z<=z0+nz;++z) for(std::uint32_t x=x0;x<=x0+nx;++x) {
            const auto xl=x ? x-1 : x,xr=std::min(x+1,d.samplesX-1),zl=z ? z-1 : z,zr=std::min(z+1,d.samplesZ-1);
            const auto dx=point(d,xr,z,offsetX,offsetZ)-point(d,xl,z,offsetX,offsetZ),dz=point(d,x,zr,offsetX,offsetZ)-point(d,x,zl,offsetX,offsetZ);
            const auto p=point(d,x,z,offsetX,offsetZ);const auto tangent=normalized(dx);
            mesh->vertices.push_back({p,unit(cross(dz,dx)),{p.x/d.textureMeters,p.z/d.textureMeters},{tangent.x,tangent.y,tangent.z,-1}});
        }
        bool hasHoles=false;
        for(std::uint32_t z=0;z<nz;++z) for(std::uint32_t x=0;x<nx;++x) {
            if(d.hole(x0+x,z0+z)) {hasHoles=true;continue;}
            const auto a=z*(nx+1)+x,b=a+1,c=a+nx+1,e=c+1;
            mesh->indices.insert(mesh->indices.end(),{a,c,b,b,c,e});
        }
        mesh->materials={d.material};mesh->parts={{0,std::uint32_t(mesh->indices.size()),0}};
        auto previous=mesh->indices.size();
        // Cut chunks partition mixed tiles and retain all solid-tile perimeter
        // samples, including hole boundaries and interfaces to finer neighbours.
        for(std::uint32_t level=0;level<d.lodLevels;++level) {
            auto coarse=coarseChunk(d,x0,z0,nx,nz,2u<<level,offsetX,offsetZ,hasHoles);
            if(coarse->indices.size()<previous) {previous=coarse->indices.size();mesh->lods.push_back({std::move(coarse),d.lodPixels[level]});}
        }
    return mesh;
}
bool interval(Vec3 lo,Vec3 hi,Vec3 o,Vec3 v,float& start,float& end) {
    const float a[]={lo.x,lo.y,lo.z},b[]={hi.x,hi.y,hi.z},c[]={o.x,o.y,o.z},e[]={v.x,v.y,v.z};
    for(int i=0;i<3;++i) {
        if(e[i]==0) { if(c[i]<a[i]||c[i]>b[i]) return false; }
        else { float t0=(a[i]-c[i])/e[i],t1=(b[i]-c[i])/e[i];if(t0>t1) std::swap(t0,t1);start=std::max(start,t0);end=std::min(end,t1);if(start>end) return false; }
    }
    return true;
}
std::optional<float> triangle(Vec3 o,Vec3 v,Vec3 a,Vec3 b,Vec3 c,float maximum) {
    const auto e=b-a,f=c-a,p=cross(v,f);const double det=dot(e,p);
    if(std::abs(det)<1e-12) return {};
    const double inv=1/det;const auto t=o-a;const double u=dot(t,p)*inv;
    if(u < -1e-6 || u>1+1e-6) return {};
    const auto q=cross(t,e);const double w=dot(v,q)*inv;
    if(w < -1e-6 || u+w>1+1e-6) return {};
    const double distance=dot(f,q)*inv;
    if(distance < -1e-6 || distance>maximum) return {};
    return float(std::max(0.0,distance));
}
template<class F> void terrainVisit(const Node& root,F&& function) {
    root.visit([&](const Node& n,const Mat4& world) {
        if(const auto* t=dynamic_cast<const TerrainComponent*>(&n);t&&t->isActiveInHierarchy()) {t->validate();function(*t,world);}
    },root.parent() ? root.parent()->worldMatrix() : Mat4::identity());
}
}
void TerrainData::validate() const {
    require(samplesX>=2&&samplesZ>=2&&samplesX<=4097&&samplesZ<=4097&&std::uint64_t(samplesX)*samplesZ<=1048576,"Terrain sample budget exceeded or invalid dimensions");
    require(heights.size()==std::size_t(samplesX)*samplesZ,"Terrain height count mismatch");
    require(holes.empty()||holes.size()==std::size_t(samplesX-1)*(samplesZ-1),"Terrain hole count mismatch");
    for(auto value:holes) require(value<=1,"Terrain holes must be 0 or 1");
    require(chunkCells>=1&&chunkCells<=128,"Terrain chunk cells must be 1..128");
    require(std::uint64_t((samplesX-2)/chunkCells+1)*((samplesZ-2)/chunkCells+1)<=4096,"Terrain chunk budget exceeded");
    require(std::isfinite(spacing.x)&&std::isfinite(spacing.y)&&spacing.x>=.0001f&&spacing.y>=.0001f&&
        double(spacing.x)*(samplesX-1)<=1000000&&double(spacing.y)*(samplesZ-1)<=1000000,"Invalid terrain spacing or extent");
    require(std::isfinite(textureMeters)&&textureMeters>=.0001f&&textureMeters<=1000000,"Invalid terrain texture scale");
    require(lodLevels<=3,"Terrain supports zero through three LOD levels");
    float previous=std::numeric_limits<float>::infinity();
    for(float pixels:lodPixels) {require(std::isfinite(pixels)&&pixels>0&&pixels<previous&&pixels<=1000000,"Invalid terrain LOD thresholds");previous=pixels;}
    for(float h:heights) require(std::isfinite(h)&&std::abs(h)<=1000000,"Invalid terrain height");
    if(physicalSurface)physicalSurface->validate();require(cellSurfaces.empty()||cellSurfaces.size()==std::size_t(samplesX-1)*(samplesZ-1),"Invalid terrain physical cell map");for(const auto& surface:cellSurfaces)if(surface)surface->validate();
    require(material!=nullptr,"Missing terrain material");material->validate();
}
std::size_t TerrainData::derivedBytes() const {
    validate();
    const std::size_t nx=(samplesX-2)/chunkCells+1,nz=(samplesZ-2)/chunkCells+1;
    const auto base=(std::size_t(samplesX-1)+nx)*(std::size_t(samplesZ-1)+nz)*sizeof(Vertex)+
        std::size_t(samplesX-1)*(samplesZ-1)*6*sizeof(std::uint32_t)+nx*nz*(sizeof(Mesh)+sizeof(MeshPart)+64);
    return base*(lodLevels+1)*2; // Includes conservative coarse generation/transport capacity.
}
std::shared_ptr<const TerrainAsset> TerrainAsset::create(TerrainData data,bool buildChunks) {
    data.validate();return std::shared_ptr<const TerrainAsset>(new TerrainAsset(std::move(data),buildChunks));
}
std::shared_ptr<const TerrainAsset> TerrainAsset::createPatch(TerrainData data,TerrainPatch patch,bool build,std::shared_ptr<const Mesh> prepared) {
    data.validate();
    require(patch.cellsX&&patch.cellsZ&&patch.cellsX<=data.chunkCells&&patch.cellsZ<=data.chunkCells&&
        std::uint64_t(patch.x)+patch.cellsX<data.samplesX&&std::uint64_t(patch.z)+patch.cellsZ<data.samplesZ&&
        std::uint64_t(patch.offsetX)+data.samplesX<=65537&&std::uint64_t(patch.offsetZ)+data.samplesZ<=65537&&
        double(patch.offsetX+data.samplesX-1)*data.spacing.x<=1000000&&double(patch.offsetZ+data.samplesZ-1)*data.spacing.y<=1000000,"Invalid terrain source patch");
    if(prepared) {
        std::size_t solidCells=0;
        for(std::uint32_t z=patch.z;z<patch.z+patch.cellsZ;++z) for(std::uint32_t x=patch.x;x<patch.x+patch.cellsX;++x) if(!data.hole(x,z)) ++solidCells;
        require(build&&prepared->vertices.size()==std::size_t(patch.cellsX+1)*(patch.cellsZ+1)&&prepared->indices.size()==solidCells*6,"Invalid prepared terrain chunk");
        auto structure=[&](const Mesh& mesh) {
            require(mesh.materials.size()==1&&mesh.materials[0]==data.material&&mesh.parts.size()==1&&mesh.parts[0].firstIndex==0&&mesh.parts[0].indexCount==mesh.indices.size()&&mesh.parts[0].materialIndex==0&&mesh.skin.empty()&&mesh.morphTargets.empty()&&!mesh.skeleton,"Invalid prepared terrain mesh metadata");
            for(const auto& v:mesh.vertices) {require(finite(v.position)&&finite(v.normal)&&std::isfinite(v.uv.x)&&std::isfinite(v.uv.y),"Nonfinite prepared terrain vertex");for(float f:v.tangent) require(std::isfinite(f),"Invalid prepared terrain tangent");}
        };
        structure(*prepared);
        for(std::uint32_t z=0;z<=patch.cellsZ;++z) for(std::uint32_t x=0;x<=patch.cellsX;++x) {
            const auto& v=prepared->vertices[std::size_t(z)*(patch.cellsX+1)+x];const auto p=point(data,patch.x+x,patch.z+z,patch.offsetX,patch.offsetZ);
            require(v.position.x==p.x&&v.position.y==p.y&&v.position.z==p.z&&finite(v.normal),"Prepared terrain geometry disagrees with source");
        }
        std::size_t at=0;
        for(std::uint32_t z=0;z<patch.cellsZ;++z) for(std::uint32_t x=0;x<patch.cellsX;++x) {
            if(data.hole(patch.x+x,patch.z+z)) continue;
            const auto a=z*(patch.cellsX+1)+x,b=a+1,c=a+patch.cellsX+1,e=c+1;
            for(auto index:{a,c,b,b,c,e}) require(prepared->indices[at++]==index,"Invalid prepared terrain topology");
        }
        require(prepared->lods.size()<=data.lodLevels,"Invalid prepared terrain LOD count");
        float previous=std::numeric_limits<float>::infinity();
        for(const auto& lod:prepared->lods) {
            require(lod.mesh&&lod.mesh->lods.empty()&&lod.maxPixels>0&&lod.maxPixels<previous&&lod.mesh->indices.size()<prepared->indices.size(),"Invalid prepared terrain LOD");previous=lod.maxPixels;
            require(lod.mesh->vertices.size()<=prepared->vertices.size()*4&&lod.mesh->indices.size()%3==0,"Invalid prepared LOD payload");
            structure(*lod.mesh);
            for(auto index:lod.mesh->indices) require(index<lod.mesh->vertices.size(),"Invalid prepared LOD index");
            for(const auto& v:lod.mesh->vertices) require(finite(v.position)&&finite(v.normal)&&v.position.x>=float(patch.offsetX+patch.x)*data.spacing.x&&
                v.position.x<=float(patch.offsetX+patch.x+patch.cellsX)*data.spacing.x&&v.position.z>=float(patch.offsetZ+patch.z)*data.spacing.y&&
                v.position.z<=float(patch.offsetZ+patch.z+patch.cellsZ)*data.spacing.y,"Invalid prepared LOD bounds");
        }
        if(solidCells!=std::size_t(patch.cellsX)*patch.cellsZ&&!prepared->lods.empty()){
            // A prepared cut patch must carry canonical hole-aware topology. Bounds
            // checks alone cannot reject a triangle that bridges a missing cell.
            auto expected=generateChunk(data,patch.x,patch.z,patch.cellsX,patch.cellsZ,patch.offsetX,patch.offsetZ);
            for(const auto& lod:prepared->lods){
                const auto match=std::find_if(expected->lods.begin(),expected->lods.end(),[&](const MeshLod& candidate){return candidate.maxPixels==lod.maxPixels;});
                require(match!=expected->lods.end()&&match->mesh->indices==lod.mesh->indices&&match->mesh->vertices.size()==lod.mesh->vertices.size(),"Prepared LOD disagrees with hole topology");
                for(std::size_t i=0;i<lod.mesh->vertices.size();++i)require(std::memcmp(&match->mesh->vertices[i],&lod.mesh->vertices[i],sizeof(Vertex))==0,"Prepared hole LOD vertex mismatch");
            }
        }
    }
    auto result=std::shared_ptr<TerrainAsset>(new TerrainAsset(std::move(data),false));result->patch_=patch;
    if(build) result->chunks_.push_back(prepared ? std::move(prepared) : result->buildChunk(0));
    return result;
}
std::size_t TerrainData::chunkDerivedBytes() const {
    return ((std::size_t(chunkCells)+1)*(chunkCells+1)*sizeof(Vertex)+std::size_t(chunkCells)*chunkCells*6*sizeof(std::uint32_t)+sizeof(Mesh)+sizeof(MeshPart)+64)*(lodLevels+1)*2;
}
TerrainAsset::TerrainAsset(TerrainData data,bool generate):data_(std::move(data)) {
    if(generate) for(std::uint32_t i=0;i<chunkCount();++i) chunks_.push_back(buildChunk(i));
}
std::uint32_t TerrainAsset::chunkCount() const {
    return patch_ ? 1 : ((data_.samplesX-2)/data_.chunkCells+1)*((data_.samplesZ-2)/data_.chunkCells+1);
}
std::shared_ptr<const Mesh> TerrainAsset::buildChunk(std::uint32_t chunk) const {
    require(chunk<chunkCount(),"Invalid terrain chunk");
    if(chunk<chunks_.size()&&chunks_[chunk]) return chunks_[chunk];
    const auto& d=data_;const auto columns=(d.samplesX-2)/d.chunkCells+1;
    const auto x0=(chunk%columns)*d.chunkCells,z0=(chunk/columns)*d.chunkCells;
    if(patch_) return generateChunk(d,patch_->x,patch_->z,patch_->cellsX,patch_->cellsZ,patch_->offsetX,patch_->offsetZ);
    return generateChunk(d,x0,z0,std::min(d.chunkCells,d.samplesX-1-x0),std::min(d.chunkCells,d.samplesZ-1-z0));
}

std::shared_ptr<const TerrainAsset> TerrainAsset::fromHeightmap(const Texture& image,float minimum,float maximum,Vec2 spacing,
    std::uint32_t cells,float textureMeters,std::shared_ptr<const Material> material) {
    require(std::isfinite(minimum)&&std::isfinite(maximum)&&minimum<=maximum&&minimum>=-1000000&&maximum<=1000000,"Invalid terrain height range");
    require(image.width>=2&&image.height>=2&&std::uint64_t(image.width)*image.height<=1048576&&
        image.pixels.size()==std::uint64_t(image.width)*image.height*4,"Invalid heightmap dimensions or pixels");
    TerrainData d;d.samplesX=image.width;d.samplesZ=image.height;d.spacing=spacing;d.chunkCells=cells;d.textureMeters=textureMeters;d.material=std::move(material);
    d.heights.reserve(std::size_t(image.width)*image.height);
    for(std::size_t i=0;i<image.pixels.size();i+=4) d.heights.push_back(minimum+(maximum-minimum)*(float(image.pixels[i])/255));
    return create(std::move(d));
}
std::optional<TerrainGround> TerrainAsset::ground(float x,float z) const {
    require(std::isfinite(x)&&std::isfinite(z),"Invalid terrain ground coordinate");const auto& d=data_;
    const float inputX=x,inputZ=z;
    if(patch_) {
        x-=patch_->offsetX*d.spacing.x;z-=patch_->offsetZ*d.spacing.y;
        if(x<patch_->x*d.spacing.x||z<patch_->z*d.spacing.y||x>(patch_->x+patch_->cellsX)*d.spacing.x||z>(patch_->z+patch_->cellsZ)*d.spacing.y) return {};
    }
    if(x<0||z<0||x>float(d.samplesX-1)*d.spacing.x||z>float(d.samplesZ-1)*d.spacing.y) return {};
    const float gx=std::min(x/d.spacing.x,float(d.samplesX-1)),gz=std::min(z/d.spacing.y,float(d.samplesZ-1));
    const auto ix=std::min(std::uint32_t(gx),d.samplesX-2),iz=std::min(std::uint32_t(gz),d.samplesZ-2);
    if(d.hole(ix,iz)) return {};
    const float u=gx-ix,v=gz-iz;const auto a=point(d,ix,iz),b=point(d,ix+1,iz),c=point(d,ix,iz+1),e=point(d,ix+1,iz+1);
    if(u+v<=1) return TerrainGround{{inputX,a.y+(b.y-a.y)*u+(c.y-a.y)*v,inputZ},unit(cross(c-a,b-a))};
    return TerrainGround{{inputX,e.y+(c.y-e.y)*(1-u)+(b.y-e.y)*(1-v),inputZ},unit(cross(c-b,e-b))};
}
std::shared_ptr<const CollisionShape> TerrainAsset::collisionShape() const {
    if(patch_) return chunkCollision(0);
    const auto& d=data_;std::vector<Vec3> vertices;std::vector<std::uint32_t> indices;
    vertices.reserve(d.heights.size());indices.reserve(std::size_t(d.samplesX-1)*(d.samplesZ-1)*6);
    for(std::uint32_t z=0;z<d.samplesZ;++z) for(std::uint32_t x=0;x<d.samplesX;++x) vertices.push_back(point(d,x,z));
    for(std::uint32_t z=0;z<d.samplesZ-1;++z) for(std::uint32_t x=0;x<d.samplesX-1;++x) {
        if(d.hole(x,z)) continue;
        const auto a=z*d.samplesX+x,b=a+1,c=a+d.samplesX,e=c+1;indices.insert(indices.end(),{a,c,b,b,c,e});
    }
    if(indices.empty()) return {};
    auto shape=CollisionShape::triangleMesh(vertices,indices);std::vector<std::shared_ptr<const PhysicalSurface>> triangles;
    if(!d.cellSurfaces.empty())for(std::uint32_t z=0;z<d.samplesZ-1;++z)for(std::uint32_t x=0;x<d.samplesX-1;++x)if(!d.hole(x,z)){const auto& surface=d.cellSurfaces[std::size_t(z)*(d.samplesX-1)+x];triangles.push_back(surface);triangles.push_back(surface);}
    return shape->withSurfaces(d.physicalSurface,std::move(triangles));
}
std::shared_ptr<const CollisionShape> TerrainAsset::chunkCollision(std::uint32_t chunk) const {
    auto mesh=buildChunk(chunk);if(mesh->indices.empty())return {};std::vector<Vec3> points;points.reserve(mesh->vertices.size());
    for(const auto& vertex:mesh->vertices) points.push_back(vertex.position);
    const auto& d=data_;const auto columns=(d.samplesX-2)/d.chunkCells+1;
    const auto x0=patch_?patch_->x:(chunk%columns)*d.chunkCells,z0=patch_?patch_->z:(chunk/columns)*d.chunkCells;
    const auto nx=patch_?patch_->cellsX:std::min(d.chunkCells,d.samplesX-1-x0),nz=patch_?patch_->cellsZ:std::min(d.chunkCells,d.samplesZ-1-z0);
    std::vector<std::shared_ptr<const PhysicalSurface>> triangles;if(!d.cellSurfaces.empty())for(std::uint32_t z=z0;z<z0+nz;++z)for(std::uint32_t x=x0;x<x0+nx;++x)if(!d.hole(x,z)){auto surface=d.cellSurfaces[std::size_t(z)*(d.samplesX-1)+x];triangles.push_back(surface);triangles.push_back(surface);}
    return CollisionShape::triangleMesh(points,mesh->indices)->withSurfaces(d.physicalSurface,std::move(triangles));
}
void TerrainBrush::validate() const {
    require(mode>=TerrainBrushMode::Raise&&mode<=TerrainBrushMode::Fill,"Invalid terrain brush mode");
    require(std::isfinite(center.x)&&std::isfinite(center.y)&&std::isfinite(radius)&&radius>=.0001f&&radius<=1000000,"Invalid terrain brush position or radius");
    require(std::isfinite(strength)&&std::abs(strength)<=1000000&&std::isfinite(targetHeight)&&std::abs(targetHeight)<=1000000,"Invalid terrain brush strength or target");
    if(mode!=TerrainBrushMode::Raise) require(strength>=0&&strength<=1,"Blend brush strength must be 0..1");
    require(layer<4,"Terrain paint layer must be 0..3");
}
TerrainEdit TerrainAsset::withPhysicalSurfaces(std::shared_ptr<const PhysicalSurface> surface,std::vector<std::shared_ptr<const PhysicalSurface>> cells) const {
    auto data=data_;data.physicalSurface=std::move(surface);data.cellSurfaces=std::move(cells);data.validate();
    TerrainEdit edit;edit.before=edit.after=shared_from_this();if(data.physicalSurface==data_.physicalSurface&&data.cellSurfaces==data_.cellSurfaces)return edit;
    auto next=std::shared_ptr<TerrainAsset>(new TerrainAsset(std::move(data),false));next->chunks_=chunks_;next->patch_=patch_;edit.after=std::move(next);edit.surfacesChanged=true;
    for(std::uint32_t i=0;i<chunkCount();++i)edit.affectedChunks.push_back(i);return edit;
}
TerrainEdit TerrainAsset::withLayers(const MaterialLayers& layers) const {
    auto material=std::make_shared<Material>(*data_.material);material->layers=layers;material->validate();
    const auto& previous=data_.material->layers;
    bool same=materialLayerParameters(previous)==materialLayerParameters(layers)&&previous.weights==layers.weights;
    for(std::size_t i=0;i<4;++i)same&=previous.layers[i].texture==layers.layers[i].texture&&previous.layers[i].normalHeightTexture==layers.layers[i].normalHeightTexture;
    TerrainEdit result;result.before=result.after=shared_from_this();if(same)return result;
    auto data=data_;data.material=std::move(material);
    auto next=std::shared_ptr<TerrainAsset>(new TerrainAsset(std::move(data),false));
    next->chunks_=chunks_;next->patch_=patch_;
    result.after=std::move(next);result.paintChanged=true;return result;
}
TerrainEdit TerrainAsset::withHoles(std::vector<std::uint8_t> holes) const {
    require(!patch_,"Paged terrain patches cannot edit holes");
    auto data=data_;data.holes=std::move(holes);data.validate();
    TerrainEdit result;result.before=result.after=shared_from_this();
    const auto columns=(data.samplesX-2)/data.chunkCells+1;
    std::vector<bool> dirty(chunkCount());
    for(std::uint32_t z=0;z<data.samplesZ-1;++z)for(std::uint32_t x=0;x<data.samplesX-1;++x)
        if(data.hole(x,z)!=data_.hole(x,z))dirty[(z/data.chunkCells)*columns+x/data.chunkCells]=true;
    for(std::uint32_t i=0;i<dirty.size();++i)if(dirty[i])result.affectedChunks.push_back(i);
    if(result.affectedChunks.empty())return result;
    auto next=std::shared_ptr<TerrainAsset>(new TerrainAsset(std::move(data),false));
    if(eager()){next->chunks_=chunks_;for(auto i:result.affectedChunks){next->chunks_[i].reset();next->chunks_[i]=next->buildChunk(i);}}
    result.after=std::move(next);result.holesChanged=true;return result;
}
TerrainEdit TerrainAsset::edit(const TerrainBrush& brush) const {
    require(!patch_,"Paged terrain patches are immutable provider data; replace the page revision to edit");
    brush.validate();TerrainEdit result;result.before=result.after=shared_from_this();
    if(brush.strength==0) return result;
    if(double(brush.center.x)+brush.radius<0||double(brush.center.y)+brush.radius<0||
       double(brush.center.x)-brush.radius>double(data_.samplesX-1)*data_.spacing.x||double(brush.center.y)-brush.radius>double(data_.samplesZ-1)*data_.spacing.y) return result;
    if(brush.mode==TerrainBrushMode::Hole||brush.mode==TerrainBrushMode::Fill){
        auto mask=data_.holes;if(mask.empty())mask.resize(std::size_t(data_.samplesX-1)*(data_.samplesZ-1));
        for(std::uint32_t z=0;z<data_.samplesZ-1;++z)for(std::uint32_t x=0;x<data_.samplesX-1;++x){
            const double dx=(x+.5)*data_.spacing.x-brush.center.x,dz=(z+.5)*data_.spacing.y-brush.center.y;
            if(dx*dx+dz*dz<=double(brush.radius)*brush.radius)mask[std::size_t(z)*(data_.samplesX-1)+x]=brush.mode==TerrainBrushMode::Hole;
        }
        return withHoles(std::move(mask));
    }
    auto d=data_;float minX=std::numeric_limits<float>::infinity(),minZ=minX,maxX=-minX,maxZ=-minX;
    auto falloff=[&](float x,float z) {const double dx=double(x)-brush.center.x,dz=double(z)-brush.center.y;
        const float t=float(std::max(0.0,1-std::sqrt(dx*dx+dz*dz)/brush.radius));return t*t*(3-2*t);};
    auto changed=[&](float x,float z) {minX=std::min(minX,x);maxX=std::max(maxX,x);minZ=std::min(minZ,z);maxZ=std::max(maxZ,z);};
    if(brush.mode==TerrainBrushMode::Paint) {
        require(d.material->layers.enabled,"Enable layered terrain before painting");
        auto material=std::make_shared<Material>(*d.material);auto& layers=material->layers;
        require(material->uvScale.x>0&&material->uvScale.y>0,"Terrain painting requires positive material UV scale");
        auto weights=layers.weights ? std::make_shared<Texture>(*layers.weights) : std::make_shared<Texture>();
        if(!layers.weights) {
            weights->width=std::min(d.samplesX,4096u);weights->height=std::min(d.samplesZ,4096u);
            weights->pixels.resize(std::size_t(weights->width)*weights->height*4);
            for(std::size_t i=0;i<weights->pixels.size();i+=4) weights->pixels[i]=255;
            layers.weightScale={d.textureMeters/((d.samplesX-1)*d.spacing.x*material->uvScale.x),d.textureMeters/((d.samplesZ-1)*d.spacing.y*material->uvScale.y)};
            layers.weightOffset={-material->uvOffset.x*layers.weightScale.x,-material->uvOffset.y*layers.weightScale.y};
        }
        require(layers.weightScale.x>0&&layers.weightScale.y>0,"Terrain painting requires positive weight-map scale");
        weights->name="Terrain painted weights";weights->wrapU=weights->wrapV=TextureWrap::Clamp;
        for(std::uint32_t z=0;z<weights->height;++z) for(std::uint32_t x=0;x<weights->width;++x) {
            const float u=weights->width>1 ? float(x)/(weights->width-1) : .5f,v=weights->height>1 ? float(z)/(weights->height-1) : .5f;
            const float px=((u-layers.weightOffset.x)/layers.weightScale.x-material->uvOffset.x)/material->uvScale.x*d.textureMeters;
            const float pz=((v-layers.weightOffset.y)/layers.weightScale.y-material->uvOffset.y)/material->uvScale.y*d.textureMeters;
            const float alpha=falloff(px,pz)*brush.strength;if(alpha==0) continue;
            const auto index=(std::size_t(z)*weights->width+x)*4;float total=0;for(int k=0;k<4;++k) total+=weights->pixels[index+k];
            std::array<int,4> values{};int sum=0;
            for(std::uint32_t k=0;k<4;++k) {const float old=total>0 ? weights->pixels[index+k]/total : (k==0 ? 1.f : 0.f);
                values[k]=int(std::round((old*(1-alpha)+(k==brush.layer ? alpha : 0))*255));sum+=values[k];}
            // Repair quantization in the largest channel; all channels stay nonnegative.
            auto largest=std::max_element(values.begin(),values.end());*largest+=255-sum;
            bool difference=false;for(int k=0;k<4;++k) {difference|=weights->pixels[index+k]!=values[k];weights->pixels[index+k]=std::uint8_t(values[k]);}
            if(difference) changed(px,pz);
        }
        if(minX>maxX) return result;
        layers.weights=weights;material->validate();d.material=material;result.paintChanged=true;
    } else {
        for(std::uint32_t z=0;z<d.samplesZ;++z) for(std::uint32_t x=0;x<d.samplesX;++x) {
            const float px=x*d.spacing.x,pz=z*d.spacing.y,alpha=falloff(px,pz);if(alpha==0) continue;
            const auto index=std::size_t(z)*d.samplesX+x;const float old=data_.heights[index];float value=old;
            if(brush.mode==TerrainBrushMode::Raise) value=old+brush.strength*alpha;
            else {
                float target=brush.targetHeight;
                if(brush.mode==TerrainBrushMode::Smooth) {
                    double sum=0;int count=0;
                    for(int dz=-1;dz<=1;++dz) for(int dx=-1;dx<=1;++dx) {
                        const int sx=int(x)+dx,sz=int(z)+dz;if(sx>=0&&sz>=0&&sx<int(d.samplesX)&&sz<int(d.samplesZ)) {sum+=data_.heights[std::size_t(sz)*d.samplesX+sx];++count;}
                    }
                    target=float(sum/count);
                }
                value=old+(target-old)*brush.strength*alpha;
            }
            require(std::isfinite(value)&&std::abs(value)<=1000000,"Terrain brush exceeds height range");
            if(value!=old) {d.heights[index]=value;changed(px,pz);}
        }
        if(minX>maxX) return result;
        result.heightsChanged=true;
    }
    // Two source samples conservatively cover coarse interpolation and normal stencils.
    const float haloX=result.heightsChanged ? d.spacing.x*2 : 0,haloZ=result.heightsChanged ? d.spacing.y*2 : 0;
    const auto columns=(d.samplesX-2)/d.chunkCells+1;
    for(std::uint32_t i=0;i<chunkCount();++i) {
        const auto x0=(i%columns)*d.chunkCells,z0=(i/columns)*d.chunkCells;
        if(float(x0)*d.spacing.x<=maxX+haloX&&float(std::min(x0+d.chunkCells,d.samplesX-1))*d.spacing.x>=minX-haloX&&
           float(z0)*d.spacing.y<=maxZ+haloZ&&float(std::min(z0+d.chunkCells,d.samplesZ-1))*d.spacing.y>=minZ-haloZ) result.affectedChunks.push_back(i);
    }
    d.validate();auto next=std::shared_ptr<TerrainAsset>(new TerrainAsset(std::move(d),false));
    if(eager()) {
        next->chunks_=chunks_;
        if(result.heightsChanged) for(auto i:result.affectedChunks) {next->chunks_[i].reset();next->chunks_[i]=next->buildChunk(i);}
    }
    result.after=std::move(next);return result;
}
void TerrainComponent::publish(std::shared_ptr<const TerrainAsset> asset,std::shared_ptr<const Mesh> prepared,bool paint) {
    // Callers prepare slot zero before publication. No allocation in this commit.
    if(paint&&asset->data().material->layers.enabled) {
        if(!materialOverrides[0]) materialOverrides[0]=MaterialInstance{};
        materialOverrides[0]->layers=asset->data().material->layers;
    }
    asset_=std::move(asset);mesh=sourceMesh_=std::move(prepared);
}
TerrainComponent::TerrainComponent(std::string name,std::shared_ptr<const TerrainAsset> asset,std::uint32_t chunk)
    :MeshComponent(std::move(name),nullptr),asset_(std::move(asset)),chunk_(chunk) {
    require(asset_&&chunk_<asset_->chunkCount(),"Invalid terrain chunk");materialOverrides.resize(1);publish(asset_,asset_->buildChunk(chunk_),true);color={1,1,1};
}
void TerrainComponent::validate() const {
    Component::validate();require(asset_&&chunk_<asset_->chunkCount()&&mesh==sourceMesh_,"Terrain geometry cannot be replaced independently of its asset");
}
std::unique_ptr<Actor> makeTerrainActor(std::string name,std::shared_ptr<const TerrainAsset> asset,bool collision) {
    require(asset!=nullptr,"Missing terrain asset");auto actor=std::make_unique<Actor>(std::move(name));
    for(std::uint32_t i=0;i<asset->chunkCount();++i) actor->addComponent<TerrainComponent>("Terrain chunk "+std::to_string(i),asset,i);
    if(collision) {
        auto shape=asset->collisionShape();
        auto& collider=actor->addComponent<ColliderComponent>("Terrain collision",shape?shape:CollisionShape::box({.01f,.01f,.01f}));
        collider.setEnabled(bool(shape));
        RigidBodySettings settings;settings.motion=BodyMotion::Static;settings.friction=.8f;
        actor->addComponent<RigidBodyComponent>("Terrain body",settings);
    }
    return actor;
}
std::unique_ptr<Actor> makeMeshTerrainActor(std::string name,std::shared_ptr<const Mesh> mesh,bool collision,
    std::shared_ptr<const PhysicalSurface> surface) {
    require(bool(mesh),"Missing mesh terrain source");
    if(surface)surface->validate();
    auto validate=[](const Mesh& m) {
        require(!m.skeleton&&m.skin.empty()&&m.morphTargets.empty(),"Mesh terrain requires static geometry");
        require(m.vertices.size()>=3&&m.vertices.size()<=100000&&!m.indices.empty()&&m.indices.size()<=300000,"Mesh terrain section budget exceeded");
        std::vector<Vec3> positions;positions.reserve(m.vertices.size());
        for(const auto& v:m.vertices) {
            require(finite(v.position)&&finite(v.normal)&&std::isfinite(v.uv.x)&&std::isfinite(v.uv.y),"Invalid mesh terrain vertex");
            for(float f:v.tangent)require(std::isfinite(f),"Invalid mesh terrain tangent");
            positions.push_back(v.position);
        }
        for(const auto& material:m.materials){require(bool(material),"Missing mesh terrain material");material->validate();}
        std::uint64_t end=0;
        for(const auto& part:m.parts){require(part.firstIndex==end&&part.indexCount%3==0&&
            std::uint64_t(part.firstIndex)+part.indexCount<=m.indices.size()&&part.materialIndex<m.materials.size(),"Invalid mesh terrain part");end=std::uint64_t(part.firstIndex)+part.indexCount;}
        require(m.parts.empty()||end==m.indices.size(),"Incomplete mesh terrain material ranges");
        // Reuse the portable triangle contract, including finite/nondegenerate data
        // and the existing 100k vertex/100k triangle per-section budget.
        return CollisionShape::triangleMesh(positions,m.indices);
    };
    auto shape=validate(*mesh);require(mesh->lods.size()<=16,"Too many mesh terrain LODs");
    float previous=std::numeric_limits<float>::infinity();
    for(const auto& lod:mesh->lods) {
        require(lod.mesh&&lod.mesh->lods.empty()&&std::isfinite(lod.maxPixels)&&lod.maxPixels>0&&lod.maxPixels<previous,"Invalid mesh terrain LOD");
        previous=lod.maxPixels;validate(*lod.mesh);
        require(lod.mesh->parts.size()==mesh->parts.size()&&lod.mesh->materials==mesh->materials,"Mesh terrain LOD material slots differ");
        for(std::size_t i=0;i<mesh->parts.size();++i)require(mesh->parts[i].materialIndex==lod.mesh->parts[i].materialIndex,"Mesh terrain LOD part slots differ");
    }
    auto actor=std::make_unique<Actor>(std::move(name));
    actor->addComponent<MeshComponent>("Mesh terrain surface",std::move(mesh)).color={1,1,1};
    if(collision) {
        actor->addComponent<ColliderComponent>("Mesh terrain collision",shape->withSurfaces(std::move(surface)));
        RigidBodySettings body;body.motion=BodyMotion::Static;body.friction=.8f;
        actor->addComponent<RigidBodyComponent>("Mesh terrain body",body);
    }
    return actor;
}
void applyTerrainEdit(Actor& actor,const TerrainEdit& edit) {
    actor.requireIdle();
    validateEdit(edit);if(!edit.changed()) return;
    auto chunks=actor.getComponents<TerrainComponent>();require(chunks.size()==edit.before->chunkCount(),"Apply terrain edits to a complete terrain instance");
    std::vector<std::shared_ptr<const Mesh>> meshes;meshes.reserve(chunks.size());
    std::vector<bool> seen(chunks.size());std::vector<TerrainOverrides> overrides;overrides.reserve(chunks.size());
    for(auto* chunk:chunks) {chunk->validate();require(chunk->asset()==edit.before,"Stale terrain edit");require(!seen[chunk->chunk()],"Duplicate terrain chunk");seen[chunk->chunk()]=true;
        const bool dirty=(edit.heightsChanged||edit.holesChanged)&&std::find(edit.affectedChunks.begin(),edit.affectedChunks.end(),chunk->chunk())!=edit.affectedChunks.end();
        meshes.push_back(dirty ? edit.after->buildChunk(chunk->chunk()) : chunk->mesh);
        if(edit.paintChanged) overrides.push_back(paintedOverrides(*chunk,*edit.after));
    }
    auto* collider=actor.getComponent<ColliderComponent>();const bool collisionDirty=collider&&(edit.heightsChanged||edit.surfacesChanged||edit.holesChanged);
    auto shape=collisionDirty ? edit.after->collisionShape() : nullptr;
    const bool wasEmpty=collider&&edit.holesChanged&&!edit.before->collisionShape();
    for(std::size_t i=0;i<chunks.size();++i) {if(edit.paintChanged) chunks[i]->materialOverrides.swap(overrides[i]);chunks[i]->publish(edit.after,std::move(meshes[i]),false);}
    if(collisionDirty){if(shape){collider->setShape(std::move(shape));if(wasEmpty)collider->setEnabled(true);}else collider->setEnabled(false);}
}
void TerrainStreamSettings::validate() const {
    require(radiusChunks<=32&&maxResident>=1&&maxResident<=256&&buildsPerUpdate>=1&&buildsPerUpdate<=64,"Invalid terrain streaming budget");
    require(finite(origin)&&std::abs(origin.x)<=1000000&&std::abs(origin.y)<=1000000&&std::abs(origin.z)<=1000000,"Invalid terrain streaming origin");
}
TerrainStream::TerrainStream(Scene& scene,std::shared_ptr<const TerrainAsset> source,TerrainStreamSettings settings)
    :scene_(scene),asset_(std::move(source)),settings_(settings) {
    settings_.validate();require(asset_&&!asset_->eager(),"Terrain streaming requires a source-only asset");resident_.reserve(settings_.maxResident);
}
TerrainStream::~TerrainStream() {try {clear();} catch(...) {}}
void TerrainStream::clear() {
    scene_.requireIdle();
    for(auto [chunk,id]:resident_) {if(auto* actor=scene_.findActor(id)) actor->destroy();++stats_.evicted;}
    resident_.clear();scene_.flushPendingDestroy();stats_.pending=0;measure();
}
void TerrainStream::measure() {
    stats_.resident=resident_.size();stats_.geometryBytes=0;
    auto bytes=[&](const Mesh& mesh) {return mesh.vertices.size()*sizeof(Vertex)+mesh.indices.size()*sizeof(std::uint32_t);};
    for(auto [chunk,id]:resident_) if(auto* actor=scene_.findActor(id)) if(auto* part=actor->getComponent<TerrainComponent>()) {
        stats_.geometryBytes+=bytes(*part->mesh);for(const auto& lod:part->mesh->lods) stats_.geometryBytes+=bytes(*lod.mesh);
    }
}
void TerrainStream::update(Vec2 focus) {
    scene_.requireIdle();
    require(std::isfinite(focus.x)&&std::isfinite(focus.y),"Invalid terrain streaming focus");
    const auto& d=asset_->data();const auto columns=(d.samplesX-2)/d.chunkCells+1,rows=(d.samplesZ-2)/d.chunkCells+1;
    const int cx=int(std::clamp(std::floor(double(focus.x)/(d.chunkCells*d.spacing.x)),0.0,double(columns-1)));
    const int cz=int(std::clamp(std::floor(double(focus.y)/(d.chunkCells*d.spacing.y)),0.0,double(rows-1)));
    std::vector<std::pair<double,std::uint32_t>> wanted;
    for(int z=std::max(0,cz-int(settings_.radiusChunks));z<=std::min(int(rows)-1,cz+int(settings_.radiusChunks));++z)
        for(int x=std::max(0,cx-int(settings_.radiusChunks));x<=std::min(int(columns)-1,cx+int(settings_.radiusChunks));++x) {
            const double dx=(x+.5)*d.chunkCells*d.spacing.x-focus.x,dz=(z+.5)*d.chunkCells*d.spacing.y-focus.y;
            wanted.emplace_back(dx*dx+dz*dz,std::uint32_t(z)*columns+std::uint32_t(x));
        }
    std::sort(wanted.begin(),wanted.end());if(wanted.size()>settings_.maxResident) wanted.resize(settings_.maxResident);
    auto desired=[&](auto chunk) {return std::any_of(wanted.begin(),wanted.end(),[&](auto entry){return entry.second==chunk;});};
    // Prepare the bounded admission batch before evicting any valid resident state.
    std::vector<std::pair<std::uint32_t,std::unique_ptr<Actor>>> prepared;
    std::size_t pending=0;
    for(auto [distance,chunk]:wanted) {
        if(std::any_of(resident_.begin(),resident_.end(),[&](auto entry){return entry.first==chunk&&scene_.findActor(entry.second);})) continue;
        ++pending;if(prepared.size()>=settings_.buildsPerUpdate) continue;
        auto actor=std::make_unique<Actor>("Streamed terrain chunk "+std::to_string(chunk));actor->transform.position=settings_.origin;
        actor->addComponent<TerrainComponent>("Terrain chunk",asset_,chunk);
        if(settings_.collision) {
            auto shape=asset_->chunkCollision(chunk);
            auto& collider=actor->addComponent<ColliderComponent>("Chunk collision",shape?shape:CollisionShape::box({.01f,.01f,.01f}));collider.setEnabled(bool(shape));
            RigidBodySettings body;body.motion=BodyMotion::Static;body.friction=.8f;actor->addComponent<RigidBodyComponent>("Chunk body",body);
        }
        prepared.emplace_back(chunk,std::move(actor));
    }
    for(auto i=resident_.begin();i!=resident_.end();) {
        if(!desired(i->first)||!scene_.findActor(i->second)) {if(auto* actor=scene_.findActor(i->second)) actor->destroy();i=resident_.erase(i);++stats_.evicted;}else ++i;
    }
    scene_.flushPendingDestroy();
    for(auto& [chunk,actor]:prepared) {auto* raw=actor.get();scene_.adoptChild(std::move(actor));resident_.emplace_back(chunk,raw->id());++stats_.built;}
    stats_.pending=pending-prepared.size();scene_.synchronize();measure();
}
void TerrainStream::apply(const TerrainEdit& edit) {
    scene_.requireIdle();
    validateEdit(edit);
    require(edit.before==asset_&&edit.after&&!edit.after->eager(),"Stale or incompatible streaming edit");if(!edit.changed()) return;
    struct Prepared {TerrainComponent* part;ColliderComponent* collider;std::shared_ptr<const Mesh> mesh;std::shared_ptr<const CollisionShape> shape;TerrainOverrides overrides;bool collisionDirty=false,wasEmpty=false;};
    std::vector<Prepared> prepared;std::size_t rebuilt=0;
    for(auto [chunk,id]:resident_) if(auto* actor=scene_.findActor(id)) {
        const bool dirty=(edit.heightsChanged||edit.holesChanged)&&std::find(edit.affectedChunks.begin(),edit.affectedChunks.end(),chunk)!=edit.affectedChunks.end();
        if(dirty) ++rebuilt;
        auto* part=actor->getComponent<TerrainComponent>();require(part&&part->asset()==edit.before,"Streamed terrain ownership changed");part->validate();
        auto* collider=actor->getComponent<ColliderComponent>();auto mesh=dirty ? edit.after->buildChunk(chunk) : part->mesh;
        std::shared_ptr<const CollisionShape> shape;
        if((dirty||edit.surfacesChanged)&&collider)shape=edit.after->chunkCollision(chunk);
        prepared.push_back({part,collider,std::move(mesh),std::move(shape),edit.paintChanged ? paintedOverrides(*part,*edit.after) : TerrainOverrides{},bool(collider)&&(dirty||edit.surfacesChanged),bool(collider)&&edit.holesChanged&&!edit.before->chunkCollision(chunk)});
    }
    for(auto& p:prepared) {if(edit.paintChanged) p.part->materialOverrides.swap(p.overrides);p.part->publish(edit.after,std::move(p.mesh),false);if(p.collisionDirty){if(p.shape){p.collider->setShape(std::move(p.shape));if(p.wasEmpty)p.collider->setEnabled(true);}else p.collider->setEnabled(false);}}
    asset_=edit.after;stats_.lastRebuilt=rebuilt;scene_.synchronize();measure();
}
std::optional<TerrainHit> terrainRaycast(const Node& root,Vec3 origin,Vec3 direction,float maximum,bool includeHoles) {
    require(finite(origin)&&finite(direction)&&std::isfinite(maximum)&&maximum>=0,"Invalid terrain ray");
    const float length=std::sqrt(dot(direction,direction));require(std::isfinite(length)&&length>1e-7f,"Invalid terrain ray direction");direction=direction*(1/length);
    std::optional<TerrainHit> hit;
    terrainVisit(root,[&](const TerrainComponent& t,const Mat4& world) {
        const auto inv=inverseAffine(world);const auto o=transformPoint(inv,origin),v=transformDirection(inv,direction);
        const auto bounds=t.mesh->localBounds();float start=0,end=hit ? hit->distance : maximum;
        if(!interval(bounds.minimum,bounds.maximum,o,v,start,end)) return;
        const auto& m=*t.mesh;
        auto test=[&](Vec3 a,Vec3 b,Vec3 c){
            const auto distance=triangle(o,v,a,b,c,hit ? hit->distance : maximum);if(!distance)return;
            const auto normal=unit(transformDirection(transpose(inv),unit(cross(b-a,c-a))));
            hit=TerrainHit{origin+direction*(*distance),normal,*distance,t.id()};
        };
        if(includeHoles){
            const auto& d=t.asset()->data();const auto& patch=t.asset()->patch();const auto columns=(d.samplesX-2)/d.chunkCells+1;
            const auto x0=patch?patch->x:(t.chunk()%columns)*d.chunkCells,z0=patch?patch->z:(t.chunk()/columns)*d.chunkCells;
            const auto nx=patch?patch->cellsX:std::min(d.chunkCells,d.samplesX-1-x0),nz=patch?patch->cellsZ:std::min(d.chunkCells,d.samplesZ-1-z0);
            const auto ox=patch?patch->offsetX:0,oz=patch?patch->offsetZ:0;
            for(std::uint32_t z=z0;z<z0+nz;++z)for(std::uint32_t x=x0;x<x0+nx;++x){
                auto a=point(d,x,z,ox,oz),b=point(d,x+1,z,ox,oz),c=point(d,x,z+1,ox,oz),e=point(d,x+1,z+1,ox,oz);test(a,c,b);test(b,c,e);
            }
        }else for(std::size_t i=0;i<m.indices.size();i+=3)test(m.vertices[m.indices[i]].position,m.vertices[m.indices[i+1]].position,m.vertices[m.indices[i+2]].position);
    });return hit;
}
std::optional<TerrainHit> terrainGround(const Node& root,float x,float z) {
    require(std::isfinite(x)&&std::isfinite(z),"Invalid terrain ground coordinate");
    float low=std::numeric_limits<float>::infinity(),high=-low;
    terrainVisit(root,[&](const TerrainComponent& t,const Mat4& world) {
        const auto b=t.mesh->localBounds();
        for(int i=0;i<8;++i) {const auto p=transformPoint(world,{i&1 ? b.maximum.x : b.minimum.x,i&2 ? b.maximum.y : b.minimum.y,i&4 ? b.maximum.z : b.minimum.z});low=std::min(low,p.y);high=std::max(high,p.y);}
    });
    if(!std::isfinite(low)||!std::isfinite(high)) return {};
    return terrainRaycast(root,{x,high+1,z},{0,-1,0},high-low+2);
}
} // namespace engine
