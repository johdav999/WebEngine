#include "internal.hpp"

namespace engine::quests {
using namespace detail;
namespace {
struct Guard {
    bool &busy;
    explicit Guard(bool &b) : busy(b) {
        require(!b, ErrorCode::Reentrant, "Reentrant quest mutation");
        b = true;
    }
    ~Guard() { busy = false; }
};
struct Exhausted {};
struct Work {
    std::uint32_t used = 0, limit = 0;
    void count() {
        if (used == limit)
            throw Exhausted{};
        ++used;
    }
};
void appendTrace(std::vector<Trace> &entries, std::string q, std::string action, std::uint64_t seq = 0) {
    if (entries.size() == Limits::trace)
        entries.erase(entries.begin());
    entries.push_back({std::move(q), std::move(action), seq});
}
void enter(Progress &p, const Stage &stage) {
    p.stage = stage.id;
    p.choice.clear();
    p.elapsedMs = 0;
    p.counts.clear();
    for (const auto &o : stage.objectives)
        p.counts.emplace(o.id, 0);
    p.path.push_back(stage.id);
}
void availability(Snapshot &s, const Catalog &c) {
    for (const auto &q : c.definition().quests) {
        auto &p = s.quests.at(q.id);
        if (p.status == Status::Locked || p.status == Status::Available)
            p.status = unlocked(s, q) ? Status::Available : Status::Locked;
    }
}
bool done(const Progress &p, const Stage &stage) {
    auto satisfied = [&](const auto &o) { return p.counts.at(o.id) >= o.target; };
    return stage.group == Group::All
               ? std::all_of(stage.objectives.begin(), stage.objectives.end(), satisfied)
               : std::any_of(stage.objectives.begin(), stage.objectives.end(), satisfied);
}
void terminal(Snapshot &s, Progress &p, const QuestDefinition &q, Outcome outcome, bool replay,
              std::vector<EffectRequest> &effects, std::vector<Trace> &logs) {
    p.status = outcome == Outcome::Complete ? Status::Completed
               : outcome == Outcome::Fail   ? Status::Failed
                                            : Status::Cancelled;
    if (p.status == Status::Completed && !p.earned) {
        p.earned = true;
        for (const auto &e : q.effects) {
            const auto id = receipt(q, e);
            require(s.receipts.insert(id).second, ErrorCode::InvalidSnapshot,
                    "Receipt without earned completion");
            if (!replay)
                effects.push_back({id, q.id, e.type, e.payload});
        }
    }
    appendTrace(logs, q.id,
                p.status == Status::Completed ? "completed"
                : p.status == Status::Failed  ? "failed"
                                              : "cancelled");
}
void settle(Snapshot &s, const Catalog &c, const QuestDefinition &q, Work &work, bool replay,
            std::vector<EffectRequest> &effects, std::vector<Trace> &logs) {
    auto &p = s.quests.at(q.id);
    while (p.status == Status::Active) {
        const auto &stage = c.stage(q.id, p.stage);
        Evaluation evaluation{s.facts, nullptr, p.choice, p.elapsedMs};
        for (const auto &o : stage.objectives)
            if (o.mode == ObjectiveMode::Current) {
                work.count();
                p.counts.at(o.id) = std::min(
                    o.target, c.bindings().objective(o.binding).evaluate(evaluation, o.binding.parameters));
            }
        if (!done(p, stage))
            break;
        const Transition *selected = nullptr;
        for (const auto &t : stage.transitions) {
            work.count();
            if (c.bindings().condition(t.condition).evaluate(evaluation, t.condition.parameters)) {
                selected = &t;
                break;
            }
        }
        if (!selected && !stage.transitions.empty())
            break;
        if (!selected || selected->next.empty()) {
            terminal(s, p, q, selected ? selected->outcome : Outcome::Complete, replay, effects, logs);
            break;
        }
        enter(p, c.stage(q.id, selected->next));
        appendTrace(logs, q.id, "stage:" + p.stage);
        // New-stage facts may resolve immediately; the triggering event is never reused.
    }
}
} // namespace
Runtime::Runtime(std::shared_ptr<const Catalog> c) : catalog_(std::move(c)) {
    require(bool(catalog_), ErrorCode::InvalidContent, "Null quest catalogue");
    auto state = std::make_shared<Snapshot>();
    state->catalog = catalog_->definition().id;
    state->content = catalog_->definition().content;
    state->fingerprint = catalog_->fingerprint();
    state->facts = catalog_->definition().facts;
    for (const auto &q : catalog_->definition().quests)
        state->quests.emplace(q.id, Progress{});
    availability(*state, *catalog_);
    state_ = std::move(state);
}
const Progress &Runtime::view(std::string_view q) const {
    auto i = state_->quests.find(std::string(q));
    require(i != state_->quests.end(), ErrorCode::InvalidInput, "Unknown quest view");
    return i->second;
}
Batch Runtime::batch() const {
    Batch b;
    b.generation = state_->generation;
    b.revision = state_->revision;
    return b;
}
Event Runtime::event(std::string_view q, std::string type, std::string source, std::uint64_t seq,
                     Fields payload) const {
    const auto &p = view(q);
    Event e;
    e.quest = q;
    e.stage = p.stage;
    e.type = std::move(type);
    e.source = std::move(source);
    e.sequence = seq;
    e.generation = state_->generation;
    e.activation = p.activation;
    e.payload = std::move(payload);
    e.owner_ = owner_;
    return e;
}
void Runtime::post(Event e) {
    Guard guard(busy_);
    require(e.owner_.lock() == owner_, ErrorCode::Stale, "Event belongs to another runtime");
    const auto &p = view(e.quest);
    require(e.generation == state_->generation && e.activation == p.activation && e.stage == p.stage &&
                p.status == Status::Active,
            ErrorCode::Stale, "Stale/inactive quest event");
    key(e.source);
    key(e.type);
    fields(e.payload);
    auto type = catalog_->definition().events.find(e.type);
    require(type != catalog_->definition().events.end(), ErrorCode::InvalidInput, "Unknown event type");
    typed(e.payload, type->second, false);
    require(e.sequence > 0 && e.sequence < std::numeric_limits<std::uint64_t>::max() - 1,
            ErrorCode::InvalidInput, "Invalid source sequence");
    std::uint64_t last = 0;
    std::set<std::string> sources;
    for (const auto &[source, seq] : state_->sequences) {
        sources.insert(source);
        if (source == e.source)
            last = seq;
    }
    for (const auto &pending : queue_) {
        sources.insert(pending.source);
        if (pending.source == e.source)
            last = std::max(last, pending.sequence);
    }
    require(e.sequence > last, ErrorCode::Duplicate, "Duplicate/out-of-order source sequence");
    sources.insert(e.source);
    bound(sources.size(), Limits::sources, "Too many event sources");
    bound(queue_.size() + 1, Limits::events, "Event queue full");
    const auto next = increment(queueRevision_);
    queue_.push_back(std::move(e));
    queueRevision_ = next;
}
Prepared Runtime::prepare(const Batch &b, Budget budget) {
    Guard guard(busy_);
    require(b.generation == state_->generation && b.revision == state_->revision, ErrorCode::Stale,
            "Stale batch");
    require(budget.events > 0 && budget.events <= Limits::events && budget.evaluations > 0 &&
                budget.evaluations <= 1048576,
            ErrorCode::Limit, "Invalid evaluation budget");
    bound(b.commands.size(), 64, "Too many commands");
    fields(b.facts, Limits::facts);
    typed(b.facts, catalog_->definition().facts, true);
    require(b.deltaMs <= Limits::maxTimeMs, ErrorCode::Limit, "Time delta exceeds limit");
    increment(queueRevision_);
    auto s = std::make_shared<Snapshot>(*state_);
    s->revision = increment(s->revision);
    Transaction tx;
    tx.owner_ = owner_;
    tx.baseRevision_ = state_->revision;
    tx.queueRevision_ = queueRevision_;
    tx.trace_ = trace_;
    Work work{0, budget.evaluations};
    try {
        for (const auto &[k, v] : b.facts)
            s->facts.at(k) = v;
        for (auto &[id, p] : s->quests)
            if (p.status == Status::Active) {
                require(b.deltaMs <= Limits::maxTimeMs - p.elapsedMs, ErrorCode::Limit,
                        "Quest timer exhausted");
                p.elapsedMs += b.deltaMs;
            }
        for (const auto &command : b.commands) {
            const auto &q = catalog_->quest(command.quest);
            auto &p = s->quests.at(q.id);
            require(command.activation == p.activation, ErrorCode::Stale, "Stale command activation");
            if (command.kind == CommandKind::Activate || command.kind == CommandKind::Retry) {
                if (command.kind == CommandKind::Activate)
                    require(p.status == Status::Available, ErrorCode::InvalidInput, "Quest unavailable");
                else
                    require(p.status == Status::Active || p.status == Status::Completed ||
                                p.status == Status::Failed || p.status == Status::Cancelled,
                            ErrorCode::InvalidInput, "Cannot retry this quest");
                require(unlocked(*s, q), ErrorCode::InvalidInput, "Prerequisites not earned");
                p.activation = increment(p.activation);
                p.status = Status::Active;
                p.path.clear();
                enter(p, catalog_->stage(q.id, q.entry));
                appendTrace(tx.trace_, q.id, "activated");
            } else {
                require(p.status == Status::Active, ErrorCode::InvalidInput, "Quest is not active");
                if (command.kind == CommandKind::Choose) {
                    const auto &stage = catalog_->stage(q.id, p.stage);
                    require(p.choice.empty() && std::find(stage.choices.begin(), stage.choices.end(),
                                                          command.choice) != stage.choices.end(),
                            ErrorCode::InvalidInput, "Unavailable/already chosen option");
                    p.choice = command.choice;
                    appendTrace(tx.trace_, q.id, "choice:" + p.choice);
                } else if (command.kind == CommandKind::Fail || command.kind == CommandKind::Cancel)
                    terminal(*s, p, q, command.kind == CommandKind::Fail ? Outcome::Fail : Outcome::Cancel,
                             b.replay, tx.effects_, tx.trace_);
                else
                    fail(ErrorCode::InvalidInput, "Unknown command kind");
            }
        }
        for (const auto &[id, p] : s->quests)
            if (p.status == Status::Active)
                settle(*s, *catalog_, catalog_->quest(id), work, b.replay, tx.effects_, tx.trace_);
        availability(*s, *catalog_);
        tx.consumed_ = std::min<std::size_t>(queue_.size(), budget.events);
        for (std::size_t n = 0; n < tx.consumed_; ++n) {
            const auto &e = queue_[n];
            s->sequences[e.source] = e.sequence;
            auto &p = s->quests.at(e.quest);
            if (e.generation != s->generation || e.activation != p.activation || e.stage != p.stage ||
                p.status != Status::Active) {
                appendTrace(tx.trace_, e.quest, "discarded-scope", e.sequence);
                continue;
            }
            const auto &stage = catalog_->stage(e.quest, p.stage);
            Evaluation evaluation{s->facts, &e, p.choice, p.elapsedMs};
            for (const auto &o : stage.objectives)
                if (o.mode == ObjectiveMode::Accumulate) {
                    work.count();
                    const auto delta =
                        catalog_->bindings().objective(o.binding).evaluate(evaluation, o.binding.parameters);
                    auto &count = p.counts.at(o.id);
                    count += std::min(delta, o.target - count);
                }
            appendTrace(tx.trace_, e.quest, "event:" + e.type, e.sequence);
            settle(*s, *catalog_, catalog_->quest(e.quest), work, b.replay, tx.effects_, tx.trace_);
            availability(*s, *catalog_);
        }
        tx.state_ = std::move(s);
        Prepared result;
        result.status = PrepareStatus::Ready;
        result.evaluations = work.used;
        result.transaction.emplace(std::move(tx));
        return result;
    } catch (const Exhausted &) {
        return {PrepareStatus::BudgetExceeded, std::nullopt, work.used};
    }
}
bool Runtime::canCommit(const Transaction &t) const noexcept {
    return !busy_ && t.owner_ == owner_ && t.state_ && t.baseRevision_ == state_->revision &&
           t.queueRevision_ == queueRevision_ &&
           queueRevision_ < std::numeric_limits<std::uint64_t>::max() - 1;
}
bool Runtime::commit(Transaction &&t) noexcept {
    if (!canCommit(t))
        return false;
    state_ = std::move(t.state_);
    for (std::size_t n = 0; n < t.consumed_; ++n)
        queue_.pop_front();
    ++queueRevision_;
    trace_.swap(t.trace_);
    t.owner_.reset();
    return true;
}
void Runtime::restore(const Snapshot &snapshot) {
    Guard guard(busy_);
    validateSnapshot(*catalog_, snapshot);
    auto candidate = std::make_shared<Snapshot>(snapshot);
    candidate->generation = increment(std::max(state_->generation, snapshot.generation));
    candidate->revision = increment(std::max(state_->revision, snapshot.revision));
    const auto nextQueue = increment(queueRevision_);
    state_ = std::move(candidate);
    queue_.clear();
    queueRevision_ = nextQueue;
    trace_.clear();
}
} // namespace engine::quests
