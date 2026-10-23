#pragma once
#include "engine/quests/runtime.hpp"

namespace quest_example {
namespace q = engine::quests;
inline engine::ModuleRegistration<q::Bindings> customRegistration() {
    engine::ModuleDescriptor d;
    d.id = "example.delivery";
    d.dependencies.push_back({"webengine.quests", {}});
    d.schemas.push_back({"example.delivery.weight", {1, 0, 0}});
    return {d, [](q::Bindings &b) {
                q::BindingSchema schema{1,
                                        {{"minimum", q::ValueType::Integer}},
                                        [](const q::Fields &p, const q::CatalogDefinition &d) {
                                            if (std::get<std::int64_t>(p.at("minimum")) < 1)
                                                throw q::Error(q::ErrorCode::InvalidContent,
                                                               "Minimum weight must be positive");
                                            auto e = d.events.find("demo.delivered");
                                            if (e == d.events.end() || !e->second.contains("weight") ||
                                                e->second.at("weight").index() != 1)
                                                throw q::Error(q::ErrorCode::TypeMismatch,
                                                               "Delivery module needs integer weight");
                                        }};
                b.addObjective("demo.heavy",
                               {std::move(schema), q::ObjectiveMode::Accumulate,
                                [](const q::Evaluation &e, const q::Fields &p) {
                                    return e.event && e.event->type == "demo.delivered" &&
                                                   std::get<std::int64_t>(e.event->payload.at("weight")) >=
                                                       std::get<std::int64_t>(p.at("minimum"))
                                               ? 1u
                                               : 0u;
                                }});
            }};
}
inline q::BindingSnapshot bindings() {
    engine::ModuleComposition<q::Bindings> c;
    std::vector<engine::ModuleRegistration<q::Bindings>> list{customRegistration(), q::moduleRegistration()};
    return c.replace(list);
}
inline q::CatalogDefinition definition() {
    using namespace q;
    CatalogDefinition d;
    d.id = "demo.catalog";
    d.content = "demo.catalog.v1";
    d.facts = {{"gate", false}};
    d.events = {{"demo.delivered", {{"item", std::string{}}, {"weight", std::int64_t{0}}}}};
    QuestDefinition delivery;
    delivery.id = "demo.delivery";
    delivery.content = "demo.delivery.v1";
    delivery.entry = "collect";
    Stage collect;
    collect.id = "collect";
    collect.objectives = {
        {"apples",
         ObjectiveMode::Accumulate,
         {"quest.event",
          1,
          {{"event", std::string("demo.delivered")},
           {"field", std::string("item")},
           {"value", std::string("apple")}}},
         2},
        {"heavy", ObjectiveMode::Accumulate, {"demo.heavy", 1, {{"minimum", std::int64_t{2}}}}, 1}};
    collect.transitions.push_back({{"quest.always", 1, {}}, "choose"});
    Stage choice;
    choice.id = "choose";
    choice.group = Group::Any;
    choice.choices = {"friendly", "quiet"};
    choice.objectives = {
        {"friendly", ObjectiveMode::Current, {"quest.choice", 1, {{"value", std::string("friendly")}}}},
        {"quiet", ObjectiveMode::Current, {"quest.choice", 1, {{"value", std::string("quiet")}}}}};
    choice.transitions = {{{"quest.choice", 1, {{"value", std::string("friendly")}}}, "wait"},
                          {{"quest.choice", 1, {{"value", std::string("quiet")}}}, "gate"}};
    Stage wait;
    wait.id = "wait";
    wait.objectives = {
        {"time", ObjectiveMode::Current, {"quest.elapsed", 1, {{"milliseconds", std::int64_t{500}}}}}};
    Stage gate;
    gate.id = "gate";
    gate.objectives = {
        {"open", ObjectiveMode::Current, {"quest.fact", 1, {{"key", std::string("gate")}, {"value", true}}}}};
    delivery.stages = {collect, choice, wait, gate};
    delivery.effects = {{"first-completion", "demo.coins", {{"amount", std::int64_t{20}}}}};
    QuestDefinition follow;
    follow.id = "demo.followup";
    follow.content = "demo.followup.v1";
    follow.entry = "gate";
    follow.prerequisites = {delivery.id};
    follow.stages = {gate};
    d.quests = {delivery, follow};
    return d;
}
inline std::shared_ptr<const q::Catalog> catalog() {
    auto installed = bindings();
    auto bytes = q::encodeCatalog(definition());
    return q::decodeCatalog(bytes, std::move(installed));
}
struct Demo {
    std::shared_ptr<const q::Catalog> content = catalog();
    q::Runtime runtime{content};
    int wallet = 0;
    bool rejected = false;
    void apply(q::Batch batch) {
        auto prepared = runtime.prepare(batch);
        if (!prepared.transaction)
            throw std::runtime_error("Demo budget exhausted");
        auto &tx = *prepared.transaction;
        int next = wallet;
        for (const auto &effect : tx.effects()) {
            if (effect.type != "demo.coins")
                throw std::runtime_error("Unknown effect");
            next += int(std::get<std::int64_t>(effect.payload.at("amount")));
        }
        if (!runtime.commit(std::move(tx)))
            throw std::runtime_error("Stale demo candidate");
        wallet = next;
    }
    void command(q::CommandKind kind, std::string choice = {}) {
        auto b = runtime.batch();
        b.commands.push_back(
            {"demo.delivery", kind, runtime.view("demo.delivery").activation, std::move(choice)});
        apply(std::move(b));
    }
    void deliver() {
        auto i = runtime.state().sequences.find("demo.input");
        auto seq = i == runtime.state().sequences.end() ? 1 : i->second + 1;
        runtime.post(runtime.event("demo.delivery", "demo.delivered", "demo.input", seq,
                                   {{"item", std::string("apple")}, {"weight", std::int64_t{2}}}));
        apply(runtime.batch());
    }
    void action(int which) {
        if (which == 0)
            command(q::CommandKind::Activate);
        else if (which == 1)
            deliver();
        else if (which == 2)
            command(q::CommandKind::Choose, "friendly");
        else if (which == 3) {
            auto b = runtime.batch();
            b.deltaMs = 500;
            apply(std::move(b));
        } else if (which == 4) {
            auto bytes = q::encodeSnapshot(*content, runtime.state());
            runtime.restore(q::decodeSnapshot(*content, bytes));
        } else if (which == 5)
            command(q::CommandKind::Retry);
        else if (which == 6) {
            auto b = runtime.batch();
            b.deltaMs = 500;
            const auto before = q::encodeSnapshot(*content, runtime.state());
            auto candidate = runtime.prepare(b);
            rejected = candidate.transaction && candidate.transaction->effects().size() == 1 &&
                       q::encodeSnapshot(*content, runtime.state()) == before;
        } else
            throw std::invalid_argument("Unknown demo action");
    }
    std::string label() const {
        const auto &p = runtime.view("demo.delivery");
        return p.status == q::Status::Completed ? "Completed" : p.stage.empty() ? "Available" : p.stage;
    }
    int progress() const {
        const auto &p = runtime.view("demo.delivery");
        return p.status == q::Status::Completed ? 5 : int(p.path.size());
    }
};
} // namespace quest_example
