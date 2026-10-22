#include "gui_browser.hpp"
#include <emscripten.h>

EM_JS(int, gui_demo_requested, (), { return new URLSearchParams(location.search).get('gui') === '1'; });
EM_JS(int, gui_test_requested, (), { return new URLSearchParams(location.search).get('guiTest') === '1'; });
EM_JS(int, gui_extensions_requested, (), { return new URLSearchParams(location.search).get('guiExtensions') === '1'; });
EM_JS(int, gui_settings_requested, (), { return new URLSearchParams(location.search).get('guiSettings') === '1'; });
EM_JS(int, gui_theme_requested, (), { return new URLSearchParams(location.search).get('guiTheme') === '1'; });
EM_JS(int, gui_layout_requested, (), { return new URLSearchParams(location.search).get('guiLayout') === '1'; });
namespace engine {
namespace {
std::unique_ptr<GuiSystem> demo;
// Application-defined widget: paints without renderer APIs and uses the same
// portable activation/capture path as built-in controls.
class SettingsPreview : public Widget {
public:
    GuiStyleKind styleKind() const override { return GuiStyleKind::Button; }
    float level=0.65f;
    bool enabled=true;
    std::function<void()> reset;
protected:
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    bool onActivate() override { if(reset) reset();return true; }
    GuiPointerResult onPointer(const GuiPointerEvent& event) override {
        if(event.action==GuiPointer::Up&&event.inside) onActivate();
        return GuiPointerResult::Handled;
    }
    void describeAccessibility(GuiAccessibleNode& node) const override { node.role=GuiRole::Button; }
    void onPaint(GuiPainter& painter) override {
        const auto& style=painter.style();
        const auto size=painter.size();
        painter.fill({0,0,size.x,size.y},painter.focused() ? style.focusRing : style.border);
        painter.fill({2,2,std::max(0.f,size.x-4),std::max(0.f,size.y-4)},style.background);
        painter.fill({8,size.y-12,std::max(0.f,(size.x-16)*(enabled ? level : 0)),4},style.accent);
        painter.text(enabled ? "Preview / reset" : "Preview muted / reset",{8,6},style.foreground);
    }
};
}
void buildGuiDemo(std::function<void()> resetCamera) {
    if(!gui_demo_requested()) return;
    demo=std::make_unique<GuiSystem>();
    auto& panel=demo->root().add<Panel>();panel.setPosition({20,20});panel.setSize({312,338});panel.setPadding(16);panel.setLayout(VerticalLayout{8});
    auto& header=panel.add<Panel>();header.setSize({280,52});header.setLayout(HorizontalLayout{12});
    header.style.background={0,0,0,0};header.style.border={0,0,0,0};
    auto texture=std::make_shared<Texture>();texture->name="GUI demo avatar";texture->width=texture->height=48;texture->pixels.resize(48*48*4);
    for(int y=0;y<48;++y) for(int x=0;x<48;++x) {
        const auto i=(y*48+x)*4;const bool head=(x-24)*(x-24)+(y-17)*(y-17)<64;
        const bool body=y>29&&std::abs(x-24)<(y-27);
        texture->pixels[i]=head||body ? 210 : 15;texture->pixels[i+1]=head||body ? 241 : 118;
        texture->pixels[i+2]=head||body ? 250 : 151;texture->pixels[i+3]=255;
    }
    auto& image=header.add<Image>(texture);image.setSize({48,48});
    image.setAccessibleName("Player avatar");
    auto& title=header.add<TextLabel>("PLAYER PROFILE");title.setSize({220,48});
    auto& label=panel.add<TextLabel>("Player name");label.setSize({280,24});
    auto& input=panel.add<TextInput>();input.setSize({280,40});input.setPlaceholder("Enter your name");
    input.setAccessibleName("Player name");
    auto& buttons=panel.add<Panel>();buttons.setSize({280,40});buttons.setLayout(HorizontalLayout{8});buttons.style.background={0,0,0,0};buttons.style.border={0,0,0,0};
    auto& join=buttons.add<Button>("Join game");join.setSize({136,40});
    auto& clear=buttons.add<Button>("Clear");clear.setSize({136,40});
    auto& status=panel.add<TextLabel>("Ready to join");status.setSize({280,30});
    status.setLiveRegion(true);
    const auto submit=[&input,&status] { status.setText(input.text().empty() ? "Please enter your name" : "Welcome, "+input.text()+"!"); };
    join.onClick(submit);input.onSubmit(submit);
    clear.onClick([&input,&status] { input.setText("");status.setText("Ready to join"); });
    auto& reset=panel.add<Button>("Reset camera");reset.setSize({280,38});reset.onClick(std::move(resetCamera));
    auto& hint=panel.add<TextLabel>("Tab to focus / Enter to join");hint.setSize({280,22});hint.style.foreground={0.6f,0.7f,0.8f,1};
    if(gui_test_requested()) {
        auto& second=demo->root().add<TextInput>();second.setPosition({20,380});second.setSize({280,40});second.setText("Second field");
    }
    if(gui_extensions_requested()) {
        auto& scroll=demo->root().add<ScrollPanel>();scroll.setSize({280,210});scroll.setAnchor(GuiAnchor::BottomRight);scroll.setPosition({20,80});
        scroll.setPadding(12);scroll.setLayout(VerticalLayout{8});scroll.setAccessibleName("Interface settings");
        scroll.setAccessibleDescription("Scroll with the wheel, a drag, or Page Up and Page Down.");
        auto& text=scroll.add<TextLabel>("This text wraps to the available width. Interface scale changes geometry, text and hit testing together.");
        text.setStretch(true);text.setWrap(true);
        auto& row=scroll.add<Panel>();row.setStretch(true);row.setSize({0,86});row.setLayout(WrapLayout{8,8});row.style.background={0,0,0,0};row.style.border={0,0,0,0};
        for(float scale:{1.f,1.25f,1.5f}) {
            auto& button=row.add<Button>(scale==1 ? "100%" : scale==1.25f ? "125%" : "150%");button.setSize({100,38});
            button.onClick([scale] { demo->setScale(scale); });
        }
        for(int i=1;i<=5;++i) { auto& item=scroll.add<Button>("Option "+std::to_string(i));item.setStretch(true);item.setSize({0,38});item.onClick([&status,i] { status.setText("Selected option "+std::to_string(i)); }); }
    }
    if(gui_settings_requested()) {
        auto& settings=demo->root().add<ScrollPanel>();settings.setAnchor(GuiAnchor::BottomRight);
        settings.setPosition({20,120});settings.setSize({280,278});settings.setPadding(12);
        settings.setLayout(VerticalLayout{8});settings.setAccessibleName("Sound settings");
        auto& heading=settings.add<TextLabel>("SOUND SETTINGS");heading.setStretch(true);heading.setSize({0,26});
        auto& sound=settings.add<Checkbox>("Enable sound",true);sound.setStretch(true);
        auto& volumeLabel=settings.add<TextLabel>("Volume: 65%");volumeLabel.setStretch(true);volumeLabel.setSize({0,26});
        auto& volume=settings.add<Slider>();volume.setStretch(true);volume.setRange(0,100,5);volume.setValue(65);
        volume.setAccessibleName("Volume");volume.setAccessibleDescription("Use arrows, Page Up or Page Down, Home and End.");
        auto& preview=settings.add<SettingsPreview>();preview.setStretch(true);preview.setSize({0,50});
        preview.setAccessibleName("Reset sound preview");preview.setAccessibleDescription("Restore enabled sound and 65 percent volume.");
        auto& help=settings.add<TextLabel>("Local preview only. Drag the slider or use arrow keys. Activate the preview to reset.");
        help.setStretch(true);help.setWrap(true);
        sound.onChange([&preview](bool enabled) { preview.enabled=enabled; });
        volume.onChange([&preview,&volumeLabel](float value) { preview.level=value/100;volumeLabel.setText("Volume: "+std::to_string(int(value))+"%"); });
        preview.reset=[&sound,&volume] { sound.setChecked(true);volume.setValue(65); };
    }
    if(gui_theme_requested()) {
        panel.setVisible(false);
        const auto theme=[](bool light) {
            auto result=light ? GuiTheme::light() : GuiTheme{};
            result.forKind(GuiStyleKind::Checkbox).states.checked.accent={0.2f,0.65f,0.38f,1};
            return result;
        };
        demo->setTheme(theme(false));
        auto& showcase=demo->root().add<ScrollPanel>();showcase.setPosition({20,20});showcase.setSize({360,560});
        showcase.setPadding(16);showcase.setLayout(VerticalLayout{8});showcase.setAccessibleName("GUI theme showcase");
        auto& heading=showcase.add<TextLabel>("GUI THEMES / STATES");heading.setStretch(true);heading.setSize({0,26});
        const auto row=[&showcase]() -> Panel& {
            auto& result=showcase.add<Panel>();result.setStretch(true);result.setSize({0,38});result.setLayout(HorizontalLayout{8});
            result.style.background={0,0,0,0};result.style.border={0,0,0,0};return result;
        };
        auto& themes=row();auto& dark=themes.add<Button>("Dark theme");dark.setSize({160,38});
        auto& light=themes.add<Button>("Light theme");light.setSize({160,38});
        dark.onClick([theme] { demo->setTheme(theme(false)); });light.onClick([theme] { demo->setTheme(theme(true)); });
        auto& caption=showcase.add<TextLabel>("Hover, press, Tab to focus");caption.setStretch(true);caption.setSize({0,26});
        auto& name=showcase.add<TextInput>();name.setStretch(true);name.setPlaceholder("Player nickname");name.setAccessibleName("Theme nickname");
        auto& motion=showcase.add<Checkbox>("Enable animations",true);motion.setStretch(true);
        auto& level=showcase.add<Slider>();level.setStretch(true);level.setRange(0,100,5);level.setValue(65);level.setAccessibleName("Preview level");
        auto& preview=showcase.add<SettingsPreview>();preview.setStretch(true);preview.setSize({0,50});preview.setAccessibleName("Reset themed preview");
        motion.onChange([&preview](bool value){ preview.enabled=value; });level.onChange([&preview](float value){ preview.level=value/100; });
        preview.reset=[&motion,&level]{ motion.setChecked(true);level.setValue(65); };
        auto& actions=row();auto& special=actions.add<Button>("Local style");special.setSize({160,38});
        special.style.background={0.3f,0.14f,0.48f,1};special.style.foreground={1,1,1,1};
        special.stateStyles.hovered.background={0.48f,0.24f,0.68f,1};
        special.stateStyles.pressed.background={0.18f,0.06f,0.3f,1};
        special.stateStyles.focused.focusRing={1,0.72f,0.2f,1};
        // Keep the inline base color while focused, independently of theme button defaults.
        special.stateStyles.focused.background={0.3f,0.14f,0.48f,1};
        special.onClick([&caption]{ caption.setText("Local action activated"); });
        auto& disable=actions.add<Button>("Disable controls");disable.setSize({160,38});
        disable.onClick([&name,&motion,&level,&preview,&special,&disable] {
            const bool enabled=!motion.enabled();name.setEnabled(enabled);motion.setEnabled(enabled);level.setEnabled(enabled);
            preview.setEnabled(enabled);special.setEnabled(enabled);disable.setText(enabled ? "Disable controls" : "Enable controls");
        });
        auto& unavailable=showcase.add<Checkbox>("Unavailable option",true);unavailable.setStretch(true);unavailable.setEnabled(false);
        auto& artwork=showcase.add<Image>(texture);artwork.setSize({48,48});artwork.setAccessibleName("Untinted theme image");
        auto& help=showcase.add<TextLabel>("Theme changes preserve values and focus. Purple is a local override. Scroll to see all controls.");
        help.setStretch(true);help.setWrap(true);
    }
    if(gui_layout_requested()) {
        panel.setVisible(false);demo->root().setPadding(50);
        auto& showcase=demo->root().add<ScrollPanel>();showcase.setStretch(true,true);showcase.setSizeLimits({300,260},{760,560});
        showcase.setPadding(16);showcase.setLayout(VerticalLayout{12});showcase.setAccessibleName("Layout showcase");
        auto& title=showcase.add<TextLabel>("MEASURE / ALIGN / GRID");title.setStretch(true);title.setContentSize(false,true);
        auto& actions=showcase.add<Panel>();actions.setStretch(true);actions.setContentSize(false,true);actions.setLayout(WrapLayout{8,8});
        actions.style.background={0,0,0,0};actions.style.border={0,0,0,0};actions.setAccessibleName("Layout controls");
        const auto action=[&actions](const char* text)->Button& {
            auto& result=actions.add<Button>(text);result.setContentSize();result.setSizeLimits({110,38},{190,50});return result;
        };
        auto& two=action("2 columns");auto& three=action("3 columns");auto& details=action("Toggle details");
        auto& align=action("Cycle alignment");auto& hide=action("Hide card");
        auto& description=showcase.add<TextLabel>("Content-sized text and containers follow their children.");description.setStretch(true);description.setContentSize(false,true);description.setWrap(true);description.setAccessibleName("Measured description");
        details.onClick([&description] {
            const bool longText=description.text().size()<100;description.setText(longText ?
                "Content-sized labels wrap after width constraints are applied. Nested containers measure their visible children, padding and gaps. Grid rows use the tallest card, and changing the column count or hiding a card recomputes placement without recreating controls." :
                "Content-sized text and containers follow their children.");
        });
        auto& alignment=showcase.add<Panel>();alignment.setStretch(true);alignment.setSize({0,70});alignment.setLayout(HorizontalLayout{10});alignment.setContentAlignment(GuiAlign::Start,GuiAlign::Center);alignment.setAccessibleName("Alignment strip");
        for(const auto* text:{"Left","Middle","Right"}) {auto& item=alignment.add<Button>(text);item.setSize({text==std::string_view("Middle") ? 90.f : 76.f,text==std::string_view("Middle") ? 46.f : 36.f});}
        const auto alignmentState=std::make_shared<unsigned>(0);
        align.onClick([&alignment,alignmentState] {*alignmentState=(*alignmentState+1)%3;alignment.setContentAlignment(static_cast<GuiAlign>(*alignmentState),GuiAlign::Center);});
        auto& grid=showcase.add<Panel>();grid.setStretch(true);grid.setContentSize(false,true);grid.setPadding(12);grid.setLayout(GridLayout{3,12,12});grid.setContentAlignment(GuiAlign::Center,GuiAlign::Start);grid.setAccessibleName("Measured grid");
        auto& status=showcase.add<TextLabel>("Choose a card");status.setStretch(true);status.setContentSize(false,true);status.setLiveRegion(true);
        Panel* second=nullptr;
        for(int i=1;i<=6;++i) {
            auto& card=grid.add<Panel>();card.setStretch(true);card.setContentSize(false,true);card.setSizeLimits({100,90},{220,170});card.setPadding(8);card.setLayout(VerticalLayout{6});card.setAccessibleName("Card "+std::to_string(i));
            card.style.background={0.1f,0.15f,0.21f,1};
            auto& heading=card.add<TextLabel>(i%2 ? "Short item" : "A longer item title that wraps");heading.setStretch(true);heading.setWrap(true);
            auto& choose=card.add<Button>("Choose "+std::to_string(i));choose.setStretch(true);choose.setContentSize(false,true);choose.setMinSize({0,38});
            choose.onClick([&status,i]{status.setText("Selected card "+std::to_string(i));});
            if(i==2) second=&card;
        }
        two.onClick([&grid]{grid.setLayout(GridLayout{2,12,12});});three.onClick([&grid]{grid.setLayout(GridLayout{3,12,12});});
        hide.onClick([second,&hide]{second->setVisible(!second->visible());hide.setText(second->visible() ? "Hide card" : "Show card");});
    }
    attachBrowserGui(demo.get());
}
}
