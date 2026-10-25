#include "engine/rpg/catalog.hpp"
#include <algorithm>
#include <set>

namespace engine::rpg {
namespace {
void valid(bool ok, const char* message) {
    if (!ok) throw Error(ErrorCode::InvalidDefinition, message);
}
template<class T> void sortUnique(std::vector<T>& entries) {
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (std::size_t i = 1; i < entries.size(); ++i)
        if (entries[i - 1].id == entries[i].id) throw Error(ErrorCode::DuplicateDefinition, "Duplicate authored ID");
}
template<class T> const T* lookup(const std::vector<T>& entries, const ContentId& id) noexcept {
    auto found = std::lower_bound(entries.begin(), entries.end(), id,
        [](const T& entry, const ContentId& key) { return entry.id < key; });
    return found != entries.end() && found->id == id ? &*found : nullptr;
}
}
void Bindings::addAdvancementHandler(ContentId id, AdvancementHandler handler) {
    if (handlers_.contains(id) || advancement_.contains(id) || abilities_.contains(id) || encounters_.contains(id))
        throw Error(ErrorCode::DuplicateHandler, "Duplicate RPG handler");
    if (size() >= Limits::handlers) throw Error(ErrorCode::Limit, "Too many RPG handlers");
    if (!handler.version) throw Error(ErrorCode::InvalidVersion, "Handler version must be positive");
    if (!handler.evaluate) throw Error(ErrorCode::MissingValidator, "Missing advancement evaluator");
    advancement_.emplace(std::move(id), std::move(handler));
}
const AdvancementHandler* Bindings::findAdvancement(const ContentId& id) const noexcept {
    auto found = advancement_.find(id);
    return found == advancement_.end() ? nullptr : &found->second;
}
void validateCharacters(CatalogDefinition& catalog, const Bindings& bindings) {
    std::size_t total = catalog.definitions.size();
    auto count = [&](std::size_t size) {
        if (size > Limits::definitions - total) throw Error(ErrorCode::Limit, "Too many catalogue records");
        total += size;
    };
    count(catalog.characters.size()); count(catalog.progressions.size());
    count(catalog.items.size()); count(catalog.inventories.size());
    count(catalog.abilities.size()); count(catalog.statuses.size());
    for (const auto& a : catalog.abilities) { count(a.parameters.size()); count(a.costs.size()); }
    for (const auto& s : catalog.statuses) { count(s.modifiers.size()); count(s.tickEffects.size()); }
    for (const auto& item : catalog.items) { count(item.slots.size()); count(item.modifiers.size()); }
    for (const auto& inventory : catalog.inventories) count(inventory.slots.size());
    sortUnique(catalog.characters); sortUnique(catalog.progressions);
    std::set<ContentId> global;
    auto insert = [&](const ContentId& id) {
        if (!global.insert(id).second) throw Error(ErrorCode::DuplicateDefinition, "Duplicate global definition ID");
    };
    for (const auto& def : catalog.definitions) insert(def.id);
    for (const auto& def : catalog.items) insert(def.id);
    for (const auto& def : catalog.inventories) insert(def.id);
    for (const auto& def : catalog.abilities) insert(def.id);
    for (const auto& def : catalog.statuses) insert(def.id);
    for (auto& def : catalog.progressions) {
        insert(def.id);
        if (def.thresholds.size() > Limits::thresholds || def.allowedUnlocks.size() > Limits::unlocks)
            throw Error(ErrorCode::Limit, "Progression definition too large");
        count(def.thresholds.size()); count(def.allowedUnlocks.size());
        valid(def.scale > 0 && def.maximumExperience >= 0, "Invalid XP units or maximum");
        std::int64_t last = 0;
        for (auto threshold : def.thresholds) {
            valid(threshold > last && threshold <= def.maximumExperience, "XP thresholds must increase within cap");
            last = threshold;
        }
        std::sort(def.allowedUnlocks.begin(), def.allowedUnlocks.end());
        if (std::adjacent_find(def.allowedUnlocks.begin(), def.allowedUnlocks.end()) != def.allowedUnlocks.end())
            throw Error(ErrorCode::DuplicateDefinition, "Duplicate unlock ID");
        if (def.handler) {
            const auto* handler = bindings.findAdvancement(def.handler->id);
            if (!handler) throw Error(ErrorCode::MissingHandler, "Missing advancement handler");
            if (def.handler->version != handler->version)
                throw Error(ErrorCode::HandlerVersionMismatch, "Advancement handler version mismatch");
        }
    }
    for (auto& def : catalog.characters) {
        insert(def.id);
        if (def.attributes.size() > Limits::values || def.resources.size() > Limits::values)
            throw Error(ErrorCode::Limit, "Too many character values");
        count(def.attributes.size()); count(def.resources.size());
        sortUnique(def.attributes); sortUnique(def.resources);
        for (const auto& value : def.attributes)
            valid(value.scale > 0 && value.minimum <= value.initial && value.initial <= value.maximum,
                  "Invalid attribute bounds, initial value or scale");
        for (const auto& value : def.resources) {
            valid(!lookup(def.attributes, value.id), "Attribute/resource IDs must be distinct within character");
            valid(value.scale > 0 && value.minimumMaximum >= 0 &&
                value.minimumMaximum <= value.initialMaximum && value.initialMaximum <= value.maximumMaximum &&
                value.initialCurrent >= 0 && value.initialCurrent <= value.initialMaximum,
                "Invalid resource bounds, initial values or scale");
        }
        if (def.progression && !lookup(catalog.progressions, *def.progression))
            throw Error(ErrorCode::MissingDefinition, "Missing character progression");
    }
}
const CharacterDefinition* Catalog::character(const ContentId& id) const noexcept {
    return lookup(definition_.characters, id);
}
const ProgressionDefinition* Catalog::progression(const ContentId& id) const noexcept {
    return lookup(definition_.progressions, id);
}
} // namespace engine::rpg
