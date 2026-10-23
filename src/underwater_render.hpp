#pragma once
#include "engine/water.hpp"
#include "engine/camera_component.hpp"

namespace engine {
// Private postprocess payload shared by browser and native renderers; no scene ownership.
struct UnderwaterUniform {
    Mat4 inverseViewProjection,worldToLocal;
    std::array<float,4> viewport,eye,minimum,maximum,absorptionStrength,colorDensity,options,caustics;
    std::array<std::array<float,4>,16> regionPlanes{};
};
static_assert(sizeof(UnderwaterUniform)==512);
inline UnderwaterUniform prepareUnderwater(const UnderwaterView& water,const CameraComponent& camera,PixelViewport viewport,double time) {
    const auto eye=camera.worldPosition();const auto& s=water.settings;
    UnderwaterUniform out{camera.inverseViewProjection(viewport.width/viewport.height),water.worldToLocal,
        {viewport.x,viewport.y,viewport.width,viewport.height},{eye.x,eye.y,eye.z,0},
        {water.minimum.x,0,water.minimum.z,0},{water.maximum.x,0,water.maximum.z,0},
        {s.absorption.x,s.absorption.y,s.absorption.z,s.underwaterStrength},
        {s.underwaterColor.x,s.underwaterColor.y,s.underwaterColor.z,s.underwaterFogDensity},
        {s.underwaterMaxDistance,camera.projectionMode()==ProjectionMode::Orthographic ? 1.0f : 0.0f,0,s.amplitude*2+.02f},
        {s.causticsStrength,s.causticsScale,s.causticsStrength>0 ? float(time*s.causticsSpeed) : 0,s.causticsDepth}};
    if(water.region){out.minimum[1]=water.region->bottom;out.options[2]=float(water.region->outline.size());
        for(unsigned i=0;i<water.region->outline.size();++i)out.regionPlanes[i]=water.region->plane(i);}
    return out;
}
} // namespace engine
