#pragma once
#include "engine/rpg/characters.hpp"
#include <variant>

namespace engine::rpg {
struct State;
enum class LifeState { Alive, Downed, Dead };
struct VitalityState {
    ContentId resource;
    LifeState condition = LifeState::Alive, onZero = LifeState::Downed;
};
// Opt in once per character, outside encounters. Does not create a second HP pool.
struct ConfigureVitality { EntityId entity; ContentId resource; LifeState onZero = LifeState::Downed; };
// Final nonnegative, unit-checked magnitudes. Formulas/resistances belong to rulesets.
struct DamageEffect { EntityId entity; Amount amount; };
struct HealEffect { EntityId entity; Amount amount; };
struct ReviveEffect { EntityId entity; Amount amount; bool allowDead = false; };
struct DefeatEffect { EntityId entity; LifeState condition = LifeState::Downed; };
using VitalEffect = std::variant<DamageEffect, HealEffect, ReviveEffect, DefeatEffect>;
// Administrative command; player actions use authored ability/reaction effects.
struct ApplyVitalEffect { VitalEffect effect; };
enum class VitalEvent { Configured, Damaged, Healed, Revived, Defeated, Depleted };
struct VitalResult {
    EntityId entity;
    VitalEvent event;
    std::int64_t requested, applied;
    LifeState before, after;
};
bool canAct(const State&, EntityId) noexcept; // Untracked existing characters retain legacy policy.
bool teamDefeated(const State&, const ContentId&); // Nonempty assigned team, every member incapacitated.
} // namespace engine::rpg
