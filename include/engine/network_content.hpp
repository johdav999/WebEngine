#pragma once
#include "engine/network.hpp"

namespace engine::net {
// Reliable ordered bootstrap stream, separate from session messages. One owner
// thread; callers own timeout/rate policy and publish only complete content.
inline constexpr std::size_t maxContent = 4 * 1024 * 1024;
inline constexpr std::size_t contentChunkSize = 16384;
Bytes contentRequest();
Bytes contentChunk(std::span<const std::uint8_t> content, std::size_t offset);
bool isContent(std::span<const std::uint8_t> packet);
class ContentReceiver {
public:
  void receive(std::span<const std::uint8_t> packet);
  bool complete() const { return total_ && bytes_.size() == total_; }
  const Bytes &bytes() const { return bytes_; }
  void reset() {
    bytes_.clear();
    total_ = 0;
  }

private:
  Bytes bytes_;
  std::size_t total_ = 0;
};
} // namespace engine::net
