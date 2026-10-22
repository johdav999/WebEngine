#pragma once
#include "engine/quests/bindings.hpp"
#include <set>
#include <span>

namespace engine::quests {
enum class Status { Locked, Available, Active, Completed, Failed, Cancelled };
struct Progress {
    Status status = Status::Locked;
    bool earned = false;
    std::uint64_t activation = 0, elapsedMs = 0;
    std::string stage, choice;
    std::vector<std::string> path;
    std::map<std::string, std::uint32_t> counts;
};
struct Snapshot {
    std::string catalog, content, fingerprint;
    std::uint64_t generation = 1, revision = 0;
    Fields facts;
    std::map<std::string, Progress> quests;
    std::map<std::string, std::uint64_t> sequences;
    std::set<std::string> receipts;
};
// Canonical bounded little-endian wire formats; not raw C++ object memory.
std::vector<std::uint8_t> encodeCatalog(const CatalogDefinition &);
std::shared_ptr<const Catalog> decodeCatalog(std::span<const std::uint8_t>, BindingSnapshot);
std::vector<std::uint8_t> encodeSnapshot(const Catalog &, const Snapshot &);
Snapshot decodeSnapshot(const Catalog &, std::span<const std::uint8_t>);
void validateSnapshot(const Catalog &, const Snapshot &);
} // namespace engine::quests
