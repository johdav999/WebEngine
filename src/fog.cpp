#include "engine/fog.hpp"
#include <algorithm>

namespace engine {
void FogVolume::validate() const {
    for(float coordinate:{minimum.x,minimum.y,minimum.z,maximum.x,maximum.y,maximum.z})
        if(!std::isfinite(coordinate)||std::abs(coordinate)>1e6f) throw std::invalid_argument("Invalid fog volume bounds");
    const float smallest=std::min({maximum.x-minimum.x,maximum.y-minimum.y,maximum.z-minimum.z});
    for(float channel:{scattering.x,scattering.y,scattering.z})
        if(!std::isfinite(channel)||channel<0||channel>1) throw std::invalid_argument("Invalid fog volume scattering");
    if(smallest<=0||!std::isfinite(density)||density<0||density>100||!std::isfinite(edgeFade)||edgeFade<0||
       edgeFade>smallest*.5f||!std::isfinite(maxDistance)||maxDistance<=0||maxDistance>1e6f)
        throw std::invalid_argument("Invalid fog volume settings");
}
void FogSettings::validate() const {
    auto range=[](float v,float lo,float hi) {if(!std::isfinite(v)||v<lo||v>hi) throw std::invalid_argument("Invalid fog settings");};
    for(float v:{color.x,color.y,color.z}) range(v,0,1);
    range(density,0,100);range(startDistance,0,1e6f);range(baseHeight,-1e6f,1e6f);range(heightFalloff,0,100);
    if(volume) volume->validate();
    if(additionalVolumes.size()>maxAdditionalFogVolumes) throw std::invalid_argument("Too many additional fog volumes");
    for(const auto& extra:additionalVolumes) extra.validate();
}
bool hasActiveFogVolume(const std::optional<FogSettings>& fog) {
    return fog && ((fog->volume && fog->volume->density>0) ||
        std::any_of(fog->additionalVolumes.begin(),fog->additionalVolumes.end(),
                    [](const FogVolume& volume){ return volume.density>0; }));
}
FogUniform prepareFog(const std::optional<FogSettings>& fog,const CameraComponent& camera,
                      bool enabled,unsigned steps,bool selfShadows) {
    FogUniform out;
    if(!fog||!enabled) return out;
    fog->validate();const auto p=camera.worldPosition(),d=camera.forward();
    out.colorDensity={fog->color.x,fog->color.y,fog->color.z,fog->density};
    out.height={fog->startDistance,fog->baseHeight,fog->heightFalloff,camera.farPlane};
    out.eye={p.x,p.y,p.z,camera.projectionMode()==ProjectionMode::Orthographic ? 1.0f : 0.0f};
    out.forward={d.x,d.y,d.z,camera.nearPlane};
    if(!steps) return out;
    unsigned activeCount=0;
    const auto append=[&](const FogVolume& v) {
        if(v.density==0) return;
        const FogVolumeUniform payload{
            {v.minimum.x,v.minimum.y,v.minimum.z,v.edgeFade},
            {v.maximum.x,v.maximum.y,v.maximum.z,v.maxDistance},
            {v.scattering.x,v.scattering.y,v.scattering.z,v.density}
        };
        if(activeCount==0) {
            out.volumeMinSteps={v.minimum.x,v.minimum.y,v.minimum.z,float(std::min(steps,32u))};
            out.volumeMaxDistance=payload.maximumDistance;
            out.volumeColorDensity=payload.colorDensity;
            out.volumeOptions[0]=v.edgeFade;
            out.volumeOptions[2]=selfShadows?1.f:0.f;
        } else out.additionalVolumes[activeCount-1]=payload;
        ++activeCount;
    };
    if(fog->volume) append(*fog->volume);
    for(const auto& extra:fog->additionalVolumes) append(extra);
    out.volumeLighting[0]=float(activeCount);
    return out;
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
