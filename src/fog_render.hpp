#pragma once

#include "engine/fog.hpp"
#include "engine/render_quality.hpp"
#include <algorithm>

namespace engine {
inline bool fogHasSingleActiveVolume(const std::optional<FogSettings>& fog) {
    if(!fog)return false;
    const auto additional=std::count_if(fog->additionalVolumes.begin(),fog->additionalVolumes.end(),
        [](const FogVolume& box){return box.density>0;});
    return additional+(fog->volume&&fog->volume->density>0)==1;
}

inline FogUniform prepareRenderFog(const std::optional<FogSettings>& fog,const CameraComponent& camera,
                                   bool enabled,const RenderQuality& quality,bool analyticOnly=false) {
    auto result=prepareFog(fog,camera,enabled,analyticOnly?0:quality.volumetricFogSteps,
                           quality.volumetricFogSelfShadows);
    if(result.volumeLighting[0]>0) {
        result.volumeOptions[3]=float(unsigned(quality.volumetricFogSurfaceShadows)
                                    |(unsigned(quality.volumetricFogSoftShadows)<<1));
        result.volumeLighting[1]=float(quality.volumetricFogMaxLights);
    }
    return result;
}
} // namespace engine
