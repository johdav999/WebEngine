#pragma once
#include "engine/rpg/inventory.hpp"
#include "engine/rpg/abilities.hpp"
#include "engine/rpg/encounters.hpp"

namespace engine::rpg {
// Read-only through Runtime/Transaction. This is NOT a wire/save schema.
struct State {
    std::uint64_t generation = 1, revision = 0, nextEntity = 1;
    std::map<EntityId, CharacterState> characters;
    std::map<ContentId, std::uint64_t> producers;
    std::uint64_t nextItem = 1;
    std::map<EntityId, InventoryState> inventories;
    std::map<ItemId, ItemState> items;
    RandomState random = seedRandom(0);
    std::uint64_t phaseEpoch = 0;
    std::uint64_t encounterEpoch = 0, turnEpoch = 0;
    std::optional<EncounterState> encounter;
    std::map<EntityId, TacticalPosition> positions;
    std::uint64_t actionEpoch = 0;
    std::optional<PendingPresentation> presentation;
    std::map<EntityId, std::map<ContentId, CooldownState>> cooldowns;
};
struct AdvancementResult {
    EntityId entity;
    std::uint32_t fromLevel, toLevel;
    std::vector<ContentId> granted;
};
struct Results {
    std::vector<EntityId> created;
    std::vector<AdvancementResult> advancements;
    std::vector<EntityId> inventories;
    std::vector<ItemId> items;
    std::vector<ResourceEffect> resources;
    std::vector<StatusChange> statuses;
    std::vector<AbilityResult> abilities;
    std::vector<EncounterResult> encounters;
    std::vector<CooldownResult> cooldowns;
    std::vector<TacticalResult> actions;
    std::vector<ReactionResult> reactions;
    std::vector<PresentationResult> presentations;
};
} // namespace engine::rpg
