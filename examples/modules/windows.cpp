#include "scene.hpp"
#include "engine/desktop.hpp"
#include <iostream>
template<class Settings> void selectBackend(Settings& settings,bool direct=false) {
    if constexpr(requires { settings.backend; }) {
        using Backend=decltype(settings.backend);
        settings.backend=direct?Backend::Direct3D12:Backend::Dawn;
    } else if(direct) throw std::invalid_argument("Direct D3D12 is not in this engine revision");
}
int main(int argc,char** argv){try{
    using namespace engine;
    bool smoke=false;std::filesystem::path output=".";DesktopSettings settings;settings.title="WebEngine module sample";settings.width=900;settings.height=650;selectBackend(settings);
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--smoke"&&i+1<argc){smoke=true;output=argv[++i];settings.hidden=true;}else if(arg=="--d3d12")selectBackend(settings,true);else throw std::invalid_argument("Use --smoke output-directory or --d3d12");}
    counter_example::Demo demo;counter_example::View view;DesktopHost host(settings);
    auto id=host.application().loadScene(view.make());DesktopView v{view.camera->id()};host.setViews(id,{&v,1});host.application().start(id);
    DesktopFiles files;host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"),18));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({20,20});panel.setSize({390,240});panel.setPadding(12);panel.setLayout(VerticalLayout{6});
    panel.add<TextLabel>("Two independent plugin sessions").setSize({360,26});
    auto& status=panel.add<TextLabel>();status.setSize({360,26});
    auto refresh=[&]{status.setText("A: "+std::to_string(demo.first->counter.value)+" / B: "+std::to_string(demo.second->counter.value));view.project(demo);};
    auto command=[&](int n){if(n<2)demo.add(n);else demo.reset(n-2);refresh();};
    auto& addA=panel.add<Button>("Add to A");addA.onClick([&]{command(0);});
    auto& addB=panel.add<Button>("Add to B");addB.onClick([&]{command(1);});
    auto& resetA=panel.add<Button>("Reset A");resetA.onClick([&]{command(2);});refresh();
    if(!smoke)return host.run();
    std::filesystem::create_directories(output);const auto deadline=host.nowSeconds()+60;
    auto tick=[&]{if(!host.step()||host.nowSeconds()>deadline)throw std::runtime_error("Desktop module timeout/closed");if(!host.diagnostics().error.empty())throw std::runtime_error(host.diagnostics().error);};
    while(host.diagnostics().presentedFrames<10)tick();
    auto capture=[&](const char* name){host.captureFrame(output/name);do{tick();}while(host.capturePending());if(!host.captureError().empty())throw std::runtime_error(host.captureError());};
    capture("before.ppm");command(0);command(0);command(1);
    for(int i=0;i<4;++i)tick();capture("after.ppm");
    if(demo.first->counter.value!=2||demo.second->counter.value!=1||!demo.rejectsBrokenReplacement())throw std::runtime_error("Module isolation/replacement failed");
    command(2);if(demo.first->counter.value!=0||demo.second->counter.value!=1)throw std::runtime_error("Session reset isolation failed");
    host.setPaused(true);for(int i=0;i<3;++i)tick();host.setPaused(false);tick();
    host.requestClose();host.step();
    std::cout<<"Desktop module demo PASS: independent sessions, rejected replacement, reset, pause, rendered frames and shutdown\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
