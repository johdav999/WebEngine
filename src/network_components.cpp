#include "engine/network_components.hpp"
#include <algorithm>
#include <set>

namespace engine::net {
namespace {
constexpr std::string_view prefix = "WebEngine.ComponentReplication.";
void require(bool b) {
  if (!b)
    throw std::invalid_argument("Invalid gameplay component replication");
}
void put(Bytes &b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(v >> (8 * i)));
}
struct Reader {
  std::span<const std::uint8_t> bytes;
  std::size_t at = 0;
  std::uint32_t get() {
    require(bytes.size() - at >= 4);
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= std::uint32_t(bytes[at++]) << (8 * i);
    return v;
  }
};
Component &componentAt(Actor &actor, const ComponentBinding &binding,
                       const ComponentReplicationRegistry &registry) {
  require(binding.child < actor.children().size());
  auto *c = dynamic_cast<Component *>(actor.children()[binding.child].get());
  auto *type = c ? registry.find(*c) : nullptr;
  require(type && type->archive.key == binding.type &&
          componentReplication(*c) == binding.audience);
  require(!c->isPendingRemoval());
  if (binding.handle)
    require(actor.scene() && actor.scene()->findComponent(binding.handle) == c);
  return *c;
}
struct Batch final : PreparedComponentUpdate {
  struct Item {
    Component *component;
    bool enabled;
    std::unique_ptr<PreparedComponentUpdate> update;
  };
  std::vector<Item> items;
  void commit() noexcept override {
    for (auto &i : items) {
      i.update->commit();
      i.component->setEnabled(i.enabled);
    }
  }
};
} // namespace
ComponentReplication componentReplication(const Component &c) {
  auto mode = ComponentReplication::Off;
  bool seen = false;
  for (const auto &tag : c.tags())
    if (tag.starts_with(prefix)) {
      require(!seen);
      seen = true;
      if (tag == "WebEngine.ComponentReplication.v1:1")
        mode = ComponentReplication::Everyone;
      else if (tag == "WebEngine.ComponentReplication.v1:2")
        mode = ComponentReplication::OwnerOnly;
      else
        require(false);
    }
  return mode;
}
void setComponentReplication(Component &c, ComponentReplication mode) {
  require(mode <= ComponentReplication::OwnerOnly);
  auto next = c.serialize();
  std::erase_if(next.tags, [](const auto &s) { return s.starts_with(prefix); });
  if (mode != ComponentReplication::Off)
    next.tags.push_back("WebEngine.ComponentReplication.v1:" +
                        std::to_string(std::uint32_t(mode)));
  c.deserialize(next);
}
void ComponentReplicationRegistry::add(ReplicatedComponentType type) {
  require(entries_.size() < 32 && type.version && type.maxStateBytes &&
          type.maxStateBytes <= maxPayload && type.capture && type.validate &&
          type.prepare && type.replica && !type.archive.resolve);
  auto checked = codecs();
  checked.add(type.archive);
  entries_.push_back(std::move(type));
}
const ReplicatedComponentType *
ComponentReplicationRegistry::find(std::string_view key) const {
  auto it = std::find_if(entries_.begin(), entries_.end(),
                         [&](const auto &t) { return t.archive.key == key; });
  return it == entries_.end() ? nullptr : &*it;
}
const ReplicatedComponentType *
ComponentReplicationRegistry::find(const Component &c) const {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto &t) {
    return t.archive.type == typeid(c);
  });
  return it == entries_.end() ? nullptr : &*it;
}
ComponentCodecs ComponentReplicationRegistry::codecs() const {
  ComponentCodecs result;
  for (const auto &t : entries_)
    result.add(t.archive);
  return result;
}
Bytes ComponentReplicationRegistry::manifest() const {
  std::vector<const ReplicatedComponentType *> sorted;
  for (const auto &t : entries_)
    sorted.push_back(&t);
  std::sort(sorted.begin(), sorted.end(),
            [](auto *a, auto *b) { return a->archive.key < b->archive.key; });
  Bytes out;
  put(out, std::uint32_t(sorted.size()));
  for (auto *t : sorted) {
    put(out, std::uint32_t(t->archive.key.size()));
    out.insert(out.end(), t->archive.key.begin(), t->archive.key.end());
    put(out, t->archive.version);
    put(out, t->version);
    put(out, std::uint32_t(t->maxStateBytes));
  }
  return out;
}
std::vector<ComponentBinding>
bindComponents(const Actor &actor,
               const ComponentReplicationRegistry &registry) {
  std::vector<ComponentBinding> out;
  for (std::size_t i = 0; i < actor.children().size(); ++i)
    if (auto *c = dynamic_cast<const Component *>(actor.children()[i].get())) {
      auto mode = componentReplication(*c);
      auto *type = registry.find(*c);
      if (!type) {
        require(mode == ComponentReplication::Off);
        continue;
      }
      require(out.size() < maxActorComponents && c->children().empty());
      out.push_back(
          {std::uint32_t(out.size() + 1), type->archive.key, i, mode});
    }
  return out;
}
std::vector<ComponentBinding>
bindComponentHandles(Actor &actor, std::span<const ComponentBinding> bindings,
                     const ComponentReplicationRegistry &registry) {
  require(actor.scene() && bindings.size() <= maxActorComponents);
  std::vector<ComponentBinding> result(bindings.begin(), bindings.end());
  for (auto &binding : result) {
    binding.handle = {};
    auto &component = componentAt(actor, binding, registry);
    require(bool(component.id()));
    binding.handle = component.id();
  }
  return result;
}
std::vector<ComponentState>
captureComponents(const Actor &actor,
                  std::span<const ComponentBinding> bindings,
                  const ComponentReplicationRegistry &registry) {
  require(bindings.size() <= maxActorComponents);
  std::vector<ComponentState> out;
  for (const auto &binding : bindings) {
    auto &c = componentAt(const_cast<Actor &>(actor), binding, registry);
    if (binding.audience == ComponentReplication::Off)
      continue;
    auto *type = registry.find(binding.type);
    auto data = type->capture(c);
    require(data.size() <= type->maxStateBytes);
    type->validate(data);
    out.push_back({binding.slot, type->version, c.enabled, std::move(data)});
  }
  (void)encodeComponentStates(out);
  return out;
}
Bytes encodeComponentStates(std::span<const ComponentState> states) {
  require(states.size() <= maxActorComponents);
  Bytes out;
  put(out, std::uint32_t(states.size()));
  std::uint32_t last = 0;
  for (const auto &s : states) {
    require(s.slot > last && s.slot <= maxActorComponents && s.version &&
            s.data.size() <= maxPayload);
    last = s.slot;
    put(out, s.slot);
    put(out, s.version);
    put(out, s.enabled ? 1 : 0);
    put(out, std::uint32_t(s.data.size()));
    out.insert(out.end(), s.data.begin(), s.data.end());
  }
  require(out.size() <= maxPayload);
  return out;
}
std::vector<ComponentState>
decodeComponentStates(std::span<const std::uint8_t> bytes) {
  require(bytes.size() <= maxPayload);
  Reader r{bytes};
  auto count = r.get();
  require(count <= maxActorComponents);
  std::vector<ComponentState> out;
  for (std::uint32_t i = 0; i < count; ++i) {
    ComponentState s;
    s.slot = r.get();
    s.version = r.get();
    auto enabled = r.get();
    require(enabled <= 1);
    s.enabled = enabled != 0;
    auto n = r.get();
    require(n <= maxPayload && n <= bytes.size() - r.at);
    s.data.assign(bytes.begin() + r.at, bytes.begin() + r.at + n);
    r.at += n;
    out.push_back(std::move(s));
  }
  require(r.at == bytes.size());
  (void)encodeComponentStates(out);
  return out;
}
std::vector<ComponentState>
selectComponentStates(std::span<const ComponentState> states,
                      std::span<const ComponentBinding> bindings, bool owner) {
  (void)encodeComponentStates(states);
  require(bindings.size() <= maxActorComponents);
  std::vector<ComponentState> out;
  for (const auto &state : states) {
    auto it = std::find_if(bindings.begin(), bindings.end(),
                           [&](const auto &b) { return b.slot == state.slot; });
    require(it != bindings.end() && it->audience != ComponentReplication::Off);
    if (owner || it->audience == ComponentReplication::Everyone)
      out.push_back(state);
  }
  return out;
}
void validateComponentStates(std::span<const ComponentState> states,
                             std::span<const ComponentBinding> bindings,
                             const ComponentReplicationRegistry &registry,
                             bool owner) {
  (void)encodeComponentStates(states);
  require(bindings.size() <= maxActorComponents);
  std::size_t index = 0;
  std::uint32_t last = 0;
  for (const auto &b : bindings) {
    require(b.slot > last && b.slot <= maxActorComponents &&
            b.audience <= ComponentReplication::OwnerOnly);
    last = b.slot;
    auto *type = registry.find(b.type);
    require(type);
    if (b.audience == ComponentReplication::Off ||
        (!owner && b.audience == ComponentReplication::OwnerOnly))
      continue;
    require(index < states.size());
    const auto &s = states[index++];
    require(s.slot == b.slot && s.version == type->version &&
            s.data.size() <= type->maxStateBytes);
    type->validate(s.data);
  }
  require(index == states.size());
}
std::unique_ptr<PreparedComponentUpdate>
prepareComponents(Actor &actor, std::span<const ComponentState> states,
                  std::span<const ComponentBinding> bindings,
                  const ComponentReplicationRegistry &registry, bool owner) {
  validateComponentStates(states, bindings, registry, owner);
  auto batch = std::make_unique<Batch>();
  for (const auto &state : states) {
    const auto &b =
        *std::find_if(bindings.begin(), bindings.end(),
                      [&](const auto &b) { return b.slot == state.slot; });
    auto &c = componentAt(actor, b, registry);
    auto update = registry.find(b.type)->prepare(c, state.data);
    require(bool(update));
    batch->items.push_back({&c, state.enabled, std::move(update)});
  }
  return batch;
}
void prepareComponentReplicas(Actor &actor,
                              const ComponentReplicationRegistry &registry) {
  for (const auto &child : actor.children())
    if (auto *c = dynamic_cast<Component *>(child.get()))
      if (auto *type = registry.find(*c)) {
        type->replica(*c);
        c->setTickEnabled(false);
        c->setFixedTickEnabled(false);
      }
}
} // namespace engine::net
