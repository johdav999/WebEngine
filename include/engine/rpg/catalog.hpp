#pragma once
#include "engine/rpg/bindings.hpp"
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
    const BindingsHandle& bindings() const noexcept { return bindings_; }
private:
    Catalog(CatalogDefinition definition, BindingsHandle bindings)
        : bindings_(std::move(bindings)), definition_(std::move(definition)) {}
    // Definitions are destroyed before provider captures retained by bindings.
    BindingsHandle bindings_;
    CatalogDefinition definition_;
};
} // namespace engine::rpg
