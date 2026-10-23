#pragma once
#include "engine/application.hpp"
#include "engine/camera_component.hpp"
#include "engine/mesh_component.hpp"
#include "engine/network.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>

namespace multiplayer_example {
using namespace engine;
inline constexpr std::uint32_t schema =
    0x4d503031; // MP01 arena content + payload schema
inline void integer(net::Bytes &b, std::int32_t v) {
  auto n = std::bit_cast<std::uint32_t>(v);
  for (int i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(n >> (8 * i)));
}
inline std::int32_t integer(const net::Bytes &b, std::size_t offset) {
  if (offset + 4 > b.size())
    throw std::invalid_argument("Arena payload truncated");
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i)
    v |= std::uint32_t(b[offset + i]) << (8 * i);
  return std::bit_cast<std::int32_t>(v);
}
inline net::Bytes position(float x, float z, int score = 0) {
  net::Bytes b;
  integer(b, int(std::lround(x * 1000)));
  integer(b, int(std::lround(z * 1000)));
  integer(b, score);
  return b;
}
inline net::Bytes movement(int x, int z) {
  net::Bytes b;
  integer(b, x);
  integer(b, z);
  return b;
}
inline void validate(const std::vector<net::Record> &records) {
  for (const auto &r : records) {
    if ((r.kind != 1 && r.kind != 2) || r.data.size() != 12 ||
        std::abs(std::int64_t(integer(r.data, 0))) > 8000 ||
        std::abs(std::int64_t(integer(r.data, 4))) > 8000 ||
        integer(r.data, 8) < 0)
      throw std::invalid_argument("Arena snapshot invalid");
  }
}

