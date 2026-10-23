#pragma once
#include "engine/network_authoring.hpp"
#include "engine/network_gameplay_authoring.hpp"
#include "engine/network_rpc.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/network_components.hpp"
#include "engine/network_content.hpp"
#include "engine/scene_archive.hpp"
#include <map>
namespace engine::net {
inline constexpr EntityId serverEntityBase = 65536;
struct ReplicatedActor {
  std::string savedId;
  ReplicationSettings settings;
  std::uint32_t kind = 1;
  EntityId entity = 0; // Zero denotes a per-peer player instance.
  std::vector<ComponentBinding> components;
  InvocationPermissions invocations;
  std::optional<BodyMotion> physicsMotion;
};
// Optional bounded scene profile. Saved identity selects one root Actor
// template. Static visuals, inert metadata and explicitly registered gameplay
// components are admitted. Factories/behavior are application-owned trusted
// code; packages carry only validated data, never executable code or callbacks.
struct NetworkScene {
  SceneDocument document;
  std::string playerTemplate;
  std::vector<ReplicatedActor> replicated;
  ComponentReplicationRegistry components;
  InvocationRegistry invocations;
};
Bytes saveNetworkScene(const Scene &, std::span<const RenderView>,
                       const AssetLibrary &, const std::string &playerTemplate,
                       const ComponentReplicationRegistry &components = {},
                       const InvocationRegistry &invocations = {});
NetworkScene
loadNetworkScene(std::span<const std::uint8_t>,
                 const ComponentReplicationRegistry &components = {},
                 const InvocationRegistry &invocations = {});
Bytes encodeTransform(const Transform &);
Transform decodeTransform(std::span<const std::uint8_t>);
// Owns a detached immutable template and scene-scoped runtime handles. Call on
// the idle scene owner. Complete records stage validation and new instances
// before publication. Scene owns the instances; apply({}) removes them. The
// scene must outlive apply/find calls. Destruction releases only template and
// handles.
class TransformReplicas {
public:
  TransformReplicas(Scene &, const Actor &playerTemplate,
                    ComponentCodecs codecs = {});
  TransformReplicas(Scene &,
                    const std::map<std::uint32_t, const Actor *> &templates,
                    ComponentCodecs codecs = {});
  // Optional synchronous stage hook interprets payload after the first 36 pose
  // bytes. Every actor's update is prepared before any pose/state is published.
  using Stage = std::function<std::unique_ptr<PreparedComponentUpdate>(
      Actor &, const Record &)>;
  void apply(std::span<const Record>, const Stage &stage = {});
  Actor *find(EntityId) const;

private:
  Scene &scene_;
  std::map<std::uint32_t, std::unique_ptr<Node>> templates_;
  std::map<EntityId, ActorId> actors_;
  std::map<EntityId, std::uint32_t> kinds_;
  ComponentCodecs codecs_;
};
} // namespace engine::net
