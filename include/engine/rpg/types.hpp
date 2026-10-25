#pragma once
#include "engine/modules.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace engine::rpg {
inline constexpr ModuleVersion apiVersion{1, 3, 0};
// R1 publishes no snapshot wire format. Content revisions are application-owned.
struct Limits {
    static constexpr std::size_t keyBytes = 128, definitions = 4096, handlers = 256;
    static constexpr std::size_t characters = 1024, values = 64, modifierSources = 64,
        modifiers = 256, thresholds = 256, unlocks = 256, producers = 64,
        operations = 256, advancements = 256;
    static constexpr std::size_t inventories = 1024, items = 16384, inventorySlots = 256, equipmentSlots = 32;
    static constexpr std::size_t effects = 256, randomDraws = 4096, targets = 64, parameters = 64,
        statuses = 128, totalStatuses = 16384, statusStacks = 32, statusDuration = 65535, results = 512;
};
enum class ErrorCode {
    InvalidId, Limit, InvalidVersion, DuplicateDefinition, DuplicateHandler,
    MissingHandler, HandlerVersionMismatch, MissingValidator, MissingModule,
    InvalidSession, ExhaustedIdentity, InvalidDefinition, MissingDefinition,
    InvalidAmount, Overflow, StaleCommand, InvalidSequence, Reentrant,
    MissingEntity, MissingValue, InvalidModifier, BudgetExceeded, RejectedUnlock,
    ConsumedTransaction, MissingInventory, MissingItem, InvalidQuantity, Capacity, InvalidSlot, InvalidOwnership, IncompatibleStack, RejectedEffect, InvalidTarget, InvalidStatus, StalePhase
};
class Error : public std::invalid_argument {
public:
    Error(ErrorCode code, std::string message)
        : std::invalid_argument(std::move(message)), code_(code) {}
    ErrorCode code() const noexcept { return code_; }
private:
    ErrorCode code_;
};

// Owned, validated, namespaced ASCII key. No implicit string conversion.
class ContentId {
public:
    explicit ContentId(std::string value);
    const std::string& value() const noexcept { return value_; }
    auto operator<=>(const ContentId&) const = default;
private:
    std::string value_;
};
class ContentRevision {
public:
    explicit ContentRevision(std::uint64_t value);
    std::uint64_t value() const noexcept { return value_; }
    auto operator<=>(const ContentRevision&) const = default;
private:
    std::uint64_t value_;
};
class SessionId {
public:
    explicit SessionId(std::uint64_t value);
    std::uint64_t value() const noexcept { return value_; }
    auto operator<=>(const SessionId&) const = default;
private:
    std::uint64_t value_;
};
struct EntityId {
    SessionId session;
    std::uint64_t value;
    auto operator<=>(const EntityId&) const = default;
};

struct ItemId {
    SessionId session;
    std::uint64_t value;
    auto operator<=>(const ItemId&) const = default;
};

// Host chooses a unique durable namespace. Zero next means exhausted, not wrapped.
// A non-default starting cursor is for explicit construction, not snapshot restore.
class EntityIds {
public:
    explicit EntityIds(SessionId session, std::uint64_t next = 1) noexcept
        : session_(session), next_(next) {}
    EntityId allocate();
    SessionId session() const noexcept { return session_; }
    std::uint64_t next() const noexcept { return next_; }
private:
    SessionId session_;
    std::uint64_t next_;
};
} // namespace engine::rpg
