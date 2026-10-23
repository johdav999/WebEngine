#pragma once
#include "engine/network.hpp"
#include <functional>

namespace engine::net {
enum class InvocationDirection : std::uint32_t { ServerRpc, ClientEvent };
struct InvocationType {
  std::uint32_t id = 0, version = 1;
  std::string key;
  InvocationDirection direction = InvocationDirection::ServerRpc;
  std::size_t maxBytes = 64;
  // Pure validation: no dispatch, live mutation or retained payload borrows.
  std::function<void(std::span<const std::uint8_t>)> validate;
};
// Optional owning schema catalogue. Callback captures must outlive its copies.
// Dispatch/recipient/game policy remains on the host's owner thread.
class InvocationRegistry {
public:
  void add(InvocationType);
  const InvocationType *find(std::uint32_t) const;
  void validate(std::uint32_t, InvocationDirection,
                std::span<const std::uint8_t>) const;
  const auto &entries() const { return entries_; }
  Bytes manifest() const;
private:
  std::vector<InvocationType> entries_;
};
} // namespace engine::net
