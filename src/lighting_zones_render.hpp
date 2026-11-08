#pragma once

#include "engine/environment_lighting.hpp"
#include "engine/dynamic_reflections.hpp"

namespace engine {
using LightingZoneSources = std::array<std::shared_ptr<const Environment>,maxLightingZones>;
using LightingZoneConstants = std::array<std::array<float,4>,maxLightingZones*2>;
using ReflectionProbeConstants = std::array<std::array<float,4>,3>;

inline ReflectionProbeConstants reflectionProbeConstants(const ReflectionProbeSnapshot* probe,
                                                         std::size_t cubeIndex) {
    if(!probe)return {};
    return {{{probe->minimum.x,probe->minimum.y,probe->minimum.z,probe->blendDistance},
             {probe->maximum.x,probe->maximum.y,probe->maximum.z,probe->intensity},
             {probe->position.x,probe->position.y,probe->position.z,float(cubeIndex)}}};
}

inline LightingZoneSources lightingZoneSources(std::span<const LightingZone> zones) {
    LightingZoneSources sources{};
    for(std::size_t i=0;i<zones.size();++i) sources[i]=zones[i].environment;
    return sources;
}

inline LightingZoneConstants lightingZoneConstants(std::span<const LightingZone> zones) {
    LightingZoneConstants constants{};
    for(std::size_t i=0;i<zones.size();++i) {
        const auto& zone=zones[i];
        constants[2*i]={zone.minimum.x,zone.minimum.y,zone.minimum.z,zone.blendDistance};
        constants[2*i+1]={zone.maximum.x,zone.maximum.y,zone.maximum.z,zone.intensity};
    }
    return constants;
}

inline std::array<float,4> lightingZoneRotations(std::span<const LightingZone> zones) {
    std::array<float,4> rotations{};
    for(std::size_t i=0;i<zones.size();++i) rotations[i]=zones[i].rotation;
    return rotations;
}
} // namespace engine
