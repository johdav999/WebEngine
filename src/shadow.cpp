#include "engine/shadow.hpp"

namespace engine {
namespace { bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); } }
void ShadowSettings::validate() const {
    if(!finite(center)||!std::isfinite(halfExtent)||halfExtent<=0||!std::isfinite(halfDepth)||halfDepth<=0
        ||!std::isfinite(depthBias)||depthBias<0||depthBias>0.1f||!std::isfinite(normalBias)||normalBias<0)
        throw std::invalid_argument("Invalid directional shadow settings");
}
Mat4 directionalShadowMatrix(Vec3 direction,Vec3 center,const ShadowSettings& settings,std::uint32_t resolution) {
    settings.validate();
    const float length=std::sqrt(dot(direction,direction));
    if(!finite(direction)||!finite(center)||!std::isfinite(length)||length<1e-7f||!resolution)
        throw std::invalid_argument("Invalid directional shadow view");
    const auto forward=direction*(1/length);
    const auto up=std::abs(forward.y)>0.99f ? Vec3{0,0,1} : Vec3{0,1,0};
    const auto right=normalized(cross(forward,up)),vertical=cross(right,forward);
    const double texel=2.0*settings.halfExtent/resolution;
    Mat4 view=Mat4::identity();
    view.m[0]=right.x;view.m[4]=right.y;view.m[8]=right.z;
    view.m[1]=vertical.x;view.m[5]=vertical.y;view.m[9]=vertical.z;
    view.m[2]=-forward.x;view.m[6]=-forward.y;view.m[10]=-forward.z;
    view.m[12]=float(-std::round(dot(right,center)/texel)*texel);
    view.m[13]=float(-std::round(dot(vertical,center)/texel)*texel);
    view.m[14]=dot(forward,center)-settings.halfDepth;
    const auto result=orthographic(2*settings.halfExtent,1,0,2*settings.halfDepth)*view;
    for(float value:result.m) if(!std::isfinite(value)) throw std::invalid_argument("Unrepresentable directional shadow view");
    return result;
}
} // namespace engine
