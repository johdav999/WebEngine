#pragma once
#include "engine/rpg/characters.hpp"
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
struct AdvancementHandler {
    std::uint32_t version = 1;
    std::function<UnlockProposal(const Advancement&)> evaluate;
};
class Bindings {
public:
    // Installation-only mutation. Published ModuleSnapshot exposes const access.
    void addDefinitionHandler(ContentId id, DefinitionHandler handler);
    const DefinitionHandler* find(const ContentId& id) const noexcept;
    std::size_t size() const noexcept { return handlers_.size() + advancement_.size(); }
    void addAdvancementHandler(ContentId, AdvancementHandler);
    const AdvancementHandler* findAdvancement(const ContentId&) const noexcept;
private:
    std::map<ContentId, DefinitionHandler> handlers_;
    std::map<ContentId, AdvancementHandler> advancement_;
};
using Composition = ModuleComposition<Bindings>;
using BindingsHandle = Composition::Handle;
ModuleRegistration<Bindings> moduleRegistration();
} // namespace engine::rpg
