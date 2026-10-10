#include "engine/mesh_detail.hpp"
#include <limits>
namespace engine {
float projectedMeshPixels(const MeshBounds& b,const Mat4& model,const Mat4& viewProjection,float width,float height) {
    const auto full=std::numeric_limits<float>::infinity();
    if(!b.valid || !(width>0 && height>0)) return full;
    const auto m=viewProjection*model;
    double lowX=full,lowY=full,highX=-full,highY=-full;
    for(int i=0;i<8;++i) {
        const Vec3 p{i&1 ? b.maximum.x : b.minimum.x,i&2 ? b.maximum.y : b.minimum.y,i&4 ? b.maximum.z : b.minimum.z};
        double q[4]{};
        for(int r=0;r<4;++r) q[r]=double(m.m[r])*p.x+double(m.m[4+r])*p.y+double(m.m[8+r])*p.z+m.m[12+r];
        if(!(q[3]>1e-8) || q[2]<=0) return full;
        const auto x=q[0]/q[3],y=q[1]/q[3];
        if(!std::isfinite(x)||!std::isfinite(y)) return full;
        lowX=std::min(lowX,x);highX=std::max(highX,x);lowY=std::min(lowY,y);highY=std::max(highY,y);
    }
    return float(std::max((highX-lowX)*width,(highY-lowY)*height)*0.5);
}
std::size_t selectMeshLod(const Mesh& mesh,float pixels,std::size_t previous) {
    if(std::isnan(pixels)||pixels<0) return 0;
    auto level=std::min(previous,mesh.lods.size());
    while(level && pixels>mesh.lods[level-1].maxPixels*1.1f) --level;
    while(level<mesh.lods.size() && pixels<mesh.lods[level].maxPixels*0.9f) ++level;
    return level;
}
}
