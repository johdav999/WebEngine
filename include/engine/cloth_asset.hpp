#pragma once
#include "engine/clothing.hpp"
#include <cstddef>

namespace engine {
struct ClothAsset {
    std::shared_ptr<const Mesh> mesh;
    std::shared_ptr<const ClothDefinition> cloth;
};
// WECG version 1, exported by tools/blender-cloth/export_cloth.py. No implicit IO.
// Validates rest hierarchy and inverse binds before remapping to the exact driver rig.
// Geometry is in the driver's Y-up, meter, mesh bind space. Throws without publication.
ClothAsset loadClothAsset(std::span<const std::byte> bytes,
    std::shared_ptr<const Skeleton> driverRig,
    std::shared_ptr<const Material> material={});
}
