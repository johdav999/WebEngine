#pragma once
#include "engine/gui.hpp"
#include "engine/input.hpp"
#include <SDL3/SDL.h>
#include <map>
#include <tuple>

namespace engine {
// Owner-thread contact routing. SDL device IDs never cross the portable ABI.
class SdlPointerInput {
public:
    using Submit=std::function<bool(std::span<const InputPacket>)>;
    SdlPointerInput(GuiSystem& gui,Submit submit):gui_(gui),submit_(std::move(submit)) {}
    // Mouse remains in DesktopHost; both paths share one GUI capture owner.
    bool ownsGui() const;
    bool event(const SDL_Event&,SDL_Window&,Vec2 logical,Vec2 pixels,bool gameplay,bool mouseOwnsGui);
    void cancel();
private:
    enum class Owner { Scene,Gui,Ignored };
    struct Contact { int id;InputDevice device;Vec2 position{};float pressure=0;std::uint32_t buttons=0;Owner owner=Owner::Ignored;bool down=false; };
    using Key=std::tuple<int,Uint64,Uint64>;
    GuiSystem& gui_;Submit submit_;
    std::map<Key,Contact> contacts_;
    int nextId_=2;
    std::uint64_t generation_=0;
    bool send(Contact&,InputEventType,Vec2,Vec2,Uint64);
};
}
