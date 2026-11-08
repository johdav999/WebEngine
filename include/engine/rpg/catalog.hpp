#pragma once
#include "engine/rpg/bindings.hpp"
#include "engine/rpg/inventory.hpp"
#include "engine/rpg/builds.hpp"
#include <memory>
#include <span>
#include <vector>

namespace engine::rpg {
struct CatalogDefinition {
    ContentId id;
    ContentRevision revision;
    std::vector<Definition> definitions;
    std::vector<CharacterDefinition> characters;
    std::vector<ProgressionDefinition> progressions;
    std::vector<ItemDefinition> items;
    std::vector<InventoryDefinition> inventories;
    std::vector<AbilityDefinition> abilities;
    std::vector<StatusDefinition> statuses;
    std::vector<EquipmentRule> equipmentRules;
    std::vector<ProgressionChoices> choiceRules;
};
class Catalog {
public:
    static std::shared_ptr<const Catalog> create(CatalogDefinition, BindingsHandle);
    const ContentId& id() const noexcept { return definition_.id; }
    ContentRevision revision() const noexcept { return definition_.revision; }
    std::span<const Definition> definitions() const noexcept { return definition_.definitions; }
    const Definition* find(const ContentId&) const noexcept;
    const CharacterDefinition* character(const ContentId&) const noexcept;
    const ProgressionDefinition* progression(const ContentId&) const noexcept;
    const AbilityDefinition* ability(const ContentId&) const noexcept;
    const StatusDefinition* status(const ContentId&) const noexcept;
    const ItemDefinition* item(const ContentId&) const noexcept;
    const InventoryDefinition* inventory(const ContentId&) const noexcept;
    const BindingsHandle& bindings() const noexcept { return bindings_; }
    const CatalogDefinition& definition() const noexcept { return definition_; }
private:
    Catalog(CatalogDefinition definition, BindingsHandle bindings)
        : bindings_(std::move(bindings)), definition_(std::move(definition)) {}
    // Definitions are destroyed before provider captures retained by bindings.
    BindingsHandle bindings_;
    CatalogDefinition definition_;
};
} // namespace engine::rpg
