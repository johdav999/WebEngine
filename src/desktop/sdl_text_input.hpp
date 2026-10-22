#pragma once
#include "engine/gui.hpp"
#include <SDL3/SDL.h>

namespace engine {
// Private owner-thread adapter. Borrows the host's window and retained GUI.
// No retained widget pointers or asynchronous callbacks cross focus changes.
class SdlTextInput {
public:
    SdlTextInput(SDL_Window& window,GuiSystem& gui):window_(window),gui_(gui) {}
    ~SdlTextInput(); // Stops OS input without invoking application widget hooks.
    void sync(bool enabled=true);
    void cancel();
    bool key(const SDL_KeyboardEvent& event);
    bool event(const SDL_Event& event);
private:
    SDL_Window& window_;GuiSystem& gui_;
    std::uint64_t focus_=0,composition_=0;
    std::uint32_t revision_=0;
    std::string committed_;
    std::size_t start_=0,end_=0;
    bool backward_=false;
    void cancelComposition();
    void replace(std::string value);
};
}
