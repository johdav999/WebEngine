#include "scene_shared.hpp"
#include <chrono>
#include <iostream>
using namespace multiplayer_scene;
namespace {
std::string hex(const net::Bytes &b) {
  constexpr char d[] = "0123456789abcdef";
  std::string s;
  for (auto n : b) {
    s += d[n >> 4];
    s += d[n & 15];
  }
  return s;
}
net::Bytes unhex(const std::string &s) {
  if (s.size() > net::maxMessage * 2 || s.size() % 2)
    throw std::invalid_argument("Bridge size");
  auto digit = [](char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    throw std::invalid_argument("Bridge encoding");
  };
  net::Bytes b;
  for (std::size_t i = 0; i < s.size(); i += 2)
    b.push_back(std::uint8_t(digit(s[i]) * 16 + digit(s[i + 1])));
  return b;
}
struct Peer {
  net::PeerId id;
  net::ContentReceiver incoming;
  bool requested = false, sent = false, suspended = false;
  std::size_t offset = 0;
  double started = 0;
  std::vector<net::EntityId> visible;
};
double now() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
} // namespace
int main(int argc, char **argv) {
  const bool testCorrection =
      argc == 2 && std::string(argv[1]) == "--test-correction";
  try {
    Application app;
    std::unique_ptr<Arena> arena;
    net::Bytes content;
    const auto seed = std::uint32_t(
        std::chrono::steady_clock::now().time_since_epoch().count());
    net::ServerSession session(schema, seed ? seed : 1);
    std::map<unsigned, Peer> peers;
    unsigned frames = 0;
    std::vector<Arena::GameEvent> events;
    auto close = [&](unsigned ch) {
      auto i = peers.find(ch);
      if (i != peers.end()) {
        session.disconnect(i->second.id);
        peers.erase(i);
      }
      std::cout << "C " << ch << '\n';
    };
    auto send = [](unsigned ch, const net::Bytes &b) {
      std::cout << "D " << ch << ' ' << hex(b) << '\n';
    };
    std::cout << "READY\n" << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.size() > net::maxMessage * 2 + 64)
        throw std::invalid_argument("Bridge bound");
      std::istringstream in(line);
      char kind;
      unsigned ch = 0;
      in >> kind;
      if (kind == 'Q')
        break;
      if (kind == 'O') {
        in >> ch;
        try {
          if (!ch || peers.contains(ch))
            throw std::invalid_argument("Channel");
          const auto id = session.connect();
          if (id >= net::serverEntityBase) {
            session.disconnect(id);
            throw std::invalid_argument(
                "Scene peer identity budget exhausted; restart server");
          }
          peers.emplace(ch, Peer{id, {}, false, false, false, 0, now()});
        } catch (...) {
          close(ch);
        }
      } else if(kind=='S'||kind=='R'){
        in>>ch;auto at=peers.find(ch);if(at==peers.end())throw std::invalid_argument("Recovery channel");
        auto &peer=at->second;
        if(kind=='S'){session.suspend(peer.id);peer.suspended=true;}
        else {session.resume(peer.id);peer.suspended=false;peer.started=now();peer.incoming.reset();peer.requested=peer.sent=false;peer.offset=0;peer.visible.clear();}
        if(arena)arena->resetInputs(peer.id);
      } else if (kind == 'C') {
        in >> ch;
        close(ch);
      } else if (kind == 'D') {
        std::string data;
        in >> ch >> data;
        try {
          auto i = peers.find(ch);
          if (i == peers.end())
            throw std::invalid_argument("Peer");
          auto &peer = i->second;
          auto b = unhex(data);
          if (net::isContent(b)) {
            if (peer.requested || peer.sent)
              throw std::invalid_argument("Duplicate content request");
            if (b == net::contentRequest()) {
              if (!peer.incoming.bytes().empty())
                throw std::invalid_argument("Interrupted upload");
              peer.requested = true;
            } else {
              peer.incoming.receive(b);
              if (peer.incoming.complete()) {
                if (!content.empty()) {
                  if (content != peer.incoming.bytes())
                    throw std::invalid_argument(
                        "Content mismatch; restart server for a new authored "
                        "scene");
                } else {
                  auto candidate = net::loadNetworkScene(
                      peer.incoming.bytes(), multiplayer_gameplay::registry(),multiplayer_gameplay::invocations());
                  auto *scene = candidate.document.scene.get();
                  auto simulation = std::make_unique<Arena>(
                      *scene, candidate.playerTemplate, testCorrection,
                      candidate.replicated);
                  auto id = app.loadScene(std::move(candidate.document.scene));
                  app.start(id);
                  arena = std::move(simulation);
                  content = peer.incoming.bytes();
                  std::cerr << "Scene published " << content.size()
                            << " bytes\n";
                }
                peer.incoming.reset();
                peer.requested = true;
              }
            }
          } else {
            if (!peer.sent)
              throw std::invalid_argument("Receive scene before session Hello");
            auto response = session.receive(peer.id, b);
            if (!response.empty())
              send(ch, response);
          }
        } catch (const std::exception &e) {
          std::cerr << "Peer rejected: " << e.what() << '\n';
          close(ch);
        }
      } else if (kind == 'T') {
        std::vector<unsigned> expired;
        const auto time = now();
        auto ready = session.readyPeers();
        for (auto &[channel, p] : peers) {
          if (!p.suspended && std::find(ready.begin(), ready.end(), p.id) == ready.end() &&
              time - p.started > 30) {
            expired.push_back(channel);
            continue;
          }
          if (p.requested && !p.sent && !content.empty())
            for (int n = 0; n < 2 && !p.sent; ++n) {
              send(channel, net::contentChunk(content, p.offset));
              p.offset =
                  std::min(content.size(), p.offset + net::contentChunkSize);
              p.sent = p.offset == content.size();
            }
        }
        for (auto channel : expired)
          close(channel);
        if (arena) {
          ready = session.readyPeers();
          arena->sync(session.simulationPeers());
          for (const auto &c : session.takeCommands())
            try {
              arena->command(c);
            } catch (...) {
              for (const auto &[channel, p] : peers)
                if (p.id == c.peer) {
                  close(channel);
                  break;
                }
            }
          for(const auto &call:session.takeRpcs())try{
            if(events.size()>=net::maxQueue)throw std::invalid_argument("Gameplay event backlog");
            auto event=arena->rpc(call,time);
            if(arena->definitions.front().invocations.clientEvents&net::invocationBit(event.kind))events.push_back(std::move(event));
          }catch(const std::exception &e){std::cerr<<"RPC rejected: "<<e.what()<<'\n';for(const auto &[channel,p]:peers)if(p.id==call.peer){close(channel);break;}}
          app.advance(time);
          for(auto &event:arena->takeCollisions())if(events.size()<net::maxQueue&&
              (arena->definitions.front().invocations.clientEvents&net::invocationBit(event.kind)))events.push_back(std::move(event));
          if (++frames % 3 == 0) {
            auto records = arena->records();
            // Keep the complete ownership registry independent of relevance.
            (void)session.snapshot(records);
            for (auto &[channel, p] : peers)
              if (std::find(ready.begin(), ready.end(), p.id) != ready.end()) {
                auto own = std::find_if(
                    records.begin(), records.end(),
                    [&](const auto &r) { return r.kind==1 && r.owner == p.id; });
                std::vector<net::EntityId> relevant;
                if (own != records.end()) {
                  const auto origin = pose(own->data).position;
                  for (const auto &r : records) {
                    auto position = pose(r.data).position;
                    auto visible = std::find(p.visible.begin(), p.visible.end(),
                                             r.id) != p.visible.end();
                    if (net::relevantTo(arena->settings(r), r.owner == p.id,
                                        visible,
                                        std::hypot(position.x - origin.x,
                                                   position.z - origin.z)))
                      relevant.push_back(r.id);
                  }
                }
                std::vector<net::Record> projected;
                for (const auto &record : records)
                  if (std::find(relevant.begin(), relevant.end(), record.id) !=
                      relevant.end())
                    projected.push_back(arena->forPeer(record, p.id));
                send(channel, session.snapshotForRecords(p.id, projected));
                p.visible = std::move(relevant);
                for(const auto &event:events)if((!event.ownerOnly||event.source==p.id)&&
                    std::find(p.visible.begin(),p.visible.end(),event.source)!=p.visible.end()){
                  multiplayer_gameplay::invocations().validate(event.kind,net::InvocationDirection::ClientEvent,event.data);
                  send(channel,session.eventFor(p.id,event.source,event.kind,event.data));
                }
              }
          events.clear();
          }
        }
      } else
        throw std::invalid_argument("Bridge command");
      std::cout << std::flush;
    }
    arena.reset();
    app.shutdown();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
