// Application distribution entry point. Intentionally /MT and independent of
// SDL/Dawn/VC redistributable DLLs so broken installations can display a useful error.
#include "engine/desktop_runtime.hpp"
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <sstream>

namespace {
std::wstring argument(std::wstring_view value){std::wstring s=L"\"";unsigned slashes=0;for(auto c:value){if(c==L'\\'){++slashes;continue;}if(c==L'"'){s.append(slashes*2+1,L'\\');s+=c;}else{s.append(slashes,L'\\');s+=c;}slashes=0;}s.append(slashes*2,L'\\');return s+L'"';}
std::string utf8(std::wstring_view text){int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),s.data(),n,nullptr,nullptr);return s;}
std::string escaped(std::string_view value){std::string s="\"";for(auto c:value){if(c=='"'||c=='\\')s+='\\';s+=c<' '?' ':c;}return s+'"';}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    std::unique_ptr<engine::DesktopSessionLog> log;bool quiet=false;
    try{
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Cannot read launch arguments");
        struct Arguments{LPWSTR* data;~Arguments(){LocalFree(data);}} cleanup{argv};
        engine::DesktopLogSettings settings;settings.application="Vehicle";std::filesystem::path reportPath;bool diagnose=false;std::vector<std::wstring> child;
        for(int i=1;i<argc;++i){std::wstring key=argv[i];if(key==L"--quiet")quiet=true;else if(key==L"--diagnose")diagnose=true;
            else if((key==L"--log-dir"||key==L"--report")&&i+1<argc){const std::filesystem::path path=argv[++i];if(key==L"--log-dir")settings.directory=path;else reportPath=path;}
            else child.push_back(key);}
        log=std::make_unique<engine::DesktopSessionLog>(settings);const auto root=engine::desktopExecutablePath().parent_path();
        log->write(engine::DesktopLogLevel::Info,"Launcher build " WEBENGINE_WINDOWS_VERSION "; distribution integrity check");
        auto report=engine::inspectDesktopPackage(root);
        using VersionFunction=LONG(WINAPI*)(OSVERSIONINFOW*);OSVERSIONINFOW version{sizeof(version)};
        auto getVersion=reinterpret_cast<VersionFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
        if(!getVersion||getVersion(&version)||version.dwMajorVersion<10||version.dwBuildNumber<19041)report.errors.push_back("Windows 10 build 19041 or newer (x64) is required");
        for(const auto* name:{L"webengine_windows.exe",L"SDL3.dll",L"dxcompiler.dll",L"dxil.dll",L"assets/fonts/NotoSans-Regular.ttf",L"assets/audio/preview.wav"})
            if(!std::filesystem::is_regular_file(root/name))report.errors.push_back("Required file missing: "+utf8(name));
        if(report.ready())for(const auto* name:{L"SDL3.dll",L"dxcompiler.dll",L"dxil.dll"}){
            const auto path=root/name;auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(!module)report.errors.push_back("Cannot load "+utf8(name)+" (Windows error "+std::to_string(GetLastError())+"); reinstall the complete x64 package");else FreeLibrary(module);
        }
        for(const auto& error:report.errors)log->write(engine::DesktopLogLevel::Error,error);
        if(!reportPath.empty()){
            std::ofstream out(reportPath);out<<"{\"ready\":"<<(report.ready()?"true":"false")<<",\"version\":"<<escaped(report.version)<<",\"verifiedFiles\":"<<report.verifiedFiles<<",\"windowsBuild\":"<<version.dwBuildNumber<<",\"errors\":[";
            for(std::size_t i=0;i<report.errors.size();++i){if(i)out<<',';out<<escaped(report.errors[i]);}out<<"]}\n";if(!out)throw std::runtime_error("Cannot write dependency report");
        }
        if(!report.ready()){std::string errors="This installation is incomplete or unsupported.\n";for(const auto& error:report.errors)errors+="\n"+error;
            if(!quiet)engine::showDesktopError("WebEngine could not start",errors,log->file());return 2;}
        log->write(engine::DesktopLogLevel::Info,"Package "+report.version+"; Windows build "+std::to_string(version.dwBuildNumber));
        log->write(engine::DesktopLogLevel::Info,"Verified "+std::to_string(report.verifiedFiles)+" package files and native loader dependencies");
        if(diagnose)return 0;
        const auto runtime=root/L"webengine_windows.exe";std::wstring command=argument(runtime.native());
        for(const auto& arg:child)command+=L" "+argument(arg);command+=L" --log-dir "+argument(log->directory().native());
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
        if(!CreateProcessW(runtime.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process))throw std::runtime_error("Windows could not launch the runtime (error "+std::to_string(GetLastError())+")");
        CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);DWORD exit=1;GetExitCodeProcess(process.hProcess,&exit);CloseHandle(process.hProcess);
        std::ostringstream code;code<<"0x"<<std::hex<<exit;std::string detail="The runtime returned "+code.str()+". ";
        if(exit==0xc0000135)detail+="A dependent DLL is missing. Reinstall the complete x64 ZIP. ";
        else if(exit==0xc0000139)detail+="A DLL has an incompatible version or missing entry point. Reinstall the complete package. ";
        else if(exit==0xc000007b)detail+="A DLL has the wrong architecture or is damaged. Use the complete x64 package. ";
        log->write(exit?engine::DesktopLogLevel::Error:engine::DesktopLogLevel::Info,detail);
        if(exit&&!quiet)engine::showDesktopError("WebEngine stopped unexpectedly",detail+"Session logs and any crash dump are in the support log directory.",log->file());return int(exit);
    }catch(const std::exception& error){if(log)log->write(engine::DesktopLogLevel::Error,error.what());if(!quiet)engine::showDesktopError("WebEngine startup error",error.what(),log?log->file():std::filesystem::path{});return 1;}
}
