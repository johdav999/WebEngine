#pragma once
#include "characters.hpp"

namespace example {
inline r::CatalogDefinition inventoryDefinitions(bool tools = false) {
    auto def = characterDefinitions(tools);
    const r::ContentId slot(tools ? "example.socket" : "example.hand");
    def.inventories.push_back({r::ContentId("example.pack"), 4, {slot}});
    def.inventories.push_back({r::ContentId("example.crate"), 2, {}});
    def.items.push_back({r::ContentId(tools ? "example.probe" : "example.blade"), 1, {slot},
        {{r::ModifierTarget::Attribute, r::ContentId(tools ? "example.precision" : "example.insight"),
          {tools ? 250 : 5, r::ContentId(tools ? "example.percent" : "example.point"), tools ? 100 : 1}}}});
    def.items.push_back({r::ContentId(tools ? "example.cell" : "example.tonic"), tools ? 8 : 20, {}, {}});
    return def;
}
} // namespace example
