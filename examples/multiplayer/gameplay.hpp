#pragma once
#include "engine/editor.hpp"
#include "engine/network_component_editor.hpp"
#include "engine/network_rpc_editor.hpp"
#include <bit>
#include <cmath>

// Example game rules/adapters, deliberately outside reusable engine modules.
namespace multiplayer_gameplay {
using namespace engine;
inline constexpr std::uint32_t spawnRpc=1,despawnRpc=2,pulseRpc=3,
  spawnedEvent=4,despawnedEvent=5,pulseEvent=6,impulseRpc=7,collisionEvent=8,impulsedEvent=9;
inline void put(net::Bytes &b, std::uint32_t n) {
  for (int i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(n >> (8 * i)));
}
inline std::uint32_t get(std::span<const std::uint8_t> b, std::size_t p) {
  if (p + 4 > b.size())
    throw std::invalid_argument("Gameplay state truncated");
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i)
    v |= std::uint32_t(b[p + i]) << (8 * i);
  return v;
}
inline const net::InvocationRegistry &invocations() {
  static const auto value=[] {
    net::InvocationRegistry r;
    for(auto [id,key,direction,bytes]:{
      std::tuple{spawnRpc,"spawn",net::InvocationDirection::ServerRpc,4u},
      std::tuple{despawnRpc,"despawn",net::InvocationDirection::ServerRpc,4u},
      std::tuple{pulseRpc,"pulse",net::InvocationDirection::ServerRpc,4u},
      std::tuple{spawnedEvent,"spawned",net::InvocationDirection::ClientEvent,8u},
      std::tuple{despawnedEvent,"despawned",net::InvocationDirection::ClientEvent,4u},
      std::tuple{pulseEvent,"pulsed",net::InvocationDirection::ClientEvent,4u}})
      r.add({id,1,key,direction,bytes,[bytes](auto b){if(b.size()!=bytes||!get(b,0))throw std::invalid_argument("Gameplay invocation payload");}});
    r.add({impulseRpc,1,"impulse",net::InvocationDirection::ServerRpc,16,[](auto b){
      if(b.size()!=16||!get(b,0))throw std::invalid_argument("Impulse RPC payload");
      float length=0;for(int axis=0;axis<3;++axis){auto v=std::bit_cast<float>(get(b,4+axis*4));if(!std::isfinite(v)||std::abs(v)>10)throw std::invalid_argument("Impulse range");length+=v*v;}
      if(length>100)throw std::invalid_argument("Impulse magnitude");}});
    r.add({collisionEvent,1,"collision",net::InvocationDirection::ClientEvent,20,[](auto b){
      if(b.size()!=20||!get(b,0))throw std::invalid_argument("Collision event payload");
      for(int i=4;i<20;i+=4)if(auto v=std::bit_cast<float>(get(b,i));!std::isfinite(v)||std::abs(v)>1e6f)throw std::invalid_argument("Collision event range");}});
    r.add({impulsedEvent,1,"impulsed",net::InvocationDirection::ClientEvent,4,[](auto b){if(b.size()!=4||!get(b,0))throw std::invalid_argument("Impulse receipt");}});
    return r;
  }();return value;
}
inline net::InvocationPermissions allInvocations() {
  net::InvocationPermissions p;
  for(const auto &type:invocations().entries())
    (type.direction==net::InvocationDirection::ServerRpc?p.serverRpcs:p.clientEvents)|=net::invocationBit(type.id);
  return p;
}
struct VitalsData {
  float health = 100;
  std::uint32_t charges = 3;
  bool invulnerable = false;
};
inline net::Bytes encode(VitalsData data) {
  if (!std::isfinite(data.health) || data.health < 0 || data.health > 100 ||
      data.charges > 999)
    throw std::invalid_argument("Vitals range");
  net::Bytes out;
  put(out, std::bit_cast<std::uint32_t>(data.health));
  put(out, data.charges);
  put(out, data.invulnerable ? 1 : 0);
  return out;
}
inline VitalsData vitals(std::span<const std::uint8_t> b) {
  if (b.size() != 12 || get(b, 8) > 1)
    throw std::invalid_argument("Vitals payload");
  VitalsData d{std::bit_cast<float>(get(b, 0)), get(b, 4), get(b, 8) != 0};
  (void)encode(d);
  return d;
}
struct ProgressData {
  std::uint32_t score = 0;
  std::string stage = "idle";
};
inline net::Bytes encode(const ProgressData &data) {
  if (data.score > 100000 || data.stage.empty() || data.stage.size() > 24 ||
      std::any_of(data.stage.begin(), data.stage.end(),
                  [](char c) { return c < 32 || c > 126; }))
    throw std::invalid_argument("Progress range");
  net::Bytes out;
  put(out, data.score);
  put(out, std::uint32_t(data.stage.size()));
  out.insert(out.end(), data.stage.begin(), data.stage.end());
  return out;
}
inline ProgressData progress(std::span<const std::uint8_t> b) {
  if (b.size() < 8 || get(b, 4) != b.size() - 8)
    throw std::invalid_argument("Progress payload");
  ProgressData d{get(b, 0), std::string(b.begin() + 8, b.end())};
  (void)encode(d);
  return d;
}
class Vitals final : public Component {
public:
  VitalsData data;
  bool replica = false;
  unsigned ticks = 0;
  Vitals() : Component("Vitals") {
    setFixedTickEnabled(true);
    net::setComponentReplication(*this, net::ComponentReplication::Everyone);
  }
  std::string_view typeName() const noexcept override {
    return "GameVitalsComponent";
  }

protected:
  void fixedTick(double) override {
    if (!replica && ++ticks % 120 == 0 && !data.invulnerable)
      data.health = std::max(0.f, data.health - 1);
  }
};
class Progress final : public Component {
public:
  ProgressData data;
  bool replica = false;
  unsigned ticks = 0;
  Progress() : Component("Progress") {
    setFixedTickEnabled(true);
    net::setComponentReplication(*this, net::ComponentReplication::OwnerOnly);
  }
  std::string_view typeName() const noexcept override {
    return "GameProgressComponent";
  }

protected:
  void fixedTick(double) override {
    if (!replica && ++ticks % 180 == 0)
      data.score = std::min(100000u, data.score + 1);
  }
};
struct VitalsUpdate final : net::PreparedComponentUpdate {
  Vitals &target;
  VitalsData data;
  VitalsUpdate(Vitals &t, VitalsData d) : target(t), data(d) {}
  void commit() noexcept override { target.data = data; }
};
struct ProgressUpdate final : net::PreparedComponentUpdate {
  Progress &target;
  ProgressData data;
  ProgressUpdate(Progress &t, ProgressData d) : target(t), data(std::move(d)) {}
  void commit() noexcept override {
    target.data.score = data.score;
    target.data.stage.swap(data.stage);
  }
};
inline const net::ComponentReplicationRegistry &registry() {
  static const auto result = [] {
    net::ComponentReplicationRegistry r;
    ComponentCodec v;
    v.key = "Example.Vitals";
    v.type = typeid(Vitals);
    v.encode = [](const Component &c) {
      auto b = encode(static_cast<const Vitals &>(c).data);
      auto span = std::as_bytes(std::span(b));
      return std::vector<std::byte>(span.begin(), span.end());
    };
    v.decode = [](std::uint32_t version,
                  std::span<const std::byte> b) -> std::unique_ptr<Component> {
      if (version != 1)
        throw std::invalid_argument("Vitals archive schema");
      auto c = std::make_unique<Vitals>();
      c->data =
          vitals({reinterpret_cast<const std::uint8_t *>(b.data()), b.size()});
      return c;
    };
    r.add({v, 1, 12,
           [](const Component &c) {
             return encode(static_cast<const Vitals &>(c).data);
           },
           [](auto b) { (void)vitals(b); },
           [](Component &c,
              auto b) -> std::unique_ptr<net::PreparedComponentUpdate> {
             return std::make_unique<VitalsUpdate>(dynamic_cast<Vitals &>(c),
                                                   vitals(b));
           },
           [](Component &c) { dynamic_cast<Vitals &>(c).replica = true; }});
    ComponentCodec p;
    p.key = "Example.Progress";
    p.type = typeid(Progress);
    p.encode = [](const Component &c) {
      auto b = encode(static_cast<const Progress &>(c).data);
      auto span = std::as_bytes(std::span(b));
      return std::vector<std::byte>(span.begin(), span.end());
    };
    p.decode = [](std::uint32_t version,
                  std::span<const std::byte> b) -> std::unique_ptr<Component> {
      if (version != 1)
        throw std::invalid_argument("Progress archive schema");
      auto c = std::make_unique<Progress>();
      c->data = progress(
          {reinterpret_cast<const std::uint8_t *>(b.data()), b.size()});
      return c;
    };
    r.add({p, 1, 32,
           [](const Component &c) {
             return encode(static_cast<const Progress &>(c).data);
           },
           [](auto b) { (void)progress(b); },
           [](Component &c,
              auto b) -> std::unique_ptr<net::PreparedComponentUpdate> {
             return std::make_unique<ProgressUpdate>(
                 dynamic_cast<Progress &>(c), progress(b));
           },
           [](Component &c) { dynamic_cast<Progress &>(c).replica = true; }});
    return r;
  }();
  return result;
}
inline ComponentCodecs editorCodecs() {
  auto c = aiControllerCodecs();
  for (const auto &type : registry().entries())
    c.add(type.archive);
  return c;
}
inline std::vector<EditorComponentFactory> factories() {
  return {{"GameVitals", "Vitals", [] { return std::make_unique<Vitals>(); }},
          {"GameProgress", "Progress",
           [] { return std::make_unique<Progress>(); }},
          {"NetworkGameplay","Gameplay RPCs and events",[]{return net::makeInvocationMetadata(allInvocations());}}};
}
inline std::shared_ptr<const EditorPropertyRegistry> properties() {
  static const auto result = [] {
    auto r = net::invocationProperties(invocations(),net::componentReplicationProperties(registry()));
    auto field = [&](EditorPropertyInfo info, auto accepts, auto read,
                     auto write) {
      r->add({std::move(info), accepts, read, write});
    };
    field(
        {"game.health",
         "Health",
         "Vitals",
         EditorPropertyType::Number,
         0,
         100,
         1,
         {},
         0.0},
        [](const Node &n) { return typeid(n) == typeid(Vitals); },
        [](const Node &n) -> EditorValue {
          return double(static_cast<const Vitals &>(n).data.health);
        },
        [](Node &n, const EditorValue &v) {
          auto &c = static_cast<Vitals &>(n);
          auto d = c.data;
          d.health = float(std::get<double>(v));
          (void)encode(d);
          c.data = d;
        });
    field(
        {"game.charges",
         "Charges",
         "Vitals",
         EditorPropertyType::Number,
         0,
         999,
         1,
         {},
         0.0},
        [](const Node &n) { return typeid(n) == typeid(Vitals); },
        [](const Node &n) -> EditorValue {
          return double(static_cast<const Vitals &>(n).data.charges);
        },
        [](Node &n, const EditorValue &v) {
          auto x = std::get<double>(v);
          if (x != std::floor(x))
            throw std::invalid_argument("Integral charges");
          static_cast<Vitals &>(n).data.charges = std::uint32_t(x);
        });
    field(
        {"game.invulnerable",
         "Invulnerable",
         "Vitals",
         EditorPropertyType::Boolean,
         0,
         1,
         1,
         {},
         false},
        [](const Node &n) { return typeid(n) == typeid(Vitals); },
        [](const Node &n) -> EditorValue {
          return static_cast<const Vitals &>(n).data.invulnerable;
        },
        [](Node &n, const EditorValue &v) {
          static_cast<Vitals &>(n).data.invulnerable = std::get<bool>(v);
        });
    field(
        {"game.score",
         "Score",
         "Progress",
         EditorPropertyType::Number,
         0,
         100000,
         1,
         {},
         0.0},
        [](const Node &n) { return typeid(n) == typeid(Progress); },
        [](const Node &n) -> EditorValue {
          return double(static_cast<const Progress &>(n).data.score);
        },
        [](Node &n, const EditorValue &v) {
          auto x = std::get<double>(v);
          if (x != std::floor(x))
            throw std::invalid_argument("Integral score");
          static_cast<Progress &>(n).data.score = std::uint32_t(x);
        });
    field(
        {"game.stage",
         "Stage",
         "Progress",
         EditorPropertyType::Text,
         0,
         1,
         1,
         {},
         std::string("idle")},
        [](const Node &n) { return typeid(n) == typeid(Progress); },
        [](const Node &n) -> EditorValue {
          return static_cast<const Progress &>(n).data.stage;
        },
        [](Node &n, const EditorValue &v) {
          auto &c = static_cast<Progress &>(n);
          auto d = c.data;
          d.stage = std::get<std::string>(v);
          (void)encode(d);
          c.data = std::move(d);
        });
    return r;
  }();
  return result;
}
inline void activate(Actor &actor) {
  auto *v = actor.getComponent<Vitals>();
  if (!v || !v->data.charges)
    return;
  --v->data.charges;
  if (!v->data.invulnerable)
    v->data.health = std::max(0.f, v->data.health - 7);
  if (auto *p = actor.getComponent<Progress>()) {
    p->data.score = std::min(100000u, p->data.score + 10);
    p->data.stage = "activated";
  }
}
} // namespace multiplayer_gameplay
