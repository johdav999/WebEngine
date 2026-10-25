#pragma once
#include "engine/rpg/characters.hpp"

namespace engine::rpg {
// Read-only through Runtime/Transaction. This is NOT a wire/save schema.
struct State {
    std::uint64_t generation = 1, revision = 0, nextEntity = 1;
    std::map<EntityId, CharacterState> characters;
    std::map<ContentId, std::uint64_t> producers;
};
struct AdvancementResult {
    EntityId entity;
    std::uint32_t fromLevel, toLevel;
    std::vector<ContentId> granted;
};
struct Results {
    std::vector<EntityId> created;
    std::vector<AdvancementResult> advancements;
};
} // namespace engine::rpg
