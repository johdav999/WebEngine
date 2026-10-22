#include "../examples/windows_content_app.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace engine;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    const auto output=std::filesystem::absolute(argc>1?argv[1]:"build/evidence/content-iteration-windows");std::filesystem::create_directories(output);
    DesktopFiles files;const auto model=output/"iteration.glb",texture=output/"iteration.tga";writeDesktopFileAtomic(model,files.readAsset("iteration.glb"));auto image=files.readAsset("iteration.tga");writeDesktopFileAtomic(texture,image);
    DesktopSettings settings;settings.hidden=true;settings.width=1280;settings.height=800;DesktopHost host(settings);content_example::WindowsContentApp app(host,model,texture);
    auto step=[&]{check(host.step(app.callbacks()),"Content host closed");std::this_thread::sleep_for(std::chrono::milliseconds(1));};
    auto until=[&](auto condition){const auto start=host.nowSeconds();while(!condition()){check(host.nowSeconds()-start<30,"Content integration timeout");step();}};
    auto capture=[&](const char* name){host.captureFrame(output/name);until([&]{return !host.capturePending();});check(host.captureError().empty(),"Content readback failed");};
    until([&]{return host.diagnostics().ready&&app.content().ready();});capture("initial.ppm");
    check(host.gui().activateWidget(app.watch().id()),"Watch GUI activation failed");until([&]{return app.content().reloadProgress().generation>0&&app.content().reloadProgress().status==AssetStatus::Ready;});
    const auto revision=app.content().modelRevision();for(std::size_t i=18;i<image.size();i+=4){image[i]=std::byte{240};image[i+1]=std::byte{60};image[i+2]=std::byte{20};}writeDesktopFileAtomic(texture,image);
    until([&]{return app.content().modelRevision()>revision;});const auto changed=app.content().modelRevision();capture("reloaded.ppm");
    const std::array bad{std::byte{1}};writeDesktopFileAtomic(texture,bad);until([&]{return app.content().reloadProgress().status==AssetStatus::Failed;});check(app.content().modelRevision()==changed,"Corrupt dependency replaced model");capture("failed.ppm");
    check(host.gui().activateWidget(app.button("nested").id()),"Nested GUI activation failed");step();capture("nested.ppm");
    check(host.gui().activateWidget(app.button("undo").id()),"Undo GUI activation failed");step();capture("undo.ppm");
    check(host.gui().activateWidget(app.button("redo").id()),"Redo GUI activation failed");step();capture("redo.ppm");
    const auto bytes=PrefabAssetArchive::save(app.content().prefab());writeDesktopFileAtomic(output/"assembly.weprefab",bytes);check(!PrefabAssetArchive::load(bytes)->dependencies().empty(),"Nested prefab export lost dependency");
    writeDesktopFileAtomic(texture,image);until([&]{return app.content().modelRevision()>changed;});
    check(host.gui().activateWidget(app.button("reimport").id())&&host.gui().activateWidget(app.button("cancel").id()),"Reimport/cancel GUI activation failed");check(app.content().reloadProgress().status==AssetStatus::Cancelled,"Cancel did not stop staged generation");
    std::ofstream report(output/"report.json");report<<"{\"passed\":true,\"frames\":"<<host.diagnostics().presentedFrames<<",\"dependencyReload\":true,\"failurePreserved\":true,\"guiActions\":true,\"nestedUndoRedo\":true,\"repair\":true,\"cancel\":true}\n";
    std::cout<<"Windows content iteration passed: "<<output<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