// Game policy, intentionally outside engine_network. The server advances these
// components using Application's existing fixed traversal. No render-driven
// state.
class Movement final : public Component {
public:
  float x = 0, z = 0;
  double age = 1, cooldown = 0;
  Movement() : Component("Authoritative movement") {
    setFixedTickEnabled(true);
  }

protected:
  void fixedTick(double dt) override {
    age += dt;
    cooldown = std::max(0., cooldown - dt);
    if (age > .25)
      return;
    auto &p = owner()->transform.position;
    p.x = std::clamp(p.x + x * float(dt) * 3.f, -7.f, 7.f);
    p.z = std::clamp(p.z + z * float(dt) * 3.f, -7.f, 7.f);
  }
};
struct Arena {
  Scene &scene;
  std::map<net::PeerId, ActorId> players;
  int score = 0;
  void sync(std::span<const net::PeerId> peers) {
    for (auto it = players.begin(); it != players.end();)
      if (std::find(peers.begin(), peers.end(), it->first) == peers.end()) {
        if (auto *a = scene.findActor(it->second))
          a->destroy();
        it = players.erase(it);
      } else
        ++it;
    for (auto p : peers)
      if (!players.contains(p)) {
        auto &a = scene.spawn("Player " + std::to_string(p));
        a.transform.position = {0, .5f, 1.5f};
        a.addComponent<Movement>();
        players.emplace(p, a.id());
      }
  }
  void command(const net::Command &c) {
    auto found = players.find(c.peer);
    if (found == players.end())
      return;
    auto *a = scene.findActor(found->second);
    if (!a)
      return;
    auto *m = a->getComponent<Movement>();
    if (c.kind == 1) {
      if (c.data.size() != 8)
        throw std::invalid_argument("Movement payload size");
      const auto x = integer(c.data, 0), z = integer(c.data, 4);
      if (std::abs(std::int64_t(x)) > 1000 || std::abs(std::int64_t(z)) > 1000)
        throw std::invalid_argument("Movement range");
      float length = std::max(1000.f, std::hypot(float(x), float(z)));
      m->x = x / length;
      m->z = z / length;
      m->age = 0;
    } else if (c.kind == 2) {
      if (!c.data.empty())
        throw std::invalid_argument("Interaction payload");
      auto p = a->transform.position;
      if (std::hypot(p.x, p.z) <= 2.2f && m->cooldown == 0) {
        score = std::min(score + 1, 1000000);
        m->cooldown = .5;
      }
    } else
      throw std::invalid_argument("Unknown arena command");
  }
  std::vector<net::Record> records() {
    std::vector<net::Record> v{{1, 0, 2, position(0, 0, score)}};
    for (auto [peer, id] : players)
      if (auto *a = scene.findActor(id))
        v.push_back(
            {peer + 1, peer, 1,
             position(a->transform.position.x, a->transform.position.z)});
    return v;
  }
};
struct View {
  ComponentId camera;
  std::map<net::EntityId, ActorId> actors;
  std::unique_ptr<Scene> make() {
    auto s = std::make_unique<Scene>("Multiplayer test arena");
    auto &c = s->spawn("Camera").addComponent<CameraComponent>();
    c.transform.position = {0, 11, 13};
    c.lookAt({0, 0, 0});
    camera = c.id();
    auto &floor = s->spawn("Arena floor")
                      .addComponent<MeshComponent>("Floor", Mesh::cube());
    floor.transform.position = {0, -.25f, 0};
    floor.transform.scale = {16, .25f, 16};
    material(floor, {.12f, .18f, .25f});
    return s;
  }
  static void material(MeshComponent &m, Vec3 color) {
    auto material = std::make_shared<Material>();
    material->shader = MaterialShader::Unlit;
    material->baseColor = color;
    m.setMaterial(0, MaterialInstance{material});
  }
  void project(Scene &scene, const net::ClientSession &client, double dt) {
    const auto &records = client.records();
    validate(records);
    for (auto it = actors.begin(); it != actors.end();)
      if (std::none_of(records.begin(), records.end(),
                       [&](const auto &r) { return r.id == it->first; })) {
        if (auto *a = scene.findActor(it->second))
          a->destroy();
        it = actors.erase(it);
      } else
        ++it;
    for (const auto &r : records) {
      Vec3 target{integer(r.data, 0) / 1000.f, r.kind == 2 ? .25f : .6f,
                  integer(r.data, 4) / 1000.f};
      auto it = actors.find(r.id);
      if (it == actors.end()) {
        auto &a = scene.spawn(r.kind == 2 ? "Shared beacon"
                                          : "Network player " +
                                                std::to_string(r.owner));
        a.transform.position = target;
        auto &m =
            a.addComponent<MeshComponent>("Replicated mesh", Mesh::cube());
        m.transform.scale =
            r.kind == 2 ? Vec3{1, .5f, 1} : Vec3{.7f, 1.2f, .7f};
        material(m, r.kind == 2                ? Vec3{1, .7f, .15f}
                    : r.owner == client.peer() ? Vec3{.1f, .85f, .65f}
                                               : Vec3{.35f, .55f, 1});
        it = actors.emplace(r.id, a.id()).first;
      }
      if (auto *a = scene.findActor(it->second)) {
        const float blend = std::min(1.f, float(dt) * 18.f);
        auto p = a->transform.position;
        a->transform.position = p + (target - p) * blend;
        if (r.kind == 2)
          a->transform.rotation.y = float(integer(r.data, 8)) * .35f;
      }
    }
  }
};
struct Client {
  net::ClientSession session{schema};
  std::unique_ptr<net::Connection> connection;
  std::string status = "Disconnected";
  bool greeted = false;
  double lastInput = 0, lastReceive = 0, started = 0;
  int x = 0, z = 0;
  bool interact = false;
  void leave() {
    connection.reset();
    session.reset();
    greeted = false;
    x = z = 0;
    interact = false;
    status = "Disconnected";
  }
  void join(const std::string &url, double now) {
    // Validate and construct the candidate before retiring a working session.
    auto candidate = net::connectWebSocket(url);
    leave();
    connection = std::move(candidate);
    started = lastReceive = now;
    lastInput = 0;
    status = "Connecting";
  }
  void update(double now) {
    if (!connection)
      return;
    try {
      auto state = connection->state();
      if (state == net::ConnectionState::Closed ||
          state == net::ConnectionState::Failed) {
        auto error = connection->error();
        leave();
        status = error.empty() ? "Disconnected by server" : error;
        return;
      }
      if (now - lastReceive > 10) {
        leave();
        status = "Session timed out; reconnect to rejoin";
        return;
      }
      if (state != net::ConnectionState::Open)
        return;
      if (!greeted) {
        if (!connection->send(session.hello()))
          throw std::runtime_error("Send queue full");
        greeted = true;
      }
      for (auto &b : connection->receive()) {
        auto candidate = session;
        candidate.receive(b);
        validate(candidate.records());
        session = std::move(candidate);
        lastReceive = now;
      }
      if (!session.ready())
        return;
      status = "Connected as player " + std::to_string(session.peer());
      const auto target = session.peer() + 1;
      const bool spawned =
          std::any_of(session.records().begin(), session.records().end(),
                      [&](const auto &r) { return r.id == target; });
      if (spawned && now - lastInput >= .05) {
        if (!connection->send(session.command(target, 1, movement(x, z))))
          throw std::runtime_error("Send queue full");
        lastInput = now;
      }
      if (spawned && interact) {
        if (!connection->send(session.command(target, 2, {})))
          throw std::runtime_error("Send queue full");
        interact = false;
      }
    } catch (const std::exception &e) {
      auto error = std::string(e.what());
      leave();
      status = error;
    }
  }
  std::string json() const {
    std::ostringstream o;
    o << "{\"peer\":" << session.peer()
      << ",\"snapshot\":" << session.sequence() << ",\"entities\":[";
    bool first = true;
    for (const auto &r : session.records()) {
      if (!first)
        o << ',';
      first = false;
      o << "{\"id\":" << r.id << ",\"owner\":" << r.owner
        << ",\"x\":" << integer(r.data, 0) / 1000.
        << ",\"z\":" << integer(r.data, 4) / 1000.
        << ",\"score\":" << integer(r.data, 8) << '}';
    }
    o << "]}";
    return o.str();
  }
};
} // namespace multiplayer_example
