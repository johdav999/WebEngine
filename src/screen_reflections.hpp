#pragma once

#include "engine/scene.hpp"
#include <algorithm>

namespace engine {
inline bool screenReflectionReceiver(const MeshDraw& draw) {
    const auto& material=draw.material;
    return draw.layer==RenderLayer::World&&draw.blend==BlendMode::Opaque&&
           material.shader==MaterialShader::Pbr&&material.advanced.transmission==0&&material.metallic>=.25f&&material.roughness<=.5f;
}
inline bool hasScreenReflectionReceivers(const SceneFrame& frame) {
    return std::any_of(frame.meshes.begin(),frame.meshes.end(),[](const MeshDraw& draw) {
        return draw.cameraVisible&&screenReflectionReceiver(draw);
    });
}
} // namespace engine
