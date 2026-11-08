#include "engine/cluster_lod.hpp"
#include "engine/camera_component.hpp"
#include "engine/render_frame.hpp"
#include <meshoptimizer.h>
#include <algorithm>
#include <limits>

namespace engine {
namespace {
constexpr std::size_t maxVertices=300000,maxIndices=900000,maxParts=64,maxPackedIndices=6000000;
constexpr unsigned maxDepth=8;
constexpr std::size_t leafTriangles=128;
constexpr float attributeWeights[]{.1f,.1f,.1f,.1f,.1f};

float axis(Vec3 value,int index) {
    return index==0?value.x:index==1?value.y:value.z;
}
void extend(MeshBounds& bounds,Vec3 p) {
    if(!bounds.valid) { bounds={p,p,true};return; }
    bounds.minimum={std::min(bounds.minimum.x,p.x),std::min(bounds.minimum.y,p.y),std::min(bounds.minimum.z,p.z)};
    bounds.maximum={std::max(bounds.maximum.x,p.x),std::max(bounds.maximum.y,p.y),std::max(bounds.maximum.z,p.z)};
}
struct Triangle {
    std::uint32_t first;
    Vec3 center;
};
struct BuildGroup {
    ClusterGroup group;
    std::vector<std::uint32_t> indices;
    unsigned depth;
};
std::vector<std::uint64_t> perimeter(std::span<const std::uint32_t> indices) {
    std::vector<std::uint64_t> edges;
    edges.reserve(indices.size());
    for(std::size_t i=0;i<indices.size();i+=3)for(unsigned j=0;j<3;++j) {
        const auto a=indices[i+j],b=indices[i+(j+1)%3];
        if(a!=b)edges.push_back((std::uint64_t(std::min(a,b))<<32)|std::max(a,b));
    }
    std::sort(edges.begin(),edges.end());
    std::size_t kept=0;
    for(std::size_t i=0;i<edges.size();) {
        auto end=i+1;
        while(end<edges.size()&&edges[i]==edges[end])++end;
        // Retain both open and non-manifold edges with their multiplicities.
        if(end-i!=2)for(auto j=i;j<end;++j)edges[kept++]=edges[j];
        i=end;
    }
    edges.resize(kept);
    return edges;
}
struct Builder {
    const Mesh& source;
    std::vector<BuildGroup> groups;
    std::vector<unsigned char> locks;
    float maxPatchError;

