#pragma once
#include "engine/rpg/catalog.hpp"
#include "engine/rpg/state.hpp"
#include "engine/rpg/snapshot.hpp"
#include <variant>
#include <stop_token>

namespace engine::rpg {
struct CombatOption;
struct CombatScoreContext;
struct CombatAiBudget;
class CombatPlan;
struct RecordAward { AwardRecord award; };
struct CreateCharacter { ContentId definition; };
struct SetAttributeBase { EntityId entity; ContentId attribute; Amount amount; };
struct AdjustResource { EntityId entity; ContentId resource; Amount delta; };
struct SetModifiers { EntityId entity; ContentId source; std::vector<Modifier> modifiers; };
struct RemoveModifiers { EntityId entity; ContentId source; };
struct AddExperience { EntityId entity; Amount amount; };
// Flat, bounded authoring values. Empty loot entries represent an explicit no-drop.
using RewardGrant = std::variant<AddExperience, AddItem, AdjustResource, ApplyStatus>;
struct LootEntry { ContentId id; std::uint32_t weight; std::vector<RewardGrant> grants; };
struct RollLoot { ContentId table; std::vector<LootEntry> entries; std::uint32_t rolls = 1; };
struct GrantReward { AwardId id; std::vector<RewardGrant> grants; std::vector<RollLoot> loot; };
// Exact, conserved resource payment: rejects insufficient funds or receiver headroom.
struct TransferResource { EntityId payer, payee; ContentId resource; Amount amount; };
struct TradeItems {
    AwardId id;
    EntityId source, destination;
    ItemId item;
    std::int64_t quantity;
    std::optional<TransferResource> payment;
};
// Game-authored recipe resolved to explicit input stacks and output destinations.
struct CraftItems {
    AwardId id;
    std::vector<RemoveItem> ingredients;
    std::vector<AddItem> products;
    std::vector<TransferResource> payments;
};
using Operation = std::variant<CreateCharacter, SetAttributeBase, AdjustResource,
                               SetModifiers, RemoveModifiers, AddExperience, CreateInventory, AddItem, RemoveItem,
                               SplitItem, MergeItems, TransferItem, EquipItem, UnequipItem, UseAbility, ApplyStatus, DispelStatus, AdvancePhase, BeginEncounter, EndTurn, FinishEncounter, ExitEncounter, SetCooldown, SetTacticalPosition, TacticalAction, AcknowledgePresentation, RecordAward, SetParty, RemoveParty, ConfigureVitality, ApplyVitalEffect, RollLoot, GrantReward, SetProgressionChoices, TransferResource, TradeItems, CraftItems>;
struct Budget { std::size_t operations = 256, advancements = 256, effects = 256, randomDraws = 4096, encounterWork = 4096, reactions = 256, reactionDepth = 8; };
class Command {
public:
    std::uint64_t generation() const noexcept { return generation_; }
    std::uint64_t revision() const noexcept { return revision_; }
private:
    friend class Runtime;
    Command(std::shared_ptr<const void> owner, std::uint64_t generation, std::uint64_t revision,
            ContentId producer, std::uint64_t sequence, std::vector<Operation> operations)
        : owner_(std::move(owner)), generation_(generation), revision_(revision),
          producer_(std::move(producer)), sequence_(sequence), operations_(std::move(operations)) {}
    std::shared_ptr<const void> owner_;
    std::uint64_t generation_, revision_;
    ContentId producer_;
    std::uint64_t sequence_;
    std::vector<Operation> operations_;
};
class Transaction {
public:
    Transaction(Transaction&&) noexcept = default;
    Transaction& operator=(Transaction&&) noexcept = default;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    const State& state() const;
    const Results& results() const noexcept { return results_; }
private:
    friend class Runtime;
    Transaction() = default;
    std::shared_ptr<const void> owner_;
    std::uint64_t generation_ = 0, revision_ = 0;
    std::shared_ptr<const State> state_;
    Results results_;
    bool consumed_ = false;
    std::optional<WorldStamp> world_;
    std::stop_token stop_;
};
// Single owner thread. No concurrent access, IO, clock or implicit command queue.
class Runtime {
public:
    Runtime(std::shared_ptr<const Catalog>, SessionId, std::uint64_t seed = 0);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    const Catalog& catalog() const noexcept { return *catalog_; }
    SessionId session() const noexcept { return session_; }
    const State& state() const noexcept { return *state_; }
    std::uint64_t nextEntity() const noexcept { return state_->nextEntity; }
    EntityId allocateEntityId(); // R1 compatibility: reserved ID, no character; invalidates candidates.
    Command command(ContentId producer, std::uint64_t sequence, std::vector<Operation>) const;
    std::shared_ptr<const SpatialQuote> spatialQuote(EntityId, SpatialSnapshot, std::optional<PathQuote> = {}) const;
    Transaction prepare(const Command&, Budget = {}, std::optional<WorldStamp> = {});
    CombatPlan planCombat(ContentId producer, std::uint64_t sequence, EntityId actor,
        std::span<const CombatOption>,
        const std::function<std::optional<std::int64_t>(const CombatScoreContext&)>&,
        const CombatAiBudget&, WorldStamp, std::stop_token = {});
    Transaction prepareCombat(const CombatPlan&, WorldStamp, Budget = {});
    bool canCommit(const Transaction&, std::optional<WorldStamp> = {}) const noexcept;
    bool commit(Transaction&&, std::optional<WorldStamp> = {}) noexcept;
    void invalidateCommands(); // Explicit host lifecycle boundary, no progression reset.
    std::vector<std::uint8_t> save() const;
    void restore(const Snapshot&); // Validated detached state; no effects, new generation.
    void restore(std::span<const std::uint8_t>);
private:
    std::shared_ptr<const Catalog> catalog_;
    Transaction prepareUnlocked(const Command&, Budget, std::optional<WorldStamp>);
    SessionId session_;
    std::shared_ptr<const void> owner_ = std::make_shared<int>(0);
    std::shared_ptr<const State> state_;
    bool busy_ = false;
};
} // namespace engine::rpg
