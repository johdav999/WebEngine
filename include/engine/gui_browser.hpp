#pragma once
#include "engine/gui.hpp"

namespace engine {
// Optional Emscripten/browser adapter for the host's #viewport canvas. The caller
// owns the GUI; detach with nullptr before destroying it. Call sync after each
// frame so programmatic text, selection and visibility changes reach the bridge.
void attachBrowserGui(GuiSystem* gui);
void syncBrowserGui(bool requestFocus=false);
GuiSystem* browserGui();
}
