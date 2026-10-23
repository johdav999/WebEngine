#pragma once
#include "engine/network.hpp"
#include <algorithm>
#include <functional>
#include <stdexcept>
namespace engine::net {
// Owner-thread opaque state replay. The application supplies a pure
// deterministic step; callbacks are borrowed only for the synchronous call. No
// clock or scene.
class PredictionHistory {
public:
  using Step = std::function<Bytes(const Bytes &, const Bytes &)>;
  struct Input {
    std::uint32_t sequence;
    Bytes data;
  };
  static constexpr std::size_t capacity = 128;
  void reset() { *this = PredictionHistory{}; }
  bool ready() const { return ready_; }
  bool full() const { return pending_.size() == capacity; }
  const Bytes &state() const { return state_; }
  std::size_t pending() const { return pending_.size(); }
  std::uint32_t acknowledged() const { return ack_; }
  void push(std::uint32_t sequence, Bytes input, const Step &step) {
    if (!ready_ || full() || issued_ == UINT32_MAX || sequence != issued_ + 1 ||
        input.size() > maxPayload)
      throw std::invalid_argument("Prediction input/sequence/budget");
    auto next = step(state_, input);
    check(next);
    pending_.push_back({sequence, std::move(input)});
    state_ = std::move(next);
    issued_ = sequence;
  }
  void reconcile(std::uint32_t ack, Bytes authority, const Step &step) {
    if (ack < ack_ || ack > issued_)
      throw std::invalid_argument("Prediction acknowledgement");
    check(authority);
    auto retained = pending_;
    std::erase_if(retained, [&](const Input &i) { return i.sequence <= ack; });
    for (const auto &i : retained) {
      authority = step(authority, i.data);
      check(authority);
    }
    state_ = std::move(authority);
    pending_ = std::move(retained);
    ack_ = ack;
    ready_ = true;
  }

private:
  static void check(const Bytes &b) {
    if (b.empty() || b.size() > maxPayload)
      throw std::invalid_argument("Prediction state budget");
  }
  Bytes state_;
  std::vector<Input> pending_;
  std::uint32_t issued_ = 0, ack_ = 0;
  bool ready_ = false;
};
} // namespace engine::net
