#pragma once
#include "engine/rpg/catalog.hpp"
#include "engine/rpg/state.hpp"
#include <variant>

namespace engine::rpg {
struct CreateCharacter { ContentId definition; };
struct SetAttributeBase { EntityId entity; ContentId attribute; Amount amount; };
struct AdjustResource { EntityId entity; ContentId resource; Amount delta; };
struct SetModifiers { EntityId entity; ContentId source; std::vector<Modifier> modifiers; };
struct RemoveModifiers { EntityId entity; ContentId source; };
struct AddExperience { EntityId entity; Amount amount; };
using Operation = std::variant<CreateCharacter, SetAttributeBase, AdjustResource,
                               SetModifiers, RemoveModifiers, AddExperience, CreateInventory, AddItem, RemoveItem,
                               SplitItem, MergeItems, TransferItem, EquipItem, UnequipItem>;
struct Budget { std::size_t operations = 256, advancements = 256; };
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
};
// Single owner thread. No concurrent access, IO, clock or implicit command queue.
class Runtime {
public:
    Runtime(std::shared_ptr<const Catalog>, SessionId);
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
    Transaction prepare(const Command&, Budget = {});
    bool canCommit(const Transaction&) const noexcept;
    bool commit(Transaction&&) noexcept;
    void invalidateCommands(); // Explicit host lifecycle boundary, no progression reset.
private:
    std::shared_ptr<const Catalog> catalog_;
    SessionId session_;
    std::shared_ptr<const void> owner_ = std::make_shared<int>(0);
    std::shared_ptr<const State> state_ = std::make_shared<State>();
    bool busy_ = false;
};
} // namespace engine::rpg
