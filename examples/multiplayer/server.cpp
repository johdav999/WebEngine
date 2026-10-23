#include "shared.hpp"
#include <chrono>
#include <iostream>
#include <sstream>

using namespace multiplayer_example;
namespace {
std::string hex(const net::Bytes &b) {
  constexpr char digits[] = "0123456789abcdef";
  std::string s;
  for (auto n : b) {
    s += digits[n >> 4];
    s += digits[n & 15];
  }
  return s;
}
net::Bytes unhex(const std::string &s) {
  if (s.size() > net::maxMessage * 2 || s.size() % 2)
    throw std::invalid_argument("Bridge size");
  net::Bytes b;
  auto digit = [](char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    throw std::invalid_argument("Bridge encoding");
  };
  for (std::size_t i = 0; i < s.size(); i += 2)
    b.push_back(std::uint8_t(digit(s[i]) * 16 + digit(s[i + 1])));
  return b;
}
} // namespace
int main() {
  try {
    Application app;
    auto scene = std::make_unique<Scene>("Authoritative arena");
    auto *world = scene.get();
    auto id = app.loadScene(std::move(scene));
    app.start(id);
    Arena arena{*world};
    auto seed = std::uint32_t(
        std::chrono::steady_clock::now().time_since_epoch().count());
    net::ServerSession session(schema, seed ? seed : 1);
    std::map<unsigned, net::PeerId> peers;
    unsigned frame = 0;
    double time = 0;
    auto send = [&](unsigned channel, const net::Bytes &bytes) {
      std::cout << "D " << channel << ' ' << hex(bytes) << '\n';
    };
    auto close = [&](unsigned channel) {
      if (auto it = peers.find(channel); it != peers.end()) {
        session.disconnect(it->second);
        peers.erase(it);
      }
      std::cout << "C " << channel << '\n';
    };
    std::cout << "READY\n" << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.size() > net::maxMessage * 2 + 64)
        throw std::invalid_argument("Bridge line too large");
      std::istringstream in(line);
      char kind;
      unsigned channel = 0;
      in >> kind;
      if (kind == 'Q')
        break;
      if (kind == 'O') {
        in >> channel;
        try {
          if (!channel || peers.contains(channel))
            throw std::invalid_argument("Bridge channel");
          peers.emplace(channel, session.connect());
        } catch (...) {
          close(channel);
        }
      } else if (kind == 'C') {
        in >> channel;
        close(channel);
      } else if (kind == 'D') {
        std::string data;
        in >> channel >> data;
        try {
          auto found = peers.find(channel);
          if (found == peers.end())
            throw std::invalid_argument("Unknown peer");
          auto reply = session.receive(found->second, unhex(data));
          if (!reply.empty())
            send(channel, reply);
        } catch (...) {
          close(channel);
        }
      } else if (kind == 'T') {
        // Gateway wakeups are not a trusted simulation clock. C++ owns time.
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        time = std::chrono::duration<double>(now).count();
        auto ready = session.readyPeers();
        arena.sync(ready);
        for (auto &c : session.takeCommands())
          try {
            arena.command(c);
          } catch (...) {
            for (auto [ch, p] : peers)
              if (p == c.peer) {
                close(ch);
                break;
              }
          }
        app.advance(time);
        if (++frame % 3 == 0) {
          auto records = arena.records();
          auto snapshot = session.snapshot(records);
          for (auto [ch, p] : peers)
            if (std::find(ready.begin(), ready.end(), p) != ready.end())
              send(ch, snapshot);
        }
      } else
        throw std::invalid_argument("Bridge command");
      std::cout << std::flush;
    }
    app.shutdown();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
