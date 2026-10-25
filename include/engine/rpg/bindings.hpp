#pragma once
#include "engine/rpg/abilities.hpp"
#include "engine/rpg/encounters.hpp"
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
struct AbilityHandler {
    std::uint32_t version = 1, schemaVersion = 1;
    std::function<void(const AbilityDefinition&)> validate;
    std::function<EffectProposal(const AbilityContext&, Random&)> evaluate;
};
class Bindings {
public:
    // Installation-only mutation. Published ModuleSnapshot exposes const access.
    void addDefinitionHandler(ContentId id, DefinitionHandler handler);
    const DefinitionHandler* find(const ContentId& id) const noexcept;
    std::size_t size() const noexcept { return handlers_.size() + advancement_.size() + abilities_.size() + encounters_.size(); }
    void addAbilityHandler(ContentId, AbilityHandler);
    const AbilityHandler* findAbility(const ContentId&) const noexcept;
    void addAdvancementHandler(ContentId, AdvancementHandler);
    const AdvancementHandler* findAdvancement(const ContentId&) const noexcept;
    void addEncounterHandler(ContentId, EncounterHandler);
    const EncounterHandler* findEncounter(const ContentId&) const noexcept;
private:
    std::map<ContentId, EncounterHandler> encounters_;
    std::map<ContentId, DefinitionHandler> handlers_;
    std::map<ContentId, AdvancementHandler> advancement_;
    std::map<ContentId, AbilityHandler> abilities_;
};
using Composition = ModuleComposition<Bindings>;
using BindingsHandle = Composition::Handle;
ModuleRegistration<Bindings> moduleRegistration();
} // namespace engine::rpg
