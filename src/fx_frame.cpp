#include "engine/fx_frame.hpp"
#include <limits>
namespace engine {
void FxFrame::prepare(const SceneFrame& scene,const Mat4& view) {
    entries_.clear();particles_.clear();ribbons_.clear();growths_=0;for(auto& b:batches_) b.clear();
    if(scene.particles.size()+scene.meshes.size()+scene.ribbons.size()>std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many FX instances");
    const auto ec=entries_.capacity(),pc=particles_.capacity(),rc=ribbons_.capacity();
    for(std::size_t i=0;i<scene.meshes.size();++i) {
        const auto& d=scene.meshes[i];
        if(d.cameraVisible && d.blend!=BlendMode::Opaque) entries_.push_back({Kind::Mesh,static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(entries_.size()),d.layer,d.blend,transformPoint(view,transformPoint(d.model,{})).z});
    }
    for(std::size_t i=0;i<scene.particles.size();++i) {
        const auto& p=scene.particles[i];
        entries_.push_back({Kind::Particle,static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(entries_.size()),RenderLayer::World,p.blend,transformPoint(view,p.position).z});
    }
    for(std::size_t i=0;i<scene.ribbons.size();++i){const auto& r=scene.ribbons[i];
        entries_.push_back({Kind::Ribbon,std::uint32_t(i),std::uint32_t(entries_.size()),RenderLayer::World,r.blend,transformPoint(view,(r.begin+r.end)*.5f).z});}
    if(scene.ribbons.size()>std::numeric_limits<std::uint32_t>::max()/6)throw std::length_error("Too many ribbon vertices");
    const auto inverseView=scene.ribbons.empty()?Mat4::identity():inverseAffine(view);
    const auto forward=normalized(transformDirection(inverseView,{0,0,-1})),right=normalized(transformDirection(inverseView,{1,0,0}));
    // Ascending view Z is far-to-near for this engine's right-handed camera.
    std::sort(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b) {
        if(a.layer!=b.layer) return a.layer<b.layer;
        if(a.blend!=b.blend) return a.blend<b.blend;
        if(a.blend==BlendMode::Alpha&&a.depth!=b.depth) return a.depth<b.depth;
        return a.order<b.order;
    });
    for(const auto& e:entries_) {
        auto& batches=batches_.at(static_cast<std::size_t>(e.layer));const auto bc=batches.capacity();
        if(e.kind==Kind::Particle) {
            const auto& p=scene.particles[e.index];const auto first=static_cast<std::uint32_t>(particles_.size());
            float rotation=p.rotation,extra=0;
            if(p.alignment==ParticleAlignment::Velocity) {
                const auto velocity=transformDirection(view,p.velocity);
                if(velocity.x*velocity.x+velocity.y*velocity.y>1e-10f) {
                    rotation+=std::atan2(velocity.y,velocity.x)-pi*0.5f;
                    extra=std::sqrt(dot(p.velocity,p.velocity))*p.stretch;
                }
            }
            Vec3 planeRight{},planeUp{};
            if(p.alignment==ParticleAlignment::Plane){const auto normal=normalized(p.planeNormal);
                planeRight=normalized(cross(normal,std::abs(normal.z)<.9f?Vec3{0,0,1}:Vec3{0,1,0}));planeUp=cross(normal,planeRight);}
            particles_.push_back({{p.position.x,p.position.y,p.position.z,p.size},{p.color.r,p.color.g,p.color.b,p.color.a},
                {p.normalizedAge,rotation,p.blend==BlendMode::Additive ? 1.0f : 0.0f,extra},{float(p.flipbook.columns),float(p.flipbook.rows),float(p.flipbook.frames),p.flipbook.cycles},{p.softDistance,p.alignment==ParticleAlignment::Plane?1.f:0.f,0,0},
                {planeRight.x,planeRight.y,planeRight.z,0},{planeUp.x,planeUp.y,planeUp.z,0}});
            if(!batches.empty()&&batches.back().particle&&batches.back().blend==p.blend&&batches.back().texture==p.texture
                &&batches.back().first+batches.back().count==first) ++batches.back().count;
            else batches.push_back({true,first,1,p.blend,p.texture});
        } else if(e.kind==Kind::Ribbon) {
            const auto& r=scene.ribbons[e.index];const auto first=std::uint32_t(ribbons_.size());
            auto side=[&](Vec3 tangent,float width){auto s=normalized(cross(tangent,forward));if(dot(s,s)<.5f)s=right;return s*(width*.5f);};
            const auto a=side(r.beginTangent,r.beginWidth),b=side(r.endTangent,r.endWidth);
            auto vertex=[&](Vec3 p,Color4 color,float u,float v){ribbons_.push_back({{p.x,p.y,p.z,r.blend==BlendMode::Additive?1.f:0.f},{color.r,color.g,color.b,color.a},{u,v,r.softDistance,0}});};
            vertex(r.begin-a,r.beginColor,r.uBegin,1);vertex(r.end-b,r.endColor,r.uEnd,1);vertex(r.begin+a,r.beginColor,r.uBegin,0);
            vertex(r.begin+a,r.beginColor,r.uBegin,0);vertex(r.end-b,r.endColor,r.uEnd,1);vertex(r.end+b,r.endColor,r.uEnd,0);
            if(!batches.empty()&&batches.back().ribbon&&batches.back().blend==r.blend&&batches.back().texture==r.texture
                &&batches.back().softDistance==r.softDistance&&batches.back().first+batches.back().count==first)batches.back().count+=6;
            else batches.push_back({false,first,6,r.blend,r.texture,true,r.softDistance});
        } else {
            const auto& d=scene.meshes[e.index];batches.push_back({false,e.index,1,d.blend,d.texture ? d.texture : Texture::white()});
        }
        growths_+=bc!=batches.capacity();
    }
    growths_+=(ec!=entries_.capacity())+(pc!=particles_.capacity())+(rc!=ribbons_.capacity());
}
std::size_t FxFrame::storageBytes() const {
    auto bytes=entries_.capacity()*sizeof(Entry)+particles_.capacity()*sizeof(ParticleInstance)+ribbons_.capacity()*sizeof(RibbonVertex);
    for(const auto& b:batches_) bytes+=b.capacity()*sizeof(FxBatch);
    return bytes;
}
}
