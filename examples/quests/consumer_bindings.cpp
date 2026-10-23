#include "consumer_bindings.hpp"

namespace sdk_example {
engine::ModuleRegistration<engine::quests::Bindings> inventoryRegistration() {
    namespace q = engine::quests;
    engine::ModuleDescriptor descriptor;
    descriptor.id = "example.inventory";
    descriptor.dependencies.push_back({"webengine.quests", {}});
    descriptor.schemas.push_back({"example.inventory-at-least", {1, 0, 0}});
    return {std::move(descriptor), [](q::Bindings& bindings) {
        q::BindingSchema schema;
        schema.parameters = {{"fact", q::ValueType::String}, {"minimum", q::ValueType::Integer}};
        schema.validate = [](const q::Fields& fields, const q::CatalogDefinition& definition) {
            const auto& key = std::get<std::string>(fields.at("fact"));
            auto found = definition.facts.find(key);
            if (found == definition.facts.end() || !std::holds_alternative<std::int64_t>(found->second) ||
                std::get<std::int64_t>(fields.at("minimum")) < 0)
                throw std::invalid_argument("Inventory binding needs a declared integer fact and nonnegative minimum");
        };
        bindings.addObjective("example.inventory-at-least", {std::move(schema), q::ObjectiveMode::Current,
            [](const q::Evaluation& evaluation, const q::Fields& fields) -> std::uint32_t {
                return std::get<std::int64_t>(evaluation.facts.at(std::get<std::string>(fields.at("fact")))) >=
                       std::get<std::int64_t>(fields.at("minimum"));
            }});
    }};
}
}
