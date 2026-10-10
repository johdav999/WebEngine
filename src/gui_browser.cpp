#include "gui_browser.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <sstream>
#include <iomanip>

EM_JS(void, gui_sync, (int kind,const char* value,int start,int end,int backward,int revision,int field,float x,float y,float width,float height,int focus), {
    window.EngineGui?.sync(kind,value ? UTF8ToString(value) : "",start,end,!!backward,revision,field,{x,y,width,height},!!focus);
});
EM_JS(void, gui_boot, (int loadFont), { window.EngineGui?.initialize(!!loadFont); });
EM_JS(void, gui_error, (const char* error), { window.EngineGui?.fail(UTF8ToString(error)); });
EM_JS(void, gui_accessibility, (const char* json), { window.EngineGui?.syncAccessibility(JSON.parse(UTF8ToString(json))); });
EM_JS(void, gui_accessibility_focus, (int request), { window.EngineGui?.focusAccessible(!!request); });

namespace engine {
namespace {
GuiSystem* activeGui=nullptr;
std::string accessibilityJson;
void syncAccessibility();
void layout() {
    if(!activeGui) return;
    double width=0,height=0;emscripten_get_element_css_size("#viewport",&width,&height);
    activeGui->layout({float(width),float(height)});
}
std::string quote(std::string_view text) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:text) {
        if(c=='"'||c=='\\') out<<'\\'<<char(c);
        else if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;
        else out<<char(c);
    }
    out<<'"';return out.str();
}
}
GuiSystem* browserGui() { return activeGui; }
void attachBrowserGui(GuiSystem* gui) { activeGui=gui;accessibilityJson.clear();if(gui) gui_boot(!gui->font());else { gui_sync(0,nullptr,0,0,0,0,0,0,0,0,0,0);gui_accessibility("[]"); } }
void syncBrowserGui(bool requestFocus) {
    if(!activeGui) return;
    layout();const auto* input=activeGui->focusedInput();
    if(input) {
        const auto& r=input->bounds();gui_sync(2,input->text().c_str(),input->selectionStart(),input->selectionEnd(),input->selectionBackward(),input->textRevision(),reinterpret_cast<std::uintptr_t>(input),r.x,r.y,r.width,r.height,requestFocus);
    } else gui_sync(activeGui->focused() ? 1 : 0,nullptr,0,0,0,0,0,0,0,0,0,requestFocus);
    syncAccessibility();
    gui_accessibility_focus(requestFocus);
}
namespace {
void syncAccessibility() {
    std::ostringstream out;out<<'[';bool first=true;
    for(const auto& n:activeGui->accessibilityNodes()) {
        if(!first) out<<',';first=false;
        out<<"{\"id\":"<<n.id<<",\"role\":"<<int(n.role)<<",\"name\":"<<quote(n.name)<<",\"description\":"<<quote(n.description)
           <<",\"value\":"<<quote(n.value)<<",\"disabled\":"<<(n.disabled ? "true" : "false")<<",\"focused\":"<<(n.focused ? "true" : "false")<<",\"live\":"<<(n.live ? "true" : "false")<<'}';
    }
    out<<']';auto candidate=out.str();if(candidate!=accessibilityJson) { accessibilityJson=std::move(candidate);gui_accessibility(accessibilityJson.c_str()); }
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_gui_pointer(int action,float x,float y,int extend) {
    if(!activeGui||action<0||action>3) return 0;
    layout();const bool consumed=activeGui->pointer(static_cast<GuiPointer>(action),{x,y},extend!=0);
    syncBrowserGui(action==1);return consumed;
}
EMSCRIPTEN_KEEPALIVE int engine_gui_wheel(float x,float y,float dx,float dy) { layout();return activeGui&&activeGui->wheel({x,y},{dx,dy}); }
EMSCRIPTEN_KEEPALIVE int engine_gui_focus(double id,int activate) {
    if(!activeGui||!std::isfinite(id)||id<1||id>9007199254740991.0||std::floor(id)!=id) return 0;
    layout();const bool result=activate ? activeGui->activateWidget(static_cast<std::uint64_t>(id)) : activeGui->focusWidget(static_cast<std::uint64_t>(id));
    syncBrowserGui();return result;
}
EMSCRIPTEN_KEEPALIVE int engine_gui_scale(float value) {
    if(!activeGui) return 0;
    try { activeGui->setScale(value);syncBrowserGui();return 1; }catch(const std::invalid_argument&) { return 0; }
}
EMSCRIPTEN_KEEPALIVE int engine_gui_key(int key,int shift,int repeat) {
    if(!activeGui||key<0||key>9) return 0;
    layout();const bool consumed=activeGui->key(static_cast<GuiKey>(key),shift!=0,repeat!=0);
    syncBrowserGui(consumed);return consumed;
}
EMSCRIPTEN_KEEPALIVE int engine_gui_edit(const char* value,int bytes,int start,int end,int composing,int backward) {
    if(!activeGui||!value||bytes<0||bytes>32768||start<0||end<0) return -1;
    try {
        activeGui->editText(std::string(value,bytes),start,end,composing!=0,backward!=0);
        const auto* input=activeGui->focusedInput();return input ? static_cast<int>(input->textRevision()) : -1;
    }
    catch(const std::exception& error) { gui_error(error.what());return -1; }
}
EMSCRIPTEN_KEEPALIVE void engine_gui_blur() {
    if(activeGui) { activeGui->pointer(GuiPointer::Cancel,{});activeGui->blur(); }
    syncBrowserGui();
}
EMSCRIPTEN_KEEPALIVE int engine_gui_font(const void* data,int bytes) {
    if(!activeGui||!data||bytes<12||bytes>4*1024*1024) return 0;
    try { activeGui->setFont(GuiFont::loadTrustedTrueType({static_cast<const std::byte*>(data),std::size_t(bytes)},22));return 1; }
    catch(const std::exception& error) { gui_error(error.what());return 0; }
}
// Read-only diagnostics for user acceptance and application tooling. Actual
// widget interaction still goes through pointer/keyboard and the canvas.
EMSCRIPTEN_KEEPALIVE const char* engine_gui_snapshot() {
    static std::string json;std::ostringstream out;
    out<<"{\"enabled\":"<<(activeGui ? "true" : "false")<<",\"fontReady\":"<<(activeGui&&activeGui->font() ? "true" : "false")<<",\"widgets\":[";
    bool first=true;
    if(activeGui) {
        layout();
        const auto visit=[&](auto&& self,const Widget& widget)->void {
            if(!first) out<<',';first=false;
            const char* type="root";std::string_view text;
            if(const auto* label=dynamic_cast<const TextLabel*>(&widget)) { type="label";text=label->text(); }
            if(const auto* button=dynamic_cast<const Button*>(&widget)) { type="button";text=button->text(); }
            if(const auto* input=dynamic_cast<const TextInput*>(&widget)) { type="input";text=input->text(); }
            if(dynamic_cast<const Panel*>(&widget)) type="panel";
            if(dynamic_cast<const Image*>(&widget)) type="image";
            const auto r=widget.bounds(),clip=widget.clip();
            out<<"{\"id\":"<<widget.id()<<",\"type\":"<<quote(type)<<",\"text\":"<<quote(text)<<",\"focused\":"<<(&widget==activeGui->focused() ? "true" : "false")
               <<",\"active\":"<<(widget.active() ? "true" : "false")<<",\"bounds\":{\"x\":"<<r.x<<",\"y\":"<<r.y<<",\"width\":"<<r.width<<",\"height\":"<<r.height
               <<"},\"clip\":{\"x\":"<<clip.x<<",\"y\":"<<clip.y<<",\"width\":"<<clip.width<<",\"height\":"<<clip.height<<"}";
            if(const auto* input=dynamic_cast<const TextInput*>(&widget)) out<<",\"selectionStart\":"<<input->selectionStart()<<",\"selectionEnd\":"<<input->selectionEnd()<<",\"selectionBackward\":"<<(input->selectionBackward() ? "true" : "false")<<",\"composing\":"<<(input->composing() ? "true" : "false");
            if(const auto* scroll=dynamic_cast<const ScrollPanel*>(&widget)) out<<",\"scrollOffset\":"<<scroll->scrollOffset()<<",\"maxScrollOffset\":"<<scroll->maxScrollOffset();
            if(const auto* label=dynamic_cast<const TextLabel*>(&widget)) out<<",\"lineCount\":"<<label->lineCount();
            out<<'}';for(const auto& child:widget.children()) self(self,*child);
        };visit(visit,activeGui->root());
    }
    out<<"]}";json=out.str();return json.c_str();
}
}
}
