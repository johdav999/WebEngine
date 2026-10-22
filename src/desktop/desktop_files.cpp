#include "desktop_files.hpp"
#include "gui_utf8.hpp"
#include <SDL3/SDL.h>
#include <windows.h>
#include <commdlg.h>
#include <atomic>
#include <fstream>
#include <system_error>

namespace engine {
namespace {
void budget(std::size_t value){if(!value||value>desktopFileLimit)throw std::invalid_argument("Invalid desktop file budget");}
std::filesystem::path utf8Path(const char* value){const std::string s=value?value:"";return std::filesystem::path(std::u8string(s.begin(),s.end()));}
std::wstring wide(std::string_view value) {
    gui_detail::validate(value);if(value.find('\0')!=value.npos)throw std::invalid_argument("Embedded zero in desktop text");
    if(value.empty())return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    std::wstring result(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),result.data(),count);return result;
}
void identity(std::string_view value) {
    if(value.empty()||value.size()>128)throw std::invalid_argument("Invalid desktop application identity");
    for(unsigned char c:value)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))throw std::invalid_argument("Invalid desktop application identity");
}
bool same(const std::filesystem::path& a,const std::filesystem::path& b) {
    return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;
}
std::filesystem::path resolve(const std::filesystem::path& root,const std::filesystem::path& relative) {
    if(relative.empty()||relative.has_root_path())throw std::invalid_argument("Expected a relative desktop file path");
    for(const auto& part:relative) {
        auto value=part.native();
        if(value.empty()||value==L"."||value==L".."||value.back()==L'.'||value.back()==L' ')throw std::invalid_argument("Invalid desktop path component");
        for(auto c:value)if(c<32||std::wstring_view(L":*?\"<>|").find(c)!=std::wstring_view::npos)throw std::invalid_argument("Invalid desktop path component");
        auto stem=value.substr(0,value.find(L'.'));for(auto& c:stem)if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';
        if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL"||stem==L"CONIN$"||stem==L"CONOUT$"||
            (stem.size()==4&&(stem.substr(0,3)==L"COM"||stem.substr(0,3)==L"LPT")&&
                ((stem[3]>=L'1'&&stem[3]<=L'9')||stem[3]==L'\u00b9'||stem[3]==L'\u00b2'||stem[3]==L'\u00b3')))
            throw std::invalid_argument("Reserved Windows file name");
    }
    const auto base=std::filesystem::weakly_canonical(root),candidate=std::filesystem::weakly_canonical(base/relative);
    auto at=candidate.begin();for(const auto& part:base){if(at==candidate.end()||!same(part,*at++))throw std::invalid_argument("Desktop path escapes its directory");}
    if(at==candidate.end())throw std::invalid_argument("Expected a file inside the desktop directory");return candidate;
}
void winError(const char* action){throw std::system_error(int(GetLastError()),std::system_category(),action);}
}
std::vector<std::byte> readDesktopFile(const std::filesystem::path& path,std::size_t maxBytes) {
    budget(maxBytes);if(path.empty()||path.native().find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Invalid desktop file path");
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Could not open desktop file");
    const auto size=file.tellg();if(size<0||std::uint64_t(size)>maxBytes)throw std::runtime_error("Desktop file exceeds byte budget");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));file.seekg(0);
    if(!bytes.empty()&&!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Could not read complete desktop file");
    if(file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Desktop file changed during read");return bytes;
}
void writeDesktopFileAtomic(const std::filesystem::path& path,std::span<const std::byte> bytes,std::size_t maxBytes) {
    budget(maxBytes);if(bytes.size()>maxBytes)throw std::invalid_argument("Desktop write exceeds byte budget");
    if(path.empty()||path.native().find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Invalid desktop file path");
    const auto destination=std::filesystem::absolute(path);std::filesystem::create_directories(destination.parent_path());
    static std::atomic<std::uint64_t> serial{0};std::filesystem::path temporary;HANDLE file=INVALID_HANDLE_VALUE;
    for(int i=0;i<32&&file==INVALID_HANDLE_VALUE;++i) {
        temporary=destination;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);
        file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE&&GetLastError()!=ERROR_FILE_EXISTS)winError("Create desktop temporary file");
    }
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Could not reserve desktop temporary file");
    try {
        for(std::size_t at=0;at<bytes.size();) {
            const auto count=DWORD(std::min<std::size_t>(bytes.size()-at,1024*1024));DWORD written=0;
            if(!WriteFile(file,bytes.data()+at,count,&written,nullptr)||written!=count)winError("Write desktop file");at+=written;
        }
        if(!FlushFileBuffers(file))winError("Flush desktop file");
        if(!CloseHandle(file)){file=INVALID_HANDLE_VALUE;winError("Close desktop file");}file=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))winError("Replace desktop file");
    }catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);DeleteFileW(temporary.c_str());throw;}
}
DesktopFiles::DesktopFiles(const DesktopFileSettings& settings) {
    identity(settings.organization);identity(settings.application);
    const auto base=SDL_GetBasePath();if(!base)throw std::runtime_error(SDL_GetError());
    assets_=std::filesystem::weakly_canonical(settings.assetDirectory.empty()?utf8Path(base)/"assets":std::filesystem::absolute(settings.assetDirectory));
    auto user=settings.userDirectory;
    if(user.empty()) {
        const std::unique_ptr<char,decltype(&SDL_free)> pref(SDL_GetPrefPath(settings.organization.c_str(),settings.application.c_str()),SDL_free);
        if(!pref)throw std::runtime_error(SDL_GetError());user=utf8Path(pref.get());
    }
    user=std::filesystem::weakly_canonical(std::filesystem::absolute(user));saves_=user/"saves";settings_=user/"settings";
    std::filesystem::create_directories(saves_);std::filesystem::create_directories(settings_);
}
std::filesystem::path DesktopFiles::assetPath(const std::filesystem::path& path) const{return resolve(assets_,path);}
std::filesystem::path DesktopFiles::userPath(DesktopStorage area,const std::filesystem::path& path) const {
    if(area!=DesktopStorage::Saves&&area!=DesktopStorage::Settings)throw std::invalid_argument("Invalid desktop storage area");
    return resolve(area==DesktopStorage::Saves?saves_:settings_,path);
}
std::vector<std::byte> DesktopFiles::readAsset(const std::filesystem::path& p,std::size_t limit) const{return readDesktopFile(assetPath(p),limit);}
std::vector<std::byte> DesktopFiles::readUser(DesktopStorage s,const std::filesystem::path& p,std::size_t limit) const{return readDesktopFile(userPath(s,p),limit);}
void DesktopFiles::writeUser(DesktopStorage s,const std::filesystem::path& p,std::span<const std::byte> bytes,std::size_t limit) const {writeDesktopFileAtomic(userPath(s,p),bytes,limit);}
DesktopFileDialogResult showWindowsFileDialog(void* owner,const DesktopFileDialogOptions& options,const std::function<void()>& beforeShow) {
    if(options.kind!=DesktopDialogKind::Open&&options.kind!=DesktopDialogKind::Save)throw std::invalid_argument("Invalid desktop dialog kind");
    if(options.title.size()>1024||options.defaultExtension.size()>32||options.filters.size()>32)throw std::invalid_argument("Desktop dialog options exceed limits");
    const auto title=wide(options.title),extension=wide(options.defaultExtension);std::wstring filter;
    for(auto c:extension)if(!((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')))throw std::invalid_argument("Invalid default extension");
    for(const auto& item:options.filters) {
        if(item.name.empty()||item.name.size()>128||item.extensions.empty()||item.extensions.size()>256)throw std::invalid_argument("Invalid dialog filter");
        filter+=wide(item.name);filter.push_back(0);std::wstring patterns;
        if(item.extensions=="*")patterns=L"*.*";
        else {std::size_t at=0;while(at<item.extensions.size()) {
            const auto end=item.extensions.find(';',at);const auto part=item.extensions.substr(at,end==std::string::npos?end:end-at);
            if(part.empty())throw std::invalid_argument("Empty dialog extension");
            for(unsigned char c:part)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))throw std::invalid_argument("Invalid dialog extension");
            if(!patterns.empty())patterns+=L';';patterns+=L"*."+wide(part);if(end==std::string::npos)break;at=end+1;
            if(at==item.extensions.size())throw std::invalid_argument("Empty dialog extension");
        }}filter+=patterns;filter.push_back(0);
    }
    if(filter.empty()){filter=L"All files";filter.push_back(0);filter+=L"*.*";filter.push_back(0);}filter.push_back(0);
    std::vector<wchar_t> selected(32768,0);auto normalized=options.initialPath;
    if(!normalized.empty())normalized=std::filesystem::absolute(normalized).make_preferred();
    const auto initial=normalized.native();
    if(initial.size()>=selected.size()||initial.find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Invalid initial dialog path");
    const bool directory=!normalized.empty()&&std::filesystem::is_directory(normalized);
    if(!directory)std::copy(initial.begin(),initial.end(),selected.begin());
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=static_cast<HWND>(owner);dialog.lpstrTitle=title.c_str();
    dialog.lpstrFilter=filter.c_str();dialog.lpstrFile=selected.data();dialog.nMaxFile=DWORD(selected.size());dialog.lpstrDefExt=extension.empty()?nullptr:extension.c_str();
    dialog.lpstrInitialDir=directory?initial.c_str():nullptr;
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_DONTADDTORECENT|(options.kind==DesktopDialogKind::Open?OFN_FILEMUSTEXIST:OFN_OVERWRITEPROMPT);
    beforeShow(); // Validate the complete request before changing host input/clocks.
    const BOOL chosen=options.kind==DesktopDialogKind::Open?GetOpenFileNameW(&dialog):GetSaveFileNameW(&dialog);
    if(chosen)return {DesktopDialogStatus::Selected,std::filesystem::path(selected.data()),{}};
    const DWORD error=CommDlgExtendedError();if(error)return {DesktopDialogStatus::Failed,{},"Windows file dialog error "+std::to_string(error)};return {};
}
}
