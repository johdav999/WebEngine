#include "engine/rpg/runtime.hpp"
#include <limits>
#include <set>

namespace engine::rpg {
namespace {
void need(bool value, ErrorCode code, const char* message) {
    if (!value) throw Error(code, message);
}
void append(std::vector<Operation>& out, const std::vector<RewardGrant>& grants) {
    need(grants.size() <= Limits::operations - out.size(), ErrorCode::Limit, "Reward grants exceed hard cap");
    for (const auto& grant : grants) std::visit([&](const auto& value) { out.emplace_back(value); }, grant);
}
}
// Candidate state only. No recursion, callbacks or publication here.
std::vector<Operation> expandReward(State& state, Results& results, const Operation& operation, std::size_t& draws) {
    std::vector<Operation> out;
    if (const auto* reward = std::get_if<GrantReward>(&operation)) {
        need(!state.awards.contains(reward->id), ErrorCode::DuplicateAward, "Reward already granted or suppressed");
        need(reward->loot.size() <= Limits::rewardTables, ErrorCode::Limit, "Too many reward tables");
        out.emplace_back(RecordAward{{reward->id, AwardDisposition::Granted}});
        append(out, reward->grants);
        need(reward->loot.size() <= Limits::operations - out.size(), ErrorCode::Limit, "Reward bundle too large");
        for (const auto& table : reward->loot) out.emplace_back(table);
        return out;
    }
    const auto& table = std::get<RollLoot>(operation);
    need(!table.entries.empty() && table.entries.size() <= Limits::lootEntries &&
         table.rolls > 0 && table.rolls <= Limits::lootRolls, ErrorCode::Limit, "Invalid loot table bounds");
    std::uint64_t total = 0;
    std::size_t grants = 0;
    std::set<ContentId> ids;
    for (const auto& entry : table.entries) {
        need(entry.weight > 0 && ids.insert(entry.id).second, ErrorCode::InvalidDefinition, "Invalid loot weight or duplicate entry");
        total += entry.weight;
        need(total <= std::numeric_limits<std::uint32_t>::max(), ErrorCode::Overflow, "Loot weight sum overflow");
        need(entry.grants.size() <= Limits::operations - grants, ErrorCode::Limit, "Loot definition too large");
        grants += entry.grants.size();
    }
    need(table.rolls <= Limits::results - results.loot.size(), ErrorCode::Limit, "Too many loot results");
    Random random(state.random, draws);
    for (std::uint32_t roll = 0; roll < table.rolls; ++roll) {
        auto choice = random.uniform(static_cast<std::uint32_t>(total));
        for (const auto& entry : table.entries) {
            if (choice < entry.weight) {
                append(out, entry.grants);
                results.loot.push_back({table.table, entry.id, roll});
                break;
            }
            choice -= entry.weight;
        }
    }
    return out;
}
} // namespace engine::rpg
