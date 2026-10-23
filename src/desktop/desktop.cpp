#include "engine/desktop.hpp"
#include "desktop_graphics.hpp"
#include "sdl_text_input.hpp"
#include "desktop_files.hpp"
#include "windows_lifecycle.hpp"
#include "sdl_display.hpp"
#include "sdl_pointer_input.hpp"
#include "windows_accessibility.hpp"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <map>
#include <thread>

namespace engine {
namespace {
std::atomic_flag activeHost=ATOMIC_FLAG_INIT;
struct SdlRuntime {
    SdlRuntime() {
        if(activeHost.test_and_set()) throw std::logic_error("Only one DesktopHost may own the SDL event queue");
        SDL_SetMainReady();
        if(!SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) {
            const std::string error=SDL_GetError();SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD);activeHost.clear();
            throw std::runtime_error(error);
        }
    }
    ~SdlRuntime() { SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD);activeHost.clear(); }
};
using Window=std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>;
using Gamepad=std::unique_ptr<SDL_Gamepad,decltype(&SDL_CloseGamepad)>;
void check(bool success) { if(!success) throw std::runtime_error(SDL_GetError()); }
std::filesystem::path utf8Path(const char* text) {
    const std::string value=text?text:"";return std::filesystem::path(std::u8string(value.begin(),value.end()));
}
DesktopControl keyControl(SDL_Scancode key) {
    if(key>=SDL_SCANCODE_A&&key<=SDL_SCANCODE_Z) return DesktopControl(controlId(DesktopControl::A)+key-SDL_SCANCODE_A);
    switch(key) {
    case SDL_SCANCODE_UP:return DesktopControl::Up;case SDL_SCANCODE_DOWN:return DesktopControl::Down;
    case SDL_SCANCODE_LEFT:return DesktopControl::Left;case SDL_SCANCODE_RIGHT:return DesktopControl::Right;
    case SDL_SCANCODE_SPACE:return DesktopControl::Space;case SDL_SCANCODE_LSHIFT:return DesktopControl::LeftShift;
    case SDL_SCANCODE_RSHIFT:return DesktopControl::RightShift;case SDL_SCANCODE_ESCAPE:return DesktopControl::Escape;
    case SDL_SCANCODE_RETURN:return DesktopControl::Enter;case SDL_SCANCODE_TAB:return DesktopControl::Tab;
    case SDL_SCANCODE_F11:return DesktopControl::F11;default:return DesktopControl::Unknown;
    }
}
std::optional<GuiKey> guiKey(SDL_Scancode key) {
    switch(key) {
    case SDL_SCANCODE_TAB:return GuiKey::Tab;case SDL_SCANCODE_RETURN:case SDL_SCANCODE_KP_ENTER:return GuiKey::Enter;
    case SDL_SCANCODE_SPACE:return GuiKey::Space;case SDL_SCANCODE_ESCAPE:return GuiKey::Escape;
    case SDL_SCANCODE_UP:return GuiKey::Up;case SDL_SCANCODE_DOWN:return GuiKey::Down;
    case SDL_SCANCODE_LEFT:return GuiKey::Left;case SDL_SCANCODE_RIGHT:return GuiKey::Right;
    case SDL_SCANCODE_PAGEUP:return GuiKey::PageUp;case SDL_SCANCODE_PAGEDOWN:return GuiKey::PageDown;
    case SDL_SCANCODE_HOME:return GuiKey::Home;case SDL_SCANCODE_END:return GuiKey::End;
    default:return {};
    }
}
void validate(const DesktopSettings& settings) {
    if(settings.title.empty()||settings.title.find('\0')!=std::string::npos||!settings.width||!settings.height||settings.width>16384||settings.height>16384)
        throw std::invalid_argument("Invalid desktop window settings");
    settings.quality.validate();
    if(settings.backend!=DesktopBackend::Dawn&&settings.backend!=DesktopBackend::Direct3D12&&settings.backend!=DesktopBackend::DawnVulkan&&settings.backend!=DesktopBackend::Automatic&&settings.backend!=DesktopBackend::DawnD3D11)throw std::invalid_argument("Unknown desktop graphics backend");
}
}
struct DesktopHost::Impl {
    SdlRuntime runtime;
    Window window{nullptr,SDL_DestroyWindow};
    std::unique_ptr<DesktopGraphics> graphics;
    DesktopBackend backend;
    Application app;
    GuiSystem gui;
    std::unique_ptr<SdlTextInput> text;
    std::unique_ptr<WindowsLifecycle> lifecycle;
    std::unique_ptr<WindowsAccessibility> accessibility;
    std::unique_ptr<SdlPointerInput> contacts;
    InputBindings bindings;
    std::map<std::uint64_t,std::pair<SceneId,std::vector<DesktopView>>> sceneViews;
    std::function<bool(const DesktopEvent&)> handler;
    Gamepad pad{nullptr,SDL_CloseGamepad};
    RenderQuality quality;
    std::thread::id owner=std::this_thread::get_id();
    bool closing=false,focused=true,minimized=false,enabled=true,shadows=true,padArmed=false;
    bool stepping=false,advancing=false,cancelling=false,pointerOwned=false;
    bool allowGraphicsFaultInjection=false,graphicsBlocked=false;
    std::uint64_t textFocus=0;
    ApplicationState lastState=ApplicationState::Created;

