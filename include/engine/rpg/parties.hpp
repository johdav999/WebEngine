#pragma once
#include "engine/rpg/types.hpp"
#include <map>
#include <optional>
#include <vector>

namespace engine::rpg {
struct State;
enum class ControlMode { Player, AI, Scripted };
struct PartyMember {
    EntityId entity;
    bool active = true;
    ControlMode control = ControlMode::Player;
};
struct PartyState {
    std::size_t capacity = 4, activeLimit = 4;
    std::vector<PartyMember> members; // Authored order; each character belongs to at most one party.
    std::optional<EntityId> leader; // If set, must be an active member.
};
// Create or atomically replace a complete roster; absent party IDs are created.
struct SetParty { ContentId party; PartyState state; };
struct RemoveParty { ContentId party; };
struct PartyResult { ContentId party; bool removed = false; };
struct CombatMember { ContentId team; ControlMode control = ControlMode::Player; };
enum class TeamRelation { Unassigned, Ally, Opponent };
std::optional<ContentId> partyOf(const State&, EntityId);
std::vector<EntityId> activePartyMembers(const State&, const ContentId&);
TeamRelation teamRelation(const State&, EntityId, EntityId) noexcept;
// A convenience for hosts; not authentication or an AI implementation.
bool controlledBy(const State&, EntityId, ControlMode) noexcept;
} // namespace engine::rpg
