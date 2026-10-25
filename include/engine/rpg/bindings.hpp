#pragma once
#include "engine/rpg/types.hpp"
#include <functional>
#include <map>

namespace engine::rpg {
// Metadata only in R1; domain payloads and rules arrive in later milestones.
struct Definition {
    ContentId id;
    ContentId handler;
    std::uint32_t handlerVersion = 1;
};
struct DefinitionHandler {
    std::uint32_t version = 1;
    std::function<void(const Definition&)> validate;
};
class Bindings {
public:
    // Installation-only mutation. Published ModuleSnapshot exposes const access.
    void addDefinitionHandler(ContentId id, DefinitionHandler handler);
    const DefinitionHandler* find(const ContentId& id) const noexcept;
    std::size_t size() const noexcept { return handlers_.size(); }
private:
    std::map<ContentId, DefinitionHandler> handlers_;
};
using Composition = ModuleComposition<Bindings>;
using BindingsHandle = Composition::Handle;
ModuleRegistration<Bindings> moduleRegistration();
} // namespace engine::rpg
