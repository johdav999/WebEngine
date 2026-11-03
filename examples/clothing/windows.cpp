#include "scene.hpp"
#include "engine/desktop.hpp"
#include <iostream>
int main(int argc,char** argv){try{
    using namespace engine;DesktopSettings settings;settings.title="WebEngine character wardrobe";settings.width=1100;settings.height=750;
    bool smoke=false;std::filesystem::path output=".";
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--d3d12")settings.backend=DesktopBackend::Direct3D12;
        else if(arg=="--smoke"&&i+1<argc){smoke=true;output=argv[++i];settings.hidden=true;}else throw std::invalid_argument("Use --d3d12 or --smoke output-directory");}
    clothing_example::Demo demo;DesktopHost host(settings);auto id=host.application().loadScene(demo.make());
    demo.command(8);
    DesktopView view{demo.camera->id()};host.setViews(id,{&view,1});host.application().start(id);
    DesktopFileSettings fileSettings;fileSettings.application="ClothingDemo";if(smoke)fileSettings.userDirectory=std::filesystem::absolute(output);
    DesktopFiles files(fileSettings);host.gui().setFont(GuiFont::loadTrustedTrueType(files.readAsset("fonts/NotoSans-Regular.ttf"),18));
    auto& panel=host.gui().root().add<Panel>();panel.setPosition({12,12});panel.setSize({220,450});panel.setPadding(8);panel.setLayout(VerticalLayout{3});
    panel.add<TextLabel>("Cloth milestone 8").setSize({200,24});auto& status=panel.add<TextLabel>("Ready");status.setSize({200,24});
    auto command=[&](int n){try{
        if(n<5)demo.command(n);
        else if(n==5){auto saved=demo.save();files.writeUser(DesktopStorage::Saves,"outfit.wecl",std::as_bytes(std::span(saved)),4096);}
        else if(n==6){auto data=files.readUser(DesktopStorage::Saves,"outfit.wecl",4096);demo.load(std::string_view(reinterpret_cast<const char*>(data.data()),data.size()));}
        else if(n==7)host.setPaused(host.application().state()!=ApplicationState::Paused);
        else demo.command(n);
        status.setText(n==5?"Outfit saved":n==6?"Outfit restored":"Ready");return true;
    }catch(const std::exception& e){status.setText(e.what());return false;}};
    std::array<const char*,13> labels={"Shirt","Trousers","Boots","Hat","Colors","Save outfit","Load outfit","Pause / resume","Cape on / off","Wind on / off","Reset cloth","Teleport","Moving block on / off"};
    for(int i=0;i<13;++i){auto& button=panel.add<Button>(labels[i]);button.setSize({200,24});button.onClick([&,i]{command(i);});}
    ApplicationFrameCallbacks callbacks;callbacks.present=[&](Scene& scene,const ApplicationFrame&){demo.clothing->synchronize(scene);};
    callbacks.afterSimulation=[&](Scene&,const ApplicationFrame& frame){demo.advance(frame.simulation.delta);};
    if(!smoke)return host.run(callbacks);
    std::filesystem::create_directories(output);const auto deadline=host.nowSeconds()+60;
    auto tick=[&]{if(!host.step(callbacks)||host.nowSeconds()>deadline)throw std::runtime_error("Clothing smoke timeout/closed");if(!host.diagnostics().error.empty())throw std::runtime_error(host.diagnostics().error);};
    while(host.diagnostics().presentedFrames<10)tick();
    auto capture=[&](const char* name){host.captureFrame(output/name);do{tick();}while(host.capturePending());if(!host.captureError().empty())throw std::runtime_error(host.captureError());};
    capture("before.ppm");const auto saved=demo.save();if(!command(5))throw std::runtime_error("Save failed");
    for(int i=0;i<5;++i)command(i);for(int i=0;i<3;++i)tick();capture("after.ppm");
    if(!command(6)||demo.save()!=saved)throw std::runtime_error("Restore mismatch");command(7);command(4);tick();command(7);tick();
    const auto steps=demo.clothing->clothStats(*demo.scene).steps;for(int i=0;i<30;++i)tick();
    if(demo.clothing->clothStats(*demo.scene).steps<=steps)throw std::runtime_error("Cloth did not advance");
    while(demo.clothing->clothStats(*demo.scene).environmentContacts==0)tick();
    while(demo.clothing->clothStats(*demo.scene).surfaceContacts==0)tick();
    while(demo.clothing->clothStats(*demo.scene).aerodynamicFaces==0||demo.clothing->clothStats(*demo.scene).movingContacts==0)tick();
    command(7);tick();const auto frozen=demo.clothMotion();const auto frozenEdges=demo.clothing->clothStats(*demo.scene).edgeContacts;
    for(int i=0;i<3;++i)tick();if(demo.clothMotion()!=frozen||demo.clothing->clothStats(*demo.scene).edgeContacts!=frozenEdges)throw std::runtime_error("Paused cloth moved or edge contacts advanced");
    const auto frozenStats=demo.clothing->clothStats(*demo.scene);const auto frozenObstacle=demo.obstacleOffset;
    for(int i=0;i<3;++i)tick();auto still=demo.clothing->clothStats(*demo.scene);
    if(still.aerodynamicFaces!=frozenStats.aerodynamicFaces||still.movingContacts!=frozenStats.movingContacts||demo.obstacleOffset.x!=frozenObstacle.x||demo.obstacleOffset.z!=frozenObstacle.z)throw std::runtime_error("Paused wind/moving collision advanced");
    capture("cloth.ppm");command(12);command(12);
    command(11);command(11);command(10);command(8);command(8);command(7);tick();
    host.requestClose();host.step(callbacks);std::cout<<"Windows clothing smoke PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
