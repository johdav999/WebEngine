#pragma once
#include "engine/rpg/catalog.hpp"

namespace engine::rpg {
// Owner-thread session scaffold. No ticking, rules, save codec or scene ownership.
class Runtime {
public:
    Runtime(std::shared_ptr<const Catalog>, SessionId);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    const Catalog& catalog() const noexcept { return *catalog_; }
    SessionId session() const noexcept { return ids_.session(); }
    std::uint64_t nextEntity() const noexcept { return ids_.next(); }
    EntityId allocateEntityId() { return ids_.allocate(); }
private:
    // Session-owned state dies before its catalogue/providers.
    std::shared_ptr<const Catalog> catalog_;
    EntityIds ids_;
};
} // namespace engine::rpg
