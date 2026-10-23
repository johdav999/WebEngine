#include "engine/network_scene.hpp"
#include "engine/prefab_asset.hpp"
#include "engine/collider_component.hpp"
#include "engine/camera_component.hpp"
#include "engine/mesh_component.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>
namespace engine::net {
namespace {
ArchiveLimits limits() {
  ArchiveLimits l;
  l.maxBytes = maxContent;
  l.maxAllocatedBytes = 32 * 1024 * 1024;
  l.maxNodes = 2048;
  l.maxResources = 256;
  l.maxDepth = 32;
  l.maxElements = 262144;
  return l;
}
void validate(const SceneDocument &d, const std::string &id,
              const ComponentReplicationRegistry &registry, const InvocationRegistry &invocations) {
  const auto *a = dynamic_cast<const Actor *>(d.scene->findSavedNode(id));
  if (!a || a->parent() != d.scene.get() || !a->isActiveInHierarchy() ||
      !a->getComponent<MeshComponent>())
    throw std::invalid_argument(
        "Select an enabled root mesh actor as player template");
  std::function<void(const Node &)> check = [&](const Node &n) {
    const auto &t = typeid(n);
    auto *component = dynamic_cast<const Component *>(&n);
    auto *gameplay = component ? registry.find(*component) : nullptr;
    if (component) {
      auto mode = componentReplication(*component);
      if (!gameplay && mode != ComponentReplication::Off)
        throw std::invalid_argument(
            "Component replication requires a registered adapter");
    }
    if(isInvocationMetadata(n)) {
      const auto &c=static_cast<const Component &>(n);auto permissions=invocationPermissions(c);
      auto *owner=dynamic_cast<const Actor *>(c.parent());
      if(!owner||owner->parent()!=d.scene.get()||!owner->isActiveInHierarchy()||
          (owner->savedId()!=id&&!replicationSettings(*owner))||!c.enabled||c.tickEnabled()||c.fixedTickEnabled()||
          !c.children().empty()||c.transform.matrix().m!=Transform{}.matrix().m)
        throw std::invalid_argument("Gameplay permissions require inert metadata on a replicated root actor");
      std::uint32_t rpcs=0,events=0;for(const auto &type:invocations.entries())
        (type.direction==InvocationDirection::ServerRpc?rpcs:events)|=invocationBit(type.id);
      if((permissions.serverRpcs&~rpcs)||(permissions.clientEvents&~events))throw std::invalid_argument("Unknown invocation permission");
    }
    if (gameplay) {
      auto *owner = dynamic_cast<const Actor *>(n.parent());
      if (!owner || owner->parent() != d.scene.get() ||
          !owner->isActiveInHierarchy() || !n.children().empty() ||
          (owner->savedId() != id && !replicationSettings(*owner)))
        throw std::invalid_argument("Gameplay components require a replicated "
                                    "root actor and fixed childless topology");
      auto initial = gameplay->capture(*component);
      if (initial.size() > gameplay->maxStateBytes)
        throw std::invalid_argument("Gameplay initial state budget");
      gameplay->validate(initial);
    }
    if (t != typeid(Scene) && t != typeid(Node) && t != typeid(Actor) &&
        t != typeid(MeshComponent) && t != typeid(CameraComponent) &&
        t != typeid(LightComponent) && t != typeid(RigidBodyComponent) && t != typeid(ColliderComponent) && t != typeid(PrefabInstanceComponent) && !isReplicationMetadata(n) && !isInvocationMetadata(n) && !gameplay)
      throw std::invalid_argument("Multiplayer scene profile supports static "
                                  "mesh, camera and light components only: " +
                                  n.name);
    if (auto *actor = dynamic_cast<const Actor *>(&n);
        actor && actor->tickEnabled())
      throw std::invalid_argument("Authored ticking actors are unsupported");
    if (isReplicationMetadata(n)) {
      const auto &c = static_cast<const Component &>(n);
      (void)replicationSettings(c);
      auto *owner = dynamic_cast<const Actor *>(c.parent());
      if (!owner || owner->parent() != d.scene.get() ||
          !owner->isActiveInHierarchy() ||
          !owner->getComponent<MeshComponent>() || !c.enabled ||
          c.tickEnabled() || c.fixedTickEnabled() || !c.children().empty() ||
          c.transform.matrix().m != Transform{}.matrix().m)
        throw std::invalid_argument("Replication settings require an enabled "
                                    "root mesh actor and inert metadata");
      owner->visit([](const Node &child, const Mat4 &) {
        if (dynamic_cast<const CameraComponent *>(&child) ||
            dynamic_cast<const LightComponent *>(&child))
          throw std::invalid_argument(
              "Replicated actor templates contain mesh visuals only");
      });
    }
    if(auto *body=dynamic_cast<const RigidBodyComponent *>(&n)){
      auto *owner=body->owner();
      if(!owner||owner->parent()!=d.scene.get()||owner->savedId()==id||owner->getComponents<RigidBodyComponent>().size()!=1||owner->getComponents<ColliderComponent>().size()!=1)
        throw std::invalid_argument("Network bodies require paired root components; player movement remains kinematic");
      const auto scale=owner->worldMatrix();
      for(int axis=0;axis<3;++axis){const auto length=std::sqrt(scale.m[axis*4]*scale.m[axis*4]+scale.m[axis*4+1]*scale.m[axis*4+1]+scale.m[axis*4+2]*scale.m[axis*4+2]);
        if(std::abs(length-1)>1e-5f)throw std::invalid_argument("Network physics requires unit world scale");}
      if(body->settings().motion!=BodyMotion::Static&&!replicationSettings(*owner))throw std::invalid_argument("Moving bodies must be replicated actors or runtime prefabs");
      auto *collider=owner->getComponent<ColliderComponent>();
      if(collider->parent()!=owner||body->parent()!=owner||!body->children().empty()||!collider->children().empty()||
         (collider->shape()->containsMesh()&&body->settings().motion!=BodyMotion::Static))throw std::invalid_argument("Network collider hierarchy/shape");
    }
    if(auto *collider=dynamic_cast<const ColliderComponent *>(&n)){
      if(!collider->owner()||!collider->owner()->getComponent<RigidBodyComponent>())throw std::invalid_argument("Network collider requires a body");
    }
    encodeTransform(n.transform);
    for (const auto &child : n.children())
      check(*child);
  };
  check(*d.scene);
  bool invalid = false;
  std::function<void(const Node &)> playerCheck = [&](const Node &n) {
    if (dynamic_cast<const CameraComponent *>(&n) ||
        dynamic_cast<const LightComponent *>(&n))
      invalid = true;
    for (const auto &child : n.children())
      playerCheck(*child);
  };
  playerCheck(*a);
  if (invalid || d.views.size() != 1 || !d.views[0].camera ||
      !d.views[0].camera->isActiveInHierarchy())
    throw std::invalid_argument("Use one active camera view; player templates "
                                "contain mesh visuals only");
}
void put(Bytes &b, std::uint32_t n) {
  for (int i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(n >> (8 * i)));
}
std::uint32_t get(std::span<const std::uint8_t> b, std::size_t p) {
  if (p + 4 > b.size())
    throw std::invalid_argument("Scene packet truncated");
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i)
    v |= std::uint32_t(b[p + i]) << (8 * i);
  return v;
}
} // namespace
NetworkScene loadNetworkScene(std::span<const std::uint8_t> bytes,
                              const ComponentReplicationRegistry &registry, const InvocationRegistry &invocations) {
  const auto magic = get(bytes, 0);
  if (bytes.size() > maxContent || (magic != 0x32534557 && magic != 0x33534557 && magic != 0x34534557))
    throw std::invalid_argument("Network scene profile/version");
  auto n = get(bytes, 4);
  if (!n || n > 128 || 8 + n >= bytes.size())
    throw std::invalid_argument("Player identity length");
  NetworkScene result;
  result.playerTemplate = std::string(bytes.begin() + 8, bytes.begin() + 8 + n);
  if (!Node::validSavedId(result.playerTemplate))
    throw std::invalid_argument("Player saved identity");
  std::size_t offset = 8 + n;
  if (magic == 0x33534557 || magic == 0x34534557) {
    auto size = get(bytes, offset);
    offset += 4;
    if (size > 8192 || size > bytes.size() - offset)
      throw std::invalid_argument("Component manifest budget");
    auto expected = registry.manifest();
    if (size != expected.size() ||
        !std::equal(expected.begin(), expected.end(), bytes.begin() + offset))
      throw std::invalid_argument("Gameplay component schema/content mismatch");
    offset += size;
  }
  if(magic==0x34534557){
    auto size=get(bytes,offset);offset+=4;
    if(size>8192||size>bytes.size()-offset)throw std::invalid_argument("Invocation manifest budget");
    auto expected=invocations.manifest();
    if(size!=expected.size()||!std::equal(expected.begin(),expected.end(),bytes.begin()+offset))throw std::invalid_argument("Invocation schema mismatch");
    offset+=size;
  }
  auto archive = bytes.subspan(offset);
  result.components = registry;
  result.invocations = invocations;
  result.document =
      SceneArchive::load(std::as_bytes(archive), limits(), registry.codecs());
  validate(result.document, result.playerTemplate, registry, invocations);
  std::vector<const Actor *> servers, prefabs;
  for (const auto &child : result.document.scene->children())
    if (auto *actor = dynamic_cast<const Actor *>(child.get())) {
      auto settings = replicationSettings(*actor);
      if (actor->savedId() == result.playerTemplate) {
        auto chosen = settings.value_or(ReplicationSettings{});
        if (chosen.ownership != ReplicaOwnership::Player)
          throw std::invalid_argument(
              "Selected template must use Player ownership");
        result.replicated.push_back({actor->savedId(), chosen, 1, 0});
      } else if(settings && settings->ownership==ReplicaOwnership::Prefab){
        prefabs.push_back(actor);
      } else if (settings) {
        if (settings->ownership != ReplicaOwnership::Server)
          throw std::invalid_argument(
              "Only the selected actor may use Player ownership");
        servers.push_back(actor);
      }
    }
  if (servers.size() > maxEntities - 16)
    throw std::invalid_argument("Replicated scene entity budget");
  std::sort(servers.begin(), servers.end(),
            [](auto *a, auto *b) { return a->savedId() < b->savedId(); });
  for (std::size_t i = 0; i < servers.size(); ++i)
    result.replicated.push_back(
        {servers[i]->savedId(), *replicationSettings(*servers[i]),
         std::uint32_t(i + 2), serverEntityBase + std::uint32_t(i)});
  if(prefabs.size()>32)throw std::invalid_argument("Runtime prefab catalogue budget");
  std::sort(prefabs.begin(),prefabs.end(),[](auto *a,auto *b){return a->savedId()<b->savedId();});
  for(std::size_t i=0;i<prefabs.size();++i)result.replicated.push_back({prefabs[i]->savedId(),*replicationSettings(*prefabs[i]),std::uint32_t(1024+i),0});
  for (auto &definition : result.replicated) {
    auto &actor = *dynamic_cast<Actor *>(
        result.document.scene->findSavedNode(definition.savedId));
    definition.components = bindComponents(actor, registry);
    definition.invocations = invocationPermissions(actor);
    if(magic!=0x34534557 && (definition.settings.ownership==ReplicaOwnership::Prefab||definition.invocations!=InvocationPermissions{}))
      throw std::invalid_argument("Runtime prefabs/permissions require WES4");
    if(auto *body=actor.getComponent<RigidBodyComponent>())definition.physicsMotion=body->settings().motion;
    std::size_t maximum = definition.physicsMotion?72:44;
    for (const auto &b : definition.components)
      if (b.audience != ComponentReplication::Off)
        maximum += 16 + registry.find(b.type)->maxStateBytes;
    if (maximum > maxPayload)
      throw std::invalid_argument(
          "Combined actor gameplay state budget exceeds 256 bytes");
    if (magic == 0x32534557 && !definition.components.empty())
      throw std::invalid_argument(
          "Gameplay components require a versioned WES3 manifest");
  }
  return result;
}
Bytes saveNetworkScene(const Scene &scene, std::span<const RenderView> views,
                       const AssetLibrary &assets, const std::string &player,
                       const ComponentReplicationRegistry &registry, const InvocationRegistry &invocations) {
  if (!Node::validSavedId(player))
    throw std::invalid_argument("Select a player template actor");
  auto archive =
      SceneArchive::save(scene, views, assets, {}, limits(), registry.codecs());
  Bytes result;
  bool gameplay=!invocations.entries().empty();
  scene.visit([&](const Node &node,const Mat4 &){if(isInvocationMetadata(node))gameplay=true;
    if(auto *actor=dynamic_cast<const Actor *>(&node))if(auto settings=replicationSettings(*actor);settings&&settings->ownership==ReplicaOwnership::Prefab)gameplay=true;});
  put(result, gameplay?0x34534557:(registry.entries().empty() ? 0x32534557 : 0x33534557));
  put(result, std::uint32_t(player.size()));
  result.insert(result.end(), player.begin(), player.end());
  if (gameplay || !registry.entries().empty()) {
    auto manifest = registry.manifest();
    put(result, std::uint32_t(manifest.size()));
    result.insert(result.end(), manifest.begin(), manifest.end());
  }
  if(gameplay){auto manifest=invocations.manifest();put(result,std::uint32_t(manifest.size()));result.insert(result.end(),manifest.begin(),manifest.end());}
  for (auto b : archive)
    result.push_back(std::to_integer<std::uint8_t>(b));
  (void)loadNetworkScene(result, registry, invocations);
  return result;
}
Bytes encodeTransform(const Transform &t) {
  const float v[] = {t.position.x, t.position.y, t.position.z,
                     t.rotation.x, t.rotation.y, t.rotation.z,
                     t.scale.x,    t.scale.y,    t.scale.z};
  Bytes b;
  for (auto f : v) {
    if (!std::isfinite(f) || std::abs(f) > 1000000)
      throw std::invalid_argument("Transform range");
    put(b, std::bit_cast<std::uint32_t>(f));
  }
  // Match Node::setLocalTransform before a complete snapshot is published.
  // Finite values alone do not exclude singular/degenerate scale.
  Node validation;
  validation.setLocalTransform(t);
  return b;
}
Transform decodeTransform(std::span<const std::uint8_t> b) {
  if (b.size() != 36)
    throw std::invalid_argument("Transform payload size");
  float v[9];
  for (int i = 0; i < 9; ++i)
    v[i] = std::bit_cast<float>(get(b, i * 4));
  Transform t;
  t.position = {v[0], v[1], v[2]};
  t.rotation = {v[3], v[4], v[5]};
  t.scale = {v[6], v[7], v[8]};
  encodeTransform(t);
  return t;
}
TransformReplicas::TransformReplicas(Scene &s, const Actor &a,
                                     ComponentCodecs codecs)
    : TransformReplicas(s, {{1, &a}}, std::move(codecs)) {}
