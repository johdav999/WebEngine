#include "engine/rpg/runtime.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>

namespace engine::rpg {
void applyInventory(State&, Results&, const Catalog&, SessionId, const Operation&);
void applyAbilities(State&, Results&, const Catalog&, SessionId, const Operation&, std::size_t&, std::size_t&);
void applyEncounter(State&, Results&, const Catalog&, SessionId, const Operation&, std::size_t&, std::size_t&, std::size_t&);
void reconcileEncounter(State&, Results&, const Catalog&, SessionId, std::size_t&, std::size_t&, std::size_t&);
void applyTactics(State&, Results&, const Catalog&, SessionId, const TacticalAction&, Budget, std::size_t&, std::size_t&, std::size_t&);
namespace {
std::int64_t add(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
        throw Error(ErrorCode::Overflow, "RPG integer addition overflow");
    return a + b;
}
std::uint64_t increment(std::uint64_t value) {
    if (value == std::numeric_limits<std::uint64_t>::max())
        throw Error(ErrorCode::Overflow, "RPG generation/revision exhausted");
    return value + 1;
}
void amount(const Amount& value, const ContentId& unit, std::int64_t scale) {
    if (value.unit != unit || value.scale != scale)
        throw Error(ErrorCode::InvalidAmount, "Amount unit or scale mismatch");
}
void range(std::int64_t value, std::int64_t low, std::int64_t high) {
    if (value < low || value > high) throw Error(ErrorCode::InvalidAmount, "Amount outside bounds");
}
template<class T> const T& definition(const std::vector<T>& entries, const ContentId& id) {
    auto found = std::find_if(entries.begin(), entries.end(), [&](const T& entry) { return entry.id == id; });
    if (found == entries.end()) throw Error(ErrorCode::MissingValue, "Unknown character value");
    return *found;
}
EntityId allocate(State& state, SessionId session) {
    EntityIds ids(session, state.nextEntity);
    auto result = ids.allocate();
    state.nextEntity = ids.next();
    return result;
}
void recompute(CharacterState& state, const CharacterDefinition& def) {
    std::size_t total = 0;
    for (const auto& [source, mods] : state.modifiers) total += mods.size();
    for (const auto& [source, mods] : state.equipmentModifiers) total += mods.size();
    for (const auto& [source, status] : state.statuses) total += status.modifiers.size() * status.stacks;
    if (total > Limits::modifiers) throw Error(ErrorCode::Limit, "Combined modifier limit exceeded");
    for (const auto& value : def.attributes) {
        auto& result = state.attributes.at(value.id);
        auto sum = result.base;
        for (const auto& [source, modifiers] : state.modifiers)
            for (const auto& modifier : modifiers)
                if (modifier.kind == ModifierTarget::Attribute && modifier.target == value.id)
                    sum = add(sum, modifier.delta.value);
        for (const auto& [item, modifiers] : state.equipmentModifiers)
            for (const auto& modifier : modifiers)
                if (modifier.kind == ModifierTarget::Attribute && modifier.target == value.id)
                    sum = add(sum, modifier.delta.value);
        for (const auto& [source, status] : state.statuses)
            for (std::uint32_t stack = 0; stack < status.stacks; ++stack)
                for (const auto& modifier : status.modifiers)
                    if (modifier.kind == ModifierTarget::Attribute && modifier.target == value.id)
                        sum = add(sum, modifier.delta.value);
        result.value = std::clamp(sum, value.minimum, value.maximum);
    }
    for (const auto& value : def.resources) {
        auto& result = state.resources.at(value.id);
        auto sum = result.baseMaximum;
        for (const auto& [source, modifiers] : state.modifiers)
            for (const auto& modifier : modifiers)
                if (modifier.kind == ModifierTarget::ResourceMaximum && modifier.target == value.id)
                    sum = add(sum, modifier.delta.value);
        for (const auto& [item, modifiers] : state.equipmentModifiers)
            for (const auto& modifier : modifiers)
                if (modifier.kind == ModifierTarget::ResourceMaximum && modifier.target == value.id)
                    sum = add(sum, modifier.delta.value);
        for (const auto& [source, status] : state.statuses)
            for (std::uint32_t stack = 0; stack < status.stacks; ++stack)
                for (const auto& modifier : status.modifiers)
                    if (modifier.kind == ModifierTarget::ResourceMaximum && modifier.target == value.id)
                        sum = add(sum, modifier.delta.value);
        result.maximum = std::clamp(sum, value.minimumMaximum, value.maximumMaximum);
        result.current = std::min(result.current, result.maximum);
    }
}
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) {
        if (busy) throw Error(ErrorCode::Reentrant, "RPG mutation during preparation");
        busy = true;
    }
    ~Guard() { busy = false; }
};
}
void recomputeEquipment(CharacterState& state, const CharacterDefinition& def) { recompute(state, def); }
const State& Transaction::state() const {
    if (consumed_ || !state_) throw Error(ErrorCode::ConsumedTransaction, "Transaction is consumed or moved from");
    return *state_;
}
Runtime::Runtime(std::shared_ptr<const Catalog> catalog, SessionId session, std::uint64_t seed)
    : catalog_(std::move(catalog)), session_(session) {
    if (!catalog_) throw Error(ErrorCode::MissingModule, "RPG runtime requires a catalogue");
    auto initial = std::make_shared<State>(); initial->random = seedRandom(seed); state_ = std::move(initial);
}
Command Runtime::command(ContentId producer, std::uint64_t sequence, std::vector<Operation> operations) const {
    if (operations.empty() || operations.size() > Limits::operations)
        throw Error(ErrorCode::Limit, "Command requires 1-256 operations");
    return Command(owner_, state_->generation, state_->revision, std::move(producer), sequence, std::move(operations));
}
Transaction Runtime::prepare(const Command& command, Budget budget, std::optional<WorldStamp> world) {
    Guard guard(busy_);
    if (command.owner_ != owner_ || command.generation_ != state_->generation || command.revision_ != state_->revision)
        throw Error(ErrorCode::StaleCommand, "Foreign or stale command");
    const auto watermark = state_->producers.find(command.producer_);
    if (!command.sequence_ || (watermark != state_->producers.end() && command.sequence_ <= watermark->second))
        throw Error(ErrorCode::InvalidSequence, "Duplicate or invalid producer sequence");
    if (watermark == state_->producers.end() && state_->producers.size() >= Limits::producers)
        throw Error(ErrorCode::Limit, "Too many command producers");
    if (budget.operations > Limits::operations || budget.advancements > Limits::advancements ||
        budget.effects > Limits::effects || budget.randomDraws > Limits::randomDraws || budget.encounterWork > Limits::encounterWork || budget.reactions > Limits::reactions || budget.reactionDepth > Limits::reactionDepth)
        throw Error(ErrorCode::Limit, "Budget exceeds hard limit");
    if (command.operations_.size() > budget.operations)
        throw Error(ErrorCode::BudgetExceeded, "Operation budget exhausted");
    for (const auto& op : command.operations_) {
        const bool ack = std::holds_alternative<AcknowledgePresentation>(op);
        const bool action = std::holds_alternative<TacticalAction>(op);
        if ((ack || action) && command.operations_.size() != 1)
            throw Error(ErrorCode::InvalidAction, "Tactical actions/acknowledgements must be standalone commands");
        if (state_->presentation && !ack) throw Error(ErrorCode::PresentationPending, "Recover or finish presentation before mutation");
    }
    auto candidate = std::make_shared<State>(*state_);
    candidate->revision = increment(state_->revision);
    Transaction transaction;
    transaction.owner_ = owner_; transaction.generation_ = state_->generation; transaction.revision_ = state_->revision;
    auto& results = transaction.results_;
    std::size_t advancements = 0, effects = budget.effects, draws = budget.randomDraws, work = budget.encounterWork;
    for (const auto& operation : command.operations_) {
      std::visit([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, RecordAward>) {
            const auto& award = op.award;
            if (award.id.receipt.empty() || award.id.receipt.size() > Limits::receiptBytes ||
                !std::all_of(award.id.receipt.begin(),award.id.receipt.end(),[](unsigned char c){return c >= 33 && c <= 126;}) ||
                (award.disposition != AwardDisposition::Granted && award.disposition != AwardDisposition::ReplaySuppressed))
                throw Error(ErrorCode::InvalidAward, "Invalid award receipt/disposition");
            if (candidate->awards.contains(award.id)) throw Error(ErrorCode::DuplicateAward, "Award already recorded");
            if (candidate->awards.size() >= Limits::awards) throw Error(ErrorCode::Limit, "Award ledger is full");
            candidate->awards.emplace(award.id,award.disposition); results.awards.push_back(award);
        } else if constexpr (std::is_same_v<T, TacticalAction>) {
            const auto& quote = op.spatial;
            if (!quote || quote->owner_ != owner_ || quote->generation_ != state_->generation ||
                quote->revision_ != state_->revision || quote->actor_ != op.actor ||
                quote->encounter_ != op.encounterEpoch || quote->turn_ != op.turnEpoch)
                throw Error(ErrorCode::InvalidSpatial, "Missing, foreign or stale spatial quote");
            if (!world || *world != quote->snapshot_.world) throw Error(ErrorCode::StaleWorld, "Spatial world revision changed");
            transaction.world_ = world;
            applyTactics(*candidate, results, *catalog_, session_, op, budget, effects, draws, work);
        } else if constexpr (std::is_same_v<T, AcknowledgePresentation>) {
            const auto& pending = candidate->presentation;
            if (!pending || op.actionEpoch != pending->actionEpoch || op.encounterEpoch != pending->encounterEpoch || op.turnEpoch != pending->turnEpoch)
                throw Error(ErrorCode::StalePresentation, "Missing or stale presentation acknowledgement");
            if (op.completion != PresentationCompletion::Completed && op.completion != PresentationCompletion::Skipped && op.completion != PresentationCompletion::Recovered)
                throw Error(ErrorCode::InvalidAction, "Invalid presentation completion");
            results.presentations.push_back({op.actionEpoch,op.completion});
            candidate->presentation.reset();
        } else if constexpr (std::is_same_v<T, SetTacticalPosition>) {
            if (candidate->encounter) throw Error(ErrorCode::InvalidAction, "Encounter positions change only through tactical moves");
            if (!candidate->characters.contains(op.entity)) throw Error(ErrorCode::MissingEntity, "Unknown tactical actor");
            candidate->positions.insert_or_assign(op.entity,op.position);
        } else if constexpr (std::is_same_v<T, BeginEncounter> || std::is_same_v<T, EndTurn> ||
            std::is_same_v<T, FinishEncounter> || std::is_same_v<T, ExitEncounter> || std::is_same_v<T, SetCooldown>) {
            applyEncounter(*candidate, results, *catalog_, session_, operation, effects, draws, work);
        } else if constexpr (std::is_same_v<T, CreateCharacter>) {
            const auto* def = catalog_->character(op.definition);
            if (!def) throw Error(ErrorCode::MissingDefinition, "Unknown character definition");
            if (candidate->characters.size() >= Limits::characters) throw Error(ErrorCode::Limit, "Character limit reached");
            CharacterState value{def->id};
            for (const auto& attr : def->attributes) value.attributes.emplace(attr.id, AttributeState{attr.initial, attr.initial});
            for (const auto& resource : def->resources)
                value.resources.emplace(resource.id, ResourceState{resource.initialMaximum, resource.initialMaximum, resource.initialCurrent});
            auto id = allocate(*candidate, session_);
            candidate->characters.emplace(id, std::move(value));
            results.created.push_back(id);
        } else if constexpr (std::is_same_v<T, CreateInventory> || std::is_same_v<T, AddItem> ||
            std::is_same_v<T, RemoveItem> || std::is_same_v<T, SplitItem> || std::is_same_v<T, MergeItems> ||
            std::is_same_v<T, TransferItem> || std::is_same_v<T, EquipItem> || std::is_same_v<T, UnequipItem>) {
            applyInventory(*candidate, results, *catalog_, session_, operation);
        } else if constexpr (std::is_same_v<T, UseAbility> || std::is_same_v<T, ApplyStatus> ||
            std::is_same_v<T, DispelStatus> || std::is_same_v<T, AdvancePhase>) {
            if constexpr (std::is_same_v<T, UseAbility>)
                if (candidate->encounter) throw Error(ErrorCode::InvalidAction, "Use TacticalAction for encounter abilities/items");
            if constexpr (std::is_same_v<T, AdvancePhase>)
                if (candidate->encounter) throw Error(ErrorCode::InvalidEncounter, "Encounter owns phase advancement until exit");
            applyAbilities(*candidate, results, *catalog_, session_, operation, effects, draws);
        } else {
            const auto found = candidate->characters.find(op.entity);
            if (found == candidate->characters.end()) throw Error(ErrorCode::MissingEntity, "Unknown or foreign character");
            auto& character = found->second;
            const auto& def = *catalog_->character(character.definition);
            if constexpr (std::is_same_v<T, SetAttributeBase>) {
                const auto& attr = definition(def.attributes, op.attribute);
                amount(op.amount, attr.unit, attr.scale); range(op.amount.value, attr.minimum, attr.maximum);
                character.attributes.at(attr.id).base = op.amount.value;
                recompute(character, def);
            } else if constexpr (std::is_same_v<T, AdjustResource>) {
                const auto& resource = definition(def.resources, op.resource);
                amount(op.delta, resource.unit, resource.scale);
                auto& value = character.resources.at(resource.id);
                auto next = add(value.current, op.delta.value);
                range(next, 0, value.maximum); value.current = next;
            } else if constexpr (std::is_same_v<T, SetModifiers>) {
                if (op.modifiers.empty() || op.modifiers.size() > Limits::modifiers)
                    throw Error(ErrorCode::Limit, "Modifier source requires 1-256 modifiers");
                if (!character.modifiers.contains(op.source) && character.modifiers.size() >= Limits::modifierSources)
                    throw Error(ErrorCode::Limit, "Too many modifier sources");
                auto modifiers = op.modifiers;
                std::sort(modifiers.begin(), modifiers.end(), [](const Modifier& a, const Modifier& b) {
                    return a.kind == b.kind ? a.target < b.target : a.kind < b.kind;
                });
                for (std::size_t i = 0; i < modifiers.size(); ++i) {
                    const auto& modifier = modifiers[i];
                    if (i && modifiers[i - 1].kind == modifier.kind && modifiers[i - 1].target == modifier.target)
                        throw Error(ErrorCode::InvalidModifier, "Duplicate modifier target in source");
                    if (modifier.kind == ModifierTarget::Attribute) {
                        const auto& target = definition(def.attributes, modifier.target);
                        amount(modifier.delta, target.unit, target.scale);
                    } else if (modifier.kind == ModifierTarget::ResourceMaximum) {
                        const auto& target = definition(def.resources, modifier.target);
                        amount(modifier.delta, target.unit, target.scale);
                    } else throw Error(ErrorCode::InvalidModifier, "Unknown modifier target kind");
                }
                character.modifiers.insert_or_assign(op.source, std::move(modifiers));
                std::size_t total = 0;
                for (const auto& [source, entries] : character.modifiers) total += entries.size();
                for (const auto& [item, entries] : character.equipmentModifiers) total += entries.size();
                if (total > Limits::modifiers) throw Error(ErrorCode::Limit, "Too many character modifiers");
                recompute(character, def);
            } else if constexpr (std::is_same_v<T, RemoveModifiers>) {
                if (!character.modifiers.erase(op.source)) throw Error(ErrorCode::InvalidModifier, "Unknown modifier source");
                recompute(character, def);
            } else if constexpr (std::is_same_v<T, AddExperience>) {
                if (!def.progression) throw Error(ErrorCode::MissingDefinition, "Character has no progression");
                const auto& progress = *catalog_->progression(*def.progression);
                amount(op.amount, progress.unit, progress.scale);
                if (op.amount.value < 0) throw Error(ErrorCode::InvalidAmount, "Experience awards cannot be negative");
                auto xp = add(character.experience, op.amount.value);
                range(xp, 0, progress.maximumExperience);
                character.experience = xp;
                while (character.level < progress.thresholds.size() && xp >= progress.thresholds[character.level]) {
                    if (++advancements > budget.advancements) throw Error(ErrorCode::BudgetExceeded, "Advancement budget exhausted");
                    const auto previous = character.level;
                    UnlockProposal proposal;
                    if (progress.handler)
                        proposal = catalog_->bindings()->registry().findAdvancement(progress.handler->id)->evaluate(
                            {op.entity, character, previous, previous + 1});
                    if (!proposal.accepted) throw Error(ErrorCode::RejectedUnlock, "Game rejected advancement");
                    if (proposal.unlocks.size() > Limits::unlocks) throw Error(ErrorCode::Limit, "Unlock proposal too large");
                    std::sort(proposal.unlocks.begin(), proposal.unlocks.end());
                    if (std::adjacent_find(proposal.unlocks.begin(), proposal.unlocks.end()) != proposal.unlocks.end())
                        throw Error(ErrorCode::RejectedUnlock, "Duplicate proposed unlock");
                    AdvancementResult result{op.entity, previous, previous + 1, {}};
                    for (const auto& unlock : proposal.unlocks) {
                        if (!std::binary_search(progress.allowedUnlocks.begin(), progress.allowedUnlocks.end(), unlock))
                            throw Error(ErrorCode::RejectedUnlock, "Undeclared unlock");
                        if (character.unlocks.insert(unlock).second) result.granted.push_back(unlock);
                    }
                    if (character.unlocks.size() > Limits::unlocks) throw Error(ErrorCode::Limit, "Character unlock limit reached");
                    ++character.level;
                    results.advancements.push_back(std::move(result));
                }
            }
        }
    }, operation);
      // Lifecycle operations reconcile internally; other mutations check outcomes and active eligibility.
      if (!std::holds_alternative<BeginEncounter>(operation) && !std::holds_alternative<EndTurn>(operation) &&
          !std::holds_alternative<FinishEncounter>(operation) && !std::holds_alternative<ExitEncounter>(operation) &&
          !std::holds_alternative<TacticalAction>(operation) && !std::holds_alternative<AcknowledgePresentation>(operation) && !std::holds_alternative<RecordAward>(operation))
          reconcileEncounter(*candidate, results, *catalog_, session_, effects, draws, work);
    }
    if (results.created.size() + results.advancements.size() + results.inventories.size() + results.items.size() +
        results.resources.size() + results.statuses.size() + results.abilities.size() + results.encounters.size() + results.cooldowns.size() + results.actions.size() + results.reactions.size() + results.presentations.size() + results.awards.size() > Limits::results)
        throw Error(ErrorCode::Limit, "Result record limit exceeded");
    candidate->producers.insert_or_assign(command.producer_, command.sequence_);
    transaction.state_ = std::move(candidate);
    return transaction;
}
bool Runtime::canCommit(const Transaction& transaction, std::optional<WorldStamp> world) const noexcept {
    return (!transaction.world_ || (world && *world == *transaction.world_)) && !busy_ && !transaction.consumed_ && transaction.state_ && transaction.owner_ == owner_ &&
        transaction.generation_ == state_->generation && transaction.revision_ == state_->revision;
}
bool Runtime::commit(Transaction&& transaction, std::optional<WorldStamp> world) noexcept {
    if (!canCommit(transaction, world)) return false;
    state_.swap(transaction.state_); // Retire old state with the transaction, outside publication.
    transaction.consumed_ = true;
    return true;
}
EntityId Runtime::allocateEntityId() {
    Guard guard(busy_);
    if (state_->presentation) throw Error(ErrorCode::PresentationPending, "Presentation is pending");
    auto candidate = std::make_shared<State>(*state_);
    candidate->revision = increment(state_->revision);
    auto id = allocate(*candidate, session_);
    state_ = std::move(candidate);
    return id;
}
void Runtime::invalidateCommands() {
    Guard guard(busy_);
    auto candidate = std::make_shared<State>(*state_);
    candidate->generation = increment(state_->generation);
    candidate->revision = increment(state_->revision);
    state_ = std::move(candidate);
}
} // namespace engine::rpg
