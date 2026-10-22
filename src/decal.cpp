#include "engine/decal.hpp"
#include "engine/scene.hpp"
#include "engine/effect_component.hpp"
#include <limits>
namespace engine {
void DecalSettings::validate() const {
    if(!texture||!texture->width||!texture->height||texture->pixels.size()!=std::uint64_t(texture->width)*texture->height*4
        ||!std::isfinite(color.x)||!std::isfinite(color.y)||!std::isfinite(color.z)||color.x<0||color.y<0||color.z<0
        ||!std::isfinite(opacity)||opacity<0||opacity>1||!std::isfinite(normalOffset)||normalOffset<0||normalOffset>1
        ||!std::isfinite(normalThreshold)||normalThreshold< -1||normalThreshold>1
        ||!std::isfinite(lifetime)||lifetime<0||lifetime>86400||!std::isfinite(fadeOut)||fadeOut<0||fadeOut>lifetime
        ||(blend!=BlendMode::Alpha&&blend!=BlendMode::Additive)||!maxSourceTriangles||maxSourceTriangles>1000000||!maxTriangles||maxTriangles>100000)
        throw std::invalid_argument("Invalid decal settings");
}
bool DecalSettings::operator==(const DecalSettings& o) const {
    return texture==o.texture&&lifetime==o.lifetime&&fadeOut==o.fadeOut&&color.x==o.color.x&&color.y==o.color.y&&color.z==o.color.z&&opacity==o.opacity&&normalOffset==o.normalOffset
        &&normalThreshold==o.normalThreshold&&blend==o.blend&&receiverMask==o.receiverMask&&maxSourceTriangles==o.maxSourceTriangles&&maxTriangles==o.maxTriangles;
}
DecalComponent::DecalComponent(std::string name,DecalSettings settings):Component(std::move(name)){setSettings(std::move(settings));}
void DecalComponent::setSettings(DecalSettings settings){settings.validate();if(settings.lifetime>0&&age_>settings.lifetime)throw std::invalid_argument("Decal lifetime precedes its current age");settings_=std::move(settings);setTickEnabled(settings_.lifetime>0);cached_=false;}
float DecalComponent::effectiveOpacity() const{return settings_.opacity*(settings_.lifetime>0&&settings_.fadeOut>0?float(std::clamp((settings_.lifetime-age_)/settings_.fadeOut,0.0,1.0)):1.f);}
void DecalComponent::tick(double seconds){
    if(settings_.lifetime<=0)return;
    if(effectSpawned_)try{setWorldTransform(spawnPose_);}catch(const std::invalid_argument&){if(scene())destroy();return;}
    for(auto* p=parent();p;p=p->parent())if(auto* effect=dynamic_cast<EffectComponent*>(p);effect&&effect->paused())return;
    age_=std::min(double(settings_.lifetime),age_+seconds);if(age_>=settings_.lifetime&&scene())destroy();
}
void DecalComponent::appendDraw(SceneFrame& frame,const Mat4& world) const {
    Mat4 inverse;
    try{inverse=inverseAffine(world);}catch(const std::invalid_argument&){return;}
    const auto direction=normalized(transformDirection(transpose(inverse),{0,1,0}));
    auto accepts=[&](const MeshDraw& d){return d.receivesDecals&&(d.visibilityMask&settings_.receiverMask)&&d.layer==RenderLayer::World&&d.blend==BlendMode::Opaque
        &&d.material.alphaMode==AlphaMode::Opaque&&d.material.shader!=MaterialShader::Water&&!d.skinPose&&!d.morphPose;};
    std::uint64_t hash=1469598103934665603ull;
    auto bytes=[&](const void* p,std::size_t n){auto* b=static_cast<const unsigned char*>(p);while(n--){hash^=*b++;hash*=1099511628211ull;}};
    bytes(world.m.data(),sizeof(world.m));
    for(const auto& d:frame.meshes)if(accepts(d)){const auto* p=d.mesh.get();bytes(&p,sizeof(p));bytes(d.model.m.data(),sizeof(d.model.m));bytes(&d.firstIndex,sizeof(d.firstIndex));bytes(&d.indexCount,sizeof(d.indexCount));}
    if(!cached_||key_!=hash){
        ++builds_;limited_=false;auto candidate=std::make_shared<Mesh>();std::uint32_t tested=0;
        struct Point {Vec3 position,normal;};
        for(const auto& d:frame.meshes){if(!accepts(d))continue;
            const auto toLocal=inverse*d.model;const auto& bounds=d.mesh->localBounds();bool outside=false;
            if(bounds.valid){Vec3 lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
                for(int i=0;i<8;++i){const auto p=transformPoint(toLocal,{i&1?bounds.maximum.x:bounds.minimum.x,i&2?bounds.maximum.y:bounds.minimum.y,i&4?bounds.maximum.z:bounds.minimum.z});lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
                outside=hi.x<-.5f||hi.y<-.5f||hi.z<-.5f||lo.x>.5f||lo.y>.5f||lo.z>.5f;}
            if(outside)continue;
            for(std::size_t j=d.firstIndex;j<std::size_t(d.firstIndex)+d.indexCount;j+=3){
                if(tested++>=settings_.maxSourceTriangles){limited_=true;break;}
                std::array<Point,12> polygon{},scratch{};std::size_t count=3;std::array<Vec3,3> positions;
                for(int k=0;k<3;++k){const auto& v=d.mesh->vertices.at(d.mesh->indices.at(j+k));positions[k]=transformPoint(d.model,v.position);polygon[k]={transformPoint(toLocal,v.position),normalized(transformDirection(d.normal,v.normal))};}
                auto face=normalized(cross(positions[1]-positions[0],positions[2]-positions[0]));if(d.mirrored)face=face*-1;
                if(dot(face,direction)<settings_.normalThreshold)continue;
                for(int plane=0;plane<6&&count;++plane){
                    auto distance=[&](Vec3 p){const float c=plane/2==0?p.x:plane/2==1?p.y:p.z;return .5f+(plane%2?-c:c);};
                    std::size_t next=0;
                    for(std::size_t k=0;k<count;++k){const auto& a=polygon[k];const auto& b=polygon[(k+1)%count];const float da=distance(a.position),db=distance(b.position);
                        if(da>=0)scratch.at(next++)=a;
                        if((da>=0)!=(db>=0)){const float t=da/(da-db);scratch.at(next++)={a.position+(b.position-a.position)*t,a.normal+(b.normal-a.normal)*t};}}
                    polygon=scratch;count=next;
                }
                if(count<3)continue;
                if(candidate->indices.size()/3+count-2>settings_.maxTriangles){limited_=true;break;}
                const auto first=std::uint32_t(candidate->vertices.size());
                for(std::size_t k=0;k<count;++k){const auto& p=polygon[k];auto n=normalized(p.normal);if(dot(n,n)<.5f)n=face;
                    candidate->vertices.push_back({p.position+transformDirection(inverse,n)*settings_.normalOffset,normalized(transformDirection(transpose(world),n)),{p.position.x+.5f,.5f-p.position.z}});}
                for(std::uint32_t k=1;k+1<count;++k)candidate->indices.insert(candidate->indices.end(),{first,first+k,first+k+1});
            }
            if(limited_)break;
        }
        projected_=limited_||candidate->indices.empty()?nullptr:candidate;key_=hash;cached_=true;
    }
    if(!projected_)return;
    Material material;material.shader=MaterialShader::Unlit;material.baseColorTexture=settings_.texture;material.doubleSided=true;
    material.alphaMode=settings_.blend==BlendMode::Alpha?AlphaMode::Blend:AlphaMode::Additive;
    MeshDraw draw{projected_,world,settings_.color,RenderLayer::World,settings_.texture,0,std::uint32_t(projected_->indices.size()),transpose(inverse),{},settings_.blend,effectiveOpacity(),material,false,false,false,true,{},false,projected_,projected_->localBounds(),id(),0,{}};
    draw.receivesDecals=false;draw.visibilityMask=visibilityMask;
    const auto layer=std::size_t(RenderLayer::World),mc=frame.meshes.capacity(),lc=frame.layerDraws[layer].capacity();
    frame.layerDraws[layer].push_back(std::uint32_t(frame.meshes.size()));frame.meshes.push_back(std::move(draw));++frame.meshCount;++frame.decalCount;
    frame.storageGrowths+=(mc!=frame.meshes.capacity())+(lc!=frame.layerDraws[layer].capacity());
}
}
