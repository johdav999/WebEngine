#include "engine/rpg/quest_bridge.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <set>
#include "wire.hpp"

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
    :policy_(std::move(policy)),source_(std::move(source)),rpgCatalog_(std::move(rpg)),questCatalog_(std::move(quests)),
     kernels_(std::make_unique<Kernels>(rpgCatalog_,questCatalog_,session,seed)) {
    need(bool(policy_.reward),ErrorCode::MissingHandler,"Quest bridge requires a reward mapper");
}
Command Coordinator::command() const {
    Command command;command.owner_=owner_;command.rpgGeneration_=kernels_->rpg.state().generation;
    command.rpgRevision_=kernels_->rpg.state().revision;command.batch=kernels_->quests.batch();return command;
}
q::Event Coordinator::event(ProjectedEvent projection) const {
    return kernels_->quests.event(projection.quest,std::move(projection.type),std::move(projection.source),projection.sequence,std::move(projection.payload));
}
void Coordinator::post(q::Event event) {Guard guard(busy_);kernels_->quests.post(std::move(event));}
bool Coordinator::projectEvent(const Results& results,const EventProjector& project) {
    Guard guard(busy_);need(bool(project),ErrorCode::MissingHandler,"Missing event projector");
    auto projected=project(kernels_->rpg.state(),results);if(!projected)return false;
    kernels_->quests.post(event(std::move(*projected)));return true;
}
Transaction Coordinator::prepare(const Command& command,Budget budget,q::Budget questBudget) {
    Guard guard(busy_);
    need(command.owner_==owner_ && command.rpgGeneration_==kernels_->rpg.state().generation && command.rpgRevision_==kernels_->rpg.state().revision,
        ErrorCode::StaleCommand,"Foreign or stale RPG/quest command");
    need(!kernels_->rpg.state().presentation,ErrorCode::PresentationPending,"Finish presentation before quest coordination");
    need(budget.operations<=Limits::operations && budget.advancements<=Limits::advancements &&
        budget.effects<=Limits::effects && budget.randomDraws<=Limits::randomDraws &&
        budget.encounterWork<=Limits::encounterWork && budget.reactions<=Limits::reactions &&
        budget.reactionDepth<=Limits::reactionDepth,ErrorCode::Limit,"Reward budget exceeds hard caps");
    need(sequence_!=std::numeric_limits<std::uint64_t>::max(),ErrorCode::Overflow,"Bridge sequence exhausted");
    auto batch=command.batch;
    if(policy_.facts) {
        auto projected=policy_.facts(kernels_->rpg.state());
        need(projected.size()<=q::Limits::facts,ErrorCode::Limit,"Fact projection too large");
        for(auto& [key,value]:projected) {
            need(!batch.facts.contains(key),ErrorCode::InvalidAward,"Projected fact overlaps explicit fact");
            batch.facts.emplace(std::move(key),std::move(value));
        }
    }
    auto prepared=kernels_->quests.prepare(batch,questBudget);
    need(prepared.status==q::PrepareStatus::Ready && prepared.transaction.has_value(),ErrorCode::BudgetExceeded,"Quest evaluation budget exhausted");
    auto& quest=*prepared.transaction;
    std::vector<Operation> operations;
    auto append=[&](Operation operation) {
        need(operations.size()<Limits::operations && operations.size()<budget.operations,ErrorCode::BudgetExceeded,"Compound reward operation budget exhausted");
        operations.push_back(std::move(operation));
    };
    std::set<std::string> added;
    for(const auto& receipt:quest.state().receipts)
        if(!kernels_->quests.state().receipts.contains(receipt))added.insert(receipt);
    need(added.size()<=Limits::operations,ErrorCode::BudgetExceeded,"Too many new quest receipts in one transaction");
    need(added.size()<=Limits::awards-kernels_->rpg.state().awards.size(),ErrorCode::Limit,"Award ledger capacity exhausted");
    if(batch.replay) {
        need(quest.effects().empty(),ErrorCode::InvalidAward,"Replay unexpectedly emitted effects");
        for(const auto& receipt:added)append(RecordAward{{{source_,receipt},AwardDisposition::ReplaySuppressed}});
    } else {
        need(quest.effects().size()==added.size(),ErrorCode::InvalidAward,"Quest effect/receipt mismatch");
        for(const auto& effect:quest.effects()) {
            need(added.erase(effect.receipt)==1,ErrorCode::InvalidAward,"Duplicate or unrecorded quest effect");
            const AwardId award{source_,effect.receipt};
            need(!kernels_->rpg.state().awards.contains(award),ErrorCode::DuplicateAward,"Quest award already recorded");
            auto proposal=policy_.reward(effect,kernels_->rpg.state());
            need(proposal.accepted,ErrorCode::InvalidAward,"Reward mapping rejected effect");
            need(proposal.operations.size()<=Limits::operations,ErrorCode::Limit,"Reward proposal too large");
            for(const auto& operation:proposal.operations)std::visit([&](const auto& value){append(value);},operation);
            append(RecordAward{{award,AwardDisposition::Granted}});
        }
    }
    Transaction tx;tx.owner_=owner_;tx.generation_=kernels_->rpg.state().generation;tx.revision_=kernels_->rpg.state().revision;tx.sequence_=sequence_;
    if(!operations.empty()) {
        tx.rpg_.emplace(kernels_->rpg.prepare(kernels_->rpg.command(producer(),sequence_+1,std::move(operations)),budget));
        tx.rewards_=tx.rpg_->results(); // All allocation/copies happen before publication.
    }
    tx.quests_.emplace(std::move(quest));return tx;
}
bool Coordinator::canCommit(const Transaction& tx) const noexcept {
    return !busy_ && !tx.consumed_ && tx.owner_==owner_ && tx.quests_ && tx.sequence_==sequence_ &&
        sequence_!=std::numeric_limits<std::uint64_t>::max() && tx.generation_==kernels_->rpg.state().generation &&
        tx.revision_==kernels_->rpg.state().revision && (!tx.rpg_ || kernels_->rpg.canCommit(*tx.rpg_)) && kernels_->quests.canCommit(*tx.quests_);
}
bool Coordinator::commit(Transaction&& tx) noexcept {
    if(!canCommit(tx))return false;
    busy_=true;
    // Owner thread, private runtimes, no intervening callbacks/allocations/queue writes.
    // RPG publication cannot change any predicate of the quest candidate (see docs/rpg-quests.md).
    // False below is an internal invariant violation, never a recoverable partial commit path.
    if(tx.rpg_ && !kernels_->rpg.commit(std::move(*tx.rpg_)))std::terminate();
    if(!kernels_->quests.commit(std::move(*tx.quests_)))std::terminate();
    ++sequence_;tx.consumed_=true;busy_=false;return true;
}
RpgTransaction Coordinator::prepareRpg(ContentId id,std::uint64_t sequence,std::vector<Operation> operations,Budget budget,std::optional<WorldStamp> world) {
    Guard guard(busy_);
    need(id!=producer(),ErrorCode::InvalidAward,"Bridge award producer is reserved");
    for(const auto& operation:operations) {
        need(!std::holds_alternative<RecordAward>(operation),ErrorCode::InvalidAward,"Only coordinated quest publication may write this ledger");
        if (const auto* reward=std::get_if<GrantReward>(&operation))
            need(reward->id.source!=source_,ErrorCode::InvalidAward,"Quest award source is reserved");
    }
    return RpgTransaction(owner_,kernels_->rpg.prepare(kernels_->rpg.command(std::move(id),sequence,std::move(operations)),budget,world));
}
bool Coordinator::canCommitRpg(const RpgTransaction& tx,std::optional<WorldStamp> world) const noexcept {
    return !busy_ && tx.owner_==owner_ && kernels_->rpg.canCommit(tx.tx_,world);
}
CombatPlan Coordinator::planCombat(ContentId id,std::uint64_t sequence,EntityId actor,
    std::span<const CombatOption> options,const CombatScorer& score,const CombatAiBudget& budget,WorldStamp world,std::stop_token stop){
    Guard guard(busy_);
    need(id!=producer(),ErrorCode::InvalidAward,"Bridge award producer is reserved");
    return kernels_->rpg.planCombat(std::move(id),sequence,actor,options,score,budget,world,stop);
}
RpgTransaction Coordinator::prepareCombat(const CombatPlan& plan,WorldStamp world,Budget budget){
    Guard guard(busy_);return RpgTransaction(owner_,kernels_->rpg.prepareCombat(plan,world,budget));
}
bool Coordinator::commitRpg(RpgTransaction&& tx,std::optional<WorldStamp> world) noexcept {
    if(!canCommitRpg(tx,world))return false;
    return kernels_->rpg.commit(std::move(tx.tx_),world);
}
Results Coordinator::applyRpg(ContentId id,std::uint64_t sequence,std::vector<Operation> operations,Budget budget,std::optional<WorldStamp> world) {
    for(const auto& operation:operations)
        need(!std::holds_alternative<TacticalAction>(operation),ErrorCode::InvalidAction,"Use prepareRpg/commitRpg with a fresh world observation for spatial actions");
    auto tx=prepareRpg(std::move(id),sequence,std::move(operations),budget,world);
    auto results=tx.results();if(!commitRpg(std::move(tx),world))std::terminate();return results;
}
std::shared_ptr<const SpatialQuote> Coordinator::spatialQuote(EntityId actor,SpatialSnapshot snapshot,std::optional<PathQuote> path) const {
    need(!busy_,ErrorCode::Reentrant,"Cannot quote during bridge preparation");return kernels_->rpg.spatialQuote(actor,std::move(snapshot),std::move(path));
}
void Coordinator::invalidateRpgCommands(){Guard guard(busy_);kernels_->rpg.invalidateCommands();}
namespace {
void receiptAgreement(const State&r,const q::Snapshot&qstate,const ContentId&source,std::uint64_t sequence){
    std::size_t count=0;
    for(auto&[award,disposition]:r.awards)if(award.source==source){
        wire::need(qstate.receipts.contains(award.receipt),"Award absent from quest receipts");++count;
    }
    wire::need(count==qstate.receipts.size(),"Quest receipt absent from award ledger");
    auto i=r.producers.find(producer());
    wire::need(i==r.producers.end()||i->second<=sequence,"Invalid bridge producer sequence");
}
}
std::vector<std::uint8_t> Coordinator::save() const {
    need(!busy_,ErrorCode::Reentrant,"Save during bridge preparation");
    receiptAgreement(rpgState(),questState(),source_,sequence_);
    auto r=kernels_->rpg.save();auto q=q::encodeSnapshot(*questCatalog_,questState());
    wire::Writer w;wire::put(w,source_);w.number(sequence_);wire::blob(w,r);wire::blob(w,q);return wire::wrap(3,w.bytes);
}
void Coordinator::restore(std::span<const std::uint8_t> bytes) {
    Guard guard(busy_);auto reader=wire::unwrap(bytes,3);
    wire::need(wire::get<ContentId>(reader)==source_,"Wrong bridge award source");auto sequence=reader.number();
    auto rbytes=wire::blob(reader);auto qbytes=wire::blob(reader);reader.end();
    auto r=decodeSnapshot(*rpgCatalog_,rbytes);auto q=q::decodeSnapshot(*questCatalog_,qbytes);
    wire::need(r.session==kernels_->rpg.session(),"Wrong session namespace");
    receiptAgreement(r.state,q,source_,sequence);
    r.state.generation=std::max(r.state.generation,rpgState().generation);
    r.state.revision=std::max(r.state.revision,rpgState().revision);
    q.generation=std::max(q.generation,questState().generation);
    q.revision=std::max(q.revision,questState().revision);
    auto candidate=std::make_unique<Kernels>(rpgCatalog_,questCatalog_,r.session,0);
    candidate->rpg.restore(r);candidate->quests.restore(q);
    auto owner=std::shared_ptr<const void>(std::make_shared<int>(0));
    // Everything above can fail without touching either live runtime or its queue.
    // No callbacks/allocations between swaps. Owner-thread observers cannot interleave.
    kernels_.swap(candidate);owner_.swap(owner);sequence_=sequence;
    // Retired runtimes are destroyed only after complete publication, under the guard.
}
} // namespace engine::rpg::quest_bridge
