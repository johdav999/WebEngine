#include "engine/gui.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

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
class HookWidget : public Widget {
public:
    int paints=0,cancels=0,activations=0,moves=0,keys=0;
    Vec2 lastPosition{};
protected:
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onPaint(GuiPainter& painter) override {
        ++paints;painter.fill({-10,-10,80,80},{1,0,0,1});
        painter.text("Custom \xce\xa9",{0,0},{1,1,1,1});
    }
    GuiPointerResult onPointer(const GuiPointerEvent& event) override {
        lastPosition=event.position;
        if(event.action==GuiPointer::Cancel) ++cancels;
        if(event.action==GuiPointer::Move) ++moves;
        if(event.action==GuiPointer::Up&&event.inside) onActivate();
        return GuiPointerResult::Capture;
    }
    bool onActivate() override { ++activations;return true; }
    bool onKey(const GuiKeyEvent& event) override { if(event.key!=GuiKey::Right)return false;++keys;return true; }
    void describeAccessibility(GuiAccessibleNode& node) const override { node.role=GuiRole::Button;node.name="Custom hook";node.id=0; }
};
void customWidgets() {
    GuiSystem gui;gui.setFont(font());gui.setScale(2);
    auto& panel=gui.root().add<Panel>();panel.setPosition({10,10});panel.setSize({100,100});
    auto& custom=panel.add<HookWidget>();custom.setPosition({20,20});custom.setSize({40,30});
    gui.layout({500,400});
    const auto& draw=gui.draw(0);require(custom.paints==1&&!draw.vertices.empty(),"Application-defined widgets paint on the first frame");
    bool clipped=false;
    for(const auto& c:draw.commands) if(c.clip.x==60&&c.clip.y==60&&c.clip.width==80&&c.clip.height==60) clipped=true;
    require(clipped,"Custom paint stays inside the system-owned scaled clip");
    gui.pointer(GuiPointer::Down,{80,80});require(custom.lastPosition.x==10&&custom.lastPosition.y==10,"Hook coordinates are local design units");
    gui.pointer(GuiPointer::Move,{400,300});require(custom.moves==1&&gui.capturesPointer(),"Custom capture receives moves outside bounds");
    gui.pointer(GuiPointer::Up,{400,300});require(custom.activations==0&&!gui.capturesPointer(),"Outside release reaches hook without activation");
    gui.pointer(GuiPointer::Down,{80,80});gui.pointer(GuiPointer::Cancel,{});require(custom.cancels==1,"Explicit cancellation reaches custom widget");
    gui.pointer(GuiPointer::Down,{80,80});custom.setVisible(false);gui.layout({500,400});
    require(custom.cancels==2&&!gui.capturesPointer()&&!gui.focused(),"Hidden capture is cancelled before losing focus");
    custom.setVisible(true);gui.layout({500,400});require(gui.focusWidget(custom.id()),"Custom widget participates in focus navigation");
    require(gui.key(GuiKey::Right)&&custom.keys==1,"Portable key hook receives focused input");
    gui.key(GuiKey::Space);gui.key(GuiKey::Space,false,true);
    require(custom.activations==1,"Generic activation suppresses repeat");
    require(gui.activateWidget(custom.id())&&custom.activations==2,"Accessible activation uses the custom hook");
    auto nodes=gui.accessibilityNodes();
    require(std::any_of(nodes.begin(),nodes.end(),[&](const auto& n){return n.id==custom.id()&&n.role==GuiRole::Button&&n.name=="Custom hook";}),"Custom semantics preserve system identity");
    gui.pointer(GuiPointer::Down,{80,80});custom.setEnabled(false);gui.layout({500,400});
    require(custom.cancels==3&&!gui.activateWidget(custom.id()),"Disabling cancels capture and rejects delayed activation");
}
void checkboxAndSlider() {
    GuiSystem gui;gui.setFont(font());
    auto& scroll=gui.root().add<ScrollPanel>();scroll.setSize({250,135});scroll.setLayout(VerticalLayout{5});
    auto& checkbox=scroll.add<Checkbox>("Sound");checkbox.setSize({200,38});
    auto& slider=scroll.add<Slider>();slider.setSize({200,38});slider.setAccessibleName("Volume");slider.setRange(0,100,5);slider.setValue(50);
    auto& filler=scroll.add<Panel>();filler.setSize({200,200});
    int toggles=0,changes=0;checkbox.onChange([&](bool){++toggles;});slider.onChange([&](float){++changes;});
    gui.layout({400,400});
    gui.pointer(GuiPointer::Down,{20,20});gui.pointer(GuiPointer::Up,{20,20});
    require(checkbox.checked()&&toggles==1,"Checkbox activates once on release");
    checkbox.setChecked(true);require(toggles==1,"Unchanged checkbox values do not notify");
    gui.key(GuiKey::Space);gui.key(GuiKey::Space,false,true);require(!checkbox.checked()&&toggles==2,"Checkbox supports keyboard activation without repeat");
    gui.pointer(GuiPointer::Down,{20,20});gui.pointer(GuiPointer::Move,{20,80});gui.pointer(GuiPointer::Up,{20,80});
    require(!checkbox.checked()&&toggles==2,"Scroll drag cancels pending checkbox activation");
    gui.pointer(GuiPointer::Down,{20,20});gui.pointer(GuiPointer::Cancel,{});gui.pointer(GuiPointer::Up,{20,20});
    require(toggles==2,"Pointer cancellation does not toggle checkbox");
    gui.pointer(GuiPointer::Down,{20,20});checkbox.setEnabled(false);gui.layout({400,400});
    require(!gui.capturesPointer(),"Disabling an ordinary press also clears its pending scroll ownership");
    checkbox.setEnabled(true);gui.layout({400,400});
    gui.pointer(GuiPointer::Down,{12,60});require(slider.value()==0,"Slider track click selects minimum immediately");
    gui.pointer(GuiPointer::Move,{188,110});require(slider.value()==100&&scroll.scrollOffset()==0,"Slider owns diagonal drags inside a scroll panel");
    gui.pointer(GuiPointer::Move,{500,300});gui.pointer(GuiPointer::Up,{500,300});
    require(slider.value()==100&&!gui.capturesPointer(),"Slider clamps captured outside release");
    slider.setValue(52);require(slider.value()==50,"Slider values snap to configured steps");
    const int before=changes;slider.setValue(50);require(changes==before,"Unchanged snapped values do not notify");
    rejects([&]{slider.setRange(100,0,5);});rejects([&]{slider.setRange(0,100,0);});rejects([&]{slider.setRange(0,100,101);});
    rejects([&]{slider.setRange(-std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),1);});
    rejects([&]{slider.setValue(NAN);});rejects([&]{slider.setValue(INFINITY);});
    require(slider.minimum()==0&&slider.maximum()==100&&slider.step()==5&&slider.value()==50&&changes==before,"Invalid range/value preserves complete state and notifications");
    require(gui.focusWidget(slider.id()),"Slider is focusable");
    gui.key(GuiKey::Right);gui.key(GuiKey::Up);require(slider.value()==60,"Right and Up increase the focused slider");
    gui.key(GuiKey::Left);gui.key(GuiKey::Down);require(slider.value()==50,"Left and Down decrease the focused slider");
    gui.key(GuiKey::PageUp);require(slider.value()==100,"Page Up advances ten steps");
    gui.key(GuiKey::Home);require(slider.value()==0,"Home reaches minimum without scrolling");
    gui.key(GuiKey::End);gui.key(GuiKey::PageDown);require(slider.value()==50&&scroll.scrollOffset()==0,"End and Page Down update value without scrolling");
    require(gui.setAccessibleValue(slider.id(),73)&&slider.value()==75,"Accessible numeric actions use the same validation and snapping");
    require(!gui.setAccessibleValue(slider.id(),NAN)&&!gui.setAccessibleValue(0,50),"Invalid semantic requests are rejected");
    slider.setRange(0,1,.3f);gui.key(GuiKey::End);require(slider.value()==1,"Maximum remains reachable when step does not divide range");
    auto nodes=gui.accessibilityNodes();
    require(std::any_of(nodes.begin(),nodes.end(),[&](const auto& n){return n.id==checkbox.id()&&n.role==GuiRole::Checkbox&&!n.checked;}),"Checkbox exposes checked semantics");
    require(std::any_of(nodes.begin(),nodes.end(),[&](const auto& n){return n.id==slider.id()&&n.role==GuiRole::Slider&&n.minimum==0&&n.maximum==1&&n.number==1;}),"Slider exposes name, range and current value");
    gui.setScale(2);gui.layout({800,600});gui.pointer(GuiPointer::Down,{24,120});
    require(slider.value()==0,"Scaled slider hit testing uses design units");
    slider.setEnabled(false);gui.layout({800,600});gui.pointer(GuiPointer::Move,{376,120});gui.pointer(GuiPointer::Up,{376,120});
    require(slider.value()==0&&!gui.capturesPointer()&&!gui.setAccessibleValue(slider.id(),1),"Disabled slider cancels drag and rejects semantic edits");
    slider.setEnabled(true);gui.layout({800,600});gui.pointer(GuiPointer::Down,{200,120});gui.key(GuiKey::Escape);
    const float cancelled=slider.value();gui.pointer(GuiPointer::Move,{376,120});gui.pointer(GuiPointer::Up,{376,120});
    require(slider.value()==cancelled&&!gui.capturesPointer(),"Escape cancels a slider drag without rolling back published values");
    require(!gui.draw(0).vertices.empty(),"Checkbox and slider paint through generic hooks");
}
bool colorIs(GuiColor a,GuiColor b) { return std::abs(a.r-b.r)<.0001f&&std::abs(a.g-b.g)<.0001f&&std::abs(a.b-b.b)<.0001f&&std::abs(a.a-b.a)<.0001f; }
class ThemePaintWidget : public Widget {
public:
    GuiStyle seen;
    void onPaint(GuiPainter& painter) override { seen=painter.style();const auto size=painter.size();painter.fill({0,0,size.x,size.y},seen.background); }
};
void themesAndStates() {
    GuiSystem gui;
    auto& button=gui.root().add<Button>("Theme action");button.setSize({100,40});
    auto& image=gui.root().add<Image>(Texture::white());image.setPosition({150,0});image.setSize({30,30});
    auto& custom=gui.root().add<ThemePaintWidget>();custom.setPosition({200,0});custom.setSize({30,30});
    auto& checkbox=gui.root().add<Checkbox>("Check",true);checkbox.setPosition({0,50});
    gui.layout({500,300});
    const GuiColor red{.8f,.1f,.1f,1},green{.1f,.8f,.1f,1},blue{.1f,.1f,.8f,1},yellow{.8f,.8f,.1f,1},purple{.6f,.1f,.7f,1};
    GuiTheme theme;theme.defaults.background=red;theme.defaults.foreground=green;
    theme.common.normal.border=blue;theme.forKind(GuiStyleKind::Button).normal.background=green;
    auto& states=theme.forKind(GuiStyleKind::Button).states;
    states.hovered.background=blue;states.focused.background=yellow;states.pressed.background=purple;
    states.disabled.background=red;states.disabled.opacity=.2f;
    theme.forKind(GuiStyleKind::Checkbox).states.checked.accent=yellow;
    gui.setTheme(theme);
    require(colorIs(gui.resolvedStyle(button).background,green)&&colorIs(gui.resolvedStyle(button).border,blue),"Shared and component defaults compose");
    require(colorIs(gui.resolvedStyle(image).foreground,{1,1,1,1}),"Images inherit a neutral default tint");
    require(colorIs(gui.resolvedStyle(checkbox).accent,yellow),"Checked state applies without hover/focus");
    button.style.background=red;
    require(colorIs(gui.resolvedStyle(button).background,red),"Inline normal color overrides shared/component defaults");
    button.style.background.reset();require(colorIs(gui.resolvedStyle(button).background,green),"Resetting one override restores live inheritance");
    gui.pointer(GuiPointer::Move,{10,10});require(colorIs(gui.resolvedStyle(button).background,blue),"Hover uses component state style");
    gui.focusWidget(button.id());require(colorIs(gui.resolvedStyle(button).background,yellow),"Focus takes precedence over hover");
    gui.pointer(GuiPointer::Down,{10,10});require(colorIs(gui.resolvedStyle(button).background,purple),"Pressed style remains visible while focused and hovered");
    button.stateStyles.pressed.background=green;require(colorIs(gui.resolvedStyle(button).background,green),"Inline state overrides component state");
    button.setEnabled(false);gui.layout({500,300});
    require(!gui.visualState(button).pressed&&!gui.visualState(button).hovered&&colorIs(gui.resolvedStyle(button).background,red)&&gui.resolvedStyle(button).opacity==.2f,"Disabled beats other states and clears interaction");
    button.stateStyles.disabled.opacity=1;require(gui.resolvedStyle(button).opacity==1,"Disabled appearance is explicitly overridable");
    const auto& draw=gui.draw(0);require(colorIs(custom.seen.background,red),"Custom painter receives the same resolved theme");
    require(!draw.vertices.empty(),"Themed built-in and custom controls generate geometry");
    const auto previousVertices=draw.vertices.size();
    custom.stateStyles.hovered.opacity=NAN;
    rejects([&]{gui.draw(1);});require(draw.vertices.size()==previousVertices,"Invalid inactive state edits retain the last complete draw list");
    custom.stateStyles.hovered.opacity.reset();
    const auto old=gui.theme().defaults.background;
    theme.defaults.opacity=NAN;rejects([&]{gui.setTheme(theme);});require(colorIs(gui.theme().defaults.background,old),"Invalid theme replacement preserves the installed value");
    theme=GuiTheme::light();theme.forKind(GuiStyleKind::Slider).states.disabled.opacity=2;
    rejects([&]{gui.setTheme(theme);});require(colorIs(gui.theme().defaults.background,old),"Invalid component state rejects the entire theme");
    theme=GuiTheme::light();theme.defaults.selection.a=INFINITY;rejects([&]{gui.setTheme(theme);});
    rejects([&]{button.style.background={NAN,0,0,1};});
    rejects([&]{button.style.foreground={0,0,0,2};});
    rejects([&]{theme.forKind(GuiStyleKind::Count);});
    GuiStyle literal;literal.background={0,0,0,0};button.style=literal;
    require(gui.resolvedStyle(button).background.a==1,"Disabled state is applied after an inline full style");
    button.setEnabled(true);gui.layout({500,300});gui.blur();gui.pointer(GuiPointer::Move,{450,250});
    require(gui.resolvedStyle(button).background.a==0,"Transparent inline style remains an explicit override");
    const auto id=button.id();const auto bounds=button.bounds();gui.focusWidget(checkbox.id());
    gui.setTheme(GuiTheme::light());
    require(checkbox.checked()&&gui.focused()==&checkbox&&button.id()==id&&button.bounds().x==bounds.x,"Theme replacement preserves control state, identity, layout and focus");
    require(gui.resolvedStyle(button).background.a==0,"Inline normal colors survive theme replacement");
    button.style=GuiStyleOverrides{};
    require(colorIs(gui.resolvedStyle(button).background,GuiTheme::light().defaults.background),"Fresh overrides restore the current theme");
    GuiSystem opacityGui;
    auto& paint=opacityGui.root().add<ThemePaintWidget>();paint.setSize({30,30});
    paint.style.background={1,0,0,.8f};paint.style.opacity=.5f;opacityGui.layout({100,100});
    require(std::abs(opacityGui.draw(0).vertices.front().color.a-.4f)<.0001f,"Custom painter applies theme opacity exactly once");
    paint.setEnabled(false);paint.stateStyles.disabled.opacity=1;
    require(std::abs(opacityGui.draw(0).vertices.front().color.a-.8f)<.0001f,"Explicit disabled opacity replaces automatic dimming");
}
void sizingAlignmentAndGrid() {
    GuiSystem gui;gui.setFont(font());
    auto& panel=gui.root().add<Panel>();panel.setPosition({10,10});panel.setSize({210,150});panel.setPadding(5);panel.setLayout(GridLayout{2,10,8});
    auto& a=panel.add<Button>("A");a.setSize({50,20});
    auto& b=panel.add<Button>("B");b.setSize({60,40});b.setAlignment(GuiAlign::Center,GuiAlign::Center);
    auto& c=panel.add<Button>("C");c.setSize({300,30});c.setSizeLimits({20,10},{80,50});c.setStretch(true);c.setAlignment(GuiAlign::End,GuiAlign::Start);
    gui.layout({600,400});
    require(a.bounds().x==15&&a.bounds().y==15&&b.bounds().x==137.5f,"Grid equal columns honor padding and cell alignment");
    require(c.bounds().x==30&&c.bounds().y==63&&c.bounds().width==80,"Grid clamps stretch to maximum before end alignment and uses row height");
    const auto oldMin=c.minSize(),oldMax=c.maxSize();
    rejects([&]{c.setMinSize({90,10});});rejects([&]{c.setMaxSize({10,20});});rejects([&]{c.setSizeLimits({NAN,0},{100,100});});
    require(c.minSize().x==oldMin.x&&c.maxSize().x==oldMax.x,"Rejected size limits preserve both bounds");
    rejects([&]{panel.setLayout(GridLayout{0,0,0});});rejects([&]{panel.setLayout(GridLayout{65,0,0});});rejects([&]{panel.setLayout(GridLayout{2,-1,0});});
    require(panel.layoutType()==GuiLayout::Grid&&panel.gridColumns()==2,"Invalid grids preserve the previous layout");
    b.setVisible(false);gui.layout({600,400});require(c.bounds().y==15&&c.bounds().x==135,"Hidden grid children leave no reserved cells");
    b.setVisible(true);gui.setScale(2);gui.layout({1200,800});require(c.bounds().x==60&&c.bounds().y==126&&c.bounds().width==160,"Grid constraints and gaps scale in design units");
    gui.setScale(1);
    auto& column=gui.root().add<Panel>();column.setPosition({250,0});column.setSize({200,120});column.setPadding(10);column.setLayout(VerticalLayout{5});column.setContentAlignment(GuiAlign::Center,GuiAlign::End);
    auto& first=column.add<Button>("First");first.setSize({50,20});
    auto& last=column.add<Button>("Last");last.setSize({70,30});last.setAlignment(GuiAlign::End,GuiAlign::Start);
    gui.layout({600,400});require(first.bounds().x==325&&first.bounds().y==55&&last.bounds().x==370&&last.bounds().y==80,"Flow alignment packs the group and permits child cross-axis overrides");
    rejects([&]{column.setContentAlignment(GuiAlign::Start,static_cast<GuiAlign>(99));});gui.layout({600,400});require(first.bounds().y==55,"Invalid alignment does not partially change the other axis");
    auto& absolute=gui.root().add<Button>("Aligned");absolute.setSize({50,20});absolute.setAlignment(GuiAlign::End,GuiAlign::Center);absolute.setPosition({-10,0});
    gui.layout({600,400});require(absolute.bounds().x==540&&absolute.bounds().y==190,"Absolute self alignment uses the parent content rectangle");
    absolute.clearAlignment();absolute.setAnchor(GuiAnchor::BottomRight);absolute.setPosition({10,10});gui.layout({600,400});require(absolute.bounds().x==540&&absolute.bounds().y==370,"Clearing alignment restores the legacy anchor");
    auto& fit=gui.root().add<Panel>();fit.setPosition({0,180});fit.setPadding(10);fit.setLayout(VerticalLayout{5});fit.setContentSize();
    auto& label=fit.add<TextLabel>("Content");label.setContentSize();
    auto& action=fit.add<Button>("Go");action.setContentSize();
    gui.layout({600,500});require(std::abs(label.bounds().width-gui.font()->measure("Content"))<.01f,"Content-sized labels measure proportional font advances");
    require(fit.bounds().height==label.bounds().height+action.bounds().height+25,"Container content height follows children, gaps and padding");
    const auto oldWidth=fit.bounds().width;action.setText("A substantially longer action");gui.layout({600,500});require(fit.bounds().width>oldWidth,"Content size responds to changed text without manual dimensions");
    action.setVisible(false);gui.layout({600,500});require(fit.bounds().height==label.bounds().height+20,"Hidden content children do not reserve height or trailing gaps");
    auto& wrapped=gui.root().add<TextLabel>("Words wrap into multiple measured lines at the constrained width.");wrapped.setPosition({300,180});wrapped.setContentSize();wrapped.setWrap(true);wrapped.setMaxSize({110,55});
    gui.layout({600,500});require(wrapped.bounds().width==110&&wrapped.bounds().height==55&&wrapped.lineCount()>2,"Width is constrained before wrapping and maximum height clips measured text");
    auto texture=std::make_shared<Texture>();texture->width=60;texture->height=30;
    auto& image=gui.root().add<Image>(texture);image.setContentSize();image.setPosition({460,0});gui.layout({600,500});require(image.bounds().width==60&&image.bounds().height==30,"Image content size uses intrinsic texture dimensions");
    image.setSize({120,10});image.setContentSize(false,true);gui.layout({600,500});require(image.bounds().height==60,"One content-sized image axis preserves source aspect ratio");
    auto& dependency=gui.root().add<Panel>();dependency.setPosition({460,80});dependency.setPadding(10);dependency.setContentSize();
    auto& stretched=dependency.add<Button>("Stable");stretched.setSize({60,20});stretched.setStretch(true,true);
    gui.layout({600,500});require(dependency.bounds().width==80&&dependency.bounds().height==40&&stretched.bounds().width==60,"Content measurement ignores dependent stretch to avoid size cycles");
    gui.focusWidget(a.id());const auto bounds=a.bounds();const auto root=gui.root().bounds();
    a.setPosition({std::numeric_limits<float>::max(),0});a.setSize({std::numeric_limits<float>::max(),20});rejects([&]{gui.layout({800,700});});
    require(a.bounds().x==bounds.x&&gui.root().bounds().width==root.width&&gui.focused()==&a,"Overflow rejects the complete layout without publishing bounds or focus changes");
    a.setPosition({0,0});a.setSize({50,20});
    GuiSystem scrolling;scrolling.setFont(font());
    auto& scroll=scrolling.root().add<ScrollPanel>();scroll.setSize({220,80});scroll.setPadding(5);scroll.setLayout(VerticalLayout{0});
    auto& grid=scroll.add<Panel>();grid.setStretch(true);grid.setContentSize(false,true);grid.setLayout(GridLayout{2,8,8});
    Button* end=nullptr;for(int i=0;i<6;++i) {auto& item=grid.add<Button>("Item "+std::to_string(i));item.setStretch(true);end=&item;}
    scrolling.layout({400,300});require(grid.bounds().height==130&&scroll.maxScrollOffset()==60,"Content-sized grid contributes full extent to scrolling");
    require(scrolling.focusWidget(end->id())&&end->clip().height>0,"Keyboard/accessible focus reveals measured grid descendants");
    const auto offset=scroll.scrollOffset();scrolling.setScale(2);scrolling.layout({800,600});require(scroll.scrollOffset()==offset&&grid.bounds().height==260,"Scroll offsets stay in design units after measured layout scales");
    require(!scrolling.draw(0).vertices.empty(),"Measured grid controls render through the retained draw list");
    GuiSystem offsets;
    auto& row=offsets.root().add<Panel>();row.setPadding(10);row.setContentSize();row.setLayout(HorizontalLayout{5});
    row.add<Image>().setSize({20,20});auto& shifted=row.add<Image>();shifted.setSize({30,10});shifted.setPosition({40,0});
    offsets.layout({300,200});require(row.bounds().width==115&&shifted.bounds().x+shifted.bounds().width<=row.bounds().x+row.bounds().width-10,"Content width encloses offset flow children instead of clipping their placed extent");
}
}
int main() {
    try { layoutAndInput();anchorsClipsAndOrdering();textAndRendering();extensions();customWidgets();checkboxAndSlider();themesAndStates();sizingAlignmentAndGrid();std::cout<<checks<<" GUI checks passed\n";return 0; }
    catch(const std::exception& error) { std::cerr<<"GUI test failed: "<<error.what()<<'\n';return 1; }
}
