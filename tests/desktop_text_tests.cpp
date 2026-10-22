#include "desktop/sdl_text_input.hpp"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <iostream>
#include <memory>
using namespace engine;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct ClipboardRestore {
    std::unique_ptr<char,decltype(&SDL_free)> saved{SDL_GetClipboardText(),SDL_free};
    ~ClipboardRestore(){if(saved)SDL_SetClipboardText(saved.get());}
};
void run() {
    SDL_SetMainReady();require(SDL_InitSubSystem(SDL_INIT_VIDEO),"SDL video init failed");
    struct Runtime {~Runtime(){SDL_QuitSubSystem(SDL_INIT_VIDEO);}} runtime;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Text service contract",480,300,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    require(bool(window),"SDL window failed");ClipboardRestore clipboard;
    GuiSystem gui;auto& input=gui.root().add<TextInput>();input.setSize({400,38});
    auto& other=gui.root().add<TextInput>();other.setPosition({0,50});other.setSize({400,38});
    SdlTextInput service(*window,gui);gui.layout({480,300});require(gui.focusWidget(input.id()),"Focus failed");service.sync();
    require(SDL_TextInputActive(window.get()),"Focused textbox did not start OS text input");
    auto key=[&](SDL_Scancode code,SDL_Keymod mod=SDL_KMOD_NONE,bool repeat=false) {
        SDL_KeyboardEvent event{};event.down=true;event.scancode=code;event.mod=mod;event.repeat=repeat;
        const auto consumed=service.key(event);service.sync();return consumed;
    };
    auto text=[&](const char* value,bool composing=false,int start=0,int length=0) {
        SDL_Event event{};event.type=composing?SDL_EVENT_TEXT_EDITING:SDL_EVENT_TEXT_INPUT;
        if(composing){event.edit.windowID=SDL_GetWindowID(window.get());event.edit.text=value;event.edit.start=start;event.edit.length=length;}
        else {event.text.windowID=SDL_GetWindowID(window.get());event.text.text=value;}
        service.event(event);service.sync();
    };
    text("Johan \xc3\xa5\xe4\xb8\xad\xf0\x9f\x9a\x97");
    require(input.text()=="Johan \xc3\xa5\xe4\xb8\xad\xf0\x9f\x9a\x97","Unicode insertion failed");
    key(SDL_SCANCODE_LEFT,SDL_KMOD_SHIFT);require(input.selectionEnd()-input.selectionStart()==4&&input.selectionBackward(),"Shift selection split UTF-8");
    key(SDL_SCANCODE_BACKSPACE);require(input.text()=="Johan \xc3\xa5\xe4\xb8\xad","Selection deletion failed");
    key(SDL_SCANCODE_BACKSPACE);require(input.text()=="Johan \xc3\xa5","Codepoint backspace failed");
    key(SDL_SCANCODE_HOME);key(SDL_SCANCODE_RIGHT,SDL_KMOD_CTRL);require(input.selectionStart()==6,"Word navigation failed");
    key(SDL_SCANCODE_DELETE);require(input.text()=="Johan ","Forward Unicode delete failed");
    key(SDL_SCANCODE_A,SDL_KMOD_CTRL);key(SDL_SCANCODE_C,SDL_KMOD_CTRL);
    {std::unique_ptr<char,decltype(&SDL_free)> value(SDL_GetClipboardText(),SDL_free);require(value&&std::string(value.get())=="Johan ","Clipboard copy failed");}
    key(SDL_SCANCODE_X,SDL_KMOD_CTRL);require(input.text().empty(),"Clipboard cut failed");
    require(SDL_SetClipboardText("\xc3\x85sa\nDriver\t2"),"Clipboard write failed");key(SDL_SCANCODE_V,SDL_KMOD_CTRL);
    require(input.text()=="\xc3\x85sa Driver 2","Clipboard Unicode/single-line paste failed");
    key(SDL_SCANCODE_A,SDL_KMOD_CTRL);text("Driver");input.setSelection(0,6);
    text("\xe4\xb8\xad\xe6\x96\x87",true,1,1);
    require(input.composing()&&input.selectionStart()==3&&input.selectionEnd()==6,"IME character offsets were not UTF-8 bytes");
    text("\xe4\xb8\xad",true,1,0);require(input.text()=="\xe4\xb8\xad","Repeated preedit appended stale text");
    require(key(SDL_SCANCODE_RETURN)&&input.composing(),"IME Enter escaped to submit");
    require(key(SDL_SCANCODE_ESCAPE)&&!input.composing()&&input.text()=="Driver","IME Escape did not restore committed selection");
    text("\xe4\xb8\xad",true,1,0);text("\xe4\xb8\xad\xe6\x96\x87");
    require(!input.composing()&&input.text()=="\xe4\xb8\xad\xe6\x96\x87","IME commit duplicated preedit");
    const auto committed=input.text();input.setSelection(0,input.text().size());text("pending",true);
    SDL_Event stale{};stale.type=SDL_EVENT_TEXT_INPUT;stale.text.windowID=SDL_GetWindowID(window.get());stale.text.text="old focus";
    require(SDL_PushEvent(&stale),"Cannot queue stale text");
    require(gui.focusWidget(other.id()),"Second focus failed");service.sync();
    SDL_Event pending{};while(SDL_PollEvent(&pending))require(pending.type!=SDL_EVENT_TEXT_INPUT,"Queued text survived a focus handoff");
    require(input.text()==committed&&!input.composing()&&other.text().empty(),"Focus change committed preedit or edited another widget");
    other.setText("keep");other.setSelection(0,4);text("temporary",true);other.setText("Application wins");service.sync();
    require(other.text()=="Application wins","Preedit cancellation overwrote application edit");
    // Malformed and oversized input reject atomically, preserving text and selection.
    const auto before=other.text();const auto selection=other.selectionStart();text("\xc0\xaf");
    std::string huge(32769,'x');text(huge.c_str());require(other.text()==before&&other.selectionStart()==selection,"Invalid input changed textbox");
    text("temporary",true);other.setVisible(false);gui.layout({480,300});service.sync();
    require(other.text()==before&&!SDL_TextInputActive(window.get()),"Hidden textbox retained preedit/OS input");
    other.setVisible(true);gui.layout({480,300});gui.focusWidget(other.id());service.sync();
    service.cancel();gui.blur();text("stale");require(other.text()==before,"Text after cancellation changed widget");
    require(!SDL_TextInputActive(window.get()),"Cancellation retained OS text input");
    std::cout<<"Desktop Unicode, selection, clipboard, preedit, invalid input and focus tests passed\n";
}
}
int main(){try{run();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
