#pragma once
#include "engine/rpg/types.hpp"
#include <variant>
#include <vector>

namespace engine::rpg {
struct State;
struct SpatialSnapshot;
// Game-defined quantized coordinates; no scene, navigation or renderer types.
struct TacticalPosition { std::int64_t x = 0, y = 0, z = 0; auto operator<=>(const TacticalPosition&) const = default; };
struct SingleTarget { EntityId entity; };
struct SphereTargets { TacticalPosition center; std::int64_t radius = 0; };
// Cone has a 90-degree full angle and a spherical length boundary.
struct ConeTargets { TacticalPosition origin, direction; std::int64_t length = 0; };
// Oriented cylinder with flat end caps; radius is its half-width.
struct LineTargets { TacticalPosition origin, direction; std::int64_t length = 0, radius = 0; };
enum class TargetTeam { Any, Allies, Opponents };
struct TargetQuery {
    std::variant<SingleTarget, SphereTargets, ConeTargets, LineTargets> shape = SphereTargets{};
    std::int64_t range = 0; // Actor to selected entity / area origin. Inclusive Euclidean range.
    TargetTeam team = TargetTeam::Any;
    bool includeSelf = false, requireLineOfSight = true;
};
// Exact integer geometry bounds keep all intermediates portable in uint64_t.
inline constexpr std::int64_t targetingCoordinateLimit = 500000;
inline constexpr std::int64_t targetingExtentLimit = 1000000;
inline constexpr std::int64_t targetingDirectionLimit = 1000;
// Pure preview, sorted unique IDs. Host legalTargets supplies LOS/extra eligibility.
std::vector<EntityId> selectTargets(const State&, const SpatialSnapshot&, EntityId actor, const TargetQuery&);
} // namespace engine::rpg
