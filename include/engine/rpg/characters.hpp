#pragma once
#include "engine/rpg/types.hpp"
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace engine::rpg {
struct Amount { std::int64_t value; ContentId unit; std::int64_t scale = 1; };
struct AttributeDefinition {
    ContentId id, unit;
    std::int64_t scale = 1, minimum = 0, maximum = 100, initial = 0;
};
struct ResourceDefinition {
    ContentId id, unit;
    std::int64_t scale = 1, minimumMaximum = 0, maximumMaximum = 100,
                 initialMaximum = 100, initialCurrent = 100;
};
struct HandlerReference { ContentId id; std::uint32_t version = 1; };
struct ProgressionDefinition {
    ContentId id, unit;
    std::int64_t scale = 1, maximumExperience = 0;
    // Level zero initially; each strictly increasing positive threshold adds one.
    std::vector<std::int64_t> thresholds;
    std::vector<ContentId> allowedUnlocks;
    std::optional<HandlerReference> handler;
};
struct CharacterDefinition {
    ContentId id;
    std::vector<AttributeDefinition> attributes;
    std::vector<ResourceDefinition> resources;
    std::optional<ContentId> progression;
};
enum class ModifierTarget { Attribute, ResourceMaximum };
struct Modifier { ModifierTarget kind; ContentId target; Amount delta; };
struct AttributeState { std::int64_t base = 0, value = 0; };
struct ResourceState { std::int64_t baseMaximum = 0, maximum = 0, current = 0; };
struct CharacterState {
    ContentId definition;
    std::map<ContentId, AttributeState> attributes;
    std::map<ContentId, ResourceState> resources;
    std::map<ContentId, std::vector<Modifier>> modifiers;
    std::int64_t experience = 0;
    std::uint32_t level = 0;
    std::set<ContentId> unlocks;
};
struct Advancement {
    EntityId entity;
    const CharacterState& character;
    std::uint32_t fromLevel, toLevel;
};
struct UnlockProposal { bool accepted = true; std::vector<ContentId> unlocks; };
} // namespace engine::rpg