    std::uint32_t build(std::span<Triangle> triangles,unsigned depth,bool partition=true) {
        MeshBounds bounds,centers;
        for(const auto& triangle:triangles) {
            extend(centers,triangle.center);
            for(int i=0;i<3;++i)extend(bounds,source.vertices[source.indices[triangle.first+i]].position);
        }
        const auto id=std::uint32_t(groups.size());
        groups.push_back({{bounds,{},0,{}},{},depth});
        if(partition&&triangles.size()>leafTriangles&&depth<maxDepth) {
            const auto extent=centers.maximum-centers.minimum;
            int split=extent.y>extent.x?1:0;
            if(extent.z>axis(extent,split))split=2;
            const auto middle=triangles.size()/2;
            std::nth_element(triangles.begin(),triangles.begin()+middle,triangles.end(),[split](const Triangle& a,const Triangle& b) {
                const float x=axis(a.center,split),y=axis(b.center,split);
                return x==y?a.first<b.first:x<y;
            });
            const auto left=build(triangles.first(middle),depth+1);
            const auto right=build(triangles.subspan(middle),depth+1);
            groups[id].group.children={left,right};
        }
        auto& node=groups[id];
        node.indices.reserve(triangles.size()*3);
        // Stable order within a partition, independent of nth_element implementation.
        std::sort(triangles.begin(),triangles.end(),[](const Triangle& a,const Triangle& b){return a.first<b.first;});
        for(const auto& triangle:triangles)
            node.indices.insert(node.indices.end(),source.indices.begin()+triangle.first,source.indices.begin()+triangle.first+3);
        if(!node.group.leaf()) {
            const auto& left=groups[node.group.children[0]];
            const auto& right=groups[node.group.children[1]];
            const auto target=std::max<std::size_t>(3,(left.indices.size()+right.indices.size())/6*3);
            std::vector<std::uint32_t> simplified(node.indices.size());
            const auto boundary=perimeter(node.indices);
            for(auto edge:boundary) {
                locks[std::uint32_t(edge>>32)]=meshopt_SimplifyVertex_Lock;
                locks[std::uint32_t(edge)]=meshopt_SimplifyVertex_Lock;
            }
            float error=0;
            const auto size=meshopt_simplifyWithAttributes(simplified.data(),node.indices.data(),node.indices.size(),
                &source.vertices.front().position.x,source.vertices.size(),sizeof(Vertex),&source.vertices.front().normal.x,
                sizeof(Vertex),attributeWeights,5,locks.data(),target,maxPatchError,
                meshopt_SimplifyLockBorder|meshopt_SimplifyErrorAbsolute|meshopt_SimplifySparse,&error);
            for(auto edge:boundary)locks[std::uint32_t(edge>>32)]=locks[std::uint32_t(edge)]=0;
            node.group.error=std::max(left.group.error,right.group.error);
            simplified.resize(size);
            if(size&&size<left.indices.size()+right.indices.size()&&std::isfinite(error)&&perimeter(simplified)==boundary) {
                node.indices.swap(simplified);
                node.group.error=std::max(error,node.group.error);
            } else {
                // A constrained patch can still coarsen to its children's existing cut.
                node.indices=left.indices;
                node.indices.insert(node.indices.end(),right.indices.begin(),right.indices.end());
            }
        }
        return id;
    }
};

double rowLength(const Mat4& m,int row) {
    return std::hypot(double(m.m[row]),double(m.m[4+row]),double(m.m[8+row]));
}
double projectedError(const ClusterGroup& group,const Mat4& clip,const DetailView& view) {
    const double full=std::numeric_limits<double>::infinity();
    const double error=group.error;
    const double dw=rowLength(clip,3)*error,dz=rowLength(clip,2)*error;
    double minW=full,maxX=0,maxY=0;
    for(int i=0;i<8;++i) {
        const auto& lo=group.bounds.minimum;
        const auto& hi=group.bounds.maximum;
        const Vec3 p{i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z};
        double q[4];
        for(int r=0;r<4;++r)q[r]=double(clip.m[r])*p.x+double(clip.m[4+r])*p.y+double(clip.m[8+r])*p.z+clip.m[12+r];
        if(!(q[3]>dw+1e-8&&q[2]>dz))return full;
        minW=std::min(minW,q[3]);maxX=std::max(maxX,std::abs(q[0]/q[3]));maxY=std::max(maxY,std::abs(q[1]/q[3]));
    }
    // Bound the change in x/w and y/w over a local-space displacement of error.
    const auto x=(rowLength(clip,0)*error+maxX*dw)*view.width;
    const auto y=(rowLength(clip,1)*error+maxY*dw)*view.height;
    return .5*std::max(x,y)/(minW-dw);
}
}

std::shared_ptr<const ClusterLod> ClusterLod::build(const Mesh& source) {
    return build(source,std::numeric_limits<float>::max());
}

std::shared_ptr<const ClusterLod> ClusterLod::build(const Mesh& source,float maxPatchError) {
    if(!(maxPatchError>0)||!std::isfinite(maxPatchError))throw std::invalid_argument("Invalid cluster patch error limit");
    if(source.vertices.empty()||source.vertices.size()>maxVertices||source.indices.empty()
        ||source.indices.size()>maxIndices||source.indices.size()%3||source.parts.size()>maxParts
        ||source.skeleton||!source.skin.empty()||!source.morphTargets.empty())
        throw std::invalid_argument("Cluster LOD requires bounded static indexed geometry");
    for(const auto& vertex:source.vertices) {
        for(float value:{vertex.position.x,vertex.position.y,vertex.position.z,vertex.normal.x,vertex.normal.y,
                         vertex.normal.z,vertex.uv.x,vertex.uv.y})
            if(!std::isfinite(value)||std::abs(value)>1000000)throw std::invalid_argument("Invalid cluster LOD vertex");
        for(float value:vertex.tangent)if(!std::isfinite(value))throw std::invalid_argument("Invalid cluster LOD tangent");
    }
    for(auto index:source.indices)if(index>=source.vertices.size())throw std::invalid_argument("Invalid cluster LOD index");
    std::size_t end=0;
    for(const auto& part:source.parts) {
        if(part.firstIndex!=end||!part.indexCount||part.indexCount%3||part.indexCount>source.indices.size()-end
            ||part.materialIndex>=source.materials.size())throw std::invalid_argument("Invalid cluster LOD material partition");
        end+=part.indexCount;
    }
    if(!source.parts.empty()&&end!=source.indices.size())throw std::invalid_argument("Incomplete cluster LOD material partition");

    auto result=std::shared_ptr<ClusterLod>(new ClusterLod);
    auto geometry=std::make_shared<Mesh>();
    geometry->vertices=source.vertices;
    Builder builder{source,{},std::vector<unsigned char>(source.vertices.size()),maxPatchError};
    auto buildPart=[&](std::uint32_t first,std::uint32_t count) {
        std::vector<Triangle> triangles;
        triangles.reserve(count/3);
        for(auto index=first;index<first+count;index+=3) {
            const auto a=source.vertices[source.indices[index]].position;
            const auto b=source.vertices[source.indices[index+1]].position;
            const auto c=source.vertices[source.indices[index+2]].position;
            triangles.push_back({index,(a+b+c)*(1.f/3.f)});
        }
        // A non-manifold edge can have triangles on both sides of multiple
        // independent cuts. Keep that part intact.
        const auto boundary=perimeter(std::span(source.indices).subspan(first,count));
        const bool manifold=std::adjacent_find(boundary.begin(),boundary.end())==boundary.end();
        result->roots_.push_back(builder.build(triangles,0,manifold));
    };
    if(source.parts.empty())buildPart(0,std::uint32_t(source.indices.size()));
    else for(const auto& part:source.parts)buildPart(part.firstIndex,part.indexCount);
    for(unsigned depth=0;depth<=maxDepth;++depth)for(auto& node:builder.groups) {
        if(node.depth!=depth)continue;
        if(node.indices.size()>maxPackedIndices-geometry->indices.size())throw std::length_error("Cluster LOD packed index budget exceeded");
        node.group.range={std::uint32_t(geometry->indices.size()),std::uint32_t(node.indices.size())};
        geometry->indices.insert(geometry->indices.end(),node.indices.begin(),node.indices.end());
    }
    for(const auto& node:builder.groups)result->groups_.push_back(node.group);
    result->geometry_=std::move(geometry);
    return result;
}

bool ClusterLod::select(std::size_t part,const Mat4& model,const DetailView& view,float scale,
                        const Frustum* frustum,std::span<ClusterRange> output,std::size_t& count,
                        const std::function<bool(std::uint32_t,double)>& pageReady) const {
    count=0;
    if(part>=roots_.size()||!(view.width>0&&view.height>0&&scale>0))return false;
    const auto clip=view.viewProjection*model;
    // A part has at most 2^maxDepth leaves, regardless of source triangle count.
    std::array<ClusterRange,1u<<maxDepth> selected;
    std::size_t size=0;
    auto visit=[&](auto&& self,std::uint32_t id)->void {
        const auto& node=groups_[id];
        if(frustum&&!frustum->intersectsTransformedAabb(node.bounds.minimum,node.bounds.maximum,model))return;
        if(!node.leaf()) {
            const auto error=projectedError(node,clip,view)*scale;
            if(!(error<=1)) {
                // Request both halves and retain the complete parent until both can draw.
                bool ready=true;
                if(pageReady)for(auto child:node.children) {
                    if(!pageReady(groups_[child].range.page,error))ready=false;
                }
                if(ready) {
                    for(auto child:node.children)self(self,child);
                    return;
                }
            }
        }
        auto range=node.range;
        range.bounds=node.bounds;
        selected[size++]=range;
    };
    visit(visit,roots_[part]);
    std::sort(selected.begin(),selected.begin()+size,[](auto a,auto b){
        return a.page==b.page?a.firstIndex<b.firstIndex:a.page<b.page;
    });
    std::size_t runs=0;
    for(std::size_t i=0;i<size;++i) {
        if(runs&&selected[runs-1].page==selected[i].page
            &&selected[runs-1].firstIndex+selected[runs-1].indexCount==selected[i].firstIndex) {
            selected[runs-1].indexCount+=selected[i].indexCount;
            extend(selected[runs-1].bounds,selected[i].bounds.minimum);
            extend(selected[runs-1].bounds,selected[i].bounds.maximum);
        } else selected[runs++]=selected[i];
    }
    if(runs>output.size())return false;
    std::copy_n(selected.begin(),runs,output.begin());
    count=runs;
    return true;
}
} // namespace engine
