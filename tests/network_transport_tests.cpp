#include "engine/network.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace engine::net;
using Clock = std::chrono::steady_clock;
template <class F> void until(F test) {
  const auto end = Clock::now() + std::chrono::seconds(8);
  while (!test()) {
    if (Clock::now() > end)
      throw std::runtime_error("Native transport timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
int main(int argc, char **argv) {
  try {
    const std::string endpoint = argc > 1 ? argv[1] : "ws://127.0.0.1:8177";
    PeerId previous = 0;
    for (int i = 0; i < 3; ++i) {
      auto connection = connectWebSocket(endpoint);
      ClientSession client(0x4d503031);
      until([&] { return connection->state() == ConnectionState::Open; });
      if (!connection->send(client.hello()))
        throw std::runtime_error("Hello rejected");
      until([&] {
        for (auto &b : connection->receive())
          client.receive(b);
        return client.ready() && !client.records().empty();
      });
      if (client.peer() <= previous)
        throw std::runtime_error("Reconnect reused peer");
      previous = client.peer();
      if (connection->send(Bytes(maxMessage + 1)))
        throw std::runtime_error("Oversize send accepted");
      // Duplicate Hello is a protocol error: the server closes only this
      // connection.
      connection->send(client.hello());
      until([&] {
        auto s = connection->state();
        return s == ConnectionState::Closed || s == ConnectionState::Failed;
      });
      connection.reset();
    }
    const auto start = Clock::now();
    for (int i = 0; i < 3; ++i) {
      auto pending = connectWebSocket(endpoint);
      pending.reset();
    }
    if (Clock::now() - start > std::chrono::seconds(10))
      throw std::runtime_error("Connecting cancellation too slow");
    std::cout << "native transport PASS: handshake, snapshots, remote close, "
                 "reconnect, budgets, pending cancellation\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
