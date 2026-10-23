#pragma once
#include "engine/network.hpp"
#include "engine/scene_archive.hpp"
#include <functional>

namespace engine::net {
inline constexpr std::size_t maxActorComponents = 8;
enum class ComponentReplication : std::uint32_t { Off, Everyone, OwnerOnly };
ComponentReplication componentReplication(const Component &);
void setComponentReplication(Component &, ComponentReplication);
// Plans own all staged allocations. prepare must not mutate live state or
// hierarchy; commit must not throw, call application callbacks or mutate
// topology.
class PreparedComponentUpdate {
public:
  virtual ~PreparedComponentUpdate() = default;
  virtual void commit() noexcept = 0;
};
struct ReplicatedComponentType {
  ComponentCodec archive; // Trusted detached factories; no resolve callbacks.
  std::uint32_t version = 1;
  std::size_t maxStateBytes = 128;
  std::function<Bytes(const Component &)> capture;
  std::function<void(std::span<const std::uint8_t>)> validate;
  std::function<std::unique_ptr<PreparedComponentUpdate>(
      Component &, std::span<const std::uint8_t>)>
      prepare;
  // Called on detached replicas before adoption, including unreplicated fields.
  // Suppress authoritative behavior/subscriptions; the engine also disables
  // ticks.
  std::function<void(Component &)> replica;
};
class ComponentReplicationRegistry {
public:
  void add(ReplicatedComponentType);
  const ReplicatedComponentType *find(std::string_view) const;
  const ReplicatedComponentType *find(const Component &) const;
  const auto &entries() const { return entries_; }
  ComponentCodecs codecs() const;
  Bytes
  manifest() const; // Canonical exact compatibility, never RTTI or callbacks.
private:
  std::vector<ReplicatedComponentType> entries_;
};
struct ComponentBinding {
  std::uint32_t slot = 0;
  std::string type;
  std::size_t child = 0; // Fixed template topology, direct actor child.
  ComponentReplication audience = ComponentReplication::Off;
  ComponentId handle; // Optional runtime binding; never encoded or persisted.
};
struct ComponentState {
  std::uint32_t slot = 0, version = 0;
  bool enabled = true;
  Bytes data;
};
std::vector<ComponentBinding>
bindComponents(const Actor &, const ComponentReplicationRegistry &);
std::vector<ComponentBinding>
bindComponentHandles(Actor &, std::span<const ComponentBinding>,
                     const ComponentReplicationRegistry &);
std::vector<ComponentState>
captureComponents(const Actor &, std::span<const ComponentBinding>,
                  const ComponentReplicationRegistry &);
Bytes encodeComponentStates(std::span<const ComponentState>);
std::vector<ComponentState>
    decodeComponentStates(std::span<const std::uint8_t>);
std::vector<ComponentState>
selectComponentStates(std::span<const ComponentState>,
                      std::span<const ComponentBinding>, bool owner);
void validateComponentStates(std::span<const ComponentState>,
                             std::span<const ComponentBinding>,
                             const ComponentReplicationRegistry &, bool owner);
std::unique_ptr<PreparedComponentUpdate>
prepareComponents(Actor &, std::span<const ComponentState>,
                  std::span<const ComponentBinding>,
                  const ComponentReplicationRegistry &, bool owner);
void prepareComponentReplicas(Actor &, const ComponentReplicationRegistry &);
} // namespace engine::net
