#pragma once
#include "engine/rpg/runtime.hpp"
#include "engine/rpg/combat_ai.hpp"
#include "engine/quests/runtime.hpp"

namespace engine::rpg::quest_bridge {
// Explicit compiled mappings, not another quest evaluator or merged binding registry.
using RewardOperation = std::variant<AddExperience, AddItem, AdjustResource, ApplyStatus, RollLoot>;
struct RewardProposal { bool accepted = true; std::vector<RewardOperation> operations; };
struct Policy {
    std::function<engine::quests::Fields(const State&)> facts;
    std::function<RewardProposal(const engine::quests::EffectRequest&, const State&)> reward;
};
struct ProjectedEvent {
    std::string quest, type, source;
    std::uint64_t sequence;
    engine::quests::Fields payload;
};
using EventProjector = std::function<std::optional<ProjectedEvent>(const State&, const Results&)>;
class Command {
public:
    engine::quests::Batch batch;
private:
    friend class Coordinator;
    std::shared_ptr<const void> owner_;
    std::uint64_t rpgGeneration_ = 0, rpgRevision_ = 0;
};
class Transaction {
public:
    Transaction(Transaction&&) noexcept = default;
    Transaction& operator=(Transaction&&) noexcept = default;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    const engine::quests::Snapshot& questState() const;
    const Results& rewards() const noexcept { return rewards_; }
    std::span<const engine::quests::EffectRequest> effects() const;
private:
    friend class Coordinator;
    Transaction() = default;
    std::shared_ptr<const void> owner_;
    std::optional<engine::quests::Transaction> quests_;
    std::optional<engine::rpg::Transaction> rpg_;
    Results rewards_;
    std::uint64_t generation_ = 0, revision_ = 0, sequence_ = 0;
    bool consumed_ = false;
};
class RpgTransaction {
public:
    RpgTransaction(RpgTransaction&&) noexcept = default;
    RpgTransaction& operator=(RpgTransaction&&) noexcept = default;
    RpgTransaction(const RpgTransaction&) = delete;
    RpgTransaction& operator=(const RpgTransaction&) = delete;
    const State& state() const { return tx_.state(); }
    const Results& results() const noexcept { return tx_.results(); }
private:
    friend class Coordinator;
    RpgTransaction(std::shared_ptr<const void> owner, engine::rpg::Transaction tx)
        :owner_(std::move(owner)),tx_(std::move(tx)){}
    std::shared_ptr<const void> owner_;
    engine::rpg::Transaction tx_;
};
// Owns both runtimes. One owner thread, no raw mutable runtime access or independent restore.
class Coordinator {
public:
    Coordinator(std::shared_ptr<const Catalog>, std::shared_ptr<const engine::quests::Catalog>,
                SessionId, ContentId awardSource, Policy, std::uint64_t seed = 0);
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;
    const State& rpgState() const noexcept { return kernels_->rpg.state(); }
    const engine::quests::Snapshot& questState() const noexcept { return kernels_->quests.state(); }
    std::size_t pending() const noexcept { return kernels_->quests.pending(); }
    Command command() const;
    engine::quests::Event event(ProjectedEvent) const;
    void post(engine::quests::Event);
    bool projectEvent(const Results&, const EventProjector&); // At most one atomic queue append.
    Transaction prepare(const Command&, Budget = {}, engine::quests::Budget = {});
    bool canCommit(const Transaction&) const noexcept;
    bool commit(Transaction&&) noexcept;
    RpgTransaction prepareRpg(ContentId producer, std::uint64_t sequence, std::vector<Operation>,
                              Budget = {}, std::optional<WorldStamp> = {});
    bool canCommitRpg(const RpgTransaction&, std::optional<WorldStamp> = {}) const noexcept;
    CombatPlan planCombat(ContentId producer, std::uint64_t sequence, EntityId,
        std::span<const CombatOption>, const CombatScorer&, const CombatAiBudget&, WorldStamp, std::stop_token = {});
    RpgTransaction prepareCombat(const CombatPlan&, WorldStamp, Budget = {});
    bool commitRpg(RpgTransaction&&, std::optional<WorldStamp> = {}) noexcept;
    Results applyRpg(ContentId producer, std::uint64_t sequence, std::vector<Operation>,
                     Budget = {}, std::optional<WorldStamp> = {});
    std::shared_ptr<const SpatialQuote> spatialQuote(EntityId, SpatialSnapshot, std::optional<PathQuote> = {}) const;
    void invalidateRpgCommands();
    std::vector<std::uint8_t> save() const;
    void restore(std::span<const std::uint8_t>); // Both kernels, one detached aggregate swap.
private:
    Policy policy_; // Captures outlive the two runtimes.
    ContentId source_;
    struct Kernels {
        engine::rpg::Runtime rpg;
        engine::quests::Runtime quests;
        Kernels(std::shared_ptr<const Catalog> r, std::shared_ptr<const engine::quests::Catalog> q,
                SessionId session, std::uint64_t seed):rpg(std::move(r),session,seed),quests(std::move(q)){}
    };
    std::shared_ptr<const Catalog> rpgCatalog_;
    std::shared_ptr<const engine::quests::Catalog> questCatalog_;
    std::unique_ptr<Kernels> kernels_;
    std::shared_ptr<const void> owner_ = std::make_shared<int>(0);
    std::uint64_t sequence_ = 0;
    bool busy_ = false;
};
} // namespace engine::rpg::quest_bridge
