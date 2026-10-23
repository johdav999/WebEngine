#include "consumer_bindings.hpp"
#include "engine/quests/runtime.hpp"
#include <iostream>

namespace q = engine::quests;
namespace {
void require(bool ok) { if (!ok) throw std::runtime_error("Quest SDK consumer contract failed"); }
std::size_t apply(q::Runtime& runtime, q::Batch batch, int& coins) {
    auto prepared = runtime.prepare(batch);
    require(prepared.status == q::PrepareStatus::Ready && prepared.transaction.has_value());
    int reward = 0;
    for (const auto& effect : prepared.transaction->effects()) {
        require(effect.type == "example.reward" && effect.receipt == "example.collect/first");
        reward += int(std::get<std::int64_t>(effect.payload.at("coins")));
    }
    const auto effects = prepared.transaction->effects().size();
    // Stage and validate the game's other state before publishing the kernel.
    const int candidateCoins = coins + reward;
    require(candidateCoins >= 0 && candidateCoins <= 1000);
    require(runtime.commit(std::move(*prepared.transaction)));
    coins = candidateCoins;
    return effects;
}
}
int main() {
    try {
        engine::ModuleComposition<q::Bindings> modules;
        std::vector<engine::ModuleRegistration<q::Bindings>> selected{
            q::moduleRegistration(), sdk_example::inventoryRegistration()};
        auto bindings = modules.replace(selected);
        q::CatalogDefinition definition;
        definition.id = "example.catalogue"; definition.content = "example.catalogue.v1";
        definition.facts.emplace("tokens", std::int64_t(0));
        q::QuestDefinition quest;
        quest.id = "example.collect"; quest.content = "example.collect.v1"; quest.entry = "collect";
        q::Stage stage; stage.id = "collect";
        stage.objectives.push_back({"inventory", q::ObjectiveMode::Current,
            {"example.inventory-at-least", 1, {{"fact", std::string("tokens")}, {"minimum", std::int64_t(2)}}}});
        quest.stages.push_back(std::move(stage));
        quest.effects.push_back({"first", "example.reward", {{"coins", std::int64_t(3)}}});
        definition.quests.push_back(std::move(quest));
        auto catalogue = q::Catalog::create(std::move(definition), std::move(bindings));
        q::Runtime first(catalogue), second(catalogue); int coins = 0;
        auto activate = first.batch();
        activate.commands.push_back({"example.collect", q::CommandKind::Activate, first.view("example.collect").activation, {}});
        require(apply(first, std::move(activate), coins) == 0);
        auto collected = first.batch(); collected.facts["tokens"] = std::int64_t(2);
        require(apply(first, std::move(collected), coins) == 1 && coins == 3);
        require(first.view("example.collect").earned && !second.view("example.collect").earned);
        const auto save = q::encodeSnapshot(*catalogue, first.state());
        q::Runtime loaded(catalogue); loaded.restore(q::decodeSnapshot(*catalogue, save));
        require(apply(loaded, loaded.batch(), coins) == 0 && coins == 3);
        // Removing the composition cannot invalidate a retained catalogue/provider.
        modules.replace({});
        require(apply(first, first.batch(), coins) == 0 && coins == 3);
        std::cout << "{\"sessions\":2,\"firstEarned\":true,\"secondEarned\":false,\"coins\":3,\"receipts\":1,\"reloadEffects\":0}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
