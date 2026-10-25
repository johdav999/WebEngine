#include "engine/rpg/quest_bridge.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <set>

namespace engine::rpg::quest_bridge {
namespace q = engine::quests;
namespace {
void need(bool ok, ErrorCode code, const char* message) { if (!ok) throw Error(code,message); }
ContentId producer() { return ContentId("webengine.quest-awards"); }
struct Guard {
    bool& busy;
    explicit Guard(bool& value):busy(value) {need(!busy,ErrorCode::Reentrant,"Reentrant quest bridge mutation");busy=true;}
    ~Guard(){busy=false;}
};
}
const q::Snapshot& Transaction::questState() const {
    need(!consumed_ && owner_ && quests_,ErrorCode::ConsumedTransaction,"Consumed bridge candidate");return quests_->state();
}
std::span<const q::EffectRequest> Transaction::effects() const {
    need(!consumed_ && owner_ && quests_,ErrorCode::ConsumedTransaction,"Consumed bridge candidate");return quests_->effects();
}
Coordinator::Coordinator(std::shared_ptr<const Catalog> rpg,std::shared_ptr<const q::Catalog> quests,
    SessionId session,ContentId source,Policy policy,std::uint64_t seed)
    :policy_(std::move(policy)),source_(std::move(source)),rpg_(std::move(rpg),session,seed),quests_(std::move(quests)) {
    need(bool(policy_.reward),ErrorCode::MissingHandler,"Quest bridge requires a reward mapper");
}
Command Coordinator::command() const {
    Command command;command.owner_=owner_;command.rpgGeneration_=rpg_.state().generation;
    command.rpgRevision_=rpg_.state().revision;command.batch=quests_.batch();return command;
}
q::Event Coordinator::event(ProjectedEvent projection) const {
    return quests_.event(projection.quest,std::move(projection.type),std::move(projection.source),projection.sequence,std::move(projection.payload));
}
void Coordinator::post(q::Event event) {Guard guard(busy_);quests_.post(std::move(event));}
bool Coordinator::projectEvent(const Results& results,const EventProjector& project) {
    Guard guard(busy_);need(bool(project),ErrorCode::MissingHandler,"Missing event projector");
    auto projected=project(rpg_.state(),results);if(!projected)return false;
    quests_.post(event(std::move(*projected)));return true;
}
Transaction Coordinator::prepare(const Command& command,Budget budget,q::Budget questBudget) {
    Guard guard(busy_);
    need(command.owner_==owner_ && command.rpgGeneration_==rpg_.state().generation && command.rpgRevision_==rpg_.state().revision,
        ErrorCode::StaleCommand,"Foreign or stale RPG/quest command");
    need(!rpg_.state().presentation,ErrorCode::PresentationPending,"Finish presentation before quest coordination");
    need(budget.operations<=Limits::operations && budget.advancements<=Limits::advancements &&
        budget.effects<=Limits::effects && budget.randomDraws<=Limits::randomDraws &&
        budget.encounterWork<=Limits::encounterWork && budget.reactions<=Limits::reactions &&
        budget.reactionDepth<=Limits::reactionDepth,ErrorCode::Limit,"Reward budget exceeds hard caps");
    need(sequence_!=std::numeric_limits<std::uint64_t>::max(),ErrorCode::Overflow,"Bridge sequence exhausted");
    auto batch=command.batch;
    if(policy_.facts) {
        auto projected=policy_.facts(rpg_.state());
        need(projected.size()<=q::Limits::facts,ErrorCode::Limit,"Fact projection too large");
        for(auto& [key,value]:projected) {
            need(!batch.facts.contains(key),ErrorCode::InvalidAward,"Projected fact overlaps explicit fact");
            batch.facts.emplace(std::move(key),std::move(value));
        }
    }
    auto prepared=quests_.prepare(batch,questBudget);
    need(prepared.status==q::PrepareStatus::Ready && prepared.transaction.has_value(),ErrorCode::BudgetExceeded,"Quest evaluation budget exhausted");
    auto& quest=*prepared.transaction;
    std::vector<Operation> operations;
    auto append=[&](Operation operation) {
        need(operations.size()<Limits::operations && operations.size()<budget.operations,ErrorCode::BudgetExceeded,"Compound reward operation budget exhausted");
        operations.push_back(std::move(operation));
    };
    std::set<std::string> added;
    for(const auto& receipt:quest.state().receipts)
        if(!quests_.state().receipts.contains(receipt))added.insert(receipt);
    need(added.size()<=Limits::operations,ErrorCode::BudgetExceeded,"Too many new quest receipts in one transaction");
    need(added.size()<=Limits::awards-rpg_.state().awards.size(),ErrorCode::Limit,"Award ledger capacity exhausted");
    if(batch.replay) {
        need(quest.effects().empty(),ErrorCode::InvalidAward,"Replay unexpectedly emitted effects");
        for(const auto& receipt:added)append(RecordAward{{{source_,receipt},AwardDisposition::ReplaySuppressed}});
    } else {
        need(quest.effects().size()==added.size(),ErrorCode::InvalidAward,"Quest effect/receipt mismatch");
        for(const auto& effect:quest.effects()) {
            need(added.erase(effect.receipt)==1,ErrorCode::InvalidAward,"Duplicate or unrecorded quest effect");
            const AwardId award{source_,effect.receipt};
            need(!rpg_.state().awards.contains(award),ErrorCode::DuplicateAward,"Quest award already recorded");
            auto proposal=policy_.reward(effect,rpg_.state());
            need(proposal.accepted,ErrorCode::InvalidAward,"Reward mapping rejected effect");
            need(proposal.operations.size()<=Limits::operations,ErrorCode::Limit,"Reward proposal too large");
            for(const auto& operation:proposal.operations)std::visit([&](const auto& value){append(value);},operation);
            append(RecordAward{{award,AwardDisposition::Granted}});
        }
    }
    Transaction tx;tx.owner_=owner_;tx.generation_=rpg_.state().generation;tx.revision_=rpg_.state().revision;tx.sequence_=sequence_;
    if(!operations.empty()) {
        tx.rpg_.emplace(rpg_.prepare(rpg_.command(producer(),sequence_+1,std::move(operations)),budget));
        tx.rewards_=tx.rpg_->results(); // All allocation/copies happen before publication.
    }
    tx.quests_.emplace(std::move(quest));return tx;
}
bool Coordinator::canCommit(const Transaction& tx) const noexcept {
    return !busy_ && !tx.consumed_ && tx.owner_==owner_ && tx.quests_ && tx.sequence_==sequence_ &&
        sequence_!=std::numeric_limits<std::uint64_t>::max() && tx.generation_==rpg_.state().generation &&
        tx.revision_==rpg_.state().revision && (!tx.rpg_ || rpg_.canCommit(*tx.rpg_)) && quests_.canCommit(*tx.quests_);
}
bool Coordinator::commit(Transaction&& tx) noexcept {
    if(!canCommit(tx))return false;
    busy_=true;
    // Owner thread, private runtimes, no intervening callbacks/allocations/queue writes.
    // RPG publication cannot change any predicate of the quest candidate (see docs/rpg-quests.md).
    // False below is an internal invariant violation, never a recoverable partial commit path.
    if(tx.rpg_ && !rpg_.commit(std::move(*tx.rpg_)))std::terminate();
    if(!quests_.commit(std::move(*tx.quests_)))std::terminate();
    ++sequence_;tx.consumed_=true;busy_=false;return true;
}
RpgTransaction Coordinator::prepareRpg(ContentId id,std::uint64_t sequence,std::vector<Operation> operations,Budget budget,std::optional<WorldStamp> world) {
    Guard guard(busy_);
    need(id!=producer(),ErrorCode::InvalidAward,"Bridge award producer is reserved");
    for(const auto& operation:operations)
        need(!std::holds_alternative<RecordAward>(operation),ErrorCode::InvalidAward,"Only coordinated quest publication may write this ledger");
    return RpgTransaction(owner_,rpg_.prepare(rpg_.command(std::move(id),sequence,std::move(operations)),budget,world));
}
bool Coordinator::canCommitRpg(const RpgTransaction& tx,std::optional<WorldStamp> world) const noexcept {
    return !busy_ && tx.owner_==owner_ && rpg_.canCommit(tx.tx_,world);
}
bool Coordinator::commitRpg(RpgTransaction&& tx,std::optional<WorldStamp> world) noexcept {
    if(!canCommitRpg(tx,world))return false;
    return rpg_.commit(std::move(tx.tx_),world);
}
Results Coordinator::applyRpg(ContentId id,std::uint64_t sequence,std::vector<Operation> operations,Budget budget,std::optional<WorldStamp> world) {
    for(const auto& operation:operations)
        need(!std::holds_alternative<TacticalAction>(operation),ErrorCode::InvalidAction,"Use prepareRpg/commitRpg with a fresh world observation for spatial actions");
    auto tx=prepareRpg(std::move(id),sequence,std::move(operations),budget,world);
    auto results=tx.results();if(!commitRpg(std::move(tx),world))std::terminate();return results;
}
std::shared_ptr<const SpatialQuote> Coordinator::spatialQuote(EntityId actor,SpatialSnapshot snapshot,std::optional<PathQuote> path) const {
    need(!busy_,ErrorCode::Reentrant,"Cannot quote during bridge preparation");return rpg_.spatialQuote(actor,std::move(snapshot),std::move(path));
}
void Coordinator::invalidateRpgCommands(){Guard guard(busy_);rpg_.invalidateCommands();}
} // namespace engine::rpg::quest_bridge
