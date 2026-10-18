#include "engine/fog.hpp"
#include <algorithm>

namespace engine {
void FogSettings::validate() const {
    auto range=[](float v,float lo,float hi) {if(!std::isfinite(v)||v<lo||v>hi) throw std::invalid_argument("Invalid fog settings");};
    for(float v:{color.x,color.y,color.z}) range(v,0,1);
    range(density,0,100);range(startDistance,0,1e6f);range(baseHeight,-1e6f,1e6f);range(heightFalloff,0,100);
}
FogUniform prepareFog(const std::optional<FogSettings>& fog,const CameraComponent& camera,bool enabled) {
    FogUniform out;
    if(!fog||!enabled) return out;
    fog->validate();const auto p=camera.worldPosition(),d=camera.forward();
    out.colorDensity={fog->color.x,fog->color.y,fog->color.z,fog->density};
    out.height={fog->startDistance,fog->baseHeight,fog->heightFalloff,camera.farPlane};
    out.eye={p.x,p.y,p.z,camera.projectionMode()==ProjectionMode::Orthographic ? 1.0f : 0.0f};
    out.forward={d.x,d.y,d.z,camera.nearPlane};return out;
}
float fogTransmission(const FogSettings& fog,Vec3 origin,Vec3 endpoint) {
    fog.validate();
    for(float v:{origin.x,origin.y,origin.z,endpoint.x,endpoint.y,endpoint.z}) if(!std::isfinite(v)) throw std::invalid_argument("Invalid fog segment");
    const double dx=double(endpoint.x)-origin.x,dy=double(endpoint.y)-origin.y,dz=double(endpoint.z)-origin.z;
    const double distance=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(distance<=fog.startDistance||fog.density==0) return 1;
    const double y=origin.y+dy*fog.startDistance/distance;
    const double a=-fog.heightFalloff*(y-fog.baseHeight),b=-fog.heightFalloff*(double(endpoint.y)-fog.baseHeight),delta=std::abs(b-a);
    const double average=std::exp(std::clamp(std::max(a,b),-40.0,40.0))*(delta<.001 ? 1-delta*.5+delta*delta/6 : -std::expm1(-delta)/delta);
    return float(std::exp(-std::min(80.0,fog.density*(distance-fog.startDistance)*average)));
}
} // namespace engine
