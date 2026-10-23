#include "engine/network_prefabs.hpp"
#include <limits>

namespace engine::net {
PrefabSpawner::PrefabSpawner(Scene &scene,ComponentCodecs codecs,EntityId first,std::size_t capacity)
    :scene_(scene),codecs_(std::move(codecs)),next_(first),capacity_(capacity){
  if(first<runtimeEntityBase||capacity>maxEntities)throw std::invalid_argument("Prefab identity/capacity");
}
void PrefabSpawner::add(std::uint32_t kind,const Actor &prototype){
  if(!kind||templates_.contains(kind)||templates_.size()>=32)throw std::invalid_argument("Prefab catalogue kind/budget");
  auto copy=SceneArchive::duplicateSubtree(prototype,{},codecs_);
  templates_.emplace(kind,std::move(copy));
}
EntityId PrefabSpawner::spawn(std::uint32_t kind,PeerId owner,const Transform &transform,
                               const std::function<void(Actor &)> &initialize){
  scene_.requireIdle();
  auto at=templates_.find(kind);
  if(at==templates_.end()||instances_.size()>=capacity_||next_==std::numeric_limits<EntityId>::max())
    throw std::invalid_argument("Prefab spawn type/capacity/identity");
  (void)encodeTransform(transform);
  auto candidate=SceneArchive::duplicateSubtree(*at->second,{},codecs_);
  auto &actor=dynamic_cast<Actor &>(*candidate);
  if(initialize)initialize(actor);
  actor.setLocalTransform(transform);
  // Allocate instance storage before attaching. Node adoption rolls itself back
  // on attachment failure. No application callbacks run in this transaction.
  auto staged=instances_;staged.emplace(next_,SpawnedPrefab{next_,owner,kind,{}});
  scene_.adoptChild(std::move(candidate));
  staged.at(next_).actor=actor.id();instances_.swap(staged);
  return next_++;
}
Actor *PrefabSpawner::find(EntityId entity) const {
  auto at=instances_.find(entity);return at==instances_.end()?nullptr:scene_.findActor(at->second.actor);
}
bool PrefabSpawner::despawn(EntityId entity){
  scene_.requireIdle();auto at=instances_.find(entity);if(at==instances_.end())return false;
  if(auto *actor=find(entity))actor->destroy();instances_.erase(at);return true;
}
void PrefabSpawner::clear(){while(!instances_.empty())despawn(instances_.begin()->first);}
} // namespace engine::net
