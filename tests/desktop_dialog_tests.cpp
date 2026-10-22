#include "engine/desktop.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace engine;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Action {bool cancel;std::atomic<bool> found=false,done=false;DWORD thread;};
BOOL CALLBACK visit(HWND window,LPARAM data) {
    auto& action=*reinterpret_cast<Action*>(data);wchar_t kind[128]{};GetClassNameW(window,kind,128);
    if(std::wstring_view(kind)==L"#32770"&&IsWindowVisible(window)) {action.found=true;PostMessageW(window,WM_COMMAND,action.cancel?IDCANCEL:IDOK,0);}
    return TRUE;
}
DesktopFileDialogResult choose(DesktopHost& host,const DesktopFileDialogOptions& options,bool cancel) {
    Action action{cancel,false,false,GetCurrentThreadId()};
    std::jthread driver([&]{const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!action.done){if(std::chrono::steady_clock::now()>deadline)action.cancel=true;EnumThreadWindows(action.thread,visit,reinterpret_cast<LPARAM>(&action));std::this_thread::sleep_for(std::chrono::milliseconds(100));}});
    auto result=host.showFileDialog(options);action.done=true;driver.join();
    if(!action.found)throw std::runtime_error("Native file dialog was not observed: "+result.error+" status "+std::to_string(int(result.status)));
    require(host.application().suspended()==false,"Dialog retained suspension");return result;
}
}
int main(){try {
    DesktopSettings settings;settings.hidden=true;DesktopHost host(settings);
    auto& focused=host.gui().root().add<TextInput>();focused.setSize({240,38});host.gui().layout({640,480});host.gui().focusWidget(focused.id());
    DesktopFileDialogOptions invalid;invalid.filters={{"Invalid","../bad"}};bool rejected=false;
    try{host.showFileDialog(invalid);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&host.gui().focused()==&focused&&!host.application().suspended(),"Invalid dialog changed focus/clocks");
    const auto path=std::filesystem::path(DESKTOP_TEST_DIR)/L"dialog-\u00e5.weapp";const std::array<std::byte,1> bytes{std::byte{3}};writeDesktopFileAtomic(path,bytes);
    DesktopFileDialogOptions options;options.title="WebEngine native dialog acceptance";options.initialPath=path;options.filters={{"Application archives","weapp"}};options.defaultExtension="weapp";
    auto opened=choose(host,options,false);require(opened.status==DesktopDialogStatus::Selected&&opened.path==path,"Native open did not return Unicode fixture path");
    auto cancelled=choose(host,options,true);require(cancelled.status==DesktopDialogStatus::Cancelled&&cancelled.path.empty(),"Native cancel returned a path");
    options.kind=DesktopDialogKind::Save;options.initialPath=path.parent_path()/"new-dialog-output";
    auto saved=choose(host,options,false);require(saved.status==DesktopDialogStatus::Selected&&saved.path.extension()==".weapp","Native save did not apply extension");
    std::cout<<"Native Windows open, save and cancellation passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
