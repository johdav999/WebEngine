#include "engine/desktop_runtime.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
using namespace engine;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void child(const std::filesystem::path& directory,bool terminate){
    const auto executable=desktopExecutablePath();std::wstring command=L"\""+executable.native()+L"\" "+(terminate?L"--terminate":L"--seh")+L" \""+directory.native()+L"\"";
    STARTUPINFOW info{sizeof(info)};PROCESS_INFORMATION process{};check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&info,&process),"Cannot create crash probe");
    CloseHandle(process.hThread);const auto waited=WaitForSingleObject(process.hProcess,20000);DWORD exit=0;GetExitCodeProcess(process.hProcess,&exit);CloseHandle(process.hProcess);check(waited==WAIT_OBJECT_0&&exit!=0,"Crash probe failed to exit with error");
}
}
int main(int argc,char** argv){try{
    if(argc==3&&(std::string(argv[1])=="--seh"||std::string(argv[1])=="--terminate")){
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);DesktopLogSettings settings;settings.directory=std::filesystem::path(argv[2]);DesktopSessionLog log(settings);log.write(DesktopLogLevel::Info,"Crash fixture reached");
        if(std::string(argv[1])=="--terminate")std::terminate();RaiseException(0xe0100001,EXCEPTION_NONCONTINUABLE,0,nullptr);return 99;
    }
    check(argc==2,"Expected diagnostic fixture directory");const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
    DesktopLogSettings settings;settings.directory=root/"logs";settings.retainedLogs=3;settings.retainedDumps=2;settings.redirectStandardStreams=true;
    std::filesystem::path last;
    for(int i=0;i<5;++i){DesktopSessionLog log(settings);last=log.file();bool rejected=false;try{DesktopSessionLog duplicate(settings);}catch(const std::logic_error&){rejected=true;}check(rejected,"Concurrent diagnostic session accepted");log.write(DesktopLogLevel::Error,"Expected logged error");std::fprintf(stderr,"Captured stderr fixture\n");std::fflush(stderr);}
    const auto text=read(last);check(text.find("Expected logged error")!=text.npos&&text.find("Captured stderr fixture")!=text.npos&&text.find("session closed")!=text.npos,"Session log/stdio capture incomplete");
    unsigned logs=0;for(const auto& file:std::filesystem::directory_iterator(settings.directory))if(file.path().extension()==".log")++logs;check(logs<=3,"Diagnostic retention exceeded");
    const auto package=root/"package";std::filesystem::create_directories(package);std::ofstream(package/"hello.txt")<<"hello";
    auto manifest=[&](std::string_view name){std::ofstream out(package/"package.manifest",std::ios::binary);out<<"WEBENGINE_PACKAGE 1\nversion\t0.1.0\n2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\t5\t"<<name<<'\n';};
    manifest("hello.txt");check(inspectDesktopPackage(package).ready()&&inspectDesktopPackage(package).verifiedFiles==1,"Valid package did not verify");
    std::ofstream(package/"hello.txt")<<"other";check(!inspectDesktopPackage(package).ready(),"Corrupt file accepted");
    manifest("../outside.txt");check(!inspectDesktopPackage(package).ready(),"Escaping package path accepted");
    child(root/"seh",false);child(root/"terminate",true);
    for(const auto& directory:{root/"seh",root/"terminate"}){
        bool fatal=false,dump=false;for(const auto& file:std::filesystem::directory_iterator(directory)){
            if(file.path().extension()==".log")fatal|=read(file.path()).find("FATAL")!=std::string::npos;
            if(file.path().extension()==".dmp")dump|=read(file.path()).starts_with("MDMP");
        }check(fatal&&dump,"Fatal exception did not produce a session log and valid minidump");
    }
    std::cout<<"Desktop runtime logs, retention, integrity and SEH/terminate dump tests passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
