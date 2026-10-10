#pragma once
#include "engine/texture.hpp"
#include "engine/math.hpp"
#include <functional>
#include <memory>

namespace engine {
struct EnvironmentLevel { std::uint32_t size=0;std::vector<std::array<float,4>> pixels; }; // +X,-X,+Y,-Y,+Z,-Z
// Immutable baked image-based lighting. All pixels contain linear HDR radiance.
struct Environment {
    std::string name;
    std::vector<EnvironmentLevel> specular;
    EnvironmentLevel diffuse;
    std::uint32_t lutSize=0;
    std::vector<std::array<float,4>> brdf;
    void validate() const;
    static std::shared_ptr<const Environment> studio();
    static std::shared_ptr<const Environment> fromEquirectangular(const Texture& image);
    static std::shared_ptr<const Environment> bake(std::string name,const std::function<Vec3(Vec3)>& radiance,std::uint32_t size=32);
};
Vec3 cubeDirection(std::uint32_t face,float u,float v);
std::uint16_t floatToHalf(float value);
} // namespace engine
