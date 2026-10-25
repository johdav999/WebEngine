#pragma once
#include "engine/rpg/characters.hpp"

namespace engine::rpg {
// Quantities are whole items; maximumStack is positive signed 64-bit.
struct ItemDefinition {
    ContentId id;
    std::int64_t maximumStack = 1;
    std::vector<ContentId> slots;
    std::vector<Modifier> modifiers;
};
struct InventoryDefinition {
    ContentId id;
    std::size_t capacity = 0; // Unequipped stacks; equipped instances do not occupy bag space.
    std::vector<ContentId> slots;
};
struct InventoryState {
    ContentId definition;
    std::optional<EntityId> character; // At most one loadout per character.
};
struct ItemState {
    ContentId definition;
    std::int64_t quantity;
    EntityId inventory;
    std::optional<ContentId> slot;
};
struct CreateInventory { ContentId definition; std::optional<EntityId> character; };
struct AddItem { EntityId inventory; ContentId definition; std::int64_t quantity; };
struct RemoveItem { EntityId inventory; ItemId item; std::int64_t quantity; };
struct SplitItem { EntityId inventory; ItemId item; std::int64_t quantity; };
struct MergeItems { EntityId inventory; ItemId source, destination; };
struct TransferItem { EntityId source, destination; ItemId item; };
struct EquipItem { EntityId inventory; ItemId item; ContentId slot; };
struct UnequipItem { EntityId inventory; ItemId item; };
} // namespace engine::rpg
