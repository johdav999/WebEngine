#include "engine/render_view.hpp"

namespace engine {
void RenderViews::set(std::span<const RenderView> views) {
    for(const auto& view:views) {
        if(!view.camera) throw std::invalid_argument("Render view requires a camera");
        if(view.clearColor) for(float value:*view.clearColor)
            if(!std::isfinite(value) || value<0 || value>1)
                throw std::invalid_argument("Render view clear color must be finite and in [0,1]");
    }
    std::vector<RenderView> candidate(views.begin(),views.end());
    views_.swap(candidate);
}
void RenderViews::resolve(std::uint32_t width,std::uint32_t height,std::vector<ResolvedRenderView>& output) const {
    output.clear();
    if(!width || !height) return;
    const auto edge=[](float pixel,std::uint32_t limit) {
        return static_cast<std::uint32_t>(std::clamp(std::ceil(double(pixel)-0.5),0.0,double(limit)));
    };
    for(std::size_t i=0;i<views_.size();++i) {
        const auto& camera=*views_[i].camera;
        if(!camera.active()) continue;
        auto v=camera.pixelViewport(float(width),float(height));
        // Clamp float roundoff at the render-target edge before WebGPU validation.
        v.width=std::min(v.width,float(width)-v.x);v.height=std::min(v.height,float(height)-v.y);
        const auto x=edge(v.x,width),y=edge(v.y,height);
        const auto right=edge(v.x+v.width,width),bottom=edge(v.y+v.height,height);
        if(right>x && bottom>y) output.push_back({i,v,x,y,right-x,bottom-y});
    }
}
} // namespace engine
