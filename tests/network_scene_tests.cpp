#include "../examples/multiplayer/scene_shared.hpp"
#include "engine/camera_component.hpp"
#include "engine/editor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/network_scene.hpp"
#include "engine/rigid_body_component.hpp"
#include <iostream>
#include <stdexcept>
#ifndef __EMSCRIPTEN__
#include <fstream>
#endif
using namespace engine;
void require(bool b) {
  if (!b)
    throw std::runtime_error("Network scene assertion");
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  require(caught);
}
void authoring() {
  Scene scene;
  auto &player = scene.spawn("Player");
  player.setSavedId("player");
  player.addComponent<MeshComponent>("mesh", Mesh::cube());
  auto &server = scene.spawn("Server");
  server.setSavedId("server");
  server.addComponent<MeshComponent>("mesh", Mesh::sphere());
  auto &camera = scene.spawn("Camera").addComponent<CameraComponent>();
  RenderView view{&camera};
  EditorSession editor(scene);
  editor.select(player.savedId());
  require(editor.addComponent("NetworkReplication"));
  auto id = editor.selected()->savedId();
  require(editor.setProperty("network.relevance", 2.0));
  require(net::replicationSettings(player)->relevance ==
          net::ReplicaRelevance::OwnerOnly);
  require(editor.undo() && net::replicationSettings(player)->relevance ==
                               net::ReplicaRelevance::Distance);
  require(editor.redo() && net::replicationSettings(player)->relevance ==
                               net::ReplicaRelevance::OwnerOnly);
  const auto old = SceneArchive::save(scene, {&view, 1});
  const auto revision = editor.revision();
  rejects([&] { editor.setProperty("network.ownership", 1.0); });
  rejects([&] { editor.setProperty("network.enter", 6.0); });
  require(SceneArchive::save(scene, {&view, 1}) == old &&
          editor.revision() == revision);
  require(editor.setProperty("network.relevance", 0.0));
  require(editor.setProperty("network.exit", 7.0));
  require(editor.setProperty("network.enter", 6.0));
  auto roundtrip = SceneArchive::load(SceneArchive::save(scene, {&view, 1}));
  auto *loaded =
      dynamic_cast<Actor *>(roundtrip.scene->findSavedNode("player"));
  require(net::replicationSettings(*loaded) ==
          net::replicationSettings(player));
  auto copy = SceneArchive::duplicateSubtree(player);
  require(net::replicationSettings(static_cast<const Actor &>(*copy)) ==
          net::replicationSettings(player));
  require(editor.removeSelectedComponents());
  require(!net::replicationSettings(player));
  require(editor.undo() && editor.select(id));
  editor.select(server.savedId());
  require(editor.addComponent("NetworkReplication"));
  require(editor.setProperty("network.ownership", 1.0));
  require(editor.setProperty("network.relevance", 1.0));
  auto bytes = net::saveNetworkScene(scene, {&view, 1}, {}, player.savedId());
  auto network = net::loadNetworkScene(bytes);
  require(network.replicated.size() == 2);
  require(network.replicated[1].entity == net::serverEntityBase &&
          network.replicated[1].kind == 2);
  rejects(
      [&] { net::saveNetworkScene(scene, {&view, 1}, {}, server.savedId()); });
  auto &metadata = static_cast<Component &>(*editor.selected());
  auto settings = net::replicationSettings(metadata);
  auto bad = settings;
  bad.exitRadius = 0;
  rejects([&] { net::setReplicationSettings(metadata, bad); });
  require(net::replicationSettings(metadata) == settings);
  metadata.addTag("WebEngine.Replication.v2:0:0:0:0");
  rejects(
      [&] { net::saveNetworkScene(scene, {&view, 1}, {}, player.savedId()); });
  metadata.removeTag("WebEngine.Replication.v2:0:0:0:0");
  server.adoptChild(net::makeReplicationMetadata(settings));
  rejects(
      [&] { net::saveNetworkScene(scene, {&view, 1}, {}, player.savedId()); });
  server.removeChild(*server.children().back());
  scene.flushPendingDestroy();
  auto distance = net::ReplicationSettings{};
  require(net::relevantTo(distance, false, false, 4) &&
          !net::relevantTo(distance, false, false, 4.1f));
  require(net::relevantTo(distance, false, true, 5) &&
          !net::relevantTo(distance, false, true, 5.1f));
  require(net::relevantTo(distance, true, false, 100));
  distance.relevance = net::ReplicaRelevance::Always;
  require(net::relevantTo(distance, false, false, 100));
  distance.relevance = net::ReplicaRelevance::OwnerOnly;
  require(!net::relevantTo(distance, false, true, 0));
  rejects([&] { net::relevantTo(distance, false, false, -1); });
  net::TransformReplicas replicas(scene, {{1, &player}, {2, &server}});
  std::vector<net::Record> records{
      {1, 1, 1, net::encodeTransform(player.transform)},
      {net::serverEntityBase, 0, 2, net::encodeTransform(server.transform)}};
  replicas.apply(records);
  require(replicas.find(1)->getComponent<MeshComponent>()->mesh ==
          player.getComponent<MeshComponent>()->mesh);
  require(replicas.find(net::serverEntityBase)
              ->getComponent<MeshComponent>()
              ->mesh == server.getComponent<MeshComponent>()->mesh);
  auto handle = replicas.find(net::serverEntityBase)->id();
  records[1].kind = 1;
  rejects([&] { replicas.apply(records); });
  require(replicas.find(net::serverEntityBase)->id() == handle);
  records[1].kind = 3;
  rejects([&] { replicas.apply(records); });
  records[1].kind = 2;
  replicas.apply(std::span(records).first(1));
  require(!replicas.find(net::serverEntityBase));
  replicas.apply(records);
  require(replicas.find(net::serverEntityBase)->id() != handle);
  // Legacy WES2 without annotations still resolves the selected player
  // template.
  Scene legacy;
  auto &a = legacy.spawn();
  a.addComponent<MeshComponent>("mesh", Mesh::cube());
  auto &c = legacy.spawn().addComponent<CameraComponent>();
  RenderView v{&c};
  require(net::loadNetworkScene(
              net::saveNetworkScene(legacy, {&v, 1}, {}, a.savedId()))
              .replicated.size() == 1);
}
int main(int argc, char **argv) {
  try {
    authoring();
    {
      net::TransformTimeline timeline;
      Transform a, b;
      b.position.x = 1;
      require(!timeline.sample(0));
      require(timeline.push(0, a));
      require(!timeline.push(.1, b));
      auto mid = timeline.sample(.15);
      require(mid->interpolated &&
              std::abs(mid->transform.position.x - .5f) < .0001f);
      require(timeline.sample(0)->transform.position.x == 0 &&
              timeline.sample(2)->transform.position.x == 1);
      rejects([&] { timeline.push(.05, a); });
      require(timeline.size() == 2);
      auto bad = b;
      bad.scale.x = 0;
      rejects([&] { timeline.push(.2, bad); });
      require(timeline.size() == 2);
      b.position.x = 2;
      timeline.push(.1, b);
      require(timeline.size() == 2 &&
              timeline.sample(2)->transform.position.x == 2);
      b.position.x = 9;
      require(timeline.push(.2, b) && timeline.size() == 1);
      require(timeline.push(1, b) && timeline.size() == 1);
      b.scale.x = -1;
      require(timeline.push(1.1, b) && timeline.size() == 1);
      timeline.reset();
      a.rotation.y = 3.13f;
      b = a;
      b.rotation.y = -3.13f;
      timeline.push(0, a);
      timeline.push(.1, b);
      require(std::abs(std::abs(timeline.sample(.15)->transform.rotation.y) -
                       pi) < .001f);
      for (int i = 2; i < 100; ++i)
        timeline.push(i * .1, b);
      require(timeline.size() == net::TransformTimeline::capacity);
      timeline.reset();
      require(!timeline.sample(0));
      rejects([&] { net::TransformTimeline invalid(-1); });
      net::CorrectionSmoother smoothing;
      Transform predicted;
      predicted.position.x = 1;
      smoothing.rebase({0, 0, 0}, {1, 0, 0});
      require(smoothing.present(predicted, 0).position.x == 0);
      require(std::abs(smoothing.present(predicted, .08).position.x - .5f) <
              .0001f);
      rejects([&] { smoothing.present(predicted, -1); });
      require(std::abs(smoothing.offset().x + .5f) < .0001f);
      require(std::abs(smoothing.present(predicted, .08).position.x - .75f) <
              .0001f);
      smoothing.rebase({1, 0, 0}, {.5f, 0, 0});
      predicted.position.x = .5f;
      require(std::abs(smoothing.present(predicted, 0).position.x - .75f) <
              .0001f);
      require(smoothing.present(predicted, 1).position.x == .5f);
      smoothing.rebase({0, 0, 0}, {10, 0, 0});
      require(smoothing.offset().x == 0);
      net::CorrectionSmoother once, twice;
      once.rebase({0, 0, 0}, {1, 0, 0});
      twice.rebase({0, 0, 0}, {1, 0, 0});
      once.present(predicted, .08);
      twice.present(predicted, .04);
      twice.present(predicted, .04);
      require(std::abs(once.offset().x - twice.offset().x) < .00001f);
    }
    {
      net::PredictionHistory h;
      auto step = [](const net::Bytes &state, const net::Bytes &input) {
        return net::Bytes{std::uint8_t(state.at(0) + input.at(0))};
      };
      h.reconcile(0, {0}, step);
      h.push(1, {2}, step);
      h.push(2, {3}, step);
      require(h.state()[0] == 5);
      h.reconcile(1, {1}, step);
      require(h.state()[0] == 4 && h.pending() == 1 && h.acknowledged() == 1);
      rejects([&] { h.reconcile(3, {9}, step); });
      require(h.state()[0] == 4 && h.pending() == 1);
      rejects([&] { h.push(2, {1}, step); });
      rejects([&] {
        h.reconcile(1, {9}, [](const auto &, const auto &) -> net::Bytes {
          throw std::runtime_error("replay failed");
        });
      });
      require(h.state()[0] == 4);
      h.reconcile(2, {5}, step);
      require(h.state()[0] == 5 && !h.pending());
      for (unsigned i = 3; i < 131; ++i)
        h.push(i, {0}, step);
      require(h.full());
      rejects([&] { h.push(131, {0}, step); });
      h.reset();
      require(!h.ready() && !h.pending());
      Scene simulation;
      auto &source = simulation.spawn("Template");
      source.addComponent<MeshComponent>("Mesh", Mesh::cube());
      multiplayer_scene::Arena arena(simulation, source.savedId());
      net::PeerId peer = 1;
      arena.sync({&peer, 1});
      for (unsigned i = 1; i <= 10; ++i)
        arena.command(
            {peer, peer, i, 1, multiplayer_example::movement(1000, 0)});
      require(multiplayer_scene::pose(arena.records()[0].data).position.x == 0);
      for (int i = 0; i < 10; ++i)
        simulation.fixedUpdate(1. / 60);
      auto record = arena.records()[0];
      require(std::abs(multiplayer_scene::pose(record.data).position.x - .5f) <
                  .0001f &&
              multiplayer_scene::acknowledgement(record.data) == 10);
    }
    struct Sink final : net::Connection {
      unsigned sent = 0;
      net::ConnectionState state() const override {
        return net::ConnectionState::Open;
      }
      std::string error() const override { return {}; }
      bool send(std::span<const std::uint8_t>) override {
        ++sent;
        return true;
      }
      std::vector<net::Bytes> receive() override { return {}; }
    };
    multiplayer_scene::Client paced;
    auto sink = std::make_unique<Sink>();
    auto *counter = sink.get();
    paced.connection = std::move(sink);
    paced.publication.resize(net::maxContent, 42);
    for (int i = 0; i < 1000; ++i)
      paced.update(i / 1000.);
    require(counter->sent >= 50 && counter->sent <= 60 && !paced.requested);
    Scene s("Authored courtyard");
    s.setSavedId("multiplayer.courtyard");
    auto &a = s.spawn("Player template");
    a.setSavedId("player.template");
    a.transform.position = {2, .8f, 1};
    auto &playerMesh = a.addComponent<MeshComponent>("Player", Mesh::sphere());
    auto material = std::make_shared<Material>();
    material->shader = MaterialShader::Unlit;
    material->baseColor = {.85f, .15f, .5f};
    playerMesh.setMaterial(0, MaterialInstance{material});
    a.transform.scale = {.7f, 1.2f, .7f};
    auto &floor = s.spawn("Authored blue platform")
                      .addComponent<MeshComponent>("Platform", Mesh::cube());
    floor.transform.position = {0, -.5f, 0};
    floor.transform.scale = {12, .25f, 12};
    auto ground = std::make_shared<Material>();
    ground->shader = MaterialShader::Unlit;
    ground->baseColor = {.12f, .25f, .4f};
    floor.setMaterial(0, MaterialInstance{ground});
    auto &tower = s.spawn("Authored orange landmark")
                      .addComponent<MeshComponent>("Landmark", Mesh::cube());
    tower.transform.position = {-3, 1, 0};
    tower.transform.scale = {1, 2, 1};
    auto orange = std::make_shared<Material>();
    orange->shader = MaterialShader::Unlit;
    orange->baseColor = {1, .5f, .1f};
    tower.setMaterial(0, MaterialInstance{orange});
    tower.owner()->setSavedId("server.landmark");
    auto serverSettings = net::ReplicationSettings{};
    serverSettings.ownership = net::ReplicaOwnership::Server;
    serverSettings.relevance = net::ReplicaRelevance::Always;
    tower.owner()->adoptChild(net::makeReplicationMetadata(serverSettings));
    auto &c = s.spawn("Camera").addComponent<CameraComponent>();
    c.transform.position = {0, 10, 12};
    c.lookAt({0, 0, 0});
    RenderView view{&c};
    playerMesh.color = {1, 1, 1};
    floor.color = {1, 1, 1};
    tower.color = {1, 1, 1};
    auto original = SceneArchive::save(s, {&view, 1});
    auto bytes = net::saveNetworkScene(s, {&view, 1}, {}, a.savedId());
    require(original == SceneArchive::save(s, {&view, 1}));
#ifndef __EMSCRIPTEN__
    if (argc == 2) {
      auto dir = std::filesystem::u8path(argv[1]);
      std::filesystem::create_directories(dir);
      SceneArchive::saveFile(dir / "courtyard.wescene", s, {&view, 1});
      std::ofstream out(dir / "courtyard.weonline", std::ios::binary);
      out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      auto gameplay=SceneArchive::load(original);
      auto* landmark=dynamic_cast<Actor*>(gameplay.scene->findSavedNode("server.landmark"));
      landmark->addComponent<multiplayer_gameplay::Vitals>();
      auto &token=gameplay.scene->spawn("Runtime token prefab");token.setSavedId("prefab.token");
      token.addComponent<MeshComponent>("Token",Mesh::cube()).color={.2f,1,.4f};
      token.getComponent<MeshComponent>()->transform.scale={.5f,.5f,.5f};
      token.transform.position={0,3,0};
      token.addComponent<RigidBodyComponent>("Authoritative body");
      token.addComponent<ColliderComponent>("Authoritative collider",CollisionShape::box({.25f,.25f,.25f}));
      auto *floorActor=dynamic_cast<Actor *>(gameplay.scene->findSavedNode(floor.owner()->savedId()));
      RigidBodySettings groundSettings;groundSettings.motion=BodyMotion::Static;
      floorActor->addComponent<RigidBodyComponent>("Static ground body",groundSettings);
      floorActor->addComponent<ColliderComponent>("Static ground collider",CollisionShape::box({6,.125f,6})).transform.position={0,-.5f,0};
      token.addComponent<multiplayer_gameplay::Vitals>().data={77,2,false};
      auto prefabSettings=net::ReplicationSettings{};prefabSettings.ownership=net::ReplicaOwnership::Prefab;
      token.adoptChild(net::makeReplicationMetadata(prefabSettings));
      SceneArchive::saveFile(dir/"gameplay.wescene",*gameplay.scene,gameplay.views,gameplay.assets,{},{},multiplayer_gameplay::registry().codecs());
      auto online=net::saveNetworkScene(*gameplay.scene,gameplay.views,gameplay.assets,"player.template",multiplayer_gameplay::registry(),multiplayer_gameplay::invocations());
      std::ofstream game(dir/"gameplay.weonline",std::ios::binary);game.write(reinterpret_cast<const char*>(online.data()),online.size());
      auto *lobbyPlayer=dynamic_cast<Actor *>(gameplay.scene->findSavedNode("player.template"));
      lobbyPlayer->addComponent<multiplayer_gameplay::Vitals>().data={91,5,false};lobbyPlayer->addComponent<multiplayer_gameplay::Progress>().data.stage="ready";
      lobbyPlayer->adoptChild(net::makeInvocationMetadata(multiplayer_gameplay::allInvocations()));
      if(!net::replicationSettings(*lobbyPlayer))lobbyPlayer->adoptChild(net::makeReplicationMetadata({net::ReplicaOwnership::Player,net::ReplicaRelevance::Distance,6,7}));
      SceneArchive::saveFile(dir/"lobby.wescene",*gameplay.scene,gameplay.views,gameplay.assets,{},{},multiplayer_gameplay::registry().codecs());
      auto lobby=net::saveNetworkScene(*gameplay.scene,gameplay.views,gameplay.assets,"player.template",multiplayer_gameplay::registry(),multiplayer_gameplay::invocations());
      std::ofstream lobbyFile(dir/"lobby.weonline",std::ios::binary);lobbyFile.write(reinterpret_cast<const char*>(lobby.data()),lobby.size());
    }
#endif
    net::ContentReceiver transfer;
    for (std::size_t offset = 0; offset < bytes.size();
         offset += net::contentChunkSize)
      transfer.receive(net::contentChunk(bytes, offset));
    require(transfer.complete() && transfer.bytes() == bytes);
    rejects([&] { transfer.receive(net::contentChunk(bytes, 0)); });
    require(transfer.bytes() == bytes);
    net::Bytes large(65537, 42);
    net::ContentReceiver ordered;
    ordered.receive(net::contentChunk(large, 0));
    rejects([&] { ordered.receive(net::contentChunk(large, 32768)); });
    require(ordered.bytes().size() == 16384);
    for (std::size_t p = 16384; p < large.size(); p += 16384)
      ordered.receive(net::contentChunk(large, p));
    require(ordered.bytes() == large);
    auto loaded = net::loadNetworkScene(bytes);
    require(loaded.playerTemplate == a.savedId() &&
            loaded.document.scene->savedId() == s.savedId());
    auto *templateActor = dynamic_cast<Actor *>(
        loaded.document.scene->findSavedNode(a.savedId()));
    net::TransformReplicas replicas(*loaded.document.scene, *templateActor);
    Transform t = a.transform;
    t.position.x = 4;
    std::vector<net::Record> records{{1, 1, 1, net::encodeTransform(t)}};
    replicas.apply(records);
    require(replicas.find(1)->transform.position.x == 4);
    auto id = replicas.find(1)->id();
    auto corrupt = records;
    corrupt.push_back({2, 2, 1, {}});
    rejects([&] { replicas.apply(corrupt); });
    require(replicas.find(1)->id() == id && !replicas.find(2));
    auto changed = t;
    changed.position.x = 6;
    corrupt = {{1, 1, 1, net::encodeTransform(changed)},
               {2, 2, 1, net::encodeTransform(t)}};
    for (int i = 24; i < 28; ++i)
      corrupt[1].data[i] = 0;
    rejects([&] { replicas.apply(corrupt); });
    require(replicas.find(1)->transform.position.x == 4 && !replicas.find(2));
    corrupt = records;
    corrupt[0].data[3] = 0x7f;
    corrupt[0].data[2] = 0xc0;
    rejects([&] { replicas.apply(corrupt); });
    require(replicas.find(1)->transform.position.x == 4);
    replicas.apply({});
    require(!replicas.find(1));
    loaded.document.scene->flushPendingDestroy();
    replicas.apply(records);
    require(replicas.find(1)->id() != id);
    rejects([&] { net::saveNetworkScene(s, {&view, 1}, {}, "missing"); });
    auto &hidden = s.spawn("Disabled unsupported physics");
    hidden.enabled = false;
    hidden.addComponent<RigidBodyComponent>();
    rejects([&] { net::saveNetworkScene(s, {&view, 1}, {}, a.savedId()); });
    hidden.destroy();
    s.flushPendingDestroy();
    a.addComponent<RigidBodyComponent>();
    rejects([&] { net::saveNetworkScene(s, {&view, 1}, {}, a.savedId()); });
    auto bad = bytes;
    bad.back() ^= 255;
    rejects([&] { net::loadNetworkScene(bad); });
    std::cout << "Network scene package, ordered transfer, atomic validation, "
                 "replica lifecycle PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
