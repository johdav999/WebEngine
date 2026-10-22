#include "engine/desktop_runtime.hpp"
#include <windows.h>
#include <shlobj.h>
#include <dbghelp.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#include <mutex>
#include <stdexcept>

namespace engine {
namespace {
std::atomic<void*> activeSession=nullptr;
std::atomic_flag sessionOwned=ATOMIC_FLAG_INIT;
std::atomic<unsigned> sessionSequence=0;
std::wstring wide(std::string_view text){if(text.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);if(!n)throw std::invalid_argument("Invalid UTF-8 text");std::wstring s(n,0);MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),s.data(),n);return s;}
void validId(std::string_view value){if(value.empty()||value.size()>64||value.front()=='.'||value.back()=='.'||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=value.npos)throw std::invalid_argument("Invalid diagnostic application identifier");}
std::filesystem::path defaultDirectory(const DesktopLogSettings& settings){
    PWSTR path=nullptr;const auto status=SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&path);
    if(FAILED(status))throw std::runtime_error("Cannot locate per-user diagnostics directory");
    std::filesystem::path root(path);CoTaskMemFree(path);return root/wide(settings.organization)/wide(settings.application)/L"logs";
}
void retain(const std::filesystem::path& directory,std::wstring_view extension,unsigned count){
    std::vector<std::filesystem::directory_entry> files;
    for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file()&&entry.path().filename().native().starts_with(L"session-")&&entry.path().extension()==extension)files.push_back(entry);
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.path().filename()<b.path().filename();});
    while(files.size()>=count){std::error_code error;std::filesystem::remove(files.front().path(),error);files.erase(files.begin());}
}
}
struct DesktopSessionLog::Impl {
    std::filesystem::path folder,path,dump;
    HANDLE handle=INVALID_HANDLE_VALUE;
    HMODULE dbghelp=nullptr;
    using DumpFunction=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
    DumpFunction writeDump=nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter=nullptr;
    std::terminate_handler previousTerminate=nullptr;
    int output=-1,error=-1;
    bool initializedOutput=false,initializedError=false;
    bool hooks=false,limited=false;
    std::atomic_flag crashing=ATOMIC_FLAG_INIT;
    std::mutex mutex;
    explicit Impl(const DesktopLogSettings& settings){
        validId(settings.organization);validId(settings.application);
        if(settings.directory.native().find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Invalid diagnostic directory");
        if(!settings.retainedLogs||settings.retainedLogs>64||!settings.retainedDumps||settings.retainedDumps>16)throw std::invalid_argument("Invalid diagnostic retention limits");
        folder=std::filesystem::absolute(settings.directory.empty()?defaultDirectory(settings):settings.directory);
        std::filesystem::create_directories(folder);retain(folder,L".log",settings.retainedLogs);retain(folder,L".dmp",settings.retainedDumps);
        SYSTEMTIME time{};GetSystemTime(&time);wchar_t name[100];swprintf_s(name,L"session-%04u%02u%02uT%02u%02u%02u-%03u-%lu-%06u",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,GetCurrentProcessId(),++sessionSequence);
        path=folder/(std::wstring(name)+L".log");dump=folder/(std::wstring(name)+L".dmp");
        handle=CreateFileW(path.c_str(),FILE_APPEND_DATA|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create diagnostic session log");
        if(settings.crashDumps){dbghelp=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(dbghelp)writeDump=reinterpret_cast<DumpFunction>(GetProcAddress(dbghelp,"MiniDumpWriteDump"));}
        if(settings.redirectStandardStreams){
            std::fflush(nullptr);int descriptor=-1;
            if(!_wsopen_s(&descriptor,path.c_str(),_O_WRONLY|_O_APPEND|_O_BINARY,_SH_DENYNO,_S_IREAD|_S_IWRITE)){
                auto redirect=[&](FILE* stream,int& saved,bool& initialized){
                    if(_fileno(stream)>=0)saved=_dup(_fileno(stream));
                    else {FILE* opened=nullptr;initialized=_wfreopen_s(&opened,L"NUL",L"w",stream)==0;}
                    if(_fileno(stream)>=0)_dup2(descriptor,_fileno(stream));
                };
                redirect(stdout,output,initializedOutput);redirect(stderr,error,initializedError);_close(descriptor);
            }else append("WARNING standard stream redirection unavailable\r\n");
        }
        previousFilter=SetUnhandledExceptionFilter(exceptionFilter);previousTerminate=std::set_terminate(terminateHandler);hooks=true;
        append("INFO session started; timestamps are UTC; fatal dumps are best effort\r\n");
    }
    ~Impl(){
        if(hooks){SetUnhandledExceptionFilter(previousFilter);if(std::get_terminate()==terminateHandler)std::set_terminate(previousTerminate);}
        std::fflush(nullptr);if(output>=0){_dup2(output,_fileno(stdout));_close(output);}else if(initializedOutput)std::fclose(stdout);
        if(error>=0){_dup2(error,_fileno(stderr));_close(error);}else if(initializedError)std::fclose(stderr);
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);if(dbghelp)FreeLibrary(dbghelp);
    }
    void append(std::string_view text) noexcept {DWORD written=0;if(handle!=INVALID_HANDLE_VALUE){WriteFile(handle,text.data(),DWORD(text.size()),&written,nullptr);FlushFileBuffers(handle);}}
    void fatal(EXCEPTION_POINTERS* exception,const char* reason) noexcept {
        if(crashing.test_and_set())return;
        char message[240];const auto code=exception?exception->ExceptionRecord->ExceptionCode:0;
        const auto address=exception?exception->ExceptionRecord->ExceptionAddress:nullptr;
        const int n=snprintf(message,sizeof(message),"FATAL %s code=0x%08lx address=%p thread=%lu\r\n",reason,code,address,GetCurrentThreadId());
        if(n>0)append({message,std::min<std::size_t>(n,sizeof(message)-1)});
        if(writeDump){HANDLE file=CreateFileW(dump.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE){MINIDUMP_EXCEPTION_INFORMATION data{GetCurrentThreadId(),exception,FALSE};
                const bool ok=writeDump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,exception?&data:nullptr,nullptr,nullptr)!=FALSE;
                CloseHandle(file);append(ok?"FATAL minidump written beside session log\r\n":"FATAL minidump write failed\r\n");}}
    }
    static LONG WINAPI exceptionFilter(EXCEPTION_POINTERS* exception) noexcept {if(auto* self=static_cast<Impl*>(activeSession.load()))self->fatal(exception,"unhandled Windows exception");return EXCEPTION_EXECUTE_HANDLER;}
    static void terminateHandler() noexcept {if(auto* self=static_cast<Impl*>(activeSession.load()))self->fatal(nullptr,"unhandled C++ termination");ExitProcess(3);}
};
DesktopSessionLog::DesktopSessionLog(const DesktopLogSettings& settings){
    if(sessionOwned.test_and_set())throw std::logic_error("Only one desktop diagnostic session is supported");
    try{impl_=std::make_unique<Impl>(settings);activeSession=impl_.get();}catch(...){activeSession=nullptr;sessionOwned.clear();throw;}
}
DesktopSessionLog::~DesktopSessionLog(){if(impl_)impl_->append("INFO session closed\r\n");activeSession=nullptr;impl_.reset();sessionOwned.clear();}
const std::filesystem::path& DesktopSessionLog::file() const{return impl_->path;}
const std::filesystem::path& DesktopSessionLog::directory() const{return impl_->folder;}
void DesktopSessionLog::write(DesktopLogLevel level,std::string_view message) noexcept {
    try{std::lock_guard lock(impl_->mutex);LARGE_INTEGER size{};if(GetFileSizeEx(impl_->handle,&size)&&size.QuadPart>4*1024*1024){if(!impl_->limited){impl_->append("WARNING session log limit reached\r\n");impl_->limited=true;}return;}
        SYSTEMTIME time{};GetSystemTime(&time);char prefix[70];snprintf(prefix,sizeof(prefix),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %s ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,level==DesktopLogLevel::Error?"ERROR":level==DesktopLogLevel::Warning?"WARNING":"INFO");
        std::string entry(prefix);entry.append(message.substr(0,16384));entry+="\r\n";impl_->append(entry);
    }catch(...){}
}
std::filesystem::path desktopExecutablePath(){std::wstring path(32768,0);const auto n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));if(!n||n>=path.size())throw std::runtime_error("Cannot locate native executable");path.resize(n);return path;}
void showDesktopError(std::string_view title,std::string_view message,const std::filesystem::path& log) noexcept {
    try{auto text=wide(message);if(!log.empty())text+=L"\n\nSupport log:\n"+log.native();MessageBoxW(nullptr,text.c_str(),wide(title).c_str(),MB_OK|MB_ICONERROR|MB_SETFOREGROUND);}catch(...){}
}
}
