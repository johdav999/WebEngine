#include "engine/gui.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

using namespace engine;
namespace {
int checks=0;
void require(bool value,const char* message) { ++checks;if(!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& action) { bool failed=false;try { action(); }catch(const std::invalid_argument&) { failed=true; }require(failed,"Invalid GUI input must fail atomically"); }
std::shared_ptr<GuiFont> font() {
    std::ifstream file(GUI_FONT_PATH,std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
    require(!bytes.empty(),"Bundled trusted font must be readable");
    return GuiFont::loadTrustedTrueType({reinterpret_cast<const std::byte*>(bytes.data()),bytes.size()},22);
}
void layoutAndInput() {
    GuiSystem gui;gui.setFont(font());
    auto& panel=gui.root().add<Panel>();panel.setPosition({20,20});panel.setSize({220,180});panel.setPadding(10);panel.setLayout(VerticalLayout{5});
    auto& label=panel.add<TextLabel>("Name");label.setSize({200,20});
    auto& input=panel.add<TextInput>();input.setSize({200,30});
    auto& button=panel.add<Button>("Join");button.setSize({200,30});
    int clicks=0;button.onClick([&] { ++clicks; });
    gui.layout({800,600});
    require(label.bounds().x==30&&label.bounds().y==30,"Padding must offset children");
    require(input.bounds().y==55&&button.bounds().y==90,"Vertical layout must account for sizes and gap");
    require(!gui.pointer(GuiPointer::Down,{500,400}),"Clicks outside GUI belong to game");
    require(gui.pointer(GuiPointer::Down,{40,100})&&gui.capturesPointer(),"Buttons capture presses");
    require(gui.pointer(GuiPointer::Move,{600,400}),"Captured drags stay with GUI outside its bounds");
    gui.pointer(GuiPointer::Up,{600,400});require(clicks==0&&!gui.capturesPointer(),"Release outside must not activate");
    gui.pointer(GuiPointer::Down,{40,100});gui.pointer(GuiPointer::Up,{40,100});require(clicks==1,"Release over pressed button activates once");
    gui.pointer(GuiPointer::Down,{40,100});gui.pointer(GuiPointer::Cancel,{});gui.pointer(GuiPointer::Up,{40,100});require(clicks==1,"Cancelled press must not activate");
    require(gui.wheel({40,100})&&!gui.wheel({600,400}),"GUI blocks wheel only over its controls");
    gui.blur();gui.key(GuiKey::Tab);require(gui.focusedInput()==&input,"Tab starts with first focusable child");
    gui.key(GuiKey::Tab);require(gui.focused()==&button,"Tab advances focus");
    gui.key(GuiKey::Space,false,true);require(clicks==1,"Keyboard repeat must not duplicate activation");
    gui.key(GuiKey::Enter);require(clicks==2,"Enter activates a focused button");
    gui.key(GuiKey::Tab);require(gui.focusedInput()==&input,"Tab wraps within game GUI");
    gui.key(GuiKey::Tab,true);require(gui.focused()==&button,"Shift Tab reverses focus");
    button.setEnabled(false);gui.layout({800,600});require(gui.focused()==nullptr,"Disabled focus must be cleared");
    gui.pointer(GuiPointer::Down,{40,100});gui.pointer(GuiPointer::Up,{40,100});require(clicks==2,"Disabled button must not activate");
    label.setVisible(false);gui.layout({800,600});require(input.bounds().y==30,"Hidden children do not reserve flow space");
    gui.key(GuiKey::Tab);panel.setVisible(false);gui.layout({800,600});require(gui.focused()==nullptr&&!gui.wheel({40,40}),"Hidden subtree loses focus and hit testing");
    panel.setVisible(true);panel.setEnabled(false);gui.layout({800,600});require(!gui.key(GuiKey::Tab),"Disabled ancestor disables tab targets");
    rejects([&] { panel.setSize({-1,10}); });require(panel.bounds().width==220,"Invalid size preserves previous layout");
    rejects([&] { panel.setPadding(-1); });
    rejects([&] { gui.layout({NAN,10}); });
}
void anchorsClipsAndOrdering() {
    GuiSystem gui;auto& panel=gui.root().add<Panel>();panel.setPosition({10,10});panel.setSize({100,100});
    auto& button=panel.add<Button>("Overflow");button.setPosition({80,80});button.setSize({60,60});int clicks=0;button.onClick([&] { ++clicks; });
    gui.layout({400,300});require(button.clip().width==20&&button.clip().height==20,"Child clip intersects parent");
    require(!gui.pointer(GuiPointer::Down,{120,120}),"Clipped child must not capture invisible pixels");
    panel.setClipChildren(false);gui.layout({400,300});require(gui.pointer(GuiPointer::Down,{120,120}),"Unclipped child can receive input outside parent");gui.pointer(GuiPointer::Up,{120,120});require(clicks==1,"Unclipped child activates");
    auto& top=gui.root().add<Button>("Top");top.setPosition({90,90});top.setSize({60,60});int topClicks=0;top.onClick([&] { ++topClicks; });
    gui.layout({400,300});gui.pointer(GuiPointer::Down,{100,100});gui.pointer(GuiPointer::Up,{100,100});require(topClicks==1&&clicks==1,"Last drawn widget gets topmost hit");
    top.setAnchor(GuiAnchor::BottomRight);top.setPosition({10,15});gui.layout({400,300});require(top.bounds().x==330&&top.bounds().y==225,"Bottom-right anchor applies inward margins");
    top.setAnchor(GuiAnchor::Center);top.setPosition({0,0});gui.layout({600,400});require(top.bounds().x==270&&top.bounds().y==170,"Center anchor follows resized root");
    auto& row=gui.root().add<Panel>();row.setSize({200,50});row.setLayout(HorizontalLayout{7});row.setPadding(3);
    auto& a=row.add<Image>();a.setSize({30,30});auto& b=row.add<TextLabel>("B");b.setSize({40,30});
    gui.layout({600,400});require(a.bounds().x==3&&b.bounds().x==40,"Horizontal layout uses padding and spacing");
}
void textAndRendering() {
    GuiSystem gui;auto f=font();gui.setFont(f);
    require(f->measure("WWW")>f->measure("iii")*2,"Font metrics are proportional");
    require(f->measure("\xc3\x85\xc3\xa4\xc3\xb6")>0,"Unicode glyphs must measure");
    const auto old=f->atlas(f->glyph('A').page);
    f->glyph(0x3a9);const auto updated=f->atlas(f->glyph('A').page);
    require(old!=updated&&old->pixels!=updated->pixels,"Growing atlas creates immutable snapshots");
    auto& input=gui.root().add<TextInput>();input.setPosition({10,10});input.setSize({100,40});
    int changes=0,submits=0;input.onChange([&](const std::string&) { ++changes; });input.onSubmit([&] { ++submits; });
    input.setText("A\xc3\xa5\xf0\x9f\x98\x80Z");
    require(input.text().size()==8,"UTF-8 preserves multibyte values");
    input.setSelection(2,6);require(input.selectionStart()==1&&input.selectionEnd()==3,"Selection snaps to UTF-8 boundaries");
    input.setSelection(7,1,true);require(input.selectionStart()==1&&input.selectionEnd()==7&&input.selectionBackward(),"Backward selections retain both boundaries and direction");
    rejects([&] { input.setText(std::string("\xc0\x80",2)); });require(changes==1&&input.text().size()==8,"Malformed UTF-8 preserves prior text and callbacks");
    gui.layout({400,300});gui.pointer(GuiPointer::Down,{15,25});gui.pointer(GuiPointer::Up,{15,25});require(gui.focusedInput()==&input,"Pointer focuses text input");
    gui.editText("Hello\nworld",11,11,true);require(input.text()=="Hello world"&&input.composing(),"Browser edits retain composition and sanitize newlines");
    gui.key(GuiKey::Enter);require(submits==0,"Enter must not submit during composition");
    gui.editText("Hello world",11,11,false);gui.key(GuiKey::Enter);require(submits==1&&changes==2,"Composition commit does not duplicate value callback");
    input.setSelection(0,5);const auto selected=gui.draw(0.2);
    require(!selected.vertices.empty()&&!selected.commands.empty(),"Text and input create GPU draw data");
    for(const auto& command:selected.commands) {
        require(command.firstIndex+command.indexCount<=selected.indices.size(),"Draw command index range is valid");
        require(command.texture!=nullptr&&!command.clip.empty(),"Commands retain texture and clipping");
    }
    for(auto index:selected.indices) require(index<selected.vertices.size(),"Triangle indices reference valid vertices");
    const auto caretCount=gui.draw(0.2).vertices.size();const auto hiddenCaretCount=gui.draw(0.8).vertices.size();require(caretCount==hiddenCaretCount+4,"Caret blink affects one rectangle");
    gui.key(GuiKey::Escape);require(!gui.focusedInput(),"Escape clears text focus");
    GuiDrawList list;list.quad({0,0,10,10},{0,0,100,100},{1,1,1,1});list.quad({10,0,10,10},{0,0,100,100},{1,1,1,1});
    require(list.commands.size()==1&&list.commands[0].indexCount==12,"Adjacent compatible quads batch");
    list.quad({10,0,10,10},{0,0,50,50},{1,1,1,1});require(list.commands.size()==2,"Different clips split batches");
    list.quad({200,200,10,10},{0,0,50,50},{1,1,1,1});require(list.commands.size()==2,"Fully clipped rectangles produce no geometry");
}
void extensions() {
    GuiSystem gui;auto f=font();gui.setFont(f);
    auto& scroll=gui.root().add<ScrollPanel>();scroll.setPosition({10,10});scroll.setSize({200,100});scroll.setPadding(5);scroll.setLayout(VerticalLayout{4});scroll.setAccessibleName("Settings");
    auto& label=scroll.add<TextLabel>("Words wrap across lines\n\xc3\x85\xc3\xa4\xc3\xb6 andaverylongwordwithoutspaces");label.setStretch(true);label.setWrap(true);
    auto& first=scroll.add<Button>("First");first.setSize({100,30});
    auto& last=scroll.add<Button>("Last");last.setSize({100,30});last.setAccessibleDescription("Final action");
    int clicks=0;last.onClick([&] { ++clicks; });
    gui.layout({400,300});
    require(label.lineCount()>=3&&scroll.maxScrollOffset()>0,"Wrapping and newlines grow scroll content");
    require(label.bounds().width==190,"Stretch uses padded content width");
    const auto maximum=scroll.maxScrollOffset();
    gui.wheel({20,20},{0,10000});require(scroll.scrollOffset()==maximum,"Wheel clamps to content extent");
    require(gui.focusWidget(first.id()),"Focus can reveal scrolled controls");
    require(!first.clip().empty(),"Revealed focus is drawable");
    require(gui.activateWidget(last.id())&&clicks==1,"Accessible activation uses the same callback once");
    last.setEnabled(false);require(!gui.activateWidget(last.id())&&clicks==1,"Disabled accessibility action is rejected");
    require(!gui.activateWidget(UINT64_MAX),"Stale accessibility IDs do not dereference objects");
    const auto nodes=gui.accessibilityNodes();
    const auto it=std::find_if(nodes.begin(),nodes.end(),[&](const auto& n){return n.id==last.id();});
    require(it!=nodes.end()&&it->disabled&&it->name=="Last"&&it->description=="Final action","Accessible roles/names/state reflect widgets");
    scroll.setAccessibilityHidden(true);require(gui.accessibilityNodes().empty(),"Decorative subtrees can be excluded");require(!gui.focusWidget(first.id()),"Delayed focus cannot enter an accessibility-hidden subtree");scroll.setAccessibilityHidden(false);
    last.setEnabled(true);scroll.setScrollOffset(0);gui.layout({400,300});
    require(gui.focusWidget(scroll.id()),"Scroll regions accept keyboard focus");gui.key(GuiKey::End);require(scroll.scrollOffset()==maximum,"End scrolls to bottom");gui.key(GuiKey::Home);require(scroll.scrollOffset()==0,"Home scrolls to top");
    gui.focusWidget(last.id());const auto r=last.bounds();
    gui.pointer(GuiPointer::Down,{r.x+10,r.y+10});gui.pointer(GuiPointer::Move,{r.x+10,r.y+50});gui.pointer(GuiPointer::Up,{r.x+10,r.y+50});
    require(clicks==1&&!gui.capturesPointer(),"Scroll drag cancels button activation and releases capture");
    gui.setScale(2);gui.layout({800,600});
    require(scroll.bounds().x==20&&scroll.bounds().width==400,"Scale changes widget geometry in CSS pixels");
    require(label.lineCount()>=3&&std::abs(scroll.maxScrollOffset()-maximum)<0.01f,"Scale preserves design-unit scroll geometry and wrap width");
    rejects([&]{gui.setScale(0);});require(gui.scale()==2,"Rejected scale preserves previous scale");
    rejects([&]{scroll.setScrollOffset(NAN);});
    auto& row=gui.root().add<Panel>();row.setPosition({220,0});row.setSize({170,100});row.setLayout(WrapLayout{10,5});
    auto& a=row.add<Button>("A");a.setSize({80,30});auto& b=row.add<Button>("B");b.setSize({80,30});auto& c=row.add<Button>("C");c.setSize({80,30});
    gui.setScale(1);gui.layout({400,300});require(b.bounds().x==310&&c.bounds().y==35,"Flow wraps at available width and accounts for row gaps");
    b.setVisible(false);gui.layout({400,300});require(c.bounds().x==310&&c.bounds().y==0,"Hidden children do not reserve wrap cells");
    require(!gui.draw(0).vertices.empty(),"Scaled/wrapped/scroll content renders");
}
}
int main() {
    try { layoutAndInput();anchorsClipsAndOrdering();textAndRendering();extensions();std::cout<<checks<<" GUI checks passed\n";return 0; }
    catch(const std::exception& error) { std::cerr<<"GUI test failed: "<<error.what()<<'\n';return 1; }
}
