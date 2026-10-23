#include "engine/editor_desktop.hpp"
#include "engine/mesh_component.hpp"
#include "engine/animation_library.hpp"
#include "engine/prefab_asset.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace engine;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(...){rejected=true;}require(rejected,"Rejected action changed valid state");}
std::uint64_t widget(GuiSystem& gui,const std::string& name){for(const auto& n:gui.accessibilityNodes())if(n.name==name&&n.keyboardFocusable)return n.id;throw std::runtime_error("Missing GUI control: "+name);}
}
int main(int argc,char** argv){try{
    const auto evidence=argc>1?std::filesystem::path(argv[1]):std::filesystem::path(DESKTOP_EDITOR_TEST_DIR);std::filesystem::create_directories(evidence);
    DesktopSettings settings;settings.title="WebEngine native editor acceptance";settings.hidden=true;settings.width=1440;settings.height=900;DesktopHost host(settings);DesktopEditor editor(host);double time=0;
    auto frame=[&]{require(editor.step(time+=1./60),"Editor closed unexpectedly");};auto wait=[&](auto condition){const auto deadline=host.nowSeconds()+90;while(!condition()){if(host.nowSeconds()>deadline)throw std::runtime_error("Native editor acceptance timed out");frame();std::this_thread::sleep_for(std::chrono::milliseconds(2));}};
    wait([&]{return host.diagnostics().ready;});for(int i=0;i<8;++i)frame();
    auto activate=[&](const std::string& name){require(host.gui().activateWidget(widget(host.gui(),name)),"GUI control did not activate");frame();};
    activate("Cube");require(editor.session().selected()&&editor.session().selected()->name=="Cube","Quick Add did not select new object");
    const auto savedId=editor.session().selected()->savedId();const auto original=editor.session().selected()->transform;
    // Real native SDL text input, followed by the ordinary retained GUI action.
    require(host.gui().focusWidget(widget(host.gui(),"Position X")),"Transform field cannot focus");host.gui().focusedInput()->setSelection(0,host.gui().focusedInput()->text().size());frame();
    int windowCount=0;auto** nativeWindows=SDL_GetWindows(&windowCount);require(windowCount==1,"Expected editor window");const auto nativeId=SDL_GetWindowID(nativeWindows[0]);SDL_free(nativeWindows);
    SDL_Event typed{};typed.type=SDL_EVENT_TEXT_INPUT;typed.text.windowID=nativeId;typed.text.text="2.5";require(SDL_PushEvent(&typed),"Native text injection failed");frame();activate("Apply transform");require(editor.session().selected()->transform.position.x==2.5f,"GUI transform did not reach editor command");
    activate("Undo");require(editor.session().selected()->transform.position.x==original.position.x,"Undo failed");activate("Redo");require(editor.session().selected()->transform.position.x==2.5f,"Redo failed");
    require(host.gui().focusWidget(widget(host.gui(),"Scale X")),"Scale field cannot focus");host.gui().editText("0",1,1);activate("Apply transform");require(editor.session().selected()->transform.scale.x==1,"Invalid scale replaced valid scene");
    activate("Duplicate");require(editor.session().selected()->savedId()!=savedId,"Duplicate reused saved ID");activate("Undo");require(editor.source().findSavedNode(savedId),"Undo duplicate removed original");
    editor.session().select(savedId);frame();host.gui().layout({1440,900});
    const auto path=evidence/"native-scene.wescene";editor.save(path);require(!editor.session().dirty(),"Save did not clear dirty state");const auto before=readDesktopFile(path);const auto savedDocument=SceneArchive::load(before);require(savedDocument.views.front().camera->viewport().width==1,"Workspace viewport leaked into saved scene");
    editor.play();require(editor.playing()&&host.application().activeScene()!=&editor.source(),"Play reused source");rejects([&]{editor.session();});rejects([&]{editor.save(evidence/"invalid-play.wescene");});
    auto* runtime=host.application().activeScene()->findSavedNode(savedId);require(runtime,"Play lost source identities");runtime->transform.position.x=99;for(int i=0;i<15;++i)frame();editor.stop();frame();
    require(editor.source().findSavedNode(savedId)->transform.position.x==2.5f,"Play mutated authoring scene");require(!editor.session().dirty(),"Play changed authoring history");
    require(editor.session().select(savedId),"Selection failed");auto transform=editor.session().selected()->transform;transform.position.z=3;require(editor.session().setTransform(transform),"Scene edit failed");rejects([&]{editor.open(path);});require(editor.session().selected()->transform.position.z==3,"Dirty-open rejection changed source");
    const auto bad=evidence/"malformed.wescene";writeDesktopFileAtomic(bad,std::array{std::byte{1}});rejects([&]{editor.open(bad,true);});require(editor.session().selected()->transform.position.z==3,"Bad import damaged authoring scene");
    editor.open(path,true);frame();require(editor.source().findSavedNode(savedId)->transform.position.z==original.position.z,"Open did not restore archive");require(!editor.session().dirty()&&!editor.session().undoCount(),"Open retained old history");
    // Scene/camera replacement is followed by actual renderer output and a fresh GUI.
    host.captureFrame(evidence/"editor.ppm");wait([&]{return !host.capturePending();});require(host.captureError().empty(),"Framebuffer capture failed");
    // Exercise window resizing and real SDL mouse dispatch on Quick Add.
    host.setWindowSize(1280,800);for(int i=0;i<8;++i)frame();host.gui().layout({1280,800});
    const auto cube=widget(host.gui(),"Cube");GuiRect rect;for(const auto& n:host.gui().accessibilityNodes())if(n.id==cube)rect=n.bounds;
    int count=0;auto** windows=SDL_GetWindows(&count);require(count==1,"Expected one native window");const auto windowId=SDL_GetWindowID(windows[0]);SDL_free(windows);SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=windowId;e.button.button=SDL_BUTTON_LEFT;e.button.down=true;e.button.x=rect.x+rect.width/2;e.button.y=rect.y+rect.height/2;SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;e.button.down=false;SDL_PushEvent(&e);frame();frame();
    require(editor.session().dirty(),"SDL mouse click did not reach Quick Add");
    const auto d=host.diagnostics();
    editor.save(evidence/"native-after-mouse.wescene");
    if(argc>2){editor.open(std::filesystem::path(argv[2]));frame();std::string exchanged;editor.source().visit([&](const Node& n,const Mat4&){if(n.name=="Cube"&&n.transform.position.x==4)exchanged=n.savedId();});require(!exchanged.empty(),"Browser-edited scene did not load in native editor");require(editor.session().select(exchanged),"Browser scene cannot select in native editor");frame();host.captureFrame(evidence/"browser-exchange.ppm");wait([&]{return !host.capturePending();});}
    // Exercise the actual shared tool frontend inside the native SDL/Dawn host.
    activate("Authoring tools");activate("Create terrain");require(editor.session().terrainActor(),"Native terrain creation failed");activate("Stamp terrain");
    auto toolCapture=[&](const std::string& name){host.captureFrame(evidence/(name+".ppm"));wait([&]{return !host.capturePending();});require(host.captureError().empty(),"Tool framebuffer capture failed");};
    toolCapture("terrain-tools");activate("AI");activate("Add AI state");toolCapture("ai-tools");
    const auto aiBytes=editor.authoring().saveDocument();writeDesktopFileAtomic(evidence/"native.weai",aiBytes);
    activate("Animation");auto rig=std::make_shared<Skeleton>();rig->joints={{"Root",-1,{}}};rig->bindings={{},{0,Mat4::identity()}};rig->clips={{"Idle",1,2,{{},{}}}};editor.authoring().openDocument(AnimationLibraryArchive::save(*AnimationLibrary::fromSkeleton(rig)),true);frame();toolCapture("animation-tools");writeDesktopFileAtomic(evidence/"native.weanim",editor.authoring().saveDocument());
    activate("Prefab");Actor prop("Prop");prop.addComponent<MeshComponent>("Mesh",Mesh::cube());editor.authoring().openDocument(PrefabAssetArchive::save(PrefabAsset::capture(prop,"Prop")),true);frame();activate("Place linked prefab");toolCapture("prefab-tools");writeDesktopFileAtomic(evidence/"native.weprefab",editor.authoring().saveDocument());
    activate("FX");activate("Explosion preset");activate("Place FX");toolCapture("fx-tools");writeDesktopFileAtomic(evidence/"native.wefx",editor.authoring().saveDocument());
    activate("Cinematic");editor.session().select(savedId);activate("Add cinematic track for selection");toolCapture("cinematic-tools");writeDesktopFileAtomic(evidence/"native.wecin",editor.authoring().saveDocument());
    const auto cinematicButton=widget(host.gui(),"Add cinematic track for selection");editor.play();frame();require(!host.gui().activateWidget(cinematicButton),"Tool edits active during Play");editor.stop();frame();
    activate("Close tools");editor.open(path,true);frame();editor.session().select(savedId);
    require(editor.session().beginTransform(),"Cannot prepare close cancellation");auto preview=editor.session().selected()->transform;preview.position.x+=1;require(editor.session().previewTransform(preview),"Cannot preview close cancellation");host.requestClose();require(!editor.step(time+=1./60),"Native editor did not close");
    std::ofstream report(evidence/"report.json");report<<"{\"passed\":true,\"nativeDawn\":true,\"guiTransform\":true,\"invalidPreserved\":true,\"history\":true,\"archiveRoundTrip\":true,\"isolatedPlay\":true,\"nativeMouse\":true,\"specializedTools\":true,\"frames\":"<<d.presentedFrames<<",\"renderState\":"<<d.renderState<<"}\n";
    std::cout<<"Native editor: GUI edits, history, invalid input, duplicate, files, isolated Play, replacement, resize, SDL mouse and Dawn framebuffer passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
