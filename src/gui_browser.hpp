#pragma once
#include "engine/gui_browser.hpp"
#include <functional>

namespace engine {
void buildGuiDemo(std::function<void()> resetCamera);
}
