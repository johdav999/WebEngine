#pragma once
#include "builds.hpp"
namespace example {
inline r::CatalogDefinition economyDefinitions() {
    auto d = buildDefinitions();
    d.characters[0].resources.push_back({r::ContentId("example.coins"), r::ContentId("example.coin"), 1, 0, 100, 100, 10});
    return d;
}
} // namespace example
