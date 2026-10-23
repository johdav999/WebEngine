#pragma once
#include "engine/water.hpp"
#include "engine/component.hpp"

namespace engine {
// Optional finite water footprint on the water MeshComponent itself. Its XZ
// coordinates and negative bottom are in the surface's local space (surface Y=0).
// Ordinary component tags preserve the versioned annotation through archives,
// clones and history, without a new scene/component codec or retained owner.
void setWaterRegion(Component&,const std::optional<WaterRegion>&);
// Missing annotation: legacy rectangular, bottomless behavior. Malformed or
// unknown reserved annotations throw; they never silently widen a finite region.
std::optional<WaterRegion> waterRegion(const Component&);
} // namespace engine
