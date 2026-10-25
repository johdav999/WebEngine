#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <type_traits>

namespace engine::rpg {
void recomputeEquipment(CharacterState&, const CharacterDefinition&);
void applyInventory(State&, Results&, const Catalog&, SessionId, const Operation&);
namespace {
void need(bool ok, ErrorCode code, const char* text) { if (!ok) throw Error(code, text); }
template<class T> const T* lookup(const std::vector<T>& entries, const ContentId& id) noexcept {
    auto it = std::lower_bound(entries.begin(), entries.end(), id,
        [](const T& entry, const ContentId& key) { return entry.id < key; });
    return it != entries.end() && it->id == id ? &*it : nullptr;
}
bool validPhase(TurnPhase phase) {
    return phase == TurnPhase::OwnerStart || phase == TurnPhase::OwnerEnd ||
        phase == TurnPhase::RoundStart || phase == TurnPhase::RoundEnd;
}
void spend(std::size_t& budget) {
    need(budget != 0, ErrorCode::BudgetExceeded, "Effect/phase budget exhausted"); --budget;
}
void amounts(std::vector<ResourceAmount>& values, bool cost) {
    need(values.size() <= Limits::effects, ErrorCode::Limit, "Too many resource amounts");
    std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.resource < b.resource; });
    for (std::size_t i = 0; i < values.size(); ++i) {
        need(values[i].amount.scale > 0 && (!cost || values[i].amount.value >= 0), ErrorCode::InvalidDefinition, "Invalid cost or scale");
        need(!i || values[i-1].resource != values[i].resource, ErrorCode::DuplicateDefinition, "Duplicate resource amount");
    }
}
CharacterState& character(State& state, EntityId id) {
    auto it = state.characters.find(id);
    need(it != state.characters.end(), ErrorCode::MissingEntity, "Unknown effect character"); return it->second;
}
void unit(const Amount& amount, const ContentId& unit, std::int64_t scale) {
    need(amount.unit == unit && amount.scale == scale, ErrorCode::InvalidAmount, "Effect unit/scale mismatch");
}
void resource(State& state, Results& results, const Catalog& catalog, const ResourceEffect& effect, std::size_t& budget) {
    spend(budget);
    auto& ch = character(state, effect.entity);
    const auto* def = lookup(catalog.character(ch.definition)->resources, effect.resource);
    need(def != nullptr, ErrorCode::MissingValue, "Unknown effect resource");
    unit(effect.delta, def->unit, def->scale);
    auto& value = ch.resources.at(effect.resource);
    const auto delta = effect.delta.value;
    need(!((delta > 0 && value.current > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && value.current < std::numeric_limits<std::int64_t>::min() - delta)),
        ErrorCode::Overflow, "Effect arithmetic overflow");
    const auto next = value.current + delta;
    need(next >= 0 && next <= value.maximum, ErrorCode::InvalidAmount, "Effect resource outside bounds");
    value.current = next; results.resources.push_back(effect);
}
void statusTargets(const StatusDefinition& status, const CharacterDefinition& def) {
    for (const auto& mod : status.modifiers) {
        if (mod.kind == ModifierTarget::Attribute) {
            const auto* target = lookup(def.attributes, mod.target);
            need(target != nullptr, ErrorCode::InvalidModifier, "Unknown status attribute");
            unit(mod.delta, target->unit, target->scale);
        } else {
            const auto* target = lookup(def.resources, mod.target);
            need(target != nullptr, ErrorCode::InvalidModifier, "Unknown status resource");
            unit(mod.delta, target->unit, target->scale);
        }
    }
    for (const auto& tick : status.tickEffects) {
        const auto* target = lookup(def.resources, tick.resource);
        need(target != nullptr, ErrorCode::MissingValue, "Unknown status tick resource");
        unit(tick.amount, target->unit, target->scale);
    }
}
void status(State& state, Results& results, const Catalog& catalog, const ApplyStatus& effect, std::size_t& budget) {
    spend(budget);
    auto& ch = character(state, effect.entity);
    const auto* def = catalog.status(effect.definition);
    need(def != nullptr, ErrorCode::MissingDefinition, "Unknown status definition");
    const auto& characterDef = *catalog.character(ch.definition);
    statusTargets(*def, characterDef);
    auto found = ch.statuses.find(effect.source);
    auto kind = StatusChangeKind::Applied;
    if (found == ch.statuses.end()) {
        need(ch.statuses.size() < Limits::statuses, ErrorCode::Limit, "Character status limit reached");
        std::size_t total = 0;
        for (const auto& [id, value] : state.characters) total += value.statuses.size();
        need(total < Limits::totalStatuses, ErrorCode::Limit, "Global status limit reached");
        ch.statuses.emplace(effect.source, StatusState{def->id, 1, def->duration, !def->includeApplicationPhase, def->modifiers});
    } else {
        auto& current = found->second;
        need(current.definition == def->id, ErrorCode::InvalidStatus, "Status source already names another definition");
        need(def->stacking != StatusStacking::Reject, ErrorCode::InvalidStatus, "Duplicate status source");
        if (def->stacking == StatusStacking::Stack) {
            need(current.stacks < def->maximumStacks, ErrorCode::Limit, "Status stack limit reached");
            ++current.stacks; kind = StatusChangeKind::Stacked;
        } else kind = StatusChangeKind::Refreshed;
        if (def->stacking == StatusStacking::Refresh || def->refreshOnStack) {
            current.remaining = def->duration; current.skipNext = !def->includeApplicationPhase;
        }
    }
    recomputeEquipment(ch, characterDef);
    results.statuses.push_back({effect.entity, effect.source, kind});
}
void dispel(State& state, Results& results, const Catalog& catalog, const DispelStatus& effect, std::size_t& budget) {
    spend(budget); auto& ch = character(state, effect.entity);
    need(ch.statuses.erase(effect.source) == 1, ErrorCode::InvalidStatus, "Unknown dispel source");
    recomputeEquipment(ch, *catalog.character(ch.definition));
    results.statuses.push_back({effect.entity, effect.source, StatusChangeKind::Dispelled});
}
void phase(State& state, Results& results, const Catalog& catalog, const AdvancePhase& op, std::size_t& budget) {
    need(validPhase(op.phase), ErrorCode::InvalidStatus, "Invalid turn phase");
    const bool ownerPhase = op.phase == TurnPhase::OwnerStart || op.phase == TurnPhase::OwnerEnd;
    need(ownerPhase == op.owner.has_value(), ErrorCode::InvalidTarget, "Turn phase owner mismatch");
    if (op.owner) character(state, *op.owner);
    need(op.epoch > state.phaseEpoch, ErrorCode::StalePhase, "Duplicate or stale phase epoch");
    for (auto& [id, ch] : state.characters) {
        if (op.owner && *op.owner != id) continue;
        if (state.encounter && !std::binary_search(state.encounter->roster.begin(), state.encounter->roster.end(), id)) continue;
        auto cooldowns = state.cooldowns.find(id);
        if (cooldowns != state.cooldowns.end()) {
            for (auto it = cooldowns->second.begin(); it != cooldowns->second.end();) {
                if (it->second.phase != op.phase) { ++it; continue; }
                spend(budget);
                const auto remaining = --it->second.remaining;
                results.cooldowns.push_back({id, it->first, remaining});
                if (!remaining) it = cooldowns->second.erase(it); else ++it;
            }
            if (cooldowns->second.empty()) state.cooldowns.erase(cooldowns);
        }
        for (auto it = ch.statuses.begin(); it != ch.statuses.end();) {
            auto current = it++;
            auto& value = current->second;
            const auto& def = *catalog.status(value.definition);
            if (def.phase != op.phase) continue;
            spend(budget);
            if (value.skipNext) {
                value.skipNext = false;
                results.statuses.push_back({id, current->first, StatusChangeKind::Skipped});
                continue;
            }
            for (std::uint32_t stack = 0; stack < value.stacks; ++stack)
                for (const auto& tick : def.tickEffects) resource(state, results, catalog, {id, tick.resource, tick.amount}, budget);
            if (--value.remaining == 0) {
                results.statuses.push_back({id, current->first, StatusChangeKind::Expired});
                ch.statuses.erase(current);
                recomputeEquipment(ch, *catalog.character(ch.definition));
            } else results.statuses.push_back({id, current->first, StatusChangeKind::Ticked});
        }
    }
    state.phaseEpoch = op.epoch;
}
}
void Bindings::addAbilityHandler(ContentId id, AbilityHandler handler) {
    need(!handlers_.contains(id) && !advancement_.contains(id) && !abilities_.contains(id) && !encounters_.contains(id), ErrorCode::DuplicateHandler, "Duplicate RPG handler");
    need(size() < Limits::handlers, ErrorCode::Limit, "Too many RPG handlers");
    need(handler.version && handler.schemaVersion, ErrorCode::InvalidVersion, "Invalid ability handler/schema version");
    need(bool(handler.validate) && bool(handler.evaluate), ErrorCode::MissingValidator, "Ability validator and evaluator required");
    abilities_.emplace(std::move(id), std::move(handler));
}
const AbilityHandler* Bindings::findAbility(const ContentId& id) const noexcept {
    auto it = abilities_.find(id); return it == abilities_.end() ? nullptr : &it->second;
}
const AbilityDefinition* Catalog::ability(const ContentId& id) const noexcept { return lookup(definition_.abilities, id); }
const StatusDefinition* Catalog::status(const ContentId& id) const noexcept { return lookup(definition_.statuses, id); }
void validateAbilities(CatalogDefinition& catalog, const Bindings& bindings) {
    std::sort(catalog.abilities.begin(), catalog.abilities.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(catalog.statuses.begin(), catalog.statuses.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (auto& def : catalog.abilities) {
        const auto* handler = bindings.findAbility(def.handler.id);
        need(handler != nullptr, ErrorCode::MissingHandler, "Missing ability handler");
        need(def.handler.version == handler->version, ErrorCode::HandlerVersionMismatch, "Ability handler version mismatch");
        need(def.schemaVersion == handler->schemaVersion, ErrorCode::InvalidVersion, "Ability parameter schema mismatch");
        need(def.parameters.size() <= Limits::parameters, ErrorCode::Limit, "Too many ability parameters");
        for (const auto& [id, amount] : def.parameters)
            need(amount.scale > 0, ErrorCode::InvalidDefinition, "Invalid parameter scale");
        need(def.targets.minimum <= def.targets.maximum && def.targets.maximum <= Limits::targets &&
            (!def.targets.minimum || def.targets.allowSelf || def.targets.allowOthers), ErrorCode::InvalidDefinition, "Invalid target requirements");
        amounts(def.costs, true);
        if (def.itemCost) {
            const auto* item = lookup(catalog.items, def.itemCost->definition);
            need(item != nullptr, ErrorCode::MissingDefinition, "Unknown ability item cost");
            need(def.itemCost->quantity > 0 && def.itemCost->quantity <= item->maximumStack,
                ErrorCode::InvalidDefinition, "Invalid item cost quantity");
        }
    }
    for (auto& def : catalog.statuses) {
        need(validPhase(def.phase) && def.duration > 0 && def.duration <= Limits::statusDuration &&
            def.maximumStacks > 0 && def.maximumStacks <= Limits::statusStacks, ErrorCode::InvalidDefinition, "Invalid status phase or bounds");
        need(def.stacking == StatusStacking::Reject || def.stacking == StatusStacking::Refresh || def.stacking == StatusStacking::Stack,
            ErrorCode::InvalidDefinition, "Unknown status stacking policy");
        need(def.stacking == StatusStacking::Stack || (def.maximumStacks == 1 && !def.refreshOnStack),
            ErrorCode::InvalidDefinition, "Only stacking statuses may configure stack options");
        need(def.modifiers.size() <= Limits::modifiers, ErrorCode::Limit, "Too many status modifiers");
        std::sort(def.modifiers.begin(), def.modifiers.end(), [](const auto& a, const auto& b) {
            return a.kind == b.kind ? a.target < b.target : a.kind < b.kind;
        });
        for (std::size_t i = 0; i < def.modifiers.size(); ++i) {
            const auto& m = def.modifiers[i];
            need((m.kind == ModifierTarget::Attribute || m.kind == ModifierTarget::ResourceMaximum) && m.delta.scale > 0,
                ErrorCode::InvalidDefinition, "Invalid status modifier");
            need(!i || m.kind != def.modifiers[i-1].kind || m.target != def.modifiers[i-1].target,
                ErrorCode::InvalidDefinition, "Duplicate status modifier target");
        }
        amounts(def.tickEffects, false);
    }
}
void applyAbilities(State& state, Results& results, const Catalog& catalog, SessionId session,
    const Operation& operation, std::size_t& budget, std::size_t& draws) {
    std::visit([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, ApplyStatus>) status(state, results, catalog, op, budget);
        else if constexpr (std::is_same_v<T, DispelStatus>) dispel(state, results, catalog, op, budget);
        else if constexpr (std::is_same_v<T, AdvancePhase>) phase(state, results, catalog, op, budget);
        else if constexpr (std::is_same_v<T, UseAbility>) {
            character(state, op.entity);
            const auto* def = catalog.ability(op.ability);
            need(def != nullptr, ErrorCode::MissingDefinition, "Unknown ability");
            need(op.targets.size() >= def->targets.minimum && op.targets.size() <= def->targets.maximum,
                ErrorCode::InvalidTarget, "Wrong ability target count");
            auto targets = op.targets; std::sort(targets.begin(), targets.end());
            need(std::adjacent_find(targets.begin(), targets.end()) == targets.end(), ErrorCode::InvalidTarget, "Duplicate ability target");
            for (auto target : targets) {
                character(state, target);
                need(target == op.entity ? def->targets.allowSelf : def->targets.allowOthers, ErrorCode::InvalidTarget, "Target relation rejected");
            }
            for (const auto& cost : def->costs)
                resource(state, results, catalog, {op.entity, cost.resource, {-cost.amount.value, cost.amount.unit, cost.amount.scale}}, budget);
            need(def->itemCost.has_value() == op.item.has_value(), ErrorCode::InvalidOwnership, "Ability item argument mismatch");
            if (op.item) {
                spend(budget);
                auto item = state.items.find(*op.item);
                need(item != state.items.end(), ErrorCode::MissingItem, "Unknown ability item");
                need(item->second.definition == def->itemCost->definition &&
                    state.inventories.at(item->second.inventory).character == op.entity,
                    ErrorCode::InvalidOwnership, "Ability item must belong to caster loadout and match definition");
                applyInventory(state, results, catalog, session, RemoveItem{item->second.inventory, *op.item, def->itemCost->quantity});
            }
            Random random(state.random, draws);
            const auto proposal = catalog.bindings()->registry().findAbility(def->handler.id)->evaluate(
                {state, *def, op.entity, targets}, random);
            need(!random.exhausted(), ErrorCode::BudgetExceeded, "Evaluator exhausted random budget");
            need(proposal.accepted, ErrorCode::RejectedEffect, "Ability evaluator rejected action");
            need(proposal.effects.size() <= Limits::effects, ErrorCode::Limit, "Effect proposal exceeds hard cap");
            need(proposal.effects.size() <= budget, ErrorCode::BudgetExceeded, "Effect proposal exceeds remaining budget");
            std::set<std::pair<EntityId, ContentId>> sources;
            for (const auto& effect : proposal.effects) std::visit([&](const auto& value) {
                using E = std::decay_t<decltype(value)>;
                need(value.entity == op.entity || std::binary_search(targets.begin(), targets.end(), value.entity),
                    ErrorCode::InvalidTarget, "Effect escaped caster/target set");
                if constexpr (std::is_same_v<E, ResourceEffect>) resource(state, results, catalog, value, budget);
                else {
                    need(sources.emplace(value.entity, value.source).second, ErrorCode::InvalidStatus, "Duplicate status source in proposal");
                    if constexpr (std::is_same_v<E, ApplyStatus>) status(state, results, catalog, value, budget);
                    else dispel(state, results, catalog, value, budget);
                }
            }, effect);
            results.abilities.push_back({op.entity, def->id});
        }
    }, operation);
}
} // namespace engine::rpg
