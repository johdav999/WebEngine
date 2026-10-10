#pragma once
#include "engine/mesh.hpp"
namespace engine {
// Conservative AABB diameter in pixels/texels, also for orthographic views.
// Near-plane crossings and unavailable bounds retain full detail.
float projectedMeshPixels(const MeshBounds& bounds,const Mat4& model,const Mat4& viewProjection,float width,float height);
std::size_t selectMeshLod(const Mesh& mesh,float pixels,std::size_t previous);
}
