#include "engine/render_view.hpp"

namespace engine {
void RenderViews::set(std::span<const RenderView> views) {
    std::array<const RenderSurface*,maxSurfaces> targets{};
    std::size_t targetCount=0;
    std::uint64_t pixels=0;
    for(const auto& view:views) {
        if(!view.camera) throw std::invalid_argument("Render view requires a camera");
        if(view.clearColor) for(float value:*view.clearColor)
            if(!std::isfinite(value) || value<0 || value>1)
                throw std::invalid_argument("Render view clear color must be finite and in [0,1]");
        if(!view.target) continue;
        const auto end=targets.begin()+targetCount;
        if(std::find(targets.begin(),end,view.target.get())!=end)
            throw std::invalid_argument("A render surface can only have one producer");
        pixels+=std::uint64_t(view.target->width())*view.target->height();
        if(targetCount==maxSurfaces || pixels>maxSurfacePixels)
            throw std::invalid_argument("Render surface budget exceeded");
        targets[targetCount++]=view.target.get();
    }
    std::vector<RenderView> candidate(views.begin(),views.end());
    views_.swap(candidate);
}
namespace {
void resolveView(const CameraComponent& camera,std::size_t index,std::uint32_t width,
                 std::uint32_t height,std::vector<ResolvedRenderView>& output) {
    if(!camera.active()) return;
    const auto edge=[](float pixel,std::uint32_t limit) {
        return static_cast<std::uint32_t>(std::clamp(std::ceil(double(pixel)-0.5),0.0,double(limit)));
    };
    auto v=camera.pixelViewport(float(width),float(height));
    // Clamp float roundoff at the render-target edge before WebGPU validation.
    v.width=std::min(v.width,float(width)-v.x);v.height=std::min(v.height,float(height)-v.y);
    const auto x=edge(v.x,width),y=edge(v.y,height);
    const auto right=edge(v.x+v.width,width),bottom=edge(v.y+v.height,height);
    if(right>x && bottom>y) output.push_back({index,v,x,y,right-x,bottom-y});
}
}
void RenderViews::resolve(std::uint32_t width,std::uint32_t height,std::vector<ResolvedRenderView>& output) const {
    output.clear();
    if(!width || !height) return;
    for(std::size_t i=0;i<views_.size();++i)
        if(!views_[i].target) resolveView(*views_[i].camera,i,width,height,output);
}
void RenderViews::resolveSurfaces(std::vector<ResolvedRenderView>& output) const {
    output.clear();
    for(std::size_t i=0;i<views_.size();++i) {
        const auto& view=views_[i];
        if(view.target && view.refreshSurface)
            resolveView(*view.camera,i,view.target->width(),view.target->height(),output);
    }
}
bool RenderViews::hasSurface(const RenderSurface* target) const {
    return target && std::any_of(views_.begin(),views_.end(),[target](const RenderView& view) {
        return view.target.get()==target;
    });
}
} // namespace engine
