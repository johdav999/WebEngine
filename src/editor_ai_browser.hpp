#pragma once
#include "engine/state_machine.hpp"
namespace engine { std::shared_ptr<const StateMachineDefinition> browserAiDefinition();void stopBrowserAiPreview();void loadBrowserAiDefinition(std::shared_ptr<const StateMachineDefinition>); }
