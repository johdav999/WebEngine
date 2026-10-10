#include "gui_browser.hpp"
#include <emscripten.h>

EM_JS(int, gui_demo_requested, (), { return new URLSearchParams(location.search).get('gui') === '1'; });
EM_JS(int, gui_test_requested, (), { return new URLSearchParams(location.search).get('guiTest') === '1'; });
EM_JS(int, gui_extensions_requested, (), { return new URLSearchParams(location.search).get('guiExtensions') === '1'; });
namespace engine {
namespace { std::unique_ptr<GuiSystem> demo; }
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
    attachBrowserGui(demo.get());
}
}
