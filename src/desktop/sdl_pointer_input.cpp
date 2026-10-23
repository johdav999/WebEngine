#include "sdl_pointer_input.hpp"
#include "sdl_display.hpp"
#include <cmath>
#include <limits>
namespace engine {
bool SdlPointerInput::ownsGui() const {
    for(const auto& [key,c]:contacts_)if(c.owner==Owner::Gui)return true;
    return false;
}
bool SdlPointerInput::send(Contact& c,InputEventType type,Vec2 logical,Vec2 pixels,Uint64 timestamp) {
    InputPacket p;p.type=int(type);p.id=c.id;p.device=int(c.device);p.x=c.position.x;p.y=c.position.y;
    p.pixelX=p.x*pixels.x/std::max(1.f,logical.x);p.pixelY=p.y*pixels.y/std::max(1.f,logical.y);
    p.pressure=c.pressure;p.buttons=c.buttons;p.timestamp=double(timestamp)/1e9;
    return submit_({&p,1});
}
void SdlPointerInput::cancel() {
    ++generation_;const bool gui=ownsGui();auto previous=std::move(contacts_);contacts_.clear();
    if(gui)gui_.pointer(GuiPointer::Cancel,{});
    for(auto& [key,c]:previous)if(c.owner==Owner::Scene)send(c,InputEventType::PointerCancel,{1,1},{1,1},0);
}
bool SdlPointerInput::event(const SDL_Event& e,SDL_Window& window,Vec2 logical,Vec2 pixels,bool gameplay,bool mouseOwnsGui) {
    Key key;Vec2 position{};float pressure=-1;std::uint32_t buttons=0;SDL_WindowID windowId=0;
    bool down=false,up=false,cancelled=false,pen=false;
    if(e.type>=SDL_EVENT_FINGER_DOWN&&e.type<=SDL_EVENT_FINGER_CANCELED) {
        const auto& f=e.tfinger;if(f.touchID==SDL_MOUSE_TOUCHID||f.touchID==SDL_PEN_TOUCHID)return true;
        key={1,f.touchID,f.fingerID};position={f.x*logical.x,f.y*logical.y};pressure=f.pressure;windowId=f.windowID;
        down=e.type==SDL_EVENT_FINGER_DOWN;up=e.type==SDL_EVENT_FINGER_UP;cancelled=e.type==SDL_EVENT_FINGER_CANCELED;buttons=up||cancelled?0:1;
    } else {
        pen=true;
        switch(e.type) {
        case SDL_EVENT_PEN_DOWN:case SDL_EVENT_PEN_UP:
            key={2,e.ptouch.which,0};position={e.ptouch.x,e.ptouch.y};windowId=e.ptouch.windowID;buttons=e.ptouch.pen_state;
            down=e.type==SDL_EVENT_PEN_DOWN;up=e.type==SDL_EVENT_PEN_UP;break;
        case SDL_EVENT_PEN_MOTION:
            key={2,e.pmotion.which,0};position={e.pmotion.x,e.pmotion.y};windowId=e.pmotion.windowID;buttons=e.pmotion.pen_state;break;
        case SDL_EVENT_PEN_AXIS:
            if(e.paxis.axis!=SDL_PEN_AXIS_PRESSURE)return true;
            key={2,e.paxis.which,0};position={e.paxis.x,e.paxis.y};windowId=e.paxis.windowID;buttons=e.paxis.pen_state;pressure=e.paxis.value;break;
        case SDL_EVENT_PEN_BUTTON_DOWN:case SDL_EVENT_PEN_BUTTON_UP:
            key={2,e.pbutton.which,0};position={e.pbutton.x,e.pbutton.y};windowId=e.pbutton.windowID;buttons=e.pbutton.pen_state;break;
        case SDL_EVENT_PEN_PROXIMITY_OUT:
            key={2,e.pproximity.which,0};cancelled=true;break;
        default:return false;
        }
        const float units=desktopCoordinateScale(window);position={position.x/units,position.y/units};
        // Portable buttons match PointerEvent: tip=left, barrel1=right,
        // barrel2=middle, eraser=bit5. Proximity is not a button.
        buttons=((buttons&SDL_PEN_INPUT_DOWN)?1u:0u)|((buttons&SDL_PEN_INPUT_BUTTON_1)?2u:0u)|
            ((buttons&SDL_PEN_INPUT_BUTTON_2)?4u:0u)|((buttons&SDL_PEN_INPUT_ERASER_TIP)?32u:0u);
    }
    if(!cancelled&&windowId!=SDL_GetWindowID(&window))return true;
    if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(pressure)||pressure>1||pressure< -1||(!pen&&pressure<0))return true;
    auto it=contacts_.find(key);
    if(it==contacts_.end()) {
        if(up||cancelled||(!down&&!pen))return true;
        if(contacts_.size()>=32||nextId_==std::numeric_limits<int>::max()){cancel();return true;}
        it=contacts_.emplace(key,Contact{nextId_++,pen?InputDevice::Pen:InputDevice::Touch}).first;
    }
    auto& c=it->second;
    const auto generation=generation_;
    if(down&&c.down)return true;
    if(!cancelled)c.position=position;
    if(pressure>=0)c.pressure=pressure;
    c.buttons=buttons;
    if(down) {
        c.down=true;
        const bool busy=mouseOwnsGui||ownsGui();
        c.owner=Owner::Ignored;
        const bool accepted=!busy&&gui_.pointer(GuiPointer::Down,position);
        if(generation!=generation_)return true;
        if(accepted)c.owner=Owner::Gui;
        else if(gameplay&&(!busy||!gui_.wheel(position)))c.owner=Owner::Scene;
    } else if(!c.down&&!up&&!cancelled) {
        // Pen hover never takes over another device's GUI drag.
        const bool consumed=!mouseOwnsGui&&!ownsGui()&&gui_.pointer(GuiPointer::Move,position);
        if(generation!=generation_)return true;
        c.owner=consumed?Owner::Ignored:gameplay?Owner::Scene:Owner::Ignored;
    }
    const auto action=cancelled?GuiPointer::Cancel:up?GuiPointer::Up:GuiPointer::Move;
    if(c.owner==Owner::Gui&&!down)gui_.pointer(action,position);
    if(generation!=generation_)return true;
    if(c.owner==Owner::Scene) {
        if(!gameplay&&!cancelled){send(c,InputEventType::PointerCancel,logical,pixels,e.common.timestamp);c.owner=Owner::Ignored;}
        else if(!send(c,cancelled?InputEventType::PointerCancel:down?InputEventType::PointerDown:up?InputEventType::PointerUp:InputEventType::PointerMove,logical,pixels,e.common.timestamp)) {
            cancel();return true;
        }
    }
    if(up||cancelled)contacts_.erase(it);
    return true;
}
}
