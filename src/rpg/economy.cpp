#include "engine/rpg/runtime.hpp"
#include <set>

namespace engine::rpg {
namespace {
void need(bool ok, ErrorCode code, const char* message) {
    if (!ok) throw Error(code, message);
}
const ResourceState& resource(const State& state, EntityId entity, const ContentId& id) {
    auto character = state.characters.find(entity);
    need(character != state.characters.end(), ErrorCode::MissingEntity, "Payment character missing");
    auto value = character->second.resources.find(id);
    need(value != character->second.resources.end(), ErrorCode::MissingValue, "Payment resource missing");
    return value->second;
}
void receipt(const State& state, const AwardId& id, std::vector<Operation>& out) {
    need(!state.awards.contains(id), ErrorCode::DuplicateAward, "Economic transaction already recorded");
    out.emplace_back(RecordAward{{id, AwardDisposition::Granted}});
}
}
// Expanded only against detached candidate state, with normal operation budgets.
std::vector<Operation> expandEconomy(const State& state, const Operation& operation) {
    need(!state.encounter, ErrorCode::InvalidEncounter, "Exit encounter before an economic transaction");
    std::vector<Operation> out;
    if (const auto* transfer = std::get_if<TransferResource>(&operation)) {
        need(transfer->payer != transfer->payee && transfer->amount.value > 0,
             ErrorCode::InvalidAmount, "Payment must be positive between distinct characters");
        const auto& payer = resource(state, transfer->payer, transfer->resource);
        const auto& payee = resource(state, transfer->payee, transfer->resource);
        need(payer.current >= transfer->amount.value, ErrorCode::InvalidAmount, "Insufficient funds");
        need(transfer->amount.value <= payee.maximum - payee.current,
             ErrorCode::Capacity, "Payment receiver lacks headroom");
        auto debit = transfer->amount;
        debit.value = -debit.value;
        out.emplace_back(AdjustResource{transfer->payer, transfer->resource, debit});
        out.emplace_back(AdjustResource{transfer->payee, transfer->resource, transfer->amount});
    } else if (const auto* trade = std::get_if<TradeItems>(&operation)) {
        receipt(state, trade->id, out);
        need(trade->source != trade->destination, ErrorCode::InvalidOwnership, "Trade inventories must differ");
        auto item = state.items.find(trade->item);
        need(item != state.items.end(), ErrorCode::MissingItem, "Trade stock missing");
        // Remove validates ownership, quantity and equipped state. New stack IDs are intentional.
        out.emplace_back(RemoveItem{trade->source, trade->item, trade->quantity});
        if (trade->payment) out.emplace_back(*trade->payment);
        out.emplace_back(AddItem{trade->destination, item->second.definition, trade->quantity});
    } else {
        const auto& craft = std::get<CraftItems>(operation);
        receipt(state, craft.id, out);
        need(!craft.ingredients.empty() && craft.ingredients.size() <= 32 &&
             !craft.products.empty() && craft.products.size() <= 32 && craft.payments.size() <= 16,
             ErrorCode::Limit, "Recipe bounds exceeded");
        std::set<ItemId> inputs;
        for (const auto& input : craft.ingredients) {
            need(inputs.insert(input.item).second, ErrorCode::InvalidQuantity, "Duplicate recipe input stack");
            out.emplace_back(input);
        }
        for (const auto& payment : craft.payments) out.emplace_back(payment);
        for (const auto& product : craft.products) out.emplace_back(product);
    }
    return out;
}
} // namespace engine::rpg
