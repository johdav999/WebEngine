#include "sdl_text_input.hpp"
#include "sdl_display.hpp"
#include "gui_utf8.hpp"
#include <algorithm>
#include <memory>

namespace engine {
namespace {
constexpr std::size_t maxBytes=32768;
std::size_t previous(std::string_view text,std::size_t at) {
    if(at) {--at;while(at&&gui_detail::continuation(static_cast<unsigned char>(text[at])))--at;}return at;
}
std::size_t next(std::string_view text,std::size_t at) {if(at<text.size())gui_detail::next(text,at);return at;}
bool space(std::string_view text,std::size_t at) {return at<text.size()&&(text[at]==' '||text[at]=='\t'||text[at]=='\n'||text[at]=='\r');}
std::size_t word(std::string_view text,std::size_t at,bool right) {
    if(right) {while(at<text.size()&&!space(text,at))at=next(text,at);while(space(text,at))at=next(text,at);}
    else {while(at&&space(text,previous(text,at)))at=previous(text,at);while(at&&!space(text,previous(text,at)))at=previous(text,at);}
    return at;
}
std::size_t bytesAt(std::string_view text,int characters) {
    std::size_t at=0;for(int i=0;i<characters&&at<text.size();++i)gui_detail::next(text,at);return at;
}
bool readText(const char* source,std::string& result) {
    if(!source)return false;
    const auto length=SDL_strnlen(source,maxBytes+1);if(length>maxBytes)return false;
    result.assign(source,length);
    try{gui_detail::validate(result);}catch(const std::invalid_argument&){return false;}
    std::string singleLine;singleLine.reserve(result.size());
    for(std::size_t i=0;i<result.size();++i) {
        const char c=result[i];singleLine+=c=='\r'||c=='\n'||c=='\t'?' ':c;
        if(c=='\r'&&i+1<result.size()&&result[i+1]=='\n')++i;
    }
    result=std::move(singleLine);
    return true;
}
TextInput* findInput(Widget& root,std::uint64_t id) {
    if(root.id()==id)return dynamic_cast<TextInput*>(&root);
    for(const auto& child:root.children())if(auto* found=findInput(*child,id))return found;
    return nullptr;
}
}
SdlTextInput::~SdlTextInput(){if(focus_)SDL_StopTextInput(&window_);}
void SdlTextInput::cancelComposition() {
    const auto id=std::exchange(composition_,0);if(!id)return;
    SDL_ClearComposition(&window_);
    // Application edits win over an older preedit. Resolve identity on each use.
    if(auto* input=findInput(gui_.root(),id);input&&input->textRevision()==revision_) {
        if(input==gui_.focusedInput())gui_.editText(committed_,start_,end_,false,backward_);
        else {input->setText(committed_);input->setSelection(start_,end_,backward_);}
    } else if(input&&input==gui_.focusedInput()) {
        gui_.editText(input->text(),input->selectionStart(),input->selectionEnd(),false,input->selectionBackward());
    }
    committed_.clear();
}
void SdlTextInput::cancel(){cancelComposition();if(focus_)SDL_StopTextInput(&window_);focus_=0;}
void SdlTextInput::sync(bool enabled) {
    auto* input=enabled?gui_.focusedInput():nullptr;
    const auto id=input?input->id():0;
    if(id!=focus_) {
        cancel();
        // Text queued for the previous focus must never reach a different widget.
        SDL_FlushEvent(SDL_EVENT_TEXT_INPUT);SDL_FlushEvent(SDL_EVENT_TEXT_EDITING);
        input=enabled?gui_.focusedInput():nullptr;
        if(input&&SDL_StartTextInput(&window_))focus_=input->id();
    }
    input=gui_.focusedInput();
    if(!focus_||!input||input->id()!=focus_)return;
    if(composition_&&input->textRevision()!=revision_)cancelComposition();
    const auto bounds=input->bounds();const float scale=gui_.scale();
    const auto caret=input->selectionBackward()?input->selectionStart():input->selectionEnd();
    const float advance=gui_.font()?gui_.font()->measure(std::string_view(input->text()).substr(0,caret))*scale:0;
    const float units=desktopCoordinateScale(window_);
    const SDL_Rect area{int(bounds.x*units),int(bounds.y*units),std::max(1,int(bounds.width*units)),std::max(1,int(bounds.height*units))};
    SDL_SetTextInputArea(&window_,&area,int(std::clamp(advance+9*scale,0.f,std::max(0.f,bounds.width-9*scale))*units));
}
void SdlTextInput::replace(std::string value) {
    auto* input=gui_.focusedInput();if(!input||input->id()!=focus_)return;
    const auto start=input->selectionStart(),end=input->selectionEnd();
    if(input->text().size()-end+start+value.size()>maxBytes)return;
    const auto caret=start+value.size();
    value=input->text().substr(0,start)+value+input->text().substr(end);
    gui_.editText(std::move(value),caret,caret);
}
bool SdlTextInput::key(const SDL_KeyboardEvent& event) {
    auto* input=gui_.focusedInput();if(!input||input->id()!=focus_)return false;
    // Releases still belong to text focus, but the host releases mapped sources.
    if(!event.down)return true;
    const bool ctrl=(event.mod&SDL_KMOD_CTRL)!=0&&(event.mod&SDL_KMOD_ALT)==0,shift=(event.mod&SDL_KMOD_SHIFT)!=0;
    const auto code=event.scancode;
    if(code==SDL_SCANCODE_TAB){cancelComposition();return false;}
    if(code==SDL_SCANCODE_ESCAPE){if(composition_){cancelComposition();return true;}return false;}
    if(code==SDL_SCANCODE_RETURN||code==SDL_SCANCODE_KP_ENTER)return composition_!=0;
    if(composition_&&!ctrl)return true; // OS input method owns candidate navigation.
    if(ctrl)cancelComposition();
    input=gui_.focusedInput();if(!input||input->id()!=focus_)return true;
    const auto start=input->selectionStart(),end=input->selectionEnd();
    if(ctrl&&code==SDL_SCANCODE_A){gui_.editText(input->text(),0,input->text().size());return true;}
    if(ctrl&&(code==SDL_SCANCODE_C||code==SDL_SCANCODE_X)) {
        if(start!=end&&SDL_SetClipboardText(input->text().substr(start,end-start).c_str())&&code==SDL_SCANCODE_X)replace({});
        return true;
    }
    if(ctrl&&code==SDL_SCANCODE_V) {
        const std::unique_ptr<char,decltype(&SDL_free)> clipboard(SDL_GetClipboardText(),SDL_free);std::string value;
        if(readText(clipboard.get(),value))replace(std::move(value));return true;
    }
    const auto& text=input->text();
    auto caret=input->selectionBackward()?start:end;
    if(code==SDL_SCANCODE_BACKSPACE||code==SDL_SCANCODE_DELETE) {
        if(start==end) {
            const bool right=code==SDL_SCANCODE_DELETE;
            const auto target=ctrl?word(text,caret,right):right?next(text,caret):previous(text,caret);
            input->setSelection(std::min(caret,target),std::max(caret,target));
        }
        replace({});return true;
    }
    if(code==SDL_SCANCODE_LEFT||code==SDL_SCANCODE_RIGHT||code==SDL_SCANCODE_HOME||code==SDL_SCANCODE_END) {
        const auto anchor=input->selectionBackward()?end:start;
        if(code==SDL_SCANCODE_HOME)caret=0;
        else if(code==SDL_SCANCODE_END)caret=text.size();
        else {
            const bool right=code==SDL_SCANCODE_RIGHT;
            if(!shift&&start!=end)caret=right?end:start;
            else caret=ctrl?word(text,caret,right):right?next(text,caret):previous(text,caret);
        }
        gui_.editText(text,shift?std::min(anchor,caret):caret,shift?std::max(anchor,caret):caret,false,shift&&caret<anchor);
    }
    // Printable text comes from SDL_TEXT_INPUT, never keycode-to-character guesses.
    return true;
}
bool SdlTextInput::event(const SDL_Event& event) {
    if(event.type!=SDL_EVENT_TEXT_INPUT&&event.type!=SDL_EVENT_TEXT_EDITING)return false;
    auto* input=gui_.focusedInput();if(!focus_||!input||input->id()!=focus_)return true;
    const auto id=event.type==SDL_EVENT_TEXT_INPUT?event.text.windowID:event.edit.windowID;
    if(id!=SDL_GetWindowID(&window_))return true;
    std::string value;if(!readText(event.type==SDL_EVENT_TEXT_INPUT?event.text.text:event.edit.text,value))return true;
    if(event.type==SDL_EVENT_TEXT_INPUT) {
        cancelComposition();replace(std::move(value));return true;
    }
    if(value.empty()){cancelComposition();return true;}
    if(!composition_) {
        composition_=focus_;committed_=input->text();start_=input->selectionStart();end_=input->selectionEnd();backward_=input->selectionBackward();
    }
    if(committed_.size()-end_+start_+value.size()>maxBytes)return true;
    const auto selection=bytesAt(value,std::max(0,event.edit.start));
    const auto remaining=std::string_view(value).substr(selection);
    const auto length=bytesAt(remaining,std::max(0,event.edit.length));
    value=committed_.substr(0,start_)+value+committed_.substr(end_);
    gui_.editText(value,start_+selection,start_+selection+length,true);
    if(auto* current=gui_.focusedInput();current&&current->id()==composition_&&current->text()==value&&
       current->selectionStart()==start_+selection&&current->selectionEnd()==start_+selection+length)revision_=current->textRevision();
    else {revision_=0;cancelComposition();}
    return true;
}
}
