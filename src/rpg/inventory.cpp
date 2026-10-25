#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>

namespace engine::rpg {
void recomputeEquipment(CharacterState&, const CharacterDefinition&);
namespace {
void need(bool ok, ErrorCode code, const char* text) { if (!ok) throw Error(code, text); }
template<class T> const T* lookup(const std::vector<T>& entries, const ContentId& id) noexcept {
    auto it = std::lower_bound(entries.begin(), entries.end(), id,
        [](const T& entry, const ContentId& key) { return entry.id < key; });
    return it != entries.end() && it->id == id ? &*it : nullptr;
}
void slots(std::vector<ContentId>& values) {
    need(values.size() <= Limits::equipmentSlots, ErrorCode::Limit, "Too many equipment slots");
    std::sort(values.begin(), values.end());
    need(std::adjacent_find(values.begin(), values.end()) == values.end(),
        ErrorCode::DuplicateDefinition, "Duplicate equipment slot");
}
InventoryState& inventory(State& state, EntityId id) {
    auto it = state.inventories.find(id);
    need(it != state.inventories.end(), ErrorCode::MissingInventory, "Unknown inventory");
    return it->second;
}
ItemState& item(State& state, ItemId id, EntityId owner) {
    auto it = state.items.find(id);
    need(it != state.items.end(), ErrorCode::MissingItem, "Unknown or retired item");
    need(it->second.inventory == owner, ErrorCode::InvalidOwnership, "Item belongs to another inventory");
    return it->second;
}
void bag(const ItemState& value) {
    need(!value.slot, ErrorCode::InvalidOwnership, "Unequip item before moving, splitting, merging or removing");
}
void quantity(std::int64_t value, std::int64_t maximum) {
    need(value > 0 && value <= maximum, ErrorCode::InvalidQuantity, "Quantity outside stack bounds");
}
void capacity(const State& state, const Catalog& catalog, EntityId owner) {
    std::size_t count = 0;
    for (const auto& [id, value] : state.items) if (value.inventory == owner && !value.slot) ++count;
    need(count <= catalog.inventory(state.inventories.at(owner).definition)->capacity,
        ErrorCode::Capacity, "Inventory has no free stack slot");
}
ItemId allocateItem(State& state, SessionId session) {
    need(state.items.size() < Limits::items, ErrorCode::Limit, "Item instance cap reached");
    EntityIds ids(session, state.nextItem);
    auto allocated = ids.allocate(); state.nextItem = ids.next();
    return {allocated.session, allocated.value};
}
void equipment(State& state, const Catalog& catalog, EntityId owner) {
    auto& inv = state.inventories.at(owner);
    need(inv.character.has_value(), ErrorCode::InvalidOwnership, "Equipment requires a character loadout");
    auto& character = state.characters.at(*inv.character);
    const auto& def = *catalog.character(character.definition);
    character.equipmentModifiers.clear();
    std::size_t total = 0;
    for (const auto& [source, entries] : character.modifiers) total += entries.size();
    for (const auto& [id, value] : state.items) {
        if (value.inventory != owner || !value.slot) continue;
        const auto& mods = catalog.item(value.definition)->modifiers;
        total += mods.size();
        need(total <= Limits::modifiers, ErrorCode::Limit, "Too many combined character modifiers");
        for (const auto& mod : mods) {
            if (mod.kind == ModifierTarget::Attribute) {
                const auto* target = lookup(def.attributes, mod.target);
                need(target != nullptr, ErrorCode::InvalidModifier, "Equipment attribute missing");
                need(target->unit == mod.delta.unit && target->scale == mod.delta.scale,
                    ErrorCode::InvalidAmount, "Equipment attribute unit mismatch");
            } else {
                const auto* target = lookup(def.resources, mod.target);
                need(target != nullptr, ErrorCode::InvalidModifier, "Equipment resource missing");
                need(target->unit == mod.delta.unit && target->scale == mod.delta.scale,
                    ErrorCode::InvalidAmount, "Equipment resource unit mismatch");
            }
        }
        if (!mods.empty()) character.equipmentModifiers.emplace(id, mods);
    }
    recomputeEquipment(character, def); // Once after displacement, never an intermediate clamp.
}
}
void validateInventory(CatalogDefinition& catalog) {
    std::sort(catalog.items.begin(), catalog.items.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(catalog.inventories.begin(), catalog.inventories.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (auto& def : catalog.items) {
        need(def.maximumStack > 0, ErrorCode::InvalidDefinition, "Stack maximum must be positive");
        slots(def.slots);
        need(def.modifiers.size() <= Limits::modifiers, ErrorCode::Limit, "Too many item modifiers");
        need(def.modifiers.empty() || !def.slots.empty(), ErrorCode::InvalidDefinition, "Modifiers need an equipment slot");
        std::sort(def.modifiers.begin(), def.modifiers.end(), [](const auto& a, const auto& b) {
            return a.kind == b.kind ? a.target < b.target : a.kind < b.kind;
        });
        for (std::size_t i = 0; i < def.modifiers.size(); ++i) {
            const auto& mod = def.modifiers[i];
            need(mod.delta.scale > 0 && (mod.kind == ModifierTarget::Attribute || mod.kind == ModifierTarget::ResourceMaximum),
                ErrorCode::InvalidDefinition, "Invalid item modifier kind or scale");
            need(!i || mod.kind != def.modifiers[i-1].kind || mod.target != def.modifiers[i-1].target,
                ErrorCode::InvalidDefinition, "Duplicate item modifier target");
        }
    }
    for (auto& def : catalog.inventories) {
        need(def.capacity <= Limits::inventorySlots, ErrorCode::Limit, "Inventory capacity exceeds hard cap");
        slots(def.slots);
    }
}
const ItemDefinition* Catalog::item(const ContentId& id) const noexcept { return lookup(definition_.items, id); }
const InventoryDefinition* Catalog::inventory(const ContentId& id) const noexcept { return lookup(definition_.inventories, id); }

void applyInventory(State& state, Results& results, const Catalog& catalog, SessionId session, const Operation& operation) {
    std::visit([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, CreateInventory>) {
            need(catalog.inventory(op.definition) != nullptr, ErrorCode::MissingDefinition, "Unknown inventory definition");
            need(state.inventories.size() < Limits::inventories, ErrorCode::Limit, "Inventory count exceeds hard cap");
            if (op.character) {
                need(state.characters.contains(*op.character), ErrorCode::MissingEntity, "Unknown loadout character");
                for (const auto& [id, inv] : state.inventories)
                    need(inv.character != op.character, ErrorCode::InvalidOwnership, "Character already has a loadout");
            }
            EntityIds ids(session, state.nextEntity); auto id = ids.allocate(); state.nextEntity = ids.next();
            state.inventories.emplace(id, InventoryState{op.definition, op.character});
            results.inventories.push_back(id);
        } else if constexpr (std::is_same_v<T, AddItem>) {
            inventory(state, op.inventory);
            const auto* def = catalog.item(op.definition);
            need(def != nullptr, ErrorCode::MissingDefinition, "Unknown item definition");
            quantity(op.quantity, def->maximumStack);
            auto id = allocateItem(state, session);
            state.items.emplace(id, ItemState{op.definition, op.quantity, op.inventory, {}});
            capacity(state, catalog, op.inventory); results.items.push_back(id);
        } else if constexpr (std::is_same_v<T, TransferItem>) {
            inventory(state, op.source); inventory(state, op.destination);
            need(op.source != op.destination, ErrorCode::InvalidOwnership, "Self transfer is invalid");
            auto& value = item(state, op.item, op.source); bag(value);
            value.inventory = op.destination; capacity(state, catalog, op.destination);
        } else if constexpr (std::is_same_v<T, MergeItems>) {
            inventory(state, op.inventory);
            need(op.source != op.destination, ErrorCode::IncompatibleStack, "Cannot merge a stack into itself");
            auto& source = item(state, op.source, op.inventory);
            auto& dest = item(state, op.destination, op.inventory); bag(source); bag(dest);
            need(source.definition == dest.definition, ErrorCode::IncompatibleStack, "Only identical definitions merge");
            need(source.quantity <= std::numeric_limits<std::int64_t>::max() - dest.quantity,
                ErrorCode::Overflow, "Stack quantity addition overflow");
            quantity(source.quantity + dest.quantity, catalog.item(dest.definition)->maximumStack);
            dest.quantity += source.quantity; state.items.erase(op.source);
        } else if constexpr (std::is_same_v<T, RemoveItem> || std::is_same_v<T, SplitItem>) {
            inventory(state, op.inventory);
            auto& value = item(state, op.item, op.inventory); bag(value);
            quantity(op.quantity, value.quantity);
            if constexpr (std::is_same_v<T, RemoveItem>) {
                value.quantity -= op.quantity;
                if (!value.quantity) state.items.erase(op.item);
            } else {
                need(op.quantity < value.quantity, ErrorCode::InvalidQuantity, "Split must leave a nonempty source");
                auto id = allocateItem(state, session);
                state.items.emplace(id, ItemState{value.definition, op.quantity, op.inventory, {}});
                value.quantity -= op.quantity;
                capacity(state, catalog, op.inventory); results.items.push_back(id);
            }
        } else if constexpr (std::is_same_v<T, EquipItem> || std::is_same_v<T, UnequipItem>) {
            auto& inv = inventory(state, op.inventory);
            auto& value = item(state, op.item, op.inventory);
            if constexpr (std::is_same_v<T, EquipItem>) {
                bag(value);
                need(value.quantity == 1, ErrorCode::InvalidQuantity, "Split equipment to a single instance first");
                const auto& allowed = catalog.item(value.definition)->slots;
                const auto& defined = catalog.inventory(inv.definition)->slots;
                need(std::binary_search(allowed.begin(), allowed.end(), op.slot) &&
                    std::binary_search(defined.begin(), defined.end(), op.slot), ErrorCode::InvalidSlot, "Ineligible equipment slot");
                value.slot = op.slot;
                const auto occupied = occupiedEquipmentSlots(catalog, value);
                for (const auto& slot : occupied)
                    need(std::binary_search(defined.begin(), defined.end(), slot), ErrorCode::InvalidSlot, "Missing required equipment slot");
                for (auto& [id, displaced] : state.items) {
                    if (id == op.item || displaced.inventory != op.inventory || !displaced.slot) continue;
                    const auto other = occupiedEquipmentSlots(catalog, displaced);
                    if (std::any_of(other.begin(), other.end(), [&](const auto& slot) {
                        return std::binary_search(occupied.begin(), occupied.end(), slot);
                    })) displaced.slot.reset();
                }
            } else {
                need(value.slot.has_value(), ErrorCode::InvalidSlot, "Item is not equipped");
                value.slot.reset();
            }
            capacity(state, catalog, op.inventory);
            equipment(state, catalog, op.inventory);
        }
    }, operation);
}
} // namespace engine::rpg
