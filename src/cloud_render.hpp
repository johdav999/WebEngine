#pragma once

#include "engine/render_quality.hpp"
#include "engine/sky.hpp"
#include "fog_grid.hpp"
#include <limits>

namespace engine::cloud_grid {
inline constexpr std::uint32_t width=64,height=36,depth=32;
inline constexpr std::uint32_t baseIndex=width*height*fog_grid::slices;
inline constexpr std::uint32_t bytes=width*height*(fog_grid::slices+depth)*8;
inline constexpr std::uint32_t shadowLayer=4;
}

namespace engine {
inline CloudUniform prepareRenderClouds(const std::optional<SkyBackground>& sky,const CameraComponent& camera,
                                       const RenderQuality& quality,bool skyEnabled=true,
                                       std::uint32_t shadowResolutionOverride=0,bool shadowsEnabled=true) {
    quality.validate();
    if(shadowResolutionOverride>16384)throw std::invalid_argument("Invalid cloud shadow resolution");
    CloudUniform result;
    if(!sky)return result;
    sky->validate();
    const auto& clouds=sky->clouds;
    if(!skyEnabled||!clouds.enabled||!clouds.volumetric||!quality.volumetricCloudSteps||clouds.coverage<=0||clouds.density<=0)
        return result;

    const auto eye=camera.worldPosition();
    if(!std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z))
        throw std::invalid_argument("Invalid cloud camera origin");
    const auto direction=normalized(sky->dayNight.enabled?evaluateDayNight(sky->dayNight).sunDirection:sky->atmosphere.sunDirection);
    result.layer={clouds.baseHeight,clouds.thickness,clouds.maxDistance,float(quality.volumetricCloudDepth)};
    result.eye={eye.x,eye.y,eye.z,camera.projectionMode()==ProjectionMode::Perspective?1.f:0.f};
    result.sun={direction.x,direction.y,direction.z,0};
    if(!quality.volumetricCloudShadows||!shadowsEnabled||clouds.extinction<=0||direction.y<=.01f||quality.volumetricCloudShadowStrength<=0)
        return result;

    const auto resolution=shadowResolutionOverride?shadowResolutionOverride:quality.shadowResolution;
    const double halfRange=quality.volumetricCloudShadowDistance,texel=2*halfRange/resolution;
    const double distance=(double(clouds.baseHeight)-eye.y)/direction.y;
    const double projectedX=double(eye.x)+direction.x*distance,projectedZ=double(eye.z)+direction.z*distance;
    const double snappedX=std::round(projectedX/texel)*texel,snappedZ=std::round(projectedZ/texel)*texel;
    const auto representable=[&](double value) {
        const double limit=std::numeric_limits<float>::max();
        if(!std::isfinite(value)||value-halfRange<-limit||value+halfRange>limit)return false;
        const float narrowed=static_cast<float>(value);
        const double spacing=std::abs(double(std::nextafter(narrowed,std::numeric_limits<float>::infinity()))-narrowed);
        return spacing<=texel;
    };
    if(!representable(snappedX)||!representable(snappedZ))return result;
    result.sun[3]=quality.volumetricCloudShadowStrength;
    result.shadow={float(snappedX),float(snappedZ),float(halfRange),1.f/resolution};
    return result;
}
} // namespace engine
