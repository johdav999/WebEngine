#pragma once
#include "engine/quests/snapshot.hpp"
#include <deque>

namespace engine::quests {
enum class CommandKind { Activate, Retry, Choose, Fail, Cancel };
struct Command {
    std::string quest;
    CommandKind kind = CommandKind::Activate;
    std::uint64_t activation = 0;
    std::string choice;
};
struct Batch {
    std::uint64_t generation = 0, revision = 0, deltaMs = 0;
    Fields facts;
    std::vector<Command> commands;
    bool replay = false; // rebuild receipts/state while suppressing emitted effects
};
struct Budget {
    std::uint32_t events = 64, evaluations = 4096;
};
struct EffectRequest {
    std::string receipt, quest, type;
    Fields payload;
};
struct Trace {
    std::string quest, action;
    std::uint64_t sequence = 0;
};
class Transaction {
  public:
    Transaction(Transaction &&) noexcept = default;
    Transaction &operator=(Transaction &&) noexcept = default;
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    const Snapshot &state() const noexcept { return *state_; }
    std::span<const EffectRequest> effects() const noexcept { return effects_; }
    std::span<const Trace> trace() const noexcept { return trace_; }

  private:
    friend class Runtime;
    Transaction() = default;
    std::shared_ptr<const void> owner_;
    std::shared_ptr<const Snapshot> state_;
    std::uint64_t baseRevision_ = 0, queueRevision_ = 0;
    std::size_t consumed_ = 0;
    std::vector<EffectRequest> effects_;
    std::vector<Trace> trace_;
};
enum class PrepareStatus { Ready, BudgetExceeded };
struct Prepared {
    PrepareStatus status = PrepareStatus::BudgetExceeded;
    std::optional<Transaction> transaction;
    std::uint32_t evaluations = 0;
};
// One owner thread. Borrowed views expire on successful commit/restore.
// The catalogue retains the module registry and all provider captures.
class Runtime {
  public:
    explicit Runtime(std::shared_ptr<const Catalog>);
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    const Snapshot &state() const noexcept { return *state_; }
    const Progress &view(std::string_view quest) const;
    Batch batch() const;
    Event event(std::string_view quest, std::string type, std::string source, std::uint64_t sequence,
                Fields payload = {}) const;
    void post(Event); // rejects stale/duplicate/oversized input before changing queue
    std::size_t pending() const noexcept { return queue_.size(); }
    // Pure callbacks only; rejected/budget-exhausted work preserves state AND queue.
    Prepared prepare(const Batch &, Budget = {});
    bool canCommit(const Transaction &) const noexcept;
    // Host validates effects and stages its other state BEFORE this no-allocation publication.
    bool commit(Transaction &&) noexcept;
    void restore(const Snapshot &); // validated staging; clears input and rebases generation/revision
    std::span<const Trace> trace() const noexcept { return trace_; }

  private:
    std::shared_ptr<const Catalog> catalog_;
    std::shared_ptr<const void> owner_ = std::make_shared<int>(0);
    std::shared_ptr<const Snapshot> state_;
    std::deque<Event> queue_;
    std::uint64_t queueRevision_ = 0;
    bool busy_ = false;
    std::vector<Trace> trace_;
};
} // namespace engine::quests
