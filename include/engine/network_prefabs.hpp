#pragma once
#include "engine/network_scene.hpp"
#include "engine/prefab_asset.hpp"

namespace engine::net {
inline constexpr EntityId runtimeEntityBase=0x01000000;
struct SpawnedPrefab {
  EntityId entity=0;
  PeerId owner=0;
  std::uint32_t kind=0;
  ActorId actor;
};
// Authority-side whitelist/prototypes and generation-checked instance bindings.
// Scene owns actors and must outlive all calls. Idle owner only. Destruction does
// not destroy scene actors; use clear() explicitly. No retained game callbacks.
class PrefabSpawner {
public:
  explicit PrefabSpawner(Scene &,ComponentCodecs codecs={},EntityId first=runtimeEntityBase,
                         std::size_t capacity=48);
  void add(std::uint32_t kind,const Actor &prototype);
  void add(std::uint32_t kind,const PrefabAsset &asset){add(kind,asset.definition());}
  // Initialize only the detached candidate; failures publish nothing or consume
  // no identity. Do not modify external state from this synchronous callback.
  EntityId spawn(std::uint32_t kind,PeerId owner,const Transform &,
                 const std::function<void(Actor &)> &initialize={});
  bool despawn(EntityId);
  void clear();
  Actor *find(EntityId) const;
  const auto &instances() const {return instances_;}
private:
  Scene &scene_;
  ComponentCodecs codecs_;
  EntityId next_;
  std::size_t capacity_;
  std::map<std::uint32_t,std::unique_ptr<Node>> templates_;
  std::map<EntityId,SpawnedPrefab> instances_;
};
} // namespace engine::net
