#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <limits>
#include <vector>
namespace engine {
// Immutable after publication. Identity is the shared resource; id is a logical label.
struct PhysicalSurface {
    std::string id="default";
    float friction=1,restitution=0,tyreGrip=1,rollingResistance=0;
    void validate() const;
    bool operator==(const PhysicalSurface&) const = default;
};
// Source geometry identity, independent of solver packing and renderer LOD.
std::size_t physicalSurfaceBytes(const std::shared_ptr<const PhysicalSurface>&,const std::vector<std::shared_ptr<const PhysicalSurface>>&);
struct CollisionFeature {
    std::array<std::uint8_t,8> children{};
    std::uint8_t depth=0;
    std::uint32_t triangle=std::numeric_limits<std::uint32_t>::max();
};
struct PhysicsSurfaceHit {
    std::shared_ptr<const PhysicalSurface> surface;
    CollisionFeature feature;
};
}
