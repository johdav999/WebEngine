#include "engine/fx_frame.hpp"
#include <limits>
namespace engine {
void FxFrame::prepare(const SceneFrame& scene,const Mat4& view) {
    entries_.clear();particles_.clear();growths_=0;for(auto& b:batches_) b.clear();
    if(scene.particles.size()+scene.meshes.size()>std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many FX instances");
    const auto ec=entries_.capacity(),pc=particles_.capacity();
    for(std::size_t i=0;i<scene.meshes.size();++i) {
        const auto& d=scene.meshes[i];
        if(d.cameraVisible && d.blend!=BlendMode::Opaque) entries_.push_back({false,static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(entries_.size()),d.layer,d.blend,transformPoint(view,transformPoint(d.model,{})).z});
    }
    for(std::size_t i=0;i<scene.particles.size();++i) {
        const auto& p=scene.particles[i];
        entries_.push_back({true,static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(entries_.size()),RenderLayer::World,p.blend,transformPoint(view,p.position).z});
    }
    // Ascending view Z is far-to-near for this engine's right-handed camera.
    std::sort(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b) {
        if(a.layer!=b.layer) return a.layer<b.layer;
        if(a.blend!=b.blend) return a.blend<b.blend;
        if(a.blend==BlendMode::Alpha&&a.depth!=b.depth) return a.depth<b.depth;
        return a.order<b.order;
    });
    for(const auto& e:entries_) {
        auto& batches=batches_.at(static_cast<std::size_t>(e.layer));const auto bc=batches.capacity();
        if(e.particle) {
            const auto& p=scene.particles[e.index];const auto first=static_cast<std::uint32_t>(particles_.size());
            particles_.push_back({{p.position.x,p.position.y,p.position.z,p.size},{p.color.r,p.color.g,p.color.b,p.color.a},
                {p.normalizedAge,p.rotation,0,0},{float(p.flipbook.columns),float(p.flipbook.rows),float(p.flipbook.frames),p.flipbook.cycles}});
            if(!batches.empty()&&batches.back().particle&&batches.back().blend==p.blend&&batches.back().texture==p.texture
                &&batches.back().first+batches.back().count==first) ++batches.back().count;
            else batches.push_back({true,first,1,p.blend,p.texture});
        } else {
            const auto& d=scene.meshes[e.index];batches.push_back({false,e.index,1,d.blend,d.texture ? d.texture : Texture::white()});
        }
        growths_+=bc!=batches.capacity();
    }
    growths_+=(ec!=entries_.capacity())+(pc!=particles_.capacity());
}
std::size_t FxFrame::storageBytes() const {
    auto bytes=entries_.capacity()*sizeof(Entry)+particles_.capacity()*sizeof(ParticleInstance);
    for(const auto& b:batches_) bytes+=b.capacity()*sizeof(FxBatch);
    return bytes;
}
}
