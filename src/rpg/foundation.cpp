#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace engine::rpg {
void validateCharacters(CatalogDefinition&, const Bindings&);
void validateInventory(CatalogDefinition&);
void validateAbilities(CatalogDefinition&, const Bindings&);
ContentId::ContentId(std::string value) : value_(std::move(value)) {
    if (value_.size() > Limits::keyBytes) throw Error(ErrorCode::Limit, "RPG key exceeds 128 bytes");
    bool dot = false, segment = false;
    for (const unsigned char c : value_) {
        if (c == '.') {
            if (!segment) throw Error(ErrorCode::InvalidId, "Empty RPG key segment");
            dot = true;
            segment = false;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
                throw Error(ErrorCode::InvalidId, "RPG keys require lowercase ASCII namespaces");
            segment = true;
        }
    }
    if (!dot || !segment) throw Error(ErrorCode::InvalidId, "RPG keys require nonempty dotted namespaces");
}
ContentRevision::ContentRevision(std::uint64_t value) : value_(value) {
    if (!value) throw Error(ErrorCode::InvalidVersion, "Content revision must be positive");
}
SessionId::SessionId(std::uint64_t value) : value_(value) {
    if (!value) throw Error(ErrorCode::InvalidSession, "Session namespace must be nonzero");
}
EntityId EntityIds::allocate() {
    if (!next_) throw Error(ErrorCode::ExhaustedIdentity, "Entity identity space exhausted");
    const EntityId result{session_, next_};
    next_ = next_ == std::numeric_limits<std::uint64_t>::max() ? 0 : next_ + 1;
    return result;
}
void Bindings::addDefinitionHandler(ContentId id, DefinitionHandler handler) {
    if (handlers_.contains(id) || advancement_.contains(id) || abilities_.contains(id) || encounters_.contains(id)) throw Error(ErrorCode::DuplicateHandler, "Duplicate RPG handler: " + id.value());
    if (size() >= Limits::handlers) throw Error(ErrorCode::Limit, "Too many RPG handlers");
    if (!handler.version) throw Error(ErrorCode::InvalidVersion, "Handler version must be positive");
    if (!handler.validate) throw Error(ErrorCode::MissingValidator, "Missing RPG definition validator");
    handlers_.emplace(std::move(id), std::move(handler));
}
const DefinitionHandler* Bindings::find(const ContentId& id) const noexcept {
    const auto found = handlers_.find(id);
    return found == handlers_.end() ? nullptr : &found->second;
}
ModuleRegistration<Bindings> moduleRegistration() {
    ModuleDescriptor descriptor;
    descriptor.id = "webengine.rpg";
    descriptor.version = apiVersion;
    // No content or snapshot wire schema is claimed by this foundation.
    return {std::move(descriptor), [](Bindings&) {}};
}
std::shared_ptr<const Catalog> Catalog::create(CatalogDefinition definition, BindingsHandle bindings) {
    if (!bindings) throw Error(ErrorCode::MissingModule, "RPG catalogue requires retained bindings");
    bool registered = false;
    for (const auto& descriptor : bindings->descriptors()) {
        if (descriptor.id == "webengine.rpg") {
            if (descriptor.version != apiVersion)
                throw Error(ErrorCode::InvalidVersion, "Unsupported RPG module API version");
            registered = true;
        }
    }
    if (!registered) throw Error(ErrorCode::MissingModule, "Compose webengine.rpg before creating a catalogue");
    if (definition.definitions.size() > Limits::definitions)
        throw Error(ErrorCode::Limit, "Too many RPG definitions");
    auto& entries = definition.definitions;
    std::sort(entries.begin(), entries.end(), [](const Definition& a, const Definition& b) { return a.id < b.id; });
    // Validate the complete structure before calling any provider.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (i && entries[i - 1].id == entry.id)
            throw Error(ErrorCode::DuplicateDefinition, "Duplicate RPG definition: " + entry.id.value());
        const auto* handler = bindings->registry().find(entry.handler);
        if (!handler) throw Error(ErrorCode::MissingHandler, "Missing RPG handler: " + entry.handler.value());
        if (entry.handlerVersion != handler->version)
            throw Error(ErrorCode::HandlerVersionMismatch, "RPG definition handler version mismatch");
    }
    validateCharacters(definition, bindings->registry());
    validateInventory(definition);
    validateAbilities(definition, bindings->registry());
    for (const auto& entry : entries) bindings->registry().find(entry.handler)->validate(entry);
    for (const auto& ability : definition.abilities)
        bindings->registry().findAbility(ability.handler.id)->validate(ability);
    return std::shared_ptr<const Catalog>(new Catalog(std::move(definition), std::move(bindings)));
}
const Definition* Catalog::find(const ContentId& id) const noexcept {
    const auto& entries = definition_.definitions;
    auto found = std::lower_bound(entries.begin(), entries.end(), id,
        [](const Definition& entry, const ContentId& key) { return entry.id < key; });
    return found != entries.end() && found->id == id ? &*found : nullptr;
}
} // namespace engine::rpg