    explicit Impl(const DesktopSettings& settings):backend(settings.backend),app(settings.application),
        bindings([this](std::span<const InputPacket> packets){return app.submitInput(packets);}),quality(settings.quality) {
        allowGraphicsFaultInjection=settings.allowGraphicsFaultInjection;
        window.reset(SDL_CreateWindow(settings.title.c_str(),int(settings.width),int(settings.height),
            SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|(settings.hidden?SDL_WINDOW_HIDDEN:0)));
        check(bool(window));graphics=makeDesktopGraphics(backend,*window,gui,quality);text=std::make_unique<SdlTextInput>(*window,gui);
        lifecycle=std::make_unique<WindowsLifecycle>(static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window.get()),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr)));
        accessibility=std::make_unique<WindowsAccessibility>(static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window.get()),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr)));
        contacts=std::make_unique<SdlPointerInput>(gui,[this](std::span<const InputPacket> packets){return app.submitInput(packets);});
        app.setSceneBinding([this](Scene* scene){bindScene(scene);});openPad();
    }
    ~Impl() {
        accessibility.reset(); // Disconnect COM snapshots before releasing GUI/window.
        lifecycle.reset(); // Unhook before window destruction and user-owned state release.
        handler={}; // Never call application captures during teardown.
        bindings.cancel(); // GUI destruction must not invoke application widget hooks.
        if(graphics)graphics->bind(nullptr,{});app.shutdown();graphics.reset();pad.reset();
    }
    void requireOwner() const { if(owner!=std::this_thread::get_id())throw std::logic_error("DesktopHost requires its owner thread"); }
    bool emit(const DesktopEvent& event) { if(!handler)return false;auto callback=handler;return callback(event); }
    bool gameplay() const { return enabled&&focused&&!minimized&&!gui.focusedInput()&&!app.suspended()&&app.state()==ApplicationState::Running; }
    void syncText() {
        text->sync(focused&&!minimized);
        const auto* input=gui.focusedInput();const auto id=input?input->id():0;
        if(id!=textFocus){textFocus=id;cancelBindings();}
    }
    void layout() { const auto size=desktopLogicalSize(*window);gui.layout({float(size.width),float(size.height)});syncText();
        accessibility->publish(gui,desktopCoordinateScale(*window),focused&&!minimized&&!app.suspended()&&app.state()!=ApplicationState::Stopped); }
    void cancelBindings() {
        if(contacts)contacts->cancel();
        if(accessibility)accessibility->cancel();
        bindings.cancel();padArmed=false;
        InputPacket packet;packet.type=int(InputEventType::CancelAll);app.submitInput({&packet,1});
        if(!cancelling) { cancelling=true;try { emit({DesktopEventType::InputCancelled}); }catch(...) { cancelling=false;throw; }cancelling=false; }
    }
    void cancel() { text->cancel();gui.pointer(GuiPointer::Cancel,{});gui.blur();textFocus=0;pointerOwned=false;cancelBindings(); }
    std::vector<RenderView> resolve(Scene& scene,const std::vector<DesktopView>& source,bool strict) const {
        std::vector<RenderView> result;
        for(const auto& view:source) {
            auto* camera=dynamic_cast<CameraComponent*>(scene.findComponent(view.camera));
            if(!camera) { if(strict)throw std::invalid_argument("Desktop view has a stale or foreign camera");continue; }
            result.push_back({camera,view.clearColor,view.skyEnabled,view.fogEnabled});
        }
        RenderViews checked;checked.set(result);return result;
    }
    const std::vector<DesktopView>& viewsFor(Scene& scene) const {
        for(const auto& [key,value]:sceneViews) { (void)key;if(app.findScene(value.first)==&scene)return value.second; }
        static const std::vector<DesktopView> empty;return empty;
    }
    void bindScene(Scene* scene) {
        if(!scene){graphics->bind(nullptr,{});return;}
        const auto views=resolve(*scene,viewsFor(*scene),true);cancelBindings();graphics->bind(scene,views);
    }
    void openPad() {
        if(pad)return;int count=0;auto* devices=SDL_GetGamepads(&count);
        if(count)pad.reset(SDL_OpenGamepad(devices[0]));SDL_free(devices);padArmed=false;
    }
    void control(DesktopControl value,bool down,bool repeat,bool guiConsumed) {
        DesktopEvent event;event.control=value;event.down=down;event.repeat=repeat;event.guiConsumed=guiConsumed;
        const bool consumed=emit(event)||guiConsumed;
        if(guiConsumed&&down)cancelBindings();
        // Always release mapped values, even when UI/context ownership changed.
        if(!down&&value!=DesktopControl::Unknown)bindings.control(controlId(value),0);
        else if(!repeat&&!consumed&&gameplay()&&value!=DesktopControl::Unknown)bindings.control(controlId(value),1);
    }
    void pointer(const SDL_Event& event) {
        const auto which=event.type==SDL_EVENT_MOUSE_MOTION?event.motion.which:event.button.which;
        if(which==SDL_TOUCH_MOUSEID||which==SDL_PEN_MOUSEID)return;
        layout();
        const bool move=event.type==SDL_EVENT_MOUSE_MOTION;
        const float units=desktopCoordinateScale(*window);
        const Vec2 position=move?Vec2{event.motion.x/units,event.motion.y/units}:Vec2{event.button.x/units,event.button.y/units};
        const auto action=move?GuiPointer::Move:event.button.down?GuiPointer::Down:GuiPointer::Up;
        const bool primary=move||event.button.button==SDL_BUTTON_LEFT;
        const bool owned=pointerOwned;
        if(primary&&action==GuiPointer::Down&&gui.focusedInput())text->cancel();
        const bool contactOwner=contacts->ownsGui();
        const bool consumed=primary&&(contactOwner||gui.pointer(action,position,(SDL_GetModState()&SDL_KMOD_SHIFT)!=0));
        if(primary&&action==GuiPointer::Down&&consumed&&!contactOwner) { pointerOwned=true;cancelBindings(); }
        if(primary&&action==GuiPointer::Up)pointerOwned=false;
        if(!move&&(!contactOwner||!event.button.down)) {
            const auto button=event.button.button==SDL_BUTTON_LEFT?DesktopControl::MouseLeft:event.button.button==SDL_BUTTON_RIGHT?DesktopControl::MouseRight:DesktopControl::MouseMiddle;
            control(button,event.button.down,false,consumed||owned);
        }
        if(gameplay()&&!consumed&&!owned) {
            const auto logical=desktopLogicalSize(*window);int width=0,height=0;const auto d=graphics->diagnostics();width=int(d.width);height=int(d.height);
            InputPacket packet;packet.type=int(move?InputEventType::PointerMove:event.button.down?InputEventType::PointerDown:InputEventType::PointerUp);
            packet.id=1;packet.x=position.x;packet.y=position.y;packet.pixelX=position.x*width/std::max(1.f,logical.width);packet.pixelY=position.y*height/std::max(1.f,logical.height);
            const auto buttons=move?event.motion.state:SDL_GetMouseState(nullptr,nullptr);
            packet.buttons=((buttons&SDL_BUTTON_LMASK)?1u:0u)|((buttons&SDL_BUTTON_RMASK)?2u:0u)|((buttons&SDL_BUTTON_MMASK)?4u:0u)|
                ((buttons&SDL_BUTTON_X1MASK)?8u:0u)|((buttons&SDL_BUTTON_X2MASK)?16u:0u);packet.timestamp=double(SDL_GetTicksNS())/1e9;
            app.submitInput({&packet,1});
        }
    }
    void poll() {
        syncText();
        if(focused&&!minimized&&!app.suspended()) {
            if(accessibility->dispatch(gui)){cancelBindings();syncText();}
        } else accessibility->cancel();
        SDL_Event event;
        const auto id=SDL_GetWindowID(window.get());
        while(SDL_PollEvent(&event)) {
            if(event.type>=SDL_EVENT_WINDOW_FIRST&&event.type<=SDL_EVENT_WINDOW_LAST&&event.window.windowID!=id)continue;
            if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&event.key.windowID!=id)continue;
            if(event.type==SDL_EVENT_QUIT||event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if(!emit({DesktopEventType::CloseRequested}))closing=true;
            } else if(event.type==SDL_EVENT_GAMEPAD_ADDED)openPad();
            else if(event.type==SDL_EVENT_GAMEPAD_REMOVED) {
                if(pad&&event.gdevice.which==SDL_GetGamepadID(pad.get())) {cancel();pad.reset();openPad();}
            } else if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST||event.type==SDL_EVENT_WINDOW_MINIMIZED) {
                if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST)focused=false;else minimized=true;
                cancel();if(app.state()!=ApplicationState::Stopped)app.setSuspended(true);
                DesktopEvent out;out.type=DesktopEventType::FocusChanged;out.focused=false;emit(out);
            } else if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED||event.type==SDL_EVENT_WINDOW_RESTORED) {
                if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)focused=true;else minimized=false;
                if(app.state()!=ApplicationState::Stopped)app.setSuspended(!focused||minimized||lifecycle->suspended());
                DesktopEvent out;out.type=DesktopEventType::FocusChanged;out.focused=focused&&!minimized;emit(out);
            } else if(event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED||event.type==SDL_EVENT_WINDOW_RESIZED||event.type==SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                layout();emit({DesktopEventType::Resized});
            } else if(event.type==SDL_EVENT_DROP_FILE&&event.drop.windowID==id) {
                DesktopEvent out;out.type=DesktopEventType::FileDrop;out.path=utf8Path(event.drop.data);emit(out);
            } else if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&focused&&!minimized) {
                layout();bool consumed=text->key(event.key);
                if(!consumed&&event.key.down)if(auto key=guiKey(event.key.scancode))consumed=gui.key(*key,(event.key.mod&SDL_KMOD_SHIFT)!=0,event.key.repeat);
                control(keyControl(event.key.scancode),event.key.down,event.key.repeat,consumed);
            } else if((event.type==SDL_EVENT_TEXT_INPUT||event.type==SDL_EVENT_TEXT_EDITING)&&focused&&!minimized) {
                text->event(event);
            } else if(((event.type>=SDL_EVENT_FINGER_DOWN&&event.type<=SDL_EVENT_FINGER_CANCELED)||
                (event.type>=SDL_EVENT_PEN_PROXIMITY_IN&&event.type<=SDL_EVENT_PEN_AXIS))&&focused&&!minimized) {
                layout();const auto logical=desktopLogicalSize(*window);const auto d=graphics->diagnostics();
                contacts->event(event,*window,{float(logical.width),float(logical.height)},{float(d.width),float(d.height)},gameplay(),pointerOwned);
            } else if((event.type==SDL_EVENT_MOUSE_MOTION||event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_BUTTON_UP)&&focused&&!minimized) {
                if((event.type==SDL_EVENT_MOUSE_MOTION?event.motion.windowID:event.button.windowID)==id)pointer(event);
            } else if(event.type==SDL_EVENT_MOUSE_WHEEL&&event.wheel.windowID==id&&focused&&!minimized) {
                layout();const float units=desktopCoordinateScale(*window);const Vec2 position{event.wheel.mouse_x/units,event.wheel.mouse_y/units};
                const float sign=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1.f:1.f;
                const Vec2 delta{event.wheel.x*sign*32,-event.wheel.y*sign*32};
                if(!gui.wheel(position,delta)&&gameplay()) {bindings.control(controlId(DesktopControl::WheelX),event.wheel.x*sign);bindings.control(controlId(DesktopControl::WheelY),event.wheel.y*sign);}
            } else if((event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||event.type==SDL_EVENT_GAMEPAD_BUTTON_UP)&&pad&&event.gbutton.which==SDL_GetGamepadID(pad.get())&&focused&&!minimized) {
                // Modal navigation remains available with a held driving axis. Only
                // gameplay buttons wait for neutral; Start can always open the menu.
                if(!padArmed&&gameplay()&&event.gbutton.button!=SDL_GAMEPAD_BUTTON_START)continue;
                const bool down=event.gbutton.down;layout();bool consumed=false;DesktopControl button=DesktopControl::Unknown;
                switch(event.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_SOUTH:button=DesktopControl::PadSouth;if(down)consumed=gui.key(GuiKey::Enter);break;
                case SDL_GAMEPAD_BUTTON_EAST:button=DesktopControl::PadEast;if(down)consumed=gui.key(GuiKey::Escape);break;
                case SDL_GAMEPAD_BUTTON_START:button=DesktopControl::PadStart;break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:if(down)consumed=gui.key(GuiKey::Tab);break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:if(down)consumed=gui.key(GuiKey::Tab,true);break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:if(down)consumed=gui.key(GuiKey::Left);break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:if(down)consumed=gui.key(GuiKey::Right);break;
                }
                control(button,down,false,consumed);
            }
            syncText();
        }
        if(lifecycle->consumeChange()&&app.state()!=ApplicationState::Stopped) {
            cancel();app.setSuspended(true); // Also resets timing for a coalesced suspend/resume.
            app.resetClocks();app.setSuspended(!focused||minimized||lifecycle->suspended());
        }
        if(pad&&focused&&!minimized&&!lifecycle->suspended()) {
            auto axis=[&](SDL_GamepadAxis axis) {const float raw=std::clamp(float(SDL_GetGamepadAxis(pad.get(),axis))/32767.f,-1.f,1.f);return std::abs(raw)<.15f?0:std::copysign((std::abs(raw)-.15f)/.85f,raw);};
            const float x=axis(SDL_GAMEPAD_AXIS_LEFTX),y=-axis(SDL_GAMEPAD_AXIS_LEFTY);
            auto trigger=[&](SDL_GamepadAxis axis){const float raw=std::max(0.f,float(SDL_GetGamepadAxis(pad.get(),axis))/32767.f);return raw<.05f?0.f:(raw-.05f)/.95f;};
            const float forward=trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),reverse=trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            if(x==0&&y==0&&forward<.05f&&reverse<.05f&&!SDL_GetGamepadButton(pad.get(),SDL_GAMEPAD_BUTTON_SOUTH)&&!SDL_GetGamepadButton(pad.get(),SDL_GAMEPAD_BUTTON_EAST))padArmed=true;
            if(padArmed&&gameplay()) {
                bindings.control(controlId(DesktopControl::PadLeftX),x);bindings.control(controlId(DesktopControl::PadLeftY),y);
                bindings.control(controlId(DesktopControl::PadRightTrigger),forward);bindings.control(controlId(DesktopControl::PadLeftTrigger),reverse);
            }
        }
    }
};
DesktopHost::DesktopHost(const DesktopSettings& settings) { validate(settings);impl_=std::make_unique<Impl>(settings); }
DesktopHost::~DesktopHost()=default;
Application& DesktopHost::application() {return impl_->app;}const Application& DesktopHost::application() const{return impl_->app;}
GuiSystem& DesktopHost::gui(){return impl_->gui;}InputBindings& DesktopHost::bindings(){return impl_->bindings;}
void DesktopHost::setViews(SceneId id,std::span<const DesktopView> source) {
    auto& p=*impl_;p.requireOwner();if(p.advancing)throw std::logic_error("Configure desktop views at idle boundaries");
    auto* scene=p.app.findScene(id);if(!scene)throw std::invalid_argument("Unknown desktop scene");
    const std::vector<DesktopView> candidate(source.begin(),source.end());const auto views=p.resolve(*scene,candidate,true);
    auto replacement=p.sceneViews;replacement.insert_or_assign(id.value,std::make_pair(id,candidate));
    if(scene==p.app.activeScene())p.graphics->setViews(views);
    p.sceneViews.swap(replacement);
}
bool DesktopHost::step(const ApplicationFrameCallbacks& callbacks,std::optional<double> timestamp) {
    auto& p=*impl_;p.requireOwner();if(p.stepping)throw std::logic_error("DesktopHost step is not reentrant");
    struct Guard { bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;} } guard(p.stepping);
    if(p.app.state()==ApplicationState::Stopped)return false;
    if(p.closing){p.cancel();p.app.shutdown();return false;}
    if(p.app.state()!=p.lastState) {p.cancelBindings();p.lastState=p.app.state();}
    std::erase_if(p.sceneViews,[&](const auto& entry){return !p.app.findScene(entry.second.first);});
    p.poll();
    if(p.app.state()==ApplicationState::Stopped)return false;
    if(p.closing){p.cancel();p.app.shutdown();return false;}
    const auto previousBackend=p.graphics->backend();p.graphics->service();
    if(previousBackend!=p.graphics->backend()){p.graphicsBlocked=true;p.cancelBindings();if(p.closing){p.app.shutdown();return false;}}
    const auto graphicsState=p.graphics->diagnostics();
    if(!graphicsState.error.empty())throw std::runtime_error(graphicsState.error);
    if(p.app.activeScene()&&!graphicsState.ready) {if(!p.graphicsBlocked)p.cancel();p.graphicsBlocked=true;if(p.closing){p.app.shutdown();return false;}SDL_Delay(1);return true;}
    if(p.graphicsBlocked){p.app.resetClocks();p.graphicsBlocked=false;}
    auto frameCallbacks=callbacks;
    frameCallbacks.present=[&p,present=callbacks.present](Scene& scene,const ApplicationFrame& frame) {
        if(present)present(scene,frame);
        // Resolve handles again after user callbacks/ticks may have destroyed cameras.
        p.graphics->setViews(p.resolve(scene,p.viewsFor(scene),false));p.layout();p.graphics->setWaterTime(frame.simulation.seconds);p.graphics->render(frame.presentation.seconds,frame.interval*1000);
    };
    {Guard applicationGuard(p.advancing);p.app.advance(timestamp.value_or(nowSeconds()),frameCallbacks);}
    if(p.app.suspended())SDL_Delay(5);
    if(p.closing&&p.app.state()!=ApplicationState::Stopped){p.cancel();p.app.shutdown();}
    return !p.closing&&p.app.state()!=ApplicationState::Stopped;
}
int DesktopHost::run(const ApplicationFrameCallbacks& callbacks){while(step(callbacks)){}return 0;}
void DesktopHost::setEventHandler(std::function<bool(const DesktopEvent&)> handler){impl_->requireOwner();impl_->handler=std::move(handler);}
void DesktopHost::cancelInput(){impl_->requireOwner();impl_->cancel();}
void DesktopHost::setPaused(bool paused){impl_->requireOwner();impl_->cancel();if(paused)impl_->app.pause();else impl_->app.resume();impl_->lastState=impl_->app.state();}
void DesktopHost::setInputEnabled(bool enabled){impl_->requireOwner();if(enabled!=impl_->enabled){impl_->cancel();impl_->enabled=enabled;}}
bool DesktopHost::inputEnabled() const{return impl_->enabled;}
void DesktopHost::requestClose(){impl_->requireOwner();impl_->closing=true;}
bool DesktopHost::closeRequested() const{return impl_->closing;}
void DesktopHost::setFullscreen(bool enabled){impl_->requireOwner();impl_->cancel();check(SDL_SetWindowFullscreen(impl_->window.get(),enabled));}
DesktopBackend DesktopHost::backend() const{return impl_->graphics->backend();}
bool DesktopHost::fullscreen() const{return (SDL_GetWindowFlags(impl_->window.get())&SDL_WINDOW_FULLSCREEN)!=0;}
void DesktopHost::setWindowSize(std::uint32_t w,std::uint32_t h){impl_->requireOwner();if(!w||!h||w>16384||h>16384)throw std::invalid_argument("Invalid desktop window size");check(SDL_SetWindowSize(impl_->window.get(),int(w),int(h)));}
DesktopSize DesktopHost::size() const{const auto s=desktopLogicalSize(*impl_->window);return {float(s.width),float(s.height),float(s.pixelRatio)};}
double DesktopHost::nowSeconds() const{return double(SDL_GetTicksNS())/1e9;}
void DesktopHost::setQuality(const RenderQuality& quality){impl_->requireOwner();quality.validate();impl_->graphics->setQuality(quality);impl_->quality=quality;}
const RenderQuality& DesktopHost::quality() const{return impl_->quality;}
const RenderCapabilities& DesktopHost::capabilities() const{return impl_->graphics->capabilities();}
void DesktopHost::setShadowsEnabled(bool enabled){impl_->requireOwner();impl_->graphics->setShadows(enabled);impl_->shadows=enabled;}
bool DesktopHost::shadowsEnabled() const{return impl_->shadows;}
void DesktopHost::setWaterInteractions(std::span<const WaterInteraction> values){impl_->requireOwner();WaterInteractions candidate;candidate.set(values);impl_->graphics->setWaterInteractions(candidate);}
DesktopDiagnostics DesktopHost::diagnostics() const { auto d=impl_->graphics->diagnostics();d.requestedBackend=impl_->backend;d.backend=impl_->graphics->backend();return d; }
std::string DesktopHost::gpuProfileTrace() const {impl_->requireOwner();if(impl_->stepping)throw std::logic_error("GPU trace export requires an idle host");return impl_->graphics->gpuProfileTrace();}
void DesktopHost::retryGraphics(){impl_->requireOwner();const bool unavailable=!impl_->graphics->diagnostics().ready;impl_->graphics->retry();if(unavailable)impl_->graphicsBlocked=true;}
bool DesktopHost::simulateGraphicsLoss(){
    auto& p=*impl_;p.requireOwner();
    if(!p.allowGraphicsFaultInjection||p.stepping)throw std::logic_error("Graphics fault injection requires explicit opt-in and an idle host");
    if(!p.graphics->diagnostics().ready)return false;
    p.cancel();if(p.closing)return false;return p.graphics->simulateLoss();
}
bool DesktopHost::simulateGraphicsFailure(){
    auto& p=*impl_;p.requireOwner();
    if(!p.allowGraphicsFaultInjection||p.stepping)throw std::logic_error("Graphics fault injection requires explicit opt-in and an idle host");
    p.cancel();if(p.closing)return false;return p.graphics->simulateFailure();
}
void DesktopHost::captureFrame(const std::filesystem::path& path){impl_->requireOwner();if(path.empty())throw std::invalid_argument("Empty capture path");impl_->graphics->capture(path);}
bool DesktopHost::capturePending() const{return impl_->graphics->capturePending();}
std::string DesktopHost::captureError() const{return impl_->graphics->captureError();}
std::filesystem::path DesktopHost::baseDirectory() const{return utf8Path(SDL_GetBasePath());}
DesktopFileDialogResult DesktopHost::showFileDialog(const DesktopFileDialogOptions& options) {
    auto& p=*impl_;p.requireOwner();if(p.advancing||p.app.state()==ApplicationState::Stopped)throw std::logic_error("File dialogs require a live idle Application boundary");
    const bool suspended=p.app.suspended();bool entered=false;
    try {
        auto result=showWindowsFileDialog(SDL_GetPointerProperty(SDL_GetWindowProperties(p.window.get()),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr),options,[&]{p.cancel();p.app.setSuspended(true);entered=true;});
        p.app.setSuspended(suspended);p.cancel();return result;
    }catch(...){if(entered)p.app.setSuspended(suspended);throw;}
}
}
