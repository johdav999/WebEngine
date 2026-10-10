#include "engine/mesh.hpp"
#include <bit>
#include <map>

namespace engine {
void Mesh::validateMorphTargets() const {
    for(const auto& target:morphTargets) {
        if(target.positions.size()!=vertices.size() || (!target.normals.empty()&&target.normals.size()!=vertices.size())
            || (!target.tangents.empty()&&target.tangents.size()!=vertices.size()) || !std::isfinite(target.defaultWeight))
            throw std::invalid_argument("Morph streams must match mesh vertices and have finite default weights");
        for(const auto* stream:{&target.positions,&target.normals,&target.tangents}) for(auto v:*stream)
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)) throw std::invalid_argument("Morph offsets must be finite");
    }
}
const MeshBounds& Mesh::localBounds() const {
    if(boundsData_==vertices.data() && boundsSize_==vertices.size()) return bounds_;
    MeshBounds next;
    for(const auto& vertex:vertices) {
        const auto p=vertex.position;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) { next.valid=false;break; }
        if(!next.valid) { next.minimum=next.maximum=p;next.valid=true; }
        else {
            next.minimum={std::min(next.minimum.x,p.x),std::min(next.minimum.y,p.y),std::min(next.minimum.z,p.z)};
            next.maximum={std::max(next.maximum.x,p.x),std::max(next.maximum.y,p.y),std::max(next.maximum.z,p.z)};
        }
    }
    bounds_=next;boundsData_=vertices.data();boundsSize_=vertices.size();return bounds_;
}
std::shared_ptr<const Mesh> Mesh::cube() {
    auto mesh=std::make_shared<Mesh>();
    auto face=[&](Vec3 n,Vec3 u,Vec3 v) {
        const auto base=static_cast<std::uint32_t>(mesh->vertices.size());
        const Vec3 c=n*0.5f; u=u*0.5f; v=v*0.5f;
        mesh->vertices.insert(mesh->vertices.end(),{{c-u-v,n,{0,1}},{c+u-v,n,{1,1}},{c+u+v,n,{1,0}},{c-u+v,n,{0,0}}});
        for(auto i:{0u,1u,2u,0u,2u,3u}) mesh->indices.push_back(base+i);
    };
    face({0,0,1},{1,0,0},{0,1,0}); face({0,0,-1},{-1,0,0},{0,1,0});
    face({1,0,0},{0,0,-1},{0,1,0}); face({-1,0,0},{0,0,1},{0,1,0});
    face({0,1,0},{1,0,0},{0,0,-1}); face({0,-1,0},{1,0,0},{0,0,1});
    mesh->generateTangents();return mesh;
}
std::shared_ptr<const Mesh> Mesh::plane(float size) {
    if (!(size>0)) throw std::invalid_argument("Plane size must be positive");
    auto mesh=std::make_shared<Mesh>(); const float s=size/2;
    mesh->vertices={{{-s,0,s},{0,1,0},{0,1}},{{s,0,s},{0,1,0},{1,1}},{{s,0,-s},{0,1,0},{1,0}},{{-s,0,-s},{0,1,0},{0,0}}};
    mesh->indices={0,1,2,0,2,3};mesh->generateTangents();return mesh;
}
std::shared_ptr<const Mesh> Mesh::sphere(std::uint32_t segments,std::uint32_t rings) {
    if(segments<3||segments>256||rings<2||rings>128) throw std::invalid_argument("Invalid sphere resolution");
    auto mesh=std::make_shared<Mesh>();
    for(std::uint32_t y=0;y<=rings;++y) for(std::uint32_t x=0;x<=segments;++x) {
        const float u=float(x)/segments,v=float(y)/rings,theta=v*pi,phi=u*2*pi;
        Vec3 n{std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)};
        mesh->vertices.push_back({n*0.5f,n,{u,v}});
    }
    for(std::uint32_t y=0;y<rings;++y) for(std::uint32_t x=0;x<segments;++x) {
        const auto a=y*(segments+1)+x,b=a+segments+1;
        if(y>0) mesh->indices.insert(mesh->indices.end(),{a,a+1,b});
        if(y+1<rings) mesh->indices.insert(mesh->indices.end(),{a+1,b+1,b});
    }
    mesh->generateTangents();return mesh;
}
void Mesh::generateTangents() {
    validateMorphTargets();
    if(indices.size()%3) throw std::invalid_argument("Tangents require triangle indices");
    // Split vertices used by opposite UV orientations, including matching skin weights.
    auto newVertices=vertices;auto newSkin=skin;auto newIndices=indices;auto newMorphs=morphTargets;
    if(!skin.empty()&&skin.size()!=vertices.size()) throw std::invalid_argument("Invalid tangent skin stream");
    std::vector<std::array<std::uint32_t,2>> split(vertices.size(),{UINT32_MAX,UINT32_MAX});
    std::vector<std::uint32_t> orientations(vertices.size());
    for(std::size_t i=0;i<indices.size();i+=3) {
        for(int c=0;c<3;++c) if(indices[i+c]>=vertices.size()) throw std::invalid_argument("Invalid tangent index");
        const auto& a=vertices[indices[i]];const auto& b=vertices[indices[i+1]];const auto& c=vertices[indices[i+2]];
        const float det=(b.uv.x-a.uv.x)*(c.uv.y-a.uv.y)-(b.uv.y-a.uv.y)*(c.uv.x-a.uv.x);
        const int sign=det<0 ? 1 : 0;
        for(int corner=0;corner<3;++corner) {
            const auto old=indices[i+corner];auto& target=split[old][sign];
            if(target==UINT32_MAX) {
                if(split[old][1-sign]==UINT32_MAX) target=old;
                else { target=static_cast<std::uint32_t>(newVertices.size());newVertices.push_back(vertices[old]);orientations.push_back(sign);if(!skin.empty()) newSkin.push_back(skin[old]);
                    for(auto& morph:newMorphs) for(auto* stream:{&morph.positions,&morph.normals,&morph.tangents}) if(!stream->empty()) stream->push_back((*stream)[old]); }
                orientations[target]=sign;
            }
            newIndices[i+corner]=target;
        }
    }
    std::vector<Vec3> tangents(newVertices.size()),bitangents(newVertices.size());
    // Average across matching corners before welding, preserving normal/UV/orientation seams.
    std::map<std::array<std::uint32_t,9>,std::size_t> groups;std::vector<std::size_t> group(newVertices.size());
    for(std::size_t i=0;i<newVertices.size();++i) {
        const auto& v=newVertices[i];const std::array<float,8> attributes={v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y};
        std::array<std::uint32_t,9> key{};for(int c=0;c<8;++c) key[c]=std::bit_cast<std::uint32_t>(attributes[c]);key[8]=orientations[i];
        const auto [it,inserted]=groups.emplace(key,i);(void)inserted;group[i]=it->second;
    }
    for(std::size_t i=0;i<newIndices.size();i+=3) {
        const auto ia=newIndices[i],ib=newIndices[i+1],ic=newIndices[i+2];
        const auto& a=newVertices[ia];const auto& b=newVertices[ib];const auto& c=newVertices[ic];
        const Vec3 e1=b.position-a.position,e2=c.position-a.position;
        const float u1=b.uv.x-a.uv.x,v1=b.uv.y-a.uv.y,u2=c.uv.x-a.uv.x,v2=c.uv.y-a.uv.y,det=u1*v2-v1*u2;
        if(std::abs(det)<1e-10f) continue;
        const auto t=(e1*v2-e2*v1)*(1/det),bt=(e2*u1-e1*u2)*(1/det);
        for(auto index:{ia,ib,ic}) { const auto g=group[index];tangents[g]=tangents[g]+t;bitangents[g]=bitangents[g]+bt; }
    }
    for(std::size_t i=0;i<newVertices.size();++i) {
        const auto g=group[i];const auto n=normalized(newVertices[i].normal);auto t=normalized(tangents[g]-n*dot(n,tangents[g]));
        if(dot(t,t)<0.5f) t=normalized(cross(std::abs(n.y)<0.9f ? Vec3{0,1,0} : Vec3{0,0,1},n));
        newVertices[i].tangent={t.x,t.y,t.z,dot(cross(n,t),bitangents[g])<0 ? -1.0f : 1.0f};
    }
    vertices=std::move(newVertices);indices=std::move(newIndices);skin=std::move(newSkin);morphTargets=std::move(newMorphs);invalidateBounds();
}
} // namespace engine
