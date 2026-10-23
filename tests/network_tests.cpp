#include "engine/network.hpp"
#include <iostream>
#include <stdexcept>
using namespace engine::net;
void check(bool b) {
  if (!b)
    throw std::runtime_error("network assertion failed");
}
template <class F> void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  check(failed);
}
void deltaReplication() {
  ServerSession server(55, 101);
  auto p = server.connect(), q = server.connect();
  ClientSession a(55), b(55);
  a.receive(server.receive(p, a.hello()));
  b.receive(server.receive(q, b.hello()));
  std::vector<Record> world{{1, p, 1, Bytes(40, 0)},
                            {2, q, 1, Bytes(40, 1)},
                            {3, 0, 1, Bytes(40, 2)}};
  std::vector<EntityId> both{1, 2, 3}, none;
  (void)server.snapshot(world);
  a.receive(server.snapshotFor(p, both));
  b.receive(server.snapshotFor(q, none));
  check(a.records() == world && b.records() == std::vector<Record>{world[1]});
  rejects([&] { server.snapshotFor(p, both); });
  world[0].data[39] = 17;
  world[2].data[0] = 4;
  auto full = server.snapshot(world);
  auto delta = server.snapshotFor(p, both);
  check(decode(delta).type == Type::Delta && delta.size() < full.size());
  check(decode(delta).patches.size() == 2);
  const auto before = a.records();
  const auto sequence = a.sequence();
  for (std::size_t n = 0; n < delta.size(); ++n)
    rejects([&] { a.receive(std::span(delta).first(n)); });
  check(a.records() == before && a.sequence() == sequence);
  auto invalid = decode(delta);
  invalid.patches[0].changes[0].offset = 40;
  rejects([&] { a.receive(encode(invalid)); });
  invalid = decode(delta);
  invalid.patches[0].id = 999;
  rejects([&] { a.receive(encode(invalid)); });
  invalid = decode(delta);
  invalid.removed = {999};
  rejects([&] { a.receive(encode(invalid)); });
  invalid = decode(delta);
  invalid.removed = {invalid.patches[0].id};
  rejects([&] { encode(invalid); });
  invalid = decode(delta);
  invalid.patches[0].changes.push_back(invalid.patches[0].changes.front());
  rejects([&] { encode(invalid); });
  check(a.records() == before && a.sequence() == sequence);
  a.receive(delta);
  check(a.records() == world);
  b.receive(server.snapshotFor(q, none));
  check(b.records().size() == 1);
  // Resync is control traffic: no gameplay command sequence is consumed.
  auto command = a.command(1, 1, {});
  server.receive(p, command);
  server.receive(p, a.requestSnapshot());
  server.receive(p, a.requestSnapshot());
  check(decode(a.command(1, 1, {})).sequence == 2);
  (void)server.snapshot(world);
  a.receive(server.snapshotFor(p, none));
  check(a.records() == std::vector<Record>{world[0]});
  // A filtered peer cannot target someone else's hidden record.
  rejects([&] { server.receive(q, b.command(1, 1, {})); });
  check(server.takeCommands().size() == 1);
  (void)server.snapshot(world);
  a.receive(server.snapshotFor(p, both));
  check(a.records() == world);
  (void)server.snapshot(world);
  auto lost = server.snapshotFor(
      p, both); // simulate a missing reliable-stream baseline
  (void)lost;
  (void)server.snapshot(world);
  auto mismatch = server.snapshotFor(p, both);
  bool typed = false;
  try {
    a.receive(mismatch);
  } catch (const BaselineMismatch &) {
    typed = true;
  }
  check(typed && a.records() == world);
  server.receive(p, a.requestSnapshot());
  (void)server.snapshot(world);
  auto recovery = server.snapshotFor(p, both);
  check(decode(recovery).type == Type::Snapshot);
  a.receive(recovery);
  for (unsigned i = 0; i < 20; ++i) {
    (void)server.snapshot(world);
    auto packet = server.snapshotFor(p, both);
    check(decode(packet).type == (i == 19 ? Type::Snapshot : Type::Delta));
    a.receive(packet);
  }
  // Invalid relevance does not advance the peer baseline; retry same
  // publication.
  (void)server.snapshot(world);
  std::vector<EntityId> bad{999};
  rejects([&] { server.snapshotFor(p, bad); });
  bad = {1, 1};
  rejects([&] { server.snapshotFor(p, bad); });
  a.receive(server.snapshotFor(p, none));
  check(a.records().size() == 1);
  (void)server.snapshot(world);
  a.receive(server.snapshotFor(p, both, true));
  check(a.records() == world);
  // Reconstructed capacity is checked atomically, including upserts.
  Message over;
  over.type = Type::Delta;
  over.schema = 55;
  over.session = 101;
  over.baseline = a.sequence();
  over.sequence = over.baseline + 1;
  for (unsigned id = 4; id < 68; ++id)
    over.records.push_back({id, 0, 1, {}});
  rejects([&] { a.receive(encode(over)); });
  check(a.records() == world);
  server.disconnect(p);
  auto fresh = server.connect();
  a.reset();
  a.receive(server.receive(fresh, a.hello()));
  (void)server.snapshot(world);
  auto initial = server.snapshotFor(fresh, none);
  check(decode(initial).type == Type::Snapshot);
  a.receive(initial);
  check(a.records().empty());
  // All 256 payload offsets survive the byte codec, including offset 255.
  ServerSession edge(8, 8);
  auto id = edge.connect();
  ClientSession client(8);
  client.receive(edge.receive(id, client.hello()));
  std::vector<Record> data{{9, id, 1, Bytes(maxPayload, 0)}};
  (void)edge.snapshot(data);
  client.receive(edge.snapshotFor(id, {}));
  data[0].data[255] = 255;
  (void)edge.snapshot(data);
  auto patch = edge.snapshotFor(id, {});
  check(decode(patch).patches.front().changes.front().offset == 255);
  client.receive(patch);
  check(client.records() == data);
  // Metadata changes require full upserts, not payload patches.
  data[0].kind = 2;
  (void)edge.snapshot(data);
  auto changed = edge.snapshotFor(id, {});
  client.receive(changed);
  check(client.records() == data && decode(changed).type == Type::Snapshot);
  auto request = decode(client.requestSnapshot());
  request.peer++;
  rejects([&] { edge.receive(id, encode(request)); });
}
int main() {
  try {
    deltaReplication();
    ServerSession s(7, 99);
    auto p = s.connect(), q = s.connect();
    ClientSession a(7), b(7);
    a.receive(s.receive(p, a.hello()));
    b.receive(s.receive(q, b.hello()));
    std::vector<Record> records{{1, p, 1, {10, 20}}, {2, q, 1, {30}}};
    auto snapshot = s.snapshot(records);
    a.receive(snapshot);
    b.receive(snapshot);
    check(a.records() == records && a.peer() != b.peer());
    const auto old = a.records();
    auto corrupt = snapshot;
    corrupt.push_back(0);
    rejects([&] { a.receive(corrupt); });
    check(a.records() == old);
    rejects([&] { a.receive(snapshot); });
    auto cmd = a.command(1, 8, {5});
    s.receive(p, cmd);
    rejects([&] { s.receive(p, cmd); });
    auto commands = s.takeCommands();
    check(commands.size() == 1 && commands[0].peer == p);
    rejects([&] { s.receive(p, a.command(2, 8, {})); });
    check(s.takeCommands().empty());
    ClientSession wrong(8);
    auto third = s.connect();
    rejects([&] { s.receive(third, wrong.hello()); });
    check(s.readyPeers().size() == 2);
    s.disconnect(third);
    records.push_back(records.front());
    rejects([&] { s.snapshot(records); });
    records.pop_back();
    a.receive(s.snapshot(records));
    check(a.records() == old);
    for (std::size_t n = 0; n < snapshot.size(); ++n)
      rejects([&] { decode(std::span(snapshot).first(n)); });
    records[0].data.resize(maxPayload + 1);
    rejects([&] { s.snapshot(records); });
    records[0].data = {10, 20};
    for (std::size_t i = 0; i < maxQueue; ++i)
      s.receive(p, a.command(1, 8, {}));
    rejects([&] { s.receive(p, a.command(1, 8, {})); });
    s.disconnect(p);
    check(s.takeCommands().empty());
    a.reset();
    auto newPeer = s.connect();
    check(newPeer != p);
    a.receive(s.receive(newPeer, a.hello()));
    records.erase(records.begin());
    a.receive(s.snapshot(records));
    check(a.records().size() == 1);
    check(validEndpoint("ws://127.0.0.1:8177") &&
          validEndpoint("wss://example.test/session"));
    check(!validEndpoint("ws://example.test") &&
          !validEndpoint("ws://localhost@evil.test") &&
          !validEndpoint("ws://localhost/#fragment"));
    auto broken = snapshot;
    broken[4] = 2;
    rejects([&] { decode(broken); });
    a.reset();
    rejects([&] { a.receive(snapshot); });
    std::cout
        << "network PASS: ABI codec, truncation, atomic full/delta snapshots, "
           "byte patches, interest, resync, authority, sequences, budgets, "
           "lifecycle\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
