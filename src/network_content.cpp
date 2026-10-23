#include "engine/network_content.hpp"
#include <algorithm>
#include <stdexcept>
namespace engine::net {
namespace {
constexpr std::uint32_t magic = 0x32434557; // WEC2
void put(Bytes &b, std::uint32_t n) {
  for (int i = 0; i < 4; ++i)
    b.push_back(std::uint8_t(n >> (i * 8)));
}
std::uint32_t get(std::span<const std::uint8_t> b, std::size_t p) {
  if (p + 4 > b.size())
    throw std::invalid_argument("Content header truncated");
  std::uint32_t n = 0;
  for (int i = 0; i < 4; ++i)
    n |= std::uint32_t(b[p + i]) << (i * 8);
  return n;
}
} // namespace
bool isContent(std::span<const std::uint8_t> b) {
  return b.size() >= 4 && get(b, 0) == magic;
}
Bytes contentRequest() {
  Bytes b;
  put(b, magic);
  put(b, 0);
  put(b, 0);
  return b;
}
Bytes contentChunk(std::span<const std::uint8_t> b, std::size_t offset) {
  if (b.empty() || b.size() > maxContent || offset >= b.size())
    throw std::invalid_argument("Content bounds");
  Bytes out;
  put(out, magic);
  put(out, std::uint32_t(b.size()));
  put(out, std::uint32_t(offset));
  auto part = b.subspan(offset, std::min(contentChunkSize, b.size() - offset));
  out.insert(out.end(), part.begin(), part.end());
  return out;
}
void ContentReceiver::receive(std::span<const std::uint8_t> b) {
  const auto total = get(b, 4), offset = get(b, 8);
  if (!isContent(b) || !total || total > maxContent ||
      offset != bytes_.size() || (total_ && total != total_) ||
      offset >= total ||
      b.size() !=
          12 + std::min(contentChunkSize, std::size_t(total - offset)) ||
      complete())
    throw std::invalid_argument("Invalid or out-of-order content chunk");
  // Reserve before changing the accepted stream state.
  bytes_.reserve(total);
  bytes_.insert(bytes_.end(), b.begin() + 12, b.end());
  total_ = total;
}
} // namespace engine::net
