#pragma once
#include "engine/rpg/characters.hpp"

namespace engine::rpg {
struct AttributeRequirement { ContentId attribute; Amount minimum; };
// Base attributes deliberately exclude temporary/status/equipment bonuses.
struct BuildRequirements {
    std::uint32_t minimumLevel = 0;
    std::vector<AttributeRequirement> attributes;
    std::vector<ContentId> unlocks;
};
struct EquipmentRule {
    ContentId item;
    BuildRequirements requirements;
    // Occupied in addition to the selected anchor slot; duplicates coalesce.
    std::vector<ContentId> requiredSlots;
};
struct UnlockChoice {
    ContentId id;
    std::uint32_t cost = 1;
    BuildRequirements requirements;
    std::optional<ContentId> exclusiveGroup;
};
struct ProgressionChoices {
    ContentId progression;
    std::uint32_t pointsPerLevel = 1;
    std::vector<UnlockChoice> choices;
};
// Replace the complete player-selected set; empty means respec to no choices.
struct SetProgressionChoices { EntityId entity; std::vector<ContentId> choices; };
struct ChoiceResult { EntityId entity; std::vector<ContentId> selected; std::uint64_t remainingPoints; };
class Catalog;
struct State;
struct ItemState;
const EquipmentRule* equipmentRule(const Catalog&, const ContentId&) noexcept;
const ProgressionChoices* progressionChoices(const Catalog&, const ContentId&) noexcept;
bool meetsRequirements(const Catalog&, const CharacterState&, const BuildRequirements&);
std::vector<ContentId> occupiedEquipmentSlots(const Catalog&, const ItemState&);
std::uint64_t remainingChoicePoints(const Catalog&, const State&, EntityId);
} // namespace engine::rpg
