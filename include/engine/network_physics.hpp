#pragma once
#include "engine/network_scene.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/collider_component.hpp"

namespace engine::net {
// Pose remains the record's ordinary Transform. This bounded tail carries only
// authoritative velocity/configuration status, never a solver checkpoint.
struct ReplicatedPhysics {
  BodyMotion motion=BodyMotion::Static;
  bool enabled=true;
  Vec3 linear{},angular{};
};
inline constexpr std::size_t physicsStateBytes=28;
Bytes encodePhysics(const ReplicatedPhysics &);
ReplicatedPhysics decodePhysics(std::span<const std::uint8_t>);
ReplicatedPhysics capturePhysics(const Actor &);
// On detached replica clones, before adoption. Disable solver participation on
// both body and collider; render/snapshot transforms remain independent.
void preparePhysicsReplica(Actor &);
// For a loaded client bootstrap, before starting its Application simulation.
// All local physics is passive in this authoritative-scene profile.
void preparePhysicsClient(Scene &);
} // namespace engine::net