TransformReplicas::TransformReplicas(
    Scene &s, const std::map<std::uint32_t, const Actor *> &templates,
    ComponentCodecs codecs)
    : scene_(s), codecs_(std::move(codecs)) {
  if (templates.empty() || templates.size() > maxEntities)
    throw std::invalid_argument("Replica template budget");
  for (auto [kind, actor] : templates) {
    if (!kind || !actor || actor->scene() != &s)
      throw std::invalid_argument("Replica template identity");
    templates_.emplace(kind,
                       SceneArchive::duplicateSubtree(*actor, {}, codecs_));
  }
}
Actor *TransformReplicas::find(EntityId id) const {
  auto i = actors_.find(id);
  return i == actors_.end() ? nullptr : scene_.findActor(i->second);
}
void TransformReplicas::apply(std::span<const Record> records,
                              const Stage &stage) {
  if (records.size() > maxEntities)
    throw std::invalid_argument("Replica limit");
  std::set<EntityId> ids;
  std::vector<Transform> poses;
  std::map<EntityId, std::unique_ptr<Node>> pending;
  for (const auto &r : records) {
    if (!r.id || !ids.insert(r.id).second || !templates_.contains(r.kind) ||
        (kinds_.contains(r.id) && kinds_.at(r.id) != r.kind))
      throw std::invalid_argument("Replica identity/kind");
    if (stage && (r.data.size() < 36 || r.data.size() > maxPayload))
      throw std::invalid_argument("Replica payload budget");
    poses.push_back(decodeTransform(stage ? std::span(r.data).first(36)
                                          : std::span(r.data)));
    if (!find(r.id))
      pending.emplace(r.id, SceneArchive::duplicateSubtree(
                                *templates_.at(r.kind), {}, codecs_));
  }
  std::vector<std::unique_ptr<PreparedComponentUpdate>> updates;
  if (stage)
    for (const auto &r : records) {
      auto *actor = find(r.id);
      if (!actor)
        actor = dynamic_cast<Actor *>(pending.at(r.id).get());
      auto update = stage(*actor, r);
      if (!update)
        throw std::invalid_argument("Missing staged replica update");
      updates.push_back(std::move(update));
    }
  for (auto &[id, node] : pending) {
    auto *a = dynamic_cast<Actor *>(node.get());
    scene_.adoptChild(std::move(node));
    actors_[id] = a->id();
  }
  for (std::size_t i = 0; i < records.size(); ++i) {
    find(records[i].id)->setLocalTransform(poses[i]);
    kinds_[records[i].id] = records[i].kind;
  }
  for (auto &update : updates)
    update->commit();
  for (auto it = actors_.begin(); it != actors_.end();)
    if (!ids.contains(it->first)) {
      if (auto *a = scene_.findActor(it->second))
        a->destroy();
      kinds_.erase(it->first);
      it = actors_.erase(it);
    } else
      ++it;
}
} // namespace engine::net
