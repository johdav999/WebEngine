#include "../examples/multiplayer/gameplay.hpp"
#include "engine/network_scene.hpp"
#include <iostream>
#include <source_location>
using namespace engine;
namespace {
void check(bool b, std::source_location at = std::source_location::current()) {
  if (!b)
    throw std::runtime_error("Gameplay replication assertion at line " +
                             std::to_string(at.line()));
}
template <class F> void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed);
}
// Independent consumer: the engine knows nothing about this inventory class.
class Bag final : public Component {
public:
  std::vector<std::uint32_t> items{5, 7};
};
struct BagUpdate : net::PreparedComponentUpdate {
  Bag &target;
  std::vector<std::uint32_t> items;
  BagUpdate(Bag &t, std::vector<std::uint32_t> i)
      : target(t), items(std::move(i)) {}
  void commit() noexcept override { target.items.swap(items); }
};
net::ReplicatedComponentType bagType() {
  auto encode = [](const Component &c) {
    net::Bytes b;
    for (auto n : static_cast<const Bag &>(c).items)
      multiplayer_gameplay::put(b, n);
    return b;
  };
  auto decode = [](std::span<const std::uint8_t> b) {
    if (b.size() > 32 || b.size() % 4)
      throw std::invalid_argument("Inventory budget");
    std::vector<std::uint32_t> v;
    for (std::size_t i = 0; i < b.size(); i += 4)
      v.push_back(multiplayer_gameplay::get(b, i));
    return v;
  };
  ComponentCodec codec;
  codec.key = "Test.Inventory";
  codec.type = typeid(Bag);
  codec.encode = [encode](const Component &c) {
    auto b = encode(c);
    auto p = std::as_bytes(std::span(b));
    return std::vector<std::byte>(p.begin(), p.end());
  };
  codec.decode =
      [decode](std::uint32_t v,
               std::span<const std::byte> b) -> std::unique_ptr<Component> {
    if (v != 1)
      throw std::invalid_argument("Inventory schema");
    auto c = std::make_unique<Bag>();
    c->items =
        decode({reinterpret_cast<const std::uint8_t *>(b.data()), b.size()});
    return c;
  };
  return {codec,
          1,
          32,
          encode,
          [decode](auto b) { (void)decode(b); },
          [decode](Component &c,
                   auto b) -> std::unique_ptr<net::PreparedComponentUpdate> {
            return std::make_unique<BagUpdate>(dynamic_cast<Bag &>(c),
                                               decode(b));
          },
          [](Component &) {}};
}
void core() {
  auto registry = multiplayer_gameplay::registry();
  registry.add(bagType());
  rejects([&] { registry.add(bagType()); });
  auto bad = bagType();
  bad.archive.key = "Other.Inventory";
  rejects([&] { registry.add(bad); });
  bad = bagType();
  bad.archive.resolve = [](Component &, Scene &) {};
  net::ComponentReplicationRegistry empty;
  rejects([&] { empty.add(bad); });
  bad = bagType();
  bad.maxStateBytes = 257;
  rejects([&] { empty.add(bad); });
  bad = bagType();
  bad.prepare = {};
  rejects([&] { empty.add(bad); });
  Scene scene;
  auto &player = scene.spawn("Player");
  player.setSavedId("component.player");
  player.addComponent<MeshComponent>("mesh", Mesh::cube());
  auto &bag = player.addComponent<Bag>();
  net::setComponentReplication(bag, net::ComponentReplication::Everyone);
  auto &vitals = player.addComponent<multiplayer_gameplay::Vitals>();
  auto &progress = player.addComponent<multiplayer_gameplay::Progress>();
  auto &camera = scene.spawn().addComponent<CameraComponent>();
  RenderView view{&camera};
  auto bindings = net::bindComponents(player, registry);
  check(bindings.size() == 3);
  auto states = net::captureComponents(player, bindings, registry);
  check(states.size() == 3);
  auto bytes = net::encodeComponentStates(states);
  auto decoded = net::decodeComponentStates(bytes);
  check(decoded.size() == 3 && decoded[0].data == states[0].data);
  for (std::size_t i = 0; i < bytes.size(); ++i)
    rejects([&] { net::decodeComponentStates(std::span(bytes).first(i)); });
  auto invalid = states;
  invalid[1].slot = invalid[0].slot;
  rejects([&] { net::encodeComponentStates(invalid); });
  auto malformed = bytes;
  malformed[12] = 2;
  rejects([&] { net::decodeComponentStates(malformed); });
  auto publicState = net::selectComponentStates(states, bindings, false);
  check(publicState.size() == 2);
  net::validateComponentStates(publicState, bindings, registry, false);
  rejects(
      [&] { net::validateComponentStates(states, bindings, registry, false); });
  invalid = states;
  invalid[0].version++;
  rejects(
      [&] { net::validateComponentStates(invalid, bindings, registry, true); });
  invalid = states;
  invalid[1].data[3] = 0x7f;
  invalid[1].data[2] = 0xc0;
  rejects([&] {
    net::prepareComponents(player, invalid, bindings, registry, true);
  });
  check(vitals.data.health == 100 &&
        bag.items == std::vector<std::uint32_t>({5, 7}));
  bag.items = {99};
  vitals.data.health = 23;
  progress.data = {42, "checkpoint"};
  auto updated = net::captureComponents(player, bindings, registry);
  auto clone = SceneArchive::duplicateSubtree(player, {}, registry.codecs());
  auto &other = dynamic_cast<Actor &>(*clone);
  net::prepareComponentReplicas(other, registry);
  check(
      !other.getComponent<multiplayer_gameplay::Vitals>()->fixedTickEnabled());
  other.getComponent<Bag>()->items = {1};
  auto plan = net::prepareComponents(other, updated, bindings, registry, true);
  check(other.getComponent<Bag>()->items == std::vector<std::uint32_t>{1});
  plan->commit();
  check(other.getComponent<Bag>()->items == bag.items);
  updated[0].enabled = false;
  net::prepareComponents(other, updated, bindings, registry, true)->commit();
  check(!other.getComponent<Bag>()->enabled);
  auto package =
      net::saveNetworkScene(scene, {&view, 1}, {}, player.savedId(), registry);
  check(multiplayer_gameplay::get(package, 0) == 0x33534557);
  auto loaded = net::loadNetworkScene(package, registry);
  check(loaded.replicated[0].components.size() == 3);
  rejects([&] { net::loadNetworkScene(package); });
  net::ComponentReplicationRegistry different;
  for (auto t : registry.entries()) {
    if (t.archive.type == typeid(Bag))
      ++t.version;
    different.add(std::move(t));
  }
  rejects([&] { net::loadNetworkScene(package, different); });
  auto broken = package;
  broken[8 + player.savedId().size()] = 0xff;
  rejects([&] { net::loadNetworkScene(broken, registry); });
  auto &prototype = *dynamic_cast<Actor *>(
      loaded.document.scene->findSavedNode(player.savedId()));
  net::TransformReplicas replicas(*loaded.document.scene, prototype,
                                  registry.codecs());
  Transform pose;
  std::vector<net::Record> records{{1, 1, 1, net::encodeTransform(pose)},
                                   {2, 2, 1, net::encodeTransform(pose)}};
  auto stage = [&](Actor &actor, const net::Record &record) {
    if (!actor.scene())
      net::prepareComponentReplicas(actor, registry);
    return net::prepareComponents(actor, states, bindings, registry, true);
  };
  replicas.apply(records, stage);
  check(replicas.find(1)->getComponent<Bag>()->items ==
        std::vector<std::uint32_t>({5, 7}));
  auto handle = replicas.find(1)->id();
  pose.position.x = 9;
  records[0].data = net::encodeTransform(pose);
  rejects([&] {
    replicas.apply(records,
                   [&](Actor &a, const net::Record &r)
                       -> std::unique_ptr<net::PreparedComponentUpdate> {
                     if (r.id == 2)
                       throw std::invalid_argument("late preparation failure");
                     return net::prepareComponents(a, updated, bindings,
                                                   registry, true);
                   });
  });
  check(replicas.find(1)->id() == handle &&
        replicas.find(1)->transform.position.x == 0 &&
        replicas.find(1)->getComponent<Bag>()->items ==
            std::vector<std::uint32_t>({5, 7}));
  replicas.apply({});
  check(!replicas.find(1));
  replicas.apply(records, stage);
  check(replicas.find(1)->id() != handle);
  // Registration-backed editor creation, config, replication flags and history.
  EditorComponentSupport support{
      registry.codecs(),
      {{"Bag", "Inventory", [] { return std::make_unique<Bag>(); }}}};
  EditorSession editor(scene, 128, nullptr,
                       net::componentReplicationProperties(registry), support);
  editor.select(player.savedId());
  check(editor.addComponent("Bag"));
  auto id = editor.selected()->savedId();
  check(editor.setProperty("network.component", 1.0) && editor.undo() &&
        editor.redo());
  check(editor.duplicateSelected());
  check(dynamic_cast<Actor *>(editor.selected()) == nullptr &&
        dynamic_cast<Bag *>(editor.selected()));
  check(editor.undo());
  editor.select(id);
  auto &created = dynamic_cast<Bag &>(*editor.selected());
  created.items.push_back(100);
  check(editor.undo());
  check(!editor.undo()); // Property undo preserves raw state; structural undo
                         // refuses to discard it.
  auto roundtrip = SceneArchive::load(
      SceneArchive::save(scene, {&view, 1}, {}, {}, {}, registry.codecs()), {},
      registry.codecs());
  check(dynamic_cast<Bag *>(roundtrip.scene->findSavedNode(id))->items.back() ==
        100);
  bag.addTag("WebEngine.ComponentReplication.v9:1");
  rejects([&] {
    net::saveNetworkScene(scene, {&view, 1}, {}, player.savedId(), registry);
  });
  bag.removeTag("WebEngine.ComponentReplication.v9:1");
}
void projection() {
  net::ServerSession server(77, 1);
  auto p = server.connect(), q = server.connect();
  net::ClientSession a(77), b(77);
  a.receive(server.receive(p, a.hello()));
  b.receive(server.receive(q, b.hello()));
  std::vector<net::Record> global{{1, p, 1, {1, 2, 99}}, {2, q, 1, {4, 5, 88}}};
  for (int tick = 0; tick < 25; ++tick) {
    server.snapshot(global);
    auto privateA = global, privateB = global;
    privateA[1].data = {4, 5};
    privateB[0].data = {1, 2};
    if (tick == 5)
      server.receive(q, b.requestSnapshot());
    if (tick == 0) {
      auto bad = privateB;
      bad[0].owner = q;
      rejects([&] { server.snapshotForRecords(q, bad); });
    }
    a.receive(server.snapshotForRecords(p, privateA));
    b.receive(server.snapshotForRecords(q, privateB));
    check(a.records()[0].data.size() == 3 && a.records()[1].data.size() == 2 &&
          b.records()[0].data.size() == 2 && b.records()[1].data.size() == 3);
    ++global[0].data[2];
    ++global[1].data[2];
  }
  rejects([&] { server.receive(q, b.command(1, 1, {})); });
}
void topologyAndBudgets() {
  net::ComponentReplicationRegistry registry;
  registry.add(bagType());
  Scene scene;
  auto &actor = scene.spawn();
  actor.setSavedId("budget.player");
  actor.addComponent<MeshComponent>("mesh", Mesh::cube());
  auto &bag = actor.addComponent<Bag>();
  net::setComponentReplication(bag, net::ComponentReplication::Everyone);
  auto bindings = net::bindComponents(actor, registry);
  auto handles = net::bindComponentHandles(actor, bindings, registry);
  check(net::captureComponents(actor, handles, registry).size() == 1);
  actor.removeChild(bag);
  rejects([&] { net::captureComponents(actor, handles, registry); });
  scene.synchronize(); // Flush ordinary deferred removal before replacement.
  auto &replacement = actor.addComponent<Bag>();
  net::setComponentReplication(replacement,
                               net::ComponentReplication::Everyone);
  // An identical class at the same child index cannot reuse a stale handle.
  rejects([&] { net::captureComponents(actor, handles, registry); });
  handles = net::bindComponentHandles(actor, bindings, registry);
  check(net::captureComponents(actor, handles, registry).size() == 1);
  auto &camera = scene.spawn().addComponent<CameraComponent>();
  RenderView view{&camera};
  for (int i = 0; i < 4; ++i) {
    auto &extra = actor.addComponent<Bag>();
    net::setComponentReplication(extra, net::ComponentReplication::Everyone);
  }
  // Actual values fit, but five declared maxima exceed the actor's 256-byte
  // total (pose + ack + component headers). Reject before publishing content.
  rejects([&] {
    net::saveNetworkScene(scene, {&view, 1}, {}, actor.savedId(), registry);
  });
  for (const auto &child : actor.children())
    if (auto *c = dynamic_cast<Bag *>(child.get()))
      net::setComponentReplication(*c, net::ComponentReplication::Off);
  (void)net::saveNetworkScene(scene, {&view, 1}, {}, actor.savedId(), registry);
  for (int i = 0; i < 4; ++i)
    actor.addComponent<Bag>();
  rejects([&] { net::bindComponents(actor, registry); });
}
} // namespace
int main() {
  try {
    core();
    projection();
    topologyAndBudgets();
    std::cout << "Gameplay replication PASS: custom inventory/vitals/progress "
                 "adapters, codecs, editor history, atomic batches, bounds, "
                 "schemas, owner-only deltas/keyframes/resync, lifecycle\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
