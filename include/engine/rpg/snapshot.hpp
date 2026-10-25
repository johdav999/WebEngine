#pragma once
#include "engine/rpg/catalog.hpp"
#include "engine/rpg/state.hpp"

namespace engine::rpg {
inline constexpr std::uint64_t snapshotVersion = 1;
inline constexpr std::size_t snapshotByteLimit = 16 * 1024 * 1024;
struct Snapshot {
    SessionId session;
    State state;
    std::vector<std::uint8_t> catalog; // Exact canonical compatibility descriptor, not callbacks.
};
// Canonical portable bytes. Catalogue decoding retains and validates supplied bindings.
std::vector<std::uint8_t> encodeCatalog(const Catalog&);
std::shared_ptr<const Catalog> decodeCatalog(std::span<const std::uint8_t>, BindingsHandle);
void validateSnapshot(const Catalog&, const Snapshot&);
std::vector<std::uint8_t> encodeSnapshot(const Catalog&, const Snapshot&);
Snapshot decodeSnapshot(const Catalog&, std::span<const std::uint8_t>);
} // namespace engine::rpg
