#pragma once
#include "engine/quests/bindings.hpp"

namespace sdk_example {
// The module publishes pure evaluators. Each game session owns its own facts.
engine::ModuleRegistration<engine::quests::Bindings> inventoryRegistration();
}
