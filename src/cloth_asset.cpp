#include "engine/cloth_asset.hpp"
#include <bit>
#include <set>

namespace engine {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::invalid_argument(message);}
struct Reader {
    std::span<const std::byte> data;std::size_t at=0;
    std::uint32_t u32() {
        require(data.size()-at>=4,"Truncated cloth asset");std::uint32_t v=0;
        for(unsigned n=0;n<4;++n)v|=std::uint32_t(std::to_integer<unsigned char>(data[at++]))<<(n*8);
        return v;
    }
    float number() {float v=std::bit_cast<float>(u32());require(std::isfinite(v),"Nonfinite cloth asset value");return v;}
    Vec3 vec() {return {number(),number(),number()};}
    std::string name() {
        auto n=u32();require(n>0&&n<=255&&n<=data.size()-at,"Invalid cloth bone name");
        std::string s;for(unsigned i=0;i<n;++i){auto c=std::to_integer<char>(data[at++]);require(c!=0,"NUL cloth bone name");s+=c;}return s;
    }
};
bool near(const Mat4& a,const Mat4& b) {
    for(unsigned i=0;i<16;++i)if(std::abs(a.m[i]-b.m[i])>1e-4f)return false;
    return true;
}
}
ClothAsset loadClothAsset(std::span<const std::byte> bytes,std::shared_ptr<const Skeleton> rig,std::shared_ptr<const Material> material) {
    require(bytes.size()<=1024*1024&&rig!=nullptr,"Invalid cloth asset size/rig");rig->validate();
    Reader r{bytes};require(r.u32()==0x47434557&&r.u32()==1,"Unsupported cloth asset format");
    auto nb=r.u32(),nv=r.u32(),nt=r.u32(),nl=r.u32();
    require(nb>0&&nb<=256&&nv>0&&nv<=1024&&nt>0&&nt<=2048&&nl<=8192,"Cloth asset budget exceeded");
    auto cloth=std::make_shared<ClothDefinition>();cloth->iterations=r.u32();auto flags=r.u32();
    require((flags&~3u)==0,"Unknown cloth asset flags");cloth->sweptCollision=flags&1;cloth->edgeSelfCollision=flags&2;
    cloth->damping=r.number();cloth->thickness=r.number();cloth->friction=r.number();
    cloth->teleportDistance=r.number();cloth->selfCollisionDistance=r.number();cloth->aerodynamicDrag=r.number();
    std::vector<Mat4> world;std::set<std::string> names;
    for(const auto& j:rig->joints)world.push_back(j.parent<0?j.bindPose.matrix():world[j.parent]*j.bindPose.matrix());
    std::vector<std::uint32_t> joints,bindings;
    for(unsigned i=0;i<nb;++i) {
        auto name=r.name();auto parent=std::bit_cast<std::int32_t>(r.u32());Mat4 rest;
        for(auto& f:rest.m)f=r.number();
        require(names.insert(name).second&&parent>=-1&&parent<std::int32_t(i),"Invalid cloth rig hierarchy");
        std::uint32_t joint=0,count=0;
        for(unsigned j=0;j<rig->joints.size();++j)if(rig->joints[j].name==name){joint=j;++count;}
        require(count==1&&near(rest,world[joint]),"Cloth bone missing, ambiguous or rest transform differs");
        if(parent>=0)require(rig->joints[joint].parent==std::int32_t(joints[parent]),"Cloth bone parent differs");
        // Export roots may sit beneath imported armature/scene nodes. Their complete
        // world bind transform is still checked; non-root parent links must be exact.
        joints.push_back(joint);auto inverse=inverseAffine(rest);std::uint32_t binding=0;
        for(unsigned b=1;b<rig->bindings.size();++b)if(rig->bindings[b].joint==joint&&near(rig->bindings[b].inverseBind,inverse)){binding=b;break;}
        bindings.push_back(binding); // Unweighted control bones need no palette entry.
    }
    auto mesh=std::make_shared<Mesh>();mesh->skeleton=rig;std::vector<Vec3> rest;
    for(unsigned i=0;i<nv;++i) {
        Vertex v;v.position=r.vec();v.normal=r.vec();v.uv={r.number(),r.number()};
        SkinVertex skin;for(auto& j:skin.joints){auto b=r.u32();require(b<nb,"Cloth bone index out of range");j=bindings[b];}
        for(unsigned j=0;j<4;++j){skin.weights[j]=r.number();require(skin.weights[j]==0||skin.joints[j]!=0,"Cloth inverse bind incompatible with driver");}
        mesh->vertices.push_back(v);mesh->skin.push_back(skin);rest.push_back(v.position);
        cloth->maxDistance.push_back(r.number());cloth->inverseMass.push_back(r.number());
    }
    for(unsigned i=0;i<nt;++i){std::array<std::uint32_t,3> t{r.u32(),r.u32(),r.u32()};cloth->surfaceTriangles.push_back(t);mesh->indices.insert(mesh->indices.end(),t.begin(),t.end());}
    for(unsigned i=0;i<nl;++i)cloth->links.push_back({r.u32(),r.u32(),r.number()});
    require(r.at==bytes.size(),"Trailing cloth asset data");cloth->validate(rest);
    if(!material){auto m=std::make_shared<Material>();m->doubleSided=true;material=m;}
    material->validate();mesh->materials.push_back(std::move(material));mesh->parts.push_back({0,std::uint32_t(mesh->indices.size()),0});
    ClothingCatalog catalog;catalog.skeleton=rig;catalog.items.push_back({"asset","cloth",mesh,0,cloth});catalog.validate();
    const auto indices=mesh->indices;mesh->generateTangents();
    require(mesh->vertices.size()==nv&&mesh->indices==indices,"Cloth UV orientation requires unsupported vertex splitting");
    catalog.validate();return {std::move(mesh),std::move(cloth)};
}
}
