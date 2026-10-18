#include "engine/route.hpp"
#include "engine/scene.hpp"
#include "engine/actor.hpp"
#include <algorithm>
namespace engine {
namespace {
std::shared_ptr<const CollisionShape> placeholder(){static const auto shape=CollisionShape::box({.1f,.1f,.1f});return shape;}
float size(Vec3 v){return std::sqrt(dot(v,v));}
Vec3 rotate(Vec3 v,Vec3 axis,float angle){return v*std::cos(angle)+cross(axis,v)*std::sin(angle)+axis*(dot(axis,v)*(1-std::cos(angle)));}
}
void SweepProfileData::validate()const{
    if(points.size()<(closed?3u:2u)||points.size()>32||!material)throw std::invalid_argument("Sweep profile requires 2..32 open or 3..32 closed vertices and a material");
    material->validate();if(surface)surface->validate();
    for(std::size_t i=0;i<points.size();++i){auto p=points[i];if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1000||std::abs(p.y)>1000)throw std::invalid_argument("Invalid sweep profile point");
        if(i+1<points.size()||closed){auto q=points[(i+1)%points.size()];if(std::hypot(p.x-q.x,p.y-q.y)<.0001f)throw std::invalid_argument("Degenerate sweep profile edge");}
    }
}
std::shared_ptr<const SweepProfile> SweepProfile::create(SweepProfileData data){data.validate();return std::shared_ptr<const SweepProfile>(new SweepProfile(std::move(data)));}
std::size_t RouteData::rings()const{if(!spline||!std::isfinite(spacing)||spacing<.01f||spacing>100)throw std::invalid_argument("Invalid route source/spacing");return std::size_t(std::ceil(spline->length()/spacing))+1;}
void RouteData::validate()const{
    if(!spline||!profile||!std::isfinite(spacing)||spacing<.01f||spacing>100||!std::isfinite(textureMeters)||textureMeters<.01f||textureMeters>10000||maxRings<2||maxRings>4097)throw std::invalid_argument("Invalid route source, spacing, UV length or rebuild budget");
    const auto edges=profile->data().points.size()-(profile->data().closed?0:1),count=rings();
    if((spline->data().closed&&count<4)||count>maxRings||count*edges*2>262144||(count-1)*edges*2>262144||derivedBytes()>64*1024*1024)throw std::invalid_argument("Route rebuild exceeds ring/vertex/triangle/64 MiB admission budget");
}
std::size_t RouteData::derivedBytes()const{if(!profile)throw std::invalid_argument("Missing sweep profile");const auto edges=profile->data().points.size()-(profile->data().closed?0:1),count=rings();return count*128+count*edges*2*(sizeof(Vertex)*2+40)+(count-1)*edges*6*sizeof(std::uint32_t)+(collision?count*edges*2*sizeof(Vec3)+(count-1)*edges*6*sizeof(std::uint32_t)+((count-1)*edges*2)*128:0);}
std::shared_ptr<const RouteAsset> RouteAsset::create(RouteData data){data.validate();return std::shared_ptr<const RouteAsset>(new RouteAsset(std::move(data)));}
RouteAsset::RouteAsset(RouteData data):data_(std::move(data)){
    struct Frame {SplineSample sample;Vec3 right,up;};std::vector<Frame> frames;const auto count=data_.rings();frames.reserve(count);
    const auto& spline=*data_.spline;auto first=spline.atDistance(0);Vec3 up{0,1,0};if(std::abs(dot(up,first.tangent))>.9f)up={0,0,1};auto right=normalized(cross(up,first.tangent));auto previous=first.tangent;
    for(std::size_t i=0;i<count;++i){auto s=spline.atDistance(spline.length()*double(i)/(count-1));if(i+1==count&&spline.data().closed)s=spline.atDistance(0);s.distance=spline.length()*double(i)/(count-1);
        const auto axis=cross(previous,s.tangent);const float sine=size(axis),cosine=dot(previous,s.tangent);if(cosine<-.95f)throw std::invalid_argument("Route sampling crosses a reversal; reduce spacing");
        if(sine>1e-6f)right=rotate(right,axis*(1/sine),std::atan2(sine,cosine));right=normalized(right-s.tangent*dot(right,s.tangent));
        frames.push_back({s,right,normalized(cross(s.tangent,right))});previous=s.tangent;
    }
    const auto& last=frames.back();const float closure=spline.data().closed?std::atan2(dot(first.tangent,cross(last.right,frames.front().right)),dot(last.right,frames.front().right)):0;
    for(std::size_t i=0;i<count;++i){auto& f=frames[i];const float angle=f.sample.bank+closure*float(i)/float(count-1);f.right=rotate(f.right,f.sample.tangent,angle);f.up=normalized(cross(f.sample.tangent,f.right));}
    // Keep the closure bit-identical, independent of floating-point transport drift.
    if(spline.data().closed){frames.back().right=frames.front().right;frames.back().up=frames.front().up;}
    const auto& profile=data_.profile->data();const auto edges=profile.points.size()-(profile.closed?0:1);auto mesh=std::make_shared<Mesh>();mesh->vertices.reserve(count*edges*2);mesh->indices.reserve((count-1)*edges*6);mesh->materials={profile.material};
    std::vector<float> across(edges+1,0);for(std::size_t j=0;j<edges;++j){auto a=profile.points[j],b=profile.points[(j+1)%profile.points.size()];across[j+1]=across[j]+std::hypot(b.x-a.x,b.y-a.y);}
    for(const auto& f:frames)for(std::size_t j=0;j<edges;++j){const auto a=profile.points[j],b=profile.points[(j+1)%profile.points.size()];const auto edge=f.right*(b.x-a.x)+f.up*(b.y-a.y);const auto normal=normalized(cross(f.sample.tangent,edge)),tangent=normalized(edge);
        for(unsigned k=0;k<2;++k){auto p=k?b:a;mesh->vertices.push_back({f.sample.position+f.right*p.x+f.up*p.y,normal,{across[j+k]/data_.textureMeters,float(f.sample.distance)/data_.textureMeters},{tangent.x,tangent.y,tangent.z,-1}});}
    }
    auto triangle=[&](unsigned a,unsigned b,unsigned c){const auto& x=mesh->vertices[a];const auto& y=mesh->vertices[b];const auto& z=mesh->vertices[c];auto n=cross(y.position-x.position,z.position-x.position);if(size(n)<1e-7f||dot(n,x.normal+y.normal+z.normal)<=0)throw std::invalid_argument("Sweep folds or degenerates; narrow profile or refine curve/spacing");mesh->indices.insert(mesh->indices.end(),{a,b,c});};
    for(std::size_t i=0;i+1<count;++i)for(std::size_t j=0;j<edges;++j){unsigned a=unsigned((i*edges+j)*2),b=a+unsigned(edges*2);triangle(a,b,a+1);triangle(a+1,b,b+1);}
    mesh->parts={{0,unsigned(mesh->indices.size()),0}};// U follows the profile edge exactly; analytic tangents also join across the V seam.
    (void)mesh->localBounds();mesh_=mesh;
    if(data_.collision){std::vector<Vec3> positions;positions.reserve(mesh->vertices.size());for(const auto& v:mesh->vertices)positions.push_back(v.position);collision_=CollisionShape::triangleMesh(positions,mesh->indices)->withSurfaces(profile.surface);}
}
RouteComponent::RouteComponent(std::string name,std::shared_ptr<const RouteAsset> asset):MeshComponent(std::move(name),{}){
    RigidBodySettings settings;settings.motion=BodyMotion::Static;body_=&addChild<RigidBodyComponent>("Route static body",settings);protectChild(*body_);
    collider_=&addChild<ColliderComponent>("Route collision",placeholder());protectChild(*collider_);configure(std::move(asset));
}
void RouteComponent::configure(std::shared_ptr<const RouteAsset> asset){
    if(!asset)throw std::invalid_argument("Missing route asset");requireIdle();
    collider_->setShape(asset->collisionShape()?asset->collisionShape():placeholder());body_->enabled=collider_->enabled=asset->data().collision;mesh=asset->mesh();asset_=std::move(asset);
}
void RouteComponent::validate()const{
    MeshComponent::validate();if(!asset_||mesh!=asset_->mesh()||transform.matrix().m!=Mat4::identity().m||body_->settings().motion!=BodyMotion::Static||body_->enabled!=asset_->data().collision||collider_->enabled!=asset_->data().collision||collider_->transform.matrix().m!=Mat4::identity().m||collider_->surface()!=nullptr||(asset_->data().collision&&collider_->shape()!=asset_->collisionShape()))throw std::invalid_argument("Route geometry/collision is managed; use identity-local route and static owning actor");
    if(owner()&&(owner()->parent()!=scene()||owner()->getComponents<RigidBodyComponent>().size()!=1||owner()->getComponents<ColliderComponent>().size()!=1))throw std::invalid_argument("Route requires its own actor without another rigid body");
}
std::unique_ptr<Actor> makeRouteActor(std::string name,std::shared_ptr<const RouteAsset> asset){auto actor=std::make_unique<Actor>(std::move(name));actor->addComponent<RouteComponent>("Swept route",std::move(asset));return actor;}
} // namespace engine
